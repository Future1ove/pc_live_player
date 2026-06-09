#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QDateTime>
#include <QImage>
#include <QMainWindow>
#include <QPointer>
#include <QString>

class QCloseEvent;
class QShowEvent;
class QLabel;
class QTimer;
class QTextEdit;
class UrlInputEdit;
class QPushButton;
class QCheckBox;
class QVBoxLayout;
class QWidget;
class StreamWorker;
class QRPopupDialog;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    /** 许可证或心跳失效时停止拉流（与「停止检测」相同，供外部调用）。 */
    void stopStreamingForLicense();

public slots:
    void setKeyExpiry(const QDateTime &expiresAt);
    void clearKeyExpiry();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void startDetection();
    void stopDetection();
    void toggleFullscreen();
    void clearHistory();
    void onFrameReady(const QImage &image);
    void onQrDetected(const QString &text);
    void onError(const QString &text);
    void onStatus(const QString &text);
    void resetQrHighlight();
    void tickKeyExpiryDisplay();

private:
    void applyInitialWindowGeometry();
    void enterFullscreen();
    void exitFullscreen();
    void showQrPopup(const QString &text);
    /** 合并同一事件循环内多帧，只刷新最后一次，减轻偶发卡顿 */
    void flushPendingVideoFrame();

    QLabel *m_videoLabel{nullptr};
    QLabel *m_statusLabel{nullptr};
    QLabel *m_keyExpiryLabel{nullptr};
    QTimer *m_keyExpiryTimer{nullptr};
    QDateTime m_keyExpiresAt;
    UrlInputEdit *m_urlInput{nullptr};
    QCheckBox *m_fullResCheck{nullptr};
    QPushButton *m_startBtn{nullptr};
    QPushButton *m_fullscreenBtn{nullptr};
    QPushButton *m_stopBtn{nullptr};
    QLabel *m_currentQrLabel{nullptr};
    QLabel *m_qrCountLabel{nullptr};
    QTextEdit *m_historyText{nullptr};
    QVBoxLayout *m_leftLayout{nullptr};
    QWidget *m_leftWidget{nullptr};
    StreamWorker *m_worker{nullptr};
    QMainWindow *m_fullscreenWindow{nullptr};
    QPointer<QRPopupDialog> m_qrPopup;
    QString m_lastQr;
    int m_qrCount{0};
    bool m_isFullscreen{false};

    QImage m_pendingVideoFrame;
    bool m_videoFlushScheduled{false};
};

#endif
