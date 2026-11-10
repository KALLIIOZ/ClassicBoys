#include "test.h"

#include "classicboy/apu.h"
#include "classicboy/gb.h"
#include "test_util.h"

static void test_apu_registers(void)
{
    Gb gb;
    test_gb_init(&gb);
    apu_reset(&gb.apu);

    TEST_EQ_U8(apu_read_reg(&gb.apu, 0xFF26), 0xF0);
    apu_write_reg(&gb.apu, 0xFF10, 0xFF);
    TEST_EQ_U8(apu_read_reg(&gb.apu, 0xFF10), 0xFF);
    apu_write_reg(&gb.apu, 0xFF13, 0x34);
    TEST_EQ_U8(apu_read_reg(&gb.apu, 0xFF13), 0xFF);
    apu_write_reg(&gb.apu, 0xFF14, 0x80);
    TEST_EQ_U8(apu_read_reg(&gb.apu, 0xFF14), 0xBF);

    apu_write_reg(&gb.apu, 0xFF30, 0xAB);
    TEST_EQ_U8(apu_read_reg(&gb.apu, 0xFF30), 0xAB);
    apu_write_reg(&gb.apu, 0xFF26, 0x00);
    TEST_EQ_U8(apu_read_reg(&gb.apu, 0xFF26), 0x70);
    TEST_EQ_U8(apu_read_reg(&gb.apu, 0xFF10), 0x00);
    TEST_EQ_U8(apu_read_reg(&gb.apu, 0xFF30), 0xAB);
    apu_write_reg(&gb.apu, 0xFF26, 0x80);
    TEST_EQ_U8(apu_read_reg(&gb.apu, 0xFF26), 0xF0);
    TEST_EQ_U8(apu_read_reg(&gb.apu, 0xFF30), 0xAB);

    gb_destroy(&gb);
}

static void test_apu_sound(void)
{
    Gb gb;
    test_gb_init(&gb);
    apu_reset(&gb.apu);
    apu_set_sample_rate(&gb.apu, 48000);

    apu_write_reg(&gb.apu, 0xFF26, 0x80);
    apu_write_reg(&gb.apu, 0xFF24, 0x77);
    apu_write_reg(&gb.apu, 0xFF25, 0x11);
    apu_write_reg(&gb.apu, 0xFF11, 0x80);
    apu_write_reg(&gb.apu, 0xFF12, 0xF0);
    apu_write_reg(&gb.apu, 0xFF13, 0x00);
    apu_write_reg(&gb.apu, 0xFF14, 0x87);
    TEST_ASSERT((apu_read_reg(&gb.apu, 0xFF26) & 0x01) != 0);

    apu_tick(&gb.apu, 70224);
    TEST_ASSERT(gb.apu.sample_count > 700);
    TEST_ASSERT(gb.apu.sample_count < 1000);

    int16_t samples[GB_APU_MAX_SAMPLES * 2];
    size_t frames = apu_read_samples(&gb.apu, samples, GB_APU_MAX_SAMPLES);
    TEST_ASSERT(frames > 700);
    bool nonzero = false;
    for (size_t i = 0; i < frames * 2; i++) {
        if (samples[i] != 0) {
            nonzero = true;
            break;
        }
    }
    TEST_ASSERT(nonzero);
    TEST_EQ_INT(gb.apu.sample_count, 0);

    gb_destroy(&gb);
}

static void test_apu_length_envelope(void)
{
    Gb gb;
    test_gb_init(&gb);
    apu_reset(&gb.apu);
    apu_set_sample_rate(&gb.apu, 48000);

    apu_write_reg(&gb.apu, 0xFF26, 0x80);
    apu_write_reg(&gb.apu, 0xFF11, 0xBF); /* length = 1, 50% duty */
    apu_write_reg(&gb.apu, 0xFF12, 0xF0);
    apu_write_reg(&gb.apu, 0xFF13, 0x00);
    apu_write_reg(&gb.apu, 0xFF14, 0xC7); /* trigger + length enable */
    TEST_ASSERT((apu_read_reg(&gb.apu, 0xFF26) & 0x01) != 0);
    apu_tick(&gb.apu, 8192);
    TEST_ASSERT((apu_read_reg(&gb.apu, 0xFF26) & 0x01) != 0);
    apu_tick(&gb.apu, 8192);
    TEST_ASSERT((apu_read_reg(&gb.apu, 0xFF26) & 0x01) == 0);

    apu_reset(&gb.apu);
    apu_write_reg(&gb.apu, 0xFF26, 0x80);
    apu_write_reg(&gb.apu, 0xFF11, 0x80);
    apu_write_reg(&gb.apu, 0xFF12, 0xF1); /* volume 15, envelope period 1 */
    apu_write_reg(&gb.apu, 0xFF13, 0x00);
    apu_write_reg(&gb.apu, 0xFF14, 0x87);
    TEST_EQ_U8(gb.apu.ch1.volume, 15);
    apu_tick(&gb.apu, 57344);
    TEST_EQ_U8(gb.apu.ch1.volume, 14);

    gb_destroy(&gb);
}

static void test_apu_wave_noise(void)
{
    Gb gb;
    test_gb_init(&gb);
    apu_reset(&gb.apu);
    apu_set_sample_rate(&gb.apu, 48000);

    apu_write_reg(&gb.apu, 0xFF26, 0x80);
    apu_write_reg(&gb.apu, 0xFF24, 0x77);
    apu_write_reg(&gb.apu, 0xFF25, 0x44);
    for (uint16_t i = 0; i < 16; i++) {
        apu_write_reg(&gb.apu, (uint16_t)(0xFF30 + i), 0x0F);
    }
    apu_write_reg(&gb.apu, 0xFF1A, 0x80);
    apu_write_reg(&gb.apu, 0xFF1B, 0x00);
    apu_write_reg(&gb.apu, 0xFF1C, 0x20);
    apu_write_reg(&gb.apu, 0xFF1D, 0x00);
    apu_write_reg(&gb.apu, 0xFF1E, 0x87);
    TEST_ASSERT((apu_read_reg(&gb.apu, 0xFF26) & 0x04) != 0);

    apu_write_reg(&gb.apu, 0xFF21, 0xF0);
    apu_write_reg(&gb.apu, 0xFF22, 0x00);
    apu_write_reg(&gb.apu, 0xFF23, 0x80);
    TEST_ASSERT((apu_read_reg(&gb.apu, 0xFF26) & 0x08) != 0);

    apu_tick(&gb.apu, 70224);
    int16_t samples[GB_APU_MAX_SAMPLES * 2];
    size_t frames = apu_read_samples(&gb.apu, samples, GB_APU_MAX_SAMPLES);
    TEST_ASSERT(frames > 700);
    bool nonzero = false;
    for (size_t i = 0; i < frames * 2; i++) {
        if (samples[i] != 0) {
            nonzero = true;
            break;
        }
    }
    TEST_ASSERT(nonzero);

    gb_destroy(&gb);
}

void test_apu_all(void)
{
    run_test("apu/registers", test_apu_registers);
    run_test("apu/sound", test_apu_sound);
    run_test("apu/length-envelope", test_apu_length_envelope);
    run_test("apu/wave-noise", test_apu_wave_noise);
}
