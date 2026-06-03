#ifndef UI_SDL_H
#define UI_SDL_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL.h>

#include "app_types.h"

typedef struct SDLUI {
    SDL_Window *video_window;
    SDL_Renderer *video_renderer;
    SDL_Texture *video_texture;
    SDL_Window *qr_window;
    SDL_Renderer *qr_renderer;
    SDL_Texture *qr_texture;
    bool fullscreen;
    bool qr_popup_visible;
    int frame_width;
    int frame_height;
} SDLUI;

bool ui_sdl_init(SDLUI *ui, int width, int height, bool enable_vsync);
void ui_sdl_destroy(SDLUI *ui);
bool ui_sdl_render_frame(SDLUI *ui, const uint8_t *bgr, int width, int height, int stride);
void ui_sdl_set_window_title(SDLUI *ui, const char *title);
void ui_sdl_toggle_fullscreen(SDLUI *ui);
bool ui_sdl_handle_event(SDLUI *ui, const SDL_Event *event, bool *quit_requested);
bool ui_sdl_show_qr_popup(SDLUI *ui, const char *qr_text);
void ui_sdl_hide_qr_popup(SDLUI *ui);
bool ui_sdl_is_qr_popup_visible(const SDLUI *ui);

#endif
