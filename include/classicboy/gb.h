/*
 * ClassicBoy - top level Game Boy Color machine.
 */
#ifndef CB_GB_H
#define CB_GB_H

#include "classicboy/apu.h"
#include "classicboy/cart.h"
#include "classicboy/common.h"
#include "classicboy/cpu.h"
#include "classicboy/joypad.h"
#include "classicboy/mmu.h"
#include "classicboy/ppu.h"
#include "classicboy/timer.h"

typedef struct Gb {
    GbCpu cpu;
    GbMmu mmu;
    GbPpu ppu;
    GbApu apu;
    GbTimer timer;
    GbJoypad joypad;
    GbCart cart;

    bool cgb_mode;
    bool double_speed;
    unsigned speed_remainder;

    uint64_t cycles;
    bool serial_output;
    uint8_t serial_last;
    bool frame_ready;
    char error[256];
} Gb;

void gb_init(Gb *gb);
void gb_destroy(Gb *gb);
void gb_reset(Gb *gb);

GbStatus gb_load_rom(Gb *gb, const char *path);
GbStatus gb_load_rom_data(Gb *gb, const uint8_t *data, size_t size,
                          const char *path);

void gb_run_frame(Gb *gb);
void gb_tick(Gb *gb, unsigned cycles);

void gb_set_button(Gb *gb, GbButton button, bool pressed);
void gb_release_buttons(Gb *gb);
void gb_set_serial_output(Gb *gb, bool enabled);
void gb_set_sample_rate(Gb *gb, uint32_t sample_rate);
size_t gb_audio_read(Gb *gb, int16_t *out, size_t max_frames);
const uint16_t *gb_framebuffer(const Gb *gb);
const char *gb_title(const Gb *gb);
const char *gb_error(const Gb *gb);
bool gb_save_battery(Gb *gb);

bool gb_is_cgb(const Gb *gb);
bool gb_double_speed(const Gb *gb);
void gb_switch_speed(Gb *gb);
void gb_set_dmg_palette(Gb *gb, const uint32_t argb[4]);

void gb_request_interrupt(Gb *gb, unsigned bit);

#endif /* CB_GB_H */
