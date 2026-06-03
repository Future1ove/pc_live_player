#ifndef STREAMWORKER_HPP
#define STREAMWORKER_HPP

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include <QImage>
#include <QString>
#include <QThread>

extern "C" {
#include "app_types.h"
typedef struct FFmpegStreamFetcher FFmpegStreamFetcher;
}


class StreamWorker final : public QThread {
    Q_OBJECT

public:
    explicit StreamWorker(QObject *parent = nullptr);

    void setConfig(const AppConfig &config);
    void requestStop();

signals:
    void frameReady(const QImage &image);
    void qrDetected(const QString &text);
    void statusMessage(const QString &text);
    void errorOccurred(const QString &text);

protected:
    void run() override;

private:
    void qrThreadMain();

    AppConfig m_config{};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_qrThreadStop{false};
    FFmpegStreamFetcher *m_fetcher{nullptr};

    std::mutex m_qrMutex;
    std::vector<uint8_t> m_qrSnapshot;
    std::atomic<uint64_t> m_qrFrameVersion{0};
    std::atomic<int> m_canvasW{0};
    std::atomic<int> m_canvasH{0};
    std::thread m_qrThread;
};

#endif
