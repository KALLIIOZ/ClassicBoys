/*
 * ClassicBoy - cartridge / MBC emulation.
 */
#ifndef CB_CART_H
#define CB_CART_H

#include "classicboy/common.h"

typedef enum GbMbc {
    GB_MBC_NONE = 0,
    GB_MBC1,
    GB_MBC2,
    GB_MBC3,
    GB_MBC5
} GbMbc;

typedef struct GbRtc {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint16_t days;
    bool halted;
} GbRtc;

typedef struct GbCart {
    uint8_t *rom;
    size_t rom_size;
    unsigned rom_banks;

    uint8_t *ram;
    size_t ram_size;
    unsigned ram_banks;
    bool ram_dirty;

    GbMbc mbc;
    uint8_t type_code;
    bool battery;
    bool has_rtc;
    bool cgb;
    bool cgb_only;
    bool rumble;
    bool header_checksum_ok;

    uint8_t ram_enable;
    uint8_t bank1;
    uint8_t bank2;
    uint8_t ram_bank;
    uint8_t mode;
    uint8_t rtc_select;
    bool latch_armed;

    GbRtc rtc;
    GbRtc rtc_latched;
    uint32_t rtc_cycles;

    char title[17];
    char *rom_path;
    char *sav_path;
    char *rtc_path;
    bool loaded;
} GbCart;

void cart_init(GbCart *cart);
void cart_destroy(GbCart *cart);

/* Both functions replace the current cartridge only when loading succeeds. */
GbStatus cart_load_file(GbCart *cart, const char *path, char *err,
                        size_t err_len);
GbStatus cart_load_data(GbCart *cart, const uint8_t *data, size_t size,
                        const char *path, char *err, size_t err_len);

uint8_t cart_read(GbCart *cart, uint16_t addr);
void cart_write(GbCart *cart, uint16_t addr, uint8_t value);
void cart_tick(GbCart *cart, unsigned cycles);

bool cart_save_battery(GbCart *cart);
const char *cart_mbc_name(const GbCart *cart);

#endif /* CB_CART_H */
