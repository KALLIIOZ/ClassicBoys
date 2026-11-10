/*
 * ClassicBoy - Game Boy Color (CGB) emulator
 * common.h - shared types, constants and tracked allocation helpers
 *
 * Pure C11. No external dependency required by the core.
 */
#ifndef CB_COMMON_H
#define CB_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GB_SCREEN_W 160
#define GB_SCREEN_H 144
#define GB_CLOCK_HZ 4194304u
#define GB_CGB_CLOCK_HZ 8388608u
#define GB_FRAME_CYCLES 70224u
#define GB_SAMPLE_RATE 48000u
#define GB_MAX_ROM_SIZE (8u * 1024u * 1024u)
#define GB_ROM_BANK_SIZE 0x4000u
#define GB_RAM_BANK_SIZE 0x2000u
#define GB_VRAM_BANK_SIZE 0x2000u
#define GB_WRAM_BANK_SIZE 0x1000u
#define GB_WRAM_BANKS 8u
#define GB_VRAM_BANKS 2u

/* Game Boy Color palette colors are stored as 15-bit RGB (BGR555). */
static inline uint16_t gb_rgb555(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((((uint16_t)b & 0xF8u) << 7) |
                      (((uint16_t)g & 0xF8u) << 2) |
                      ((uint16_t)r >> 3));
}

static inline uint16_t gb_argb_to_rgb555(uint32_t argb)
{
    return gb_rgb555((uint8_t)((argb >> 16) & 0xFFu),
                     (uint8_t)((argb >> 8) & 0xFFu),
                     (uint8_t)(argb & 0xFFu));
}

static inline uint32_t gb_rgb555_to_argb(uint16_t color)
{
    uint32_t r = (color & 0x1Fu) << 3;
    uint32_t g = ((color >> 5) & 0x1Fu) << 3;
    uint32_t b = ((color >> 10) & 0x1Fu) << 3;
    r |= r >> 5;
    g |= g >> 5;
    b |= b >> 5;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

typedef enum GbStatus {
    GB_OK = 0,
    GB_ERR_IO,
    GB_ERR_FORMAT,
    GB_ERR_MEMORY,
    GB_ERR_UNSUPPORTED,
    GB_ERR_ARGUMENT
} GbStatus;

/* ------------------------------------------------------------------ */
/* Tracked allocator                                                   */
/*                                                                     */
/* Every core allocation goes through these helpers. In addition to    */
/* counting live blocks/bytes they place canaries around each block    */
/* so tests can detect overruns and leaks deterministically, even on   */
/* toolchains without AddressSanitizer.                                */
/* ------------------------------------------------------------------ */

void *gb_alloc(size_t size);
void *gb_calloc(size_t count, size_t size);
void *gb_realloc(void *ptr, size_t size);
void gb_free(void *ptr);
char *gb_strdup(const char *text);

/* Returns the number of corrupted blocks found (0 == all good). */
size_t gb_alloc_check(void);
void gb_alloc_stats(size_t *live_blocks, size_t *live_bytes,
                    size_t *peak_bytes);
void gb_alloc_report(FILE *out);

/* Small helpers shared by the emulator and the frontend. */
static inline uint16_t gb_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline void gb_write_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static inline uint32_t gb_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void gb_write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

#endif /* CB_COMMON_H */
