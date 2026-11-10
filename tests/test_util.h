#ifndef CB_TEST_UTIL_H
#define CB_TEST_UTIL_H

#include "classicboy/gb.h"

void test_gb_init(Gb *gb);
uint8_t *test_rom_make(size_t size, uint8_t type, uint8_t rom_code,
                       uint8_t ram_code);
void test_rom_set_code(uint8_t *rom, const uint8_t *code, size_t length);
void test_rom_set_cgb(uint8_t *rom, bool cgb_only);
void test_install_code(Gb *gb, const uint8_t *code, size_t length);
unsigned test_step(Gb *gb);
void test_run_steps(Gb *gb, int count);
bool test_alloc_clean(void);

#endif /* CB_TEST_UTIL_H */
