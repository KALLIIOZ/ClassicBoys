#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_PI 3.14159265358979323846f

static SDL_Color to_sdl(uint32_t color)
{
    SDL_Color out;
    out.a = (uint8_t)((color >> 24) & 0xFFu);
    out.r = (uint8_t)((color >> 16) & 0xFFu);
    out.g = (uint8_t)((color >> 8) & 0xFFu);
    out.b = (uint8_t)(color & 0xFFu);
    return out;
}

static void set_color(Ui *ui, uint32_t color)
{
    SDL_Color c = to_sdl(color);
    SDL_SetRenderDrawColor(ui->renderer, c.r, c.g, c.b, c.a);
}

bool ui_hovered(const Ui *ui, SDL_Rect rect)
{
    return ui && ui->mouse_x >= rect.x && ui->mouse_x < rect.x + rect.w &&
           ui->mouse_y >= rect.y && ui->mouse_y < rect.y + rect.h;
}

static int build_round_path(SDL_FPoint *points, SDL_Rect rect, float radius,
                            int segments)
{
    if (radius < 0.0f) {
        radius = 0.0f;
    }
    float limit = (float)(rect.w < rect.h ? rect.w : rect.h) * 0.5f;
    if (radius > limit) {
        radius = limit;
    }
    struct Corner {
        float cx, cy, start;
    };
    struct Corner corners[4] = {
        {(float)rect.x + radius, (float)rect.y + radius, 180.0f},
        {(float)rect.x + rect.w - radius, (float)rect.y + radius, 270.0f},
        {(float)rect.x + rect.w - radius, (float)rect.y + rect.h - radius, 0.0f},
        {(float)rect.x + radius, (float)rect.y + rect.h - radius, 90.0f},
    };
    int count = 0;
    for (int corner = 0; corner < 4; corner++) {
        for (int i = 0; i <= segments; i++) {
            float angle = (corners[corner].start +
                           (90.0f * (float)i) / (float)segments) *
                          UI_PI / 180.0f;
            points[count].x = corners[corner].cx + radius * cosf(angle);
            points[count].y = corners[corner].cy + radius * sinf(angle);
            count++;
        }
    }
    return count;
}

void ui_begin(Ui *ui, int width, int height)
{
    if (!ui) {
        return;
    }
    ui->width = width;
    ui->height = height;
    ui->want_hand = false;
}

void ui_end(Ui *ui)
{
    if (!ui) {
        return;
    }
    if (ui->want_hand != ui->hand_active) {
        SDL_SetCursor(ui->want_hand ? ui->hand_cursor : ui->arrow_cursor);
        ui->hand_active = ui->want_hand;
    }
    ui->mouse_pressed = false;
    ui->mouse_released = false;
    ui->mouse_right_pressed = false;
}

bool ui_init(Ui *ui, SDL_Renderer *renderer, const char *asset_dir)
{
    if (!ui || !renderer || !asset_dir) {
        return false;
    }
    memset(ui, 0, sizeof(*ui));
    ui->renderer = renderer;
    ui->hand_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    ui->arrow_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);

    char path[1024];
    snprintf(path, sizeof(path), "%s/fonts/Lato-Regular.ttf", asset_dir);
    if (!font_load(&ui->regular, renderer, path, 17)) {
        fprintf(stderr, "ClassicBoy: no se pudo cargar %s\n", path);
        return false;
    }
    if (!font_load(&ui->small, renderer, path, 13)) {
        return false;
    }
    snprintf(path, sizeof(path), "%s/fonts/Lato-Bold.ttf", asset_dir);
    if (!font_load(&ui->bold, renderer, path, 18)) {
        return false;
    }
    if (!font_load(&ui->title, renderer, path, 32)) {
        return false;
    }
    ui->hand_active = false;
    SDL_SetCursor(ui->arrow_cursor);
    return true;
}

void ui_shutdown(Ui *ui)
{
    if (!ui) {
        return;
    }
    font_free(&ui->small);
    font_free(&ui->regular);
    font_free(&ui->bold);
    font_free(&ui->title);
    if (ui->hand_cursor) {
        SDL_FreeCursor(ui->hand_cursor);
    }
    if (ui->arrow_cursor) {
        SDL_FreeCursor(ui->arrow_cursor);
    }
    memset(ui, 0, sizeof(*ui));
}

void ui_fill_rect(Ui *ui, SDL_Rect rect, uint32_t color)
{
    set_color(ui, color);
    SDL_RenderFillRect(ui->renderer, &rect);
}

void ui_gradient_rect(Ui *ui, SDL_Rect rect, uint32_t top, uint32_t bottom)
{
    SDL_Color c0 = to_sdl(top);
    SDL_Color c1 = to_sdl(bottom);
    SDL_Vertex vertices[6];
    SDL_FPoint p[4] = {
        {(float)rect.x, (float)rect.y},
        {(float)(rect.x + rect.w), (float)rect.y},
        {(float)(rect.x + rect.w), (float)(rect.y + rect.h)},
        {(float)rect.x, (float)(rect.y + rect.h)},
    };
    int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        int index = order[i];
        vertices[i].position = p[index];
        vertices[i].color = index < 2 ? c0 : c1;
        vertices[i].tex_coord.x = 0.0f;
        vertices[i].tex_coord.y = 0.0f;
    }
    SDL_RenderGeometry(ui->renderer, NULL, vertices, 6, NULL, 0);
}

void ui_round_rect(Ui *ui, SDL_Rect rect, int radius, uint32_t color)
{
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }
    if (radius <= 0) {
        ui_fill_rect(ui, rect, color);
        return;
    }
    const int segments = 7;
    SDL_FPoint path[4 * (segments + 1)];
    int count = build_round_path(path, rect, (float)radius, segments);
    SDL_Color c = to_sdl(color);
    SDL_Vertex *vertices =
        (SDL_Vertex *)malloc(sizeof(SDL_Vertex) * (size_t)count * 3u);
    if (!vertices) {
        ui_fill_rect(ui, rect, color);
        return;
    }
    float cx = (float)rect.x + (float)rect.w * 0.5f;
    float cy = (float)rect.y + (float)rect.h * 0.5f;
    for (int i = 0; i < count; i++) {
        SDL_Vertex *v = &vertices[i * 3];
        v[0].position.x = cx;
        v[0].position.y = cy;
        v[1].position = path[i];
        v[2].position = path[(i + 1) % count];
        for (int k = 0; k < 3; k++) {
            v[k].color = c;
            v[k].tex_coord.x = 0.0f;
            v[k].tex_coord.y = 0.0f;
        }
    }
    SDL_RenderGeometry(ui->renderer, NULL, vertices, count * 3, NULL, 0);
    free(vertices);
}

void ui_round_rect_outline(Ui *ui, SDL_Rect rect, int radius, uint32_t color,
                           int thickness)
{
    if (rect.w <= 0 || rect.h <= 0 || thickness <= 0) {
        return;
    }
    if (radius <= 0) {
        set_color(ui, color);
        for (int i = 0; i < thickness; i++) {
            SDL_Rect r = {rect.x + i, rect.y + i, rect.w - i * 2,
                          rect.h - i * 2};
            SDL_RenderDrawRect(ui->renderer, &r);
        }
        return;
    }
    const int segments = 7;
    SDL_FPoint outer[4 * (segments + 1)];
    SDL_FPoint inner[4 * (segments + 1)];
    int count = build_round_path(outer, rect, (float)radius, segments);
    SDL_Rect inner_rect = {rect.x + thickness, rect.y + thickness,
                           rect.w - thickness * 2, rect.h - thickness * 2};
    int inner_radius = radius - thickness;
    if (inner_radius < 0) {
        inner_radius = 0;
    }
    build_round_path(inner, inner_rect, (float)inner_radius, segments);

    SDL_Color c = to_sdl(color);
    SDL_Vertex *vertices =
        (SDL_Vertex *)malloc(sizeof(SDL_Vertex) * (size_t)count * 6u);
    if (!vertices) {
        return;
    }
    for (int i = 0; i < count; i++) {
        int next = (i + 1) % count;
        SDL_Vertex *v = &vertices[i * 6];
        v[0].position = outer[i];
        v[1].position = outer[next];
        v[2].position = inner[next];
        v[3].position = outer[i];
        v[4].position = inner[next];
        v[5].position = inner[i];
        for (int k = 0; k < 6; k++) {
            v[k].color = c;
            v[k].tex_coord.x = 0.0f;
            v[k].tex_coord.y = 0.0f;
        }
    }
    SDL_RenderGeometry(ui->renderer, NULL, vertices, count * 6, NULL, 0);
    free(vertices);
}

void ui_circle(Ui *ui, int cx, int cy, int radius, uint32_t color)
{
    if (radius <= 0) {
        return;
    }
    const int segments = 32;
    SDL_Color c = to_sdl(color);
    SDL_Vertex vertices[segments * 3];
    for (int i = 0; i < segments; i++) {
        float a0 = (float)i * 2.0f * UI_PI / (float)segments;
        float a1 = (float)(i + 1) * 2.0f * UI_PI / (float)segments;
        SDL_Vertex *v = &vertices[i * 3];
        v[0].position.x = (float)cx;
        v[0].position.y = (float)cy;
        v[1].position.x = (float)cx + (float)radius * cosf(a0);
        v[1].position.y = (float)cy + (float)radius * sinf(a0);
        v[2].position.x = (float)cx + (float)radius * cosf(a1);
        v[2].position.y = (float)cy + (float)radius * sinf(a1);
        for (int k = 0; k < 3; k++) {
            v[k].color = c;
            v[k].tex_coord.x = 0.0f;
            v[k].tex_coord.y = 0.0f;
        }
    }
    SDL_RenderGeometry(ui->renderer, NULL, vertices, segments * 3, NULL, 0);
}

static void ring(Ui *ui, int cx, int cy, int radius, int thickness,
                 uint32_t color)
{
    if (radius <= 0 || thickness <= 0) {
        return;
    }
    const int segments = 32;
    int inner_radius = radius - thickness;
    if (inner_radius < 0) {
        inner_radius = 0;
    }
    SDL_Color c = to_sdl(color);
    SDL_Vertex vertices[segments * 6];
    for (int i = 0; i < segments; i++) {
        float a0 = (float)i * 2.0f * UI_PI / (float)segments;
        float a1 = (float)(i + 1) * 2.0f * UI_PI / (float)segments;
        float ox0 = (float)cx + (float)radius * cosf(a0);
        float oy0 = (float)cy + (float)radius * sinf(a0);
        float ox1 = (float)cx + (float)radius * cosf(a1);
        float oy1 = (float)cy + (float)radius * sinf(a1);
        float ix0 = (float)cx + (float)inner_radius * cosf(a0);
        float iy0 = (float)cy + (float)inner_radius * sinf(a0);
        float ix1 = (float)cx + (float)inner_radius * cosf(a1);
        float iy1 = (float)cy + (float)inner_radius * sinf(a1);
        SDL_Vertex *v = &vertices[i * 6];
        v[0].position.x = ox0;
        v[0].position.y = oy0;
        v[1].position.x = ox1;
        v[1].position.y = oy1;
        v[2].position.x = ix1;
        v[2].position.y = iy1;
        v[3].position.x = ox0;
        v[3].position.y = oy0;
        v[4].position.x = ix1;
        v[4].position.y = iy1;
        v[5].position.x = ix0;
        v[5].position.y = iy0;
        for (int k = 0; k < 6; k++) {
            v[k].color = c;
            v[k].tex_coord.x = 0.0f;
            v[k].tex_coord.y = 0.0f;
        }
    }
    SDL_RenderGeometry(ui->renderer, NULL, vertices, segments * 6, NULL, 0);
}

void ui_line(Ui *ui, int x0, int y0, int x1, int y1, int thickness,
             uint32_t color)
{
    if (thickness <= 0) {
        return;
    }
    float dx = (float)(x1 - x0);
    float dy = (float)(y1 - y0);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) {
        return;
    }
    float nx = -dy / len * (float)thickness * 0.5f;
    float ny = dx / len * (float)thickness * 0.5f;
    SDL_Color c = to_sdl(color);
    SDL_Vertex vertices[6];
    SDL_FPoint p[4] = {
        {(float)x0 + nx, (float)y0 + ny},
        {(float)x1 + nx, (float)y1 + ny},
        {(float)x1 - nx, (float)y1 - ny},
        {(float)x0 - nx, (float)y0 - ny},
    };
    int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        int index = order[i];
        vertices[i].position = p[index];
        vertices[i].color = c;
        vertices[i].tex_coord.x = 0.0f;
        vertices[i].tex_coord.y = 0.0f;
    }
    SDL_RenderGeometry(ui->renderer, NULL, vertices, 6, NULL, 0);
}

static void triangle(Ui *ui, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c,
                     uint32_t color)
{
    SDL_Color col = to_sdl(color);
    SDL_Vertex vertices[3];
    vertices[0].position = a;
    vertices[1].position = b;
    vertices[2].position = c;
    for (int i = 0; i < 3; i++) {
        vertices[i].color = col;
        vertices[i].tex_coord.x = 0.0f;
        vertices[i].tex_coord.y = 0.0f;
    }
    SDL_RenderGeometry(ui->renderer, NULL, vertices, 3, NULL, 0);
}

void ui_icon(Ui *ui, UiIcon icon, int x, int y, int size, uint32_t color)
{
    int cx = x + size / 2;
    int cy = y + size / 2;
    int t = size >= 20 ? 2 : 1;
    switch (icon) {
    case UI_ICON_GRID: {
        int cell = (size - 3) / 2;
        ui_round_rect(ui, (SDL_Rect){x, y, cell, cell}, 2, color);
        ui_round_rect(ui, (SDL_Rect){x + cell + 3, y, cell, cell}, 2, color);
        ui_round_rect(ui, (SDL_Rect){x, y + cell + 3, cell, cell}, 2, color);
        ui_round_rect(ui, (SDL_Rect){x + cell + 3, y + cell + 3, cell, cell}, 2,
                      color);
        break;
    }
    case UI_ICON_GEAR:
        ring(ui, cx, cy, size / 2, t, color);
        for (int i = 0; i < 4; i++) {
            float angle = (float)i * UI_PI * 0.5f;
            int px = cx + (int)(cosf(angle) * (float)(size / 2));
            int py = cy + (int)(sinf(angle) * (float)(size / 2));
            ui_circle(ui, px, py, t + 1, color);
        }
        ui_circle(ui, cx, cy, size / 7, color);
        break;
    case UI_ICON_GAMEPAD: {
        SDL_Rect body = {x, y + size / 5, size, size - size / 5};
        ui_round_rect_outline(ui, body, size / 3, color, t);
        int bx = x + size / 4;
        int by = y + size / 2;
        ui_fill_rect(ui, (SDL_Rect){bx - 1, by - 4, 3, 9}, color);
        ui_fill_rect(ui, (SDL_Rect){bx - 4, by - 1, 9, 3}, color);
        ui_circle(ui, x + size - size / 4, by - 2, t + 1, color);
        ui_circle(ui, x + size - size / 4 + 4, by + 2, t + 1, color);
        break;
    }
    case UI_ICON_INFO:
        ring(ui, cx, cy, size / 2, t, color);
        ui_circle(ui, cx, cy - size / 4, t, color);
        ui_fill_rect(ui, (SDL_Rect){cx - t / 2, cy - size / 8, t + 1,
                                    size / 3},
                     color);
        break;
    case UI_ICON_PLAY:
        triangle(ui, (SDL_FPoint){(float)x + size * 0.22f, (float)y + size * 0.12f},
                 (SDL_FPoint){(float)x + size * 0.22f, (float)y + size * 0.88f},
                 (SDL_FPoint){(float)x + size * 0.88f, (float)y + size * 0.5f},
                 color);
        break;
    case UI_ICON_PAUSE:
        ui_round_rect(ui, (SDL_Rect){x + size / 5, y + size / 8, size / 5,
                                     size - size / 4},
                      2, color);
        ui_round_rect(ui, (SDL_Rect){x + size - size / 5 - size / 5,
                                     y + size / 8, size / 5, size - size / 4},
                      2, color);
        break;
    case UI_ICON_RESET: {
        ring(ui, cx, cy, size / 2, t, color);
        int ax = cx + size / 2 - 1;
        int ay = cy - size / 2;
        triangle(ui, (SDL_FPoint){(float)ax, (float)ay},
                 (SDL_FPoint){(float)ax - 6.0f, (float)ay + 1.0f},
                 (SDL_FPoint){(float)ax - 1.0f, (float)ay + 6.0f}, color);
        break;
    }
    case UI_ICON_CAMERA: {
        SDL_Rect body = {x, y + size / 4, size, size - size / 3};
        ui_round_rect_outline(ui, body, 3, color, t);
        ring(ui, cx, cy + size / 8, size / 5, t, color);
        ui_fill_rect(ui, (SDL_Rect){x + size / 3, y + size / 8, size / 3,
                                    size / 6},
                     color);
        break;
    }
    case UI_ICON_FOLDER: {
        SDL_Rect body = {x, y + size / 4, size, size - size / 3};
        ui_round_rect_outline(ui, body, 3, color, t);
        ui_round_rect(ui, (SDL_Rect){x + 2, y + size / 6, size / 2, size / 5},
                      2, color);
        break;
    }
    case UI_ICON_REFRESH: {
        for (int i = 0; i < 24; i++) {
            float a0 = (float)i / 24.0f * 2.0f * UI_PI;
            float a1 = (float)(i + 1) / 24.0f * 2.0f * UI_PI;
            if (i > 19) {
                continue;
            }
            ui_line(ui, cx + (int)(cosf(a0) * (float)(size / 2)),
                    cy + (int)(sinf(a0) * (float)(size / 2)),
                    cx + (int)(cosf(a1) * (float)(size / 2)),
                    cy + (int)(sinf(a1) * (float)(size / 2)), t, color);
        }
        ui_line(ui, cx + size / 2 - 5, cy - size / 2, cx + size / 2 + 1,
                cy - size / 2 + 1, t, color);
        ui_line(ui, cx + size / 2 + 1, cy - size / 2 + 1, cx + size / 2 - 1,
                cy - size / 2 + 7, t, color);
        break;
    }
    case UI_ICON_BACK:
        ui_line(ui, x + size * 3 / 4, y + size / 5, x + size / 3,
                y + size / 2, t + 1, color);
        ui_line(ui, x + size / 3, y + size / 2, x + size * 3 / 4,
                y + size - size / 5, t + 1, color);
        break;
    case UI_ICON_CHECK:
        ui_line(ui, x + size / 5, y + size / 2, x + size * 2 / 5,
                y + size * 3 / 4, t + 1, color);
        ui_line(ui, x + size * 2 / 5, y + size * 3 / 4, x + size * 4 / 5,
                y + size / 4, t + 1, color);
        break;
    default:
        break;
    }
}

void ui_text(Ui *ui, const UiFont *font, const char *text, int x, int y,
             uint32_t color)
{
    font_draw_utf8(ui->renderer, font, text, x, y, to_sdl(color));
}

void ui_text_centered(Ui *ui, const UiFont *font, const char *text,
                      SDL_Rect area, uint32_t color)
{
    int width = font_text_width_utf8(font, text);
    int x = area.x + (area.w - width) / 2;
    int y = area.y + (area.h - font->line_height) / 2;
    ui_text(ui, font, text, x, y, color);
}

void ui_text_right(Ui *ui, const UiFont *font, const char *text, int right,
                   int y, uint32_t color)
{
    int width = font_text_width_utf8(font, text);
    ui_text(ui, font, text, right - width, y, color);
}

int ui_text_width(const UiFont *font, const char *text)
{
    return font_text_width_utf8(font, text);
}

void ui_text_wrapped(Ui *ui, const UiFont *font, const char *text,
                     SDL_Rect area, uint32_t color)
{
    if (!text) {
        return;
    }
    char line[1024] = {0};
    size_t line_len = 0;
    int y = area.y;
    const char *cursor = text;
    while (*cursor) {
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == '\n') {
            ui_text(ui, font, line, area.x, y, color);
            y += font->line_height + 2;
            line[0] = '\0';
            line_len = 0;
            cursor++;
            continue;
        }
        const char *word = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\n') {
            cursor++;
        }
        size_t word_len = (size_t)(cursor - word);
        char candidate[1024];
        if (line_len + word_len + 2 >= sizeof(candidate)) {
            continue;
        }
        memcpy(candidate, line, line_len);
        size_t pos = line_len;
        if (pos > 0) {
            candidate[pos++] = ' ';
        }
        memcpy(candidate + pos, word, word_len);
        candidate[pos + word_len] = '\0';
        if (line_len > 0 &&
            font_text_width_utf8(font, candidate) > area.w) {
            ui_text(ui, font, line, area.x, y, color);
            y += font->line_height + 2;
            memcpy(line, word, word_len);
            line[word_len] = '\0';
            line_len = word_len;
        } else {
            memcpy(line, candidate, pos + word_len + 1);
            line_len = pos + word_len;
        }
        if (y > area.y + area.h) {
            break;
        }
    }
    if (line_len > 0) {
        ui_text(ui, font, line, area.x, y, color);
    }
}

void ui_panel(Ui *ui, SDL_Rect rect, int radius)
{
    ui_round_rect(ui, rect, radius, UI_PANEL);
    ui_round_rect_outline(ui, rect, radius, UI_BORDER_SOFT, 1);
}

bool ui_button(Ui *ui, SDL_Rect rect, const char *label, bool primary,
               bool enabled)
{
    bool hover = enabled && ui_hovered(ui, rect);
    uint32_t bg;
    uint32_t fg;
    uint32_t border;
    if (primary) {
        bg = !enabled ? UI_ACCENT_DIM : (hover ? UI_ACCENT_HOVER : UI_ACCENT);
        fg = UI_ACCENT_TEXT;
        border = 0;
    } else {
        bg = hover ? UI_PANEL_HOVER : UI_PANEL;
        fg = !enabled ? UI_TEXT_FAINT : (hover ? UI_TEXT : UI_TEXT_DIM);
        border = hover ? UI_ACCENT_DIM : UI_BORDER_SOFT;
    }
    ui_round_rect(ui, rect, 10, bg);
    if (border) {
        ui_round_rect_outline(ui, rect, 10, border, 1);
    }
    ui_text_centered(ui, &ui->bold, label, rect, fg);
    if (hover) {
        ui->want_hand = true;
    }
    return enabled && hover && ui->mouse_pressed;
}

bool ui_icon_button(Ui *ui, SDL_Rect rect, UiIcon icon, const char *tooltip,
                    bool primary, bool enabled)
{
    bool hover = enabled && ui_hovered(ui, rect);
    uint32_t bg = primary
                      ? (!enabled ? UI_ACCENT_DIM
                                  : (hover ? UI_ACCENT_HOVER : UI_ACCENT))
                      : (hover ? UI_PANEL_HOVER : UI_PANEL);
    uint32_t fg = primary ? UI_ACCENT_TEXT
                          : (!enabled ? UI_TEXT_FAINT : UI_TEXT);
    ui_round_rect(ui, rect, 10, bg);
    if (!primary) {
        ui_round_rect_outline(ui, rect, 10,
                              hover ? UI_ACCENT_DIM : UI_BORDER_SOFT, 1);
    }
    int icon_size = 18;
    ui_icon(ui, icon, rect.x + (rect.w - icon_size) / 2,
            rect.y + (rect.h - icon_size) / 2, icon_size, fg);
    if (hover && tooltip) {
        int width = ui_text_width(&ui->small, tooltip);
        SDL_Rect tip = {rect.x + (rect.w - width) / 2 - 10,
                        rect.y + rect.h + 8, width + 20, 26};
        if (tip.x < 4) {
            tip.x = 4;
        }
        if (tip.x + tip.w > ui->width - 4) {
            tip.x = ui->width - 4 - tip.w;
        }
        if (tip.y + tip.h > ui->height - 4) {
            tip.y = rect.y - tip.h - 8;
        }
        ui_round_rect(ui, tip, 8, UI_PANEL_ACTIVE);
        ui_round_rect_outline(ui, tip, 8, UI_BORDER, 1);
        ui_text_centered(ui, &ui->small, tooltip, tip, UI_TEXT);
    }
    if (hover) {
        ui->want_hand = true;
    }
    return enabled && hover && ui->mouse_pressed;
}

bool ui_nav_button(Ui *ui, SDL_Rect rect, const char *label, UiIcon icon,
                   bool active)
{
    bool hover = ui_hovered(ui, rect);
    if (active) {
        ui_round_rect(ui, rect, 10, UI_PANEL_ACTIVE);
        ui_round_rect(ui, (SDL_Rect){rect.x, rect.y + 10, 3, rect.h - 20}, 1,
                      UI_ACCENT);
    } else if (hover) {
        ui_round_rect(ui, rect, 10, UI_PANEL_HOVER);
    }
    int icon_size = 18;
    uint32_t icon_color = active ? UI_ACCENT : (hover ? UI_TEXT : UI_TEXT_DIM);
    uint32_t text_color = active ? UI_TEXT : (hover ? UI_TEXT : UI_TEXT_DIM);
    ui_icon(ui, icon, rect.x + 18, rect.y + (rect.h - icon_size) / 2,
            icon_size, icon_color);
    ui_text(ui, &ui->bold, label, rect.x + 50,
            rect.y + (rect.h - ui->bold.line_height) / 2, text_color);
    if (hover) {
        ui->want_hand = true;
    }
    return hover && ui->mouse_pressed;
}

bool ui_toggle(Ui *ui, SDL_Rect row, const char *label, bool *value)
{
    bool hover = ui_hovered(ui, row);
    if (hover) {
        ui_round_rect(ui, row, 8, UI_PANEL_HOVER);
        ui->want_hand = true;
    }
    ui_text(ui, &ui->regular, label, row.x + 12,
            row.y + (row.h - ui->regular.line_height) / 2, UI_TEXT);
    SDL_Rect pill = {row.x + row.w - 58, row.y + (row.h - 26) / 2, 46, 26};
    ui_round_rect(ui, pill, 13, *value ? UI_ACCENT_DIM : UI_BORDER);
    int knob_x = *value ? pill.x + pill.w - 21 : pill.x + 3;
    ui_circle(ui, knob_x + 9, pill.y + 13, 9, *value ? UI_ACCENT : UI_TEXT_FAINT);
    bool clicked = hover && ui->mouse_pressed;
    if (clicked) {
        *value = !*value;
    }
    return clicked;
}

bool ui_slider(Ui *ui, SDL_Rect track, int *value, int minimum, int maximum)
{
    if (maximum <= minimum) {
        return false;
    }
    SDL_Rect hit = {track.x - 8, track.y - 12, track.w + 16, track.h + 24};
    bool hover = ui_hovered(ui, hit);
    if (hover) {
        ui->want_hand = true;
    }
    bool changed = false;
    if (ui->mouse_down && hover) {
        float t = (float)(ui->mouse_x - track.x) / (float)track.w;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        int next = minimum + (int)lroundf(t * (float)(maximum - minimum));
        if (next != *value) {
            *value = next;
            changed = true;
        }
    }
    if (*value < minimum) {
        *value = minimum;
    }
    if (*value > maximum) {
        *value = maximum;
    }
    ui_round_rect(ui, track, track.h / 2, UI_BORDER);
    int filled = (int)((float)track.w * (float)(*value - minimum) /
                       (float)(maximum - minimum));
    if (filled > 0) {
        SDL_Rect fill = {track.x, track.y, filled, track.h};
        ui_round_rect(ui, fill, track.h / 2, UI_ACCENT);
    }
    int knob_x = track.x + filled;
    ui_circle(ui, knob_x, track.y + track.h / 2, track.h / 2 + 5, UI_TEXT);
    ui_circle(ui, knob_x, track.y + track.h / 2, track.h / 2 + 2, UI_ACCENT);
    return changed;
}

void ui_badge(Ui *ui, int x, int y, const char *text, uint32_t bg,
              uint32_t fg)
{
    int width = ui_text_width(&ui->small, text);
    SDL_Rect badge = {x, y, width + 16, 22};
    ui_round_rect(ui, badge, 7, bg);
    ui_text_centered(ui, &ui->small, text, badge, fg);
}
