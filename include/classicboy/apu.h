/*
 * ClassicBoy - 4 channel DMG audio processing unit.
 */
#ifndef CB_APU_H
#define CB_APU_H

#include "classicboy/common.h"

#define GB_APU_MAX_SAMPLES 2048u

typedef struct GbSquare {
    bool enabled;
    uint8_t duty;
    uint16_t freq;
    uint8_t volume;
    uint8_t env_initial;
    bool env_add;
    uint8_t env_period;
    uint8_t env_timer;
    bool length_enable;
    uint8_t length;
    bool sweep_enable;
    uint8_t sweep_period;
    uint8_t sweep_shift;
    bool sweep_negate;
    uint8_t sweep_timer;
    uint16_t sweep_shadow;
    bool sweep_negate_used;
    double phase;
} GbSquare;

typedef struct GbWave {
    bool enabled;
    uint16_t freq;
    uint8_t volume_code;
    bool length_enable;
    uint8_t length;
    uint8_t position;
    double phase;
} GbWave;

typedef struct GbNoise {
    bool enabled;
    uint8_t volume;
    uint8_t env_initial;
    bool env_add;
    uint8_t env_period;
    uint8_t env_timer;
    bool length_enable;
    uint8_t length;
    uint8_t divisor;
    uint8_t width;
    uint8_t shift;
    uint16_t lfsr;
    double phase;
} GbNoise;

typedef struct GbApu {
    bool powered;
    GbSquare ch1;
    GbSquare ch2;
    GbWave ch3;
    GbNoise ch4;

    uint8_t regs[0x20]; /* 0xFF10..0xFF2F */
    uint8_t wave[16];   /* 0xFF30..0xFF3F */

    uint32_t frame_cycles;
    uint8_t frame_step;
    uint32_t sample_rate;
    uint64_t sample_accum;

    int16_t samples[GB_APU_MAX_SAMPLES * 2];
    size_t sample_count;

    double dc_prev_in_l;
    double dc_prev_in_r;
    double dc_prev_out_l;
    double dc_prev_out_r;
} GbApu;

void apu_reset(GbApu *apu);
void apu_tick(GbApu *apu, unsigned cycles);
uint8_t apu_read_reg(const GbApu *apu, uint16_t reg);
void apu_write_reg(GbApu *apu, uint16_t reg, uint8_t value);
void apu_set_sample_rate(GbApu *apu, uint32_t sample_rate);
size_t apu_read_samples(GbApu *apu, int16_t *out, size_t max_frames);

#endif /* CB_APU_H */
