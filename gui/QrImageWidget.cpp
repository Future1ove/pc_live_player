#include "QrImageWidget.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPixmap>

QrImageWidget::QrImageWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setFixedSize(500, 500);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void QrImageWidget::setQrPixmap(QPixmap pm) {
    m_message.clear();
    /* 位图已在调用方按 逻辑边长×devicePixelRatio() 生成，勿再 setDevicePixelRatio，
     * 否则逻辑尺寸会变小，绘出来无法铺满内框。 */
    m_pixmap = std::move(pm);
    update();
}

void QrImageWidget::setMessage(const QString &text) {
    m_message = text;
    m_pixmap = QPixmap();
    update();
}

void QrImageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);

    const QRect outer = rect();
    /* 白底 + 绿框，不依赖 stylesheet */
    p.fillRect(outer, QColor(255, 255, 255));

    constexpr int penW = 3;
    const QRect inner = outer.adjusted(penW, penW, -penW, -penW);

    if (!m_pixmap.isNull()) {
        /* 正方形二维码：直接铺满内框（绿线内侧），避免留白 */
        p.drawPixmap(inner, m_pixmap);
    } else if (!m_message.isEmpty()) {
        p.setPen(QColor(80, 80, 80));
        p.drawText(inner, Qt::AlignCenter | Qt::TextWordWrap, m_message);
    }

    QPen pen(QColor(76, 175, 80), penW);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.addRoundedRect(outer.adjusted(1, 1, -2, -2), 8.0, 8.0);
    p.drawPath(path);
}
