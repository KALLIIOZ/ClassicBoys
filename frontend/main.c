/*
 * ClassicBoy - SDL2 frontend entry point.
 *
 * The emulator core is pure C11 and has no dependency on SDL. This file
 * owns the window, renderer, audio device, input handling, ROM library
 * integration and persistence.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "app.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include <commdlg.h>
#include <math.h>
#include <shellapi.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define APP_TITLE "ClassicBoy - Emulador de Game Boy Color (CGB)"
#define APP_FPS 59.7275
#define APP_FRAME_TIME (1.0 / APP_FPS)
#define APP_TOAST_MS 2800u
#define APP_LOGICAL_W 1280
#define APP_LOGICAL_H 800
#define APP_AUDIO_QUEUE_LIMIT (GB_SAMPLE_RATE * 4u / 2u)

const char *const app_palette_names[APP_PALETTE_COUNT] = {
    "DMG verde",
    "Pocket",
    "Ambar",
    "LCD azul",
};

const uint32_t app_palettes[APP_PALETTE_COUNT][4] = {
    {0xFF9BBC0Fu, 0xFF8BAC0Fu, 0xFF306230u, 0xFF0F380Fu},
    {0xFFC4CFA1u, 0xFF8B956Du, 0xFF4D533Cu, 0xFF1F1F1Fu},
    {0xFFFFF1C9u, 0xFFF2C57Cu, 0xFFB5762Eu, 0xFF4A2A0Au},
    {0xFFD6E8F0u, 0xFF8BB6C9u, 0xFF3E6B85u, 0xFF142C3Bu},
};

typedef struct LaunchOptions {
    bool headless;
    bool paused;
    bool capture_ui;
    bool take_screenshot;
    bool has_page;
    AppPage page;
    int frames;
    char capture_path[CB_PATH_MAX];
    const char *rom_path;
} LaunchOptions;

static uint64_t g_perf_freq = 1;
static volatile sig_atomic_t g_interrupted = 0;

static void on_signal(int signal_number)
{
    (void)signal_number;
    g_interrupted = 1;
}

/* ------------------------------------------------------------------ */
/* Windows / path helpers                                             */
/* ------------------------------------------------------------------ */

static bool utf8_to_wide(const char *source, wchar_t *dest, int capacity)
{
    if (!source || !dest || capacity <= 0) {
        return false;
    }
    int written = MultiByteToWideChar(CP_UTF8, 0, source, -1, dest, capacity);
    if (written <= 0) {
        dest[0] = L'\0';
        return false;
    }
    return true;
}

static bool wide_to_utf8(const wchar_t *source, char *dest, int capacity)
{
    if (!source || !dest || capacity <= 0) {
        return false;
    }
    int written = WideCharToMultiByte(CP_UTF8, 0, source, -1, dest, capacity,
                                      NULL, NULL);
    if (written <= 0) {
        dest[0] = '\0';
        return false;
    }
    return true;
}

static bool file_exists_utf8(const char *path)
{
    wchar_t wide[32768];
    if (!utf8_to_wide(path, wide, (int)(sizeof(wide) / sizeof(wide[0])))) {
        return false;
    }
    DWORD attributes = GetFileAttributesW(wide);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool ensure_directory_utf8(const char *path)
{
    wchar_t wide[32768];
    if (!utf8_to_wide(path, wide, (int)(sizeof(wide) / sizeof(wide[0])))) {
        return false;
    }
    DWORD attributes = GetFileAttributesW(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    if (CreateDirectoryW(wide, NULL)) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static void path_join(char *out, size_t out_size, const char *directory,
                      const char *name)
{
    if (!out || out_size == 0) {
        return;
    }
    if (!directory || !name) {
        out[0] = '\0';
        return;
    }
    size_t length = strlen(directory);
    bool separator = length > 0 && (directory[length - 1] == '/' ||
                                    directory[length - 1] == '\\');
    snprintf(out, out_size, "%s%s%s", directory, separator ? "" : "\\", name);
}

static char *trim(char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }
    char *end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
    return text;
}

static bool parse_bool(const char *text)
{
    return strcmp(text, "1") == 0 || strcmp(text, "true") == 0 ||
           strcmp(text, "yes") == 0 || strcmp(text, "on") == 0 ||
           strcmp(text, "si") == 0;
}

static int clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void init_paths(App *app)
{
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(app->base_path, sizeof(app->base_path), "%s", base);
        SDL_free(base);
    } else {
        snprintf(app->base_path, sizeof(app->base_path), ".\\");
    }
    size_t length = strlen(app->base_path);
    if (length > 0 && app->base_path[length - 1] != '/' &&
        app->base_path[length - 1] != '\\') {
        if (length + 2 < sizeof(app->base_path)) {
            app->base_path[length] = '\\';
            app->base_path[length + 1] = '\0';
        }
    }
    snprintf(app->asset_dir, sizeof(app->asset_dir), "%sassets\\",
             app->base_path);
    if (!file_exists_utf8(app->asset_dir)) {
        char fallback[CB_PATH_MAX];
        snprintf(fallback, sizeof(fallback), "assets\\");
        if (file_exists_utf8(fallback)) {
            snprintf(app->asset_dir, sizeof(app->asset_dir), "%s", fallback);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Configuration                                                      */
/* ------------------------------------------------------------------ */

static void config_defaults(AppConfig *config)
{
    memset(config, 0, sizeof(*config));
    config->palette = 0;
    config->volume = 70;
    config->integer_scale = true;
    config->show_fps = true;
    config->fullscreen = false;
    config->window_w = APP_LOGICAL_W;
    config->window_h = APP_LOGICAL_H;
}

void app_load_config(App *app)
{
    if (!app) {
        return;
    }
    config_defaults(&app->config);
    char path[CB_PATH_MAX];
    path_join(path, sizeof(path), app->base_path, "classicboy.cfg");
    FILE *file = fopen(path, "rb");
    if (!file) {
        return;
    }
    char line[2048];
    while (fgets(line, sizeof(line), file)) {
        char *separator = strchr(line, '=');
        if (!separator) {
            continue;
        }
        *separator = '\0';
        char *key = trim(line);
        char *value = trim(separator + 1);
        if (strcmp(key, "palette") == 0) {
            app->config.palette = (int)strtol(value, NULL, 10);
        } else if (strcmp(key, "volume") == 0) {
            app->config.volume = (int)strtol(value, NULL, 10);
        } else if (strcmp(key, "integer_scale") == 0) {
            app->config.integer_scale = parse_bool(value);
        } else if (strcmp(key, "show_fps") == 0) {
            app->config.show_fps = parse_bool(value);
        } else if (strcmp(key, "fullscreen") == 0) {
            app->config.fullscreen = parse_bool(value);
        } else if (strcmp(key, "window_w") == 0) {
            app->config.window_w = (int)strtol(value, NULL, 10);
        } else if (strcmp(key, "window_h") == 0) {
            app->config.window_h = (int)strtol(value, NULL, 10);
        } else if (strcmp(key, "last_rom") == 0) {
            snprintf(app->config.last_rom, sizeof(app->config.last_rom), "%s",
                     value);
        }
    }
    fclose(file);
    app->config.palette =
        clamp_int(app->config.palette, 0, APP_PALETTE_COUNT - 1);
    app->config.volume = clamp_int(app->config.volume, 0, 100);
    app->config.window_w = clamp_int(app->config.window_w, 960, 3840);
    app->config.window_h = clamp_int(app->config.window_h, 600, 2160);
}

static void config_write_now(App *app)
{
    if (!app) {
        return;
    }
    char path[CB_PATH_MAX];
    path_join(path, sizeof(path), app->base_path, "classicboy.cfg");
    FILE *file = fopen(path, "wb");
    if (!file) {
        app->config_dirty = false;
        app_toast(app, "No se pudo guardar classicboy.cfg");
        return;
    }
    fprintf(file, "palette=%d\n", app->config.palette);
    fprintf(file, "volume=%d\n", app->config.volume);
    fprintf(file, "integer_scale=%d\n", app->config.integer_scale ? 1 : 0);
    fprintf(file, "show_fps=%d\n", app->config.show_fps ? 1 : 0);
    fprintf(file, "fullscreen=%d\n", app->config.fullscreen ? 1 : 0);
    fprintf(file, "window_w=%d\n", app->config.window_w);
    fprintf(file, "window_h=%d\n", app->config.window_h);
    fprintf(file, "last_rom=%s\n", app->config.last_rom);
    fclose(file);
    app->config_dirty = false;
}

void app_save_config(App *app)
{
    if (!app) {
        return;
    }
    app->config_dirty = true;
    app->config_save_at = SDL_GetTicks64() + 350u;
}

/* ------------------------------------------------------------------ */
/* Application actions                                                */
/* ------------------------------------------------------------------ */

void app_toast(App *app, const char *format, ...)
{
    if (!app || !format) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(app->toast, sizeof(app->toast), format, arguments);
    va_end(arguments);
    app->toast_until = SDL_GetTicks64() + APP_TOAST_MS;
}

void app_apply_palette(App *app)
{
    if (!app) {
        return;
    }
    int palette_index =
        clamp_int(app->config.palette, 0, APP_PALETTE_COUNT - 1);
    gb_set_dmg_palette(&app->gb, app_palettes[palette_index]);
}

void app_refresh_library(App *app)
{
    if (!app) {
        return;
    }
    roms_scan(&app->roms, app->base_path);
    app->library_scroll = 0;
    app->library_max_scroll = 0;
    if (app->roms.count == 0) {
        app_toast(app, "No se encontraron ROMs en la carpeta roms/");
    } else if (app->roms.count == 1) {
        app_toast(app, "1 juego encontrado");
    } else {
        app_toast(app, "%d juegos encontrados", app->roms.count);
    }
}

bool app_load_rom(App *app, const char *path)
{
    if (!app || !path || path[0] == '\0') {
        return false;
    }
    if (gb_load_rom(&app->gb, path) != GB_OK) {
        app_toast(app, "No se pudo cargar: %s", gb_error(&app->gb));
        return false;
    }
    app_apply_palette(app);
    app->rom_loaded = true;
    snprintf(app->rom_path, sizeof(app->rom_path), "%s", path);
    snprintf(app->rom_title, sizeof(app->rom_title), "%s",
             gb_title(&app->gb));
    snprintf(app->rom_mbc, sizeof(app->rom_mbc), "%s · %s",
             cart_mbc_name(&app->gb.cart),
             app->gb.cart.cgb_only ? "CGB"
                                   : (app->gb.cart.cgb ? "CGB compat."
                                                       : "DMG"));
    app->rom_size = (unsigned)app->gb.cart.rom_size;
    app->rom_battery = app->gb.cart.battery;
    app->paused = false;
    app->fast_forward = false;
    app->frame_accumulator = 0.0;
    app->last_tick = SDL_GetPerformanceCounter();
    app->fps = 0.0;
    app->fps_frames = 0;
    app->fps_last_ticks = app->last_tick;
    snprintf(app->config.last_rom, sizeof(app->config.last_rom), "%s", path);
    app_save_config(app);
    if (app->audio_ok) {
        SDL_ClearQueuedAudio(app->audio_device);
        SDL_PauseAudioDevice(app->audio_device, 0);
    }
    app_toast(app, "Cargado: %s", app->rom_title);
    return true;
}

void app_reset(App *app)
{
    if (!app || !app->rom_loaded) {
        return;
    }
    gb_reset(&app->gb);
    app_apply_palette(app);
    gb_release_buttons(&app->gb);
    app->paused = false;
    app->fast_forward = false;
    app->frame_accumulator = 0.0;
    app->last_tick = SDL_GetPerformanceCounter();
    app->fps = 0.0;
    app->fps_frames = 0;
    app->fps_last_ticks = app->last_tick;
    if (app->audio_ok) {
        SDL_ClearQueuedAudio(app->audio_device);
        SDL_PauseAudioDevice(app->audio_device, 0);
    }
    app_toast(app, "Juego reiniciado");
}

void app_toggle_pause(App *app)
{
    if (!app || !app->rom_loaded) {
        return;
    }
    app->paused = !app->paused;
    app->frame_accumulator = 0.0;
    app->last_tick = SDL_GetPerformanceCounter();
    app->fps_frames = 0;
    app->fps_last_ticks = app->last_tick;
    gb_release_buttons(&app->gb);
    if (app->audio_ok) {
        if (app->paused) {
            SDL_PauseAudioDevice(app->audio_device, 1);
        } else {
            SDL_ClearQueuedAudio(app->audio_device);
            SDL_PauseAudioDevice(app->audio_device, 0);
        }
    }
    app_toast(app, app->paused ? "Juego en pausa" : "Continuando");
}

void app_take_screenshot(App *app)
{
    if (!app || !app->rom_loaded) {
        app_toast(app, "Carga un juego antes de capturar");
        return;
    }
    char directory[CB_PATH_MAX];
    path_join(directory, sizeof(directory), app->base_path, "screenshots");
    if (!ensure_directory_utf8(directory)) {
        app_toast(app, "No se pudo crear la carpeta screenshots/");
        return;
    }

    char name[64];
    char path[CB_PATH_MAX];
    uint64_t index = app->screenshot_index;
    do {
        index++;
        snprintf(name, sizeof(name), "ClassicBoy_%04llu.bmp",
                 (unsigned long long)index);
        path_join(path, sizeof(path), directory, name);
    } while (file_exists_utf8(path) && index < 1000000u);

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
        0, GB_SCREEN_W * 4, GB_SCREEN_H * 4, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) {
        app_toast(app, "No se pudo crear la captura: %s", SDL_GetError());
        return;
    }
    const uint16_t *framebuffer = gb_framebuffer(&app->gb);
    int pitch = surface->pitch / (int)sizeof(uint32_t);
    uint32_t *pixels = (uint32_t *)surface->pixels;
    for (int y = 0; y < GB_SCREEN_H; y++) {
        for (int x = 0; x < GB_SCREEN_W; x++) {
            uint32_t color =
                gb_rgb555_to_argb(framebuffer[y * GB_SCREEN_W + x]);
            for (int dy = 0; dy < 4; dy++) {
                uint32_t *row = pixels + (size_t)(y * 4 + dy) * (size_t)pitch;
                for (int dx = 0; dx < 4; dx++) {
                    row[x * 4 + dx] = color;
                }
            }
        }
    }
    int saved = SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
    if (saved != 0) {
        app_toast(app, "No se pudo guardar la captura: %s", SDL_GetError());
        return;
    }
    app->screenshot_index = index;
    app_toast(app, "Captura guardada: %s", name);
}

void app_save_game(App *app)
{
    if (!app || !app->rom_loaded) {
        app_toast(app, "No hay ningun juego cargado");
        return;
    }
    if (!app->rom_battery) {
        app_toast(app, "Este cartucho no usa guardado con bateria");
        return;
    }
    if (gb_save_battery(&app->gb)) {
        app_toast(app, "Partida guardada en disco");
    } else {
        app_toast(app, "No se pudo guardar la partida");
    }
}

void app_open_dialog(App *app)
{
    if (app) {
        app->open_dialog_requested = true;
    }
}

void app_open_roms_folder(App *app)
{
    if (!app) {
        return;
    }
    char directory[CB_PATH_MAX];
    path_join(directory, sizeof(directory), app->base_path, "roms");
    ensure_directory_utf8(directory);
    wchar_t wide[32768];
    if (!utf8_to_wide(directory, wide, (int)(sizeof(wide) / sizeof(wide[0])))) {
        app_toast(app, "Ruta de ROMs no valida");
        return;
    }
    HINSTANCE result =
        ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        app_toast(app, "No se pudo abrir la carpeta de ROMs");
    }
}

/* ------------------------------------------------------------------ */
/* Input helpers                                                      */
/* ------------------------------------------------------------------ */

static void set_input(App *app, bool *source, GbButton button, bool pressed)
{
    if (!app || button < 0 || button >= GB_BTN_COUNT) {
        return;
    }
    source[button] = pressed;
    bool active = app->key_buttons[button] || app->pad_buttons[button];
    gb_set_button(&app->gb, button, active);
}

static void release_all_inputs(App *app)
{
    if (!app) {
        return;
    }
    memset(app->key_buttons, 0, sizeof(app->key_buttons));
    memset(app->pad_buttons, 0, sizeof(app->pad_buttons));
    gb_release_buttons(&app->gb);
}

static GbButton button_for_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_UP:
        return GB_BTN_UP;
    case SDLK_DOWN:
        return GB_BTN_DOWN;
    case SDLK_LEFT:
        return GB_BTN_LEFT;
    case SDLK_RIGHT:
        return GB_BTN_RIGHT;
    case SDLK_z:
    case SDLK_j:
        return GB_BTN_A;
    case SDLK_x:
    case SDLK_k:
        return GB_BTN_B;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return GB_BTN_START;
    case SDLK_RSHIFT:
    case SDLK_BACKSPACE:
        return GB_BTN_SELECT;
    default:
        return GB_BTN_COUNT;
    }
}

static GbButton button_for_controller(Uint8 button)
{
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return GB_BTN_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return GB_BTN_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return GB_BTN_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return GB_BTN_RIGHT;
    case SDL_CONTROLLER_BUTTON_A:
        return GB_BTN_A;
    case SDL_CONTROLLER_BUTTON_B:
        return GB_BTN_B;
    case SDL_CONTROLLER_BUTTON_START:
        return GB_BTN_START;
    case SDL_CONTROLLER_BUTTON_BACK:
        return GB_BTN_SELECT;
    default:
        return GB_BTN_COUNT;
    }
}

static void toggle_fullscreen(App *app)
{
    if (!app) {
        return;
    }
    if (app->game_window_open && app->game_window) {
        bool target = !app->game_fullscreen;
        if (SDL_SetWindowFullscreen(
                app->game_window,
                target ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
            app_toast(app, "No se pudo cambiar el juego a pantalla completa");
            return;
        }
        app->game_fullscreen = target;
        app->last_tick = SDL_GetPerformanceCounter();
        app->frame_accumulator = 0.0;
        return;
    }
    if (!app->window) {
        return;
    }
    bool target = !app->fullscreen;
    if (SDL_SetWindowFullscreen(app->window,
                                target ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) !=
        0) {
        app_toast(app, "No se pudo cambiar a pantalla completa");
        return;
    }
    app->fullscreen = target;
    app->config.fullscreen = target;
    app_save_config(app);
    app->last_tick = SDL_GetPerformanceCounter();
    app->frame_accumulator = 0.0;
}

static void handle_key(App *app, SDL_Keycode key, bool pressed)
{
    if (!app) {
        return;
    }
    if (pressed) {
        switch (key) {
        case SDLK_F1:
            app->page = APP_PAGE_LIBRARY;
            release_all_inputs(app);
            return;
        case SDLK_F11:
            toggle_fullscreen(app);
            return;
        case SDLK_F12:
            app_take_screenshot(app);
            return;
        case SDLK_F5:
            app_save_game(app);
            return;
        case SDLK_ESCAPE:
        case SDLK_p:
            if (app->rom_loaded) {
                app_toggle_pause(app);
            }
            return;
        case SDLK_SPACE:
            if (app->rom_loaded && !app->fast_forward) {
                app->fast_forward = true;
                if (app->audio_ok) {
                    SDL_ClearQueuedAudio(app->audio_device);
                }
            }
            return;
        case SDLK_o:
            if ((SDL_GetModState() & KMOD_CTRL) != 0) {
                app_open_dialog(app);
            }
            return;
        default:
            break;
        }
    } else if (key == SDLK_SPACE) {
        if (app->fast_forward) {
            app->fast_forward = false;
            if (app->audio_ok) {
                SDL_ClearQueuedAudio(app->audio_device);
            }
        }
        return;
    }

    GbButton button = button_for_key(key);
    if (button == GB_BTN_COUNT) {
        return;
    }
    if (!pressed) {
        set_input(app, app->key_buttons, button, false);
        return;
    }
    if (!app->rom_loaded || app->paused) {
        return;
    }
    set_input(app, app->key_buttons, button, true);
}

static void handle_controller_button(App *app, Uint8 button, bool pressed)
{
    if (!app) {
        return;
    }
    GbButton target = button_for_controller(button);
    if (target == GB_BTN_COUNT) {
        return;
    }
    if (!pressed) {
        set_input(app, app->pad_buttons, target, false);
        return;
    }
    if (!app->rom_loaded || app->paused) {
        return;
    }
    set_input(app, app->pad_buttons, target, true);
}

static void open_controller(App *app, int device_index)
{
    if (!app || app->controller) {
        return;
    }
    if (!SDL_IsGameController(device_index)) {
        return;
    }
    SDL_GameController *controller = SDL_GameControllerOpen(device_index);
    if (!controller) {
        return;
    }
    app->controller = controller;
    const char *name = SDL_GameControllerName(controller);
    snprintf(app->controller_name, sizeof(app->controller_name), "%s",
             name ? name : "Mando");
    app_toast(app, "Mando conectado: %s", app->controller_name);
}

static void close_controller(App *app, Sint32 instance_id)
{
    if (!app || !app->controller) {
        return;
    }
    SDL_Joystick *joystick = SDL_GameControllerGetJoystick(app->controller);
    if (joystick && SDL_JoystickInstanceID(joystick) == instance_id) {
        SDL_GameControllerClose(app->controller);
        app->controller = NULL;
        app->controller_name[0] = '\0';
        memset(app->pad_buttons, 0, sizeof(app->pad_buttons));
        gb_release_buttons(&app->gb);
        app_toast(app, "Mando desconectado");
    }
}

static void update_mouse(App *app, int window_x, int window_y)
{
    if (!app || !app->renderer) {
        return;
    }
    float logical_x = 0.0f;
    float logical_y = 0.0f;
    SDL_RenderWindowToLogical(app->renderer, window_x, window_y, &logical_x,
                              &logical_y);
    app->ui.mouse_x = (int)lroundf(logical_x);
    app->ui.mouse_y = (int)lroundf(logical_y);
}

static void open_rom_dialog(App *app)
{
    if (!app) {
        return;
    }
    wchar_t file[32768];
    file[0] = L'\0';
    OPENFILENAMEW dialog;
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (app->window && SDL_GetWindowWMInfo(app->window, &wm) == SDL_TRUE &&
        wm.subsystem == SDL_SYSWM_WINDOWS) {
        dialog.hwndOwner = wm.info.win.window;
    }
    static const wchar_t filter[] =
        L"ROMs de Game Boy / Game Boy Color (*.gb;*.gbc)\0*.gb;*.gbc\0"
        L"Todos los archivos (*.*)\0*.*\0";
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = file;
    dialog.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
    dialog.lpstrTitle = L"Abrir ROM de Game Boy / Game Boy Color";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) {
        char utf8[32768];
        if (wide_to_utf8(file, utf8, (int)sizeof(utf8))) {
            if (app_load_rom(app, utf8)) {
                app->page = app_open_game_window(app) ? APP_PAGE_LIBRARY
                                                      : APP_PAGE_GAME;
            }
        }
    }
    app->ui.mouse_down = false;
    app->ui.mouse_pressed = false;
    app->ui.mouse_released = false;
    app->last_tick = SDL_GetPerformanceCounter();
    app->frame_accumulator = 0.0;
}

/* ------------------------------------------------------------------ */
/* Audio / video                                                      */
/* ------------------------------------------------------------------ */

static void queue_audio(App *app)
{
    size_t frames =
        gb_audio_read(&app->gb, app->audio_mix, GB_APU_MAX_SAMPLES);
    if (frames == 0 || !app->audio_ok) {
        return;
    }
    if (app->paused || app->fast_forward || app->config.volume <= 0) {
        return;
    }
    if (SDL_GetQueuedAudioSize(app->audio_device) > APP_AUDIO_QUEUE_LIMIT) {
        return;
    }
    if (app->config.volume < 100) {
        int volume = app->config.volume;
        size_t samples = frames * 2;
        for (size_t i = 0; i < samples; i++) {
            int value = (int)app->audio_mix[i] * volume / 100;
            app->audio_mix[i] = (int16_t)value;
        }
    }
    SDL_QueueAudio(app->audio_device, app->audio_mix,
                   (Uint32)(frames * 2 * sizeof(int16_t)));
}

static void run_emulated_frame(App *app)
{
    gb_run_frame(&app->gb);
    queue_audio(app);
}

static void update_screen_texture(App *app)
{
    if (!app || (!app->screen_texture && !app->game_texture)) {
        return;
    }
    const uint16_t *framebuffer = gb_framebuffer(&app->gb);
    if (!framebuffer) {
        return;
    }
    for (size_t i = 0; i < GB_SCREEN_W * GB_SCREEN_H; i++) {
        app->screen_pixels[i] = gb_rgb555_to_argb(framebuffer[i]);
    }
    if (app->screen_texture) {
        SDL_UpdateTexture(app->screen_texture, NULL, app->screen_pixels,
                          GB_SCREEN_W * (int)sizeof(uint32_t));
    }
    if (app->game_texture) {
        SDL_UpdateTexture(app->game_texture, NULL, app->screen_pixels,
                          GB_SCREEN_W * (int)sizeof(uint32_t));
    }
}

bool app_open_game_window(App *app)
{
    if (!app || !app->rom_loaded) {
        return false;
    }
    if (app->game_window) {
        char title[128];
        snprintf(title, sizeof(title), "%s - ClassicBoy", app->rom_title);
        SDL_SetWindowTitle(app->game_window, title);
        SDL_ShowWindow(app->game_window);
        SDL_RaiseWindow(app->game_window);
        app->game_window_open = true;
        return true;
    }

    char title[128];
    snprintf(title, sizeof(title), "%s - ClassicBoy", app->rom_title);
    app->game_window = SDL_CreateWindow(
        title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!app->game_window) {
        char message[256];
        snprintf(message, sizeof(message),
                 "No se pudo crear la ventana del juego: %s", SDL_GetError());
        app_toast(app, "%s", message);
        return false;
    }
    SDL_SetWindowMinimumSize(app->game_window, 320, 288);

    app->game_renderer =
        SDL_CreateRenderer(app->game_window, -1, SDL_RENDERER_ACCELERATED);
    if (!app->game_renderer) {
        app->game_renderer =
            SDL_CreateRenderer(app->game_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!app->game_renderer) {
        char message[256];
        snprintf(message, sizeof(message),
                 "No se pudo crear el renderizador del juego: %s",
                 SDL_GetError());
        SDL_DestroyWindow(app->game_window);
        app->game_window = NULL;
        app_toast(app, "%s", message);
        return false;
    }

    app->game_texture = SDL_CreateTexture(
        app->game_renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, GB_SCREEN_W, GB_SCREEN_H);
    if (!app->game_texture) {
        char message[256];
        snprintf(message, sizeof(message),
                 "No se pudo crear la pantalla del juego: %s", SDL_GetError());
        SDL_DestroyRenderer(app->game_renderer);
        SDL_DestroyWindow(app->game_window);
        app->game_renderer = NULL;
        app->game_window = NULL;
        app_toast(app, "%s", message);
        return false;
    }
    SDL_SetTextureBlendMode(app->game_texture, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(app->game_texture, SDL_ScaleModeNearest);
    SDL_SetRenderDrawColor(app->game_renderer, 0, 0, 0, 255);
    update_screen_texture(app);
    SDL_ShowWindow(app->game_window);
    SDL_RaiseWindow(app->game_window);
    app->game_window_open = true;
    return true;
}

static void render_game_window(App *app)
{
    if (!app || !app->game_window_open || !app->game_renderer ||
        !app->game_texture) {
        return;
    }
    int output_w = 0;
    int output_h = 0;
    if (SDL_GetRendererOutputSize(app->game_renderer, &output_w, &output_h) !=
            0 ||
        output_w <= 0 || output_h <= 0) {
        return;
    }
    double scale = fmin((double)output_w / GB_SCREEN_W,
                        (double)output_h / GB_SCREEN_H);
    int screen_w;
    int screen_h;
    if (app->config.integer_scale) {
        int integer_scale = (int)floor(scale);
        if (integer_scale < 1) {
            integer_scale = 1;
        }
        screen_w = GB_SCREEN_W * integer_scale;
        screen_h = GB_SCREEN_H * integer_scale;
    } else {
        screen_w = (int)floor(GB_SCREEN_W * scale);
        screen_h = (int)floor(GB_SCREEN_H * scale);
    }
    SDL_Rect destination = {(output_w - screen_w) / 2,
                           (output_h - screen_h) / 2, screen_w, screen_h};
    SDL_RenderClear(app->game_renderer);
    SDL_RenderCopy(app->game_renderer, app->game_texture, NULL, &destination);
    SDL_RenderPresent(app->game_renderer);
}

/* ------------------------------------------------------------------ */
/* Event loop                                                         */
/* ------------------------------------------------------------------ */

static void process_events(App *app)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            app->should_quit = true;
            break;
        case SDL_KEYDOWN:
            if (!event.key.repeat) {
                handle_key(app, event.key.keysym.sym, true);
            }
            break;
        case SDL_KEYUP:
            handle_key(app, event.key.keysym.sym, false);
            break;
        case SDL_MOUSEMOTION:
            if (event.motion.windowID == SDL_GetWindowID(app->window)) {
                update_mouse(app, event.motion.x, event.motion.y);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.windowID == SDL_GetWindowID(app->window) &&
                event.button.button == SDL_BUTTON_LEFT) {
                update_mouse(app, event.button.x, event.button.y);
                app->ui.mouse_down = true;
                app->ui.mouse_pressed = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.windowID == SDL_GetWindowID(app->window) &&
                event.button.button == SDL_BUTTON_LEFT) {
                update_mouse(app, event.button.x, event.button.y);
                app->ui.mouse_down = false;
                app->ui.mouse_released = true;
            }
            break;
        case SDL_MOUSEWHEEL:
            if (event.wheel.windowID == SDL_GetWindowID(app->window) &&
                app->page == APP_PAGE_LIBRARY) {
                app->library_scroll -= event.wheel.y * 72;
                if (app->library_scroll < 0) {
                    app->library_scroll = 0;
                }
            }
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                if (app->game_window &&
                    event.window.windowID == SDL_GetWindowID(app->game_window)) {
                    if (app->game_fullscreen) {
                        SDL_SetWindowFullscreen(app->game_window, 0);
                        app->game_fullscreen = false;
                    }
                    SDL_HideWindow(app->game_window);
                    app->game_window_open = false;
                    release_all_inputs(app);
                } else if (event.window.windowID ==
                           SDL_GetWindowID(app->window)) {
                    app->should_quit = true;
                }
                break;
            }
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                release_all_inputs(app);
                app->fast_forward = false;
                app->ui.mouse_down = false;
                app->ui.mouse_pressed = false;
                app->ui.mouse_released = false;
            } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                app->last_tick = SDL_GetPerformanceCounter();
                app->frame_accumulator = 0.0;
            }
            break;
        case SDL_DROPFILE:
            if (event.drop.file) {
                if (app_load_rom(app, event.drop.file)) {
                    app->page = app_open_game_window(app) ? APP_PAGE_LIBRARY
                                                          : APP_PAGE_GAME;
                }
                SDL_free(event.drop.file);
            }
            break;
        case SDL_CONTROLLERDEVICEADDED:
            open_controller(app, event.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            close_controller(app, event.cdevice.which);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            handle_controller_button(app, event.cbutton.button, true);
            break;
        case SDL_CONTROLLERBUTTONUP:
            handle_controller_button(app, event.cbutton.button, false);
            break;
        default:
            break;
        }
    }
}

static void update_fps(App *app)
{
    uint64_t now = SDL_GetPerformanceCounter();
    app->fps_frames++;
    uint64_t elapsed = now - app->fps_last_ticks;
    if (elapsed >= g_perf_freq / 2u) {
        app->fps = (double)app->fps_frames * (double)g_perf_freq /
                   (double)elapsed;
        app->fps_frames = 0;
        app->fps_last_ticks = now;
    }
}

/* ------------------------------------------------------------------ */
/* Command line / diagnostics                                         */
/* ------------------------------------------------------------------ */

static void print_usage(const char *program)
{
    printf("ClassicBoy %s - Emulador de Game Boy Color (CGB)\n", APP_VERSION);
    printf("Uso: %s [opciones] [archivo.gb|archivo.gbc]\n\n", program);
    printf("Opciones:\n");
    printf("  --rom RUTA       Carga una ROM al iniciar\n");
    printf("  --page NOMBRE    Pagina inicial: library, game, settings,\n");
    printf("                   controls o about\n");
    printf("  --paused         Inicia el juego en pausa\n");
    printf("  --frames N       Ejecuta N fotogramas y sale (modo de prueba)\n");
    printf("  --capture RUTA   Guarda la interfaz renderizada como BMP\n");
    printf("  --screenshot     Guarda una captura 4x del juego (modo de prueba)\n");
    printf("  --headless       Usa los controladores dummy de SDL (sin ventana)\n");
    printf("  --help           Muestra esta ayuda\n");
}

static bool parse_page(const char *name, AppPage *page)
{
    if (!name || !page) {
        return false;
    }
    if (strcmp(name, "library") == 0 || strcmp(name, "biblioteca") == 0) {
        *page = APP_PAGE_LIBRARY;
    } else if (strcmp(name, "game") == 0 || strcmp(name, "juego") == 0) {
        *page = APP_PAGE_GAME;
    } else if (strcmp(name, "settings") == 0 ||
               strcmp(name, "ajustes") == 0) {
        *page = APP_PAGE_SETTINGS;
    } else if (strcmp(name, "controls") == 0 ||
               strcmp(name, "controles") == 0) {
        *page = APP_PAGE_CONTROLS;
    } else if (strcmp(name, "about") == 0 || strcmp(name, "acerca") == 0) {
        *page = APP_PAGE_ABOUT;
    } else {
        return false;
    }
    return true;
}

static bool parse_launch_options(int argc, char **argv,
                                 LaunchOptions *options)
{
    memset(options, 0, sizeof(*options));
    options->frames = -1;
    options->page = APP_PAGE_LIBRARY;
    for (int i = 1; i < argc; i++) {
        const char *argument = argv[i];
        if (!argument) {
            continue;
        }
        if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argument, "--headless") == 0) {
            options->headless = true;
        } else if (strcmp(argument, "--paused") == 0) {
            options->paused = true;
        } else if (strcmp(argument, "--screenshot") == 0) {
            options->take_screenshot = true;
        } else if (strcmp(argument, "--rom") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "ClassicBoy: --rom necesita una ruta\n");
                return false;
            }
            options->rom_path = argv[++i];
        } else if (strcmp(argument, "--page") == 0) {
            if (i + 1 >= argc || !parse_page(argv[i + 1], &options->page)) {
                fprintf(stderr, "ClassicBoy: pagina no valida\n");
                return false;
            }
            options->has_page = true;
            i++;
        } else if (strcmp(argument, "--frames") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "ClassicBoy: --frames necesita un numero\n");
                return false;
            }
            char *end = NULL;
            long value = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0' || value < 0 || value > 10000000) {
                fprintf(stderr, "ClassicBoy: numero de fotogramas no valido\n");
                return false;
            }
            options->frames = (int)value;
        } else if (strcmp(argument, "--capture") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "ClassicBoy: --capture necesita una ruta\n");
                return false;
            }
            snprintf(options->capture_path, sizeof(options->capture_path),
                     "%s", argv[++i]);
            options->capture_ui = true;
        } else if (argument[0] == '-') {
            fprintf(stderr, "ClassicBoy: opcion desconocida: %s\n", argument);
            return false;
        } else if (!options->rom_path) {
            options->rom_path = argument;
        }
    }
    if (options->frames < 0 && (options->headless || options->capture_ui ||
                                options->take_screenshot)) {
        options->frames = options->take_screenshot ? 60 : 3;
    }
    if (options->headless) {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    }
    return true;
}

static bool save_render_capture(App *app, const char *path)
{
    if (!app || !app->renderer || !path || path[0] == '\0') {
        return false;
    }
    int width = 0;
    int height = 0;
    if (SDL_GetRendererOutputSize(app->renderer, &width, &height) != 0 ||
        width <= 0 || height <= 0) {
        return false;
    }
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
        0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) {
        return false;
    }
    bool ok = SDL_RenderReadPixels(app->renderer, NULL,
                                   SDL_PIXELFORMAT_ARGB8888, surface->pixels,
                                   surface->pitch) == 0;
    if (ok) {
        ok = SDL_SaveBMP(surface, path) == 0;
    }
    SDL_FreeSurface(surface);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Initialization / shutdown                                          */
/* ------------------------------------------------------------------ */

static bool show_error(const char *message)
{
    fprintf(stderr, "ClassicBoy: %s\n", message);
    if (SDL_WasInit(SDL_INIT_VIDEO) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ClassicBoy", message,
                                 NULL);
    }
    return false;
}

static bool init_audio(App *app)
{
    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    SDL_zero(desired);
    desired.freq = GB_SAMPLE_RATE;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    app->audio_device = SDL_OpenAudioDevice(
        NULL, 0, &desired, &obtained,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (app->audio_device == 0) {
        app->audio_ok = false;
        return true;
    }
    app->audio_ok = true;
    gb_set_sample_rate(&app->gb, obtained.freq > 0 ? (uint32_t)obtained.freq
                                                   : GB_SAMPLE_RATE);
    SDL_PauseAudioDevice(app->audio_device, 1);
    return true;
}

static bool init_app(App *app, const LaunchOptions *options)
{
    memset(app, 0, sizeof(*app));
    config_defaults(&app->config);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER |
                 SDL_INIT_EVENTS) != 0) {
        char message[512];
        snprintf(message, sizeof(message), "No se pudo iniciar SDL2: %s",
                 SDL_GetError());
        return show_error(message);
    }

    init_paths(app);
    app_load_config(app);

    Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (app->config.fullscreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    app->window = SDL_CreateWindow(APP_TITLE, SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   app->config.window_w, app->config.window_h,
                                   window_flags);
    if (!app->window) {
        char message[512];
        snprintf(message, sizeof(message), "No se pudo crear la ventana: %s",
                 SDL_GetError());
        return show_error(message);
    }
    app->fullscreen = app->config.fullscreen;
    SDL_SetWindowMinimumSize(app->window, 960, 600);

    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED |
                            SDL_RENDERER_PRESENTVSYNC;
    app->renderer = SDL_CreateRenderer(app->window, -1, renderer_flags);
    if (!app->renderer) {
        app->renderer = SDL_CreateRenderer(app->window, -1,
                                           SDL_RENDERER_ACCELERATED);
    }
    if (!app->renderer) {
        app->renderer =
            SDL_CreateRenderer(app->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!app->renderer) {
        char message[512];
        snprintf(message, sizeof(message),
                 "No se pudo crear el renderizador: %s", SDL_GetError());
        return show_error(message);
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(app->renderer, &info) == 0) {
        snprintf(app->renderer_name, sizeof(app->renderer_name), "%s",
                 info.name ? info.name : "SDL2");
        app->vsync = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
    } else {
        snprintf(app->renderer_name, sizeof(app->renderer_name), "SDL2");
        app->vsync = false;
    }
    SDL_RenderSetLogicalSize(app->renderer, APP_LOGICAL_W, APP_LOGICAL_H);
    SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);

    app->screen_texture = SDL_CreateTexture(
        app->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        GB_SCREEN_W, GB_SCREEN_H);
    if (!app->screen_texture) {
        char message[512];
        snprintf(message, sizeof(message),
                 "No se pudo crear la textura de pantalla: %s",
                 SDL_GetError());
        return show_error(message);
    }
    SDL_SetTextureBlendMode(app->screen_texture, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(app->screen_texture, SDL_ScaleModeNearest);

    if (!ui_init(&app->ui, app->renderer, app->asset_dir)) {
        return show_error(
            "No se pudieron cargar las tipografias en assets/fonts/. "
            "Comprueba que la carpeta assets este junto al ejecutable.");
    }

    gb_init(&app->gb);
    app_apply_palette(app);
    if (!init_audio(app)) {
        return false;
    }
    SDL_GameControllerEventState(SDL_ENABLE);
    int joysticks = SDL_NumJoysticks();
    for (int i = 0; i < joysticks && !app->controller; i++) {
        open_controller(app, i);
    }

    app->page = APP_PAGE_LIBRARY;
    app->start_ticks = SDL_GetTicks64();
    app->last_tick = SDL_GetPerformanceCounter();
    app->fps_last_ticks = app->last_tick;
    app->toast_until = 0;
    app->screenshot_index = 0;
    app_refresh_library(app);

    if (app->config.last_rom[0] != '\0') {
        if (file_exists_utf8(app->config.last_rom) &&
            app_load_rom(app, app->config.last_rom)) {
            /* Keep the library visible; the game is ready in the sidebar. */
        } else {
            app->config.last_rom[0] = '\0';
            app_save_config(app);
        }
    }
    if (options->rom_path && options->rom_path[0] != '\0') {
        if (app_load_rom(app, options->rom_path)) {
            app->page = APP_PAGE_GAME;
        }
    }
    if (options->has_page) {
        app->page = options->page;
    }
    if (options->paused && app->rom_loaded) {
        app->paused = true;
        if (app->audio_ok) {
            SDL_PauseAudioDevice(app->audio_device, 1);
        }
    }
    if (options->headless || options->frames >= 0 || options->capture_ui) {
        fprintf(stderr,
                "ClassicBoy test: renderer=%s vsync=%d audio=%d rom=%s "
                "page=%d paused=%d\n",
                app->renderer_name, app->vsync ? 1 : 0,
                app->audio_ok ? 1 : 0,
                app->rom_loaded ? app->rom_title : "(none)", (int)app->page,
                app->paused ? 1 : 0);
    }
    return true;
}

static void shutdown_app(App *app)
{
    if (!app) {
        return;
    }
    if (app->rom_loaded) {
        gb_save_battery(&app->gb);
    }
    if (app->game_texture) {
        SDL_DestroyTexture(app->game_texture);
        app->game_texture = NULL;
    }
    if (app->game_renderer) {
        SDL_DestroyRenderer(app->game_renderer);
        app->game_renderer = NULL;
    }
    if (app->game_window) {
        SDL_DestroyWindow(app->game_window);
        app->game_window = NULL;
    }
    if (app->window && !app->fullscreen) {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(app->window, &width, &height);
        if (width > 0 && height > 0) {
            app->config.window_w = width;
            app->config.window_h = height;
        }
    }
    config_write_now(app);
    if (app->controller) {
        SDL_GameControllerClose(app->controller);
        app->controller = NULL;
    }
    if (app->audio_device) {
        SDL_CloseAudioDevice(app->audio_device);
        app->audio_device = 0;
    }
    gb_destroy(&app->gb);
    ui_shutdown(&app->ui);
    if (app->screen_texture) {
        SDL_DestroyTexture(app->screen_texture);
        app->screen_texture = NULL;
    }
    if (app->renderer) {
        SDL_DestroyRenderer(app->renderer);
        app->renderer = NULL;
    }
    if (app->window) {
        SDL_DestroyWindow(app->window);
        app->window = NULL;
    }
    SDL_Quit();
}

/* ------------------------------------------------------------------ */
/* Deterministic test loop                                            */
/* ------------------------------------------------------------------ */

static int run_test_mode(App *app, const LaunchOptions *options)
{
    int requested = options->frames > 0 ? options->frames : 0;
    int total = requested > 0 ? requested : 1;
    app->toast[0] = '\0';
    app->toast_until = 0;
    app->fps = APP_FPS;

    for (int i = 0; i < total; i++) {
        if (i < requested && app->rom_loaded && !app->paused) {
            run_emulated_frame(app);
        }
        update_screen_texture(app);
        SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
        SDL_RenderClear(app->renderer);
        pages_draw(app);
        if (i == total - 1) {
            if (options->capture_ui) {
                if (save_render_capture(app, options->capture_path)) {
                    printf("captura de interfaz: %s\n",
                           options->capture_path);
                } else {
                    fprintf(stderr, "no se pudo guardar la captura: %s\n",
                            SDL_GetError());
                    SDL_RenderPresent(app->renderer);
                    return 1;
                }
            }
            if (options->take_screenshot) {
                app_take_screenshot(app);
                if (app->screenshot_index == 0) {
                    fprintf(stderr, "no se pudo guardar la captura del juego\n");
                    SDL_RenderPresent(app->renderer);
                    return 1;
                }
            }
        }
        SDL_RenderPresent(app->renderer);
    }
    printf("prueba completada: %d fotogramas, rom=%s, pagina=%d\n", requested,
           app->rom_loaded ? app->rom_title : "(ninguna)", (int)app->page);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                        */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    LaunchOptions options;
    if (!parse_launch_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 2;
    }

    App app;
    if (!init_app(&app, &options)) {
        shutdown_app(&app);
        return 1;
    }

    g_perf_freq = SDL_GetPerformanceFrequency();
    if (g_perf_freq == 0) {
        g_perf_freq = 1;
    }
    app.last_tick = SDL_GetPerformanceCounter();
    app.fps_last_ticks = app.last_tick;

    if (options.frames >= 0) {
        int result = run_test_mode(&app, &options);
        shutdown_app(&app);
        return result;
    }
    if (app.rom_loaded && (options.rom_path || app.page == APP_PAGE_GAME)) {
        if (app_open_game_window(&app)) {
            if (app.page == APP_PAGE_GAME) {
                app.page = APP_PAGE_LIBRARY;
            }
        } else if (options.rom_path) {
            app.page = APP_PAGE_GAME;
        }
    }

    uint64_t next_present = SDL_GetPerformanceCounter();
    while (!app.should_quit && !g_interrupted) {
        process_events(&app);

        uint64_t now = SDL_GetPerformanceCounter();
        double delta = (double)(now - app.last_tick) / (double)g_perf_freq;
        app.last_tick = now;
        if (delta < 0.0) {
            delta = 0.0;
        }
        if (delta > 0.25) {
            delta = 0.25;
        }

        if (app.rom_loaded && !app.paused) {
            double speed = app.fast_forward ? 4.0 : 1.0;
            app.frame_accumulator += delta * speed;
            int max_frames = app.fast_forward ? 8 : 3;
            int ran = 0;
            while (app.frame_accumulator >= APP_FRAME_TIME &&
                   ran < max_frames) {
                run_emulated_frame(&app);
                app.frame_accumulator -= APP_FRAME_TIME;
                ran++;
            }
            if (app.frame_accumulator > APP_FRAME_TIME * (double)max_frames) {
                app.frame_accumulator = APP_FRAME_TIME;
            }
            if (ran > 0) {
                update_fps(&app);
            }
        } else {
            app.frame_accumulator = 0.0;
            app.fps = 0.0;
            app.fps_frames = 0;
            app.fps_last_ticks = now;
        }

        update_screen_texture(&app);
        SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 255);
        SDL_RenderClear(app.renderer);
        pages_draw(&app);
        SDL_RenderPresent(app.renderer);
        render_game_window(&app);

        if (app.open_dialog_requested) {
            app.open_dialog_requested = false;
            open_rom_dialog(&app);
            next_present = SDL_GetPerformanceCounter();
        }

        if (app.config_dirty && SDL_GetTicks64() >= app.config_save_at) {
            config_write_now(&app);
        }

        if (!app.vsync) {
            next_present +=
                (uint64_t)((double)g_perf_freq / APP_FPS);
            uint64_t current = SDL_GetPerformanceCounter();
            if (current < next_present) {
                double wait = (double)(next_present - current) * 1000.0 /
                              (double)g_perf_freq;
                if (wait > 0.0) {
                    SDL_Delay((Uint32)wait);
                }
            } else {
                next_present = current;
            }
        }
    }

    shutdown_app(&app);
    return 0;
}
