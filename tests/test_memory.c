#include "test.h"

#include "classicboy/gb.h"
#include "test_util.h"

static void test_memory_map(void)
{
    Gb gb;
    test_gb_init(&gb);

    gb_write(&gb, 0xC000, 0x12);
    TEST_EQ_U8(gb_read(&gb, 0xC000), 0x12);
    TEST_EQ_U8(gb_read(&gb, 0xE000), 0x12);
    gb_write(&gb, 0xE123, 0x34);
    TEST_EQ_U8(gb_read(&gb, 0xC123), 0x34);

    gb_write(&gb, 0xFF80, 0x56);
    TEST_EQ_U8(gb_read(&gb, 0xFF80), 0x56);

    gb_write(&gb, 0xFFFF, 0xFF);
    TEST_EQ_U8(gb_read(&gb, 0xFFFF), 0x1F);

    gb_write(&gb, 0xFF0F, 0xFF);
    TEST_EQ_U8(gb_read(&gb, 0xFF0F), 0xFF);

    gb_write(&gb, 0xFEA0, 0x12);
    TEST_EQ_U8(gb_read(&gb, 0xFEA0), 0xFF);

    gb_destroy(&gb);
}

static void test_memory_dma(void)
{
    Gb gb;
    test_gb_init(&gb);
    for (int i = 0; i < 160; i++) {
        gb.mmu.wram[i] = (uint8_t)i;
    }
    gb_write(&gb, 0xFF46, 0xC0);
    TEST_ASSERT(gb.mmu.dma_active);
    TEST_EQ_U8(gb_read(&gb, 0xC000), 0xFF);
    gb_write(&gb, 0xC000, 0xAA);
    TEST_EQ_U8(gb.mmu.wram[0], 0x00);

    mmu_tick(&gb, 640);
    TEST_ASSERT(!gb.mmu.dma_active);
    for (int i = 0; i < 160; i++) {
        TEST_EQ_U8(gb.ppu.oam[i], (uint8_t)i);
    }
    TEST_EQ_U8(gb.mmu.wram[0], 0x00);

    gb_destroy(&gb);
}

static void test_memory_vram_oam(void)
{
    Gb gb;
    test_gb_init(&gb);

    gb.ppu.lcdc = 0x91;
    gb.ppu.mode = 3;
    gb_write(&gb, 0x8000, 0x12);
    TEST_EQ_U8(gb_read(&gb, 0x8000), 0xFF);

    gb.ppu.mode = 0;
    gb_write(&gb, 0x8000, 0x34);
    TEST_EQ_U8(gb_read(&gb, 0x8000), 0x34);

    gb.ppu.mode = 2;
    gb_write(&gb, 0xFE00, 0x56);
    TEST_EQ_U8(gb_read(&gb, 0xFE00), 0xFF);

    gb.ppu.mode = 0;
    gb_write(&gb, 0xFE00, 0x78);
    TEST_EQ_U8(gb_read(&gb, 0xFE00), 0x78);

    gb.ppu.lcdc = 0;
    gb.ppu.mode = 3;
    gb_write(&gb, 0x8000, 0x9A);
    TEST_EQ_U8(gb_read(&gb, 0x8000), 0x9A);

    gb_destroy(&gb);
}

static void test_timer(void)
{
    Gb gb;
    test_gb_init(&gb);
    timer_reset(&gb.timer);
    gb.mmu.io[0x0F] = 0;

    TEST_EQ_U8(gb_read(&gb, 0xFF04), 0xAB);
    timer_tick(&gb, 256);
    TEST_EQ_U8(gb_read(&gb, 0xFF04), 0xAC);
    gb_write(&gb, 0xFF04, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xFF04), 0x00);

    timer_reset(&gb.timer);
    gb.timer.tima = 0;
    gb.timer.tma = 0;
    gb_write(&gb, 0xFF07, 0x05);
    timer_tick(&gb, 16);
    TEST_EQ_U8(gb.timer.tima, 1);
    timer_tick(&gb, 8);
    TEST_EQ_U8(gb.timer.tima, 1);
    timer_tick(&gb, 8);
    TEST_EQ_U8(gb.timer.tima, 2);

    gb.timer.tima = 0xFF;
    gb.timer.tma = 0x42;
    gb.timer.div = 0xAB00;
    gb.timer.reload_delay = 0;
    gb.mmu.io[0x0F] = 0;
    timer_tick(&gb, 16);
    TEST_ASSERT((gb.mmu.io[0x0F] & 0x04) != 0);
    TEST_EQ_U8(gb.timer.tima, 0x00);
    timer_tick(&gb, 4);
    TEST_EQ_U8(gb.timer.tima, 0x42);

    gb.timer.div = 0xAB08;
    gb.timer.tima = 0;
    gb.timer.tac = 0x05;
    gb.timer.reload_delay = 0;
    gb_write(&gb, 0xFF04, 0x00);
    TEST_EQ_U8(gb.timer.tima, 1);

    gb.timer.div = 0xAB08;
    gb.timer.tima = 0;
    gb.timer.tac = 0x05;
    gb.timer.reload_delay = 0;
    gb_write(&gb, 0xFF07, 0x00);
    TEST_EQ_U8(gb.timer.tima, 1);

    gb.timer.tima = 0xFF;
    gb.timer.tma = 0x42;
    gb.timer.tac = 0x05;
    gb.timer.div = 0;
    gb.timer.reload_delay = 0;
    gb.mmu.io[0x0F] = 0;
    timer_tick(&gb, 16);
    timer_tick(&gb, 2);
    gb_write(&gb, 0xFF06, 0x99);
    timer_tick(&gb, 2);
    TEST_EQ_U8(gb.timer.tima, 0x99);

    gb_destroy(&gb);
}

static void test_joypad(void)
{
    Gb gb;
    test_gb_init(&gb);
    joypad_reset(&gb.joypad);
    gb.mmu.io[0x0F] = 0;

    gb_write(&gb, 0xFF00, 0x20);
    TEST_EQ_U8(gb_read(&gb, 0xFF00), 0xEF);
    gb_set_button(&gb, GB_BTN_RIGHT, true);
    TEST_EQ_U8(gb_read(&gb, 0xFF00), 0xEE);
    TEST_ASSERT((gb.mmu.io[0x0F] & 0x10) != 0);
    gb_set_button(&gb, GB_BTN_RIGHT, false);
    TEST_EQ_U8(gb_read(&gb, 0xFF00), 0xEF);

    gb.mmu.io[0x0F] = 0;
    gb_set_button(&gb, GB_BTN_A, true);
    TEST_EQ_U8(gb_read(&gb, 0xFF00), 0xEF);
    TEST_EQ_U8(gb.mmu.io[0x0F] & 0x10, 0x00);

    gb_write(&gb, 0xFF00, 0x10);
    TEST_EQ_U8(gb_read(&gb, 0xFF00), 0xDE);
    TEST_ASSERT((gb.mmu.io[0x0F] & 0x10) != 0);

    gb_write(&gb, 0xFF00, 0x00);
    gb_release_buttons(&gb);
    gb_set_button(&gb, GB_BTN_UP, true);
    gb_set_button(&gb, GB_BTN_B, true);
    TEST_EQ_U8(gb_read(&gb, 0xFF00), 0xC9);

    gb_release_buttons(&gb);
    TEST_EQ_U8(gb_read(&gb, 0xFF00), 0xCF);

    gb_destroy(&gb);
}

void test_memory_all(void)
{
    run_test("memory/map", test_memory_map);
    run_test("memory/dma", test_memory_dma);
    run_test("memory/vram-oam", test_memory_vram_oam);
    run_test("timer", test_timer);
    run_test("joypad", test_joypad);
}
