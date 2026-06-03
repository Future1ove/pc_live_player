#include "ui_sdl.h"

#include <stdio.h>
#include <string.h>

#ifdef HAVE_QRENCODE
#include <qrencode.h>
#endif

static bool ensure_video_texture(SDLUI *ui, int width, int height) {
    if (ui->video_texture != 0 && ui->frame_width == width && ui->frame_height == height) {
        return true;
    }

    if (ui->video_texture != 0) {
        SDL_DestroyTexture(ui->video_texture);
        ui->video_texture = 0;
    }

    ui->video_texture = SDL_CreateTexture(
        ui->video_renderer,
        SDL_PIXELFORMAT_BGR24,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height
    );
    if (ui->video_texture == 0) {
        return false;
    }

    ui->frame_width = width;
    ui->frame_height = height;
    return true;
}

static void render_texture_fit(SDL_Renderer *renderer, SDL_Texture *texture, int src_width, int src_height) {
    int window_width;
    int window_height;
    float scale_x;
    float scale_y;
    float scale;
    SDL_Rect target;

    SDL_GetRendererOutputSize(renderer, &window_width, &window_height);
    scale_x = (float)window_width / (float)src_width;
    scale_y = (float)window_height / (float)src_height;
    scale = scale_x < scale_y ? scale_x : scale_y;

    target.w = (int)((float)src_width * scale);
    target.h = (int)((float)src_height * scale);
    target.x = (window_width - target.w) / 2;
    target.y = (window_height - target.h) / 2;

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, 0, &target);
    SDL_RenderPresent(renderer);
}

bool ui_sdl_init(SDLUI *ui, int width, int height, bool enable_vsync) {
    Uint32 renderer_flags;

    if (ui == 0) {
        return false;
    }

    memset(ui, 0, sizeof(*ui));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL 初始化失败: %s\n", SDL_GetError());
        return false;
    }

    ui->video_window = SDL_CreateWindow(
        "直播流二维码快速检测器",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (ui->video_window == 0) {
        fprintf(stderr, "视频窗口创建失败: %s\n", SDL_GetError());
        ui_sdl_destroy(ui);
        return false;
    }

    renderer_flags = SDL_RENDERER_ACCELERATED;
    if (enable_vsync) {
        renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
    }

    ui->video_renderer = SDL_CreateRenderer(
        ui->video_window,
        -1,
        renderer_flags
    );
    if (ui->video_renderer == 0) {
        fprintf(stderr, "视频渲染器创建失败: %s\n", SDL_GetError());
        ui_sdl_destroy(ui);
        return false;
    }

    SDL_SetRenderDrawColor(ui->video_renderer, 0, 0, 0, 255);
    return true;
}

void ui_sdl_destroy(SDLUI *ui) {
    if (ui == 0) {
        return;
    }

    ui_sdl_hide_qr_popup(ui);

    if (ui->video_texture != 0) {
        SDL_DestroyTexture(ui->video_texture);
        ui->video_texture = 0;
    }
    if (ui->video_renderer != 0) {
        SDL_DestroyRenderer(ui->video_renderer);
        ui->video_renderer = 0;
    }
    if (ui->video_window != 0) {
        SDL_DestroyWindow(ui->video_window);
        ui->video_window = 0;
    }

    SDL_Quit();
}

bool ui_sdl_render_frame(SDLUI *ui, const uint8_t *bgr, int width, int height, int stride) {
    if (ui == 0 || bgr == 0) {
        return false;
    }

    if (!ensure_video_texture(ui, width, height)) {
        return false;
    }

    if (SDL_UpdateTexture(ui->video_texture, 0, bgr, stride) != 0) {
        fprintf(stderr, "更新视频纹理失败: %s\n", SDL_GetError());
        return false;
    }

    render_texture_fit(ui->video_renderer, ui->video_texture, width, height);
    return true;
}

void ui_sdl_set_window_title(SDLUI *ui, const char *title) {
    if (ui == 0 || ui->video_window == 0 || title == 0) {
        return;
    }

    SDL_SetWindowTitle(ui->video_window, title);
}

void ui_sdl_toggle_fullscreen(SDLUI *ui) {
    Uint32 flags;

    if (ui == 0 || ui->video_window == 0) {
        return;
    }

    ui->fullscreen = !ui->fullscreen;
    flags = ui->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0U;
    SDL_SetWindowFullscreen(ui->video_window, flags);
}

bool ui_sdl_handle_event(SDLUI *ui, const SDL_Event *event, bool *quit_requested) {
    if (ui == 0 || event == 0 || quit_requested == 0) {
        return false;
    }

    if (event->type == SDL_QUIT) {
        *quit_requested = true;
        return true;
    }

    if (event->type == SDL_WINDOWEVENT &&
        event->window.event == SDL_WINDOWEVENT_CLOSE &&
        ui->qr_window != 0 &&
        event->window.windowID == SDL_GetWindowID(ui->qr_window)) {
        ui_sdl_hide_qr_popup(ui);
        return true;
    }

    if (event->type == SDL_KEYDOWN) {
        if (event->key.keysym.sym == SDLK_F11) {
            ui_sdl_toggle_fullscreen(ui);
            return true;
        }
        if (event->key.keysym.sym == SDLK_ESCAPE) {
            if (ui->fullscreen) {
                ui_sdl_toggle_fullscreen(ui);
            } else if (ui->qr_popup_visible) {
                ui_sdl_hide_qr_popup(ui);
            } else {
                *quit_requested = true;
            }
            return true;
        }
    }

    return false;
}

#ifdef HAVE_QRENCODE
static bool render_qr_texture(SDLUI *ui, const char *qr_text) {
    QRcode *code;
    int margin = 4;
    int scale = 12;
    int img_size;
    int x;
    int y;
    void *pixels = 0;
    int pitch = 0;

    code = QRcode_encodeString8bit(qr_text, 0, QR_ECLEVEL_H);
    if (code == 0) {
        return false;
    }

    img_size = (code->width + margin * 2) * scale;
    if (ui->qr_texture != 0) {
        SDL_DestroyTexture(ui->qr_texture);
        ui->qr_texture = 0;
    }

    ui->qr_texture = SDL_CreateTexture(
        ui->qr_renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        img_size,
        img_size
    );
    if (ui->qr_texture == 0) {
        QRcode_free(code);
        return false;
    }

    if (SDL_LockTexture(ui->qr_texture, 0, &pixels, &pitch) != 0) {
        QRcode_free(code);
        return false;
    }

    memset(pixels, 255, (size_t)pitch * (size_t)img_size);

    for (y = 0; y < code->width; ++y) {
        for (x = 0; x < code->width; ++x) {
            unsigned char value = code->data[y * code->width + x] & 1U;
            int draw_x;
            int draw_y;
            int sx;
            int sy;

            if (!value) {
                continue;
            }

            for (sy = 0; sy < scale; ++sy) {
                Uint32 *row = (Uint32 *)((uint8_t *)pixels + (size_t)((y + margin) * scale + sy) * (size_t)pitch);
                for (sx = 0; sx < scale; ++sx) {
                    draw_x = (x + margin) * scale + sx;
                    draw_y = (y + margin) * scale + sy;
                    (void)draw_y;
                    row[draw_x] = 0x000000FFU;
                }
            }
        }
    }

    SDL_UnlockTexture(ui->qr_texture);
    QRcode_free(code);
    return true;
}
#endif

bool ui_sdl_show_qr_popup(SDLUI *ui, const char *qr_text) {
    if (ui == 0 || qr_text == 0 || qr_text[0] == '\0') {
        return false;
    }

#ifndef HAVE_QRENCODE
    fprintf(stderr, "当前未启用 libqrencode，无法显示二维码弹窗。二维码内容: %s\n", qr_text);
    return false;
#else
    if (ui->qr_window == 0) {
        ui->qr_window = SDL_CreateWindow(
            "检测到新二维码",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            620,
            700,
            SDL_WINDOW_SHOWN
        );
        if (ui->qr_window == 0) {
            return false;
        }

        ui->qr_renderer = SDL_CreateRenderer(
            ui->qr_window,
            -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
        );
        if (ui->qr_renderer == 0) {
            ui_sdl_hide_qr_popup(ui);
            return false;
        }
    }

    if (!render_qr_texture(ui, qr_text)) {
        return false;
    }

    SDL_SetRenderDrawColor(ui->qr_renderer, 245, 245, 245, 255);
    SDL_RenderClear(ui->qr_renderer);
    if (ui->qr_texture != 0) {
        int tex_w;
        int tex_h;
        SDL_Rect dst;

        SDL_QueryTexture(ui->qr_texture, 0, 0, &tex_w, &tex_h);
        dst.w = tex_w;
        dst.h = tex_h;
        dst.x = (620 - tex_w) / 2;
        dst.y = 30;
        SDL_RenderCopy(ui->qr_renderer, ui->qr_texture, 0, &dst);
    }
    SDL_RenderPresent(ui->qr_renderer);
    SDL_ShowWindow(ui->qr_window);
    ui->qr_popup_visible = true;
    return true;
#endif
}

void ui_sdl_hide_qr_popup(SDLUI *ui) {
    if (ui == 0) {
        return;
    }

    if (ui->qr_texture != 0) {
        SDL_DestroyTexture(ui->qr_texture);
        ui->qr_texture = 0;
    }
    if (ui->qr_renderer != 0) {
        SDL_DestroyRenderer(ui->qr_renderer);
        ui->qr_renderer = 0;
    }
    if (ui->qr_window != 0) {
        SDL_DestroyWindow(ui->qr_window);
        ui->qr_window = 0;
    }
    ui->qr_popup_visible = false;
}

bool ui_sdl_is_qr_popup_visible(const SDLUI *ui) {
    return ui != 0 && ui->qr_popup_visible;
}
