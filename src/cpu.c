#include "classicboy/cpu.h"

#include "classicboy/gb.h"
#include "classicboy/mmu.h"

#define FLAG_Z 0x80u
#define FLAG_N 0x40u
#define FLAG_H 0x20u
#define FLAG_C 0x10u

static uint8_t fetch8(Gb *gb, GbCpu *cpu)
{
    uint8_t value = gb_read(gb, cpu->pc);
    if (cpu->halt_bug) {
        cpu->halt_bug = false;
    } else {
        cpu->pc++;
    }
    return value;
}

static uint16_t fetch16(Gb *gb, GbCpu *cpu)
{
    uint8_t low = fetch8(gb, cpu);
    uint8_t high = fetch8(gb, cpu);
    return (uint16_t)(low | ((uint16_t)high << 8));
}

static uint16_t read16_word(Gb *gb, uint16_t addr)
{
    uint8_t low = gb_read(gb, addr);
    uint8_t high = gb_read(gb, (uint16_t)(addr + 1));
    return (uint16_t)(low | ((uint16_t)high << 8));
}

static void write16(Gb *gb, uint16_t addr, uint16_t value)
{
    gb_write(gb, addr, (uint8_t)(value & 0xFF));
    gb_write(gb, (uint16_t)(addr + 1), (uint8_t)(value >> 8));
}

static uint16_t hl(const GbCpu *cpu)
{
    return (uint16_t)(((uint16_t)cpu->h << 8) | cpu->l);
}

static void set_hl(GbCpu *cpu, uint16_t value)
{
    cpu->h = (uint8_t)(value >> 8);
    cpu->l = (uint8_t)(value & 0xFF);
}

static uint8_t read_reg(Gb *gb, GbCpu *cpu, int index)
{
    switch (index) {
    case 0:
        return cpu->b;
    case 1:
        return cpu->c;
    case 2:
        return cpu->d;
    case 3:
        return cpu->e;
    case 4:
        return cpu->h;
    case 5:
        return cpu->l;
    case 6:
        return gb_read(gb, hl(cpu));
    default:
        return cpu->a;
    }
}

static void write_reg(Gb *gb, GbCpu *cpu, int index, uint8_t value)
{
    switch (index) {
    case 0:
        cpu->b = value;
        break;
    case 1:
        cpu->c = value;
        break;
    case 2:
        cpu->d = value;
        break;
    case 3:
        cpu->e = value;
        break;
    case 4:
        cpu->h = value;
        break;
    case 5:
        cpu->l = value;
        break;
    case 6:
        gb_write(gb, hl(cpu), value);
        break;
    default:
        cpu->a = value;
        break;
    }
}

static bool flag(const GbCpu *cpu, uint8_t mask)
{
    return (cpu->f & mask) != 0;
}

static void set_flag(GbCpu *cpu, uint8_t mask, bool value)
{
    if (value) {
        cpu->f |= mask;
    } else {
        cpu->f &= (uint8_t)~mask;
    }
}

static bool condition(const GbCpu *cpu, int index)
{
    switch (index) {
    case 0:
        return !flag(cpu, FLAG_Z);
    case 1:
        return flag(cpu, FLAG_Z);
    case 2:
        return !flag(cpu, FLAG_C);
    default:
        return flag(cpu, FLAG_C);
    }
}

static void push16(Gb *gb, GbCpu *cpu, uint16_t value)
{
    cpu->sp = (uint16_t)(cpu->sp - 2);
    write16(gb, cpu->sp, value);
}

static uint16_t pop16(Gb *gb, GbCpu *cpu)
{
    uint16_t value = read16_word(gb, cpu->sp);
    cpu->sp = (uint16_t)(cpu->sp + 2);
    return value;
}

static void finish_step(GbCpu *cpu, unsigned cycles)
{
    (void)cycles;
    if (cpu->ei_delay > 0) {
        cpu->ei_delay--;
        if (cpu->ei_delay == 0) {
            cpu->ime = true;
        }
    }
    cpu->instructions++;
}

static unsigned service_interrupt(Gb *gb, GbCpu *cpu, uint8_t pending)
{
    unsigned bit = 0;
    while (bit < 5 && (pending & (1u << bit)) == 0) {
        bit++;
    }
    uint16_t vector = (uint16_t)(0x40u + bit * 8u);
    push16(gb, cpu, cpu->pc);
    gb->mmu.io[0x0F] &= (uint8_t)~(1u << bit);
    cpu->pc = vector;
    cpu->ime = false;
    cpu->ei_delay = 0;
    return 20;
}

static void alu_add(GbCpu *cpu, uint8_t value, bool with_carry)
{
    uint8_t carry = with_carry && flag(cpu, FLAG_C) ? 1u : 0u;
    unsigned result = (unsigned)cpu->a + value + carry;
    set_flag(cpu, FLAG_Z, (result & 0xFFu) == 0);
    set_flag(cpu, FLAG_N, false);
    set_flag(cpu, FLAG_H,
             ((cpu->a & 0x0Fu) + (value & 0x0Fu) + carry) > 0x0Fu);
    set_flag(cpu, FLAG_C, result > 0xFFu);
    cpu->a = (uint8_t)result;
}

static void alu_sub(GbCpu *cpu, uint8_t value, bool with_carry, bool store)
{
    uint8_t carry = with_carry && flag(cpu, FLAG_C) ? 1u : 0u;
    int result = (int)cpu->a - (int)value - (int)carry;
    set_flag(cpu, FLAG_Z, (result & 0xFF) == 0);
    set_flag(cpu, FLAG_N, true);
    set_flag(cpu, FLAG_H,
             ((int)(cpu->a & 0x0Fu) - (int)(value & 0x0Fu) - (int)carry) < 0);
    set_flag(cpu, FLAG_C, result < 0);
    if (store) {
        cpu->a = (uint8_t)result;
    }
}

static void alu_and(GbCpu *cpu, uint8_t value)
{
    cpu->a &= value;
    set_flag(cpu, FLAG_Z, cpu->a == 0);
    set_flag(cpu, FLAG_N, false);
    set_flag(cpu, FLAG_H, true);
    set_flag(cpu, FLAG_C, false);
}

static void alu_or(GbCpu *cpu, uint8_t value)
{
    cpu->a |= value;
    set_flag(cpu, FLAG_Z, cpu->a == 0);
    set_flag(cpu, FLAG_N, false);
    set_flag(cpu, FLAG_H, false);
    set_flag(cpu, FLAG_C, false);
}

static void alu_xor(GbCpu *cpu, uint8_t value)
{
    cpu->a ^= value;
    set_flag(cpu, FLAG_Z, cpu->a == 0);
    set_flag(cpu, FLAG_N, false);
    set_flag(cpu, FLAG_H, false);
    set_flag(cpu, FLAG_C, false);
}

static uint8_t inc8(GbCpu *cpu, uint8_t value)
{
    uint8_t result = (uint8_t)(value + 1u);
    set_flag(cpu, FLAG_Z, result == 0);
    set_flag(cpu, FLAG_N, false);
    set_flag(cpu, FLAG_H, (value & 0x0Fu) == 0x0Fu);
    return result;
}

static uint8_t dec8(GbCpu *cpu, uint8_t value)
{
    uint8_t result = (uint8_t)(value - 1u);
    set_flag(cpu, FLAG_Z, result == 0);
    set_flag(cpu, FLAG_N, true);
    set_flag(cpu, FLAG_H, (value & 0x0Fu) == 0);
    return result;
}

static void add_hl(GbCpu *cpu, uint16_t value)
{
    uint16_t current = hl(cpu);
    unsigned result = (unsigned)current + value;
    set_flag(cpu, FLAG_N, false);
    set_flag(cpu, FLAG_H, ((current & 0x0FFFu) + (value & 0x0FFFu)) > 0x0FFFu);
    set_flag(cpu, FLAG_C, result > 0xFFFFu);
    set_hl(cpu, (uint16_t)result);
}

static void daa(GbCpu *cpu)
{
    uint8_t correction = 0;
    bool carry = flag(cpu, FLAG_C);
    bool subtract = flag(cpu, FLAG_N);
    if (!subtract) {
        if (flag(cpu, FLAG_H) || (cpu->a & 0x0Fu) > 9u) {
            correction |= 0x06u;
        }
        if (carry || cpu->a > 0x99u) {
            correction |= 0x60u;
            carry = true;
        }
        cpu->a = (uint8_t)(cpu->a + correction);
    } else {
        if (flag(cpu, FLAG_H)) {
            correction |= 0x06u;
        }
        if (carry) {
            correction |= 0x60u;
        }
        cpu->a = (uint8_t)(cpu->a - correction);
    }
    set_flag(cpu, FLAG_Z, cpu->a == 0);
    set_flag(cpu, FLAG_H, false);
    set_flag(cpu, FLAG_C, carry);
    set_flag(cpu, FLAG_N, subtract);
}

static unsigned execute_cb(Gb *gb, GbCpu *cpu)
{
    uint8_t opcode = fetch8(gb, cpu);
    int group = (opcode >> 6) & 3;
    int bit = (opcode >> 3) & 7;
    int reg = opcode & 7;
    uint8_t value = read_reg(gb, cpu, reg);

    if (group == 1) {
        set_flag(cpu, FLAG_Z, (value & (1u << bit)) == 0);
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, true);
        return reg == 6 ? 12u : 8u;
    }
    if (group == 2 || group == 3) {
        uint8_t result = group == 2 ? (uint8_t)(value & (uint8_t)~(1u << bit))
                                    : (uint8_t)(value | (1u << bit));
        write_reg(gb, cpu, reg, result);
        return reg == 6 ? 16u : 8u;
    }

    uint8_t result = value;
    bool carry = flag(cpu, FLAG_C);
    bool new_carry = carry;
    switch ((opcode >> 3) & 7) {
    case 0: /* RLC */
        new_carry = (value & 0x80u) != 0;
        result = (uint8_t)((value << 1) | (new_carry ? 1u : 0u));
        break;
    case 1: /* RRC */
        new_carry = (value & 0x01u) != 0;
        result = (uint8_t)((value >> 1) | (new_carry ? 0x80u : 0u));
        break;
    case 2: /* RL */
        new_carry = (value & 0x80u) != 0;
        result = (uint8_t)((value << 1) | (carry ? 1u : 0u));
        break;
    case 3: /* RR */
        new_carry = (value & 0x01u) != 0;
        result = (uint8_t)((value >> 1) | (carry ? 0x80u : 0u));
        break;
    case 4: /* SLA */
        new_carry = (value & 0x80u) != 0;
        result = (uint8_t)(value << 1);
        break;
    case 5: /* SRA */
        new_carry = (value & 0x01u) != 0;
        result = (uint8_t)((value >> 1) | (value & 0x80u));
        break;
    case 6: /* SWAP */
        new_carry = false;
        result = (uint8_t)((value << 4) | (value >> 4));
        break;
    default: /* SRL */
        new_carry = (value & 0x01u) != 0;
        result = (uint8_t)(value >> 1);
        break;
    }
    write_reg(gb, cpu, reg, result);
    set_flag(cpu, FLAG_Z, result == 0);
    set_flag(cpu, FLAG_N, false);
    set_flag(cpu, FLAG_H, false);
    set_flag(cpu, FLAG_C, new_carry);
    return reg == 6 ? 16u : 8u;
}

static unsigned execute_op(Gb *gb, GbCpu *cpu, uint8_t opcode)
{
    if (opcode >= 0x40u && opcode <= 0x7Fu) {
        if (opcode == 0x76u) {
            uint8_t pending = (uint8_t)(gb->mmu.ie & gb->mmu.io[0x0F] & 0x1Fu);
            if (pending == 0) {
                cpu->halted = true;
            } else {
                cpu->halt_bug = true;
            }
            return 4;
        }
        int dst = (opcode >> 3) & 7;
        int src = opcode & 7;
        uint8_t value = read_reg(gb, cpu, src);
        write_reg(gb, cpu, dst, value);
        return (dst == 6 || src == 6) ? 8u : 4u;
    }

    if (opcode >= 0x80u && opcode <= 0xBFu) {
        int group = (opcode >> 3) & 7;
        int src = opcode & 7;
        uint8_t value = read_reg(gb, cpu, src);
        switch (group) {
        case 0:
            alu_add(cpu, value, false);
            break;
        case 1:
            alu_add(cpu, value, true);
            break;
        case 2:
            alu_sub(cpu, value, false, true);
            break;
        case 3:
            alu_sub(cpu, value, true, true);
            break;
        case 4:
            alu_and(cpu, value);
            break;
        case 5:
            alu_xor(cpu, value);
            break;
        case 6:
            alu_or(cpu, value);
            break;
        default:
            alu_sub(cpu, value, false, false);
            break;
        }
        return src == 6 ? 8u : 4u;
    }

    switch (opcode) {
    case 0x00:
        return 4;
    case 0x01: {
        uint16_t value = fetch16(gb, cpu);
        cpu->b = (uint8_t)(value >> 8);
        cpu->c = (uint8_t)(value & 0xFF);
        return 12;
    }
    case 0x02:
        gb_write(gb, (uint16_t)(((uint16_t)cpu->b << 8) | cpu->c), cpu->a);
        return 8;
    case 0x03: {
        uint16_t value = (uint16_t)((((uint16_t)cpu->b << 8) | cpu->c) + 1u);
        cpu->b = (uint8_t)(value >> 8);
        cpu->c = (uint8_t)(value & 0xFF);
        return 8;
    }
    case 0x04:
        cpu->b = inc8(cpu, cpu->b);
        return 4;
    case 0x05:
        cpu->b = dec8(cpu, cpu->b);
        return 4;
    case 0x06:
        cpu->b = fetch8(gb, cpu);
        return 8;
    case 0x07: {
        bool carry = (cpu->a & 0x80u) != 0;
        cpu->a = (uint8_t)((cpu->a << 1) | (carry ? 1u : 0u));
        set_flag(cpu, FLAG_Z, false);
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, false);
        set_flag(cpu, FLAG_C, carry);
        return 4;
    }
    case 0x08: {
        uint16_t addr = fetch16(gb, cpu);
        write16(gb, addr, cpu->sp);
        return 20;
    }
    case 0x09:
        add_hl(cpu, (uint16_t)(((uint16_t)cpu->b << 8) | cpu->c));
        return 8;
    case 0x0A:
        cpu->a = gb_read(gb, (uint16_t)(((uint16_t)cpu->b << 8) | cpu->c));
        return 8;
    case 0x0B: {
        uint16_t value = (uint16_t)((((uint16_t)cpu->b << 8) | cpu->c) - 1u);
        cpu->b = (uint8_t)(value >> 8);
        cpu->c = (uint8_t)(value & 0xFF);
        return 8;
    }
    case 0x0C:
        cpu->c = inc8(cpu, cpu->c);
        return 4;
    case 0x0D:
        cpu->c = dec8(cpu, cpu->c);
        return 4;
    case 0x0E:
        cpu->c = fetch8(gb, cpu);
        return 8;
    case 0x0F: {
        bool carry = (cpu->a & 0x01u) != 0;
        cpu->a = (uint8_t)((cpu->a >> 1) | (carry ? 0x80u : 0u));
        set_flag(cpu, FLAG_Z, false);
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, false);
        set_flag(cpu, FLAG_C, carry);
        return 4;
    }
    case 0x10: {
        /* STOP is two bytes: opcode + padding. On CGB it also performs the
         * KEY1 double-speed switch when bit 0 of KEY1 was armed. */
        cpu->pc++;
        if (gb->cgb_mode && (gb->mmu.io[0x4D] & 0x01u) != 0) {
            gb_switch_speed(gb);
        }
        return 4;
    }
    case 0x11: {
        uint16_t value = fetch16(gb, cpu);
        cpu->d = (uint8_t)(value >> 8);
        cpu->e = (uint8_t)(value & 0xFF);
        return 12;
    }
    case 0x12:
        gb_write(gb, (uint16_t)(((uint16_t)cpu->d << 8) | cpu->e), cpu->a);
        return 8;
    case 0x13: {
        uint16_t value = (uint16_t)((((uint16_t)cpu->d << 8) | cpu->e) + 1u);
        cpu->d = (uint8_t)(value >> 8);
        cpu->e = (uint8_t)(value & 0xFF);
        return 8;
    }
    case 0x14:
        cpu->d = inc8(cpu, cpu->d);
        return 4;
    case 0x15:
        cpu->d = dec8(cpu, cpu->d);
        return 4;
    case 0x16:
        cpu->d = fetch8(gb, cpu);
        return 8;
    case 0x17: {
        bool carry = (cpu->a & 0x80u) != 0;
        cpu->a = (uint8_t)((cpu->a << 1) | (flag(cpu, FLAG_C) ? 1u : 0u));
        set_flag(cpu, FLAG_Z, false);
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, false);
        set_flag(cpu, FLAG_C, carry);
        return 4;
    }
    case 0x18: {
        int8_t offset = (int8_t)fetch8(gb, cpu);
        cpu->pc = (uint16_t)(cpu->pc + offset);
        return 12;
    }
    case 0x19:
        add_hl(cpu, (uint16_t)(((uint16_t)cpu->d << 8) | cpu->e));
        return 8;
    case 0x1A:
        cpu->a = gb_read(gb, (uint16_t)(((uint16_t)cpu->d << 8) | cpu->e));
        return 8;
    case 0x1B: {
        uint16_t value = (uint16_t)((((uint16_t)cpu->d << 8) | cpu->e) - 1u);
        cpu->d = (uint8_t)(value >> 8);
        cpu->e = (uint8_t)(value & 0xFF);
        return 8;
    }
    case 0x1C:
        cpu->e = inc8(cpu, cpu->e);
        return 4;
    case 0x1D:
        cpu->e = dec8(cpu, cpu->e);
        return 4;
    case 0x1E:
        cpu->e = fetch8(gb, cpu);
        return 8;
    case 0x1F: {
        bool carry = (cpu->a & 0x01u) != 0;
        cpu->a = (uint8_t)((cpu->a >> 1) | (flag(cpu, FLAG_C) ? 0x80u : 0u));
        set_flag(cpu, FLAG_Z, false);
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, false);
        set_flag(cpu, FLAG_C, carry);
        return 4;
    }
    case 0x20:
    case 0x28:
    case 0x30:
    case 0x38: {
        int8_t offset = (int8_t)fetch8(gb, cpu);
        int cond = (opcode >> 3) & 3;
        if (condition(cpu, cond)) {
            cpu->pc = (uint16_t)(cpu->pc + offset);
            return 12;
        }
        return 8;
    }
    case 0x21: {
        uint16_t value = fetch16(gb, cpu);
        set_hl(cpu, value);
        return 12;
    }
    case 0x22: {
        uint16_t addr = hl(cpu);
        gb_write(gb, addr, cpu->a);
        set_hl(cpu, (uint16_t)(addr + 1u));
        return 8;
    }
    case 0x23:
        set_hl(cpu, (uint16_t)(hl(cpu) + 1u));
        return 8;
    case 0x24:
        cpu->h = inc8(cpu, cpu->h);
        return 4;
    case 0x25:
        cpu->h = dec8(cpu, cpu->h);
        return 4;
    case 0x26:
        cpu->h = fetch8(gb, cpu);
        return 8;
    case 0x27:
        daa(cpu);
        return 4;
    case 0x29:
        add_hl(cpu, hl(cpu));
        return 8;
    case 0x2A: {
        uint16_t addr = hl(cpu);
        cpu->a = gb_read(gb, addr);
        set_hl(cpu, (uint16_t)(addr + 1u));
        return 8;
    }
    case 0x2B:
        set_hl(cpu, (uint16_t)(hl(cpu) - 1u));
        return 8;
    case 0x2C:
        cpu->l = inc8(cpu, cpu->l);
        return 4;
    case 0x2D:
        cpu->l = dec8(cpu, cpu->l);
        return 4;
    case 0x2E:
        cpu->l = fetch8(gb, cpu);
        return 8;
    case 0x2F:
        cpu->a = (uint8_t)~cpu->a;
        set_flag(cpu, FLAG_N, true);
        set_flag(cpu, FLAG_H, true);
        return 4;
    case 0x31:
        cpu->sp = fetch16(gb, cpu);
        return 12;
    case 0x32: {
        uint16_t addr = hl(cpu);
        gb_write(gb, addr, cpu->a);
        set_hl(cpu, (uint16_t)(addr - 1u));
        return 8;
    }
    case 0x33:
        cpu->sp = (uint16_t)(cpu->sp + 1u);
        return 8;
    case 0x34: {
        uint16_t addr = hl(cpu);
        uint8_t value = gb_read(gb, addr);
        gb_write(gb, addr, inc8(cpu, value));
        return 12;
    }
    case 0x35: {
        uint16_t addr = hl(cpu);
        uint8_t value = gb_read(gb, addr);
        gb_write(gb, addr, dec8(cpu, value));
        return 12;
    }
    case 0x36:
        gb_write(gb, hl(cpu), fetch8(gb, cpu));
        return 12;
    case 0x37:
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, false);
        set_flag(cpu, FLAG_C, true);
        return 4;
    case 0x39:
        add_hl(cpu, cpu->sp);
        return 8;
    case 0x3A: {
        uint16_t addr = hl(cpu);
        cpu->a = gb_read(gb, addr);
        set_hl(cpu, (uint16_t)(addr - 1u));
        return 8;
    }
    case 0x3B:
        cpu->sp = (uint16_t)(cpu->sp - 1u);
        return 8;
    case 0x3C:
        cpu->a = inc8(cpu, cpu->a);
        return 4;
    case 0x3D:
        cpu->a = dec8(cpu, cpu->a);
        return 4;
    case 0x3E:
        cpu->a = fetch8(gb, cpu);
        return 8;
    case 0x3F: {
        bool carry = flag(cpu, FLAG_C);
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, false);
        set_flag(cpu, FLAG_C, !carry);
        return 4;
    }
    case 0xC0:
    case 0xC8:
    case 0xD0:
    case 0xD8: {
        int cond = (opcode >> 3) & 3;
        if (condition(cpu, cond)) {
            cpu->pc = pop16(gb, cpu);
            return 20;
        }
        return 8;
    }
    case 0xC1: {
        uint16_t value = pop16(gb, cpu);
        cpu->b = (uint8_t)(value >> 8);
        cpu->c = (uint8_t)(value & 0xFF);
        return 12;
    }
    case 0xC2:
    case 0xCA:
    case 0xD2:
    case 0xDA: {
        uint16_t addr = fetch16(gb, cpu);
        int cond = (opcode >> 3) & 3;
        if (condition(cpu, cond)) {
            cpu->pc = addr;
            return 16;
        }
        return 12;
    }
    case 0xC3:
        cpu->pc = fetch16(gb, cpu);
        return 16;
    case 0xC4:
    case 0xCC:
    case 0xD4:
    case 0xDC: {
        uint16_t addr = fetch16(gb, cpu);
        int cond = (opcode >> 3) & 3;
        if (condition(cpu, cond)) {
            push16(gb, cpu, cpu->pc);
            cpu->pc = addr;
            return 24;
        }
        return 12;
    }
    case 0xC5:
        push16(gb, cpu, (uint16_t)(((uint16_t)cpu->b << 8) | cpu->c));
        return 16;
    case 0xC6:
        alu_add(cpu, fetch8(gb, cpu), false);
        return 8;
    case 0xC7:
    case 0xCF:
    case 0xD7:
    case 0xDF:
    case 0xE7:
    case 0xEF:
    case 0xF7:
    case 0xFF:
        push16(gb, cpu, cpu->pc);
        cpu->pc = (uint16_t)(opcode & 0x38u);
        return 16;
    case 0xC9:
        cpu->pc = pop16(gb, cpu);
        return 16;
    case 0xCB:
        return execute_cb(gb, cpu);
    case 0xCD: {
        uint16_t addr = fetch16(gb, cpu);
        push16(gb, cpu, cpu->pc);
        cpu->pc = addr;
        return 24;
    }
    case 0xCE:
        alu_add(cpu, fetch8(gb, cpu), true);
        return 8;
    case 0xD1: {
        uint16_t value = pop16(gb, cpu);
        cpu->d = (uint8_t)(value >> 8);
        cpu->e = (uint8_t)(value & 0xFF);
        return 12;
    }
    case 0xD5:
        push16(gb, cpu, (uint16_t)(((uint16_t)cpu->d << 8) | cpu->e));
        return 16;
    case 0xD6:
        alu_sub(cpu, fetch8(gb, cpu), false, true);
        return 8;
    case 0xD9:
        cpu->pc = pop16(gb, cpu);
        cpu->ime = true;
        cpu->ei_delay = 0;
        return 16;
    case 0xDE:
        alu_sub(cpu, fetch8(gb, cpu), true, true);
        return 8;
    case 0xE0: {
        uint8_t offset = fetch8(gb, cpu);
        gb_write(gb, (uint16_t)(0xFF00u | offset), cpu->a);
        return 12;
    }
    case 0xE1: {
        uint16_t value = pop16(gb, cpu);
        set_hl(cpu, value);
        return 12;
    }
    case 0xE2:
        gb_write(gb, (uint16_t)(0xFF00u | cpu->c), cpu->a);
        return 8;
    case 0xE5:
        push16(gb, cpu, hl(cpu));
        return 16;
    case 0xE6:
        alu_and(cpu, fetch8(gb, cpu));
        return 8;
    case 0xE8: {
        uint8_t offset = fetch8(gb, cpu);
        uint16_t sp = cpu->sp;
        uint16_t result = (uint16_t)(sp + (int16_t)(int8_t)offset);
        set_flag(cpu, FLAG_Z, false);
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, ((sp & 0x0Fu) + (offset & 0x0Fu)) > 0x0Fu);
        set_flag(cpu, FLAG_C, ((sp & 0xFFu) + (offset & 0xFFu)) > 0xFFu);
        cpu->sp = result;
        return 16;
    }
    case 0xE9:
        cpu->pc = hl(cpu);
        return 4;
    case 0xEA: {
        uint16_t addr = fetch16(gb, cpu);
        gb_write(gb, addr, cpu->a);
        return 16;
    }
    case 0xEE:
        alu_xor(cpu, fetch8(gb, cpu));
        return 8;
    case 0xF0: {
        uint8_t offset = fetch8(gb, cpu);
        cpu->a = gb_read(gb, (uint16_t)(0xFF00u | offset));
        return 12;
    }
    case 0xF1: {
        uint16_t value = pop16(gb, cpu);
        cpu->a = (uint8_t)(value >> 8);
        cpu->f = (uint8_t)(value & 0xF0u);
        return 12;
    }
    case 0xF2:
        cpu->a = gb_read(gb, (uint16_t)(0xFF00u | cpu->c));
        return 8;
    case 0xF3:
        cpu->ime = false;
        cpu->ei_delay = 0;
        return 4;
    case 0xF5:
        push16(gb, cpu, (uint16_t)(((uint16_t)cpu->a << 8) | cpu->f));
        return 16;
    case 0xF6:
        alu_or(cpu, fetch8(gb, cpu));
        return 8;
    case 0xF8: {
        uint8_t offset = fetch8(gb, cpu);
        uint16_t sp = cpu->sp;
        uint16_t result = (uint16_t)(sp + (int16_t)(int8_t)offset);
        set_flag(cpu, FLAG_Z, false);
        set_flag(cpu, FLAG_N, false);
        set_flag(cpu, FLAG_H, ((sp & 0x0Fu) + (offset & 0x0Fu)) > 0x0Fu);
        set_flag(cpu, FLAG_C, ((sp & 0xFFu) + (offset & 0xFFu)) > 0xFFu);
        set_hl(cpu, result);
        return 12;
    }
    case 0xF9:
        cpu->sp = hl(cpu);
        return 8;
    case 0xFA: {
        uint16_t addr = fetch16(gb, cpu);
        cpu->a = gb_read(gb, addr);
        return 16;
    }
    case 0xFB:
        cpu->ei_delay = 2;
        return 4;
    case 0xFE:
        alu_sub(cpu, fetch8(gb, cpu), false, false);
        return 8;
    default:
        return 4;
    }
}

void cpu_reset(GbCpu *cpu)
{
    if (!cpu) {
        return;
    }
    memset(cpu, 0, sizeof(*cpu));
    cpu->a = 0x01;
    cpu->f = 0xB0;
    cpu->b = 0x00;
    cpu->c = 0x13;
    cpu->d = 0x00;
    cpu->e = 0xD8;
    cpu->h = 0x01;
    cpu->l = 0x4D;
    cpu->sp = 0xFFFE;
    cpu->pc = 0x0100;
    cpu->ime = false;
    cpu->halted = false;
    cpu->halt_bug = false;
    cpu->ei_delay = 0;
    cpu->instructions = 0;
}

unsigned cpu_step(Gb *gb, GbCpu *cpu)
{
    uint8_t pending = (uint8_t)(gb->mmu.ie & gb->mmu.io[0x0F] & 0x1Fu);

    if (cpu->halted) {
        if (pending == 0) {
            return 4;
        }
        cpu->halted = false;
        if (!cpu->ime) {
            cpu->halt_bug = true;
        }
    }

    if (cpu->ime && pending != 0) {
        unsigned cycles = service_interrupt(gb, cpu, pending);
        finish_step(cpu, cycles);
        return cycles;
    }

    uint8_t opcode = fetch8(gb, cpu);
    unsigned cycles = execute_op(gb, cpu, opcode);
    finish_step(cpu, cycles);
    return cycles;
}
