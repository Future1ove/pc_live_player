#include "StreamWorker.hpp"

#include <cstdlib>
#include <cstring>

#include <SDL.h>

extern "C" {
#include "ffmpeg_stream.h"
}

/* qr_detector.h 会间接包含 ZBar（含 C++），不可放在 extern "C" 中 */
#include "qr_detector.h"

namespace {
constexpr int kMaxCanvasW = 7680;
constexpr int kMaxCanvasH = 4320;
constexpr size_t kMaxFrameBytes = static_cast<size_t>(kMaxCanvasW) * static_cast<size_t>(kMaxCanvasH) * 3U;
/** 视频相对音频主时钟允许的最大超前（秒） */
constexpr double kAvSyncMaxVideoAheadSec = 0.06;
/** 视频明显落后时强制刷新画面 */
constexpr double kAvSyncForceDisplayBehindSec = 0.15;
/** 等音频追上时的最长自旋（毫秒量级步进） */
constexpr int kAvSyncSpinMax = 80;

void waitAvSyncThenRefresh(
    FFmpegStreamFetcher *fetcher,
    uint8_t *frame_copy,
    size_t copy_buffer_bytes,
    uint64_t *sequence,
    bool enable_audio) {
    if (!enable_audio || fetcher == nullptr || sequence == nullptr) {
        return;
    }

    for (int i = 0; i < kAvSyncSpinMax; ++i) {
        bool audio_valid = false;
        bool video_valid = false;
        const double audio_pts = ffmpeg_stream_get_audio_playback_pts(fetcher, &audio_valid);
        const double video_pts = ffmpeg_stream_get_latest_video_pts(fetcher, &video_valid);
        if (!audio_valid || !video_valid) {
            return;
        }
        if (video_pts <= audio_pts + kAvSyncMaxVideoAheadSec ||
            video_pts < audio_pts - kAvSyncForceDisplayBehindSec) {
            return;
        }
        QThread::usleep(1000);
        ffmpeg_stream_get_latest_frame(fetcher, frame_copy, copy_buffer_bytes, sequence);
    }
}

} /* namespace */

StreamWorker::StreamWorker(QObject *parent) : QThread(parent) {}

void StreamWorker::setConfig(const AppConfig &config) {
    m_config = config;
}

void StreamWorker::setQrEnabled(bool enabled) {
    m_qrEnabled.store(enabled, std::memory_order_release);
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
    /* 始终使用全图识别模式 */
    if (!qr_detector_init(&detector, QR_DETECTOR_MODE_FULL_RES, m_config.roi_ratio)) {
        emit errorOccurred(QStringLiteral("二维码检测器初始化失败"));
        return;
    }

    const int w = m_canvasW.load(std::memory_order_acquire);
    const int h = m_canvasH.load(std::memory_order_acquire);
    const size_t frame_bytes = (size_t)w * (size_t)h * 3U;
    std::vector<uint8_t> local(frame_bytes);
    uint64_t lastVer = 0U;

    while (!m_qrThreadStop.load(std::memory_order_acquire)) {
        /* 未启用时跳过检测，避免空转浪费 CPU */
        if (!m_qrEnabled.load(std::memory_order_acquire)) {
            QThread::msleep(50);
            lastVer = m_qrFrameVersion.load(std::memory_order_acquire);
            continue;
        }

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

            waitAvSyncThenRefresh(
                &fetcher,
                frame_copy,
                copy_buffer_bytes,
                &current_sequence,
                m_config.enable_audio
            );

            QImage img(
                frame_copy,
                canvasW,
                canvasH,
                canvasW * 3,
                QImage::Format_BGR888
            );
            emit frameReady(img.copy());
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
