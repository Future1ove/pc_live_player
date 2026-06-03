#ifndef QR_DETECTOR_H
#define QR_DETECTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zbar.h>

#include "app_types.h"

/* ZBar 在 C++ 下把类型放在 zbar 命名空间中，与纯 C 的全局 typedef 不同 */
#ifdef __cplusplus
typedef zbar::zbar_image_scanner_t *zbar_scanner_ptr;
typedef zbar::zbar_image_t *zbar_image_ptr;
#else
typedef zbar_image_scanner_t *zbar_scanner_ptr;
typedef zbar_image_t *zbar_image_ptr;
#endif

typedef struct QRDetector {
    QRDetectorMode mode;
    float roi_ratio;
    zbar_scanner_ptr scanner;
    zbar_image_ptr zbar_image;
    uint8_t *gray_buffer;
    size_t gray_buffer_size;
} QRDetector;

#ifdef __cplusplus
extern "C" {
#endif

bool qr_detector_init(QRDetector *detector, QRDetectorMode mode, float roi_ratio);
void qr_detector_destroy(QRDetector *detector);
bool qr_detector_detect(
    QRDetector *detector,
    const uint8_t *bgr,
    int width,
    int height,
    int stride,
    QRCodeResult *result
);

#ifdef __cplusplus
}
#endif

#endif
