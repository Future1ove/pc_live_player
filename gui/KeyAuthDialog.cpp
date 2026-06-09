#include "KeyAuthDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QScreen>
#include <QSettings>
#include <QVBoxLayout>

namespace {

QString loadRememberedKey() {
    QSettings s(QStringLiteral("DouyinQR"), QStringLiteral("douyin_qr"));
    return s.value(QStringLiteral("auth/remembered_key")).toString().trimmed();
}

bool loadRememberKeyPreference() {
    QSettings s(QStringLiteral("DouyinQR"), QStringLiteral("douyin_qr"));
    const QString prefKey = QStringLiteral("auth/remember_key_checked");
    if (!s.contains(prefKey)) {
        const QString k = s.value(QStringLiteral("auth/remembered_key")).toString().trimmed();
        if (!k.isEmpty()) {
            return true;
        }
    }
    return s.value(prefKey, false).toBool();
}

} // namespace

KeyAuthDialog::KeyAuthDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("密钥验证"));
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral("请输入密钥以继续使用本程序：")));

    auto *form = new QFormLayout;
    m_edit = new QLineEdit;
    m_edit->setEchoMode(QLineEdit::Password);
    m_edit->setPlaceholderText(QStringLiteral("密钥"));
    m_edit->setMinimumWidth(320);
    form->addRow(QStringLiteral("密钥"), m_edit);
    root->addLayout(form);

    m_rememberCheck = new QCheckBox(QStringLiteral("记住密钥"));
    m_rememberCheck->setChecked(loadRememberKeyPreference());
    m_rememberCheck->setToolTip(QStringLiteral("勾选后，下次启动将自动填入本次密钥（保存在本机当前用户配置）。"));
    root->addWidget(m_rememberCheck);

    auto *box =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);

    const QString saved = loadRememberedKey();
    if (!saved.isEmpty()) {
        m_edit->setText(saved);
    }
    m_edit->setFocus();

    adjustSize();
    setMinimumWidth(qMax(360, minimumWidth()));
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        if (width() > avail.width() - 32) {
            resize(avail.width() - 32, height());
        }
        const int x = avail.x() + (avail.width() - width()) / 2;
        const int y = avail.y() + (avail.height() - height()) / 2;
        move(x, y);
    }
}

void KeyAuthDialog::applyRememberKeyAfterLogin(bool remember, const QString &key) {
    QSettings s(QStringLiteral("DouyinQR"), QStringLiteral("douyin_qr"));
    s.setValue(QStringLiteral("auth/remember_key_checked"), remember);
    if (remember) {
        const QString t = key.trimmed();
        if (t.isEmpty()) {
            s.remove(QStringLiteral("auth/remembered_key"));
        } else {
            s.setValue(QStringLiteral("auth/remembered_key"), t);
        }
    } else {
        s.remove(QStringLiteral("auth/remembered_key"));
    }
}

bool KeyAuthDialog::rememberKey() const {
    return m_rememberCheck != nullptr && m_rememberCheck->isChecked();
}

QString KeyAuthDialog::key() const {
    return m_edit != nullptr ? m_edit->text() : QString();
}
