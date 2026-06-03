#include "KeyAuthClient.hpp"
#include "api_config.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QVariant>

QDateTime keyAuthParseExpiryMsg(const QString &msg) {
    const QString t = msg.trimmed();
    if (t.isEmpty()) {
        return {};
    }
    const QDateTime dt =
        QDateTime::fromString(t, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!dt.isValid()) {
        return {};
    }
    return dt;
}

namespace {

/** 兼容 code 为数字或字符串（如 "200"）。 */
static int jsonApiCode(const QJsonObject &o) {
    const QJsonValue v = o.value(QStringLiteral("code"));
    if (v.isDouble()) {
        return static_cast<int>(v.toDouble());
    }
    if (v.isString()) {
        bool ok = false;
        const int n = v.toString().trimmed().toInt(&ok);
        return ok ? n : -1;
    }
    return v.toInt(-1);
}

/** 接口错误文案：优先 error，其次 msg（成功时 msg 常为到期时间，失败时常为提示）。 */
static QString jsonApiErrMsg(const QJsonObject &o) {
    const QString e = o.value(QStringLiteral("error")).toString().trimmed();
    if (!e.isEmpty()) {
        return e;
    }
    return o.value(QStringLiteral("msg")).toString().trimmed();
}

/** data 为字符串，或为对象内 token/key/session 等字段。 */
static QString extractSessionToken(
    const QJsonObject &o,
    const QByteArray &setAuthorization,
    const QString &fallbackKey
) {
    QString session;
    const QJsonValue d = o.value(QStringLiteral("data"));
    if (d.isString()) {
        session = d.toString();
    } else if (d.isObject()) {
        const QJsonObject obj = d.toObject();
        static const QLatin1String keys[] = {
            QLatin1String("token"),
            QLatin1String("key"),
            QLatin1String("session"),
            QLatin1String("sessionKey"),
            QLatin1String("value"),
            QLatin1String("data"),
        };
        for (const auto &k : keys) {
            const QString s = obj.value(k).toString();
            if (!s.isEmpty()) {
                session = s;
                break;
            }
        }
    }
    if (session.isEmpty() && !setAuthorization.isEmpty()) {
        session = QString::fromUtf8(setAuthorization);
    }
    if (session.isEmpty()) {
        session = fallbackKey;
    }
    return session;
}

} // namespace

static QUrl makeBaseUrl() {
    QUrl base(QString::fromUtf8(kKeyApiBaseUrl));
    QString p = base.path();
    if (p.isEmpty()) {
        p = QStringLiteral("/");
    }
    if (!p.endsWith(QLatin1Char('/'))) {
        base.setPath(p + QLatin1Char('/'));
    }
    return base;
}

static QUrl makeLoginUrl(const QString &key) {
    const QString encoded =
        QString::fromUtf8(QUrl::toPercentEncoding(key.toUtf8()));
    const QString rel =
        QString::fromUtf8(kKeyLoginRelative) + QLatin1Char('/') + encoded;
    return makeBaseUrl().resolved(QUrl(rel));
}

static QUrl makeLogoutUrl() {
    QUrl base(QString::fromUtf8(kKeyApiBaseUrl));
    QString p = base.path();
    if (p.isEmpty()) {
        p = QStringLiteral("/");
    }
    if (!p.endsWith(QLatin1Char('/'))) {
        base.setPath(p + QLatin1Char('/'));
    }
    return base.resolved(QUrl(QString::fromUtf8(kKeyLogoutRelative)));
}

bool keyAuthLogin(
    const QString &keyInput,
    QString *outSessionKey,
    QString *outError,
    QDateTime *outExpiresAt
) {
    if (outSessionKey != nullptr) {
        outSessionKey->clear();
    }
    if (outError != nullptr) {
        outError->clear();
    }
    if (outExpiresAt != nullptr) {
        *outExpiresAt = QDateTime();
    }

    const QString trimmed = keyInput.trimmed();
    if (trimmed.isEmpty()) {
        if (outError != nullptr) {
            *outError = QStringLiteral("请输入密钥");
        }
        return false;
    }

    const QUrl url = makeLoginUrl(trimmed);
    QNetworkRequest req(url);
    req.setTransferTimeout(kKeyHttpTimeoutMs);
    req.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy
    );

    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(kKeyHttpTimeoutMs + 2000);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
    }
    timer.stop();

    const QByteArray body = reply->readAll();
    const QVariant statusVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int httpStatus = statusVar.isValid() ? statusVar.toInt() : -1;
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netErrStr = reply->errorString();
    const QByteArray authHdr = reply->rawHeader("Set-Authorization");

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    const bool jsonOk = (pe.error == QJsonParseError::NoError && doc.isObject());
    const QJsonObject jo = jsonOk ? doc.object() : QJsonObject{};

    reply->deleteLater();

    auto fillError = [&](QString hint) {
        if (outError == nullptr) {
            return;
        }
        QString msg = hint.trimmed();
        if (msg.isEmpty()) {
            msg = jsonApiErrMsg(jo);
        }
        if (msg.isEmpty() && netErr != QNetworkReply::NoError) {
            msg = netErrStr;
        }
        if (msg.isEmpty() && httpStatus >= 400) {
            msg = QStringLiteral("HTTP %1").arg(httpStatus);
        }
        if (msg.isEmpty()) {
            msg = QStringLiteral("登录失败");
        }
        *outError = msg;
    };

    if (netErr != QNetworkReply::NoError) {
        fillError({});
        return false;
    }

    if (!jsonOk) {
        fillError(QStringLiteral("服务器返回非 JSON，请检查网络或接口地址"));
        return false;
    }

    const int code = jsonApiCode(jo);
    if (code != 200) {
        fillError({});
        return false;
    }

    if (httpStatus >= 400) {
        fillError(QStringLiteral("HTTP %1").arg(httpStatus));
        return false;
    }

    const QString session = extractSessionToken(jo, authHdr, trimmed);

    if (outSessionKey != nullptr) {
        *outSessionKey = session;
    }
    if (outExpiresAt != nullptr) {
        *outExpiresAt = keyAuthParseExpiryMsg(jo.value(QStringLiteral("msg")).toString());
    }
    return true;
}

void keyAuthLogout(const QString &sessionKey) {
    const QString trimmed = sessionKey.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    const QUrl url = makeLogoutUrl();
    QNetworkRequest req(url);
    req.setTransferTimeout(kKeyHttpTimeoutMs);
    req.setRawHeader("Authorization", trimmed.toUtf8());
    req.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy
    );

    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(kKeyHttpTimeoutMs + 2000);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
    }
    timer.stop();
    reply->deleteLater();
}
