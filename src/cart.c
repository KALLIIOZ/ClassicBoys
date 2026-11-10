#include "classicboy/cart.h"

#include <errno.h>
#include <stdlib.h>

#define CART_HEADER_START 0x134u
#define CART_HEADER_END 0x143u
#define CART_HEADER_CHECKSUM 0x14Du

static void set_error(char *err, size_t err_len, const char *message)
{
    if (err && err_len) {
        snprintf(err, err_len, "%s", message);
    }
}

static char *replace_extension(const char *path, const char *extension)
{
    if (!path) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) {
        slash = backslash;
    }
    const char *dot = strrchr(path, '.');
    size_t base_len;
    if (dot && (!slash || dot > slash)) {
        base_len = (size_t)(dot - path);
    } else {
        base_len = strlen(path);
    }
    size_t ext_len = strlen(extension);
    char *result = (char *)gb_alloc(base_len + ext_len + 1);
    if (!result) {
        return NULL;
    }
    memcpy(result, path, base_len);
    memcpy(result + base_len, extension, ext_len + 1);
    return result;
}

static bool read_whole_file(const char *path, uint8_t **out, size_t *out_size,
                            char *err, size_t err_len)
{
    *out = NULL;
    *out_size = 0;
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (err && err_len) {
            snprintf(err, err_len, "No se pudo abrir la ROM: %s", strerror(errno));
        }
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        set_error(err, err_len, "No se pudo leer el tamano de la ROM");
        return false;
    }
    long end = ftell(file);
    if (end <= 0 || (unsigned long)end > GB_MAX_ROM_SIZE) {
        fclose(file);
        set_error(err, err_len, "Tamano de ROM invalido (maximo 8 MB)");
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        set_error(err, err_len, "No se pudo rebobinar la ROM");
        return false;
    }
    size_t size = (size_t)end;
    uint8_t *data = (uint8_t *)gb_alloc(size);
    if (!data) {
        fclose(file);
        set_error(err, err_len, "Sin memoria para cargar la ROM");
        return false;
    }
    size_t got = fread(data, 1, size, file);
    fclose(file);
    if (got != size) {
        gb_free(data);
        set_error(err, err_len, "La ROM esta truncada o no se pudo leer");
        return false;
    }
    *out = data;
    *out_size = size;
    return true;
}

static void sanitize_title(const uint8_t *rom, char *title, size_t title_len)
{
    size_t n = 0;
    for (size_t i = CART_HEADER_START; i <= CART_HEADER_END && n + 1 < title_len;
         i++) {
        uint8_t c = rom[i];
        if (c == 0) {
            break;
        }
        if (c >= 32 && c <= 126) {
            title[n++] = (char)c;
        } else {
            title[n++] = ' ';
        }
    }
    while (n > 0 && (title[n - 1] == ' ' || title[n - 1] == '\0')) {
        n--;
    }
    title[n] = '\0';
    if (n == 0) {
        snprintf(title, title_len, "Game Boy");
    }
}

static bool type_has_ram(uint8_t type)
{
    switch (type) {
    case 0x02:
    case 0x03:
    case 0x08:
    case 0x09:
    case 0x0F:
    case 0x10:
    case 0x12:
    case 0x13:
    case 0x1A:
    case 0x1B:
    case 0x1D:
    case 0x1E:
        return true;
    default:
        return false;
    }
}

static bool type_has_battery(uint8_t type)
{
    switch (type) {
    case 0x03:
    case 0x06:
    case 0x09:
    case 0x0F:
    case 0x10:
    case 0x13:
    case 0x1B:
    case 0x1E:
        return true;
    default:
        return false;
    }
}

static bool type_has_rtc(uint8_t type)
{
    return type == 0x0F || type == 0x10;
}

static bool type_has_rumble(uint8_t type)
{
    return type >= 0x1C && type <= 0x1E;
}

static bool decode_type(uint8_t type, GbMbc *mbc, char *err, size_t err_len)
{
    switch (type) {
    case 0x00:
    case 0x08:
    case 0x09:
        *mbc = GB_MBC_NONE;
        return true;
    case 0x01:
    case 0x02:
    case 0x03:
        *mbc = GB_MBC1;
        return true;
    case 0x05:
    case 0x06:
        *mbc = GB_MBC2;
        return true;
    case 0x0F:
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
        *mbc = GB_MBC3;
        return true;
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
        *mbc = GB_MBC5;
        return true;
    default:
        if (err && err_len) {
            snprintf(err, err_len,
                     "Tipo de cartucho no soportado (0x%02X). "
                     "Se admiten ROM, MBC1, MBC2, MBC3 y MBC5.",
                     type);
        }
        return false;
    }
}

static size_t ram_size_from_code(uint8_t code, GbMbc mbc, uint8_t type)
{
    if (mbc == GB_MBC2) {
        return 512;
    }
    switch (code) {
    case 0:
        return type_has_ram(type) ? 8192 : 0;
    case 1:
        return 2048;
    case 2:
        return 8192;
    case 3:
        return 32768;
    case 4:
        return 131072;
    case 5:
        return 65536;
    default:
        return type_has_ram(type) ? 8192 : 0;
    }
}

void cart_init(GbCart *cart)
{
    if (!cart) {
        return;
    }
    memset(cart, 0, sizeof(*cart));
    cart->mbc = GB_MBC_NONE;
    snprintf(cart->title, sizeof(cart->title), "Game Boy");
}

void cart_destroy(GbCart *cart)
{
    if (!cart) {
        return;
    }
    gb_free(cart->rom);
    gb_free(cart->ram);
    gb_free(cart->rom_path);
    gb_free(cart->sav_path);
    gb_free(cart->rtc_path);
    memset(cart, 0, sizeof(*cart));
    cart->mbc = GB_MBC_NONE;
}

static void load_battery(GbCart *cart)
{
    if (!cart->battery) {
        return;
    }
    if (cart->ram && cart->ram_size && cart->sav_path) {
        FILE *file = fopen(cart->sav_path, "rb");
        if (file) {
            size_t got = fread(cart->ram, 1, cart->ram_size, file);
            if (got < cart->ram_size) {
                memset(cart->ram + got, 0, cart->ram_size - got);
            }
            fclose(file);
        }
    }
    if (cart->has_rtc && cart->rtc_path) {
        FILE *file = fopen(cart->rtc_path, "rb");
        if (file) {
            uint8_t buffer[16] = {0};
            if (fread(buffer, 1, sizeof(buffer), file) == sizeof(buffer) &&
                memcmp(buffer, "CBRT", 4) == 0) {
                cart->rtc.seconds = buffer[4];
                cart->rtc.minutes = buffer[5];
                cart->rtc.hours = buffer[6];
                cart->rtc.days = (uint16_t)(buffer[7] | ((uint16_t)buffer[8] << 8));
                cart->rtc.halted = buffer[9] != 0;
                cart->rtc_cycles = gb_le32(buffer + 10) % GB_CLOCK_HZ;
                cart->rtc_latched = cart->rtc;
            }
            fclose(file);
        }
    }
}

GbStatus cart_load_data(GbCart *cart, const uint8_t *data, size_t size,
                        const char *path, char *err, size_t err_len)
{
    if (!cart || !data) {
        set_error(err, err_len, "Argumento de carga invalido");
        return GB_ERR_ARGUMENT;
    }
    if (size < 0x150) {
        set_error(err, err_len, "El archivo es demasiado pequeno para ser una ROM");
        return GB_ERR_FORMAT;
    }
    if (size > GB_MAX_ROM_SIZE) {
        set_error(err, err_len, "La ROM supera el limite de 8 MB");
        return GB_ERR_FORMAT;
    }

    GbCart fresh;
    cart_init(&fresh);

    size_t padded = (size + GB_ROM_BANK_SIZE - 1) & ~(GB_ROM_BANK_SIZE - 1);
    if (padded < GB_ROM_BANK_SIZE * 2) {
        padded = GB_ROM_BANK_SIZE * 2;
    }
    if (padded > GB_MAX_ROM_SIZE) {
        padded = GB_MAX_ROM_SIZE;
    }
    fresh.rom = (uint8_t *)gb_calloc(1, padded);
    if (!fresh.rom) {
        set_error(err, err_len, "Sin memoria para la ROM");
        return GB_ERR_MEMORY;
    }
    fresh.rom_size = padded;
    fresh.rom_banks = (unsigned)(padded / GB_ROM_BANK_SIZE);
    memcpy(fresh.rom, data, size);

    fresh.type_code = fresh.rom[0x147];
    fresh.battery = type_has_battery(fresh.type_code);
    fresh.has_rtc = type_has_rtc(fresh.type_code);
    fresh.rumble = type_has_rumble(fresh.type_code);
    fresh.cgb = (fresh.rom[0x143] & 0x80u) != 0;
    fresh.cgb_only = fresh.cgb && (fresh.rom[0x143] & 0x40u) != 0;
    sanitize_title(fresh.rom, fresh.title, sizeof(fresh.title));

    uint8_t checksum = 0;
    for (size_t i = CART_HEADER_START; i <= 0x14C; i++) {
        checksum = (uint8_t)(checksum - fresh.rom[i] - 1u);
    }
    fresh.header_checksum_ok = checksum == fresh.rom[CART_HEADER_CHECKSUM];

    if (!decode_type(fresh.type_code, &fresh.mbc, err, err_len)) {
        cart_destroy(&fresh);
        return GB_ERR_UNSUPPORTED;
    }

    uint8_t ram_code = fresh.rom[0x149];
    size_t ram_size = ram_size_from_code(ram_code, fresh.mbc, fresh.type_code);
    if (ram_size) {
        fresh.ram = (uint8_t *)gb_calloc(1, ram_size);
        if (!fresh.ram) {
            cart_destroy(&fresh);
            set_error(err, err_len, "Sin memoria para la RAM del cartucho");
            return GB_ERR_MEMORY;
        }
        fresh.ram_size = ram_size;
        fresh.ram_banks = (unsigned)(ram_size / GB_RAM_BANK_SIZE);
        if (fresh.ram_banks == 0) {
            fresh.ram_banks = 1;
        }
    }

    if (path) {
        fresh.rom_path = gb_strdup(path);
        if (fresh.battery) {
            fresh.sav_path = replace_extension(path, ".sav");
            if (fresh.has_rtc) {
                fresh.rtc_path = replace_extension(path, ".rtc");
            }
        }
        if (!fresh.rom_path || (fresh.battery && !fresh.sav_path)) {
            cart_destroy(&fresh);
            set_error(err, err_len, "Sin memoria para las rutas de guardado");
            return GB_ERR_MEMORY;
        }
    }

    fresh.ram_enable = 0;
    fresh.bank1 = 1;
    fresh.bank2 = 0;
    fresh.ram_bank = 0;
    fresh.mode = 0;
    fresh.rtc_select = 0;
    fresh.latch_armed = false;
    fresh.loaded = true;
    fresh.ram_dirty = false;

    load_battery(&fresh);

    cart_destroy(cart);
    *cart = fresh;
    return GB_OK;
}

GbStatus cart_load_file(GbCart *cart, const char *path, char *err,
                        size_t err_len)
{
    if (!path || !*path) {
        set_error(err, err_len, "Ruta de ROM vacia");
        return GB_ERR_ARGUMENT;
    }
    uint8_t *data = NULL;
    size_t size = 0;
    if (!read_whole_file(path, &data, &size, err, err_len)) {
        return GB_ERR_IO;
    }
    GbStatus status = cart_load_data(cart, data, size, path, err, err_len);
    gb_free(data);
    return status;
}

static unsigned rom_bank_for(const GbCart *cart, uint16_t addr)
{
    unsigned bank = 0;
    if (addr < 0x4000) {
        if (cart->mbc == GB_MBC1 && cart->mode) {
            bank = (unsigned)(cart->bank2 & 0x03u) << 5;
        }
        return bank % cart->rom_banks;
    }

    switch (cart->mbc) {
    case GB_MBC_NONE:
        bank = 1;
        break;
    case GB_MBC1: {
        unsigned low = cart->bank1 & 0x1Fu;
        if (low == 0) {
            low = 1;
        }
        bank = low;
        if (!cart->mode) {
            bank |= (unsigned)(cart->bank2 & 0x03u) << 5;
        }
        break;
    }
    case GB_MBC2: {
        unsigned low = cart->bank1 & 0x0Fu;
        bank = low ? low : 1;
        break;
    }
    case GB_MBC3: {
        unsigned low = cart->bank1 & 0x7Fu;
        bank = low ? low : 1;
        break;
    }
    case GB_MBC5:
        bank = (unsigned)cart->bank1 | ((unsigned)(cart->bank2 & 0x01u) << 8);
        break;
    }
    return bank % cart->rom_banks;
}

static unsigned ram_bank_for(const GbCart *cart)
{
    switch (cart->mbc) {
    case GB_MBC1:
        return cart->mode ? (unsigned)(cart->bank2 & 0x03u) : 0;
    case GB_MBC3:
        return cart->rtc_select <= 0x03u ? cart->rtc_select : 0;
    case GB_MBC5:
        /* En los cartuchos con motor, el bit 3 controla el rumble. */
        return cart->ram_bank & (cart->rumble ? 0x07u : 0x0Fu);
    default:
        return 0;
    }
}

static uint8_t rtc_read_register(const GbCart *cart, uint8_t reg)
{
    const GbRtc *r = &cart->rtc_latched;
    switch (reg) {
    case 0x08:
        return r->seconds;
    case 0x09:
        return r->minutes;
    case 0x0A:
        return r->hours;
    case 0x0B:
        return (uint8_t)(r->days & 0xFFu);
    case 0x0C:
        return (uint8_t)(((r->days >> 8) & 0x01u) | (r->halted ? 0x40u : 0u));
    default:
        return 0xFF;
    }
}

static void rtc_write_register(GbCart *cart, uint8_t reg, uint8_t value)
{
    GbRtc *r = &cart->rtc;
    switch (reg) {
    case 0x08:
        r->seconds = value % 60u;
        break;
    case 0x09:
        r->minutes = value % 60u;
        break;
    case 0x0A:
        r->hours = value % 24u;
        break;
    case 0x0B:
        r->days = (uint16_t)((r->days & 0x100u) | value);
        break;
    case 0x0C:
        r->days = (uint16_t)((r->days & 0xFFu) | ((value & 0x01u) << 8));
        r->halted = (value & 0x40u) != 0;
        break;
    default:
        break;
    }
    cart->rtc_latched = cart->rtc;
    cart->ram_dirty = true;
}

uint8_t cart_read(GbCart *cart, uint16_t addr)
{
    if (!cart || !cart->loaded) {
        return 0xFF;
    }
    if (addr < 0x8000) {
        unsigned bank = rom_bank_for(cart, addr);
        size_t offset = (size_t)bank * GB_ROM_BANK_SIZE +
                        (addr < 0x4000 ? addr : (size_t)(addr - 0x4000));
        if (offset < cart->rom_size) {
            return cart->rom[offset];
        }
        return 0xFF;
    }
    if (addr < 0xA000 || addr >= 0xC000) {
        return 0xFF;
    }
    if (!cart->ram_enable) {
        return 0xFF;
    }
    if (cart->mbc == GB_MBC3 && cart->rtc_select >= 0x08u &&
        cart->rtc_select <= 0x0Cu) {
        return cart->has_rtc ? rtc_read_register(cart, cart->rtc_select) : 0xFF;
    }
    if (!cart->ram || !cart->ram_size) {
        return 0xFF;
    }
    if (cart->mbc == GB_MBC2) {
        return (uint8_t)(0xF0u | (cart->ram[addr & 0x01FFu] & 0x0Fu));
    }
    size_t offset = (size_t)ram_bank_for(cart) * GB_RAM_BANK_SIZE +
                    (size_t)(addr & 0x1FFFu);
    if (offset >= cart->ram_size) {
        return 0xFF;
    }
    return cart->ram[offset];
}

void cart_write(GbCart *cart, uint16_t addr, uint8_t value)
{
    if (!cart || !cart->loaded) {
        return;
    }
    if (addr < 0x2000) {
        if (cart->mbc == GB_MBC2) {
            if (addr & 0x0100u) {
                cart->bank1 = value & 0x0Fu;
                return;
            }
        }
        cart->ram_enable = ((value & 0x0Fu) == 0x0Au) ? 1u : 0u;
        return;
    }
    if (addr < 0x4000) {
        if (cart->mbc == GB_MBC5 && addr >= 0x3000u) {
            cart->bank2 = value & 0x01u;
            return;
        }
        switch (cart->mbc) {
        case GB_MBC1:
            cart->bank1 = value & 0x1Fu;
            break;
        case GB_MBC2:
            cart->bank1 = value & 0x0Fu;
            break;
        case GB_MBC3:
            cart->bank1 = value & 0x7Fu;
            break;
        case GB_MBC5:
            cart->bank1 = value;
            break;
        default:
            break;
        }
        return;
    }
    if (addr < 0x6000) {
        switch (cart->mbc) {
        case GB_MBC1:
            cart->bank2 = value & 0x03u;
            break;
        case GB_MBC3:
            cart->rtc_select = value;
            break;
        case GB_MBC5:
            cart->ram_bank = value & 0x0Fu;
            break;
        default:
            break;
        }
        return;
    }
    if (addr < 0x8000) {
        if (cart->mbc == GB_MBC1) {
            cart->mode = value & 0x01u;
        } else if (cart->mbc == GB_MBC3) {
            if (value == 0x00u) {
                cart->latch_armed = true;
            } else if (value == 0x01u && cart->latch_armed) {
                cart->rtc_latched = cart->rtc;
                cart->latch_armed = false;
            }
        }
        return;
    }
    if (addr < 0xA000 || addr >= 0xC000) {
        return;
    }
    if (!cart->ram_enable) {
        return;
    }
    if (cart->mbc == GB_MBC3 && cart->rtc_select >= 0x08u &&
        cart->rtc_select <= 0x0Cu) {
        if (cart->has_rtc) {
            rtc_write_register(cart, cart->rtc_select, value);
        }
        return;
    }
    if (!cart->ram || !cart->ram_size) {
        return;
    }
    if (cart->mbc == GB_MBC2) {
        cart->ram[addr & 0x01FFu] = value & 0x0Fu;
        cart->ram_dirty = true;
        return;
    }
    size_t offset = (size_t)ram_bank_for(cart) * GB_RAM_BANK_SIZE +
                    (size_t)(addr & 0x1FFFu);
    if (offset < cart->ram_size) {
        cart->ram[offset] = value;
        cart->ram_dirty = true;
    }
}

void cart_tick(GbCart *cart, unsigned cycles)
{
    if (!cart || !cart->loaded || !cart->has_rtc || cart->rtc.halted) {
        return;
    }
    cart->rtc_cycles += cycles;
    while (cart->rtc_cycles >= GB_CLOCK_HZ) {
        cart->rtc_cycles -= GB_CLOCK_HZ;
        cart->rtc.seconds++;
        if (cart->rtc.seconds >= 60) {
            cart->rtc.seconds = 0;
            cart->rtc.minutes++;
            if (cart->rtc.minutes >= 60) {
                cart->rtc.minutes = 0;
                cart->rtc.hours++;
                if (cart->rtc.hours >= 24) {
                    cart->rtc.hours = 0;
                    cart->rtc.days++;
                    if (cart->rtc.days >= 512) {
                        cart->rtc.days = 0;
                    }
                }
            }
        }
        cart->ram_dirty = true;
    }
}

bool cart_save_battery(GbCart *cart)
{
    if (!cart || !cart->loaded || !cart->battery) {
        return true;
    }
    bool ok = true;
    if (cart->ram && cart->ram_size && cart->sav_path) {
        FILE *file = fopen(cart->sav_path, "wb");
        if (!file) {
            ok = false;
        } else {
            if (fwrite(cart->ram, 1, cart->ram_size, file) != cart->ram_size) {
                ok = false;
            }
            fclose(file);
        }
    }
    if (cart->has_rtc && cart->rtc_path) {
        FILE *file = fopen(cart->rtc_path, "wb");
        if (!file) {
            ok = false;
        } else {
            uint8_t buffer[16] = {0};
            memcpy(buffer, "CBRT", 4);
            buffer[4] = cart->rtc.seconds;
            buffer[5] = cart->rtc.minutes;
            buffer[6] = cart->rtc.hours;
            buffer[7] = (uint8_t)(cart->rtc.days & 0xFFu);
            buffer[8] = (uint8_t)((cart->rtc.days >> 8) & 0x01u);
            buffer[9] = cart->rtc.halted ? 1u : 0u;
            gb_write_le32(buffer + 10, cart->rtc_cycles);
            if (fwrite(buffer, 1, sizeof(buffer), file) != sizeof(buffer)) {
                ok = false;
            }
            fclose(file);
        }
    }
    if (ok) {
        cart->ram_dirty = false;
    }
    return ok;
}

const char *cart_mbc_name(const GbCart *cart)
{
    if (!cart) {
        return "Desconocido";
    }
    switch (cart->mbc) {
    case GB_MBC_NONE:
        return cart->ram ? "ROM + RAM" : "ROM";
    case GB_MBC1:
        return "MBC1";
    case GB_MBC2:
        return "MBC2";
    case GB_MBC3:
        return cart->has_rtc ? "MBC3 + RTC" : "MBC3";
    case GB_MBC5:
        if (cart->rumble) {
            return cart->ram ? "MBC5 + Rumble + RAM" : "MBC5 + Rumble";
        }
        return cart->ram ? "MBC5 + RAM" : "MBC5";
    default:
        return "Desconocido";
    }
}
