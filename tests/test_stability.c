#include "test.h"

#include "classicboy/gb.h"
#include "test_util.h"

static uint64_t g_rng_state = 0x123456789ABCDEF0ull;

static uint32_t rng_next(void)
{
    uint64_t x = g_rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    g_rng_state = x;
    return (uint32_t)(x >> 16);
}

static void check_alloc_zero(const char *where)
{
    size_t blocks = 0;
    size_t bytes = 0;
    gb_alloc_stats(&blocks, &bytes, NULL);
    TEST_ASSERT_MSG(blocks == 0 && bytes == 0, where);
}

static void test_stability_allocator(void)
{
    enum { SLOTS = 256, OPS = 20000 };
    void *slots[SLOTS] = {0};

    for (int op = 0; op < OPS; op++) {
        unsigned index = rng_next() % SLOTS;
        if (slots[index]) {
            if ((rng_next() & 3u) == 0) {
                size_t new_size = (size_t)(rng_next() % 8192u) + 1u;
                void *fresh = gb_realloc(slots[index], new_size);
                if (fresh) {
                    slots[index] = fresh;
                    memset(fresh, 0xA5, new_size);
                }
            } else {
                gb_free(slots[index]);
                slots[index] = NULL;
            }
        } else {
            size_t size = (size_t)(rng_next() % 4096u) + 1u;
            slots[index] = gb_alloc(size);
            TEST_ASSERT(slots[index] != NULL);
            if (slots[index]) {
                memset(slots[index], 0x5A, size);
            }
        }
        if ((op % 2048) == 0) {
            TEST_ASSERT(gb_alloc_check() == 0);
        }
    }
    for (int i = 0; i < SLOTS; i++) {
        gb_free(slots[i]);
    }
    TEST_ASSERT(gb_alloc_check() == 0);
    check_alloc_zero("allocator stress leaked blocks");

    char *copy = gb_strdup("ClassicBoy");
    TEST_ASSERT(copy != NULL);
    TEST_EQ_STR(copy, "ClassicBoy");
    gb_free(copy);
    TEST_ASSERT(gb_alloc_check() == 0);
    check_alloc_zero("gb_strdup leaked");
}

static void test_stability_init_destroy(void)
{
    for (int i = 0; i < 300; i++) {
        Gb gb;
        gb_init(&gb);
        gb.cpu.a = (uint8_t)i;
        gb_tick(&gb, 456);
        gb_destroy(&gb);
        if ((i % 50) == 0) {
            TEST_ASSERT(gb_alloc_check() == 0);
        }
    }
    TEST_ASSERT(gb_alloc_check() == 0);
    check_alloc_zero("gb_init/gb_destroy leaked");

    for (int i = 0; i < 200; i++) {
        Gb gb;
        test_gb_init(&gb);
        uint8_t *rom = test_rom_make(0x8000, 0x00, 0x00, 0x00);
        TEST_ASSERT(rom != NULL);
        TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x8000, NULL), GB_OK);
        gb_free(rom);
        gb_run_frame(&gb);
        gb_destroy(&gb);
        if ((i % 25) == 0) {
            TEST_ASSERT(gb_alloc_check() == 0);
        }
    }
    TEST_ASSERT(gb_alloc_check() == 0);
    check_alloc_zero("ROM load/destroy leaked");
}

static void test_stability_rom_fuzz(void)
{
    Gb gb;
    test_gb_init(&gb);

    uint8_t *base = test_rom_make(0x8000, 0x00, 0x00, 0x00);
    TEST_ASSERT(base != NULL);
    TEST_EQ_INT(gb_load_rom_data(&gb, base, 0x8000, NULL), GB_OK);
    gb_free(base);

    for (int i = 0; i < 400; i++) {
        size_t size = (size_t)(rng_next() % 24000u);
        uint8_t *data = (uint8_t *)gb_alloc(size ? size : 1);
        TEST_ASSERT(data != NULL);
        for (size_t j = 0; j < size; j++) {
            data[j] = (uint8_t)rng_next();
        }
        uint8_t *before = gb.cart.rom;
        GbStatus status = gb_load_rom_data(&gb, data, size, NULL);
        if (status == GB_OK) {
            TEST_ASSERT(gb.cart.loaded);
            TEST_ASSERT(gb.cart.rom != NULL);
            TEST_ASSERT(gb.cart.rom_banks >= 2);
            TEST_ASSERT(gb.cart.title[16] == '\0');
        } else {
            TEST_ASSERT(gb.cart.rom == before);
            TEST_ASSERT(gb.cart.loaded);
        }
        gb_free(data);
        if ((i % 20) == 0) {
            TEST_ASSERT(gb_alloc_check() == 0);
        }
    }

    gb_destroy(&gb);
    TEST_ASSERT(gb_alloc_check() == 0);
    check_alloc_zero("ROM fuzz leaked");
}

static void test_stability_cpu_fuzz(void)
{
    for (int seed = 0; seed < 48; seed++) {
        Gb gb;
        test_gb_init(&gb);

        uint8_t *rom = test_rom_make(0x8000, 0x00, 0x00, 0x00);
        TEST_ASSERT(rom != NULL);
        TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x8000, NULL), GB_OK);
        gb_free(rom);

        for (size_t i = 0; i < sizeof(gb.mmu.wram); i++) {
            gb.mmu.wram[i] = (uint8_t)rng_next();
        }
        gb.cpu.pc = 0xC000;
        gb.cpu.sp = 0xD000;
        gb.cpu.a = (uint8_t)rng_next();
        gb.cpu.f = (uint8_t)(rng_next() & 0xF0u);

        for (int step = 0; step < 4000; step++) {
            unsigned cycles = cpu_step(&gb, &gb.cpu);
            TEST_ASSERT(cycles > 0);
            TEST_ASSERT(cycles <= 24);
            gb_tick(&gb, cycles);
        }
        TEST_ASSERT(gb_alloc_check() == 0);
        gb_destroy(&gb);
    }
    check_alloc_zero("CPU fuzz leaked");
}

static void test_stability_long_run(void)
{
    Gb gb;
    test_gb_init(&gb);
    gb_set_sample_rate(&gb, 48000);

    const uint8_t code[] = {0x18, 0xFE};
    uint8_t *rom = test_rom_make(0x8000, 0x00, 0x00, 0x00);
    TEST_ASSERT(rom != NULL);
    test_rom_set_code(rom, code, sizeof(code));
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x8000, NULL), GB_OK);
    gb_free(rom);

    int16_t samples[GB_APU_MAX_SAMPLES * 2];
    size_t total_samples = 0;
    uint64_t last_cycles = 0;
    for (int frame = 0; frame < 600; frame++) {
        if ((frame % 2) == 0) {
            gb_set_button(&gb, GB_BTN_A, true);
            gb_set_button(&gb, GB_BTN_RIGHT, true);
        } else {
            gb_release_buttons(&gb);
        }
        gb_run_frame(&gb);
        TEST_ASSERT(gb.frame_ready);
        TEST_ASSERT(gb.cycles > last_cycles);
        last_cycles = gb.cycles;
        total_samples += gb_audio_read(&gb, samples, GB_APU_MAX_SAMPLES);
        if ((frame % 100) == 0) {
            TEST_ASSERT(gb_alloc_check() == 0);
        }
    }

    TEST_ASSERT(gb.cycles >= (uint64_t)GB_FRAME_CYCLES * 600u);
    TEST_ASSERT(gb.cycles < (uint64_t)GB_FRAME_CYCLES * 600u + 600u * 24u);
    TEST_ASSERT(total_samples > 470000);
    TEST_ASSERT(total_samples < 495000);
    TEST_EQ_U8(gb.ppu.ly, 0);

    gb_destroy(&gb);
    TEST_ASSERT(gb_alloc_check() == 0);
    check_alloc_zero("long run leaked");
}

void test_stability_all(void)
{
    run_test("stability/allocator", test_stability_allocator);
    run_test("stability/init-destroy", test_stability_init_destroy);
    run_test("stability/rom-fuzz", test_stability_rom_fuzz);
    run_test("stability/cpu-fuzz", test_stability_cpu_fuzz);
    run_test("stability/long-run", test_stability_long_run);
}
