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
} AudioOutput;

bool audio_output_init(AudioOutput *output);
bool audio_output_open(AudioOutput *output, int preferred_sample_rate);
/** 返回 false 表示本帧被丢弃（用于背压，避免清空队列造成爆音） */
bool audio_output_queue_frame(AudioOutput *output, const AVFrame *frame);
void audio_output_clear(AudioOutput *output);
void audio_output_close(AudioOutput *output);
void audio_output_destroy(AudioOutput *output);

#endif
