#include "classicboy/mmu.h"

#include "classicboy/apu.h"
#include "classicboy/cart.h"
#include "classicboy/gb.h"
#include "classicboy/joypad.h"
#include "classicboy/ppu.h"
#include "classicboy/timer.h"

static unsigned active_wram_bank(const Gb *gb)
{
    if (!gb->cgb_mode) {
        return 1;
    }
    return gb->mmu.wram_bank ? gb->mmu.wram_bank : 1u;
}

static uint8_t read_wram(const Gb *gb, uint16_t addr)
{
    if (addr < 0xD000u) {
        return gb->mmu.wram[addr - 0xC000u];
    }
    return gb->mmu.wram[active_wram_bank(gb) * GB_WRAM_BANK_SIZE +
                       (addr - 0xD000u)];
}

static uint8_t read_echo(const Gb *gb, uint16_t addr)
{
    uint16_t offset = (uint16_t)(addr - 0xE000u);
    if (offset < GB_WRAM_BANK_SIZE) {
        return gb->mmu.wram[offset];
    }
    return gb->mmu.wram[active_wram_bank(gb) * GB_WRAM_BANK_SIZE +
                       (offset - GB_WRAM_BANK_SIZE)];
}

static void write_wram(Gb *gb, uint16_t addr, uint8_t value)
{
    if (addr < 0xD000u) {
        gb->mmu.wram[addr - 0xC000u] = value;
        return;
    }
    gb->mmu.wram[active_wram_bank(gb) * GB_WRAM_BANK_SIZE +
                 (addr - 0xD000u)] = value;
}

static void write_echo(Gb *gb, uint16_t addr, uint8_t value)
{
    uint16_t offset = (uint16_t)(addr - 0xE000u);
    if (offset < GB_WRAM_BANK_SIZE) {
        gb->mmu.wram[offset] = value;
    } else {
        gb->mmu.wram[active_wram_bank(gb) * GB_WRAM_BANK_SIZE +
                     (offset - GB_WRAM_BANK_SIZE)] = value;
    }
}

static uint8_t read_dispatch(Gb *gb, uint16_t addr)
{
    if (addr < 0x8000u) {
        return cart_read(&gb->cart, addr);
    }
    if (addr < 0xA000u) {
        if (!ppu_vram_accessible(&gb->ppu)) {
            return 0xFF;
        }
        return gb->ppu.vram[(gb->ppu.vram_bank & 1u) * GB_VRAM_BANK_SIZE +
                            (addr - 0x8000u)];
    }
    if (addr < 0xC000u) {
        return cart_read(&gb->cart, addr);
    }
    if (addr < 0xE000u) {
        return read_wram(gb, addr);
    }
    if (addr < 0xFE00u) {
        return read_echo(gb, addr);
    }
    if (addr < 0xFEA0u) {
        if (!ppu_oam_accessible(&gb->ppu)) {
            return 0xFF;
        }
        return gb->ppu.oam[addr - 0xFE00u];
    }
    if (addr < 0xFF00u) {
        return 0xFF;
    }
    if (addr < 0xFF80u) {
        switch (addr) {
        case 0xFF00u:
            return joypad_read(&gb->joypad);
        case 0xFF01u:
            return gb->mmu.io[0x01];
        case 0xFF02u:
            return (uint8_t)(0x7Eu | (gb->mmu.io[0x02] & 0x81u));
        case 0xFF04u:
        case 0xFF05u:
        case 0xFF06u:
        case 0xFF07u:
            return timer_read(&gb->timer, addr);
        case 0xFF0Fu:
            return (uint8_t)(0xE0u | (gb->mmu.io[0x0F] & 0x1Fu));
        case 0xFF46u:
            return gb->mmu.io[0x46];
        case 0xFF4Du:
            if (!gb->cgb_mode) {
                return 0xFF;
            }
            return (uint8_t)(0x7Eu | (gb->double_speed ? 0x80u : 0u) |
                             (gb->mmu.io[0x4D] & 0x01u));
        case 0xFF4Fu:
            if (!gb->cgb_mode) {
                return 0xFF;
            }
            return (uint8_t)(0xFEu | (gb->ppu.vram_bank & 1u));
        case 0xFF51u:
        case 0xFF52u:
        case 0xFF53u:
        case 0xFF54u:
        case 0xFF55u:
            if (!gb->cgb_mode) {
                return 0xFF;
            }
            return ppu_read_reg(&gb->ppu, addr);
        case 0xFF68u:
        case 0xFF69u:
        case 0xFF6Au:
        case 0xFF6Bu:
        case 0xFF6Cu:
            if (!gb->cgb_mode) {
                return 0xFF;
            }
            return ppu_read_reg(&gb->ppu, addr);
        case 0xFF70u:
            if (!gb->cgb_mode) {
                return 0xFF;
            }
            return (uint8_t)(0xF8u | (gb->mmu.wram_bank & 0x07u));
        default:
            break;
        }
        if (addr >= 0xFF10u && addr <= 0xFF3Fu) {
            return apu_read_reg(&gb->apu, addr);
        }
        if (addr >= 0xFF40u && addr <= 0xFF4Bu) {
            return ppu_read_reg(&gb->ppu, addr);
        }
        return gb->mmu.io[addr - 0xFF00u];
    }
    if (addr < 0xFFFFu) {
        return gb->mmu.hram[addr - 0xFF80u];
    }
    return gb->mmu.ie;
}

static void write_dispatch(Gb *gb, uint16_t addr, uint8_t value)
{
    if (addr < 0x8000u) {
        cart_write(&gb->cart, addr, value);
        return;
    }
    if (addr < 0xA000u) {
        if (ppu_vram_accessible(&gb->ppu)) {
            gb->ppu.vram[(gb->ppu.vram_bank & 1u) * GB_VRAM_BANK_SIZE +
                         (addr - 0x8000u)] = value;
        }
        return;
    }
    if (addr < 0xC000u) {
        cart_write(&gb->cart, addr, value);
        return;
    }
    if (addr < 0xE000u) {
        write_wram(gb, addr, value);
        return;
    }
    if (addr < 0xFE00u) {
        write_echo(gb, addr, value);
        return;
    }
    if (addr < 0xFEA0u) {
        if (ppu_oam_accessible(&gb->ppu)) {
            gb->ppu.oam[addr - 0xFE00u] = value;
        }
        return;
    }
    if (addr < 0xFF00u) {
        return;
    }
    if (addr < 0xFF80u) {
        uint8_t offset = (uint8_t)(addr - 0xFF00u);
        switch (addr) {
        case 0xFF00u:
            joypad_write(gb, &gb->joypad, value);
            return;
        case 0xFF01u:
            gb->mmu.io[0x01] = value;
            return;
        case 0xFF02u:
            gb->mmu.io[0x02] = value;
            if (value & 0x80u) {
                if (value & 0x01u) {
                    gb->mmu.serial_active = true;
                    gb->mmu.serial_cycles = 0;
                }
            } else {
                gb->mmu.serial_active = false;
            }
            return;
        case 0xFF04u:
        case 0xFF05u:
        case 0xFF06u:
        case 0xFF07u:
            timer_write(gb, &gb->timer, addr, value);
            return;
        case 0xFF0Fu:
            gb->mmu.io[0x0F] = value & 0x1Fu;
            return;
        case 0xFF46u:
            gb->mmu.io[0x46] = value;
            gb->mmu.dma_active = true;
            gb->mmu.dma_source_high = value;
            gb->mmu.dma_index = 0;
            gb->mmu.dma_cycle = 0;
            return;
        case 0xFF4Du:
            if (!gb->cgb_mode) {
                return;
            }
            gb->mmu.io[0x4D] = value & 0x01u;
            return;
        case 0xFF4Fu:
            if (!gb->cgb_mode) {
                return;
            }
            gb->ppu.vram_bank = value & 0x01u;
            return;
        case 0xFF51u:
        case 0xFF52u:
        case 0xFF53u:
        case 0xFF54u:
        case 0xFF55u:
            if (!gb->cgb_mode) {
                return;
            }
            ppu_write_reg(gb, &gb->ppu, addr, value);
            return;
        case 0xFF68u:
        case 0xFF69u:
        case 0xFF6Au:
        case 0xFF6Bu:
        case 0xFF6Cu:
            if (!gb->cgb_mode) {
                return;
            }
            ppu_write_reg(gb, &gb->ppu, addr, value);
            return;
        case 0xFF70u:
            if (!gb->cgb_mode) {
                return;
            }
            gb->mmu.wram_bank = value & 0x07u;
            return;
        default:
            break;
        }
        if (addr >= 0xFF10u && addr <= 0xFF3Fu) {
            apu_write_reg(&gb->apu, addr, value);
            return;
        }
        if (addr >= 0xFF40u && addr <= 0xFF4Bu) {
            ppu_write_reg(gb, &gb->ppu, addr, value);
            return;
        }
        gb->mmu.io[offset] = value;
        return;
    }
    if (addr < 0xFFFFu) {
        gb->mmu.hram[addr - 0xFF80u] = value;
        return;
    }
    gb->mmu.ie = value & 0x1Fu;
}

void mmu_reset(Gb *gb)
{
    GbMmu *mmu = &gb->mmu;
    memset(mmu, 0, sizeof(*mmu));
    memset(mmu->io, 0xFF, sizeof(mmu->io));
    mmu->io[0x00] = 0xCF;
    mmu->io[0x01] = 0x00;
    mmu->io[0x02] = 0x7E;
    mmu->io[0x0F] = 0xE1;
    mmu->io[0x40] = 0x91;
    mmu->io[0x47] = 0xFC;
    mmu->io[0x48] = 0xFF;
    mmu->io[0x49] = 0xFF;
    mmu->ie = 0;
    mmu->wram_bank = 0;

    if (gb->cgb_mode) {
        mmu->io[0x4D] = 0x00;
        mmu->io[0x4F] = 0xFE;
        mmu->io[0x51] = 0xFF;
        mmu->io[0x52] = 0xFF;
        mmu->io[0x53] = 0xFF;
        mmu->io[0x54] = 0xFF;
        mmu->io[0x55] = 0xFF;
        mmu->io[0x68] = 0x00;
        mmu->io[0x69] = 0x00;
        mmu->io[0x6A] = 0x00;
        mmu->io[0x6B] = 0x00;
        mmu->io[0x6C] = 0x00;
        mmu->io[0x70] = 0xF8;
    }
}

uint8_t gb_read(Gb *gb, uint16_t addr)
{
    if (gb->mmu.dma_active && addr < 0xFF80u) {
        return 0xFF;
    }
    return read_dispatch(gb, addr);
}

void gb_write(Gb *gb, uint16_t addr, uint8_t value)
{
    if (gb->mmu.dma_active && addr < 0xFF80u) {
        return;
    }
    write_dispatch(gb, addr, value);
}

void gb_request_interrupt(Gb *gb, unsigned bit)
{
    if (bit < 5u) {
        gb->mmu.io[0x0F] |= (uint8_t)(1u << bit);
    }
}

void mmu_tick(Gb *gb, unsigned cycles)
{
    GbMmu *mmu = &gb->mmu;
    if (mmu->dma_active) {
        for (unsigned i = 0; i < cycles; i++) {
            mmu->dma_cycle++;
            if (mmu->dma_cycle >= 4u) {
                mmu->dma_cycle = 0;
                if (mmu->dma_index < 160u) {
                    uint16_t source =
                        (uint16_t)(((uint16_t)mmu->dma_source_high << 8) |
                                   mmu->dma_index);
                    gb->ppu.oam[mmu->dma_index] = read_dispatch(gb, source);
                    mmu->dma_index++;
                    if (mmu->dma_index >= 160u) {
                        mmu->dma_active = false;
                    }
                }
            }
        }
    }
    if (mmu->serial_active) {
        mmu->serial_cycles += cycles;
        if (mmu->serial_cycles >= 4096u) {
            mmu->serial_active = false;
            mmu->serial_cycles = 0;
            uint8_t byte = mmu->io[0x01];
            gb->serial_last = byte;
            mmu->io[0x01] = 0xFF;
            mmu->io[0x02] &= 0x7Fu;
            gb_request_interrupt(gb, 3);
            if (gb->serial_output) {
                putchar((int)byte);
                fflush(stdout);
            }
        }
    }
}
