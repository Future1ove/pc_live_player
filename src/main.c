#include <stdio.h>
#include <locale.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "app.h"

int main(int argc, char **argv) {
    AppConfig config;

#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif
    setlocale(LC_ALL, ".UTF-8");

    if (!app_parse_args(argc, argv, &config)) {
        return 1;
    }

    printf("直播流二维码快速检测器 - C 版本\n");
    printf("URL: %s\n", config.url);
    printf("解码: %s\n", config.use_gpu ? "GPU(CUDA 优先)" : "CPU");
    printf(
        "画质: %s\n",
        config.fast_decode ? "速度优先(最近邻缩放/跳非参考帧)" : "清晰(双三次缩放/完整解码)"
    );
    printf("垂直同步: %s\n", config.vsync ? "开启(更稳、可能限制帧率)" : "关闭(更高帧率)");
    printf("音频: %s\n", config.enable_audio ? "启用(FFmpeg + SDL2 Audio)" : "关闭");
    printf("检测模式: %s\n", config.detector_mode == QR_DETECTOR_MODE_FULL_RES ? "全分辨率" : "快速ROI");
    printf("输出分辨率: %dx%d\n", config.output_width, config.output_height);
    printf("\n");

    return app_run(&config);
}
