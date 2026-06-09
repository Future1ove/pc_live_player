#include "app_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void app_set_default_config(AppConfig *config) {
    if (config == 0) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->use_gpu = true;
    config->enable_audio = true;
    config->fast_decode = false;
    config->vsync = false;
    config->use_native_resolution = true;
    config->detector_mode = QR_DETECTOR_MODE_FAST;
    config->output_width = 0;
    config->output_height = 0;
    config->roi_ratio = 0.6f;
}

void app_print_usage(const char *exe_name) {
    printf("用法:\n");
    printf(
        "  %s --url <直播流URL> [--gpu|--cpu] [--audio] [--full-res] [--fast-decode] [--vsync]\n"
        "      [--width 1920] [--height 1080] [--roi 0.6]\n",
        exe_name
    );
    printf("\n快捷键:\n");
    printf("  F11  切换全屏\n");
    printf("  ESC  退出全屏 / 关闭二维码弹窗 / 退出程序\n");
}

static bool parse_int_arg(const char *text, int *value) {
    char *end = 0;
    long parsed;

    if (text == 0 || value == 0) {
        return false;
    }

    parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }

    *value = (int)parsed;
    return true;
}

static bool parse_float_arg(const char *text, float *value) {
    char *end = 0;
    double parsed;

    if (text == 0 || value == 0) {
        return false;
    }

    parsed = strtod(text, &end);
    if (end == text || *end != '\0') {
        return false;
    }

    *value = (float)parsed;
    return true;
}

bool app_parse_args(int argc, char **argv, AppConfig *config) {
    int i;

    if (config == 0) {
        return false;
    }

    app_set_default_config(config);

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--url") == 0 && i + 1 < argc) {
            snprintf(config->url, sizeof(config->url), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--gpu") == 0) {
            config->use_gpu = true;
        } else if (strcmp(argv[i], "--cpu") == 0) {
            config->use_gpu = false;
        } else if (strcmp(argv[i], "--audio") == 0) {
            config->enable_audio = true;
        } else if (strcmp(argv[i], "--full-res") == 0) {
            config->detector_mode = QR_DETECTOR_MODE_FULL_RES;
        } else if (strcmp(argv[i], "--fast-decode") == 0) {
            config->fast_decode = true;
        } else if (strcmp(argv[i], "--vsync") == 0) {
            config->vsync = true;
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &config->output_width)) {
                return false;
            }
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            if (!parse_int_arg(argv[++i], &config->output_height)) {
                return false;
            }
        } else if (strcmp(argv[i], "--roi") == 0 && i + 1 < argc) {
            if (!parse_float_arg(argv[++i], &config->roi_ratio)) {
                return false;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            app_print_usage(argv[0]);
            return false;
        } else {
            fprintf(stderr, "未知参数: %s\n", argv[i]);
            app_print_usage(argv[0]);
            return false;
        }
    }

    if (config->url[0] == '\0') {
        app_print_usage(argv[0]);
        return false;
    }

    return true;
}
