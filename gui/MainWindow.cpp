#include "MainWindow.hpp"

#include "QRPopupDialog.hpp"
#include "StreamProvider.hpp"
#include "StreamWorker.hpp"
#include "UrlInputEdit.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

#include <QApplication>
#include <QCloseEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QScrollArea>
#include <QShowEvent>
#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QTextEdit>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QSizePolicy>

extern "C" {
#include "app_config.h"
}

namespace {

QString mainWindowStylesheet() {
    return QStringLiteral(
        R"(
QMainWindow { background-color: #080a0e; }
QWidget#mainCentral { background-color: #080a0e; }
QWidget#leftPanel, QWidget#rightPanel { background-color: #080a0e; }
QScrollArea#rightScroll {
  background-color: #080a0e;
  border: none;
}
QScrollArea#rightScroll QWidget#scrollViewport {
  background-color: #080a0e;
  border: none;
}
QLabel#panelHeadline {
  color: #f1f5f9;
  font-size: 22px;
  font-weight: 700;
  letter-spacing: 0.02em;
}
QLabel#panelSubline {
  color: #64748b;
  font-size: 12px;
  margin-top: 2px;
}
QLabel#keyExpiryLabel {
  color: #5eead4;
  font-size: 12px;
  font-weight: 600;
  margin-top: 6px;
}
QLabel#sectionTitle {
  color: #64748b;
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.12em;
}
QLabel#videoStage {
  background-color: #020617;
  border: 1px solid rgba(212, 175, 55, 0.35);
  border-radius: 4px;
  color: #475569;
  font-size: 15px;
  padding: 2px;
}
QLabel#statusLabel {
  color: #94a3b8;
  font-size: 13px;
  padding: 10px 14px;
  background-color: rgba(15, 23, 42, 0.85);
  border: 1px solid rgba(148, 163, 184, 0.15);
  border-radius: 8px;
}
QLabel#qrCountLabel {
  color: #94a3b8;
  font-size: 13px;
  padding: 4px 2px;
}
QGroupBox {
  font-size: 13px;
  font-weight: 600;
  color: #cbd5e1;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 12px;
  margin-top: 16px;
  padding: 18px 14px 14px 14px;
  background-color: rgba(255, 255, 255, 0.03);
}
QGroupBox::title {
  subcontrol-origin: margin;
  subcontrol-position: top left;
  left: 14px;
  padding: 0 8px;
  color: #94a3b8;
}
QTextEdit {
  background-color: #0c1220;
  color: #e2e8f0;
  border: 1px solid rgba(148, 163, 184, 0.2);
  border-radius: 8px;
  padding: 10px;
  font-size: 13px;
  selection-background-color: #1d4ed8;
  selection-color: #f8fafc;
}
QTextEdit#historyLog {
  font-family: "Cascadia Mono", Consolas, "SF Mono", monospace;
  font-size: 12px;
}
QCheckBox { color: #cbd5e1; font-size: 13px; spacing: 8px; }
QCheckBox::indicator {
  width: 18px;
  height: 18px;
  border-radius: 4px;
  border: 1px solid rgba(148, 163, 184, 0.35);
  background-color: #0c1220;
}
QCheckBox::indicator:checked {
  background-color: #0d9488;
  border-color: #14b8a6;
}
QComboBox {
  background-color: #0c1220;
  color: #e2e8f0;
  border: 1px solid rgba(148, 163, 184, 0.2);
  border-radius: 8px;
  padding: 8px 12px;
  font-size: 13px;
  min-height: 22px;
}
QComboBox::drop-down { border: none; width: 28px; }
QComboBox QAbstractItemView {
  background-color: #111827;
  color: #e5e7eb;
  selection-background-color: #1e3a8a;
  border: 1px solid rgba(148, 163, 184, 0.25);
}
QPushButton#btnStart {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #10b981, stop:1 #059669);
  color: #f0fdf4;
  font-size: 14px;
  font-weight: 700;
  padding: 12px 16px;
  border-radius: 8px;
  border: 1px solid rgba(16, 185, 129, 0.45);
}
QPushButton#btnStart:hover {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #34d399, stop:1 #10b981);
}
QPushButton#btnStart:disabled { background: #334155; color: #64748b; border-color: #475569; }
QPushButton#btnFs {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #38bdf8, stop:1 #2563eb);
  color: #f0f9ff;
  font-size: 14px;
  font-weight: 700;
  padding: 12px 16px;
  border-radius: 8px;
  border: 1px solid rgba(56, 189, 248, 0.45);
}
QPushButton#btnFs:hover {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #7dd3fc, stop:1 #3b82f6);
}
QPushButton#btnFs:disabled { background: #334155; color: #64748b; border-color: #475569; }
QPushButton#btnStop {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #f87171, stop:1 #dc2626);
  color: #fff7ed;
  font-size: 14px;
  font-weight: 700;
  padding: 12px 16px;
  border-radius: 8px;
  border: 1px solid rgba(248, 113, 113, 0.45);
}
QPushButton#btnStop:hover {
  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #fca5a5, stop:1 #ef4444);
}
QPushButton#btnStop:disabled { background: #334155; color: #64748b; border-color: #475569; }
QPushButton#btnClear {
  background-color: rgba(217, 119, 6, 0.25);
  color: #fdba74;
  font-size: 13px;
  font-weight: 600;
  padding: 10px 14px;
  border-radius: 8px;
  border: 1px solid rgba(251, 191, 36, 0.35);
}
QPushButton#btnClear:hover {
  background-color: rgba(245, 158, 11, 0.35);
  color: #ffedd5;
}
QScrollBar:vertical {
  background: #0f172a;
  width: 10px;
  margin: 0;
  border-radius: 5px;
}
QScrollBar::handle:vertical {
  background: #334155;
  min-height: 28px;
  border-radius: 5px;
}
QScrollBar::handle:vertical:hover { background: #475569; }
)");
}

QString qrLabelStyleIdle() {
    return QStringLiteral(
        "QLabel#currentQrLabel {"
        "font-size: 15px; font-weight: 600; color: #5eead4;"
        "padding: 14px 16px;"
        "background-color: rgba(15, 23, 42, 0.9);"
        "border: 1px solid rgba(94, 234, 212, 0.25);"
        "border-radius: 10px;"
        "}"
    );
}

QString qrLabelStyleHighlight() {
    return QStringLiteral(
        "QLabel#currentQrLabel {"
        "font-size: 15px; font-weight: 700; color: #ecfdf5;"
        "padding: 14px 16px;"
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0d9488, stop:1 #047857);"
        "border: 1px solid rgba(45, 212, 191, 0.55);"
        "border-radius: 10px;"
        "}"
    );
}

QString videoLabelFullscreenStyle() {
    return QStringLiteral(
        "QLabel#videoStage {"
        "background-color: #000000;"
        "border: none;"
        "border-radius: 0px;"
        "color: #475569;"
        "padding: 0px;"
        "}"
    );
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("直播流二维码检测"));
    setMinimumSize(880, 560);

    setStyleSheet(mainWindowStylesheet());

    auto *mainWidget = new QWidget;
    mainWidget->setObjectName(QStringLiteral("mainCentral"));
    setCentralWidget(mainWidget);
    auto *root = new QHBoxLayout(mainWidget);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    m_leftWidget = new QWidget;
    m_leftWidget->setObjectName(QStringLiteral("leftPanel"));
    m_leftWidget->setAttribute(Qt::WA_StyledBackground, true);
    m_leftWidget->setMinimumWidth(260);
    m_leftLayout = new QVBoxLayout(m_leftWidget);
    m_leftLayout->setContentsMargins(0, 0, 0, 0);
    m_leftLayout->setSpacing(8);

    auto *stageTitle = new QLabel(QStringLiteral("实时画面"));
    stageTitle->setObjectName(QStringLiteral("sectionTitle"));
    m_leftLayout->addWidget(stageTitle);

    m_videoLabel = new QLabel;
    m_videoLabel->setObjectName(QStringLiteral("videoStage"));
    m_videoLabel->setMinimumSize(320, 180);
    m_videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setText(QStringLiteral("等待连接…"));
    m_leftLayout->addWidget(m_videoLabel, 1);

    m_statusLabel = new QLabel(QStringLiteral("状态 · 等待开始"));
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_leftLayout->addWidget(m_statusLabel);

    /* 左栏预览为主：stretch 大于右侧控制栏，横屏流少留白、画面更大 */
    root->addWidget(m_leftWidget, 5);

    auto *rightScroll = new QScrollArea;
    rightScroll->setObjectName(QStringLiteral("rightScroll"));
    rightScroll->setAttribute(Qt::WA_StyledBackground, true);
    rightScroll->setWidgetResizable(true);
    rightScroll->setFrameShape(QFrame::NoFrame);
    rightScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightScroll->setMinimumWidth(300);
    rightScroll->viewport()->setObjectName(QStringLiteral("scrollViewport"));
    rightScroll->viewport()->setAttribute(Qt::WA_StyledBackground, true);

    auto *rightPanel = new QWidget;
    rightPanel->setObjectName(QStringLiteral("rightPanel"));
    rightPanel->setAttribute(Qt::WA_StyledBackground, true);
    auto *right = new QVBoxLayout(rightPanel);
    right->setSpacing(14);
    right->setContentsMargins(0, 0, 0, 0);

    auto *rightHeader = new QWidget;
    auto *rhLay = new QVBoxLayout(rightHeader);
    rhLay->setContentsMargins(0, 2, 0, 8);
    rhLay->setSpacing(4);
    auto *panelHeadline = new QLabel(QStringLiteral("控制中心"));
    panelHeadline->setObjectName(QStringLiteral("panelHeadline"));
    auto *panelSub = new QLabel(QStringLiteral("清晰预览 · 扫码识别 · 历史可追溯"));
    panelSub->setObjectName(QStringLiteral("panelSubline"));
    rhLay->addWidget(panelHeadline);
    rhLay->addWidget(panelSub);
    m_keyExpiryLabel = new QLabel(QStringLiteral("密钥到期：--"));
    m_keyExpiryLabel->setObjectName(QStringLiteral("keyExpiryLabel"));
    m_keyExpiryLabel->setWordWrap(true);
    rhLay->addWidget(m_keyExpiryLabel);
    right->addWidget(rightHeader);

    auto *inputGroup = new QGroupBox(QStringLiteral("直播间"));
    auto *inputLay = new QVBoxLayout(inputGroup);
    inputLay->setSpacing(10);

    m_urlInput = new UrlInputEdit;
    m_urlInput->setPlaceholderText(
        QStringLiteral("粘贴抖音/小红书直播间链接，或 v.douyin.com 短链…")
    );
    m_urlInput->setMinimumHeight(qMax(64, fontMetrics().height() * 4 + 12));
    m_urlInput->setMaximumHeight(qMax(120, fontMetrics().height() * 6 + 16));
    m_urlInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_urlInput->setFocusPolicy(Qt::StrongFocus);
    m_urlInput->setTabChangesFocus(false);
    inputLay->addWidget(m_urlInput);

    right->addWidget(inputGroup);

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);

    m_startBtn = new QPushButton(QStringLiteral("开始检测"));
    m_startBtn->setObjectName(QStringLiteral("btnStart"));
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::startDetection);
    btnRow->addWidget(m_startBtn);

    m_fullscreenBtn = new QPushButton(QStringLiteral("全屏"));
    m_fullscreenBtn->setObjectName(QStringLiteral("btnFs"));
    m_fullscreenBtn->setCursor(Qt::PointingHandCursor);
    m_fullscreenBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_fullscreenBtn, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);
    btnRow->addWidget(m_fullscreenBtn);

    m_stopBtn = new QPushButton(QStringLiteral("停止"));
    m_stopBtn->setObjectName(QStringLiteral("btnStop"));
    m_stopBtn->setCursor(Qt::PointingHandCursor);
    m_stopBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::stopDetection);
    btnRow->addWidget(m_stopBtn);

    right->addLayout(btnRow);

    auto *qrGroup = new QGroupBox(QStringLiteral("当前二维码"));
    auto *qrLay = new QVBoxLayout(qrGroup);

    m_fullResCheck = new QCheckBox(QStringLiteral("全图识别（关则仅扫画面中央约 60%）"));
    m_fullResCheck->setChecked(false);
    qrLay->addWidget(m_fullResCheck);

    m_currentQrLabel = new QLabel(QStringLiteral("当前：无"));
    m_currentQrLabel->setObjectName(QStringLiteral("currentQrLabel"));
    m_currentQrLabel->setStyleSheet(qrLabelStyleIdle());
    m_currentQrLabel->setWordWrap(true);
    qrLay->addWidget(m_currentQrLabel);

    m_qrCountLabel = new QLabel(QStringLiteral("累计识别：0 次"));
    m_qrCountLabel->setObjectName(QStringLiteral("qrCountLabel"));
    qrLay->addWidget(m_qrCountLabel);

    right->addWidget(qrGroup);

    auto *histGroup = new QGroupBox(QStringLiteral("识别历史"));
    auto *histLay = new QVBoxLayout(histGroup);

    m_historyText = new QTextEdit;
    m_historyText->setObjectName(QStringLiteral("historyLog"));
    m_historyText->setReadOnly(true);
    m_historyText->setMinimumHeight(qMax(80, fontMetrics().height() * 5));
    m_historyText->setMaximumHeight(qMax(200, fontMetrics().height() * 12));
    m_historyText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    histLay->addWidget(m_historyText);

    auto *clearBtn = new QPushButton(QStringLiteral("清空历史"));
    clearBtn->setObjectName(QStringLiteral("btnClear"));
    clearBtn->setCursor(Qt::PointingHandCursor);
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearHistory);
    histLay->addWidget(clearBtn);

    right->addWidget(histGroup);

    rightPanel->setMinimumWidth(280);
    rightScroll->setWidget(rightPanel);
    root->addWidget(rightScroll, 2);
}

void MainWindow::applyInitialWindowGeometry() {
    QScreen *screen = this->screen();
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        resize(1280, 800);
        return;
    }

    const QRect avail = screen->availableGeometry();
    const int targetW = qBound(minimumWidth(), static_cast<int>(avail.width() * 0.92), 1680);
    const int targetH = qBound(minimumHeight(), static_cast<int>(avail.height() * 0.90), 980);
    resize(targetW, targetH);
    const int x = avail.x() + (avail.width() - width()) / 2;
    const int y = avail.y() + (avail.height() - height()) / 2;
    move(x, y);
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    static bool s_initialGeometryApplied = false;
    if (!s_initialGeometryApplied) {
        s_initialGeometryApplied = true;
        applyInitialWindowGeometry();
    }
}

void MainWindow::setKeyExpiry(const QDateTime &expiresAt) {
    m_keyExpiresAt = expiresAt;
    if (m_keyExpiryTimer == nullptr) {
        m_keyExpiryTimer = new QTimer(this);
        connect(m_keyExpiryTimer, &QTimer::timeout, this, &MainWindow::tickKeyExpiryDisplay);
    }
    if (!expiresAt.isValid()) {
        m_keyExpiryTimer->stop();
        if (m_keyExpiryLabel != nullptr) {
            m_keyExpiryLabel->setText(QStringLiteral("密钥到期：未提供"));
        }
        return;
    }
    m_keyExpiryTimer->start(1000);
    tickKeyExpiryDisplay();
}

void MainWindow::clearKeyExpiry() {
    m_keyExpiresAt = QDateTime();
    if (m_keyExpiryTimer != nullptr) {
        m_keyExpiryTimer->stop();
    }
    if (m_keyExpiryLabel != nullptr) {
        m_keyExpiryLabel->setText(QStringLiteral("密钥到期：--"));
    }
}

void MainWindow::tickKeyExpiryDisplay() {
    if (m_keyExpiryLabel == nullptr || !m_keyExpiresAt.isValid()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    if (now >= m_keyExpiresAt) {
        m_keyExpiryLabel->setText(
            QStringLiteral("密钥已于 %1 到期")
                .arg(m_keyExpiresAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
        );
        if (m_keyExpiryTimer != nullptr) {
            m_keyExpiryTimer->stop();
        }
        return;
    }
    qint64 secTotal = now.secsTo(m_keyExpiresAt);
    const qint64 days = secTotal / 86400;
    secTotal %= 86400;
    const int h = static_cast<int>(secTotal / 3600);
    secTotal %= 3600;
    const int mi = static_cast<int>(secTotal / 60);
    const int s = static_cast<int>(secTotal % 60);

    QString remain;
    if (days > 0) {
        remain = QStringLiteral("%1天 %2:%3:%4")
                     .arg(days)
                     .arg(h, 2, 10, QLatin1Char('0'))
                     .arg(mi, 2, 10, QLatin1Char('0'))
                     .arg(s, 2, 10, QLatin1Char('0'));
    } else {
        remain = QStringLiteral("%1:%2:%3")
                     .arg(h, 2, 10, QLatin1Char('0'))
                     .arg(mi, 2, 10, QLatin1Char('0'))
                     .arg(s, 2, 10, QLatin1Char('0'));
    }
    m_keyExpiryLabel->setText(
        QStringLiteral("到期：%1  ·  剩余 %2")
            .arg(m_keyExpiresAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), remain)
    );
}

void MainWindow::stopStreamingForLicense() {
    stopDetection();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_isFullscreen) {
        exitFullscreen();
    }
    stopDetection();
    event->accept();
}

void MainWindow::startDetection() {
    const QString url = streamProviderExtractUrl(m_urlInput->toPlainText());
    if (url.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("警告"), QStringLiteral("请输入抖音直播间链接"));
        return;
    }

    if (m_worker != nullptr && m_worker->isRunning()) {
        QMessageBox::warning(
            this,
            QStringLiteral("提示"),
            QStringLiteral("上一路检测仍在结束中，请稍候再试或先等待停止完成")
        );
        return;
    }

    m_startBtn->setEnabled(false);
    m_startBtn->setText(QStringLiteral("正在解析…"));
    m_statusLabel->setText(QStringLiteral("状态 · 正在解析直播间地址…"));
    QApplication::processEvents();

    StreamResolveResult resolved{};
    QString resolveErr;
    if (!streamProviderResolve(url, &resolved, &resolveErr)) {
        QMessageBox::warning(this, QStringLiteral("解析失败"), resolveErr);
        m_startBtn->setEnabled(true);
        m_startBtn->setText(QStringLiteral("开始检测"));
        m_statusLabel->setText(QStringLiteral("状态 · 解析失败"));
        return;
    }

    m_lastQr.clear();
    m_qrCount = 0;
    m_historyText->clear();
    m_currentQrLabel->setText(QStringLiteral("当前：无"));
    m_qrCountLabel->setText(QStringLiteral("累计识别：0 次"));

    AppConfig cfg{};
    app_set_default_config(&cfg);
    const QByteArray streamUtf8 = resolved.streamUrl.toUtf8();
    std::snprintf(cfg.url, sizeof(cfg.url), "%s", streamUtf8.constData());
    cfg.use_gpu = true;
    cfg.enable_audio = true;
    cfg.detector_mode =
        m_fullResCheck->isChecked() ? QR_DETECTOR_MODE_FULL_RES : QR_DETECTOR_MODE_FAST;
    cfg.use_native_resolution = true;
    cfg.output_width = 0;
    cfg.output_height = 0;
    cfg.fast_decode = false;
    cfg.vsync = false;

    m_startBtn->setText(QStringLiteral("正在连接…"));
    m_statusLabel->setText(
        QStringLiteral("状态 · 已解析源流，正在连接（%1）…").arg(resolved.qualityHint)
    );

    m_worker = new StreamWorker(this);
    m_worker->setConfig(cfg);

    connect(m_worker, &StreamWorker::frameReady, this, &MainWindow::onFrameReady, Qt::QueuedConnection);
    connect(m_worker, &StreamWorker::qrDetected, this, &MainWindow::onQrDetected, Qt::QueuedConnection);
    connect(m_worker, &StreamWorker::errorOccurred, this, &MainWindow::onError, Qt::QueuedConnection);
    connect(m_worker, &StreamWorker::statusMessage, this, &MainWindow::onStatus, Qt::QueuedConnection);

    m_worker->start();

    m_stopBtn->setEnabled(true);
    m_urlInput->setEnabled(false);
    m_fullResCheck->setEnabled(false);
}

void MainWindow::stopDetection() {
    if (m_isFullscreen) {
        exitFullscreen();
    }

    m_pendingVideoFrame = QImage();
    m_videoFlushScheduled = false;

    if (m_worker != nullptr) {
        m_worker->requestStop();
        /* 不在此处调用 ffmpeg_stream_stop：应在工作线程内结束（见 StreamWorker::run 收尾） */
        if (!m_worker->wait(45000)) {
            QMessageBox::warning(
                this,
                QStringLiteral("提示"),
                QStringLiteral("检测线程未在 45 秒内结束（常见于网络阻塞）。\n请直接关闭本窗口后重新打开程序。\n（已避免强制结束线程以防崩溃）")
            );
            m_statusLabel->setText(QStringLiteral("状态 · 停止超时，请关闭程序"));
            return;
        }
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    m_startBtn->setEnabled(true);
    m_startBtn->setText(QStringLiteral("开始检测"));
    m_stopBtn->setEnabled(false);
    m_urlInput->setEnabled(true);
    m_fullResCheck->setEnabled(true);
    m_statusLabel->setText(QStringLiteral("状态 · 已停止"));
}

void MainWindow::onFrameReady(const QImage &image) {
    if (image.isNull()) {
        return;
    }
    /* 工作线程 QueuedConnection 可能积压多帧；只保留最新一帧，下一拍统一缩放/贴图，避免「偶尔一顿」 */
    m_pendingVideoFrame = image;
    if (!m_videoFlushScheduled) {
        m_videoFlushScheduled = true;
        QTimer::singleShot(0, this, [this]() { flushPendingVideoFrame(); });
    }
}

void MainWindow::flushPendingVideoFrame() {
    m_videoFlushScheduled = false;
    if (m_pendingVideoFrame.isNull()) {
        return;
    }
    QImage img = std::move(m_pendingVideoFrame);
    QPixmap pm = QPixmap::fromImage(img);
    /* 按内容区（含 stylesheet 的 padding）缩放，避免四边被裁掉一块 */
    QSize target = m_videoLabel->contentsRect().size();
    if (target.width() < 1 || target.height() < 1) {
        target = m_videoLabel->size();
    }
    if (target.width() < 1 || target.height() < 1) {
        return;
    }
    const qreal dpr = m_videoLabel->devicePixelRatioF();
    const QSize hiTarget(
        qMax(1, static_cast<int>(std::lround(target.width() * dpr))),
        qMax(1, static_cast<int>(std::lround(target.height() * dpr)))
    );
    pm = pm.scaled(hiTarget, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pm.setDevicePixelRatio(dpr);
    m_videoLabel->setPixmap(pm);
}

void MainWindow::onQrDetected(const QString &text) {
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        return;
    }

    /* 同内容且弹窗仍开着时不每帧重开，避免刷屏；关闭后再次识别到同内容仍可弹窗并记历史 */
    if (m_qrPopup != nullptr && m_qrPopup->isVisible() && t == m_lastQr) {
        return;
    }

    m_lastQr = t;

    m_qrCount += 1;
    m_currentQrLabel->setText(QStringLiteral("当前：") + t);
    m_qrCountLabel->setText(QStringLiteral("累计识别：%1 次").arg(m_qrCount));

    const QString ts =
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    m_historyText->append(QStringLiteral("[%1] %2").arg(ts, t));
    if (m_historyText->verticalScrollBar() != nullptr) {
        m_historyText->verticalScrollBar()->setValue(m_historyText->verticalScrollBar()->maximum());
    }

    m_currentQrLabel->setStyleSheet(qrLabelStyleHighlight());
    QTimer::singleShot(2000, this, &MainWindow::resetQrHighlight);

    showQrPopup(t);
}

void MainWindow::resetQrHighlight() {
    m_currentQrLabel->setStyleSheet(qrLabelStyleIdle());
}

void MainWindow::showQrPopup(const QString &text) {
    if (text.trimmed().isEmpty()) {
        return;
    }

    /* 仅 close：已设 WA_DeleteOnClose，勿再 deleteLater，否则易二次释放闪退 */
    if (m_qrPopup) {
        m_qrPopup->close();
    }

    auto *dlg = new QRPopupDialog(text, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_qrPopup = dlg;
    if (QScreen *ps = QGuiApplication::primaryScreen()) {
        const QRect screen = ps->availableGeometry();
        const int dw = 600;
        const int dh = 700;
        const int x = screen.x() + screen.width() - dw - 50;
        const int y = screen.y() + (screen.height() - dh) / 2;
        dlg->move(x, y);
    }
    dlg->show();
}

void MainWindow::onError(const QString &text) {
    QMessageBox::critical(this, QStringLiteral("错误"), text);
    stopDetection();
}

void MainWindow::onStatus(const QString &text) {
    m_statusLabel->setText(QStringLiteral("状态 · ") + text);
}

void MainWindow::clearHistory() {
    m_lastQr.clear();
    m_qrCount = 0;
    m_historyText->clear();
    m_currentQrLabel->setText(QStringLiteral("当前：无"));
    m_qrCountLabel->setText(QStringLiteral("累计识别：0 次"));
    resetQrHighlight();
}

void MainWindow::toggleFullscreen() {
    if (!m_isFullscreen) {
        enterFullscreen();
    } else {
        exitFullscreen();
    }
}

void MainWindow::enterFullscreen() {
    if (m_isFullscreen) {
        return;
    }

    m_fullscreenWindow = new QMainWindow;
    m_fullscreenWindow->setWindowTitle(QStringLiteral("直播流检测 - 全屏模式"));
    m_fullscreenWindow->setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    auto *cw = new QWidget;
    cw->setStyleSheet(QStringLiteral("background-color: black;"));
    m_fullscreenWindow->setCentralWidget(cw);
    auto *lay = new QVBoxLayout(cw);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_videoLabel->setParent(cw);
    m_videoLabel->setMinimumSize(0, 0);
    m_videoLabel->setFixedSize(m_fullscreenWindow->size());
    m_videoLabel->setStyleSheet(videoLabelFullscreenStyle());
    lay->addWidget(m_videoLabel);

    m_fullscreenWindow->showFullScreen();
    m_fullscreenWindow->raise();
    m_fullscreenWindow->activateWindow();

    /* 独立全屏窗口无菜单栏，需显式绑定 ESC（否则焦点在子控件上时收不到） */
    auto *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_fullscreenWindow);
    escShortcut->setContext(Qt::ApplicationShortcut);
    QObject::connect(escShortcut, &QShortcut::activated, this, &MainWindow::exitFullscreen);

    QTimer::singleShot(0, this, [this]() {
        if (m_fullscreenWindow != nullptr && m_videoLabel != nullptr) {
            m_videoLabel->setFixedSize(m_fullscreenWindow->size());
        }
    });
    m_isFullscreen = true;
    m_fullscreenBtn->setText(QStringLiteral("退出全屏"));
}

void MainWindow::exitFullscreen() {
    if (!m_isFullscreen) {
        return;
    }

    m_videoLabel->setParent(m_leftWidget);
    m_videoLabel->setMinimumSize(320, 180);
    m_videoLabel->setMaximumSize(16777215, 16777215);
    m_videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoLabel->setStyleSheet(QString());
    m_leftLayout->insertWidget(1, m_videoLabel);
    m_leftLayout->setStretch(1, 1);

    if (m_fullscreenWindow != nullptr) {
        m_fullscreenWindow->close();
        m_fullscreenWindow->deleteLater();
        m_fullscreenWindow = nullptr;
    }

    m_isFullscreen = false;
    m_fullscreenBtn->setText(QStringLiteral("全屏播放"));
}
