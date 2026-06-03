#ifndef FFMPEG_STREAM_H
#define FFMPEG_STREAM_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/pixfmt.h>
#ifdef __cplusplus
}
#endif

#include "app_types.h"
#include "audio_output.h"

typedef struct AVFormatContext AVFormatContext;
typedef struct AVCodecContext AVCodecContext;
typedef struct AVCodec AVCodec;
typedef struct AVBufferRef AVBufferRef;
typedef struct AVFrame AVFrame;
typedef struct AVPacket AVPacket;
typedef struct SwsContext SwsContext;

#define FFMPEG_AUDIO_PKT_QUEUE_CAP 32

typedef struct FFmpegStreamFetcher {
    AppConfig config;
    SDL_Thread *thread;
    SDL_mutex *frame_mutex;
    bool running;
    bool stop_requested;
    bool frame_ready;
    char last_error[APP_STATUS_CAPACITY];
    char status_text[APP_STATUS_CAPACITY];

    uint8_t *latest_bgr;
    uint8_t *work_bgr;
    size_t latest_bgr_size;
    uint64_t latest_sequence;

    AVFormatContext *format_ctx;
    AVCodecContext *video_codec_ctx;
    const AVCodec *video_codec;
    AVBufferRef *video_hw_device_ctx;
    enum AVPixelFormat video_hw_pix_fmt;
    AVFrame *video_frame;
    AVFrame *video_drain_hold;
    AVFrame *video_transfer_frame;
    AVCodecContext *audio_codec_ctx;
    const AVCodec *audio_codec;
    AVFrame *audio_frame;
    AVPacket *packet;
    SwsContext *sws_ctx;
    int video_stream_index;
    int audio_stream_index;
    int source_width;
    int source_height;
    int scaled_width;
    int scaled_height;
    /** 首帧前尚未分配 BGR 画布（与 AppConfig.use_native_resolution 配套） */
    bool native_resolution_pending;

    AudioOutput audio_output;

    /** 待解码音频包（demux 与视频解码解耦，避免视频卡顿时音频饿死） */
    AVPacket *audio_pkt_queue[FFMPEG_AUDIO_PKT_QUEUE_CAP];
    int audio_pkt_count;
} FFmpegStreamFetcher;

bool ffmpeg_stream_init(FFmpegStreamFetcher *fetcher, const AppConfig *config);
bool ffmpeg_stream_start(FFmpegStreamFetcher *fetcher);
bool ffmpeg_stream_get_latest_frame(
    FFmpegStreamFetcher *fetcher,
    uint8_t *out_buffer,
    size_t out_buffer_size,
    uint64_t *out_sequence
);
/** 当前解码画布宽高（原生模式下首帧后才有效） */
void ffmpeg_stream_get_canvas_size(const FFmpegStreamFetcher *fetcher, int *out_w, int *out_h);
void ffmpeg_stream_stop(FFmpegStreamFetcher *fetcher);
void ffmpeg_stream_destroy(FFmpegStreamFetcher *fetcher);

#endif
