#ifndef KEY_AUTH_CLIENT_HPP
#define KEY_AUTH_CLIENT_HPP

#include <QDateTime>
#include <QString>

/** 从接口 JSON 的 msg 字段解析密钥到期时间（yyyy-MM-dd HH:mm:ss），无法解析则返回无效时间。 */
QDateTime keyAuthParseExpiryMsg(const QString &msg);

/** 同步调用后端 GET /fish/key/pc/:key，成功时写入会话 key（data 或 Set-Authorization）。 */
bool keyAuthLogin(
    const QString &keyInput,
    QString *outSessionKey,
    QString *outError,
    QDateTime *outExpiresAt = nullptr
);

/** 同步调用后端 GET /fish/keylogout，请求头 Authorization 携带会话 key（退出进程前通知服务端）。 */
void keyAuthLogout(const QString &sessionKey);

#endif
