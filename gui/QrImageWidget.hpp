#ifndef QRIMAGEWIDGET_HPP
#define QRIMAGEWIDGET_HPP

#include <QPixmap>
#include <QString>
#include <QWidget>

/** 仅用 QPainter 绘码，避免 QLabel::setPixmap 在部分 Windows/Qt6 样式下不显示 */
class QrImageWidget final : public QWidget {
    Q_OBJECT

public:
    explicit QrImageWidget(QWidget *parent = nullptr);

    void setQrPixmap(QPixmap pm);
    void setMessage(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap m_pixmap;
    QString m_message;
};

#endif
