#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_STATUS_CAPACITY 256
#define APP_QR_TEXT_CAPACITY 2048
#define APP_HISTORY_CAPACITY 1024

typedef enum QRDetectorMode {
    QR_DETECTOR_MODE_FAST = 0,
    QR_DETECTOR_MODE_FULL_RES = 1
} QRDetectorMode;

typedef struct AppConfig {
    char url[APP_QR_TEXT_CAPACITY];
    bool use_gpu;
    bool enable_audio;
    bool fast_decode;
    bool vsync;
    /** 为 true 时由首帧确定画布尺寸，忽略 output_width/height 初值（GUI 传 0） */
    bool use_native_resolution;
    QRDetectorMode detector_mode;
    int output_width;
    int output_height;
    float roi_ratio;
} AppConfig;

typedef struct DecodedFrame {
    uint8_t *data;
    int width;
    int height;
    int stride;
    size_t size;
    uint64_t sequence;
} DecodedFrame;

typedef struct QRCodeResult {
    bool found;
    char data[APP_QR_TEXT_CAPACITY];
    int x;
    int y;
    int width;
    int height;
} QRCodeResult;

#endif
