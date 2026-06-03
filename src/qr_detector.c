#include "qr_detector.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    kTier1MaxSide = 640,
    kTier2MaxSide = 960,
    kTier3MaxSide = 1280,
    kTier4MaxSide = 1600,
    kTier5MaxSide = 1920,
    kNativeGrayMaxSide = 1440
};

static int clamp255(int v) {
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return v;
}

static void zbar_set_qr_density(QRDetector *detector, int density) {
    if (density < 1) {
        density = 1;
    }
    if (density > 10) {
        density = 10;
    }
    zbar_image_scanner_set_config(detector->scanner, ZBAR_QRCODE, ZBAR_CFG_X_DENSITY, density);
    zbar_image_scanner_set_config(detector->scanner, ZBAR_QRCODE, ZBAR_CFG_Y_DENSITY, density);
}

static bool fill_result_from_symbol(
    const zbar_symbol_t *symbol,
    int roi_x,
    int roi_y,
    int roi_w,
    int roi_h,
    int gw,
    int gh,
    QRCodeResult *result) {
    int i;
    int points = zbar_symbol_get_loc_size(symbol);
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;
    const char *data = zbar_symbol_get_data(symbol);

    if (data == 0 || data[0] == '\0') {
        return false;
    }

    if (points > 0) {
        min_x = max_x = zbar_symbol_get_loc_x(symbol, 0);
        min_y = max_y = zbar_symbol_get_loc_y(symbol, 0);
        for (i = 1; i < points; ++i) {
            int px = zbar_symbol_get_loc_x(symbol, i);
            int py = zbar_symbol_get_loc_y(symbol, i);
            if (px < min_x) {
                min_x = px;
            }
            if (py < min_y) {
                min_y = py;
            }
            if (px > max_x) {
                max_x = px;
            }
            if (py > max_y) {
                max_y = py;
            }
        }
    }

    result->found = true;
    snprintf(result->data, sizeof(result->data), "%s", data);
    /* ZBar 坐标在 gw×gh 灰度图上；映射回 ROI 像素坐标 */
    if (gw > 0 && gh > 0 && (gw != roi_w || gh != roi_h)) {
        result->x = roi_x + (min_x * roi_w) / gw;
        result->y = roi_y + (min_y * roi_h) / gh;
        result->width = ((max_x - min_x) * roi_w) / gw;
        result->height = ((max_y - min_y) * roi_h) / gh;
    } else {
        result->x = roi_x + min_x;
        result->y = roi_y + min_y;
        result->width = max_x - min_x;
        result->height = max_y - min_y;
    }
    return true;
}

static bool scan_gray_zbar(
    QRDetector *detector,
    uint8_t *gray,
    int gw,
    int gh,
    int roi_x,
    int roi_y,
    int roi_w,
    int roi_h,
    QRCodeResult *result) {
    zbar_image_t *image = detector->zbar_image;
    const zbar_symbol_t *symbol;
    size_t gray_size = (size_t)gw * (size_t)gh;

    if (image == 0) {
        return false;
    }

    zbar_image_set_format(image, zbar_fourcc('Y', '8', '0', '0'));
    zbar_image_set_size(image, (unsigned int)gw, (unsigned int)gh);
    zbar_image_set_data(image, gray, (unsigned long)gray_size, 0);

    if (zbar_scan_image(detector->scanner, image) <= 0) {
        return false;
    }

    symbol = zbar_image_first_symbol(image);
    while (symbol != 0) {
        if (zbar_symbol_get_type(symbol) == ZBAR_QRCODE) {
            if (fill_result_from_symbol(symbol, roi_x, roi_y, roi_w, roi_h, gw, gh, result)) {
                return true;
            }
        }
        symbol = zbar_symbol_next(symbol);
    }
    return false;
}

static void bgr_roi_to_gray_downscaled(
    const uint8_t *bgr,
    int width,
    int height,
    int stride,
    int roi_x,
    int roi_y,
    int roi_w,
    int roi_h,
    int out_w,
    int out_h,
    uint8_t *gray_out) {
    int x;
    int y;
    (void)width;
    (void)height;

    for (y = 0; y < out_h; ++y) {
        int sy = roi_y + (y * roi_h) / out_h;
        if (sy >= roi_y + roi_h) {
            sy = roi_y + roi_h - 1;
        }
        for (x = 0; x < out_w; ++x) {
            int sx = roi_x + (x * roi_w) / out_w;
            if (sx >= roi_x + roi_w) {
                sx = roi_x + roi_w - 1;
            }
            {
                const uint8_t *p = bgr + (size_t)sy * (size_t)stride + (size_t)sx * 3U;
                gray_out[(size_t)y * (size_t)out_w + (size_t)x] =
                    (uint8_t)((p[2] * 77U + p[1] * 150U + p[0] * 29U) >> 8);
            }
        }
    }
}

static void stretch_gray_inplace(uint8_t *buf, size_t n) {
    size_t i;
    uint8_t mn = 255;
    uint8_t mx = 0;

    if (n == 0U) {
        return;
    }
    for (i = 0; i < n; ++i) {
        if (buf[i] < mn) {
            mn = buf[i];
        }
        if (buf[i] > mx) {
            mx = buf[i];
        }
    }
    if (mx <= mn) {
        return;
    }
    {
        const int range = (int)mx - (int)mn;
        for (i = 0; i < n; ++i) {
            buf[i] = (uint8_t)((((int)buf[i] - (int)mn) * 255 + range / 2) / range);
        }
    }
}

static void invert_gray(const uint8_t *src, uint8_t *dst, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        dst[i] = (uint8_t)(255 - (int)src[i]);
    }
}

static void unsharp_gray_3x3(const uint8_t *src, uint8_t *dst, int w, int h) {
    int x;
    int y;

    if (w < 3 || h < 3) {
        memcpy(dst, src, (size_t)w * (size_t)h);
        return;
    }

    memcpy(dst, src, (size_t)w * (size_t)h);

    for (y = 1; y < h - 1; ++y) {
        for (x = 1; x < w - 1; ++x) {
            int sum = 0;
            int dy;
            int dx;
            for (dy = -1; dy <= 1; ++dy) {
                for (dx = -1; dx <= 1; ++dx) {
                    sum += (int)src[(size_t)(y + dy) * (size_t)w + (size_t)(x + dx)];
                }
            }
            {
                int blur = sum / 9;
                int c = (int)src[(size_t)y * (size_t)w + (size_t)x];
                int v = c + ((c - blur) * 8) / 10;
                dst[(size_t)y * (size_t)w + (size_t)x] = (uint8_t)clamp255(v);
            }
        }
    }
}

static bool try_max_side_pass(
    QRDetector *detector,
    const uint8_t *bgr,
    int width,
    int height,
    int stride,
    int roi_x,
    int roi_y,
    int roi_w,
    int roi_h,
    int max_side,
    QRCodeResult *result) {
    int max_dim = roi_w > roi_h ? roi_w : roi_h;
    int down_w;
    int down_h;
    size_t pix;
    uint8_t *small_gray;
    uint8_t *small_tmp;

    if (max_dim > max_side) {
        down_w = (roi_w * max_side + max_dim - 1) / max_dim;
        down_h = (roi_h * max_side + max_dim - 1) / max_dim;
    } else {
        /* 原先错误地直接 return false，1080p 永远试不到「整幅 ROI」等价分辨率 */
        down_w = roi_w;
        down_h = roi_h;
    }
    if (down_w < 1) {
        down_w = 1;
    }
    if (down_h < 1) {
        down_h = 1;
    }

    pix = (size_t)down_w * (size_t)down_h;
    small_gray = (uint8_t *)malloc(pix);
    small_tmp = (uint8_t *)malloc(pix);
    if (small_gray == 0 || small_tmp == 0) {
        free(small_gray);
        free(small_tmp);
        return false;
    }

    bgr_roi_to_gray_downscaled(
        bgr,
        width,
        height,
        stride,
        roi_x,
        roi_y,
        roi_w,
        roi_h,
        down_w,
        down_h,
        small_gray
    );

    stretch_gray_inplace(small_gray, pix);

    zbar_set_qr_density(detector, 2);
    if (scan_gray_zbar(
            detector,
            small_gray,
            down_w,
            down_h,
            roi_x,
            roi_y,
            roi_w,
            roi_h,
            result)) {
        free(small_gray);
        free(small_tmp);
        zbar_set_qr_density(detector, 2);
        return true;
    }

    unsharp_gray_3x3(small_gray, small_tmp, down_w, down_h);
    zbar_set_qr_density(detector, 4);
    if (scan_gray_zbar(
            detector,
            small_tmp,
            down_w,
            down_h,
            roi_x,
            roi_y,
            roi_w,
            roi_h,
            result)) {
        free(small_gray);
        free(small_tmp);
        zbar_set_qr_density(detector, 2);
        return true;
    }

    /* 反色：直播叠加层常为白底/反色，与手机取景不一致时 ZBar 易漏 */
    invert_gray(small_gray, small_tmp, pix);
    zbar_set_qr_density(detector, 3);
    if (scan_gray_zbar(
            detector,
            small_tmp,
            down_w,
            down_h,
            roi_x,
            roi_y,
            roi_w,
            roi_h,
            result)) {
        free(small_gray);
        free(small_tmp);
        zbar_set_qr_density(detector, 2);
        return true;
    }

    free(small_gray);
    free(small_tmp);
    zbar_set_qr_density(detector, 2);
    return false;
}

static bool ensure_gray_capacity(QRDetector *detector, size_t required_size) {
    uint8_t *new_buffer;

    if (detector->gray_buffer_size >= required_size) {
        return true;
    }

    new_buffer = (uint8_t *)realloc(detector->gray_buffer, required_size);
    if (new_buffer == 0) {
        return false;
    }

    detector->gray_buffer = new_buffer;
    detector->gray_buffer_size = required_size;
    return true;
}

static void bgr_to_gray_roi(
    const uint8_t *bgr,
    int width,
    int height,
    int stride,
    int roi_x,
    int roi_y,
    int roi_width,
    int roi_height,
    uint8_t *gray_out) {
    int x;
    int y;
    (void)width;
    (void)height;

    for (y = 0; y < roi_height; ++y) {
        const uint8_t *src_row = bgr + (size_t)(roi_y + y) * (size_t)stride + (size_t)roi_x * 3U;
        uint8_t *dst_row = gray_out + (size_t)y * (size_t)roi_width;
        x = 0;
        for (; x <= roi_width - 4; x += 4) {
            const uint8_t *p0 = src_row + (size_t)x * 3U;
            const uint8_t *p1 = p0 + 3U;
            const uint8_t *p2 = p1 + 3U;
            const uint8_t *p3 = p2 + 3U;
            dst_row[x] = (uint8_t)((p0[2] * 77U + p0[1] * 150U + p0[0] * 29U) >> 8);
            dst_row[x + 1] = (uint8_t)((p1[2] * 77U + p1[1] * 150U + p1[0] * 29U) >> 8);
            dst_row[x + 2] = (uint8_t)((p2[2] * 77U + p2[1] * 150U + p2[0] * 29U) >> 8);
            dst_row[x + 3] = (uint8_t)((p3[2] * 77U + p3[1] * 150U + p3[0] * 29U) >> 8);
        }
        for (; x < roi_width; ++x) {
            const uint8_t *pixel = src_row + (size_t)x * 3U;
            dst_row[x] = (uint8_t)((pixel[2] * 77U + pixel[1] * 150U + pixel[0] * 29U) >> 8);
        }
    }
}

bool qr_detector_init(QRDetector *detector, QRDetectorMode mode, float roi_ratio) {
    if (detector == 0) {
        return false;
    }

    memset(detector, 0, sizeof(*detector));
    detector->mode = mode;
    detector->roi_ratio = roi_ratio > 0.0f ? roi_ratio : 0.6f;
    detector->scanner = zbar_image_scanner_create();
    if (detector->scanner == 0) {
        return false;
    }

    zbar_image_scanner_set_config(detector->scanner, 0, ZBAR_CFG_ENABLE, 0);
    zbar_image_scanner_set_config(detector->scanner, ZBAR_QRCODE, ZBAR_CFG_ENABLE, 1);

    detector->zbar_image = zbar_image_create();
    if (detector->zbar_image == 0) {
        zbar_image_scanner_destroy(detector->scanner);
        detector->scanner = 0;
        return false;
    }

    return true;
}

void qr_detector_destroy(QRDetector *detector) {
    if (detector == 0) {
        return;
    }

    if (detector->scanner != 0) {
        zbar_image_scanner_destroy(detector->scanner);
        detector->scanner = 0;
    }

    if (detector->zbar_image != 0) {
        zbar_image_destroy(detector->zbar_image);
        detector->zbar_image = 0;
    }

    free(detector->gray_buffer);
    detector->gray_buffer = 0;
    detector->gray_buffer_size = 0U;
}

bool qr_detector_detect(
    QRDetector *detector,
    const uint8_t *bgr,
    int width,
    int height,
    int stride,
    QRCodeResult *result) {
    int roi_x = 0;
    int roi_y = 0;
    int roi_width = width;
    int roi_height = height;
    size_t gray_size;
    int max_roi_side;

    if (result != 0) {
        memset(result, 0, sizeof(*result));
    }

    if (detector == 0 || bgr == 0 || width <= 0 || height <= 0 || stride <= 0 || result == 0) {
        return false;
    }

    if (detector->mode == QR_DETECTOR_MODE_FAST) {
        roi_width = (int)((float)width * detector->roi_ratio);
        roi_height = (int)((float)height * detector->roi_ratio);
        if (roi_width <= 0 || roi_width > width) {
            roi_width = width;
        }
        if (roi_height <= 0 || roi_height > height) {
            roi_height = height;
        }
        roi_x = (width - roi_width) / 2;
        roi_y = (height - roi_height) / 2;
    }

    max_roi_side = roi_width > roi_height ? roi_width : roi_height;

    /*
     * 顺序：先较高分辨率（利于小码）→ 再低分辨率快扫；大于 1440 时再试 1920 等效整幅。
     */
    if (max_roi_side > kTier3MaxSide) {
        if (try_max_side_pass(
                detector,
                bgr,
                width,
                height,
                stride,
                roi_x,
                roi_y,
                roi_width,
                roi_height,
                kTier3MaxSide,
                result)) {
            return true;
        }
    }
    if (max_roi_side > kTier4MaxSide) {
        if (try_max_side_pass(
                detector,
                bgr,
                width,
                height,
                stride,
                roi_x,
                roi_y,
                roi_width,
                roi_height,
                kTier4MaxSide,
                result)) {
            return true;
        }
    }
    if (max_roi_side > kTier2MaxSide) {
        if (try_max_side_pass(
                detector,
                bgr,
                width,
                height,
                stride,
                roi_x,
                roi_y,
                roi_width,
                roi_height,
                kTier2MaxSide,
                result)) {
            return true;
        }
    }
    if (max_roi_side > kTier1MaxSide) {
        if (try_max_side_pass(
                detector,
                bgr,
                width,
                height,
                stride,
                roi_x,
                roi_y,
                roi_width,
                roi_height,
                kTier1MaxSide,
                result)) {
            return true;
        }
    }
    if (max_roi_side > kNativeGrayMaxSide) {
        if (try_max_side_pass(
                detector,
                bgr,
                width,
                height,
                stride,
                roi_x,
                roi_y,
                roi_width,
                roi_height,
                kTier5MaxSide,
                result)) {
            return true;
        }
        zbar_set_qr_density(detector, 2);
        return false;
    }

    gray_size = (size_t)roi_width * (size_t)roi_height;
    if (!ensure_gray_capacity(detector, gray_size)) {
        zbar_set_qr_density(detector, 2);
        return false;
    }

    bgr_to_gray_roi(bgr, width, height, stride, roi_x, roi_y, roi_width, roi_height, detector->gray_buffer);
    stretch_gray_inplace(detector->gray_buffer, gray_size);

    zbar_set_qr_density(detector, 3);
    if (scan_gray_zbar(
            detector,
            detector->gray_buffer,
            roi_width,
            roi_height,
            roi_x,
            roi_y,
            roi_width,
            roi_height,
            result)) {
        zbar_set_qr_density(detector, 2);
        return true;
    }

    zbar_set_qr_density(detector, 5);
    if (scan_gray_zbar(
            detector,
            detector->gray_buffer,
            roi_width,
            roi_height,
            roi_x,
            roi_y,
            roi_width,
            roi_height,
            result)) {
        zbar_set_qr_density(detector, 2);
        return true;
    }
    {
        uint8_t *inv = (uint8_t *)malloc(gray_size);
        if (inv != 0) {
            invert_gray(detector->gray_buffer, inv, gray_size);
            zbar_set_qr_density(detector, 4);
            if (scan_gray_zbar(
                    detector,
                    inv,
                    roi_width,
                    roi_height,
                    roi_x,
                    roi_y,
                    roi_width,
                    roi_height,
                    result)) {
                free(inv);
                zbar_set_qr_density(detector, 2);
                return true;
            }
            free(inv);
        }
    }

    zbar_set_qr_density(detector, 2);
    return false;
}
