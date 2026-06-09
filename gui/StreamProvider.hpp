#ifndef STREAM_PROVIDER_HPP
#define STREAM_PROVIDER_HPP

#include <QString>

struct StreamResolveResult {
    QString streamUrl;
    QString anchorName;
    QString qualityHint;
};

/** 从分享文案等混杂文本中提取 http(s) 链接（优先 v.douyin.com 短链）；无则返回 trim 后的原文。 */
QString streamProviderExtractUrl(const QString &raw);

/** 是否为可直接交给 FFmpeg 的流地址（flv/m3u8/rtmp 等）。 */
bool streamProviderIsDirectStreamUrl(const QString &url);

/** 是否为抖音直播间/短链页面 URL。 */
bool streamProviderIsDouyinPageUrl(const QString &url);

/**
 * 将用户输入解析为可拉流的 URL。
 * - 已是流地址：原样返回
 * - xiaohongshu.com/livestream/.../{id}：零请求拼接 xhscdn FLV
 * - live.douyin.com / v.douyin.com：HTTP 解析后返回 FLV（优先 H.264）
 */
bool streamProviderResolve(const QString &input, StreamResolveResult *out, QString *outError);

#endif
