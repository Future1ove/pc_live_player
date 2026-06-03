#include "StreamWorker.hpp"

#include <cstdlib>
#include <cstring>

#include <SDL.h>

#include <QDateTime>

extern "C" {
#include "ffmpeg_stream.h"
}

/* qr_detector.h 会间接包含 ZBar（含 C++），不可放在 extern "C" 中 */
#include "qr_detector.h"

namespace {
constexpr int kMaxCanvasW = 3840;
constexpr int kMaxCanvasH = 2160;
constexpr size_t kMaxFrameBytes = static_cast<size_t>(kMaxCanvasW) * static_cast<size_t>(kMaxCanvasH) * 3U;
} /* namespace */

StreamWorker::StreamWorker(QObject *parent) : QThread(parent) {}

void StreamWorker::setConfig(const AppConfig &config) {
    m_config = config;
}

void StreamWorker::requestStop() {
    /* 禁止在 GUI 线程调用 ffmpeg_stream_stop：会阻塞界面直至解码线程结束，表现为卡死。
     * 结束拉流只在 run() 收尾由工作线程执行 ffmpeg_stream_destroy。 */
    m_stop.store(true);
    m_qrThreadStop.store(true);
}

void StreamWorker::qrThreadMain() {
    while (!m_qrThreadStop.load(std::memory_order_acquire) && m_canvasW.load(std::memory_order_acquire) <= 0) {
        QThread::msleep(5);
    }
    if (m_qrThreadStop.load(std::memory_order_acquire)) {
        return;
    }

    QRDetector detector{};
    if (!qr_detector_init(&detector, m_config.detector_mode, m_config.roi_ratio)) {
        emit errorOccurred(QStringLiteral("二维码检测器初始化失败"));
        return;
    }

    const int w = m_canvasW.load(std::memory_order_acquire);
    const int h = m_canvasH.load(std::memory_order_acquire);
    const size_t frame_bytes = (size_t)w * (size_t)h * 3U;
    std::vector<uint8_t> local(frame_bytes);
    uint64_t lastVer = 0U;

    while (!m_qrThreadStop.load(std::memory_order_acquire)) {
        const uint64_t ver = m_qrFrameVersion.load(std::memory_order_acquire);
        if (ver == lastVer) {
            QThread::msleep(1);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m_qrMutex);
            if (m_qrSnapshot.size() != frame_bytes) {
                lastVer = m_qrFrameVersion.load(std::memory_order_relaxed);
                continue;
            }
            std::memcpy(local.data(), m_qrSnapshot.data(), frame_bytes);
            lastVer = m_qrFrameVersion.load(std::memory_order_relaxed);
        }

        QRCodeResult result{};
        if (qr_detector_detect(&detector, local.data(), w, h, w * 3, &result)) {
            if (result.found && result.data[0] != '\0') {
                emit qrDetected(QString::fromUtf8(result.data));
            }
        }
    }

    qr_detector_destroy(&detector);
}

void StreamWorker::run() {
    m_stop.store(false);
    m_qrThreadStop.store(false);
    m_canvasW.store(0);
    m_canvasH.store(0);

    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        emit errorOccurred(QString::fromUtf8(SDL_GetError()));
        return;
    }

    FFmpegStreamFetcher fetcher{};
    uint8_t *frame_copy = nullptr;
    uint64_t current_sequence = 0U;
    qint64 last_emit_ms = 0;
    unsigned qr_push_skip = 0U;

    m_fetcher = &fetcher;

    if (!ffmpeg_stream_init(&fetcher, &m_config)) {
        emit errorOccurred(QString::fromUtf8(fetcher.last_error));
        m_fetcher = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_TIMER);
        return;
    }

    const bool native = m_config.use_native_resolution;
    int canvasW = m_config.output_width;
    int canvasH = m_config.output_height;
    const size_t copy_buffer_bytes = native ? kMaxFrameBytes : (size_t)canvasW * (size_t)canvasH * 3U;

    frame_copy = (uint8_t *)std::malloc(copy_buffer_bytes);
    if (frame_copy == nullptr) {
        emit errorOccurred(QStringLiteral("帧缓冲分配失败"));
        ffmpeg_stream_destroy(&fetcher);
        m_fetcher = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_TIMER);
        return;
    }

    if (!ffmpeg_stream_start(&fetcher)) {
        emit errorOccurred(QStringLiteral("FFmpeg 拉流线程启动失败"));
        std::free(frame_copy);
        ffmpeg_stream_destroy(&fetcher);
        m_fetcher = nullptr;
        SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_TIMER);
        return;
    }

    if (!native) {
        m_canvasW.store(canvasW);
        m_canvasH.store(canvasH);
        m_qrSnapshot.resize((size_t)canvasW * (size_t)canvasH * 3U);
        m_qrThread = std::thread([this]() { qrThreadMain(); });
    }

    emit statusMessage(QStringLiteral("直播流连接成功，开始检测"));

    bool qr_thread_started = !native;

    while (!m_stop.load()) {
        if (ffmpeg_stream_get_latest_frame(&fetcher, frame_copy, copy_buffer_bytes, &current_sequence)) {
            if (native && !qr_thread_started) {
                ffmpeg_stream_get_canvas_size(&fetcher, &canvasW, &canvasH);
                if (canvasW > 0 && canvasH > 0) {
                    m_canvasW.store(canvasW);
                    m_canvasH.store(canvasH);
                    m_qrSnapshot.resize((size_t)canvasW * (size_t)canvasH * 3U);
                    m_qrThread = std::thread([this]() { qrThreadMain(); });
                    qr_thread_started = true;
                }
            }

            if (m_stop.load()) {
                break;
            }

            const size_t frame_bytes = (size_t)canvasW * (size_t)canvasH * 3U;

            qr_push_skip += 1U;
            if ((qr_push_skip & 1U) == 0U) {
                std::lock_guard<std::mutex> lock(m_qrMutex);
                std::memcpy(m_qrSnapshot.data(), frame_copy, frame_bytes);
                m_qrFrameVersion.fetch_add(1U, std::memory_order_release);
            }

            if (m_stop.load()) {
                break;
            }

            const qint64 now_emit = QDateTime::currentMSecsSinceEpoch();
            if (now_emit - last_emit_ms >= 33) {
                QImage img(
                    frame_copy,
                    canvasW,
                    canvasH,
                    canvasW * 3,
                    QImage::Format_BGR888
                );
                emit frameReady(img.copy());
                last_emit_ms = now_emit;
            }
        } else {
            QThread::msleep(1);
        }
    }

    m_qrThreadStop.store(true);
    if (m_qrThread.joinable()) {
        m_qrThread.join();
    }

    std::free(frame_copy);
    ffmpeg_stream_destroy(&fetcher);
    m_fetcher = nullptr;
    SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_TIMER);
}
