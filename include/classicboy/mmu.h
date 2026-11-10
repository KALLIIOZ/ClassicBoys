/*
 * ClassicBoy - memory bus and I/O dispatch.
 */
#ifndef CB_MMU_H
#define CB_MMU_H

#include "classicboy/common.h"

struct Gb;

typedef struct GbMmu {
    /*
     * Eight 4 KiB WRAM banks. Bank 0 is always mapped at 0xC000 and the
     * selected bank (1-7 on CGB, always 1 on DMG) at 0xD000.
     */
    uint8_t wram[GB_WRAM_BANKS * GB_WRAM_BANK_SIZE];
    uint8_t hram[0x7F];
    uint8_t io[0x80];
    uint8_t ie;

    /* Raw SVBK value (0 is treated as 1 when selecting the bank). */
    uint8_t wram_bank;

    bool dma_active;
    uint8_t dma_source_high;
    uint8_t dma_index;
    uint8_t dma_cycle;

    bool serial_active;
    uint32_t serial_cycles;
} GbMmu;

void mmu_reset(struct Gb *gb);
uint8_t gb_read(struct Gb *gb, uint16_t addr);
void gb_write(struct Gb *gb, uint16_t addr, uint8_t value);
void mmu_tick(struct Gb *gb, unsigned cycles);

#endif /* CB_MMU_H */
