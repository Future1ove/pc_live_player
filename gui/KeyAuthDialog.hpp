#ifndef KEY_AUTH_DIALOG_HPP
#define KEY_AUTH_DIALOG_HPP

#include <QDialog>
#include <QString>

class QCheckBox;
class QLineEdit;

class KeyAuthDialog final : public QDialog {
    Q_OBJECT

public:
    explicit KeyAuthDialog(QWidget *parent = nullptr);

    QString key() const;
    bool rememberKey() const;

    /** 登录成功后：勾选则保存密钥与偏好；未勾选则清除已保存密钥。 */
    static void applyRememberKeyAfterLogin(bool remember, const QString &key);

private:
    QLineEdit *m_edit{nullptr};
    QCheckBox *m_rememberCheck{nullptr};
};

#endif
