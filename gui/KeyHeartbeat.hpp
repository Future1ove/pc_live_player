#ifndef KEY_HEARTBEAT_HPP
#define KEY_HEARTBEAT_HPP

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class QNetworkReply;
class QTimer;

/** 定时向后端发送心跳（Authorization 头携带会话 key）。 */
class KeyHeartbeat final : public QObject {
    Q_OBJECT

public:
    explicit KeyHeartbeat(QObject *parent = nullptr);

    void setSessionKey(const QString &key);
    void start();
    void stop();

signals:
    void sessionLost(const QString &reason);
    /** 心跳响应里若 msg 为到期时间字符串，则发出以便 UI 同步倒计时。 */
    void keyExpiresAtUpdated(const QDateTime &expiresAt);

private slots:
    void onTimer();
    void onReplyFinished();

private:
    QString m_key;
    QTimer *m_timer{nullptr};
    QNetworkAccessManager m_nam;
    QNetworkReply *m_pending{nullptr};
};

#endif
