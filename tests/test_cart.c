#include "test.h"

#include "classicboy/gb.h"
#include "test_util.h"

static uint8_t *make_banked_rom(size_t size, uint8_t type, uint8_t rom_code,
                                uint8_t ram_code)
{
    uint8_t *rom = test_rom_make(size, type, rom_code, ram_code);
    if (!rom) {
        return NULL;
    }
    unsigned banks = (unsigned)(size / 0x4000u);
    for (unsigned bank = 0; bank < banks; bank++) {
        rom[(size_t)bank * 0x4000u] = (uint8_t)(bank & 0xFFu);
        rom[(size_t)bank * 0x4000u + 1] = (uint8_t)(bank >> 8);
    }
    return rom;
}

static void test_cart_header(void)
{
    Gb gb;
    test_gb_init(&gb);

    uint8_t *rom = test_rom_make(0x8000, 0x00, 0x00, 0x00);
    TEST_ASSERT(rom != NULL);
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x8000, NULL), GB_OK);
    TEST_EQ_STR(gb_title(&gb), "CLASSICBOY TEST");
    TEST_ASSERT(gb.cart.header_checksum_ok);
    TEST_EQ_INT(gb.cart.mbc, GB_MBC_NONE);
    TEST_EQ_STR(cart_mbc_name(&gb.cart), "ROM");
    TEST_ASSERT(!gb.cart.battery);
    TEST_ASSERT(!gb.cart.cgb);
    TEST_EQ_U16(gb.cpu.pc, 0x0100);

    /* Corrupting the header checksum must not reject the ROM. */
    rom[0x14D] ^= 0xFF;
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x8000, NULL), GB_OK);
    TEST_ASSERT(!gb.cart.header_checksum_ok);

    /* The CGB flag switches the core into Game Boy Color mode. */
    test_rom_set_cgb(rom, false);
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x8000, NULL), GB_OK);
    TEST_ASSERT(gb.cart.cgb);
    TEST_ASSERT(!gb.cart.cgb_only);
    TEST_ASSERT(gb.cgb_mode);
    TEST_ASSERT(gb_is_cgb(&gb));
    TEST_EQ_U8(gb.cpu.a, 0x11);

    /* Bit 6 marks cartridges that only run on Game Boy Color. */
    test_rom_set_cgb(rom, true);
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x8000, NULL), GB_OK);
    TEST_ASSERT(gb.cart.cgb);
    TEST_ASSERT(gb.cart.cgb_only);

    gb_free(rom);
    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cart_rom_only(void)
{
    Gb gb;
    test_gb_init(&gb);

    uint8_t *rom = make_banked_rom(0x8000, 0x00, 0x00, 0x00);
    TEST_ASSERT(rom != NULL);
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x8000, NULL), GB_OK);
    TEST_EQ_U8(gb_read(&gb, 0x0000), 0x00);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x01);
    TEST_EQ_U8(gb_read(&gb, 0x7FFF), 0x00);

    /* Writing to the ROM area must not change the contents. */
    gb_write(&gb, 0x2000, 0x05);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x01);
    gb_write(&gb, 0xA000, 0x12);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0xFF);

    gb_free(rom);
    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cart_mbc1(void)
{
    Gb gb;
    test_gb_init(&gb);

    const size_t size = 0x100000; /* 1 MB, 64 banks */
    uint8_t *rom = make_banked_rom(size, 0x01, 0x05, 0x00);
    TEST_ASSERT(rom != NULL);
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, size, NULL), GB_OK);
    TEST_EQ_INT(gb.cart.mbc, GB_MBC1);
    TEST_EQ_STR(cart_mbc_name(&gb.cart), "MBC1");
    TEST_EQ_U16(gb.cart.rom_banks, 64);

    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x01);
    gb_write(&gb, 0x2000, 0x05);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x05);
    gb_write(&gb, 0x2000, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x01);

    gb_write(&gb, 0x2000, 0x1F);
    gb_write(&gb, 0x4000, 0x01);
    gb_write(&gb, 0x6000, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x3F);
    TEST_EQ_U8(gb_read(&gb, 0x4001), 0x00);

    /* Mode 1 uses the upper bank bits on the 0000-3FFF window. */
    gb_write(&gb, 0x6000, 0x01);
    TEST_EQ_U8(gb_read(&gb, 0x0000), 0x20);
    TEST_EQ_U8(gb_read(&gb, 0x0001), 0x00);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x1F);

    gb_free(rom);
    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cart_mbc1_ram(void)
{
    Gb gb;
    test_gb_init(&gb);

    uint8_t *rom = test_rom_make(0x40000, 0x03, 0x02, 0x03);
    TEST_ASSERT(rom != NULL);
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x40000, NULL), GB_OK);
    TEST_EQ_INT(gb.cart.ram_size, 32768);
    TEST_ASSERT(gb.cart.battery);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0xFF);

    gb_write(&gb, 0x0000, 0x0A);
    gb_write(&gb, 0x4000, 0x02);
    gb_write(&gb, 0x6000, 0x01);
    gb_write(&gb, 0xA000, 0x42);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x42);

    gb_write(&gb, 0x4000, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x00);
    gb_write(&gb, 0x4000, 0x02);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x42);

    gb_write(&gb, 0x0000, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0xFF);

    gb_free(rom);
    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cart_mbc2(void)
{
    Gb gb;
    test_gb_init(&gb);

    uint8_t *rom = make_banked_rom(0x40000, 0x06, 0x02, 0x00);
    TEST_ASSERT(rom != NULL);
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x40000, NULL), GB_OK);
    TEST_EQ_INT(gb.cart.mbc, GB_MBC2);
    TEST_EQ_INT(gb.cart.ram_size, 512);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x01);

    gb_write(&gb, 0x2100, 0x03);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x03);
    gb_write(&gb, 0x0100, 0x05);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x05);

    TEST_EQ_U8(gb_read(&gb, 0xA000), 0xFF);
    gb_write(&gb, 0x0000, 0x0A);
    gb_write(&gb, 0xA000, 0xAB);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0xFB);
    gb_write(&gb, 0xA200, 0x03);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0xF3);
    TEST_EQ_U8(gb_read(&gb, 0xA200), 0xF3);

    gb_write(&gb, 0x0000, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0xFF);

    gb_free(rom);
    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cart_mbc3(void)
{
    Gb gb;
    test_gb_init(&gb);

    uint8_t *rom = make_banked_rom(0x40000, 0x10, 0x02, 0x03);
    TEST_ASSERT(rom != NULL);
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x40000, NULL), GB_OK);
    TEST_EQ_INT(gb.cart.mbc, GB_MBC3);
    TEST_ASSERT(gb.cart.has_rtc);
    TEST_ASSERT(gb.cart.battery);
    TEST_EQ_STR(cart_mbc_name(&gb.cart), "MBC3 + RTC");

    gb_write(&gb, 0x2000, 0x07);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x07);
    gb_write(&gb, 0x2000, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x01);

    gb_write(&gb, 0x0000, 0x0A);
    gb_write(&gb, 0x4000, 0x02);
    gb_write(&gb, 0xA000, 0x5A);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x5A);
    gb_write(&gb, 0x4000, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x00);
    gb_write(&gb, 0x4000, 0x02);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x5A);

    /* RTC registers are only visible after latching. */
    gb_write(&gb, 0x4000, 0x08);
    gb_write(&gb, 0xA000, 30);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 30);
    gb_write(&gb, 0x6000, 0x00);
    gb_write(&gb, 0x6000, 0x01);
    cart_tick(&gb.cart, GB_CLOCK_HZ);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 30);
    gb_write(&gb, 0x6000, 0x00);
    gb_write(&gb, 0x6000, 0x01);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 31);

    gb_write(&gb, 0x4000, 0x0A);
    gb_write(&gb, 0xA000, 12);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 12);
    gb_write(&gb, 0x4000, 0x0C);
    gb_write(&gb, 0xA000, 0x41);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x41);
    gb_write(&gb, 0xA000, 0x40);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x40);
    gb_write(&gb, 0xA000, 0x00);

    gb_write(&gb, 0x0000, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0xFF);

    gb_free(rom);
    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cart_mbc5(void)
{
    Gb gb;
    test_gb_init(&gb);

    const size_t size = 8u * 1024u * 1024u;
    uint8_t *rom = make_banked_rom(size, 0x1B, 0x08, 0x03);
    TEST_ASSERT(rom != NULL);
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, size, NULL), GB_OK);
    TEST_EQ_INT(gb.cart.mbc, GB_MBC5);
    TEST_EQ_U16(gb.cart.rom_banks, 512);
    TEST_EQ_U16(gb.cart.ram_size, 32768);
    TEST_EQ_STR(cart_mbc_name(&gb.cart), "MBC5 + RAM");

    gb_write(&gb, 0x2000, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x00);
    TEST_EQ_U8(gb_read(&gb, 0x4001), 0x00);

    /* 9-bit bank number: low byte at 2000, bit 8 at 3000. */
    gb_write(&gb, 0x2000, 0x50);
    gb_write(&gb, 0x3000, 0x01);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x50);
    TEST_EQ_U8(gb_read(&gb, 0x4001), 0x01);

    gb_write(&gb, 0x3000, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0x4000), 0x50);
    TEST_EQ_U8(gb_read(&gb, 0x4001), 0x00);

    gb_write(&gb, 0x0000, 0x0A);
    gb_write(&gb, 0x4000, 0x03);
    gb_write(&gb, 0xA000, 0x33);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x33);
    gb_write(&gb, 0x4000, 0x00);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x00);
    gb_write(&gb, 0x4000, 0x03);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x33);

    gb_free(rom);
    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cart_mbc5_rumble(void)
{
    Gb gb;
    test_gb_init(&gb);

    /* MBC5 + rumble + RAM + battery with 128 KiB of cartridge RAM. */
    uint8_t *rom = test_rom_make(0x8000, 0x1E, 0x00, 0x04);
    TEST_ASSERT(rom != NULL);
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x8000, NULL), GB_OK);
    TEST_EQ_INT(gb.cart.mbc, GB_MBC5);
    TEST_ASSERT(gb.cart.rumble);
    TEST_ASSERT(gb.cart.battery);
    TEST_EQ_STR(cart_mbc_name(&gb.cart), "MBC5 + Rumble + RAM");

    gb_write(&gb, 0x0000, 0x0A);
    gb_write(&gb, 0x4000, 0x07);
    gb_write(&gb, 0xA000, 0x77);
    gb_write(&gb, 0x4000, 0x00);
    gb_write(&gb, 0xA000, 0x00);

    /* Bit 3 drives the rumble motor, so it must not select a RAM bank. */
    gb_write(&gb, 0x4000, 0x08);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x00);
    gb_write(&gb, 0x4000, 0x0F);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x77);

    gb_free(rom);
    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cart_load_failures(void)
{
    Gb gb;
    test_gb_init(&gb);

    uint8_t *good = test_rom_make(0x8000, 0x00, 0x00, 0x00);
    TEST_ASSERT(good != NULL);
    TEST_EQ_INT(gb_load_rom_data(&gb, good, 0x8000, NULL), GB_OK);
    uint8_t *saved_rom = gb.cart.rom;
    char saved_title[17];
    memcpy(saved_title, gb.cart.title, sizeof(saved_title));

    uint8_t tiny[16] = {0};
    TEST_EQ_INT(gb_load_rom_data(&gb, tiny, sizeof(tiny), NULL), GB_ERR_FORMAT);
    TEST_ASSERT(gb.cart.rom == saved_rom);
    TEST_EQ_STR(gb.cart.title, saved_title);

    uint8_t *bad = test_rom_make(0x8000, 0x99, 0x00, 0x00);
    TEST_ASSERT(bad != NULL);
    TEST_EQ_INT(gb_load_rom_data(&gb, bad, 0x8000, NULL),
                GB_ERR_UNSUPPORTED);
    TEST_ASSERT(gb.cart.rom == saved_rom);
    TEST_EQ_STR(gb.cart.title, saved_title);
    TEST_ASSERT(strstr(gb_error(&gb), "no soportado") != NULL);

    TEST_EQ_INT(gb_load_rom(&gb, "this_file_does_not_exist_12345.gb"),
                GB_ERR_IO);
    TEST_ASSERT(gb.cart.rom == saved_rom);

    gb_free(good);
    gb_free(bad);
    gb_destroy(&gb);
    TEST_ASSERT(test_alloc_clean());
}

static void test_cart_battery(void)
{
    const char *path = "cb_test_battery.gb";
    const char *sav = "cb_test_battery.sav";
    remove(sav);

    uint8_t *rom = test_rom_make(0x8000, 0x03, 0x00, 0x02);
    TEST_ASSERT(rom != NULL);

    Gb gb;
    test_gb_init(&gb);
    TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x8000, path), GB_OK);
    TEST_ASSERT(gb.cart.battery);
    TEST_EQ_STR(gb.cart.sav_path, sav);
    gb_write(&gb, 0x0000, 0x0A);
    gb_write(&gb, 0xA000, 0x77);
    gb_write(&gb, 0xA1FF, 0x99);
    TEST_ASSERT(gb_save_battery(&gb));

    FILE *file = fopen(sav, "rb");
    TEST_ASSERT(file != NULL);
    if (file) {
        fclose(file);
    }

    TEST_EQ_INT(gb_load_rom_data(&gb, rom, 0x8000, path), GB_OK);
    gb_write(&gb, 0x0000, 0x0A);
    TEST_EQ_U8(gb_read(&gb, 0xA000), 0x77);
    TEST_EQ_U8(gb_read(&gb, 0xA1FF), 0x99);

    TEST_ASSERT(gb_save_battery(&gb));
    gb_destroy(&gb);
    gb_free(rom);
    remove(sav);
    TEST_ASSERT(test_alloc_clean());
}

void test_cart_all(void)
{
    run_test("cart/header", test_cart_header);
    run_test("cart/rom-only", test_cart_rom_only);
    run_test("cart/mbc1", test_cart_mbc1);
    run_test("cart/mbc1-ram", test_cart_mbc1_ram);
    run_test("cart/mbc2", test_cart_mbc2);
    run_test("cart/mbc3", test_cart_mbc3);
    run_test("cart/mbc5", test_cart_mbc5);
    run_test("cart/mbc5-rumble", test_cart_mbc5_rumble);
    run_test("cart/load-failures", test_cart_load_failures);
    run_test("cart/battery", test_cart_battery);
}
