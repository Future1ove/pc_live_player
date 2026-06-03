#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

#include "app_config.h"
#include "ffmpeg_stream.h"
#include "latency_monitor.h"
#include "qr_detector.h"
#include "status_history.h"
#include "ui_sdl.h"

static void format_timestamp(char *buffer, size_t buffer_size) {
    time_t now = time(0);
    struct tm tm_now;
    Uint64 ticks = SDL_GetTicks64();
    unsigned int ms = (unsigned int)(ticks % 1000U);

#ifdef _WIN32
    localtime_s(&tm_now, &now);
#else
    localtime_r(&now, &tm_now);
#endif

    snprintf(
        buffer,
        buffer_size,
        "%02d:%02d:%02d.%03u",
        tm_now.tm_hour,
        tm_now.tm_min,
        tm_now.tm_sec,
        ms
    );
}

static void update_title(
    SDLUI *ui,
    const AppConfig *config,
    const FFmpegStreamFetcher *fetcher,
    const StatusHistory *history,
    double fps,
    double latency_ms) {
    char title[4096];

    snprintf(
        title,
        sizeof(title),
        "直播流二维码快速检测器 | 模式=%s | FPS=%.1f | 估算延迟=%.1fms | 已检测=%llu | 当前=%s | 状态=%s",
        config->detector_mode == QR_DETECTOR_MODE_FULL_RES ? "全分辨率" : "快速ROI",
        fps,
        latency_ms,
        (unsigned long long)history->count,
        history->current_qr[0] != '\0' ? history->current_qr : "无",
        fetcher->status_text
    );
    ui_sdl_set_window_title(ui, title);
}

static void handle_detected_qr(
    const AppConfig *config,
    SDLUI *ui,
    StatusHistory *history,
    const QRCodeResult *result) {
    bool popup_closed;
    char timestamp[64];

    if (result == 0 || !result->found || result->data[0] == '\0') {
        return;
    }

    if (config->detector_mode != QR_DETECTOR_MODE_FULL_RES) {
        return;
    }

    popup_closed = !ui_sdl_is_qr_popup_visible(ui);
    if (strcmp(result->data, history->last_qr) != 0 || popup_closed) {
        format_timestamp(timestamp, sizeof(timestamp));
        status_history_append(history, timestamp, result->data);
        printf("[%s] 检测到二维码: %s\n", timestamp, result->data);
        fflush(stdout);
        ui_sdl_show_qr_popup(ui, result->data);
    }
}

int app_run(const AppConfig *config) {
    FFmpegStreamFetcher fetcher;
    QRDetector detector;
    SDLUI ui;
    StatusHistory history;
    LatencyMonitor latency_monitor;
    uint8_t *frame_copy = 0;
    size_t frame_copy_size;
    uint64_t current_sequence = 0U;
    uint64_t display_frame_index = 0U;
    Uint64 last_title_update = 0U;
    bool quit_requested = false;
    bool ok = false;

    if (config == 0) {
        return 1;
    }

    if (!ui_sdl_init(&ui, 1600, 900, config->vsync)) {
        return 1;
    }

    if (!status_history_init(&history, APP_HISTORY_CAPACITY)) {
        ui_sdl_destroy(&ui);
        return 1;
    }

    latency_monitor_init(&latency_monitor);

    if (!qr_detector_init(&detector, config->detector_mode, config->roi_ratio)) {
        status_history_destroy(&history);
        ui_sdl_destroy(&ui);
        return 1;
    }

    if (!ffmpeg_stream_init(&fetcher, config)) {
        qr_detector_destroy(&detector);
        status_history_destroy(&history);
        ui_sdl_destroy(&ui);
        return 1;
    }

    frame_copy_size = (size_t)config->output_width * (size_t)config->output_height * 3U;
    frame_copy = (uint8_t *)malloc(frame_copy_size);
    if (frame_copy == 0) {
        ffmpeg_stream_destroy(&fetcher);
        qr_detector_destroy(&detector);
        status_history_destroy(&history);
        ui_sdl_destroy(&ui);
        return 1;
    }

    if (!ffmpeg_stream_start(&fetcher)) {
        fprintf(stderr, "启动 FFmpeg 拉流线程失败。\n");
        free(frame_copy);
        ffmpeg_stream_destroy(&fetcher);
        qr_detector_destroy(&detector);
        status_history_destroy(&history);
        ui_sdl_destroy(&ui);
        return 1;
    }

    while (!quit_requested) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            ui_sdl_handle_event(&ui, &event, &quit_requested);
        }

        if (ffmpeg_stream_get_latest_frame(&fetcher, frame_copy, frame_copy_size, &current_sequence)) {
            QRCodeResult result;
            bool run_qr;

            latency_monitor_add_frame(&latency_monitor, (double)SDL_GetTicks64() / 1000.0);
            memset(&result, 0, sizeof(result));
            display_frame_index += 1U;
            run_qr = config->detector_mode == QR_DETECTOR_MODE_FULL_RES ||
                (display_frame_index % 2U) == 1U;
            if (run_qr) {
                qr_detector_detect(
                    &detector,
                    frame_copy,
                    config->output_width,
                    config->output_height,
                    config->output_width * 3,
                    &result
                );
            }
            handle_detected_qr(config, &ui, &history, &result);
            ui_sdl_render_frame(
                &ui,
                frame_copy,
                config->output_width,
                config->output_height,
                config->output_width * 3
            );
        } else {
            SDL_Delay(1);
        }

        if (SDL_GetTicks64() - last_title_update >= 500U) {
            update_title(
                &ui,
                config,
                &fetcher,
                &history,
                latency_monitor_get_fps(&latency_monitor),
                latency_monitor_get_estimated_latency_ms(&latency_monitor)
            );
            last_title_update = SDL_GetTicks64();
        }
    }

    ok = true;
    free(frame_copy);
    ffmpeg_stream_destroy(&fetcher);
    qr_detector_destroy(&detector);
    status_history_destroy(&history);
    ui_sdl_destroy(&ui);
    return ok ? 0 : 1;
}
