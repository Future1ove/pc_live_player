#include "ffmpeg_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

static bool url_seems_flv(const char *url) {
    const char *ext;

    if (url == 0) {
        return false;
    }

    ext = strstr(url, ".flv");
    if (ext == 0) {
        return false;
    }

    return ext[4] == '\0' || ext[4] == '?' || ext[4] == '&';
}

static void ffmpeg_error_string(int errnum, char *buffer, size_t buffer_size) {
    if (buffer == 0 || buffer_size == 0U) {
        return;
    }

    if (av_strerror(errnum, buffer, buffer_size) < 0) {
        snprintf(buffer, buffer_size, "ffmpeg error %d", errnum);
    }
}

static void set_status(FFmpegStreamFetcher *fetcher, const char *text) {
    if (fetcher == 0 || text == 0) {
        return;
    }
    snprintf(fetcher->status_text, sizeof(fetcher->status_text), "%s", text);
}

static void set_error(FFmpegStreamFetcher *fetcher, const char *prefix, int errnum) {
    char errbuf[128];

    if (fetcher == 0 || prefix == 0) {
        return;
    }

    ffmpeg_error_string(errnum, errbuf, sizeof(errbuf));
    snprintf(fetcher->last_error, sizeof(fetcher->last_error), "%s: %s", prefix, errbuf);
    fprintf(stderr, "%s\n", fetcher->last_error);
}

static enum AVPixelFormat select_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    const enum AVPixelFormat *current = pix_fmts;
    FFmpegStreamFetcher *fetcher = (FFmpegStreamFetcher *)ctx->opaque;

    while (current != 0 && *current != AV_PIX_FMT_NONE) {
        if (*current == fetcher->video_hw_pix_fmt) {
            return *current;
        }
        ++current;
    }

    fprintf(stderr, "D3D11VA 硬件像素格式不可用，回退到软件解码。\n");
    return pix_fmts[0];
}

static bool init_video_hw_decoder(FFmpegStreamFetcher *fetcher) {
    enum AVHWDeviceType device_type = av_hwdevice_find_type_by_name("d3d11va");
    const AVCodecHWConfig *config = 0;
    int i = 0;

    if (device_type == AV_HWDEVICE_TYPE_NONE) {
        return false;
    }

    while ((config = avcodec_get_hw_config(fetcher->video_codec, i)) != 0) {
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
            config->device_type == device_type) {
            fetcher->video_hw_pix_fmt = config->pix_fmt;
            break;
        }
        ++i;
    }

    if (config == 0) {
        return false;
    }

    if (av_hwdevice_ctx_create(&fetcher->video_hw_device_ctx, device_type, 0, 0, 0) < 0) {
        return false;
    }

    fetcher->video_codec_ctx->opaque = fetcher;
    fetcher->video_codec_ctx->get_format = select_hw_format;
    fetcher->video_codec_ctx->hw_device_ctx = av_buffer_ref(fetcher->video_hw_device_ctx);
    return fetcher->video_codec_ctx->hw_device_ctx != 0;
}

static bool open_stream_decoder(
    FFmpegStreamFetcher *fetcher,
    int stream_index,
    AVCodecContext **out_codec_ctx,
    const AVCodec **out_codec,
    bool use_hw_for_video) {
    int ret;
    enum AVCodecID codec_id;

    *out_codec = 0;
    *out_codec_ctx = 0;

    codec_id = fetcher->format_ctx->streams[stream_index]->codecpar->codec_id;
    *out_codec = avcodec_find_decoder(codec_id);
    if (*out_codec == 0) {
        return false;
    }

    *out_codec_ctx = avcodec_alloc_context3(*out_codec);
    if (*out_codec_ctx == 0) {
        return false;
    }

    ret = avcodec_parameters_to_context(
        *out_codec_ctx,
        fetcher->format_ctx->streams[stream_index]->codecpar
    );
    if (ret < 0) {
        set_error(fetcher, "拷贝编解码参数失败", ret);
        avcodec_free_context(out_codec_ctx);
        return false;
    }

    if (use_hw_for_video && fetcher->config.use_gpu) {
        (*out_codec_ctx)->thread_count = 1;
    } else {
        /* 软件解码（尤其 HEVC 高码率 FLV）用多线程，减轻单路拖死音频 */
        (*out_codec_ctx)->thread_count = 4;
    }
    (*out_codec_ctx)->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    if (fetcher->config.fast_decode) {
        (*out_codec_ctx)->flags2 |= AV_CODEC_FLAG2_FAST;
    }

    if (use_hw_for_video && fetcher->config.use_gpu && !init_video_hw_decoder(fetcher)) {
        fprintf(stderr, "D3D11VA 硬件解码初始化失败，自动回退 CPU。\n");
        fetcher->config.use_gpu = false;
    }

    if (use_hw_for_video && fetcher->config.fast_decode) {
        (*out_codec_ctx)->skip_frame = AVDISCARD_NONREF;
        (*out_codec_ctx)->skip_loop_filter = AVDISCARD_NONREF;
    }

    ret = avcodec_open2(*out_codec_ctx, *out_codec, 0);
    if (ret < 0) {
        set_error(fetcher, "打开解码器失败", ret);
        avcodec_free_context(out_codec_ctx);
        return false;
    }

    return true;
}

static void calculate_scaled_size(FFmpegStreamFetcher *fetcher, int src_width, int src_height) {
    double scale_x;
    double scale_y;
    double scale;

    fetcher->source_width = src_width;
    fetcher->source_height = src_height;

    scale_x = (double)fetcher->config.output_width / (double)src_width;
    scale_y = (double)fetcher->config.output_height / (double)src_height;
    scale = scale_x < scale_y ? scale_x : scale_y;

    if (scale <= 0.0) {
        fetcher->scaled_width = src_width;
        fetcher->scaled_height = src_height;
        return;
    }

    fetcher->scaled_width = (int)((double)src_width * scale);
    fetcher->scaled_height = (int)((double)src_height * scale);

    if (fetcher->scaled_width < 2) {
        fetcher->scaled_width = 2;
    }
    if (fetcher->scaled_height < 2) {
        fetcher->scaled_height = 2;
    }

    fetcher->scaled_width &= ~1;
    fetcher->scaled_height &= ~1;
}

static void clear_bgr_letterbox(
    uint8_t *buf,
    int out_w,
    int out_h,
    int pad_x,
    int pad_y,
    int scaled_w,
    int scaled_h) {
    const int stride = out_w * 3;
    int y;
    const int bottom_y = pad_y + scaled_h;

    for (y = 0; y < pad_y && y < out_h; ++y) {
        memset(buf + (size_t)y * (size_t)stride, 0, (size_t)stride);
    }
    for (y = pad_y; y < bottom_y && y < out_h; ++y) {
        uint8_t *row = buf + (size_t)y * (size_t)stride;
        if (pad_x > 0) {
            memset(row, 0, (size_t)pad_x * 3U);
        }
        if (pad_x + scaled_w < out_w) {
            const int right = pad_x + scaled_w;
            memset(row + (size_t)right * 3U, 0, (size_t)(out_w - right) * 3U);
        }
    }
    for (y = bottom_y; y < out_h; ++y) {
        memset(buf + (size_t)y * (size_t)stride, 0, (size_t)stride);
    }
}

static bool ensure_sws_context(FFmpegStreamFetcher *fetcher, const AVFrame *frame) {
    if (fetcher->sws_ctx != 0 &&
        fetcher->source_width == frame->width &&
        fetcher->source_height == frame->height) {
        return true;
    }

    if (fetcher->sws_ctx != 0) {
        sws_freeContext(fetcher->sws_ctx);
        fetcher->sws_ctx = 0;
    }

    calculate_scaled_size(fetcher, frame->width, frame->height);
    {
        /* 直播预览优先流畅：双线性足够，BICUBIC 在 1080p 上易与音频争抢 CPU */
        int sws_flags = SWS_FAST_BILINEAR;
        if (fetcher->config.fast_decode) {
            sws_flags = SWS_POINT;
        }
        fetcher->sws_ctx = sws_getContext(
            frame->width,
            frame->height,
            (enum AVPixelFormat)frame->format,
            fetcher->scaled_width,
            fetcher->scaled_height,
            AV_PIX_FMT_BGR24,
            sws_flags,
            0,
            0,
            0
        );
    }

    return fetcher->sws_ctx != 0;
}

static bool convert_frame_to_canvas(FFmpegStreamFetcher *fetcher, const AVFrame *frame) {
    uint8_t *dst_data[4] = {0};
    int dst_linesize[4] = {0};
    int pad_x;
    int pad_y;

    if (fetcher->native_resolution_pending) {
        int src_w = frame->width & ~1;
        int src_h = frame->height & ~1;
        if (src_w < 2) {
            src_w = 2;
        }
        if (src_h < 2) {
            src_h = 2;
        }
        /* 画布尺寸完全跟随源流（仅做偶数对齐） */
        fetcher->config.output_width = src_w;
        fetcher->config.output_height = src_h;
        fetcher->latest_bgr_size =
            (size_t)fetcher->config.output_width * (size_t)fetcher->config.output_height * 3U;
        free(fetcher->latest_bgr);
        free(fetcher->work_bgr);
        fetcher->latest_bgr = (uint8_t *)malloc(fetcher->latest_bgr_size);
        fetcher->work_bgr = (uint8_t *)malloc(fetcher->latest_bgr_size);
        if (fetcher->latest_bgr == 0 || fetcher->work_bgr == 0) {
            set_error(fetcher, "分配原生分辨率画布失败", -1);
            fetcher->latest_bgr_size = 0U;
            return false;
        }
        memset(fetcher->latest_bgr, 0, fetcher->latest_bgr_size);
        memset(fetcher->work_bgr, 0, fetcher->latest_bgr_size);
        fetcher->native_resolution_pending = false;
    }

    if (!ensure_sws_context(fetcher, frame)) {
        return false;
    }

    pad_x = (fetcher->config.output_width - fetcher->scaled_width) / 2;
    pad_y = (fetcher->config.output_height - fetcher->scaled_height) / 2;

    clear_bgr_letterbox(
        fetcher->work_bgr,
        fetcher->config.output_width,
        fetcher->config.output_height,
        pad_x,
        pad_y,
        fetcher->scaled_width,
        fetcher->scaled_height
    );

    dst_data[0] = fetcher->work_bgr +
        (size_t)pad_y * (size_t)(fetcher->config.output_width * 3) +
        (size_t)pad_x * 3U;
    dst_linesize[0] = fetcher->config.output_width * 3;

    sws_scale(
        fetcher->sws_ctx,
        (const uint8_t *const *)frame->data,
        frame->linesize,
        0,
        frame->height,
        dst_data,
        dst_linesize
    );

    SDL_LockMutex(fetcher->frame_mutex);
    {
        uint8_t *tmp = fetcher->latest_bgr;
        fetcher->latest_bgr = fetcher->work_bgr;
        fetcher->work_bgr = tmp;
        fetcher->latest_sequence += 1U;
        fetcher->frame_ready = true;
        if (frame->pts != AV_NOPTS_VALUE && fetcher->format_ctx != 0 &&
            fetcher->video_stream_index >= 0) {
            const AVRational tb =
                fetcher->format_ctx->streams[fetcher->video_stream_index]->time_base;
            fetcher->latest_video_pts_sec = (double)frame->pts * av_q2d(tb);
            fetcher->latest_video_pts_valid = true;
        }
    }
    SDL_UnlockMutex(fetcher->frame_mutex);
    return true;
}

static bool process_video_frame(FFmpegStreamFetcher *fetcher, AVFrame *frame) {
    AVFrame *source_frame = frame;
    int ret;

    if (fetcher->video_hw_device_ctx != 0 && frame->format == fetcher->video_hw_pix_fmt) {
        av_frame_unref(fetcher->video_transfer_frame);
        ret = av_hwframe_transfer_data(fetcher->video_transfer_frame, frame, 0);
        if (ret < 0) {
            set_error(fetcher, "D3D11VA 帧回传失败", ret);
            return false;
        }
        source_frame = fetcher->video_transfer_frame;
    }

    return convert_frame_to_canvas(fetcher, source_frame);
}

static bool process_audio_frame(FFmpegStreamFetcher *fetcher, AVFrame *frame) {
    double pts_seconds = -1.0;

    if (!fetcher->config.enable_audio) {
        return true;
    }

    if (frame->pts != AV_NOPTS_VALUE && fetcher->format_ctx != 0 &&
        fetcher->audio_stream_index >= 0) {
        const AVRational tb =
            fetcher->format_ctx->streams[fetcher->audio_stream_index]->time_base;
        pts_seconds = (double)frame->pts * av_q2d(tb);
    }

    return audio_output_queue_frame(&fetcher->audio_output, frame, pts_seconds);
}

static bool drain_decoder_frames(
    FFmpegStreamFetcher *fetcher,
    AVCodecContext *codec_ctx,
    AVFrame *frame,
    bool is_video) {
    int ret;

    if (is_video) {
        /* 每帧都入画布，便于预览帧率尽量跟源流 */
        while (!fetcher->stop_requested) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                return true;
            }
            if (ret < 0) {
                set_error(fetcher, "接收视频帧失败", ret);
                return false;
            }
            if (!process_video_frame(fetcher, frame)) {
                fprintf(stderr, "视频帧转换失败。\n");
            }
            av_frame_unref(frame);
        }
        return true;
    }

    while (!fetcher->stop_requested) {
        ret = avcodec_receive_frame(codec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return true;
        }
        if (ret < 0) {
            set_error(fetcher, "接收音频帧失败", ret);
            return false;
        }

        if (!process_audio_frame(fetcher, frame)) {
            fprintf(stderr, "音频帧输出失败。\n");
        }

        av_frame_unref(frame);
    }

    return true;
}

static int interrupt_callback(void *opaque) {
    FFmpegStreamFetcher *fetcher = (FFmpegStreamFetcher *)opaque;
    return fetcher != 0 && fetcher->stop_requested ? 1 : 0;
}

static void audio_pkt_queue_clear(FFmpegStreamFetcher *fetcher) {
    int i;

    if (fetcher == 0) {
        return;
    }

    for (i = 0; i < fetcher->audio_pkt_count; ++i) {
        if (fetcher->audio_pkt_queue[i] != 0) {
            av_packet_free(&fetcher->audio_pkt_queue[i]);
        }
    }
    fetcher->audio_pkt_count = 0;
}

static bool audio_pkt_queue_push(FFmpegStreamFetcher *fetcher, AVPacket *pkt) {
    AVPacket *copy;

    if (fetcher == 0 || pkt == 0) {
        return false;
    }

    if (fetcher->audio_pkt_count >= FFMPEG_AUDIO_PKT_QUEUE_CAP) {
        av_packet_free(&fetcher->audio_pkt_queue[0]);
        memmove(
            &fetcher->audio_pkt_queue[0],
            &fetcher->audio_pkt_queue[1],
            (size_t)(fetcher->audio_pkt_count - 1) * sizeof(fetcher->audio_pkt_queue[0])
        );
        fetcher->audio_pkt_count -= 1;
    }

    copy = av_packet_alloc();
    if (copy == 0) {
        return false;
    }
    if (av_packet_ref(copy, pkt) < 0) {
        av_packet_free(&copy);
        return false;
    }

    fetcher->audio_pkt_queue[fetcher->audio_pkt_count++] = copy;
    return true;
}

static bool process_audio_packet(FFmpegStreamFetcher *fetcher, AVPacket *pkt) {
    int ret;

    if (fetcher->audio_codec_ctx == 0 || pkt == 0) {
        return true;
    }

    ret = avcodec_send_packet(fetcher->audio_codec_ctx, pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        set_error(fetcher, "发送音频包到解码器失败", ret);
        return false;
    }
    return drain_decoder_frames(fetcher, fetcher->audio_codec_ctx, fetcher->audio_frame, false);
}

static void drain_pending_audio_packets(FFmpegStreamFetcher *fetcher) {
    int limit = 16;
    int processed = 0;

    while (fetcher != 0 && fetcher->audio_pkt_count > 0 && processed < limit &&
           !fetcher->stop_requested) {
        AVPacket *pkt = fetcher->audio_pkt_queue[0];
        memmove(
            &fetcher->audio_pkt_queue[0],
            &fetcher->audio_pkt_queue[1],
            (size_t)(fetcher->audio_pkt_count - 1) * sizeof(fetcher->audio_pkt_queue[0])
        );
        fetcher->audio_pkt_count -= 1;

        if (pkt != 0) {
            if (!process_audio_packet(fetcher, pkt)) {
                fprintf(stderr, "音频包解码失败。\n");
            }
            av_packet_free(&pkt);
        }
        processed += 1;
    }
}

static bool open_video_pipeline(FFmpegStreamFetcher *fetcher) {
    fetcher->video_stream_index = av_find_best_stream(
        fetcher->format_ctx,
        AVMEDIA_TYPE_VIDEO,
        -1,
        -1,
        (const AVCodec **)&fetcher->video_codec,
        0
    );
    if (fetcher->video_stream_index < 0) {
        set_error(fetcher, "未找到视频流", fetcher->video_stream_index);
        return false;
    }

    if (!open_stream_decoder(
            fetcher,
            fetcher->video_stream_index,
            &fetcher->video_codec_ctx,
            &fetcher->video_codec,
            true)) {
        return false;
    }

    fetcher->video_frame = av_frame_alloc();
    fetcher->video_drain_hold = av_frame_alloc();
    fetcher->video_transfer_frame = av_frame_alloc();
    if (fetcher->video_frame == 0 || fetcher->video_drain_hold == 0 ||
        fetcher->video_transfer_frame == 0) {
        snprintf(fetcher->last_error, sizeof(fetcher->last_error), "分配视频帧缓冲失败");
        return false;
    }

    return true;
}

static bool open_audio_pipeline(FFmpegStreamFetcher *fetcher) {
    int ret;

    fetcher->audio_stream_index = av_find_best_stream(
        fetcher->format_ctx,
        AVMEDIA_TYPE_AUDIO,
        -1,
        -1,
        (const AVCodec **)&fetcher->audio_codec,
        0
    );
    if (fetcher->audio_stream_index < 0) {
        fprintf(stderr, "未找到音频流，继续无音频模式。\n");
        fetcher->config.enable_audio = false;
        return true;
    }

    if (!open_stream_decoder(
            fetcher,
            fetcher->audio_stream_index,
            &fetcher->audio_codec_ctx,
            &fetcher->audio_codec,
            false)) {
        fprintf(stderr, "打开音频解码器失败，继续无音频模式。\n");
        fetcher->config.enable_audio = false;
        return true;
    }

    fetcher->audio_frame = av_frame_alloc();
    if (fetcher->audio_frame == 0) {
        snprintf(fetcher->last_error, sizeof(fetcher->last_error), "分配音频帧缓冲失败");
        return false;
    }

    if (!audio_output_open(&fetcher->audio_output, fetcher->audio_codec_ctx->sample_rate)) {
        fprintf(stderr, "SDL 音频设备打开失败，继续无音频模式。\n");
        fetcher->config.enable_audio = false;
        return true;
    }

    ret = 0;
    return ret == 0;
}

static int decode_thread(void *userdata) {
    FFmpegStreamFetcher *fetcher = (FFmpegStreamFetcher *)userdata;
    AVDictionary *options = 0;
    const AVInputFormat *input_format = 0;
    int ret;

    avformat_network_init();
    set_status(fetcher, "正在连接直播流");

    if (url_seems_flv(fetcher->config.url)) {
        /* FLV 直播：保留小缓冲抗抖动；不用 nobuffer，避免部分抖音 CDN 流间歇性卡顿 */
        av_dict_set(&options, "fflags", "discardcorrupt+flush_packets", 0);
        av_dict_set(&options, "rtbufsize", "8388608", 0);
        av_dict_set(&options, "max_delay", "500000", 0);
        input_format = av_find_input_format("live_flv");
        av_dict_set(&options, "flv_ignore_prevtag", "1", 0);
    } else {
        av_dict_set(&options, "fflags", "nobuffer+discardcorrupt", 0);
        av_dict_set(&options, "rtbufsize", "4194304", 0);
        av_dict_set(&options, "max_delay", "250000", 0);
    }
    av_dict_set(&options, "flags", "low_delay", 0);
    av_dict_set(&options, "probesize", "1048576", 0);
    av_dict_set(&options, "analyzeduration", "1000000", 0);
    av_dict_set(&options, "rw_timeout", "15000000", 0);
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);

    if (strstr(fetcher->config.url, "douyincdn.com") != 0 ||
        strstr(fetcher->config.url, "pull-flv") != 0 ||
        strstr(fetcher->config.url, "douyin.com") != 0) {
        av_dict_set(
            &options,
            "headers",
            "Referer: https://live.douyin.com/\r\n"
            "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
            "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n",
            0
        );
    } else if (strstr(fetcher->config.url, "xhscdn.com") != 0 ||
               strstr(fetcher->config.url, "xiaohongshu.com") != 0) {
        av_dict_set(
            &options,
            "headers",
            "Referer: https://www.xiaohongshu.com/\r\n"
            "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
            "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\n",
            0
        );
    }

    fetcher->format_ctx = avformat_alloc_context();
    if (fetcher->format_ctx == 0) {
        snprintf(fetcher->last_error, sizeof(fetcher->last_error), "分配格式上下文失败");
        fetcher->running = false;
        return -1;
    }
    fetcher->format_ctx->interrupt_callback.callback = interrupt_callback;
    fetcher->format_ctx->interrupt_callback.opaque = fetcher;

    ret = avformat_open_input(&fetcher->format_ctx, fetcher->config.url, input_format, &options);
    av_dict_free(&options);
    if (ret < 0) {
        set_error(fetcher, "打开输入流失败", ret);
        fetcher->running = false;
        return -1;
    }

    ret = avformat_find_stream_info(fetcher->format_ctx, 0);
    if (ret < 0) {
        set_error(fetcher, "读取流信息失败", ret);
        fetcher->running = false;
        return -1;
    }

    if (!open_video_pipeline(fetcher)) {
        fetcher->running = false;
        return -1;
    }

    {
        const AVCodecParameters *vp =
            fetcher->format_ctx->streams[fetcher->video_stream_index]->codecpar;
        const char *vcodec = "unknown";
        if (fetcher->video_codec != 0 && fetcher->video_codec->name != 0) {
            vcodec = fetcher->video_codec->name;
        }
        fprintf(
            stderr,
            "视频流: %s %dx%d%s\n",
            vcodec,
            vp->width,
            vp->height,
            fetcher->config.use_gpu ? " (D3D11VA)" : " (CPU)"
        );
        if (fetcher->video_codec_ctx != 0 &&
            fetcher->video_codec_ctx->codec_id == AV_CODEC_ID_HEVC &&
            !fetcher->config.use_gpu) {
            fprintf(
                stderr,
                "提示: HEVC/H.265 软件解码负载很高，当前 D3D11VA 不可用。\n"
            );
        }
    }

    if (fetcher->config.enable_audio && !open_audio_pipeline(fetcher)) {
        fetcher->running = false;
        return -1;
    }

    fetcher->packet = av_packet_alloc();
    if (fetcher->packet == 0) {
        snprintf(fetcher->last_error, sizeof(fetcher->last_error), "分配 FFmpeg 包缓冲失败");
        fetcher->running = false;
        return -1;
    }

    set_status(
        fetcher,
        fetcher->config.enable_audio ?
            (fetcher->config.use_gpu ?
                 "直播流连接成功，D3D11VA 视频解码 + SDL 音频中" :
                 "直播流连接成功，CPU 视频解码 + SDL 音频中") :
            (fetcher->config.use_gpu ? "直播流连接成功，D3D11VA 硬件解码中" : "直播流连接成功，CPU 解码中")
    );

    while (!fetcher->stop_requested) {
        drain_pending_audio_packets(fetcher);

        ret = av_read_frame(fetcher->format_ctx, fetcher->packet);
        if (ret == AVERROR_EOF) {
            set_status(fetcher, "流结束");
            break;
        }
        if (ret < 0) {
            set_error(fetcher, "读取媒体包失败", ret);
            SDL_Delay(10);
            continue;
        }

        if (fetcher->config.enable_audio &&
            fetcher->packet->stream_index == fetcher->audio_stream_index) {
            if (!audio_pkt_queue_push(fetcher, fetcher->packet)) {
                fprintf(stderr, "音频包入队失败，已丢弃。\n");
            }
            av_packet_unref(fetcher->packet);
            continue;
        }

        if (fetcher->packet->stream_index == fetcher->video_stream_index) {
            ret = avcodec_send_packet(fetcher->video_codec_ctx, fetcher->packet);
            av_packet_unref(fetcher->packet);
            if (ret < 0) {
                set_error(fetcher, "发送视频包到解码器失败", ret);
                continue;
            }
            if (!drain_decoder_frames(fetcher, fetcher->video_codec_ctx, fetcher->video_frame, true)) {
                break;
            }
            drain_pending_audio_packets(fetcher);
        } else {
            av_packet_unref(fetcher->packet);
        }
    }

    drain_pending_audio_packets(fetcher);
    audio_pkt_queue_clear(fetcher);

    if (fetcher->config.enable_audio) {
        audio_output_clear(&fetcher->audio_output);
    }

    fetcher->running = false;
    return 0;
}

bool ffmpeg_stream_init(FFmpegStreamFetcher *fetcher, const AppConfig *config) {
    if (fetcher == 0 || config == 0) {
        return false;
    }

    memset(fetcher, 0, sizeof(*fetcher));
    fetcher->config = *config;
    fetcher->video_stream_index = -1;
    fetcher->audio_stream_index = -1;
    fetcher->frame_mutex = SDL_CreateMutex();
    if (fetcher->frame_mutex == 0) {
        return false;
    }

    fetcher->native_resolution_pending = config->use_native_resolution;
    if (config->use_native_resolution) {
        fetcher->config.output_width = 0;
        fetcher->config.output_height = 0;
        fetcher->latest_bgr_size = 0U;
        fetcher->latest_bgr = 0;
        fetcher->work_bgr = 0;
    } else {
        fetcher->latest_bgr_size =
            (size_t)config->output_width * (size_t)config->output_height * 3U;
        fetcher->latest_bgr = (uint8_t *)malloc(fetcher->latest_bgr_size);
        fetcher->work_bgr = (uint8_t *)malloc(fetcher->latest_bgr_size);
        if (fetcher->latest_bgr == 0 || fetcher->work_bgr == 0) {
            ffmpeg_stream_destroy(fetcher);
            return false;
        }

        memset(fetcher->latest_bgr, 0, fetcher->latest_bgr_size);
        memset(fetcher->work_bgr, 0, fetcher->latest_bgr_size);
    }

    if (!audio_output_init(&fetcher->audio_output)) {
        ffmpeg_stream_destroy(fetcher);
        return false;
    }
    snprintf(fetcher->status_text, sizeof(fetcher->status_text), "等待开始");
    return true;
}

bool ffmpeg_stream_start(FFmpegStreamFetcher *fetcher) {
    if (fetcher == 0 || fetcher->running) {
        return false;
    }

    fetcher->stop_requested = false;
    fetcher->running = true;
    fetcher->thread = SDL_CreateThread(decode_thread, "ffmpeg_decode_thread", fetcher);
    if (fetcher->thread == 0) {
        fetcher->running = false;
        return false;
    }
    return true;
}

bool ffmpeg_stream_get_latest_frame(
    FFmpegStreamFetcher *fetcher,
    uint8_t *out_buffer,
    size_t out_buffer_size,
    uint64_t *out_sequence) {
    bool copied = false;

    if (fetcher == 0 || out_buffer == 0 || fetcher->latest_bgr_size == 0U ||
        out_buffer_size < fetcher->latest_bgr_size) {
        return false;
    }

    SDL_LockMutex(fetcher->frame_mutex);
    if (fetcher->frame_ready && (out_sequence == 0 || *out_sequence != fetcher->latest_sequence)) {
        memcpy(out_buffer, fetcher->latest_bgr, fetcher->latest_bgr_size);
        if (out_sequence != 0) {
            *out_sequence = fetcher->latest_sequence;
        }
        copied = true;
    }
    SDL_UnlockMutex(fetcher->frame_mutex);
    return copied;
}

void ffmpeg_stream_get_canvas_size(const FFmpegStreamFetcher *fetcher, int *out_w, int *out_h) {
    if (fetcher == 0 || out_w == 0 || out_h == 0) {
        return;
    }
    *out_w = fetcher->config.output_width;
    *out_h = fetcher->config.output_height;
}

double ffmpeg_stream_get_latest_video_pts(const FFmpegStreamFetcher *fetcher, bool *out_valid) {
    double pts = 0.0;
    bool valid = false;

    if (fetcher == 0) {
        if (out_valid != 0) {
            *out_valid = false;
        }
        return 0.0;
    }

    if (fetcher->frame_mutex != 0) {
        SDL_LockMutex((SDL_mutex *)fetcher->frame_mutex);
        valid = fetcher->latest_video_pts_valid;
        pts = fetcher->latest_video_pts_sec;
        SDL_UnlockMutex((SDL_mutex *)fetcher->frame_mutex);
    }

    if (out_valid != 0) {
        *out_valid = valid;
    }
    return pts;
}

double ffmpeg_stream_get_audio_playback_pts(const FFmpegStreamFetcher *fetcher, bool *out_valid) {
    bool valid = false;
    double pts = -1.0;

    if (fetcher == 0 || !fetcher->config.enable_audio) {
        if (out_valid != 0) {
            *out_valid = false;
        }
        return -1.0;
    }

    pts = audio_output_get_playback_pts(&fetcher->audio_output);
    valid = pts >= 0.0;
    if (out_valid != 0) {
        *out_valid = valid;
    }
    return pts;
}

void ffmpeg_stream_stop(FFmpegStreamFetcher *fetcher) {
    if (fetcher == 0) {
        return;
    }

    fetcher->stop_requested = true;

    if (fetcher->thread != 0) {
        SDL_WaitThread(fetcher->thread, 0);
        fetcher->thread = 0;
    }

    audio_output_close(&fetcher->audio_output);
    fetcher->running = false;
}

void ffmpeg_stream_destroy(FFmpegStreamFetcher *fetcher) {
    if (fetcher == 0) {
        return;
    }

    ffmpeg_stream_stop(fetcher);

    audio_pkt_queue_clear(fetcher);

    if (fetcher->packet != 0) {
        av_packet_free(&fetcher->packet);
    }
    if (fetcher->video_frame != 0) {
        av_frame_free(&fetcher->video_frame);
    }
    if (fetcher->video_drain_hold != 0) {
        av_frame_free(&fetcher->video_drain_hold);
    }
    if (fetcher->video_transfer_frame != 0) {
        av_frame_free(&fetcher->video_transfer_frame);
    }
    if (fetcher->audio_frame != 0) {
        av_frame_free(&fetcher->audio_frame);
    }
    if (fetcher->video_codec_ctx != 0) {
        avcodec_free_context(&fetcher->video_codec_ctx);
    }
    if (fetcher->audio_codec_ctx != 0) {
        avcodec_free_context(&fetcher->audio_codec_ctx);
    }
    if (fetcher->format_ctx != 0) {
        avformat_close_input(&fetcher->format_ctx);
    }
    if (fetcher->video_hw_device_ctx != 0) {
        av_buffer_unref(&fetcher->video_hw_device_ctx);
    }
    if (fetcher->sws_ctx != 0) {
        sws_freeContext(fetcher->sws_ctx);
        fetcher->sws_ctx = 0;
    }

    audio_output_destroy(&fetcher->audio_output);

    free(fetcher->latest_bgr);
    free(fetcher->work_bgr);
    fetcher->latest_bgr = 0;
    fetcher->work_bgr = 0;
    fetcher->latest_bgr_size = 0U;

    if (fetcher->frame_mutex != 0) {
        SDL_DestroyMutex(fetcher->frame_mutex);
        fetcher->frame_mutex = 0;
    }
}
