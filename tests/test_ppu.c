#include "test.h"

#include "classicboy/gb.h"
#include "test_util.h"

static void ppu_start(Gb *gb)
{
    ppu_reset(&gb->ppu);
    gb->mmu.io[0x0F] = 0;
    gb->mmu.ie = 0;
}

static void test_ppu_timing(void)
{
    Gb gb;
    test_gb_init(&gb);
    ppu_start(&gb);

    ppu_tick(&gb, 80);
    TEST_EQ_U8(gb.ppu.mode, 3);
    ppu_tick(&gb, 172);
    TEST_EQ_U8(gb.ppu.mode, 0);
    TEST_EQ_U8(gb.ppu.ly, 0);
    ppu_tick(&gb, 204);
    TEST_EQ_U8(gb.ppu.ly, 1);
    TEST_EQ_U8(gb.ppu.mode, 2);

    ppu_start(&gb);
    ppu_tick(&gb, 65664);
    TEST_EQ_U8(gb.ppu.ly, 144);
    TEST_EQ_U8(gb.ppu.mode, 1);
    TEST_ASSERT((gb.mmu.io[0x0F] & 0x01) != 0);

    ppu_start(&gb);
    gb.ppu.lyc = 1;
    gb.ppu.stat = 0x40;
    ppu_tick(&gb, 456);
    TEST_EQ_U8(gb.ppu.ly, 1);
    TEST_ASSERT((gb.mmu.io[0x0F] & 0x02) != 0);

    ppu_start(&gb);
    gb.ppu.stat = 0x08;
    ppu_tick(&gb, 252);
    TEST_EQ_U8(gb.ppu.mode, 0);
    TEST_ASSERT((gb.mmu.io[0x0F] & 0x02) != 0);

    gb_destroy(&gb);
}

static void test_ppu_background(void)
{
    Gb gb;
    test_gb_init(&gb);
    ppu_start(&gb);

    gb.ppu.lcdc = 0;
    memset(gb.ppu.vram, 0, sizeof(gb.ppu.vram));
    for (int row = 0; row < 8; row++) {
        gb.ppu.vram[row * 2] = 0xFF;
        gb.ppu.vram[row * 2 + 1] = 0x00;
    }
    gb.ppu.vram[0x1800] = 0x00;
    gb.ppu.bgp = 0xE4;
    gb.ppu.lcdc = 0x91;
    gb.ppu.mode = 2;
    gb.ppu.dot = 0;
    gb.ppu.ly = 0;

    ppu_tick(&gb, 252);
    TEST_EQ_U8(gb.ppu.ly, 0);
    TEST_EQ_U16(gb.ppu.pixels[0], gb.ppu.dmg_colors[1]);
    TEST_EQ_U16(gb.ppu.pixels[8], gb.ppu.dmg_colors[1]);
    TEST_EQ_U16(gb.ppu.pixels[159], gb.ppu.dmg_colors[1]);

    gb.ppu.pixels[0] = 3;
    ppu_write_reg(&gb, &gb.ppu, 0xFF40, 0x00);
    TEST_EQ_U16(gb.ppu.pixels[0], gb.ppu.dmg_colors[0]);

    gb_destroy(&gb);
}

static void test_ppu_window(void)
{
    Gb gb;
    test_gb_init(&gb);
    ppu_start(&gb);

    gb.ppu.lcdc = 0;
    memset(gb.ppu.vram, 0, sizeof(gb.ppu.vram));
    for (int row = 0; row < 8; row++) {
        gb.ppu.vram[0x0010 + row * 2] = 0xFF;
        gb.ppu.vram[0x0010 + row * 2 + 1] = 0x00;
    }
    for (int i = 0; i < 20; i++) {
        gb.ppu.vram[0x1C00 + i] = 0x01;
    }
    gb.ppu.bgp = 0xE4;
    gb.ppu.wy = 0;
    gb.ppu.wx = 7;
    gb.ppu.lcdc = 0xF1;
    gb.ppu.mode = 2;
    gb.ppu.dot = 0;
    gb.ppu.ly = 0;
    gb.ppu.window_line = 0;

    ppu_tick(&gb, 252);
    TEST_EQ_U16(gb.ppu.pixels[0], gb.ppu.dmg_colors[1]);
    TEST_EQ_U16(gb.ppu.pixels[159], gb.ppu.dmg_colors[1]);
    TEST_EQ_U8(gb.ppu.window_line, 1);

    gb_destroy(&gb);
}

static void test_ppu_sprites(void)
{
    Gb gb;
    test_gb_init(&gb);
    ppu_start(&gb);

    gb.ppu.lcdc = 0;
    memset(gb.ppu.vram, 0, sizeof(gb.ppu.vram));
    for (int row = 0; row < 8; row++) {
        gb.ppu.vram[0x0010 + row * 2] = 0xFF;
        gb.ppu.vram[0x0010 + row * 2 + 1] = 0x00;
    }
    gb.ppu.oam[0] = 16;
    gb.ppu.oam[1] = 8;
    gb.ppu.oam[2] = 1;
    gb.ppu.oam[3] = 0;
    gb.ppu.bgp = 0xE4;
    gb.ppu.obp0 = 0xE4;
    gb.ppu.lcdc = 0x93;
    gb.ppu.mode = 2;
    gb.ppu.dot = 0;
    gb.ppu.ly = 0;

    ppu_tick(&gb, 252);
    TEST_EQ_U16(gb.ppu.pixels[0], gb.ppu.dmg_colors[1]);

    for (int row = 0; row < 8; row++) {
        gb.ppu.vram[row * 2] = 0xFF;
        gb.ppu.vram[row * 2 + 1] = 0x00;
    }
    for (int row = 0; row < 8; row++) {
        gb.ppu.vram[0x0010 + row * 2] = 0x00;
        gb.ppu.vram[0x0010 + row * 2 + 1] = 0xFF;
    }
    gb.ppu.oam[3] = 0x80;
    gb.ppu.mode = 2;
    gb.ppu.dot = 0;
    gb.ppu.ly = 0;
    gb.ppu.stat_line = false;
    ppu_tick(&gb, 252);
    TEST_EQ_U16(gb.ppu.pixels[0], gb.ppu.dmg_colors[1]);

    gb.ppu.oam[3] = 0x00;
    gb.ppu.mode = 2;
    gb.ppu.dot = 0;
    gb.ppu.ly = 0;
    gb.ppu.stat_line = false;
    ppu_tick(&gb, 252);
    TEST_EQ_U16(gb.ppu.pixels[0], gb.ppu.dmg_colors[2]);

    gb_destroy(&gb);
}

void test_ppu_all(void)
{
    run_test("ppu/timing", test_ppu_timing);
    run_test("ppu/background", test_ppu_background);
    run_test("ppu/window", test_ppu_window);
    run_test("ppu/sprites", test_ppu_sprites);
}
