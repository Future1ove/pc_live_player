#include "StreamProvider.hpp"

#include <QByteArray>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

constexpr int kHttpTimeoutMs = 30000;
static const char kDesktopUa[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
static const char kMobileUa[] =
    "Mozilla/5.0 (Linux; Android 11; SAMSUNG SM-G973U) AppleWebKit/537.36 "
    "(KHTML, like Gecko) SamsungBrowser/14.2 Chrome/87.0.4280.141 Mobile Safari/537.36";

static QString hostOf(const QString &url) {
    return QUrl(url).host().toLower();
}

static QString normalizeFlvUrl(QString url) {
    url = url.trimmed();
    if (url.startsWith(QStringLiteral("https://")) &&
        (url.contains(QStringLiteral("douyincdn.com")) ||
         url.contains(QStringLiteral("pull-flv")))) {
        url.replace(0, 8, QStringLiteral("http://"));
    }
    return url;
}

static QNetworkRequest makeRequest(const QUrl &url, const QByteArray &userAgent) {
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", userAgent);
    req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kHttpTimeoutMs);
    return req;
}

static QByteArray syncGet(
    QNetworkAccessManager *nam,
    const QNetworkRequest &req,
    QString *outError,
  int *outStatus = nullptr) {
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply *reply = nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(kHttpTimeoutMs);
    loop.exec();
    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        if (outError != nullptr) {
            *outError = QStringLiteral("请求超时");
        }
        return {};
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (outStatus != nullptr) {
        *outStatus = status;
    }
    if (reply->error() != QNetworkReply::NoError) {
        if (outError != nullptr) {
            *outError = reply->errorString();
        }
        reply->deleteLater();
        return {};
    }
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    return body;
}

static QString flvFromPullDataObj(const QJsonObject &pullData) {
    const QString streamDataStr = pullData.value(QStringLiteral("stream_data")).toString();
    if (!streamDataStr.isEmpty()) {
        const QJsonDocument nested = QJsonDocument::fromJson(streamDataStr.toUtf8());
        if (nested.isObject()) {
            const QJsonObject root = nested.object();
            const QJsonObject origin =
                root.value(QStringLiteral("data")).toObject()
                    .value(QStringLiteral("origin")).toObject()
                    .value(QStringLiteral("main")).toObject();
            const QString flv = origin.value(QStringLiteral("flv")).toString();
            if (!flv.isEmpty()) {
                return flv;
            }
        }
    }
    const QJsonObject data = pullData.value(QStringLiteral("data")).toObject();
    const QString flv = data.value(QStringLiteral("flv")).toString();
    return flv;
}

static QJsonObject streamDataFromPageConfig(const QJsonObject &config) {
    const QJsonObject basic = config.value(QStringLiteral("basicPlayerProps")).toObject();
    const QJsonObject plugins = basic.value(QStringLiteral("pluginsConfigInternal")).toObject();
    const QJsonObject liveStream = plugins.value(QStringLiteral("liveStream")).toObject();
    const QJsonObject streamData = liveStream.value(QStringLiteral("streamData")).toObject();
    if (!streamData.isEmpty()) {
        return streamData;
    }
    const QJsonObject legacy =
        config.value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("origin")).toObject()
            .value(QStringLiteral("main")).toObject();
    if (!legacy.isEmpty()) {
        return legacy;
    }
    return {};
}

static QString flvFromStreamDataObj(const QJsonObject &streamData) {
    QString flv = flvFromPullDataObj(streamData.value(QStringLiteral("h264PullData")).toObject());
    if (!flv.isEmpty()) {
        return flv;
    }
    return flvFromPullDataObj(streamData.value(QStringLiteral("h265PullData")).toObject());
}

static QString decodeHtmlAttr(const QString &raw) {
    QString s = raw;
    s.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    s.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    s.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    s.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    return s;
}

static QString extractDataConfigFromHtml(const QString &html) {
    static const QRegularExpression re(
        QStringLiteral(R"(<script[^>]*\sdata-config=["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(html);
    if (m.hasMatch()) {
        return decodeHtmlAttr(m.captured(1));
    }
    return {};
}

static QString firstFlvInHtml(const QString &html) {
    static const QRegularExpression flvRe(
        QStringLiteral(R"(https?://[^\s"'\\]+\.flv[^\s"'\\]*)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = flvRe.match(html);
    if (m.hasMatch()) {
        return m.captured(0);
    }
    return {};
}

static QString pickFlvFromApiMap(const QJsonObject &flvMap) {
    static const char *keys[] = {"ORIGIN", "FULL_HD1", "HD1", "SD1", "SD2"};
    for (const char *k : keys) {
        const QString u = flvMap.value(QString::fromLatin1(k)).toString();
        if (!u.isEmpty()) {
            return u;
        }
    }
    for (auto it = flvMap.begin(); it != flvMap.end(); ++it) {
        const QString u = it.value().toString();
        if (!u.isEmpty()) {
            return u;
        }
    }
    return {};
}

static bool parseRoomRedirect(const QString &redirectUrl, QString *roomId, QString *userId, QString *outError) {
    const QUrl url(redirectUrl);
    const QUrlQuery q(url);
    QString rid = q.queryItemValue(QStringLiteral("roomId"));
    if (rid.isEmpty()) {
        rid = q.queryItemValue(QStringLiteral("liveId"));
    }
    if (rid.isEmpty()) {
        const QString path = url.path();
        const int slash = path.lastIndexOf(QLatin1Char('/'));
        if (slash >= 0) {
            rid = path.mid(slash + 1);
        }
    }
    QString uid = q.queryItemValue(QStringLiteral("user_id"));
    if (uid.isEmpty()) {
        uid = q.queryItemValue(QStringLiteral("sec_user_id"));
    }
    if (rid.isEmpty() || uid.isEmpty()) {
        if (outError != nullptr) {
            *outError = QStringLiteral("短链重定向中未能解析 room_id / user_id");
        }
        return false;
    }
    *roomId = rid;
    *userId = uid;
    return true;
}

static QString resolveViaLivePage(QNetworkAccessManager *nam, const QString &pageUrl, QString *outError) {
    QUrl u(pageUrl.trimmed());
    if (!u.isValid()) {
        if (outError != nullptr) {
            *outError = QStringLiteral("无效的 URL");
        }
        return {};
    }
    QNetworkRequest req = makeRequest(u, QByteArray(kDesktopUa));
    req.setRawHeader("Referer", "https://live.douyin.com/");
    const QByteArray html = syncGet(nam, req, outError);
    if (html.isEmpty()) {
        if (outError != nullptr && outError->isEmpty()) {
            *outError = QStringLiteral("获取直播间页面失败");
        }
        return {};
    }
    const QString htmlStr = QString::fromUtf8(html);

    const QString dataConfig = extractDataConfigFromHtml(htmlStr);
    if (!dataConfig.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(dataConfig.toUtf8());
        if (doc.isObject()) {
            const QJsonObject streamData = streamDataFromPageConfig(doc.object());
            const QString flv = flvFromStreamDataObj(streamData);
            if (!flv.isEmpty()) {
                return normalizeFlvUrl(flv);
            }
        }
    }

    const QString fallback = firstFlvInHtml(htmlStr);
    if (!fallback.isEmpty()) {
        return normalizeFlvUrl(fallback);
    }

    if (outError != nullptr) {
        *outError = QStringLiteral("未能从直播间页面解析出流地址（主播可能未开播）");
    }
    return {};
}

static QString resolveViaShortLinkApi(QNetworkAccessManager *nam, const QString &shortUrl, QString *outError) {
    QNetworkRequest req = makeRequest(QUrl(shortUrl.trimmed()), QByteArray(kMobileUa));
    req.setRawHeader(
        "Cookie",
        "s_v_web_id=verify_lk07kv74_QZYCUApD_xhiB_405x_Ax51_GYO9bUIyZQVf");
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QNetworkReply *reply = nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(kHttpTimeoutMs);
    loop.exec();
    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        if (outError != nullptr) {
            *outError = QStringLiteral("短链请求超时");
        }
        return {};
    }
    const QUrl finalUrl = reply->url();
    reply->deleteLater();

    if (!finalUrl.toString().contains(QStringLiteral("reflow/"))) {
        if (outError != nullptr) {
            *outError = QStringLiteral("短链未指向直播间（可能为用户主页）");
        }
        return {};
    }

    QString roomId;
    QString userId;
    if (!parseRoomRedirect(finalUrl.toString(), &roomId, &userId, outError)) {
        return {};
    }

    QUrlQuery params;
    params.addQueryItem(QStringLiteral("verifyFp"), QStringLiteral("verify_lxj5zv70_7szNlAB7_pxNY_48Vh_ALKF_GA1Uf3yteoOY"));
    params.addQueryItem(QStringLiteral("type_id"), QStringLiteral("0"));
    params.addQueryItem(QStringLiteral("live_id"), QStringLiteral("1"));
    params.addQueryItem(QStringLiteral("room_id"), roomId);
    params.addQueryItem(QStringLiteral("sec_user_id"), userId);
    params.addQueryItem(QStringLiteral("version_code"), QStringLiteral("99.99.99"));
    params.addQueryItem(QStringLiteral("app_id"), QStringLiteral("1128"));

    QUrl apiUrl(QStringLiteral("https://webcast.amemv.com/webcast/room/reflow/info/"));
    apiUrl.setQuery(params);

    QNetworkRequest apiReq = makeRequest(apiUrl, QByteArray(kDesktopUa));
    apiReq.setRawHeader("Referer", "https://live.douyin.com/");
    const QByteArray body = syncGet(nam, apiReq, outError);
    if (body.isEmpty()) {
        return {};
    }

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        if (outError != nullptr) {
            *outError = QStringLiteral("直播间 API 返回格式异常");
        }
        return {};
    }
    const QJsonObject root = doc.object();
    const QJsonObject room = root.value(QStringLiteral("data")).toObject().value(QStringLiteral("room")).toObject();
    if (room.isEmpty()) {
        if (outError != nullptr) {
            *outError = QStringLiteral("未能获取直播间数据");
        }
        return {};
    }

  if (room.value(QStringLiteral("status")).toInt(4) != 2) {
        if (outError != nullptr) {
            *outError = QStringLiteral("主播可能未开播");
        }
        return {};
    }

    const QJsonObject streamUrl = room.value(QStringLiteral("stream_url")).toObject();
    QString flv = pickFlvFromApiMap(streamUrl.value(QStringLiteral("flv_pull_url")).toObject());

    const QJsonObject liveCore = streamUrl.value(QStringLiteral("live_core_sdk_data")).toObject();
    const QJsonObject pullData = liveCore.value(QStringLiteral("pull_data")).toObject();
    const QString streamDataStr = pullData.value(QStringLiteral("stream_data")).toString();
    if (!streamDataStr.isEmpty()) {
        const QJsonDocument sd = QJsonDocument::fromJson(streamDataStr.toUtf8());
        if (sd.isObject()) {
            const QJsonObject origin =
                sd.object().value(QStringLiteral("data")).toObject()
                    .value(QStringLiteral("origin")).toObject()
                    .value(QStringLiteral("main")).toObject();
            const QString originFlv = origin.value(QStringLiteral("flv")).toString();
            if (!originFlv.isEmpty()) {
                flv = originFlv;
            }
        }
    }

    if (flv.isEmpty()) {
        if (outError != nullptr) {
            *outError = QStringLiteral("未能解析出直播流地址");
        }
        return {};
    }

    if (outError != nullptr) {
        outError->clear();
    }
    return normalizeFlvUrl(flv);
}

static QString tryXiaohongshuFastPath(const QString &input) {
    const QString u = input.trimmed();
    if (!u.contains(QStringLiteral("xiaohongshu"), Qt::CaseInsensitive)) {
        return {};
    }
    static const QRegularExpression re(
        QStringLiteral(R"(livestream/(?:[^/?#]+/)*([0-9]+))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(u);
    if (!m.hasMatch()) {
        return {};
    }
    return QStringLiteral("http://live.xhscdn.com/live/%1.flv").arg(m.captured(1));
}

} // namespace

static QString trimUrlTail(QString url) {
    while (!url.isEmpty()) {
        const QChar c = url.back();
        if (c.isLetterOrNumber() || c == QLatin1Char('/') || c == QLatin1Char('-') ||
            c == QLatin1Char('_') || c == QLatin1Char('=') || c == QLatin1Char('&') ||
            c == QLatin1Char('?') || c == QLatin1Char('%')) {
            break;
        }
        if (QStringLiteral(".,;:!?)]}>'\"").contains(c)) {
            url.chop(1);
        } else {
            break;
        }
    }
    return url;
}

static int streamUrlPriority(const QString &url) {
    const QString u = url.toLower();
    if (u.contains(QStringLiteral("v.douyin.com"))) {
        return 100;
    }
    if (u.contains(QStringLiteral("live.douyin.com"))) {
        return 95;
    }
    if (u.contains(QStringLiteral("xiaohongshu")) && u.contains(QStringLiteral("livestream"))) {
        return 90;
    }
    if (u.contains(QStringLiteral(".flv")) || u.contains(QStringLiteral("douyincdn.com")) ||
        u.contains(QStringLiteral("xhscdn.com")) || u.contains(QStringLiteral("pull-flv"))) {
        return 80;
    }
    if (u.contains(QStringLiteral(".m3u8"))) {
        return 75;
    }
    if (u.startsWith(QStringLiteral("http://")) || u.startsWith(QStringLiteral("https://"))) {
        return 10;
    }
    return 0;
}

QString streamProviderExtractUrl(const QString &raw) {
    const QString t = raw.trimmed();
    if (t.isEmpty()) {
        return t;
    }

    static const QRegularExpression urlRe(
        QStringLiteral(R"((https?://[^\s<>"']+))"),
        QRegularExpression::CaseInsensitiveOption);

    QString best;
    int bestPriority = -1;

    auto consider = [&](const QString &candidate) {
        const QString u = trimUrlTail(candidate.trimmed());
        if (!u.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) &&
            !u.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
            return;
        }
        const int p = streamUrlPriority(u);
        if (p > bestPriority || (p == bestPriority && best.isEmpty())) {
            bestPriority = p;
            best = u;
        }
    };

    QRegularExpressionMatchIterator it = urlRe.globalMatch(t);
    while (it.hasNext()) {
        consider(it.next().captured(1));
    }

    if (!best.isEmpty()) {
        return best;
    }

    return t;
}

bool streamProviderIsDirectStreamUrl(const QString &url) {
    const QString u = url.trimmed().toLower();
    if (u.isEmpty()) {
        return false;
    }
    if (u.startsWith(QStringLiteral("rtmp://")) || u.startsWith(QStringLiteral("rtmps://"))) {
        return true;
    }
    if (u.contains(QStringLiteral(".flv")) || u.contains(QStringLiteral("pull-flv"))) {
        return true;
    }
    if (u.contains(QStringLiteral(".m3u8"))) {
        return true;
    }
    return false;
}

bool streamProviderIsDouyinPageUrl(const QString &url) {
    const QString h = hostOf(url.trimmed());
    if (h.isEmpty()) {
        return false;
    }
    return h == QStringLiteral("live.douyin.com") || h == QStringLiteral("v.douyin.com") ||
        h.endsWith(QStringLiteral(".douyin.com")) && url.contains(QStringLiteral("douyin"));
}

bool streamProviderResolve(const QString &input, StreamResolveResult *out, QString *outError) {
    if (out != nullptr) {
        *out = {};
    }
    const QString trimmed = streamProviderExtractUrl(input);
    if (trimmed.isEmpty()) {
        if (outError != nullptr) {
            *outError = QStringLiteral("请输入直播间或流地址");
        }
        return false;
    }

    const QString xhsFlv = tryXiaohongshuFastPath(trimmed);
    if (!xhsFlv.isEmpty()) {
        if (out != nullptr) {
            out->streamUrl = xhsFlv;
            out->qualityHint = QStringLiteral("小红书源流");
        }
        return true;
    }

    if (streamProviderIsDirectStreamUrl(trimmed)) {
        if (out != nullptr) {
            out->streamUrl = normalizeFlvUrl(trimmed);
            out->qualityHint = QStringLiteral("直连流");
        }
        return true;
    }

    QNetworkAccessManager nam;
    QString err;
    QString flv;

    const QString host = hostOf(trimmed);
    if (host == QStringLiteral("v.douyin.com")) {
        flv = resolveViaShortLinkApi(&nam, trimmed, &err);
    } else if (host == QStringLiteral("live.douyin.com") || host.contains(QStringLiteral("douyin"))) {
        flv = resolveViaLivePage(&nam, trimmed, &err);
    } else {
        if (outError != nullptr) {
            *outError = QStringLiteral(
                "无法识别的地址。请粘贴抖音/小红书直播间链接，或可直接播放的 flv/m3u8 地址。"
            );
        }
        return false;
    }

    if (flv.isEmpty()) {
        if (outError != nullptr) {
            *outError = err.isEmpty() ? QStringLiteral("解析抖音直播流失败") : err;
        }
        return false;
    }

    if (out != nullptr) {
        out->streamUrl = flv;
        out->qualityHint = QStringLiteral("源流");
    }
    return true;
}
