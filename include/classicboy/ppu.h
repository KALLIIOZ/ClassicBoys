/*
 * ClassicBoy - Game Boy Color picture processing unit.
 */
#ifndef CB_PPU_H
#define CB_PPU_H

#include "classicboy/common.h"

struct Gb;

typedef struct GbPpu {
    /* Two 8 KiB VRAM banks (CGB). Bank 0 also holds DMG tile/map data. */
    uint8_t vram[GB_VRAM_BANKS * GB_VRAM_BANK_SIZE];
    uint8_t oam[0xA0];

    uint8_t lcdc;
    uint8_t stat;
    uint8_t scy;
    uint8_t scx;
    uint8_t ly;
    uint8_t lyc;
    uint8_t bgp;
    uint8_t obp0;
    uint8_t obp1;
    uint8_t wy;
    uint8_t wx;

    uint8_t mode;
    uint32_t dot;
    uint8_t window_line;
    bool stat_line;
    bool frame_ready;

    bool cgb;
    uint8_t vram_bank;

    /* CGB palette memory: 8 palettes x 4 colors, stored as RGB555. */
    uint16_t bg_palette[32];
    uint16_t obj_palette[32];
    uint8_t bcps;
    uint8_t ocps;
    uint8_t opri;

    /* DMG compatibility tint used when the cartridge is not a CGB game. */
    uint16_t dmg_colors[4];

    /* CGB HDMA/GDMA state. */
    uint8_t hdma1;
    uint8_t hdma2;
    uint8_t hdma3;
    uint8_t hdma4;
    uint8_t hdma5;
    uint16_t hdma_source;
    uint16_t hdma_dest;
    uint8_t hdma_blocks;
    bool hdma_active;
    bool hdma_hblank;

    /* RGB555 framebuffer. */
    uint16_t pixels[GB_SCREEN_W * GB_SCREEN_H];
} GbPpu;

void ppu_reset(GbPpu *ppu);
void ppu_tick(struct Gb *gb, unsigned cycles);
uint8_t ppu_read_reg(const GbPpu *ppu, uint16_t reg);
void ppu_write_reg(struct Gb *gb, GbPpu *ppu, uint16_t reg, uint8_t value);
bool ppu_vram_accessible(const GbPpu *ppu);
bool ppu_oam_accessible(const GbPpu *ppu);
void ppu_clear_framebuffer(GbPpu *ppu);
void ppu_set_dmg_palette(GbPpu *ppu, const uint32_t argb[4]);

#endif /* CB_PPU_H */
