#include "audio_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#ifdef __cplusplus
}
#endif

static int get_frame_channels(const AVFrame *frame) {
    if (frame->ch_layout.nb_channels > 0) {
        return frame->ch_layout.nb_channels;
    }
    return 0;
}

static bool get_frame_channel_layout(const AVFrame *frame, AVChannelLayout *layout_out) {
    if (frame == 0 || layout_out == 0) {
        return false;
    }

    if (frame->ch_layout.nb_channels > 0) {
        if (av_channel_layout_copy(layout_out, &frame->ch_layout) == 0) {
            return true;
        }
    }

    if (get_frame_channels(frame) > 0) {
        av_channel_layout_default(layout_out, get_frame_channels(frame));
        return true;
    }

    return false;
}

static int bytes_per_second(const AudioOutput *output) {
    const int bytes_per_sample = av_get_bytes_per_sample(output->dst_sample_fmt);
    if (bytes_per_sample <= 0 || output->dst_sample_rate <= 0 || output->dst_channels <= 0) {
        return 0;
    }
    return output->dst_sample_rate * output->dst_channels * bytes_per_sample;
}

static bool ensure_resample_buffer(AudioOutput *output, int required_size) {
    uint8_t *new_buffer;

    if (required_size <= output->resample_buffer_size) {
        return true;
    }

    new_buffer = (uint8_t *)av_realloc(output->resample_buffer, (size_t)required_size);
    if (new_buffer == 0) {
        return false;
    }

    output->resample_buffer = new_buffer;
    output->resample_buffer_size = required_size;
    return true;
}

static bool configure_resampler(AudioOutput *output, const AVFrame *frame) {
    int src_channels = get_frame_channels(frame);
    AVChannelLayout src_layout;
    AVChannelLayout dst_layout;
    int ret;

    memset(&src_layout, 0, sizeof(src_layout));
    memset(&dst_layout, 0, sizeof(dst_layout));

    if (src_channels <= 0 || !get_frame_channel_layout(frame, &src_layout)) {
        return false;
    }

    if (output->swr_ctx != 0) {
        swr_free(&output->swr_ctx);
    }

    av_channel_layout_default(&dst_layout, output->dst_channels);
    ret = swr_alloc_set_opts2(
        &output->swr_ctx,
        &dst_layout,
        output->dst_sample_fmt,
        output->dst_sample_rate,
        &src_layout,
        (enum AVSampleFormat)frame->format,
        frame->sample_rate,
        0,
        0
    );
    av_channel_layout_uninit(&dst_layout);
    if (ret < 0 || output->swr_ctx == 0) {
        av_channel_layout_uninit(&src_layout);
        return false;
    }

    ret = swr_init(output->swr_ctx);
    if (ret < 0) {
        swr_free(&output->swr_ctx);
        av_channel_layout_uninit(&src_layout);
        return false;
    }

    output->src_channels = src_channels;
    output->src_sample_rate = frame->sample_rate;
    output->src_sample_fmt = (enum AVSampleFormat)frame->format;
    av_channel_layout_uninit(&output->src_ch_layout);
    if (av_channel_layout_copy(&output->src_ch_layout, &src_layout) != 0) {
        av_channel_layout_uninit(&src_layout);
        swr_free(&output->swr_ctx);
        return false;
    }
    av_channel_layout_uninit(&src_layout);
    return true;
}

bool audio_output_init(AudioOutput *output) {
    if (output == 0) {
        return false;
    }

    memset(output, 0, sizeof(*output));
    output->dst_channels = 2;
    output->dst_sample_rate = 48000;
    output->dst_sample_fmt = AV_SAMPLE_FMT_S16;
    output->src_channels = 0;
    output->src_sample_rate = 0;
    output->src_sample_fmt = AV_SAMPLE_FMT_NONE;
    memset(&output->src_ch_layout, 0, sizeof(output->src_ch_layout));
    return true;
}

bool audio_output_open(AudioOutput *output, int preferred_sample_rate) {
    SDL_AudioSpec desired_spec;

    if (output == 0) {
        return false;
    }

    memset(&desired_spec, 0, sizeof(desired_spec));
    desired_spec.freq = preferred_sample_rate > 0 ? preferred_sample_rate : output->dst_sample_rate;
    desired_spec.format = AUDIO_S16SYS;
    desired_spec.channels = (Uint8)output->dst_channels;
    /* 较小缓冲利于直播低延迟与音画同步 */
    desired_spec.samples = 1024;
    desired_spec.callback = 0;

    output->device_id = SDL_OpenAudioDevice(0, 0, &desired_spec, &output->obtained_spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (output->device_id == 0) {
        fprintf(stderr, "打开 SDL 音频设备失败: %s\n", SDL_GetError());
        return false;
    }

    output->dst_channels = output->obtained_spec.channels;
    output->dst_sample_rate = output->obtained_spec.freq;
    SDL_PauseAudioDevice(output->device_id, 0);
    output->started = true;
    output->queued_pts_end = 0.0;
    output->pts_valid = false;
    return true;
}

double audio_output_get_queued_seconds(const AudioOutput *output) {
    int bps;

    if (output == 0 || output->device_id == 0) {
        return 0.0;
    }

    bps = bytes_per_second(output);
    if (bps <= 0) {
        return 0.0;
    }

    return (double)SDL_GetQueuedAudioSize(output->device_id) / (double)bps;
}

double audio_output_get_playback_pts(const AudioOutput *output) {
    if (output == 0 || !output->pts_valid) {
        return -1.0;
    }

    return output->queued_pts_end - audio_output_get_queued_seconds(output);
}

bool audio_output_queue_frame(AudioOutput *output, const AVFrame *frame, double pts_seconds) {
    int src_channels;
    int dst_nb_samples;
    int bytes_per_sample;
    int buffer_size;
    int converted_samples;
    int queued_bytes;
    int bps;

    if (output == 0 || frame == 0 || !output->started) {
        return false;
    }

    src_channels = get_frame_channels(frame);
    if (frame->nb_samples <= 0 || src_channels <= 0 || frame->sample_rate <= 0) {
        return false;
    }

    if (output->swr_ctx == 0 ||
        output->src_channels != src_channels ||
        output->src_sample_rate != frame->sample_rate ||
        output->src_sample_fmt != (enum AVSampleFormat)frame->format ||
        av_channel_layout_compare(&output->src_ch_layout, &frame->ch_layout) != 0) {
        if (!configure_resampler(output, frame)) {
            fprintf(stderr, "初始化音频重采样器失败。\n");
            return false;
        }
    }

    dst_nb_samples = (int)av_rescale_rnd(
        swr_get_delay(output->swr_ctx, frame->sample_rate) + frame->nb_samples,
        output->dst_sample_rate,
        frame->sample_rate,
        AV_ROUND_UP
    );
    bytes_per_sample = av_get_bytes_per_sample(output->dst_sample_fmt);
    buffer_size = dst_nb_samples * output->dst_channels * bytes_per_sample;

    if (!ensure_resample_buffer(output, buffer_size)) {
        return false;
    }

    converted_samples = swr_convert(
        output->swr_ctx,
        &output->resample_buffer,
        dst_nb_samples,
        (const uint8_t * const *)frame->extended_data,
        frame->nb_samples
    );
    if (converted_samples < 0) {
        fprintf(stderr, "音频重采样失败。\n");
        return false;
    }

    queued_bytes = converted_samples * output->dst_channels * bytes_per_sample;
    bps = bytes_per_second(output);

    if (bps > 0) {
        const int queued = (int)SDL_GetQueuedAudioSize(output->device_id);
        const double queued_sec = (double)queued / (double)bps;
        const double frame_sec = (double)queued_bytes / (double)bps;
        /* 目标缓冲约 80–200ms：过长则丢帧，避免音画严重脱节 */
        if (queued_sec > 0.35) {
            return true;
        }
        if (queued_sec > 0.22) {
            SDL_ClearQueuedAudio(output->device_id);
            if (pts_seconds >= 0.0) {
                output->queued_pts_end = pts_seconds + frame_sec;
                output->pts_valid = true;
            }
        } else if (pts_seconds >= 0.0) {
            if (!output->pts_valid) {
                output->queued_pts_end = pts_seconds + frame_sec;
                output->pts_valid = true;
            } else {
                output->queued_pts_end = pts_seconds + frame_sec;
            }
        }
    }

    if (SDL_QueueAudio(output->device_id, output->resample_buffer, (Uint32)queued_bytes) != 0) {
        fprintf(stderr, "写入 SDL 音频队列失败: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

void audio_output_clear(AudioOutput *output) {
    if (output == 0 || output->device_id == 0) {
        return;
    }

    SDL_ClearQueuedAudio(output->device_id);
    output->queued_pts_end = 0.0;
    output->pts_valid = false;
}

void audio_output_close(AudioOutput *output) {
    if (output == 0) {
        return;
    }

    if (output->device_id != 0) {
        SDL_ClearQueuedAudio(output->device_id);
        SDL_CloseAudioDevice(output->device_id);
        output->device_id = 0;
    }

    output->started = false;
    output->queued_pts_end = 0.0;
    output->pts_valid = false;
    output->src_channels = 0;
    output->src_sample_rate = 0;
    output->src_sample_fmt = AV_SAMPLE_FMT_NONE;
    av_channel_layout_uninit(&output->src_ch_layout);

    if (output->swr_ctx != 0) {
        swr_free(&output->swr_ctx);
    }
}

void audio_output_destroy(AudioOutput *output) {
    if (output == 0) {
        return;
    }

    audio_output_close(output);
    av_freep(&output->resample_buffer);
    output->resample_buffer_size = 0;
}
