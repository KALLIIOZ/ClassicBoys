#include "test.h"

#include "classicboy/gb.h"
#include "test_util.h"

static bool load_cgb_rom(Gb *gb, bool cgb_only)
{
    uint8_t *rom = test_rom_make(0x8000, 0x00, 0x00, 0x00);
    if (!rom) {
        return false;
    }
    test_rom_set_cgb(rom, cgb_only);
    const uint8_t code[] = {0x18, 0xFE};
    test_rom_set_code(rom, code, sizeof(code));
    GbStatus status = gb_load_rom_data(gb, rom, 0x8000, NULL);
    gb_free(rom);
    return status == GB_OK;
}

static bool load_dmg_rom(Gb *gb)
{
    uint8_t *rom = test_rom_make(0x8000, 0x00, 0x00, 0x00);
    if (!rom) {
        return false;
    }
    const uint8_t code[] = {0x18, 0xFE};
    test_rom_set_code(rom, code, sizeof(code));
    GbStatus status = gb_load_rom_data(gb, rom, 0x8000, NULL);
    gb_free(rom);
    return status == GB_OK;
}

static void fill_tile_color(uint8_t *vram, unsigned tile, uint8_t color)
{
    for (int row = 0; row < 8; row++) {
        uint8_t low = (color & 0x01u) ? 0xFFu : 0x00u;
        uint8_t high = (color & 0x02u) ? 0xFFu : 0x00u;
        vram[tile * 16 + (unsigned)row * 2] = low;
        vram[tile * 16 + (unsigned)row * 2 + 1] = high;
    }
}

static void render_line(Gb *gb, uint8_t ly, uint8_t lcdc)
{
    gb->ppu.lcdc = lcdc;
    gb->ppu.mode = 2;
    gb->ppu.dot = 0;
    gb->ppu.ly = ly;
    gb->ppu.window_line = 0;
    gb->ppu.stat_line = false;
    ppu_tick(gb, 252);
}

static void test_cgb_boot(void)
{
    Gb gb;
    test_gb_init(&gb);
    TEST_ASSERT(load_cgb_rom(&gb, false));

    TEST_ASSERT(gb.cgb_mode);
    TEST_ASSERT(gb_is_cgb(&gb));
    TEST_ASSERT(gb.ppu.cgb);
    TEST_ASSERT(!gb.cart.cgb_only);
    TEST_EQ_U8(gb.cpu.a, 0x11);
    TEST_EQ_U8(gb.cpu.f, 0x80);
    TEST_EQ_U8(gb.cpu.b, 0x00);
    TEST_EQ_U8(gb.cpu.c, 0x00);
    TEST_EQ_U8(gb.cpu.d, 0xFF);
    TEST_EQ_U8(gb.cpu.e, 0x56);
    TEST_EQ_U8(gb.cpu.h, 0x00);
    TEST_EQ_U8(gb.cpu.l, 0x0D);
    TEST_EQ_U16(gb.cpu.pc, 0x0100);
    TEST_EQ_U16(gb.cpu.sp, 0xFFFE);
    TEST_EQ_U16(gb_framebuffer(&gb)[0], 0x7FFF);
    TEST_EQ_U16(gb.ppu.pixels[GB_SCREEN_W * GB_SCREEN_H - 1], 0x7FFF);

    TEST_ASSERT(load_cgb_rom(&gb, true));
    TEST_ASSERT(gb.cart.cgb);
    TEST_ASSERT(gb.cart.cgb_only);

    TEST_ASSERT(load_dmg_rom(&gb));
    TEST_ASSERT(!gb.cgb_mode);
    TEST_ASSERT(!gb_is_cgb(&gb));
    TEST_ASSERT(!gb.ppu.cgb);
    TEST_EQ_U8(gb.cpu.a, 0x01);
    TEST_EQ_U16(gb_framebuffer(&gb)[0], gb.ppu.dmg_colors[0]);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cgb_wram_banking(void)
{
    Gb gb;
    test_gb_init(&gb);
    TEST_ASSERT(load_cgb_rom(&gb, true));

    gb_write(&gb, 0xC000, 0x11);
    gb_write(&gb, 0xD000, 0x22);
    TEST_EQ_U8(gb_read(&gb, 0xD000), 0x22);
    TEST_EQ_U8(gb.mmu.wram[GB_WRAM_BANK_SIZE], 0x22);

    gb_write(&gb, 0xFF70, 0x02);
    TEST_EQ_U8(gb_read(&gb, 0xFF70), 0xFA);
    TEST_EQ_U8(gb.mmu.wram_bank, 2);
    gb_write(&gb, 0xD000, 0x33);
    TEST_EQ_U8(gb_read(&gb, 0xD000), 0x33);
    TEST_EQ_U8(gb.mmu.wram[2 * GB_WRAM_BANK_SIZE], 0x33);
    TEST_EQ_U8(gb_read(&gb, 0xC000), 0x11);

    gb_write(&gb, 0xFF70, 0x01);
    TEST_EQ_U8(gb_read(&gb, 0xD000), 0x22);

    gb_write(&gb, 0xFF70, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xFF70), 0xF8);
    TEST_EQ_U8(gb_read(&gb, 0xD000), 0x22);

    gb_write(&gb, 0xFF70, 0x02);
    TEST_EQ_U8(gb_read(&gb, 0xF000), 0x33);
    TEST_EQ_U8(gb_read(&gb, 0xE000), 0x11);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cgb_vram_banking(void)
{
    Gb gb;
    test_gb_init(&gb);
    TEST_ASSERT(load_cgb_rom(&gb, true));

    gb_write(&gb, 0x8000, 0xAA);
    TEST_EQ_U8(gb_read(&gb, 0x8000), 0xAA);
    gb_write(&gb, 0xFF4F, 0x01);
    TEST_EQ_U8(gb_read(&gb, 0xFF4F), 0xFF);
    TEST_EQ_U8(gb.ppu.vram_bank, 1);
    TEST_EQ_U8(gb_read(&gb, 0x8000), 0x00);
    gb_write(&gb, 0x8000, 0xBB);
    TEST_EQ_U8(gb.ppu.vram[0], 0xAA);
    TEST_EQ_U8(gb.ppu.vram[GB_VRAM_BANK_SIZE], 0xBB);

    gb_write(&gb, 0xFF4F, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0x8000), 0xAA);
    gb_write(&gb, 0xFF4F, 0x01);
    TEST_EQ_U8(gb_read(&gb, 0x8000), 0xBB);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cgb_palette_ram(void)
{
    Gb gb;
    test_gb_init(&gb);
    TEST_ASSERT(load_cgb_rom(&gb, true));

    gb_write(&gb, 0xFF68, 0x80);
    gb_write(&gb, 0xFF69, 0x1F);
    gb_write(&gb, 0xFF69, 0x03);
    TEST_EQ_U16(gb.ppu.bg_palette[0], 0x031F);
    TEST_EQ_U8(gb.ppu.bcps, 0x82);
    gb_write(&gb, 0xFF68, 0x01);
    TEST_EQ_U8(gb_read(&gb, 0xFF69), 0x03);
    TEST_EQ_U8(gb_read(&gb, 0xFF68), 0x01);

    gb_write(&gb, 0xFF6A, 0x80);
    gb_write(&gb, 0xFF6B, 0x0F);
    gb_write(&gb, 0xFF6B, 0x7C);
    TEST_EQ_U16(gb.ppu.obj_palette[0], 0x7C0F);
    TEST_EQ_U8(gb.ppu.ocps, 0x82);

    gb_write(&gb, 0xFF6C, 0x01);
    TEST_EQ_U8(gb_read(&gb, 0xFF6C), 0xFF);
    gb_write(&gb, 0xFF6C, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xFF6C), 0xFE);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cgb_background(void)
{
    Gb gb;
    test_gb_init(&gb);
    TEST_ASSERT(load_cgb_rom(&gb, true));

    memset(gb.ppu.vram, 0, sizeof(gb.ppu.vram));
    fill_tile_color(gb.ppu.vram, 0, 1);
    fill_tile_color(gb.ppu.vram + GB_VRAM_BANK_SIZE, 0, 2);
    for (unsigned i = 0; i < 20; i++) {
        gb.ppu.vram[0x1800 + i] = 0x00;
        gb.ppu.vram[GB_VRAM_BANK_SIZE + 0x1800 + i] = 0x02;
    }
    uint16_t color_one = gb_rgb555(0xF8, 0x00, 0x00);
    uint16_t color_two = gb_rgb555(0x00, 0xF8, 0x00);
    gb.ppu.bg_palette[2 * 4 + 1] = color_one;
    gb.ppu.bg_palette[2 * 4 + 2] = color_two;

    render_line(&gb, 0, 0x91);
    TEST_EQ_U16(gb.ppu.pixels[0], color_one);
    TEST_EQ_U16(gb.ppu.pixels[7], color_one);
    TEST_EQ_U16(gb.ppu.pixels[GB_SCREEN_W - 1], color_one);

    /* Bit 3 of the tile attributes selects the VRAM bank. */
    gb.ppu.vram[GB_VRAM_BANK_SIZE + 0x1800] = 0x0A;
    render_line(&gb, 0, 0x91);
    TEST_EQ_U16(gb.ppu.pixels[0], color_two);

    /* With the background disabled, CGB output is white. */
    render_line(&gb, 0, 0x90);
    TEST_EQ_U16(gb.ppu.pixels[0], 0x7FFF);
    TEST_EQ_U16(gb.ppu.pixels[GB_SCREEN_W - 1], 0x7FFF);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cgb_flips(void)
{
    Gb gb;
    test_gb_init(&gb);
    TEST_ASSERT(load_cgb_rom(&gb, true));

    memset(gb.ppu.vram, 0, sizeof(gb.ppu.vram));
    uint16_t color_one = gb_rgb555(0xF8, 0x00, 0x00);
    uint16_t color_zero = gb_rgb555(0x00, 0x00, 0xF8);
    gb.ppu.bg_palette[1] = color_one;
    gb.ppu.bg_palette[0] = color_zero;

    /* Only the leftmost pixel of tile 0 has color 1. */
    gb.ppu.vram[0] = 0x80;
    gb.ppu.vram[0x1800] = 0x00;
    gb.ppu.vram[GB_VRAM_BANK_SIZE + 0x1800] = 0x00;
    render_line(&gb, 0, 0x91);
    TEST_EQ_U16(gb.ppu.pixels[0], color_one);
    TEST_EQ_U16(gb.ppu.pixels[7], color_zero);

    gb.ppu.vram[GB_VRAM_BANK_SIZE + 0x1800] = 0x20;
    render_line(&gb, 0, 0x91);
    TEST_EQ_U16(gb.ppu.pixels[0], color_zero);
    TEST_EQ_U16(gb.ppu.pixels[7], color_one);

    gb.ppu.vram[GB_VRAM_BANK_SIZE + 0x1800] = 0x40;
    gb.ppu.vram[0] = 0x80;
    render_line(&gb, 0, 0x91);
    TEST_EQ_U16(gb.ppu.pixels[0], color_zero);
    gb.ppu.vram[7 * 2] = 0x80;
    render_line(&gb, 7, 0x91);
    TEST_EQ_U16(gb.ppu.pixels[7 * GB_SCREEN_W], color_one);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cgb_sprites(void)
{
    Gb gb;
    test_gb_init(&gb);
    TEST_ASSERT(load_cgb_rom(&gb, true));

    memset(gb.ppu.vram, 0, sizeof(gb.ppu.vram));
    uint16_t bg_color = gb_rgb555(0x00, 0x00, 0xF8);
    uint16_t obj_color = gb_rgb555(0xF8, 0x00, 0x00);
    gb.ppu.bg_palette[1] = bg_color;
    gb.ppu.obj_palette[3 * 4 + 1] = obj_color;

    /* Only the leftmost pixel of sprite tile 1 is opaque. */
    gb.ppu.vram[GB_VRAM_BANK_SIZE + 1 * 16] = 0x80;
    gb.ppu.oam[0] = 16;
    gb.ppu.oam[1] = 8;
    gb.ppu.oam[2] = 1;
    gb.ppu.oam[3] = 0x08 | 0x03;

    render_line(&gb, 0, 0x93);
    TEST_EQ_U16(gb.ppu.pixels[0], obj_color);
    TEST_EQ_U16(gb.ppu.pixels[7], 0x7FFF);

    /* Sprite X flip. */
    gb.ppu.oam[3] = 0x08 | 0x03 | 0x20;
    render_line(&gb, 0, 0x93);
    TEST_EQ_U16(gb.ppu.pixels[0], 0x7FFF);
    TEST_EQ_U16(gb.ppu.pixels[7], obj_color);

    /* An opaque background with priority hides the sprite. */
    fill_tile_color(gb.ppu.vram, 0, 1);
    gb.ppu.vram[0x1800] = 0x00;
    gb.ppu.oam[3] = 0x08 | 0x03 | 0x80;
    render_line(&gb, 0, 0x93);
    TEST_EQ_U16(gb.ppu.pixels[0], bg_color);

    gb.ppu.oam[3] = 0x08 | 0x03;
    render_line(&gb, 0, 0x93);
    TEST_EQ_U16(gb.ppu.pixels[0], obj_color);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cgb_double_speed(void)
{
    Gb gb;
    test_gb_init(&gb);
    TEST_ASSERT(load_cgb_rom(&gb, true));

    gb_write(&gb, 0xFF4D, 0x01);
    TEST_EQ_U8(gb_read(&gb, 0xFF4D), 0x7F);

    gb.mmu.wram[0] = 0x10;
    gb.mmu.wram[1] = 0x00;
    gb.mmu.wram[2] = 0x10;
    gb.mmu.wram[3] = 0x00;
    gb.cpu.pc = 0xC000;
    TEST_EQ_INT(cpu_step(&gb, &gb.cpu), 4);
    TEST_ASSERT(gb.double_speed);
    TEST_ASSERT(gb_double_speed(&gb));
    TEST_EQ_U8(gb_read(&gb, 0xFF4D), 0xFE);
    TEST_EQ_U16(gb.cpu.pc, 0xC002);

    uint64_t before = gb.cycles;
    gb_tick(&gb, 4);
    TEST_EQ_U16(gb.cycles - before, 2);
    gb_tick(&gb, 5);
    TEST_EQ_U16(gb.cycles - before, 4);
    gb_tick(&gb, 1);
    TEST_EQ_U16(gb.cycles - before, 5);

    gb_write(&gb, 0xFF4D, 0x01);
    gb.cpu.pc = 0xC002;
    TEST_EQ_INT(cpu_step(&gb, &gb.cpu), 4);
    TEST_ASSERT(!gb.double_speed);
    TEST_EQ_U8(gb_read(&gb, 0xFF4D), 0x7E);

    before = gb.cycles;
    gb_tick(&gb, 4);
    TEST_EQ_U16(gb.cycles - before, 4);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cgb_gdma(void)
{
    Gb gb;
    test_gb_init(&gb);
    TEST_ASSERT(load_cgb_rom(&gb, true));

    for (unsigned i = 0; i < 32; i++) {
        gb_write(&gb, (uint16_t)(0xC000u + i), (uint8_t)(0x40u + i));
    }
    gb_write(&gb, 0xFF51, 0xC0);
    gb_write(&gb, 0xFF52, 0x00);
    gb_write(&gb, 0xFF53, 0x00);
    gb_write(&gb, 0xFF54, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xFF51), 0xC0);
    TEST_EQ_U8(gb_read(&gb, 0xFF52), 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xFF53), 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xFF54), 0x00);

    gb_write(&gb, 0xFF55, 0x01);
    for (unsigned i = 0; i < 32; i++) {
        TEST_EQ_U8(gb.ppu.vram[i], (uint8_t)(0x40u + i));
    }
    TEST_EQ_U8(gb_read(&gb, 0xFF55), 0xFF);
    TEST_ASSERT(!gb.ppu.hdma_active);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cgb_hdma(void)
{
    Gb gb;
    test_gb_init(&gb);
    TEST_ASSERT(load_cgb_rom(&gb, true));

    for (unsigned i = 0; i < 32; i++) {
        gb_write(&gb, (uint16_t)(0xC000u + i), (uint8_t)(0x80u + i));
    }
    gb_write(&gb, 0xFF51, 0xC0);
    gb_write(&gb, 0xFF52, 0x00);
    gb_write(&gb, 0xFF53, 0x00);
    gb_write(&gb, 0xFF54, 0x00);
    gb_write(&gb, 0xFF4F, 0x01);
    gb_write(&gb, 0xFF55, 0x81);
    TEST_EQ_U8(gb_read(&gb, 0xFF55), 0x01);
    TEST_ASSERT(gb.ppu.hdma_active);
    TEST_ASSERT(gb.ppu.hdma_hblank);
    TEST_EQ_U8(gb.ppu.vram[0], 0x00);

    render_line(&gb, 0, 0x91);
    TEST_EQ_U8(gb.ppu.vram[0], 0x80);
    TEST_EQ_U8(gb.ppu.vram[15], 0x8F);
    TEST_EQ_U8(gb.ppu.vram[16], 0x00);
    TEST_EQ_U8(gb.ppu.vram[GB_VRAM_BANK_SIZE], 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xFF55), 0x00);

    ppu_tick(&gb, 456);
    TEST_EQ_U8(gb.ppu.vram[16], 0x90);
    TEST_EQ_U8(gb.ppu.vram[31], 0x9F);
    TEST_EQ_U8(gb_read(&gb, 0xFF55), 0xFF);
    TEST_ASSERT(!gb.ppu.hdma_active);

    /* Cancelling an HBlank transfer reports the remaining blocks. */
    gb_write(&gb, 0xFF55, 0x82);
    TEST_EQ_U8(gb_read(&gb, 0xFF55), 0x02);
    render_line(&gb, 0, 0x91);
    TEST_EQ_U8(gb_read(&gb, 0xFF55), 0x01);
    gb_write(&gb, 0xFF55, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xFF55), 0x81);
    TEST_ASSERT(!gb.ppu.hdma_active);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cgb_dmg_compatibility(void)
{
    Gb gb;
    test_gb_init(&gb);
    TEST_ASSERT(load_dmg_rom(&gb));

    TEST_ASSERT(!gb.cgb_mode);
    TEST_ASSERT(!gb.ppu.cgb);
    TEST_EQ_U8(gb_read(&gb, 0xFF4F), 0xFF);
    TEST_EQ_U8(gb_read(&gb, 0xFF70), 0xFF);
    TEST_EQ_U8(gb_read(&gb, 0xFF68), 0xFF);
    TEST_EQ_U8(gb_read(&gb, 0xFF55), 0xFF);

    gb_write(&gb, 0xFF70, 0x02);
    gb_write(&gb, 0xFF4F, 0x01);
    gb_write(&gb, 0xFF68, 0x80);
    gb_write(&gb, 0xFF69, 0x1F);
    gb_write(&gb, 0xFF55, 0x00);
    TEST_EQ_U8(gb.ppu.vram_bank, 0);
    TEST_EQ_U8(gb.mmu.wram_bank, 0);
    TEST_EQ_U16(gb.ppu.bg_palette[0], 0x7FFF);

    gb_write(&gb, 0xD000, 0x22);
    gb_write(&gb, 0xFF70, 0x02);
    gb_write(&gb, 0xD000, 0x33);
    TEST_EQ_U8(gb_read(&gb, 0xD000), 0x33);
    TEST_EQ_U8(gb.mmu.wram[GB_WRAM_BANK_SIZE], 0x33);

    const uint32_t tint[4] = {0xFFFFFFFFu, 0xFFFF0000u, 0xFF00FF00u,
                              0xFF0000FFu};
    gb_set_dmg_palette(&gb, tint);
    for (unsigned i = 0; i < 4; i++) {
        TEST_EQ_U16(gb.ppu.dmg_colors[i], gb_argb_to_rgb555(tint[i]));
    }

    memset(gb.ppu.vram, 0, sizeof(gb.ppu.vram));
    fill_tile_color(gb.ppu.vram, 0, 1);
    gb.ppu.vram[0x1800] = 0x00;
    gb.ppu.bgp = 0xE4;
    render_line(&gb, 0, 0x91);
    TEST_EQ_U16(gb.ppu.pixels[0], gb.ppu.dmg_colors[1]);
    TEST_EQ_U16(gb.ppu.pixels[0], gb_argb_to_rgb555(tint[1]));

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

void test_cgb_all(void)
{
    run_test("cgb/boot", test_cgb_boot);
    run_test("cgb/wram-banking", test_cgb_wram_banking);
    run_test("cgb/vram-banking", test_cgb_vram_banking);
    run_test("cgb/palette-ram", test_cgb_palette_ram);
    run_test("cgb/background", test_cgb_background);
    run_test("cgb/flips", test_cgb_flips);
    run_test("cgb/sprites", test_cgb_sprites);
    run_test("cgb/double-speed", test_cgb_double_speed);
    run_test("cgb/gdma", test_cgb_gdma);
    run_test("cgb/hdma", test_cgb_hdma);
    run_test("cgb/dmg-compatibility", test_cgb_dmg_compatibility);
}
