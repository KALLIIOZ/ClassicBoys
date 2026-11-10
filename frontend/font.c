#include "font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_truetype.h"

#define FONT_ATLAS_W 1024
#define FONT_ATLAS_H 1024

uint32_t font_next_codepoint(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    if (!s || *s == 0) {
        return 0;
    }
    uint32_t cp;
    if (s[0] < 0x80) {
        cp = s[0];
        *text += 1;
        return cp;
    }
    if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
        *text += 2;
        return cp;
    }
    if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x0F) << 12) |
             ((uint32_t)(s[1] & 0x3F) << 6) | (uint32_t)(s[2] & 0x3F);
        *text += 3;
        return cp;
    }
    if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x07) << 18) |
             ((uint32_t)(s[1] & 0x3F) << 12) |
             ((uint32_t)(s[2] & 0x3F) << 6) | (uint32_t)(s[3] & 0x3F);
        *text += 4;
        return cp;
    }
    *text += 1;
    return '?';
}

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long end = ftell(file);
    if (end <= 0 || end > 32 * 1024 * 1024) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    unsigned char *data = (unsigned char *)malloc((size_t)end);
    if (!data) {
        fclose(file);
        return NULL;
    }
    size_t got = fread(data, 1, (size_t)end, file);
    fclose(file);
    if (got != (size_t)end) {
        free(data);
        return NULL;
    }
    *size = (size_t)end;
    return data;
}

bool font_load(UiFont *font, SDL_Renderer *renderer, const char *path,
               int pixel_height)
{
    if (!font || !renderer || !path || pixel_height < 6) {
        return false;
    }
    memset(font, 0, sizeof(*font));

    size_t ttf_size = 0;
    unsigned char *ttf = read_file(path, &ttf_size);
    if (!ttf) {
        return false;
    }
    stbtt_fontinfo info;
    int offset = stbtt_GetFontOffsetForIndex(ttf, 0);
    if (offset < 0 || !stbtt_InitFont(&info, ttf, offset)) {
        free(ttf);
        return false;
    }

    float scale = stbtt_ScaleForPixelHeight(&info, (float)pixel_height);
    int ascent_raw = 0;
    int descent_raw = 0;
    int gap_raw = 0;
    stbtt_GetFontVMetrics(&info, &ascent_raw, &descent_raw, &gap_raw);
    font->pixel_height = pixel_height;
    font->ascent = (int)lroundf((float)ascent_raw * scale);
    font->descent = (int)lroundf((float)descent_raw * scale);
    font->line_height = font->ascent - font->descent +
                        (int)lroundf((float)gap_raw * scale);
    if (font->line_height < pixel_height) {
        font->line_height = pixel_height + 2;
    }

    size_t atlas_pixels = (size_t)FONT_ATLAS_W * FONT_ATLAS_H;
    unsigned char *bitmap = (unsigned char *)calloc(atlas_pixels, 1);
    if (!bitmap) {
        free(ttf);
        return false;
    }

    int pen_x = 1;
    int pen_y = 1;
    int row_height = 0;
    for (uint32_t cp = 32; cp < 256; cp++) {
        int advance = 0;
        int lsb = 0;
        stbtt_GetCodepointHMetrics(&info, (int)cp, &advance, &lsb);
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetCodepointBitmapBox(&info, (int)cp, scale, scale, &x0, &y0,
                                    &x1, &y1);
        int w = x1 - x0;
        int h = y1 - y0;
        if (w < 0 || h < 0) {
            w = 0;
            h = 0;
        }
        if (pen_x + w + 1 > FONT_ATLAS_W) {
            pen_x = 1;
            pen_y += row_height + 1;
            row_height = 0;
        }
        if (pen_y + h + 1 > FONT_ATLAS_H) {
            free(bitmap);
            free(ttf);
            return false;
        }
        UiGlyph *glyph = &font->glyphs[cp];
        glyph->w = w;
        glyph->h = h;
        glyph->xoff = x0;
        glyph->yoff = y0;
        glyph->xadv = (int)lroundf((float)advance * scale);
        glyph->u0 = (float)pen_x / (float)FONT_ATLAS_W;
        glyph->v0 = (float)pen_y / (float)FONT_ATLAS_H;
        glyph->u1 = (float)(pen_x + w) / (float)FONT_ATLAS_W;
        glyph->v1 = (float)(pen_y + h) / (float)FONT_ATLAS_H;
        if (w > 0 && h > 0) {
            stbtt_MakeCodepointBitmap(&info, bitmap + (size_t)pen_y * FONT_ATLAS_W + pen_x,
                                      w, h, FONT_ATLAS_W, scale, scale, (int)cp);
        }
        pen_x += w + 2;
        if (h > row_height) {
            row_height = h;
        }
    }

    uint32_t *rgba = (uint32_t *)malloc(atlas_pixels * sizeof(uint32_t));
    if (!rgba) {
        free(bitmap);
        free(ttf);
        return false;
    }
    for (size_t i = 0; i < atlas_pixels; i++) {
        uint32_t alpha = bitmap[i];
        rgba[i] = (alpha << 24) | 0x00FFFFFFu;
    }
    font->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STATIC, FONT_ATLAS_W,
                                      FONT_ATLAS_H);
    if (!font->texture) {
        free(rgba);
        free(bitmap);
        free(ttf);
        return false;
    }
    SDL_UpdateTexture(font->texture, NULL, rgba, FONT_ATLAS_W * 4);
    SDL_SetTextureBlendMode(font->texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(font->texture, SDL_ScaleModeNearest);
    font->atlas_w = FONT_ATLAS_W;
    font->atlas_h = FONT_ATLAS_H;
    font->loaded = true;

    free(rgba);
    free(bitmap);
    free(ttf);
    return true;
}

void font_free(UiFont *font)
{
    if (!font) {
        return;
    }
    if (font->texture) {
        SDL_DestroyTexture(font->texture);
    }
    memset(font, 0, sizeof(*font));
}

static int draw_line(SDL_Renderer *renderer, const UiFont *font,
                     const char *text, int x, int y, SDL_Color color)
{
    int start_x = x;
    int baseline = y + font->ascent;
    const char *cursor = text;
    while (cursor && *cursor) {
        uint32_t cp = font_next_codepoint(&cursor);
        if (cp == 0) {
            break;
        }
        if (cp > 255) {
            cp = '?';
        }
        const UiGlyph *glyph = &font->glyphs[cp];
        if (glyph->w > 0 && glyph->h > 0) {
            SDL_Rect dst;
            dst.x = x + glyph->xoff;
            dst.y = baseline + glyph->yoff;
            dst.w = glyph->w;
            dst.h = glyph->h;
            SDL_Rect src;
            src.x = (int)(glyph->u0 * (float)font->atlas_w);
            src.y = (int)(glyph->v0 * (float)font->atlas_h);
            src.w = glyph->w;
            src.h = glyph->h;
            SDL_SetTextureColorMod(font->texture, color.r, color.g, color.b);
            SDL_SetTextureAlphaMod(font->texture, color.a);
            SDL_RenderCopy(renderer, font->texture, &src, &dst);
        }
        x += glyph->xadv;
    }
    return x - start_x;
}

int font_draw_utf8(SDL_Renderer *renderer, const UiFont *font,
                   const char *text, int x, int y, SDL_Color color)
{
    if (!renderer || !font || !font->loaded || !text) {
        return 0;
    }
    int max_width = 0;
    int start_x = x;
    const char *cursor = text;
    while (*cursor) {
        const char *line_start = cursor;
        while (*cursor && *cursor != '\n') {
            cursor++;
        }
        size_t len = (size_t)(cursor - line_start);
        char *line = (char *)malloc(len + 1);
        if (!line) {
            break;
        }
        memcpy(line, line_start, len);
        line[len] = '\0';
        int width = draw_line(renderer, font, line, x, y, color);
        free(line);
        if (width > max_width) {
            max_width = width;
        }
        if (*cursor == '\n') {
            cursor++;
            y += font->line_height;
        }
    }
    (void)start_x;
    return max_width;
}

int font_draw(SDL_Renderer *renderer, const UiFont *font, const char *text,
              int x, int y, SDL_Color color)
{
    return font_draw_utf8(renderer, font, text, x, y, color);
}

static int measure_line(const UiFont *font, const char *text)
{
    int width = 0;
    const char *cursor = text;
    while (cursor && *cursor) {
        uint32_t cp = font_next_codepoint(&cursor);
        if (cp > 255) {
            cp = '?';
        }
        width += font->glyphs[cp].xadv;
    }
    return width;
}

int font_text_width_utf8(const UiFont *font, const char *text)
{
    if (!font || !font->loaded || !text) {
        return 0;
    }
    int max_width = 0;
    const char *cursor = text;
    while (*cursor) {
        const char *line_start = cursor;
        while (*cursor && *cursor != '\n') {
            cursor++;
        }
        size_t len = (size_t)(cursor - line_start);
        char *line = (char *)malloc(len + 1);
        if (!line) {
            break;
        }
        memcpy(line, line_start, len);
        line[len] = '\0';
        int width = measure_line(font, line);
        free(line);
        if (width > max_width) {
            max_width = width;
        }
        if (*cursor == '\n') {
            cursor++;
        }
    }
    return max_width;
}

int font_text_width(const UiFont *font, const char *text)
{
    return font_text_width_utf8(font, text);
}
