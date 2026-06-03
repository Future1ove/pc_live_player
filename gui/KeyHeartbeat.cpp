#include "KeyHeartbeat.hpp"
#include "KeyAuthClient.hpp"
#include "api_config.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

static QUrl makeBeatsUrl() {
    QUrl base(QString::fromUtf8(kKeyApiBaseUrl));
    QString p = base.path();
    if (p.isEmpty()) {
        p = QStringLiteral("/");
    }
    if (!p.endsWith(QLatin1Char('/'))) {
        base.setPath(p + QLatin1Char('/'));
    }
    return base.resolved(QUrl(QString::fromUtf8(kKeyBeatsRelative)));
}

KeyHeartbeat::KeyHeartbeat(QObject *parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(kKeyHeartbeatIntervalMs);
    QObject::connect(m_timer, &QTimer::timeout, this, &KeyHeartbeat::onTimer);
}

void KeyHeartbeat::setSessionKey(const QString &key) {
    m_key = key.trimmed();
}

void KeyHeartbeat::start() {
    if (m_key.isEmpty()) {
        return;
    }
    if (!m_timer->isActive()) {
        m_timer->start();
    }
    /* 启动后立即发一次心跳，便于尽快发现失效 */
    onTimer();
}

void KeyHeartbeat::stop() {
    if (m_timer != nullptr) {
        m_timer->stop();
    }
    if (m_pending != nullptr) {
        m_pending->abort();
        m_pending->deleteLater();
        m_pending = nullptr;
    }
}

void KeyHeartbeat::onTimer() {
    if (m_key.isEmpty()) {
        return;
    }
    if (m_pending != nullptr) {
        return;
    }

    const QUrl url = makeBeatsUrl();
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", m_key.toUtf8());
    req.setTransferTimeout(kKeyHttpTimeoutMs);

    m_pending = m_nam.post(req, QByteArray());
    QObject::connect(m_pending, &QNetworkReply::finished, this, &KeyHeartbeat::onReplyFinished);
}

void KeyHeartbeat::onReplyFinished() {
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (reply == nullptr || reply != m_pending) {
        return;
    }
    m_pending = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        emit sessionLost(QString());
        return;
    }

    const QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        emit sessionLost(QString());
        return;
    }

    const QJsonObject o = doc.object();
    const int code = o.value(QStringLiteral("code")).toInt(-1);
    if (code == 200) {
        const QDateTime exp = keyAuthParseExpiryMsg(o.value(QStringLiteral("msg")).toString());
        if (exp.isValid()) {
            emit keyExpiresAtUpdated(exp);
        }
        return;
    }

    emit sessionLost(QString());
}
