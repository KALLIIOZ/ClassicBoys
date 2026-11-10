#include "app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint32_t cartridge_colors[6] = {
    UI_RGB(0x8B, 0xE0, 0x4E), UI_RGB(0x5A, 0xA9, 0xFF),
    UI_RGB(0xF5, 0xB8, 0x4B), UI_RGB(0xC0, 0x84, 0xFC),
    UI_RGB(0xFF, 0x7A, 0x7A), UI_RGB(0x4F, 0xD1, 0xC5),
};

static void ellipsize(const UiFont *font, const char *text, int max_width,
                      char *out, size_t out_size)
{
    if (!text || !out || out_size == 0) {
        return;
    }
    if (max_width <= 0) {
        out[0] = '\0';
        return;
    }
    if (font_text_width_utf8(font, text) <= max_width) {
        snprintf(out, out_size, "%s", text);
        return;
    }
    size_t len = strlen(text);
    while (len > 0) {
        while (len > 0 && ((unsigned char)text[len] & 0xC0u) == 0x80u) {
            len--;
        }
        char buffer[512];
        snprintf(buffer, sizeof(buffer), "%.*s...", (int)len, text);
        if (font_text_width_utf8(font, buffer) <= max_width) {
            snprintf(out, out_size, "%s", buffer);
            return;
        }
        len--;
    }
    snprintf(out, out_size, "...");
}

static void format_size(unsigned size, char *out, size_t out_size)
{
    if (size >= 1024u * 1024u) {
        snprintf(out, out_size, "%.1f MB", (double)size / (1024.0 * 1024.0));
    } else {
        snprintf(out, out_size, "%u KB", (size + 1023u) / 1024u);
    }
}

static void draw_logo(App *app, int x, int y, int size)
{
    Ui *ui = &app->ui;
    ui_round_rect(ui, (SDL_Rect){x, y, size, size}, size / 4, UI_ACCENT);
    SDL_Rect text_area = {x, y, size, size};
    ui_text_centered(ui, &ui->bold, "CB", text_area, UI_ACCENT_TEXT);
}

static void draw_sidebar(App *app)
{
    Ui *ui = &app->ui;
    ui_fill_rect(ui, (SDL_Rect){0, 0, 248, 800}, UI_SIDEBAR);
    ui_fill_rect(ui, (SDL_Rect){247, 0, 1, 800}, UI_BORDER_SOFT);

    draw_logo(app, 20, 24, 46);
    ui_text(ui, &ui->title, "ClassicBoy", 78, 22, UI_TEXT);
    ui_text(ui, &ui->small, "Emulador Game Boy Color", 78, 60, UI_TEXT_FAINT);

    struct NavItem {
        const char *label;
        UiIcon icon;
        AppPage page;
    };
    const struct NavItem nav[] = {
        {"Biblioteca", UI_ICON_GRID, APP_PAGE_LIBRARY},
        {"Ajustes", UI_ICON_GEAR, APP_PAGE_SETTINGS},
        {"Controles", UI_ICON_GAMEPAD, APP_PAGE_CONTROLS},
        {"Acerca de", UI_ICON_INFO, APP_PAGE_ABOUT},
    };
    int y = 136;
    for (int i = 0; i < 4; i++) {
        SDL_Rect rect = {16, y, 216, 48};
        if (ui_nav_button(ui, rect, nav[i].label, nav[i].icon,
                          app->page == nav[i].page)) {
            app->page = nav[i].page;
            if (app->page != APP_PAGE_GAME) {
                gb_release_buttons(&app->gb);
            }
        }
        y += 52;
    }

    ui_fill_rect(ui, (SDL_Rect){20, 356, 208, 1}, UI_BORDER_SOFT);
    ui_text(ui, &ui->small, "AHORA", 20, 372, UI_TEXT_FAINT);

    SDL_Rect game_panel = {16, 392, 216, 104};
    ui_round_rect(ui, game_panel, 12, UI_PANEL);
    ui_round_rect_outline(ui, game_panel, 12, UI_BORDER_SOFT, 1);
    if (app->rom_loaded) {
        char title[64];
        ellipsize(&ui->bold, app->rom_title, 184, title, sizeof(title));
        ui_text(ui, &ui->bold, title, 30, 408, UI_TEXT);
        ui_text(ui, &ui->small, app->rom_mbc, 30, 434, UI_TEXT_DIM);
        ui_circle(ui, 34, 464, 4,
                  app->paused ? UI_AMBER : UI_ACCENT);
        ui_text(ui, &ui->small, app->paused ? "En pausa" : "En ejecución", 46,
                456, UI_TEXT_DIM);
        if (ui_hovered(ui, game_panel)) {
            ui_round_rect_outline(ui, game_panel, 12, UI_ACCENT_DIM, 1);
            ui->want_hand = true;
            if (ui->mouse_pressed) {
                app->page = app_open_game_window(app) ? APP_PAGE_LIBRARY
                                                      : APP_PAGE_GAME;
            }
        }
    } else {
        ui_text(ui, &ui->bold, "Sin juego", 30, 408, UI_TEXT_DIM);
        ui_text(ui, &ui->small, "Abre una ROM para empezar", 30, 436,
                UI_TEXT_FAINT);
    }

    int bottom = 764;
    char version[64];
    snprintf(version, sizeof(version), "ClassicBoy %s", APP_VERSION);
    ui_text(ui, &ui->small, version, 20, bottom, UI_TEXT_FAINT);
    ui_text(ui, &ui->small, "Núcleo CGB · C11", 20, bottom + 20,
            UI_TEXT_FAINT);

    if (app->config.show_fps && app->rom_loaded) {
        char fps[32];
        snprintf(fps, sizeof(fps), "%.0f FPS", app->fps);
        ui_text_right(ui, &ui->small, fps, 228, bottom - 44, UI_TEXT_FAINT);
    }
    ui_circle(ui, 26, bottom - 66, 4,
              app->audio_ok ? UI_ACCENT : UI_DANGER);
    ui_text(ui, &ui->small, app->audio_ok ? "Audio activo" : "Sin audio", 38,
            bottom - 74, UI_TEXT_FAINT);
}

static void draw_header(App *app, const char *title, const char *subtitle)
{
    Ui *ui = &app->ui;
    char clipped[128];
    ellipsize(&ui->title, title, 740, clipped, sizeof(clipped));
    ui_text(ui, &ui->title, clipped, 288, 40, UI_TEXT);
    ui_text(ui, &ui->regular, subtitle, 288, 84, UI_TEXT_DIM);
}

static void draw_cartridge(App *app, SDL_Rect area, int variant)
{
    Ui *ui = &app->ui;
    uint32_t accent = cartridge_colors[variant % 6];
    ui_round_rect(ui, (SDL_Rect){area.x, area.y, area.w, area.h}, 10,
                  UI_RGB(0x11, 0x17, 0x1E));
    ui_circle(ui, area.x + area.w - 26, area.y + 24, 34,
              UI_RGBA(0x8B, 0xE0, 0x4E, 0x12));
    ui_circle(ui, area.x + 30, area.y + area.h - 12, 44,
              UI_RGBA(0x5A, 0xA9, 0xFF, 0x0E));

    SDL_Rect body = {area.x + (area.w - 78) / 2, area.y + 12, 78, 96};
    ui_round_rect(ui, (SDL_Rect){body.x + 2, body.y + 5, body.w, body.h}, 9,
                  UI_RGBA(0, 0, 0, 80));
    ui_round_rect(ui, body, 9, UI_RGB(0x2B, 0x35, 0x43));
    ui_round_rect_outline(ui, body, 9, UI_BORDER, 1);
    ui_round_rect(ui, (SDL_Rect){body.x + body.w / 2 - 13, body.y - 2, 26, 7},
                  2, UI_RGB(0x1A, 0x21, 0x2B));
    SDL_Rect label = {body.x + 9, body.y + 21, body.w - 18, 42};
    ui_round_rect(ui, label, 5, accent);
    ui_text_centered(ui, &ui->bold, "GBC", label, UI_ACCENT_TEXT);
    for (int i = 0; i < 3; i++) {
        int width = 28 - i * 7;
        ui_round_rect(ui,
                      (SDL_Rect){body.x + (body.w - width) / 2,
                                 body.y + 72 + i * 7, width, 3},
                      1, UI_TEXT_FAINT);
    }
}

static bool draw_library_card(App *app, SDL_Rect card, const RomEntry *rom,
                              int index)
{
    Ui *ui = &app->ui;
    bool hover = ui_hovered(ui, card);
    ui_round_rect(ui, card, 14, hover ? UI_PANEL_HOVER : UI_PANEL);
    ui_round_rect_outline(ui, card, 14, hover ? UI_ACCENT_DIM : UI_BORDER_SOFT,
                          hover ? 2 : 1);
    draw_cartridge(app,
                   (SDL_Rect){card.x + 14, card.y + 14, card.w - 28, 122},
                   index);

    char title[128];
    ellipsize(&ui->bold, rom->title, card.w - 28, title, sizeof(title));
    ui_text(ui, &ui->bold, title, card.x + 14, card.y + 146, UI_TEXT);

    ui_badge(ui, card.x + 14, card.y + 176, rom->mbc, UI_RGB(0x22, 0x2D, 0x3A),
             UI_TEXT_DIM);
    char size[32];
    format_size(rom->size, size, sizeof(size));
    ui_text_right(ui, &ui->small, size, card.x + card.w - 14, card.y + 181,
                  UI_TEXT_FAINT);

    if (rom->battery) {
        ui_circle(ui, card.x + 20, card.y + 211, 4, UI_ACCENT);
        ui_text(ui, &ui->small, "Guardado con batería", card.x + 30,
                card.y + 202, UI_TEXT_FAINT);
    } else {
        ui_text(ui, &ui->small, "Sin guardado", card.x + 14, card.y + 202,
                UI_TEXT_FAINT);
    }
    if (hover) {
        ui->want_hand = true;
    }
    return hover && ui->mouse_pressed;
}

static void draw_library_page(App *app)
{
    Ui *ui = &app->ui;
    draw_header(app, "Biblioteca",
                "Tus juegos de Game Boy y Game Boy Color, listos para jugar");

    SDL_Rect refresh = {946, 44, 126, 42};
    SDL_Rect open = {1084, 44, 156, 42};
    if (ui_button(ui, refresh, "Actualizar", false, true)) {
        app_refresh_library(app);
    }
    if (ui_button(ui, open, "Abrir ROM", true, true)) {
        app_open_dialog(app);
    }

    SDL_Rect grid = {288, 156, 952, 608};
    if (app->roms.count == 0) {
        SDL_Rect art = {288 + 952 / 2 - 90, 188, 180, 156};
        draw_cartridge(app, art, 0);
        const char *title = "Tu biblioteca está vacía";
        int width = ui_text_width(&ui->title, title);
        ui_text(ui, &ui->title, title, 288 + (952 - width) / 2, 380, UI_TEXT);
        SDL_Rect text_area = {388, 430, 752, 80};
        ui_text_wrapped(
            ui, &ui->regular,
            "Coloca tus archivos .gb o .gbc en la carpeta roms/ junto al "
            "ejecutable o abre una ROM desde tu equipo. También puedes "
            "arrastrar un archivo sobre la ventana.",
            text_area, UI_TEXT_DIM);
        SDL_Rect open_button = {288 + 952 / 2 - 90, 540, 180, 46};
        if (ui_button(ui, open_button, "Abrir ROM", true, true)) {
            app_open_dialog(app);
        }
        ui_text_centered(ui, &ui->small,
                         "ClassicBoy no incluye ROMs: usa tus propios cartuchos.",
                         (SDL_Rect){288, 604, 952, 24}, UI_TEXT_FAINT);
        return;
    }

    const int columns = 4;
    const int card_w = 224;
    const int card_h = 232;
    const int gap = 18;
    int rows = (app->roms.count + columns - 1) / columns;
    int content_h = rows * card_h + (rows > 1 ? (rows - 1) * gap : 0);
    app->library_max_scroll = content_h > grid.h ? content_h - grid.h : 0;
    if (app->library_scroll > app->library_max_scroll) {
        app->library_scroll = app->library_max_scroll;
    }
    if (app->library_scroll < 0) {
        app->library_scroll = 0;
    }

    SDL_RenderSetClipRect(app->renderer, &grid);
    for (int i = 0; i < app->roms.count; i++) {
        int row = i / columns;
        int col = i % columns;
        SDL_Rect card = {grid.x + col * (card_w + gap),
                         grid.y + row * (card_h + gap) - app->library_scroll,
                         card_w, card_h};
        if (card.y + card.h < grid.y || card.y > grid.y + grid.h) {
            continue;
        }
        if (draw_library_card(app, card, &app->roms.items[i], i)) {
            if (app_load_rom(app, app->roms.items[i].path)) {
                app->page = app_open_game_window(app) ? APP_PAGE_LIBRARY
                                                      : APP_PAGE_GAME;
            }
        }
    }
    SDL_RenderSetClipRect(app->renderer, NULL);

    if (app->library_max_scroll > 0) {
        SDL_Rect track = {1250, grid.y, 6, grid.h};
        ui_round_rect(ui, track, 3, UI_BORDER_SOFT);
        int thumb_h = grid.h * grid.h / content_h;
        if (thumb_h < 40) {
            thumb_h = 40;
        }
        int thumb_y = grid.y + (grid.h - thumb_h) * app->library_scroll /
                                    app->library_max_scroll;
        ui_round_rect(ui, (SDL_Rect){1250, thumb_y, 6, thumb_h}, 3,
                      UI_TEXT_FAINT);
    }
}

static void draw_game_toolbar(App *app)
{
    Ui *ui = &app->ui;
    SDL_Rect pause = {1046, 44, 44, 44};
    SDL_Rect reset = {1098, 44, 44, 44};
    SDL_Rect camera = {1150, 44, 44, 44};
    SDL_Rect library = {1202, 44, 44, 44};

    if (ui_icon_button(ui, pause,
                       app->paused ? UI_ICON_PLAY : UI_ICON_PAUSE,
                       app->paused ? "Reanudar" : "Pausa", true, true)) {
        app_toggle_pause(app);
    }
    if (ui_icon_button(ui, reset, UI_ICON_RESET, "Reiniciar", false, true)) {
        app_reset(app);
    }
    if (ui_icon_button(ui, camera, UI_ICON_CAMERA, "Captura (F12)", false,
                       true)) {
        app_take_screenshot(app);
    }
    if (ui_icon_button(ui, library, UI_ICON_FOLDER, "Biblioteca (F1)", false,
                       true)) {
        app->page = APP_PAGE_LIBRARY;
        gb_release_buttons(&app->gb);
    }
}

static void draw_game_page(App *app)
{
    Ui *ui = &app->ui;
    if (!app->rom_loaded) {
        app->page = APP_PAGE_LIBRARY;
        return;
    }
    char size[32];
    format_size(app->rom_size, size, sizeof(size));
    char subtitle[160];
    snprintf(subtitle, sizeof(subtitle), "%s · %s · %s", app->rom_mbc, size,
             app->rom_battery ? "guardado con batería" : "sin guardado");
    draw_header(app, app->rom_title, subtitle);
    draw_game_toolbar(app);

    SDL_Rect area = {288, 140, 952, 620};
    int max_w = area.w - 56;
    int max_h = area.h - 56;
    double fit = (double)max_w / 160.0;
    double fit_h = (double)max_h / 144.0;
    if (fit_h < fit) {
        fit = fit_h;
    }
    int screen_w;
    int screen_h;
    if (app->config.integer_scale) {
        int scale = (int)floor(fit);
        if (scale < 1) {
            scale = 1;
        }
        screen_w = 160 * scale;
        screen_h = 144 * scale;
    } else {
        screen_w = (int)(160.0 * fit);
        screen_h = (int)(144.0 * fit);
    }
    SDL_Rect screen = {area.x + (area.w - screen_w) / 2,
                       area.y + (area.h - screen_h) / 2, screen_w, screen_h};
    SDL_Rect bezel = {screen.x - 18, screen.y - 18, screen.w + 36,
                      screen.h + 36};

    ui_round_rect(ui, (SDL_Rect){bezel.x - 4, bezel.y - 4, bezel.w + 8,
                                 bezel.h + 8},
                  22, UI_RGBA(0x8B, 0xE0, 0x4E, 0x14));
    ui_round_rect(ui, bezel, 20, UI_RGB(0x07, 0x0A, 0x0E));
    ui_round_rect_outline(ui, bezel, 20, UI_BORDER, 1);
    ui_round_rect(ui, (SDL_Rect){screen.x - 4, screen.y - 4, screen.w + 8,
                                 screen.h + 8},
                  6, UI_RGB(0x1A, 0x22, 0x2C));

    if (app->screen_texture) {
        SDL_RenderCopy(app->renderer, app->screen_texture, NULL, &screen);
    } else {
        ui_fill_rect(ui, screen, UI_RGB(0x9B, 0xBC, 0x0F));
    }

    if (app->fast_forward) {
        SDL_Rect badge = {bezel.x + 14, bezel.y + 14, 190, 28};
        ui_round_rect(ui, badge, 8, UI_RGBA(0x0B, 0x10, 0x08, 220));
        ui_circle(ui, badge.x + 16, badge.y + 14, 4, UI_ACCENT);
        ui_text(ui, &ui->small, "AVANCE RÁPIDO x4", badge.x + 28,
                badge.y + 6, UI_ACCENT);
    }

    const char *hint =
        "Flechas: cruceta   ·   Z / X: A / B   ·   Enter: Start   ·   "
        "Mayús: Select   ·   Esc: pausa   ·   F12: captura";
    ui_text_centered(ui, &ui->small, hint, (SDL_Rect){288, 770, 952, 22},
                     UI_TEXT_FAINT);

    if (app->paused) {
        ui_fill_rect(ui, area, UI_RGBA(0x06, 0x09, 0x0D, 170));
        SDL_Rect panel = {area.x + (area.w - 440) / 2,
                          area.y + (area.h - 260) / 2, 440, 260};
        ui_round_rect(ui, panel, 18, UI_PANEL);
        ui_round_rect_outline(ui, panel, 18, UI_BORDER, 1);
        int title_w = ui_text_width(&ui->title, "PAUSA");
        ui_text(ui, &ui->title, "PAUSA", panel.x + (panel.w - title_w) / 2,
                panel.y + 26, UI_TEXT);
        ui_text_centered(ui, &ui->regular, "El juego está en pausa",
                         (SDL_Rect){panel.x, panel.y + 70, panel.w, 24},
                         UI_TEXT_DIM);
        SDL_Rect resume = {panel.x + 130, panel.y + 108, 180, 46};
        if (ui_button(ui, resume, "Reanudar", true, true)) {
            app_toggle_pause(app);
        }
        SDL_Rect restart = {panel.x + 74, panel.y + 168, 140, 42};
        SDL_Rect back = {panel.x + 226, panel.y + 168, 140, 42};
        if (ui_button(ui, restart, "Reiniciar", false, true)) {
            app_reset(app);
        }
        if (ui_button(ui, back, "Biblioteca", false, true)) {
            app->page = APP_PAGE_LIBRARY;
            gb_release_buttons(&app->gb);
        }
        ui_text_centered(ui, &ui->small,
                         "El guardado de batería se escribe al salir o con F5.",
                         (SDL_Rect){panel.x, panel.y + 222, panel.w, 20},
                         UI_TEXT_FAINT);
    }
}

static bool palette_card(App *app, SDL_Rect rect, int index)
{
    Ui *ui = &app->ui;
    bool selected = app->config.palette == index;
    bool hover = ui_hovered(ui, rect);
    ui_round_rect(ui, rect, 12, hover ? UI_PANEL_HOVER : UI_PANEL_ACTIVE);
    ui_round_rect_outline(ui, rect, 12,
                          selected ? UI_ACCENT : UI_BORDER_SOFT,
                          selected ? 2 : 1);
    int stripe_w = (rect.w - 24) / 4;
    for (int i = 0; i < 4; i++) {
        SDL_Rect stripe = {rect.x + 8 + i * (stripe_w + 1), rect.y + 8,
                           stripe_w, 40};
        ui_round_rect(ui, stripe, 4, app_palettes[index][i]);
    }
    ui_text_centered(ui, &ui->small, app_palette_names[index],
                     (SDL_Rect){rect.x, rect.y + 52, rect.w, 22},
                     selected ? UI_TEXT : UI_TEXT_DIM);
    if (selected) {
        ui_circle(ui, rect.x + rect.w - 14, rect.y + 14, 9, UI_ACCENT);
        ui_icon(ui, UI_ICON_CHECK, rect.x + rect.w - 20, rect.y + 8, 12,
                UI_ACCENT_TEXT);
    }
    if (hover) {
        ui->want_hand = true;
    }
    return hover && ui->mouse_pressed;
}

static void status_row(App *app, int x, int y, int width, const char *label,
                       const char *value)
{
    Ui *ui = &app->ui;
    ui_text(ui, &ui->small, label, x, y + 4, UI_TEXT_FAINT);
    char clipped[128];
    ellipsize(&ui->regular, value, width - 130, clipped, sizeof(clipped));
    ui_text_right(ui, &ui->regular, clipped, x + width, y, UI_TEXT);
    ui_fill_rect(ui, (SDL_Rect){x, y + 30, width, 1}, UI_BORDER_SOFT);
}

static void draw_settings_page(App *app)
{
    Ui *ui = &app->ui;
    draw_header(app, "Ajustes", "Personaliza la experiencia de juego");

    SDL_Rect appearance = {288, 160, 600, 300};
    ui_panel(ui, appearance, 16);
    ui_text(ui, &ui->bold, "Apariencia", 312, 180, UI_TEXT);
    ui_text(ui, &ui->small, "PALETA DMG (COMPATIBILIDAD)", 312, 218,
            UI_TEXT_FAINT);
    int card_w = 126;
    int gap = 12;
    for (int i = 0; i < APP_PALETTE_COUNT; i++) {
        SDL_Rect card = {312 + i * (card_w + gap), 242, card_w, 86};
        if (palette_card(app, card, i)) {
            app->config.palette = i;
            app_apply_palette(app);
            app_save_config(app);
        }
    }
    SDL_Rect integer_row = {312, 344, 552, 44};
    if (ui_toggle(ui, integer_row, "Escalado entero (píxeles nítidos)",
                  &app->config.integer_scale)) {
        app_save_config(app);
    }
    SDL_Rect fps_row = {312, 392, 552, 44};
    if (ui_toggle(ui, fps_row, "Mostrar contador de FPS",
                  &app->config.show_fps)) {
        app_save_config(app);
    }

    SDL_Rect audio_panel = {288, 480, 600, 150};
    ui_panel(ui, audio_panel, 16);
    ui_text(ui, &ui->bold, "Audio", 312, 500, UI_TEXT);
    ui_text(ui, &ui->small, "Volumen", 312, 538, UI_TEXT_DIM);
    char volume_text[16];
    snprintf(volume_text, sizeof(volume_text), "%d%%", app->config.volume);
    ui_text_right(ui, &ui->bold, volume_text, 864, 534, UI_TEXT);
    SDL_Rect track = {312, 578, 552, 8};
    if (ui_slider(ui, track, &app->config.volume, 0, 100)) {
        app_save_config(app);
    }
    ui_text(ui, &ui->small,
            app->audio_ok ? "Salida SDL2 · 48 kHz · estéreo"
                          : "No se pudo abrir el dispositivo de audio",
            312, 602, UI_TEXT_FAINT);

    SDL_Rect status = {908, 160, 332, 470};
    ui_panel(ui, status, 16);
    ui_text(ui, &ui->bold, "Estado del sistema", 932, 180, UI_TEXT);
    char current_rom[128];
    if (app->rom_loaded) {
        ellipsize(&ui->regular, app->rom_title, 220, current_rom,
                  sizeof(current_rom));
    } else {
        snprintf(current_rom, sizeof(current_rom), "Ninguna");
    }
    status_row(app, 932, 222, 284, "Versión", APP_VERSION);
    status_row(app, 932, 262, 284, "Renderizador", app->renderer_name);
    status_row(app, 932, 302, 284, "Audio",
               app->audio_ok ? "48 kHz estéreo" : "No disponible");
    status_row(app, 932, 342, 284, "Escalado",
               app->config.integer_scale ? "Entero" : "Ajustado");
    status_row(app, 932, 382, 284, "ROM actual", current_rom);

    SDL_Rect folder = {932, 438, 284, 42};
    if (ui_button(ui, folder, "Abrir carpeta de ROMs", false, true)) {
        app_open_roms_folder(app);
    }
    SDL_Rect restore = {932, 490, 284, 42};
    if (ui_button(ui, restore, "Restaurar valores", false, true)) {
        app->config.palette = 0;
        app->config.volume = 70;
        app->config.integer_scale = true;
        app->config.show_fps = true;
        app_apply_palette(app);
        app_save_config(app);
        app_toast(app, "Ajustes restaurados");
    }
    ui_text(ui, &ui->small, "La configuración se guarda automáticamente.",
            932, 560, UI_TEXT_FAINT);
    ui_text(ui, &ui->small, "classicboy.cfg", 932, 580, UI_TEXT_FAINT);
}

static void key_row(App *app, int x, int y, int width, const char *action,
                    const char *key)
{
    Ui *ui = &app->ui;
    ui_text(ui, &ui->small, action, x, y + 5, UI_TEXT_FAINT);
    ui_text_right(ui, &ui->regular, key, x + width, y, UI_TEXT);
    ui_fill_rect(ui, (SDL_Rect){x, y + 32, width, 1}, UI_BORDER_SOFT);
}

static void draw_controls_page(App *app)
{
    Ui *ui = &app->ui;
    draw_header(app, "Controles", "Teclado y mando, sin configuración previa");

    SDL_Rect keyboard = {288, 160, 460, 500};
    ui_panel(ui, keyboard, 16);
    ui_text(ui, &ui->bold, "Teclado", 312, 180, UI_TEXT);
    const char *keys[][2] = {
        {"Cruceta", "Flechas"},
        {"Botón A", "Z / J"},
        {"Botón B", "X / K"},
        {"Start", "Enter"},
        {"Select", "Mayús derecho / Retroceso"},
        {"Pausa", "Esc / P"},
        {"Avance rápido", "Espacio (mantener)"},
        {"Captura", "F12"},
        {"Pantalla completa", "F11"},
        {"Biblioteca", "F1"},
        {"Guardar batería", "F5"},
    };
    int y = 222;
    for (int i = 0; i < 11; i++) {
        key_row(app, 312, y, 412, keys[i][0], keys[i][1]);
        y += 38;
    }

    SDL_Rect gamepad = {772, 160, 468, 500};
    ui_panel(ui, gamepad, 16);
    ui_text(ui, &ui->bold, "Mando", 796, 180, UI_TEXT);
    const char *pad[][2] = {
        {"Cruceta", "D-pad"},
        {"Botón A", "Botón inferior"},
        {"Botón B", "Botón derecho"},
        {"Start", "Start"},
        {"Select", "Back"},
    };
    y = 222;
    for (int i = 0; i < 5; i++) {
        key_row(app, 796, y, 420, pad[i][0], pad[i][1]);
        y += 38;
    }
    ui_text(ui, &ui->small, "MANDO DETECTADO", 796, 430, UI_TEXT_FAINT);
    SDL_Rect controller_card = {796, 456, 420, 64};
    ui_round_rect(ui, controller_card, 12, UI_PANEL_ACTIVE);
    ui_icon(ui, UI_ICON_GAMEPAD, 812, 474, 26,
            app->controller ? UI_ACCENT : UI_TEXT_FAINT);
    char controller[128];
    ellipsize(&ui->regular,
              app->controller ? app->controller_name : "Ninguno conectado", 350,
              controller, sizeof(controller));
    ui_text(ui, &ui->regular, controller, 852, 462, UI_TEXT);
    ui_text(ui, &ui->small,
            app->controller ? "Conectado por SDL GameController"
                            : "Conecta un mando y se detectará solo",
            852, 488, UI_TEXT_FAINT);
    ui_text(ui, &ui->small,
            "Los mandos XInput y DirectInput se reconocen automáticamente.",
            796, 540, UI_TEXT_FAINT);
    ui_text(ui, &ui->small,
            "Puedes cambiar de juego sin cerrar la aplicación con F1.",
            796, 562, UI_TEXT_FAINT);
}

static void draw_about_page(App *app)
{
    Ui *ui = &app->ui;
    draw_header(app, "Acerca de",
                "ClassicBoy · Emulador de Game Boy Color (CGB)");

    SDL_Rect main_panel = {288, 160, 600, 500};
    ui_panel(ui, main_panel, 16);
    draw_logo(app, 320, 190, 64);
    ui_text(ui, &ui->title, "ClassicBoy", 400, 190, UI_TEXT);
    ui_badge(ui, 400, 232, "v" APP_VERSION, UI_ACCENT_DIM, UI_TEXT);

    ui_text_wrapped(
        ui, &ui->regular,
        "ClassicBoy es un emulador de Game Boy Color escrito desde cero en "
        "C11. Incluye una CPU SM83 completa, PPU con paletas y memoria de "
        "vídeo de CGB, APU de cuatro canales, temporizadores, joypad, "
        "doble velocidad y controladores de cartucho MBC1, MBC2, MBC3 "
        "(con RTC) y MBC5, además de guardado de partidas con batería. Los "
        "juegos DMG se ejecutan en modo de compatibilidad.",
        (SDL_Rect){320, 284, 536, 150}, UI_TEXT_DIM);
    ui_text_wrapped(
        ui, &ui->regular,
        "La interfaz está construida con SDL2 y tipografías Lato con "
        "antialiasing. El núcleo del emulador no depende de ninguna "
        "biblioteca externa y está cubierto por pruebas de estabilidad, "
        "fuzz y fugas de memoria.",
        (SDL_Rect){320, 430, 536, 120}, UI_TEXT_DIM);

    ui_badge(ui, 320, 582, "C11", UI_PANEL_ACTIVE, UI_TEXT);
    ui_badge(ui, 374, 582, "SDL2 2.32.10", UI_PANEL_ACTIVE, UI_TEXT);
    ui_badge(ui, 486, 582, "stb_truetype", UI_PANEL_ACTIVE, UI_TEXT);
    ui_badge(ui, 604, 582, "Lato · SIL OFL", UI_PANEL_ACTIVE, UI_TEXT);
    ui_text(ui, &ui->small, "Sin BIOS ni ROMs incluidas.", 320, 618,
            UI_TEXT_FAINT);

    SDL_Rect credits = {908, 160, 332, 500};
    ui_panel(ui, credits, 16);
    ui_text(ui, &ui->bold, "Créditos y pruebas", 932, 180, UI_TEXT);
    const char *rows[][2] = {
        {"Emulación", "Núcleo propio CGB"},
        {"Gráficos", "SDL2 Renderer"},
        {"Tipografía", "Lato Regular / Bold"},
        {"Rasterizado", "stb_truetype"},
        {"Suites", "53"},
        {"Comprobaciones", "396 825"},
        {"Fuzz", "CPU y ROM"},
        {"Memoria", "Canarios propios"},
    };
    int y = 222;
    for (int i = 0; i < 8; i++) {
        ui_text(ui, &ui->small, rows[i][0], 932, y + 4, UI_TEXT_FAINT);
        ui_text_right(ui, &ui->regular, rows[i][1], 1216, y, UI_TEXT);
        ui_fill_rect(ui, (SDL_Rect){932, y + 30, 284, 1}, UI_BORDER_SOFT);
        y += 38;
    }
    ui_text_wrapped(
        ui, &ui->small,
        "Este emulador no incluye ROMs ni archivos de BIOS. Utiliza "
        "únicamente copias de cartuchos que poseas legalmente.",
        (SDL_Rect){932, 540, 284, 100}, UI_TEXT_FAINT);
}

static void draw_toast(App *app)
{
    if (!app->toast[0] || SDL_GetTicks64() >= app->toast_until) {
        return;
    }
    Ui *ui = &app->ui;
    int width = ui_text_width(&ui->small, app->toast) + 46;
    if (width > 560) {
        width = 560;
    }
    SDL_Rect rect = {1240 - width, 738, width, 44};
    ui_round_rect(ui, rect, 12, UI_PANEL_ACTIVE);
    ui_round_rect_outline(ui, rect, 12, UI_BORDER, 1);
    ui_fill_rect(ui, (SDL_Rect){rect.x + 12, rect.y + 12, 3, 20}, UI_ACCENT);
    char clipped[256];
    ellipsize(&ui->small, app->toast, width - 40, clipped, sizeof(clipped));
    ui_text(ui, &ui->small, clipped, rect.x + 26,
            rect.y + (rect.h - ui->small.line_height) / 2, UI_TEXT);
}

void pages_draw(App *app)
{
    Ui *ui = &app->ui;
    ui_begin(ui, 1280, 800);
    ui_gradient_rect(ui, (SDL_Rect){0, 0, 1280, 800}, UI_BG_TOP, UI_BG_BOTTOM);
    draw_sidebar(app);

    switch (app->page) {
    case APP_PAGE_LIBRARY:
        draw_library_page(app);
        break;
    case APP_PAGE_GAME:
        draw_game_page(app);
        break;
    case APP_PAGE_SETTINGS:
        draw_settings_page(app);
        break;
    case APP_PAGE_CONTROLS:
        draw_controls_page(app);
        break;
    case APP_PAGE_ABOUT:
        draw_about_page(app);
        break;
    default:
        draw_library_page(app);
        break;
    }
    draw_toast(app);
    ui_end(ui);
}
