#include "classicboy/ppu.h"

#include "classicboy/gb.h"
#include "classicboy/mmu.h"

static uint8_t shade_from_palette(uint8_t palette, uint8_t color)
{
    return (uint8_t)((palette >> (color * 2u)) & 0x03u);
}

static uint8_t stat_value(const GbPpu *ppu)
{
    uint8_t coincidence = (ppu->ly == ppu->lyc) ? 0x04u : 0x00u;
    return (uint8_t)(0x80u | (ppu->stat & 0x78u) | coincidence |
                     (ppu->mode & 0x03u));
}

static void update_stat(struct Gb *gb, GbPpu *ppu)
{
    bool line = false;
    if ((ppu->stat & 0x08u) && ppu->mode == 0) {
        line = true;
    }
    if ((ppu->stat & 0x10u) && ppu->mode == 1) {
        line = true;
    }
    if ((ppu->stat & 0x20u) && ppu->mode == 2) {
        line = true;
    }
    if ((ppu->stat & 0x40u) && ppu->ly == ppu->lyc) {
        line = true;
    }
    if (line && !ppu->stat_line) {
        gb_request_interrupt(gb, 1);
    }
    ppu->stat_line = line;
}

void ppu_clear_framebuffer(GbPpu *ppu)
{
    if (!ppu) {
        return;
    }
    uint16_t blank = ppu->cgb ? 0x7FFFu : ppu->dmg_colors[0];
    for (size_t i = 0; i < GB_SCREEN_W * GB_SCREEN_H; i++) {
        ppu->pixels[i] = blank;
    }
}

void ppu_set_dmg_palette(GbPpu *ppu, const uint32_t argb[4])
{
    if (!ppu || !argb) {
        return;
    }
    for (unsigned i = 0; i < 4; i++) {
        ppu->dmg_colors[i] = gb_argb_to_rgb555(argb[i]);
    }
}

void ppu_reset(GbPpu *ppu)
{
    if (!ppu) {
        return;
    }
    memset(ppu, 0, sizeof(*ppu));
    ppu->lcdc = 0x91;
    ppu->stat = 0x00;
    ppu->bgp = 0xFC;
    ppu->obp0 = 0xFF;
    ppu->obp1 = 0xFF;
    ppu->ly = 0;
    ppu->mode = 2;
    ppu->dot = 0;
    ppu->cgb = false;
    ppu->vram_bank = 0;
    ppu->opri = 0x00;
    ppu->bcps = 0x00;
    ppu->ocps = 0x00;

    ppu->dmg_colors[0] = gb_rgb555(0x9B, 0xBC, 0x0F);
    ppu->dmg_colors[1] = gb_rgb555(0x8B, 0xAC, 0x0F);
    ppu->dmg_colors[2] = gb_rgb555(0x30, 0x62, 0x30);
    ppu->dmg_colors[3] = gb_rgb555(0x0F, 0x38, 0x0F);

    for (unsigned i = 0; i < 32; i++) {
        ppu->bg_palette[i] = 0x7FFFu;
        ppu->obj_palette[i] = 0x7FFFu;
    }

    ppu->hdma1 = 0xFF;
    ppu->hdma2 = 0xFF;
    ppu->hdma3 = 0xFF;
    ppu->hdma4 = 0xFF;
    ppu->hdma5 = 0xFF;
    ppu->hdma_source = 0x0000;
    ppu->hdma_dest = 0x8000;
    ppu->hdma_blocks = 0;
    ppu->hdma_active = false;
    ppu->hdma_hblank = false;

    ppu_clear_framebuffer(ppu);
}

bool ppu_vram_accessible(const GbPpu *ppu)
{
    if (!(ppu->lcdc & 0x80u)) {
        return true;
    }
    return ppu->mode != 3;
}

bool ppu_oam_accessible(const GbPpu *ppu)
{
    if (!(ppu->lcdc & 0x80u)) {
        return true;
    }
    return ppu->mode == 0 || ppu->mode == 1;
}

/* ------------------------------------------------------------------ */
/* Background / window rendering                                     */
/* ------------------------------------------------------------------ */

static uint16_t dmg_bg_pixel(const GbPpu *ppu, int x, int y,
                             bool *used_window, uint8_t *shade_out)
{
    if (!(ppu->lcdc & 0x01u)) {
        *shade_out = 0;
        return ppu->dmg_colors[0];
    }

    uint16_t map_base = (ppu->lcdc & 0x08u) ? 0x1C00u : 0x1800u;
    int map_x;
    int map_y;

    int window_start = (int)ppu->wx - 7;
    bool window = (ppu->lcdc & 0x20u) && ppu->wy <= ppu->ly &&
                  ppu->wx <= 166u && x >= window_start && window_start < 160;
    if (window) {
        map_base = (ppu->lcdc & 0x40u) ? 0x1C00u : 0x1800u;
        map_x = x - window_start;
        map_y = ppu->window_line;
        *used_window = true;
    } else {
        map_x = (ppu->scx + x) & 0xFF;
        map_y = (ppu->scy + y) & 0xFF;
    }

    uint8_t tile_index =
        ppu->vram[map_base + (uint16_t)((map_y >> 3) * 32 + (map_x >> 3))];
    int tile_address;
    if (ppu->lcdc & 0x10u) {
        tile_address = tile_index * 16;
    } else {
        tile_address = 0x1000 + ((int)(int8_t)tile_index) * 16;
    }
    uint16_t row = (uint16_t)((map_y & 7) * 2);
    uint8_t low = ppu->vram[(tile_address + row) & 0x1FFF];
    uint8_t high = ppu->vram[(tile_address + row + 1) & 0x1FFF];
    unsigned bit = (unsigned)(7 - (map_x & 7));
    uint8_t color = (uint8_t)(((low >> bit) & 1u) |
                              (((high >> bit) & 1u) << 1));
    uint8_t shade = shade_from_palette(ppu->bgp, color);
    *shade_out = shade;
    return ppu->dmg_colors[shade & 0x03u];
}

static uint16_t cgb_bg_pixel(const GbPpu *ppu, int x, int y,
                             bool *used_window, bool *opaque,
                             bool *priority)
{
    *opaque = false;
    *priority = false;
    if (!(ppu->lcdc & 0x01u)) {
        /* On CGB the background is white when LCDC bit 0 is cleared. */
        return 0x7FFFu;
    }

    uint16_t map_base = (ppu->lcdc & 0x08u) ? 0x1C00u : 0x1800u;
    int map_x;
    int map_y;

    int window_start = (int)ppu->wx - 7;
    bool window = (ppu->lcdc & 0x20u) && ppu->wy <= ppu->ly &&
                  ppu->wx <= 166u && x >= window_start && window_start < 160;
    if (window) {
        map_base = (ppu->lcdc & 0x40u) ? 0x1C00u : 0x1800u;
        map_x = x - window_start;
        map_y = ppu->window_line;
        *used_window = true;
    } else {
        map_x = (ppu->scx + x) & 0xFF;
        map_y = (ppu->scy + y) & 0xFF;
    }

    uint16_t map_offset =
        (uint16_t)(map_base + (uint16_t)((map_y >> 3) * 32 + (map_x >> 3)));
    uint8_t tile_index = ppu->vram[map_offset];
    uint8_t attributes = ppu->vram[GB_VRAM_BANK_SIZE + map_offset];

    unsigned bank = (attributes >> 3) & 1u;
    unsigned palette_index = attributes & 0x07u;
    bool xflip = (attributes & 0x20u) != 0;
    bool yflip = (attributes & 0x40u) != 0;
    *priority = (attributes & 0x80u) != 0;

    int tile_address;
    if (ppu->lcdc & 0x10u) {
        tile_address = tile_index * 16;
    } else {
        tile_address = 0x1000 + ((int)(int8_t)tile_index) * 16;
    }
    int row = map_y & 7;
    if (yflip) {
        row = 7 - row;
    }
    uint16_t vram_base = (uint16_t)(bank * GB_VRAM_BANK_SIZE);
    uint8_t low =
        ppu->vram[vram_base + ((tile_address + row * 2) & 0x1FFF)];
    uint8_t high =
        ppu->vram[vram_base + ((tile_address + row * 2 + 1) & 0x1FFF)];
    unsigned bit = xflip ? (unsigned)(map_x & 7)
                         : (unsigned)(7 - (map_x & 7));
    uint8_t color = (uint8_t)(((low >> bit) & 1u) |
                              (((high >> bit) & 1u) << 1));
    *opaque = color != 0;
    return ppu->bg_palette[(palette_index * 4u + color) & 0x1Fu];
}

/* ------------------------------------------------------------------ */
/* Sprite rendering                                                  */
/* ------------------------------------------------------------------ */

typedef struct SpriteCandidate {
    int oam_index;
    int x;
} SpriteCandidate;

static uint16_t dmg_sprite_color(const GbPpu *ppu, uint8_t flags,
                                 uint8_t color)
{
    uint8_t palette = (flags & 0x10u) ? ppu->obp1 : ppu->obp0;
    uint8_t shade = shade_from_palette(palette, color);
    return ppu->dmg_colors[shade & 0x03u];
}

static void render_scanline(struct Gb *gb)
{
    GbPpu *ppu = &gb->ppu;
    if (ppu->ly >= GB_SCREEN_H) {
        return;
    }

    uint16_t bg_colors[GB_SCREEN_W];
    bool bg_opaque[GB_SCREEN_W];
    bool bg_priority[GB_SCREEN_W];
    bool window_used = false;

    for (int x = 0; x < GB_SCREEN_W; x++) {
        bg_priority[x] = false;
        if (ppu->cgb) {
            bg_colors[x] = cgb_bg_pixel(ppu, x, ppu->ly, &window_used,
                                        &bg_opaque[x], &bg_priority[x]);
        } else {
            uint8_t shade = 0;
            bg_colors[x] =
                dmg_bg_pixel(ppu, x, ppu->ly, &window_used, &shade);
            bg_opaque[x] = shade != 0;
        }
    }
    if (window_used) {
        ppu->window_line++;
    }

    uint16_t sprite_colors[GB_SCREEN_W];
    bool sprite_set[GB_SCREEN_W];
    bool claimed[GB_SCREEN_W];
    memset(sprite_set, 0, sizeof(sprite_set));
    memset(claimed, 0, sizeof(claimed));

    if (ppu->lcdc & 0x02u) {
        SpriteCandidate sprites[10];
        int count = 0;
        int height = (ppu->lcdc & 0x04u) ? 16 : 8;
        for (int i = 0; i < 40 && count < 10; i++) {
            int sy = (int)ppu->oam[i * 4] - 16;
            if ((int)ppu->ly < sy || (int)ppu->ly >= sy + height) {
                continue;
            }
            sprites[count].oam_index = i;
            sprites[count].x = (int)ppu->oam[i * 4 + 1] - 8;
            count++;
        }

        /* DMG priority (and CGB with OPRI=1) sorts by X, OAM order breaks
         * ties. CGB with OPRI=0 keeps OAM order. */
        bool sort_by_x = !ppu->cgb || (ppu->opri & 0x01u) != 0;
        if (sort_by_x) {
            for (int i = 1; i < count; i++) {
                SpriteCandidate key = sprites[i];
                int j = i - 1;
                while (j >= 0 && sprites[j].x > key.x) {
                    sprites[j + 1] = sprites[j];
                    j--;
                }
                sprites[j + 1] = key;
            }
        }

        for (int s = 0; s < count; s++) {
            int i = sprites[s].oam_index;
            int sy = (int)ppu->oam[i * 4] - 16;
            int sx = (int)ppu->oam[i * 4 + 1] - 8;
            if (sx <= -8 || sx >= GB_SCREEN_W) {
                continue;
            }
            uint8_t tile = ppu->oam[i * 4 + 2];
            uint8_t flags = ppu->oam[i * 4 + 3];
            int row = (int)ppu->ly - sy;
            if (flags & 0x40u) {
                row = height - 1 - row;
            }
            if (height == 16) {
                tile &= 0xFEu;
            }
            unsigned bank = 0;
            unsigned palette_index = 0;
            if (ppu->cgb) {
                bank = (flags >> 3) & 1u;
                palette_index = flags & 0x07u;
            }

            uint16_t vram_base = (uint16_t)(bank * GB_VRAM_BANK_SIZE);
            int tile_address = (int)tile * 16 + row * 2;
            uint8_t low =
                ppu->vram[vram_base + (tile_address & 0x1FFF)];
            uint8_t high =
                ppu->vram[vram_base + ((tile_address + 1) & 0x1FFF)];

            for (int px = 0; px < 8; px++) {
                int x = sx + px;
                if (x < 0 || x >= GB_SCREEN_W) {
                    continue;
                }
                int bit = (flags & 0x20u) ? px : 7 - px;
                uint8_t color = (uint8_t)(((low >> bit) & 1u) |
                                          (((high >> bit) & 1u) << 1));
                if (color == 0 || claimed[x]) {
                    continue;
                }
                claimed[x] = true;

                if (ppu->cgb) {
                    bool hidden =
                        bg_opaque[x] &&
                        ((bg_priority[x]) || (flags & 0x80u) != 0);
                    if (hidden) {
                        continue;
                    }
                    sprite_colors[x] =
                        ppu->obj_palette[(palette_index * 4u + color) & 0x1Fu];
                } else {
                    if ((flags & 0x80u) && bg_opaque[x]) {
                        continue;
                    }
                    sprite_colors[x] = dmg_sprite_color(ppu, flags, color);
                }
                sprite_set[x] = true;
            }
        }
    }

    uint16_t *out = &ppu->pixels[ppu->ly * GB_SCREEN_W];
    for (int x = 0; x < GB_SCREEN_W; x++) {
        out[x] = sprite_set[x] ? sprite_colors[x] : bg_colors[x];
    }
}

/* ------------------------------------------------------------------ */
/* CGB HDMA / GDMA                                                   */
/* ------------------------------------------------------------------ */

static void hdma_sync_registers(GbPpu *ppu)
{
    ppu->hdma1 = (uint8_t)(ppu->hdma_source >> 8);
    ppu->hdma2 = (uint8_t)(ppu->hdma_source & 0xF0u);
    ppu->hdma3 = (uint8_t)(0x80u | ((ppu->hdma_dest >> 8) & 0x1Fu));
    ppu->hdma4 = (uint8_t)(ppu->hdma_dest & 0xF0u);
}

static void hdma_transfer_block(Gb *gb, GbPpu *ppu)
{
    if (!ppu->hdma_active || ppu->hdma_blocks == 0) {
        return;
    }
    for (unsigned i = 0; i < 16; i++) {
        uint8_t value = gb_read(gb, ppu->hdma_source);
        uint16_t vram_offset =
            (uint16_t)(((ppu->vram_bank & 1u) * GB_VRAM_BANK_SIZE) +
                       (ppu->hdma_dest & 0x1FFFu));
        ppu->vram[vram_offset] = value;
        ppu->hdma_source++;
        ppu->hdma_dest =
            (uint16_t)(0x8000u | ((ppu->hdma_dest + 1u) & 0x1FFFu));
    }
    ppu->hdma_blocks--;
    if (ppu->hdma_blocks == 0) {
        ppu->hdma_active = false;
        ppu->hdma_hblank = false;
        ppu->hdma5 = 0xFF;
    } else {
        ppu->hdma5 = (uint8_t)(ppu->hdma_blocks - 1u);
    }
    hdma_sync_registers(ppu);
}

static uint8_t hdma_read_register(const GbPpu *ppu, uint16_t reg)
{
    switch (reg) {
    case 0xFF51u:
        return ppu->hdma1;
    case 0xFF52u:
        return ppu->hdma2;
    case 0xFF53u:
        return ppu->hdma3;
    case 0xFF54u:
        return ppu->hdma4;
    case 0xFF55u:
        return ppu->hdma5;
    default:
        return 0xFF;
    }
}

static void hdma_write_register(Gb *gb, GbPpu *ppu, uint16_t reg,
                                uint8_t value)
{
    switch (reg) {
    case 0xFF51u:
        if (ppu->hdma_active) {
            return;
        }
        ppu->hdma1 = value;
        ppu->hdma_source = (uint16_t)((ppu->hdma_source & 0x00FFu) |
                                      ((uint16_t)value << 8));
        break;
    case 0xFF52u:
        if (ppu->hdma_active) {
            return;
        }
        ppu->hdma2 = value & 0xF0u;
        ppu->hdma_source = (uint16_t)((ppu->hdma_source & 0xFF00u) |
                                      (value & 0xF0u));
        break;
    case 0xFF53u:
        if (ppu->hdma_active) {
            return;
        }
        ppu->hdma3 = value & 0x1Fu;
        ppu->hdma_dest = (uint16_t)(0x8000u |
                                    ((uint16_t)(value & 0x1Fu) << 8) |
                                    (ppu->hdma_dest & 0x00F0u));
        break;
    case 0xFF54u:
        if (ppu->hdma_active) {
            return;
        }
        ppu->hdma4 = value & 0xF0u;
        ppu->hdma_dest = (uint16_t)((ppu->hdma_dest & 0xFF00u) |
                                    (value & 0xF0u));
        break;
    case 0xFF55u:
        if (ppu->hdma_active) {
            if ((value & 0x80u) == 0) {
                /* Cancel the active HDMA transfer. */
                ppu->hdma_active = false;
                ppu->hdma_hblank = false;
                ppu->hdma5 =
                    (uint8_t)(0x80u | ((ppu->hdma_blocks - 1u) & 0x7Fu));
            }
            return;
        }
        ppu->hdma_blocks = (uint8_t)((value & 0x7Fu) + 1u);
        ppu->hdma_hblank = (value & 0x80u) != 0;
        ppu->hdma_active = true;
        ppu->hdma5 = (uint8_t)(ppu->hdma_blocks - 1u);
        if (!ppu->hdma_hblank) {
            while (ppu->hdma_active) {
                hdma_transfer_block(gb, ppu);
            }
            ppu->hdma5 = 0xFF;
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Palette RAM access                                                */
/* ------------------------------------------------------------------ */

static uint8_t palette_read_byte(const uint16_t *ram, uint8_t index)
{
    uint8_t i = (uint8_t)(index & 0x3Fu);
    uint16_t word = ram[i / 2u];
    return (i & 1u) ? (uint8_t)(word >> 8) : (uint8_t)(word & 0xFFu);
}

static void palette_write_byte(uint16_t *ram, uint8_t *index, uint8_t value)
{
    uint8_t i = (uint8_t)(*index & 0x3Fu);
    if (i & 1u) {
        ram[i / 2u] = (uint16_t)((ram[i / 2u] & 0x00FFu) |
                                 ((uint16_t)value << 8));
    } else {
        ram[i / 2u] = (uint16_t)((ram[i / 2u] & 0xFF00u) | value);
    }
    if (*index & 0x80u) {
        uint8_t next = (uint8_t)((*index + 1u) & 0x3Fu);
        *index = (uint8_t)((*index & 0x80u) | next);
    }
}

/* ------------------------------------------------------------------ */
/* PPU timing                                                        */
/* ------------------------------------------------------------------ */

void ppu_tick(struct Gb *gb, unsigned cycles)
{
    GbPpu *ppu = &gb->ppu;
    if (!(ppu->lcdc & 0x80u)) {
        return;
    }

    ppu->dot = ppu->dot + cycles;
    while (ppu->dot >= 456u) {
        ppu->dot -= 456u;
        ppu->ly++;
        if (ppu->ly == 144u) {
            ppu->mode = 1;
            gb_request_interrupt(gb, 0);
            update_stat(gb, ppu);
        } else if (ppu->ly >= 154u) {
            ppu->ly = 0;
            ppu->window_line = 0;
            ppu->mode = 2;
            ppu->frame_ready = true;
            update_stat(gb, ppu);
        } else {
            ppu->mode = 2;
            update_stat(gb, ppu);
        }
    }

    if (ppu->ly < 144u) {
        if (ppu->mode == 2 && ppu->dot >= 80u) {
            ppu->mode = 3;
            update_stat(gb, ppu);
        }
        if (ppu->mode == 3 && ppu->dot >= 252u) {
            ppu->mode = 0;
            render_scanline(gb);
            if (ppu->hdma_active && ppu->hdma_hblank) {
                hdma_transfer_block(gb, ppu);
            }
            update_stat(gb, ppu);
        }
    }
}

uint8_t ppu_read_reg(const GbPpu *ppu, uint16_t reg)
{
    switch (reg) {
    case 0xFF40u:
        return ppu->lcdc;
    case 0xFF41u:
        return stat_value(ppu);
    case 0xFF42u:
        return ppu->scy;
    case 0xFF43u:
        return ppu->scx;
    case 0xFF44u:
        return ppu->ly;
    case 0xFF45u:
        return ppu->lyc;
    case 0xFF47u:
        return ppu->bgp;
    case 0xFF48u:
        return ppu->obp0;
    case 0xFF49u:
        return ppu->obp1;
    case 0xFF4Au:
        return ppu->wy;
    case 0xFF4Bu:
        return ppu->wx;
    case 0xFF51u:
    case 0xFF52u:
    case 0xFF53u:
    case 0xFF54u:
    case 0xFF55u:
        return hdma_read_register(ppu, reg);
    case 0xFF68u:
        return ppu->bcps;
    case 0xFF69u:
        return palette_read_byte(ppu->bg_palette, ppu->bcps);
    case 0xFF6Au:
        return ppu->ocps;
    case 0xFF6Bu:
        return palette_read_byte(ppu->obj_palette, ppu->ocps);
    case 0xFF6Cu:
        return (uint8_t)(0xFEu | (ppu->opri & 0x01u));
    default:
        return 0xFF;
    }
}

void ppu_write_reg(struct Gb *gb, GbPpu *ppu, uint16_t reg, uint8_t value)
{
    switch (reg) {
    case 0xFF40u: {
        bool was_on = (ppu->lcdc & 0x80u) != 0;
        bool now_on = (value & 0x80u) != 0;
        ppu->lcdc = value;
        if (was_on && !now_on) {
            ppu->ly = 0;
            ppu->dot = 0;
            ppu->mode = 0;
            ppu->stat_line = false;
            ppu_clear_framebuffer(ppu);
        } else if (!was_on && now_on) {
            ppu->ly = 0;
            ppu->dot = 0;
            ppu->mode = 2;
            ppu->window_line = 0;
            ppu->stat_line = false;
            update_stat(gb, ppu);
        }
        break;
    }
    case 0xFF41u:
        ppu->stat = value & 0x78u;
        update_stat(gb, ppu);
        break;
    case 0xFF42u:
        ppu->scy = value;
        break;
    case 0xFF43u:
        ppu->scx = value;
        break;
    case 0xFF44u:
        ppu->ly = 0;
        ppu->dot = 0;
        ppu->mode = 2;
        update_stat(gb, ppu);
        break;
    case 0xFF45u:
        ppu->lyc = value;
        update_stat(gb, ppu);
        break;
    case 0xFF47u:
        ppu->bgp = value;
        break;
    case 0xFF48u:
        ppu->obp0 = value;
        break;
    case 0xFF49u:
        ppu->obp1 = value;
        break;
    case 0xFF4Au:
        ppu->wy = value;
        break;
    case 0xFF4Bu:
        ppu->wx = value;
        break;
    case 0xFF51u:
    case 0xFF52u:
    case 0xFF53u:
    case 0xFF54u:
    case 0xFF55u:
        hdma_write_register(gb, ppu, reg, value);
        break;
    case 0xFF68u:
        ppu->bcps = value & 0xBFu;
        break;
    case 0xFF69u:
        palette_write_byte(ppu->bg_palette, &ppu->bcps, value);
        break;
    case 0xFF6Au:
        ppu->ocps = value & 0xBFu;
        break;
    case 0xFF6Bu:
        palette_write_byte(ppu->obj_palette, &ppu->ocps, value);
        break;
    case 0xFF6Cu:
        ppu->opri = value & 0x01u;
        break;
    default:
        break;
    }
}
