#include "KeyAuthClient.hpp"
#include "KeyAuthDialog.hpp"
#include "KeyHeartbeat.hpp"
#include "MainWindow.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QGuiApplication>
#include <QMessageBox>
#include <QObject>

static bool runInitialLogin(QString *sessionKey, QDateTime *expiresAt) {
    for (;;) {
        KeyAuthDialog dlg(nullptr);
        if (dlg.exec() != QDialog::Accepted) {
            return false;
        }
        QString err;
        if (keyAuthLogin(dlg.key(), sessionKey, &err, expiresAt)) {
            KeyAuthDialog::applyRememberKeyAfterLogin(dlg.rememberKey(), dlg.key());
            return true;
        }
        QMessageBox::warning(nullptr, QStringLiteral("登录失败"), err);
    }
}

int main(int argc, char *argv[]) {
    /* 高 DPI：避免 Windows 对非 DPI 感知程序做位图拉伸（模糊 + 输入框点不中） */
    /* Round：多显示器/125%/150% 缩放下比 PassThrough 更稳定，减少控件错位或点不中 */
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::Round
    );
    QApplication app(argc, argv);
    QApplication::setStyle(QStringLiteral("Fusion"));

    QString sessionKey;
    QDateTime keyExpiresAt;
    if (!runInitialLogin(&sessionKey, &keyExpiresAt)) {
        return 0;
    }

    MainWindow window;
    window.setKeyExpiry(keyExpiresAt);
    KeyHeartbeat heartbeat(&window);
    heartbeat.setSessionKey(sessionKey);

    QObject::connect(&heartbeat, &KeyHeartbeat::keyExpiresAtUpdated, &window, &MainWindow::setKeyExpiry);

    QObject::connect(&heartbeat, &KeyHeartbeat::sessionLost, &window, [&heartbeat, &window, &sessionKey]() {
        heartbeat.stop();
        window.clearKeyExpiry();
        window.stopStreamingForLicense();
        QMessageBox::warning(
            &window,
            QStringLiteral("验证失效"),
            QStringLiteral("密钥已失效或心跳失败，请重新登录。")
        );
        QString newKey;
        QDateTime newExpires;
        for (;;) {
            KeyAuthDialog dlg(&window);
            if (dlg.exec() != QDialog::Accepted) {
                QApplication::quit();
                return;
            }
            QString err;
            if (keyAuthLogin(dlg.key(), &newKey, &err, &newExpires)) {
                KeyAuthDialog::applyRememberKeyAfterLogin(dlg.rememberKey(), dlg.key());
                break;
            }
            QMessageBox::warning(&window, QStringLiteral("登录失败"), err);
        }
        sessionKey = newKey;
        heartbeat.setSessionKey(newKey);
        heartbeat.start();
        window.setKeyExpiry(newExpires);
    });

    heartbeat.start();
    window.show();

    QObject::connect(&app, &QApplication::aboutToQuit, [&heartbeat, &sessionKey]() {
        heartbeat.stop();
        keyAuthLogout(sessionKey);
    });

    return app.exec();
}
