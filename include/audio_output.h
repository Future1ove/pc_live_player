#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#ifdef __cplusplus
}
#endif

typedef struct AVFrame AVFrame;

typedef struct AudioOutput {
    SDL_AudioDeviceID device_id;
    SDL_AudioSpec obtained_spec;
    SwrContext *swr_ctx;
    uint8_t *resample_buffer;
    int resample_buffer_size;
    int dst_channels;
    int dst_sample_rate;
    enum AVSampleFormat dst_sample_fmt;
    int src_channels;
    int src_sample_rate;
    enum AVSampleFormat src_sample_fmt;
    AVChannelLayout src_ch_layout;
    bool started;
    /** 已入队音频在流时间轴上的结束 PTS（秒） */
    double queued_pts_end;
    bool pts_valid;
} AudioOutput;

bool audio_output_init(AudioOutput *output);
bool audio_output_open(AudioOutput *output, int preferred_sample_rate);
/**
 * 将解码后的 PCM 写入 SDL 队列。
 * @param pts_seconds 流时间轴 PTS（秒）；传入 <0 表示无效，仅入队不计入同步时钟。
 * @return false 表示本帧被丢弃（背压）
 */
bool audio_output_queue_frame(AudioOutput *output, const AVFrame *frame, double pts_seconds);
/** 当前音频播放位置（秒，流时间轴）；无有效 PTS 时返回 -1 */
double audio_output_get_playback_pts(const AudioOutput *output);
/** 已排队待播放时长（秒） */
double audio_output_get_queued_seconds(const AudioOutput *output);
void audio_output_clear(AudioOutput *output);
void audio_output_close(AudioOutput *output);
void audio_output_destroy(AudioOutput *output);

#endif
