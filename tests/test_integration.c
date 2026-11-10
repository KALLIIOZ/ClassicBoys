#include "test.h"

#include "classicboy/gb.h"
#include "test_util.h"

static bool load_program(Gb *gb, const uint8_t *code, size_t length,
                         uint8_t type)
{
    uint8_t *rom = test_rom_make(0x8000, type, 0x00, 0x00);
    if (!rom) {
        return false;
    }
    test_rom_set_code(rom, code, length);
    GbStatus status = gb_load_rom_data(gb, rom, 0x8000, NULL);
    gb_free(rom);
    return status == GB_OK;
}

static void test_integration_serial(void)
{
    Gb gb;
    test_gb_init(&gb);

    const uint8_t code[] = {
        0x3E, 0x42,       /* LD A,0x42 */
        0xE0, 0x01,       /* LDH (0x01),A  -> SB */
        0x3E, 0x81,       /* LD A,0x81 */
        0xE0, 0x02,       /* LDH (0x02),A  -> SC start */
        0x18, 0xFE        /* JR -2 */
    };
    TEST_ASSERT(load_program(&gb, code, sizeof(code), 0x00));

    gb_run_frame(&gb);
    TEST_EQ_U8(gb.serial_last, 0x42);
    TEST_ASSERT(!gb.mmu.serial_active);
    TEST_ASSERT((gb.mmu.io[0x0F] & 0x08) != 0);
    TEST_ASSERT((gb_read(&gb, 0xFF02) & 0x80) == 0);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_integration_frame(void)
{
    Gb gb;
    test_gb_init(&gb);

    const uint8_t code[] = {0x18, 0xFE};
    TEST_ASSERT(load_program(&gb, code, sizeof(code), 0x00));

    /* A solid tile 0 rendered with a 1 -> shade 1 palette. */
    for (int row = 0; row < 8; row++) {
        gb.ppu.vram[row * 2] = 0xFF;
        gb.ppu.vram[row * 2 + 1] = 0x00;
    }
    gb.ppu.bgp = 0xE4;

    uint64_t before = gb.cycles;
    gb_run_frame(&gb);
    TEST_ASSERT(gb.frame_ready);
    TEST_ASSERT(gb.ppu.frame_ready);
    TEST_ASSERT(gb.cycles - before >= GB_FRAME_CYCLES);
    TEST_ASSERT(gb.cycles - before < GB_FRAME_CYCLES + 24);
    TEST_EQ_U8(gb.ppu.ly, 0);
    TEST_ASSERT(gb.ppu.dot < 24);

    for (int y = 0; y < GB_SCREEN_H; y++) {
        TEST_EQ_U16(gb.ppu.pixels[y * GB_SCREEN_W], gb.ppu.dmg_colors[1]);
        TEST_EQ_U16(gb.ppu.pixels[y * GB_SCREEN_W + GB_SCREEN_W - 1],
                    gb.ppu.dmg_colors[1]);
    }
    TEST_EQ_U16(gb.ppu.pixels[0], gb.ppu.dmg_colors[1]);
    TEST_EQ_U16(gb.ppu.pixels[GB_SCREEN_W * GB_SCREEN_H - 1],
                gb.ppu.dmg_colors[1]);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_integration_timer_irq(void)
{
    Gb gb;
    test_gb_init(&gb);

    const uint8_t code[] = {
        0xF3,             /* DI */
        0x31, 0x00, 0xD0, /* LD SP,0xD000 */
        0x3E, 0x05,       /* LD A,0x05 (TAC: enable, 16 cycles) */
        0xE0, 0x07,       /* LDH (0x07),A */
        0x3E, 0x00,       /* LD A,0x00 */
        0xE0, 0x05,       /* LDH (0x05),A */
        0x3E, 0x04,       /* LD A,0x04 (timer) */
        0xE0, 0xFF,       /* LDH (0xFF),A -> IE */
        0xFB,             /* EI */
        0x76,             /* HALT */
        0x18, 0xFD        /* JR -3 -> HALT */
    };
    TEST_ASSERT(load_program(&gb, code, sizeof(code), 0x00));

    const uint8_t isr[] = {
        0x21, 0x00, 0xC1, /* LD HL,0xC100 */
        0x34,             /* INC (HL) */
        0xD9              /* RETI */
    };
    memcpy(gb.cart.rom + 0x50, isr, sizeof(isr));

    gb_run_frame(&gb);
    TEST_ASSERT(gb.mmu.wram[0x100] >= 10);
    TEST_EQ_U8(gb.mmu.ie, 0x04);
    TEST_ASSERT(gb.cpu.instructions > 10);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_integration_audio(void)
{
    Gb gb;
    test_gb_init(&gb);
    gb_set_sample_rate(&gb, 48000);

    const uint8_t code[] = {
        0x3E, 0x80, 0xE0, 0x26, /* NR52 on */
        0x3E, 0x77, 0xE0, 0x24, /* NR50 max volume */
        0x3E, 0x11, 0xE0, 0x25, /* NR51 ch1 both sides */
        0x3E, 0x80, 0xE0, 0x11, /* NR11 duty 50% */
        0x3E, 0xF0, 0xE0, 0x12, /* NR12 volume 15 */
        0x3E, 0x00, 0xE0, 0x13, /* NR13 freq low */
        0x3E, 0x87, 0xE0, 0x14, /* NR14 trigger */
        0x18, 0xFE
    };
    TEST_ASSERT(load_program(&gb, code, sizeof(code), 0x00));

    int16_t samples[GB_APU_MAX_SAMPLES * 2];
    size_t total = 0;
    size_t nonzero = 0;
    for (int frame = 0; frame < 60; frame++) {
        gb_run_frame(&gb);
        size_t got = gb_audio_read(&gb, samples, GB_APU_MAX_SAMPLES);
        total += got;
        for (size_t i = 0; i < got * 2; i++) {
            if (samples[i] != 0) {
                nonzero++;
            }
        }
    }
    TEST_ASSERT(total > 47000);
    TEST_ASSERT(total < 49500);
    TEST_ASSERT(nonzero > total / 2);
    TEST_ASSERT(gb.apu.sample_count < GB_APU_MAX_SAMPLES);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_integration_rom_replace(void)
{
    Gb gb;
    test_gb_init(&gb);

    const uint8_t code_a[] = {0x3E, 0x11, 0x18, 0xFE};
    TEST_ASSERT(load_program(&gb, code_a, sizeof(code_a), 0x00));
    gb_run_frame(&gb);
    uint64_t old_cycles = gb.cycles;
    TEST_ASSERT(old_cycles > 0);

    uint8_t *rom_b = test_rom_make(0x10000, 0x01, 0x03, 0x00);
    TEST_ASSERT(rom_b != NULL);
    memcpy(rom_b + 0x134, "SECOND GAME", 11);
    rom_b[0x134 + 11] = 0;
    rom_b[0x100] = 0x3E;
    rom_b[0x101] = 0x22;
    rom_b[0x102] = 0x18;
    rom_b[0x103] = 0xFE;
    TEST_EQ_INT(gb_load_rom_data(&gb, rom_b, 0x10000, NULL), GB_OK);
    gb_free(rom_b);

    TEST_EQ_STR(gb_title(&gb), "SECOND GAME");
    TEST_EQ_INT(gb.cart.mbc, GB_MBC1);
    TEST_EQ_U16(gb.cpu.pc, 0x0100);
    TEST_EQ_U16(gb.cycles, 0);
    TEST_EQ_U8(gb.cpu.a, 0x01);
    TEST_EQ_U8(gb.mmu.wram[0x0000], 0x00);

    gb_run_frame(&gb);
    TEST_EQ_U8(gb.cpu.a, 0x22);

    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

void test_integration_all(void)
{
    run_test("integration/serial", test_integration_serial);
    run_test("integration/frame", test_integration_frame);
    run_test("integration/timer-irq", test_integration_timer_irq);
    run_test("integration/audio", test_integration_audio);
    run_test("integration/rom-replace", test_integration_rom_replace);
}
