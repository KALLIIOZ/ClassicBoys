/*
 * ClassicBoy - minimal immediate-mode UI toolkit on top of SDL2.
 */
#ifndef CB_UI_H
#define CB_UI_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "font.h"

#define UI_RGB(r, g, b)                                                       \
    ((uint32_t)(0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) |  \
                (uint32_t)(b)))
#define UI_RGBA(r, g, b, a)                                                   \
    ((uint32_t)(((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) |               \
                ((uint32_t)(g) << 8) | (uint32_t)(b)))

#define UI_BG_TOP UI_RGB(0x0C, 0x10, 0x16)
#define UI_BG_BOTTOM UI_RGB(0x13, 0x1A, 0x23)
#define UI_SIDEBAR UI_RGB(0x0E, 0x13, 0x1A)
#define UI_PANEL UI_RGB(0x18, 0x20, 0x2A)
#define UI_PANEL_HOVER UI_RGB(0x20, 0x2A, 0x36)
#define UI_PANEL_ACTIVE UI_RGB(0x24, 0x31, 0x40)
#define UI_BORDER UI_RGB(0x2A, 0x36, 0x46)
#define UI_BORDER_SOFT UI_RGB(0x20, 0x29, 0x35)
#define UI_TEXT UI_RGB(0xEC, 0xF1, 0xF7)
#define UI_TEXT_DIM UI_RGB(0x9B, 0xA8, 0xB8)
#define UI_TEXT_FAINT UI_RGB(0x6B, 0x78, 0x88)
#define UI_ACCENT UI_RGB(0x8B, 0xE0, 0x4E)
#define UI_ACCENT_HOVER UI_RGB(0xA5, 0xF2, 0x6C)
#define UI_ACCENT_DIM UI_RGB(0x3E, 0x6B, 0x28)
#define UI_ACCENT_TEXT UI_RGB(0x0A, 0x14, 0x06)
#define UI_DANGER UI_RGB(0xFF, 0x6B, 0x6B)
#define UI_BLUE UI_RGB(0x5A, 0xA9, 0xFF)
#define UI_AMBER UI_RGB(0xF5, 0xB8, 0x4B)

typedef enum UiIcon {
    UI_ICON_NONE = 0,
    UI_ICON_GRID,
    UI_ICON_GEAR,
    UI_ICON_GAMEPAD,
    UI_ICON_INFO,
    UI_ICON_PLAY,
    UI_ICON_PAUSE,
    UI_ICON_RESET,
    UI_ICON_CAMERA,
    UI_ICON_FOLDER,
    UI_ICON_REFRESH,
    UI_ICON_BACK,
    UI_ICON_CHECK
} UiIcon;

typedef struct Ui {
    SDL_Renderer *renderer;
    UiFont small;
    UiFont regular;
    UiFont bold;
    UiFont title;

    int width;
    int height;

    int mouse_x;
    int mouse_y;
    bool mouse_down;
    bool mouse_pressed;
    bool mouse_released;
    bool want_hand;
    bool hand_active;
    SDL_Cursor *hand_cursor;
    SDL_Cursor *arrow_cursor;
} Ui;

bool ui_init(Ui *ui, SDL_Renderer *renderer, const char *asset_dir);
void ui_shutdown(Ui *ui);

void ui_begin(Ui *ui, int width, int height);
void ui_end(Ui *ui);

void ui_fill_rect(Ui *ui, SDL_Rect rect, uint32_t color);
void ui_gradient_rect(Ui *ui, SDL_Rect rect, uint32_t top, uint32_t bottom);
void ui_round_rect(Ui *ui, SDL_Rect rect, int radius, uint32_t color);
void ui_round_rect_outline(Ui *ui, SDL_Rect rect, int radius, uint32_t color,
                           int thickness);
void ui_circle(Ui *ui, int cx, int cy, int radius, uint32_t color);
void ui_line(Ui *ui, int x0, int y0, int x1, int y1, int thickness,
             uint32_t color);
void ui_icon(Ui *ui, UiIcon icon, int x, int y, int size, uint32_t color);

void ui_text(Ui *ui, const UiFont *font, const char *text, int x, int y,
             uint32_t color);
void ui_text_centered(Ui *ui, const UiFont *font, const char *text,
                      SDL_Rect area, uint32_t color);
void ui_text_right(Ui *ui, const UiFont *font, const char *text, int right,
                   int y, uint32_t color);
void ui_text_wrapped(Ui *ui, const UiFont *font, const char *text,
                     SDL_Rect area, uint32_t color);
int ui_text_width(const UiFont *font, const char *text);

void ui_panel(Ui *ui, SDL_Rect rect, int radius);

bool ui_button(Ui *ui, SDL_Rect rect, const char *label, bool primary,
               bool enabled);
bool ui_icon_button(Ui *ui, SDL_Rect rect, UiIcon icon, const char *tooltip,
                    bool primary, bool enabled);
bool ui_nav_button(Ui *ui, SDL_Rect rect, const char *label, UiIcon icon,
                   bool active);
bool ui_toggle(Ui *ui, SDL_Rect row, const char *label, bool *value);
bool ui_slider(Ui *ui, SDL_Rect track, int *value, int minimum, int maximum);
void ui_badge(Ui *ui, int x, int y, const char *text, uint32_t bg,
              uint32_t fg);
bool ui_hovered(const Ui *ui, SDL_Rect rect);

#endif /* CB_UI_H */
