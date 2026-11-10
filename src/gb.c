#include "classicboy/gb.h"

#include <stdarg.h>

static void set_gb_error(Gb *gb, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(gb->error, sizeof(gb->error), fmt, args);
    va_end(args);
}

void gb_init(Gb *gb)
{
    if (!gb) {
        return;
    }
    memset(gb, 0, sizeof(*gb));
    cart_init(&gb->cart);
    gb->serial_output = false;
    gb_reset(gb);
}

void gb_destroy(Gb *gb)
{
    if (!gb) {
        return;
    }
    cart_destroy(&gb->cart);
    memset(gb, 0, sizeof(*gb));
    cart_init(&gb->cart);
}

void gb_reset(Gb *gb)
{
    if (!gb) {
        return;
    }
    uint32_t sample_rate = gb->apu.sample_rate;
    if (sample_rate == 0) {
        sample_rate = GB_SAMPLE_RATE;
    }
    gb->cgb_mode = gb->cart.loaded && gb->cart.cgb;
    gb->double_speed = false;
    gb->speed_remainder = 0;

    cpu_reset(&gb->cpu);
    if (gb->cgb_mode) {
        /* Post-boot register values expected by CGB software. */
        gb->cpu.a = 0x11;
        gb->cpu.f = 0x80;
        gb->cpu.b = 0x00;
        gb->cpu.c = 0x00;
        gb->cpu.d = 0xFF;
        gb->cpu.e = 0x56;
        gb->cpu.h = 0x00;
        gb->cpu.l = 0x0D;
    }
    mmu_reset(gb);
    ppu_reset(&gb->ppu);
    gb->ppu.cgb = gb->cgb_mode;
    ppu_clear_framebuffer(&gb->ppu);
    apu_reset(&gb->apu);
    apu_set_sample_rate(&gb->apu, sample_rate);
    timer_reset(&gb->timer);
    joypad_reset(&gb->joypad);
    gb->cycles = 0;
    gb->frame_ready = false;
    gb->error[0] = '\0';
}

GbStatus gb_load_rom_data(Gb *gb, const uint8_t *data, size_t size,
                          const char *path)
{
    if (!gb || !data) {
        if (gb) {
            set_gb_error(gb, "Datos de ROM invalidos");
        }
        return GB_ERR_ARGUMENT;
    }
    if (gb->cart.loaded) {
        (void)cart_save_battery(&gb->cart);
    }
    char err[256] = {0};
    GbStatus status = cart_load_data(&gb->cart, data, size, path, err,
                                     sizeof(err));
    if (status != GB_OK) {
        set_gb_error(gb, "%s", err[0] ? err : "No se pudo cargar la ROM");
        return status;
    }
    gb_reset(gb);
    return GB_OK;
}

GbStatus gb_load_rom(Gb *gb, const char *path)
{
    if (!gb || !path) {
        if (gb) {
            set_gb_error(gb, "Ruta de ROM invalida");
        }
        return GB_ERR_ARGUMENT;
    }
    if (gb->cart.loaded) {
        (void)cart_save_battery(&gb->cart);
    }
    char err[256] = {0};
    GbStatus status = cart_load_file(&gb->cart, path, err, sizeof(err));
    if (status != GB_OK) {
        set_gb_error(gb, "%s", err[0] ? err : "No se pudo cargar la ROM");
        return status;
    }
    gb_reset(gb);
    return GB_OK;
}

void gb_tick(Gb *gb, unsigned cycles)
{
    /*
     * cpu_step() reports CPU T-cycles. In CGB double-speed mode the CPU,
     * timer, serial port and OAM DMA run twice as fast as normal while the
     * PPU, APU, VRAM HDMA and cartridge clock keep running at 4.194304 MHz.
     * The CPU still executes the same number of T-cycles per instruction,
     * so the hardware components that share its clock use the unhalved
     * count while the rest use hardware cycles.
     */
    unsigned hardware_cycles = cycles;
    if (gb->double_speed) {
        gb->speed_remainder += cycles;
        hardware_cycles = gb->speed_remainder / 2u;
        gb->speed_remainder %= 2u;
    }

    gb->cycles += hardware_cycles;
    timer_tick(gb, cycles);
    ppu_tick(gb, hardware_cycles);
    apu_tick(&gb->apu, hardware_cycles);
    mmu_tick(gb, cycles);
    cart_tick(&gb->cart, hardware_cycles);
}

void gb_run_frame(Gb *gb)
{
    if (!gb) {
        return;
    }
    uint64_t start = gb->cycles;
    gb->ppu.frame_ready = false;
    while (gb->cycles - start < GB_FRAME_CYCLES) {
        unsigned cycles = cpu_step(gb, &gb->cpu);
        gb_tick(gb, cycles);
    }
    gb->frame_ready = true;
}

void gb_set_button(Gb *gb, GbButton button, bool pressed)
{
    if (!gb) {
        return;
    }
    joypad_set_button(gb, &gb->joypad, button, pressed);
}

void gb_release_buttons(Gb *gb)
{
    if (!gb) {
        return;
    }
    joypad_release_all(gb, &gb->joypad);
}

void gb_set_serial_output(Gb *gb, bool enabled)
{
    if (gb) {
        gb->serial_output = enabled;
    }
}

void gb_set_sample_rate(Gb *gb, uint32_t sample_rate)
{
    if (gb) {
        apu_set_sample_rate(&gb->apu, sample_rate);
    }
}

size_t gb_audio_read(Gb *gb, int16_t *out, size_t max_frames)
{
    if (!gb) {
        return 0;
    }
    return apu_read_samples(&gb->apu, out, max_frames);
}

const uint16_t *gb_framebuffer(const Gb *gb)
{
    return gb ? gb->ppu.pixels : NULL;
}

const char *gb_title(const Gb *gb)
{
    return gb ? gb->cart.title : "Game Boy";
}

const char *gb_error(const Gb *gb)
{
    return gb ? gb->error : "Error desconocido";
}

bool gb_save_battery(Gb *gb)
{
    return gb ? cart_save_battery(&gb->cart) : false;
}

bool gb_is_cgb(const Gb *gb)
{
    return gb ? gb->cgb_mode : false;
}

bool gb_double_speed(const Gb *gb)
{
    return gb ? gb->double_speed : false;
}

void gb_switch_speed(Gb *gb)
{
    if (!gb || !gb->cgb_mode) {
        return;
    }
    gb->double_speed = !gb->double_speed;
    gb->speed_remainder = 0;
    gb->mmu.io[0x4D] &= (uint8_t)~0x01u;
    if (gb->double_speed) {
        gb->mmu.io[0x4D] |= 0x80u;
    } else {
        gb->mmu.io[0x4D] &= (uint8_t)~0x80u;
    }
    /* A speed switch resets the divider. */
    gb->timer.div = 0;
}

void gb_set_dmg_palette(Gb *gb, const uint32_t argb[4])
{
    if (!gb || !argb) {
        return;
    }
    ppu_set_dmg_palette(&gb->ppu, argb);
}
