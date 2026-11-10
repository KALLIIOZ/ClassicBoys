#include "classicboy/apu.h"

static const uint8_t apu_read_mask[0x20] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00,
    0xFF, 0xBF, 0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF,
    0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static const uint8_t duty_table[4] = {0x01, 0x81, 0x87, 0x7E};

static void clear_channels(GbApu *apu)
{
    memset(&apu->ch1, 0, sizeof(apu->ch1));
    memset(&apu->ch2, 0, sizeof(apu->ch2));
    memset(&apu->ch3, 0, sizeof(apu->ch3));
    memset(&apu->ch4, 0, sizeof(apu->ch4));
}

static void power_off(GbApu *apu)
{
    clear_channels(apu);
    memset(apu->regs, 0, sizeof(apu->regs));
    apu->powered = false;
    apu->frame_cycles = 0;
    apu->frame_step = 0;
    apu->sample_accum = 0;
    apu->sample_count = 0;
    apu->dc_prev_in_l = apu->dc_prev_in_r = 0.0;
    apu->dc_prev_out_l = apu->dc_prev_out_r = 0.0;
}

static void power_on(GbApu *apu)
{
    clear_channels(apu);
    memset(apu->regs, 0, sizeof(apu->regs));
    apu->powered = true;
    apu->frame_cycles = 0;
    apu->frame_step = 0;
    apu->sample_accum = 0;
    apu->sample_count = 0;
    apu->dc_prev_in_l = apu->dc_prev_in_r = 0.0;
    apu->dc_prev_out_l = apu->dc_prev_out_r = 0.0;
}

void apu_reset(GbApu *apu)
{
    if (!apu) {
        return;
    }
    memset(apu, 0, sizeof(*apu));
    apu->sample_rate = GB_SAMPLE_RATE;
    apu->powered = true;
    apu->regs[0x14] = 0x77;
    apu->regs[0x15] = 0xF3;
}

static void trigger_square(GbApu *apu, GbSquare *ch, bool channel1)
{
    uint8_t env = apu->regs[channel1 ? 0x02 : 0x07];
    if ((env & 0xF8u) == 0) {
        ch->enabled = false;
        return;
    }
    uint8_t length_data = apu->regs[channel1 ? 0x01 : 0x06] & 0x3Fu;
    ch->enabled = true;
    ch->length = (uint8_t)(64u - length_data);
    ch->volume = (uint8_t)(env >> 4);
    ch->env_initial = ch->volume;
    ch->env_add = (env & 0x08u) != 0;
    ch->env_period = env & 0x07u;
    ch->env_timer = ch->env_period;
    ch->phase = 0.0;
    if (channel1) {
        uint8_t sweep = apu->regs[0x00];
        ch->sweep_period = (uint8_t)((sweep >> 4) & 0x07u);
        ch->sweep_negate = (sweep & 0x08u) != 0;
        ch->sweep_shift = sweep & 0x07u;
        ch->sweep_enable = ch->sweep_period != 0 || ch->sweep_shift != 0;
        ch->sweep_timer = ch->sweep_period ? ch->sweep_period : 8;
        ch->sweep_shadow = ch->freq;
        ch->sweep_negate_used = false;
        if (ch->sweep_shift != 0) {
            uint16_t delta = (uint16_t)(ch->sweep_shadow >> ch->sweep_shift);
            uint16_t next = ch->sweep_negate
                                ? (uint16_t)(ch->sweep_shadow - delta)
                                : (uint16_t)(ch->sweep_shadow + delta);
            if (next > 2047u) {
                ch->enabled = false;
            }
        }
    }
}

static void trigger_wave(GbApu *apu)
{
    GbWave *ch = &apu->ch3;
    if ((apu->regs[0x0A] & 0x80u) == 0) {
        ch->enabled = false;
        return;
    }
    uint8_t length_data = apu->regs[0x0B];
    ch->enabled = true;
    ch->length = (uint8_t)(64u - (length_data & 0x3Fu));
    ch->volume_code = (uint8_t)((apu->regs[0x0C] >> 5) & 0x03u);
    ch->phase = 0.0;
    ch->position = 0;
}

static void trigger_noise(GbApu *apu)
{
    GbNoise *ch = &apu->ch4;
    uint8_t env = apu->regs[0x11];
    if ((env & 0xF8u) == 0) {
        ch->enabled = false;
        return;
    }
    uint8_t length_data = apu->regs[0x10];
    ch->enabled = true;
    ch->length = (uint8_t)(64u - (length_data & 0x3Fu));
    ch->volume = (uint8_t)(env >> 4);
    ch->env_initial = ch->volume;
    ch->env_add = (env & 0x08u) != 0;
    ch->env_period = env & 0x07u;
    ch->env_timer = ch->env_period;
    ch->lfsr = 0x7FFFu;
    ch->phase = 0.0;
    ch->divisor = apu->regs[0x12] & 0x07u;
    ch->width = (uint8_t)((apu->regs[0x12] >> 3) & 0x01u);
    ch->shift = (uint8_t)(apu->regs[0x12] >> 4);
}

static void clock_length(bool enabled, bool length_enable, uint8_t *length)
{
    if (!enabled || !length_enable || *length == 0) {
        return;
    }
    (*length)--;
}

static void clock_envelope(bool enabled, uint8_t env_period, uint8_t env_add,
                           uint8_t *env_timer, uint8_t *volume)
{
    if (!enabled || env_period == 0) {
        return;
    }
    if (*env_timer > 0) {
        (*env_timer)--;
    }
    if (*env_timer == 0) {
        *env_timer = env_period;
        if (env_add) {
            if (*volume < 15) {
                (*volume)++;
            }
        } else {
            if (*volume > 0) {
                (*volume)--;
            }
        }
    }
}

static void clock_sweep(GbApu *apu, GbSquare *ch)
{
    if (!ch->enabled || !ch->sweep_enable || ch->sweep_period == 0) {
        return;
    }
    if (ch->sweep_timer > 0) {
        ch->sweep_timer--;
    }
    if (ch->sweep_timer != 0) {
        return;
    }
    ch->sweep_timer = ch->sweep_period;
    if (ch->sweep_shift == 0) {
        return;
    }
    if (ch->sweep_negate && ch->sweep_negate_used) {
        ch->enabled = false;
        return;
    }
    uint16_t delta = (uint16_t)(ch->sweep_shadow >> ch->sweep_shift);
    uint16_t next = ch->sweep_negate
                        ? (uint16_t)(ch->sweep_shadow - delta)
                        : (uint16_t)(ch->sweep_shadow + delta);
    if (next > 2047u) {
        ch->enabled = false;
        return;
    }
    ch->sweep_shadow = next;
    ch->freq = next;
    if (ch->sweep_negate) {
        ch->sweep_negate_used = true;
    }
    (void)apu;
}

static void frame_sequencer(GbApu *apu)
{
    apu->frame_step = (uint8_t)((apu->frame_step + 1u) & 7u);
    if ((apu->frame_step & 1u) == 0) {
        clock_length(apu->ch1.enabled, apu->ch1.length_enable, &apu->ch1.length);
        clock_length(apu->ch2.enabled, apu->ch2.length_enable, &apu->ch2.length);
        clock_length(apu->ch3.enabled, apu->ch3.length_enable, &apu->ch3.length);
        clock_length(apu->ch4.enabled, apu->ch4.length_enable, &apu->ch4.length);
        if (apu->ch1.length_enable && apu->ch1.length == 0) {
            apu->ch1.enabled = false;
        }
        if (apu->ch2.length_enable && apu->ch2.length == 0) {
            apu->ch2.enabled = false;
        }
        if (apu->ch3.length_enable && apu->ch3.length == 0) {
            apu->ch3.enabled = false;
        }
        if (apu->ch4.length_enable && apu->ch4.length == 0) {
            apu->ch4.enabled = false;
        }
    }
    if (apu->frame_step == 2 || apu->frame_step == 6) {
        clock_sweep(apu, &apu->ch1);
    }
    if (apu->frame_step == 7) {
        clock_envelope(apu->ch1.enabled, apu->ch1.env_period, apu->ch1.env_add,
                       &apu->ch1.env_timer, &apu->ch1.volume);
        clock_envelope(apu->ch2.enabled, apu->ch2.env_period, apu->ch2.env_add,
                       &apu->ch2.env_timer, &apu->ch2.volume);
        clock_envelope(apu->ch4.enabled, apu->ch4.env_period, apu->ch4.env_add,
                       &apu->ch4.env_timer, &apu->ch4.volume);
    }
}

static double square_sample(GbSquare *ch, uint32_t sample_rate)
{
    if (!ch->enabled || ch->freq >= 2048u) {
        return 0.0;
    }
    double frequency = 131072.0 / (double)(2048u - ch->freq);
    ch->phase += frequency / (double)sample_rate;
    while (ch->phase >= 1.0) {
        ch->phase -= 1.0;
    }
    int index = (int)(ch->phase * 8.0);
    if (index < 0) {
        index = 0;
    }
    index &= 7;
    uint8_t bit = (uint8_t)((duty_table[ch->duty & 3u] >> (7 - index)) & 1u);
    return bit ? (double)ch->volume : 0.0;
}

static double wave_sample(GbApu *apu, uint32_t sample_rate)
{
    GbWave *ch = &apu->ch3;
    if (!ch->enabled || ch->freq >= 2048u || ch->volume_code == 0) {
        return 0.0;
    }
    double frequency = 65536.0 / (double)(2048u - ch->freq);
    ch->phase += frequency / (double)sample_rate;
    while (ch->phase >= 1.0) {
        ch->phase -= 1.0;
    }
    int index = (int)(ch->phase * 32.0) & 31;
    uint8_t byte = apu->wave[index / 2];
    uint8_t nibble = (index & 1) ? (uint8_t)(byte & 0x0Fu)
                                 : (uint8_t)(byte >> 4);
    unsigned shift = (unsigned)(ch->volume_code - 1u);
    return (double)(nibble >> shift);
}

static double noise_sample(GbNoise *ch, uint32_t sample_rate)
{
    if (!ch->enabled) {
        return 0.0;
    }
    double divisor = ch->divisor == 0 ? 0.5 : (double)ch->divisor;
    double clock_hz = 262144.0 / (divisor * (double)(1u << ch->shift));
    ch->phase += clock_hz / (double)sample_rate;
    while (ch->phase >= 1.0) {
        ch->phase -= 1.0;
        uint16_t feedback = (uint16_t)((ch->lfsr ^ (ch->lfsr >> 1)) & 1u);
        ch->lfsr >>= 1;
        if (ch->width) {
            ch->lfsr = (uint16_t)((ch->lfsr & 0x7Fu) | (feedback << 6));
        } else {
            ch->lfsr = (uint16_t)((ch->lfsr & 0x3FFFu) | (feedback << 14));
        }
    }
    return (ch->lfsr & 1u) ? 0.0 : (double)ch->volume;
}

static double dc_block(double input, double *prev_in, double *prev_out)
{
    double output = input - *prev_in + 0.995 * *prev_out;
    *prev_in = input;
    *prev_out = output;
    return output;
}

static int16_t clamp_sample(double value)
{
    if (value > 32767.0) {
        return 32767;
    }
    if (value < -32768.0) {
        return -32768;
    }
    return (int16_t)value;
}

static void emit_sample(GbApu *apu)
{
    double left = 0.0;
    double right = 0.0;
    if (apu->powered) {
        double s1 = square_sample(&apu->ch1, apu->sample_rate);
        double s2 = square_sample(&apu->ch2, apu->sample_rate);
        double s3 = wave_sample(apu, apu->sample_rate);
        double s4 = noise_sample(&apu->ch4, apu->sample_rate);
        uint8_t pan = apu->regs[0x15];
        if (pan & 0x10u) {
            left += s1;
        }
        if (pan & 0x20u) {
            left += s2;
        }
        if (pan & 0x40u) {
            left += s3;
        }
        if (pan & 0x80u) {
            left += s4;
        }
        if (pan & 0x01u) {
            right += s1;
        }
        if (pan & 0x02u) {
            right += s2;
        }
        if (pan & 0x04u) {
            right += s3;
        }
        if (pan & 0x08u) {
            right += s4;
        }
        uint8_t volume = apu->regs[0x14];
        left *= (double)((volume & 0x07u) + 1u) * 24.0;
        right *= (double)(((volume >> 4) & 0x07u) + 1u) * 24.0;
    }
    left = dc_block(left, &apu->dc_prev_in_l, &apu->dc_prev_out_l);
    right = dc_block(right, &apu->dc_prev_in_r, &apu->dc_prev_out_r);
    if (apu->sample_count < GB_APU_MAX_SAMPLES) {
        apu->samples[apu->sample_count * 2] = clamp_sample(left);
        apu->samples[apu->sample_count * 2 + 1] = clamp_sample(right);
        apu->sample_count++;
    }
}

void apu_tick(GbApu *apu, unsigned cycles)
{
    if (!apu || apu->sample_rate == 0) {
        return;
    }
    if (apu->powered) {
        apu->frame_cycles += cycles;
        while (apu->frame_cycles >= 8192u) {
            apu->frame_cycles -= 8192u;
            frame_sequencer(apu);
        }
    }
    apu->sample_accum += (uint64_t)cycles * apu->sample_rate;
    while (apu->sample_accum >= GB_CLOCK_HZ) {
        apu->sample_accum -= GB_CLOCK_HZ;
        emit_sample(apu);
    }
}

uint8_t apu_read_reg(const GbApu *apu, uint16_t reg)
{
    if (reg >= 0xFF30u && reg <= 0xFF3Fu) {
        return apu->wave[reg - 0xFF30u];
    }
    if (reg < 0xFF10u || reg > 0xFF2Fu) {
        return 0xFF;
    }
    uint8_t offset = (uint8_t)(reg - 0xFF10u);
    if (offset == 0x16u) {
        if (!apu->powered) {
            return 0x70;
        }
        uint8_t value = 0xF0u;
        if (apu->ch1.enabled) {
            value |= 0x01u;
        }
        if (apu->ch2.enabled) {
            value |= 0x02u;
        }
        if (apu->ch3.enabled) {
            value |= 0x04u;
        }
        if (apu->ch4.enabled) {
            value |= 0x08u;
        }
        return value;
    }
    if (!apu->powered) {
        return offset < 0x16u ? 0x00u : 0xFFu;
    }
    return (uint8_t)(apu->regs[offset] | apu_read_mask[offset]);
}

static void update_frequency_high(GbSquare *ch, uint8_t value)
{
    ch->freq = (uint16_t)((ch->freq & 0x00FFu) | ((uint16_t)(value & 0x07u) << 8));
}

void apu_write_reg(GbApu *apu, uint16_t reg, uint8_t value)
{
    if (!apu) {
        return;
    }
    if (reg >= 0xFF30u && reg <= 0xFF3Fu) {
        apu->wave[reg - 0xFF30u] = value;
        return;
    }
    if (reg < 0xFF10u || reg > 0xFF2Fu) {
        return;
    }
    uint8_t offset = (uint8_t)(reg - 0xFF10u);

    if (offset == 0x16u) {
        bool on = (value & 0x80u) != 0;
        if (on && !apu->powered) {
            power_on(apu);
        } else if (!on && apu->powered) {
            power_off(apu);
        }
        return;
    }
    if (!apu->powered) {
        return;
    }
    if (offset == 0x15u || offset == 0x1Fu || offset >= 0x17u) {
        if (offset >= 0x17u) {
            return;
        }
    }

    apu->regs[offset] = value;

    switch (offset) {
    case 0x00: {
        GbSquare *ch = &apu->ch1;
        ch->sweep_period = (uint8_t)((value >> 4) & 0x07u);
        ch->sweep_negate = (value & 0x08u) != 0;
        ch->sweep_shift = value & 0x07u;
        ch->sweep_enable = ch->sweep_period != 0 || ch->sweep_shift != 0;
        break;
    }
    case 0x01:
        apu->ch1.duty = (uint8_t)(value >> 6);
        break;
    case 0x02: {
        GbSquare *ch = &apu->ch1;
        ch->env_initial = (uint8_t)(value >> 4);
        ch->env_add = (value & 0x08u) != 0;
        ch->env_period = value & 0x07u;
        if ((value & 0xF8u) == 0) {
            ch->enabled = false;
        }
        break;
    }
    case 0x03:
        apu->ch1.freq = (uint16_t)((apu->ch1.freq & 0x0700u) | value);
        break;
    case 0x04:
        update_frequency_high(&apu->ch1, value);
        apu->ch1.length_enable = (value & 0x40u) != 0;
        if (apu->ch1.length_enable && apu->ch1.length == 0) {
            apu->ch1.enabled = false;
        }
        if (value & 0x80u) {
            trigger_square(apu, &apu->ch1, true);
        }
        break;
    case 0x06:
        apu->ch2.duty = (uint8_t)(value >> 6);
        break;
    case 0x07: {
        GbSquare *ch = &apu->ch2;
        ch->env_initial = (uint8_t)(value >> 4);
        ch->env_add = (value & 0x08u) != 0;
        ch->env_period = value & 0x07u;
        if ((value & 0xF8u) == 0) {
            ch->enabled = false;
        }
        break;
    }
    case 0x08:
        apu->ch2.freq = (uint16_t)((apu->ch2.freq & 0x0700u) | value);
        break;
    case 0x09:
        update_frequency_high(&apu->ch2, value);
        apu->ch2.length_enable = (value & 0x40u) != 0;
        if (apu->ch2.length_enable && apu->ch2.length == 0) {
            apu->ch2.enabled = false;
        }
        if (value & 0x80u) {
            trigger_square(apu, &apu->ch2, false);
        }
        break;
    case 0x0A:
        if ((value & 0x80u) == 0) {
            apu->ch3.enabled = false;
        }
        break;
    case 0x0B:
        break;
    case 0x0C:
        apu->ch3.volume_code = (uint8_t)((value >> 5) & 0x03u);
        break;
    case 0x0D:
        apu->ch3.freq = (uint16_t)((apu->ch3.freq & 0x0700u) | value);
        break;
    case 0x0E:
        apu->ch3.freq =
            (uint16_t)((apu->ch3.freq & 0x00FFu) | ((uint16_t)(value & 0x07u) << 8));
        apu->ch3.length_enable = (value & 0x40u) != 0;
        if (apu->ch3.length_enable && apu->ch3.length == 0) {
            apu->ch3.enabled = false;
        }
        if (value & 0x80u) {
            trigger_wave(apu);
        }
        break;
    case 0x10:
        break;
    case 0x11: {
        GbNoise *ch = &apu->ch4;
        ch->env_initial = (uint8_t)(value >> 4);
        ch->env_add = (value & 0x08u) != 0;
        ch->env_period = value & 0x07u;
        if ((value & 0xF8u) == 0) {
            ch->enabled = false;
        }
        break;
    }
    case 0x12:
        apu->ch4.divisor = value & 0x07u;
        apu->ch4.width = (uint8_t)((value >> 3) & 0x01u);
        apu->ch4.shift = (uint8_t)(value >> 4);
        break;
    case 0x13:
        apu->ch4.length_enable = (value & 0x40u) != 0;
        if (apu->ch4.length_enable && apu->ch4.length == 0) {
            apu->ch4.enabled = false;
        }
        if (value & 0x80u) {
            trigger_noise(apu);
        }
        break;
    case 0x14:
    case 0x15:
        break;
    default:
        break;
    }
}

void apu_set_sample_rate(GbApu *apu, uint32_t sample_rate)
{
    if (!apu) {
        return;
    }
    if (sample_rate < 8000u) {
        sample_rate = GB_SAMPLE_RATE;
    }
    apu->sample_rate = sample_rate;
    apu->sample_accum = 0;
    apu->sample_count = 0;
}

size_t apu_read_samples(GbApu *apu, int16_t *out, size_t max_frames)
{
    if (!apu || !out || max_frames == 0) {
        return 0;
    }
    size_t frames = apu->sample_count;
    if (frames > max_frames) {
        frames = max_frames;
    }
    memcpy(out, apu->samples, frames * 2 * sizeof(int16_t));
    if (frames < apu->sample_count) {
        memmove(apu->samples, apu->samples + frames * 2,
                (apu->sample_count - frames) * 2 * sizeof(int16_t));
    }
    apu->sample_count -= frames;
    return frames;
}
