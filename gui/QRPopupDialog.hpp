#ifndef QRPOPUPDIALOG_HPP
#define QRPOPUPDIALOG_HPP

#include <QDialog>
#include <QString>

class QPushButton;
class QrImageWidget;

class QRPopupDialog final : public QDialog {
    Q_OBJECT

public:
    explicit QRPopupDialog(const QString &qrData, QWidget *parent = nullptr);

private:
    void renderQrToLabel();

    QrImageWidget *m_qrView{nullptr};
    QString m_qrText;
};

#endif
