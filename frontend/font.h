/*
 * ClassicBoy - TrueType font rendering for the SDL2 frontend.
 */
#ifndef CB_FONT_H
#define CB_FONT_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct UiGlyph {
    float u0, v0, u1, v1;
    int w, h;
    int xoff, yoff;
    int xadv;
} UiGlyph;

typedef struct UiFont {
    SDL_Texture *texture;
    int atlas_w;
    int atlas_h;
    int pixel_height;
    int ascent;
    int descent;
    int line_height;
    UiGlyph glyphs[256];
    bool loaded;
} UiFont;

bool font_load(UiFont *font, SDL_Renderer *renderer, const char *path,
               int pixel_height);
void font_free(UiFont *font);

/* Draws text with its top-left corner at (x, y). Returns the advance width. */
int font_draw(SDL_Renderer *renderer, const UiFont *font, const char *text,
              int x, int y, SDL_Color color);
int font_draw_utf8(SDL_Renderer *renderer, const UiFont *font,
                   const char *text, int x, int y, SDL_Color color);
int font_text_width(const UiFont *font, const char *text);
int font_text_width_utf8(const UiFont *font, const char *text);

/* Decodes one UTF-8 codepoint and advances *text. */
uint32_t font_next_codepoint(const char **text);

#endif /* CB_FONT_H */
