/*
 * ClassicBoy - shared application state for the SDL2 frontend.
 */
#ifndef CB_APP_H
#define CB_APP_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "classicboy/gb.h"
#include "roms.h"
#include "ui.h"

#define APP_VERSION "2.2.0"
#define APP_PALETTE_COUNT 4

typedef enum AppPage {
    APP_PAGE_LIBRARY = 0,
    APP_PAGE_GAME,
    APP_PAGE_SETTINGS,
    APP_PAGE_CONTROLS,
    APP_PAGE_ABOUT
} AppPage;

typedef struct AppConfig {
    int palette;
    int volume;
    bool integer_scale;
    bool show_fps;
    bool fullscreen;
    int window_w;
    int window_h;
    char last_rom[CB_PATH_MAX];
} AppConfig;

typedef struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *screen_texture;
    SDL_Window *game_window;
    SDL_Renderer *game_renderer;
    SDL_Texture *game_texture;
    bool game_window_open;
    bool game_fullscreen;
    bool vsync;
    Ui ui;

    char base_path[CB_PATH_MAX];
    char asset_dir[CB_PATH_MAX];
    char renderer_name[64];

    Gb gb;
    bool rom_loaded;
    bool paused;
    bool fast_forward;
    bool fullscreen;
    bool should_quit;
    bool now_menu_open;
    char rom_path[CB_PATH_MAX];
    char rom_title[64];
    char rom_mbc[64];
    unsigned rom_size;
    bool rom_battery;

    AppPage page;
    Roms roms;
    int library_scroll;
    int library_max_scroll;

    AppConfig config;
    bool config_dirty;
    uint64_t config_save_at;
    bool open_dialog_requested;

    SDL_AudioDeviceID audio_device;
    bool audio_ok;
    int16_t audio_mix[GB_APU_MAX_SAMPLES * 2];

    SDL_GameController *controller;
    char controller_name[128];
    bool key_buttons[GB_BTN_COUNT];
    bool pad_buttons[GB_BTN_COUNT];

    char toast[256];
    uint64_t toast_until;
    uint64_t start_ticks;

    double fps;
    uint64_t fps_frames;
    uint64_t fps_last_ticks;

    double frame_accumulator;
    uint64_t last_tick;
    uint64_t screenshot_index;
    uint32_t screen_pixels[GB_SCREEN_W * GB_SCREEN_H];
} App;

extern const char *const app_palette_names[APP_PALETTE_COUNT];
extern const uint32_t app_palettes[APP_PALETTE_COUNT][4];

void app_toast(App *app, const char *fmt, ...);
bool app_load_rom(App *app, const char *path);
void app_reset(App *app);
void app_refresh_library(App *app);
void app_open_dialog(App *app);
bool app_open_game_window(App *app);
void app_open_roms_folder(App *app);
void app_toggle_pause(App *app);
void app_stop_game(App *app);
void app_take_screenshot(App *app);
void app_save_game(App *app);
void app_save_config(App *app);
void app_load_config(App *app);
void app_apply_palette(App *app);

void pages_draw(App *app);

#endif /* CB_APP_H */
