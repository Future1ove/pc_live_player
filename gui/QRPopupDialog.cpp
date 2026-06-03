#include "QRPopupDialog.hpp"

#include "QrImageWidget.hpp"

#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#ifdef HAVE_QRENCODE
#include <qrencode.h>

#include <algorithm>
#include <cstring>
#include <utility>

static QRcode *encodeQr(const QByteArray &utf8) {
    QRcode *code = QRcode_encodeString8bit(utf8.constData(), 0, QR_ECLEVEL_H);
    if (code == nullptr) {
        code = QRcode_encodeString(utf8.constData(), 0, QR_ECLEVEL_H, QR_MODE_8, 1);
    }
    if (code == nullptr && utf8.size() > 0) {
        code = QRcode_encodeData(
            utf8.size(),
            reinterpret_cast<const unsigned char *>(utf8.constData()),
            0,
            QR_ECLEVEL_H
        );
    }
    return code;
}

static void paintQrCodeToImage(QImage &img, const QRcode *code, int margin, int scale) {
    const int w = code->width;
    for (int y = 0; y < w; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char v = code->data[static_cast<size_t>(y) * static_cast<size_t>(w) +
                                              static_cast<size_t>(x)];
            if ((v & 1U) == 0U) {
                continue;
            }
            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    const int px = (x + margin) * scale + sx;
                    const int py = (y + margin) * scale + sy;
                    if (px >= 0 && px < img.width() && py >= 0 && py < img.height()) {
                        img.setPixel(px, py, qRgb(0, 0, 0));
                    }
                }
            }
        }
    }
}
#endif

void QRPopupDialog::renderQrToLabel() {
#ifdef HAVE_QRENCODE
    if (m_qrView == nullptr) {
        return;
    }

    const QByteArray utf8 = m_qrText.toUtf8();
    QRcode *code = encodeQr(utf8);
    if (code == nullptr) {
        m_qrView->setMessage(QStringLiteral("生成二维码失败（编码）"));
        return;
    }

    const int margin = 4;
    const qreal dpr = std::max(1.0, static_cast<double>(m_qrView->devicePixelRatioF()));
    const int targetPx = static_cast<int>(500.0 * dpr + 0.5);
    const int modules = code->width + margin * 2;
    int scale = (targetPx + modules - 1) / modules;
    scale = std::clamp(scale, 6, 24);

    const int img_size = (code->width + margin * 2) * scale;
    const int maxDim = 4096;
    int safe_scale = scale;
    if (img_size > maxDim) {
        safe_scale = maxDim / modules;
        safe_scale = std::max(safe_scale, 4);
    }

    const int final_img_size = (code->width + margin * 2) * safe_scale;
    QImage img(final_img_size, final_img_size, QImage::Format_RGB32);
    if (img.isNull()) {
        QRcode_free(code);
        m_qrView->setMessage(QStringLiteral("生成失败：图像过大"));
        return;
    }
    img.fill(Qt::white);
    paintQrCodeToImage(img, code, margin, safe_scale);
    QRcode_free(code);

    QPixmap pm = QPixmap::fromImage(std::move(img));
    if (pm.isNull()) {
        m_qrView->setMessage(QStringLiteral("生成失败：无法转换为位图"));
        return;
    }

    /* 与 QrImageWidget 逻辑边长 500 一致，按 DPR 放大像素，且不在 pixmap 上设 DPR */
    const int sidePx = static_cast<int>(500.0 * dpr + 0.5);
    pm = pm.scaled(sidePx, sidePx, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    if (pm.isNull()) {
        m_qrView->setMessage(QStringLiteral("生成失败：缩放失败"));
        return;
    }

    m_qrView->setQrPixmap(pm);
#else
    m_qrView->setMessage(QStringLiteral("未编译 libqrencode"));
#endif
}

QRPopupDialog::QRPopupDialog(const QString &qrData, QWidget *parent) : QDialog(parent) {
    m_qrText = qrData.trimmed();
    if (m_qrText.isEmpty()) {
        m_qrText = QStringLiteral("(空)");
    }

    setWindowTitle(QStringLiteral("新二维码 · Studio"));
    setFixedSize(600, 700);
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    setStyleSheet(QStringLiteral(
        "QDialog {"
        "background-color: #0c1018;"
        "border: 1px solid rgba(212, 175, 55, 0.25);"
        "border-radius: 12px;"
        "}"
    ));

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(18);
    layout->setContentsMargins(24, 22, 24, 22);

    auto *title = new QLabel(QStringLiteral("识别到新二维码"));
    title->setStyleSheet(QStringLiteral(
        "font-size: 20px; font-weight: 700; color: #f1f5f9;"
        "background: transparent; border: none; padding: 8px;"
        "letter-spacing: 0.02em;"
    ));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    m_qrView = new QrImageWidget(this);
    layout->addWidget(m_qrView, 0, Qt::AlignCenter);

    auto *dataLabel = new QLabel(QStringLiteral("内容：") + m_qrText);
    dataLabel->setStyleSheet(QStringLiteral(
        "font-size: 13px; color: #cbd5e1;"
        "background-color: rgba(15, 23, 42, 0.95);"
        "border: 1px solid rgba(148, 163, 184, 0.25);"
        "border-radius: 10px; padding: 12px 14px;"
    ));
    dataLabel->setWordWrap(true);
    dataLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(dataLabel);

    auto *closeBtn = new QPushButton(QStringLiteral("关闭"));
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #14b8a6, stop:1 #0d9488);"
        "color: #ecfdf5; font-size: 15px; font-weight: 700;"
        "padding: 12px 24px; border-radius: 8px;"
        "min-width: 200px;"
        "border: 1px solid rgba(45, 212, 191, 0.4);"
        "}"
        "QPushButton:hover {"
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #2dd4bf, stop:1 #14b8a6);"
        "}"
    ));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeBtn, 0, Qt::AlignCenter);

#ifndef HAVE_QRENCODE
    m_qrView->setMessage(QStringLiteral("未编译 libqrencode"));
#else
    /* 事件循环下一轮再绘：尺寸/DPR 已就绪；用 QPainter 控件不依赖 QLabel */
    QTimer::singleShot(0, this, [this]() { renderQrToLabel(); });
#endif
}
