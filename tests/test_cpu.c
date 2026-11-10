#include "test.h"

#include "classicboy/gb.h"
#include "test_util.h"

#define FZ 0x80u
#define FN 0x40u
#define FH 0x20u
#define FC 0x10u

static bool has_flag(const Gb *gb, uint8_t mask)
{
    return (gb->cpu.f & mask) != 0;
}

static unsigned run_op(Gb *gb, uint8_t opcode)
{
    gb->cpu.pc = 0xC000;
    gb->mmu.wram[0] = opcode;
    return cpu_step(gb, &gb->cpu);
}

static unsigned run_cb(Gb *gb, uint8_t opcode)
{
    gb->cpu.pc = 0xC000;
    gb->mmu.wram[0] = 0xCB;
    gb->mmu.wram[1] = opcode;
    return cpu_step(gb, &gb->cpu);
}

static void test_cpu_alu(void)
{
    Gb gb;
    test_gb_init(&gb);

    gb.cpu.a = 0x0F;
    gb.cpu.b = 0x01;
    gb.cpu.f = 0;
    TEST_EQ_U16(run_op(&gb, 0x80), 4);
    TEST_EQ_U8(gb.cpu.a, 0x10);
    TEST_ASSERT(!has_flag(&gb, FZ));
    TEST_ASSERT(!has_flag(&gb, FN));
    TEST_ASSERT(has_flag(&gb, FH));
    TEST_ASSERT(!has_flag(&gb, FC));

    gb.cpu.a = 0xFF;
    gb.cpu.b = 0x00;
    gb.cpu.f = FC;
    (void)run_op(&gb, 0x88);
    TEST_EQ_U8(gb.cpu.a, 0x00);
    TEST_ASSERT(has_flag(&gb, FZ));
    TEST_ASSERT(has_flag(&gb, FH));
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FN));

    gb.cpu.a = 0x10;
    gb.cpu.b = 0x01;
    gb.cpu.f = 0;
    (void)run_op(&gb, 0x90);
    TEST_EQ_U8(gb.cpu.a, 0x0F);
    TEST_ASSERT(has_flag(&gb, FN));
    TEST_ASSERT(has_flag(&gb, FH));
    TEST_ASSERT(!has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FZ));

    gb.cpu.a = 0x00;
    gb.cpu.b = 0x00;
    gb.cpu.f = FC;
    (void)run_op(&gb, 0x98);
    TEST_EQ_U8(gb.cpu.a, 0xFF);
    TEST_ASSERT(has_flag(&gb, FN));
    TEST_ASSERT(has_flag(&gb, FH));
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FZ));

    gb.cpu.a = 0xF0;
    gb.cpu.b = 0x0F;
    gb.cpu.f = 0;
    (void)run_op(&gb, 0xA0);
    TEST_EQ_U8(gb.cpu.a, 0x00);
    TEST_ASSERT(has_flag(&gb, FZ));
    TEST_ASSERT(has_flag(&gb, FH));
    TEST_ASSERT(!has_flag(&gb, FN));
    TEST_ASSERT(!has_flag(&gb, FC));

    gb.cpu.a = 0xFF;
    gb.cpu.b = 0x0F;
    gb.cpu.f = 0;
    (void)run_op(&gb, 0xA8);
    TEST_EQ_U8(gb.cpu.a, 0xF0);
    TEST_ASSERT(!has_flag(&gb, FZ));
    TEST_ASSERT(!has_flag(&gb, FN));
    TEST_ASSERT(!has_flag(&gb, FH));
    TEST_ASSERT(!has_flag(&gb, FC));

    gb.cpu.a = 0xF0;
    gb.cpu.b = 0x0F;
    gb.cpu.f = 0;
    (void)run_op(&gb, 0xB0);
    TEST_EQ_U8(gb.cpu.a, 0xFF);

    gb.cpu.a = 0x10;
    gb.cpu.b = 0x10;
    gb.cpu.f = 0;
    (void)run_op(&gb, 0xB8);
    TEST_EQ_U8(gb.cpu.a, 0x10);
    TEST_ASSERT(has_flag(&gb, FZ));
    TEST_ASSERT(has_flag(&gb, FN));
    TEST_ASSERT(!has_flag(&gb, FH));
    TEST_ASSERT(!has_flag(&gb, FC));

    gb_destroy(&gb);
}

static void test_cpu_inc_dec(void)
{
    Gb gb;
    test_gb_init(&gb);

    gb.cpu.b = 0x0F;
    gb.cpu.f = FC;
    (void)run_op(&gb, 0x04);
    TEST_EQ_U8(gb.cpu.b, 0x10);
    TEST_ASSERT(has_flag(&gb, FH));
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FN));
    TEST_ASSERT(!has_flag(&gb, FZ));

    gb.cpu.b = 0x00;
    gb.cpu.f = FC;
    (void)run_op(&gb, 0x05);
    TEST_EQ_U8(gb.cpu.b, 0xFF);
    TEST_ASSERT(has_flag(&gb, FN));
    TEST_ASSERT(has_flag(&gb, FH));
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FZ));

    gb.cpu.b = 0x01;
    gb.cpu.f = 0;
    (void)run_op(&gb, 0x05);
    TEST_EQ_U8(gb.cpu.b, 0x00);
    TEST_ASSERT(has_flag(&gb, FZ));

    gb.cpu.h = 0xC1;
    gb.cpu.l = 0x00;
    gb.mmu.wram[0x0100] = 0xFF;
    gb.cpu.pc = 0xC000;
    gb.mmu.wram[0] = 0x34;
    TEST_EQ_U16(cpu_step(&gb, &gb.cpu), 12);
    TEST_EQ_U8(gb.mmu.wram[0x0100], 0x00);
    TEST_ASSERT(has_flag(&gb, FZ));
    TEST_ASSERT(!has_flag(&gb, FN));
    TEST_ASSERT(has_flag(&gb, FH));

    gb.cpu.h = 0xC1;
    gb.cpu.l = 0x00;
    gb.mmu.wram[0x0100] = 0x00;
    gb.cpu.pc = 0xC000;
    gb.mmu.wram[0] = 0x35;
    TEST_EQ_U16(cpu_step(&gb, &gb.cpu), 12);
    TEST_EQ_U8(gb.mmu.wram[0x0100], 0xFF);
    TEST_ASSERT(has_flag(&gb, FN));
    TEST_ASSERT(has_flag(&gb, FH));
    TEST_ASSERT(!has_flag(&gb, FZ));

    gb_destroy(&gb);
}

static void test_cpu_16bit(void)
{
    Gb gb;
    test_gb_init(&gb);

    gb.cpu.h = 0x0F;
    gb.cpu.l = 0xFF;
    gb.cpu.b = 0x00;
    gb.cpu.c = 0x01;
    gb.cpu.f = FZ;
    (void)run_op(&gb, 0x09);
    TEST_EQ_U16(((unsigned)gb.cpu.h << 8) | gb.cpu.l, 0x1000);
    TEST_ASSERT(has_flag(&gb, FH));
    TEST_ASSERT(!has_flag(&gb, FC));
    TEST_ASSERT(has_flag(&gb, FZ));

    gb.cpu.h = 0xFF;
    gb.cpu.l = 0xFF;
    gb.cpu.sp = 0x0001;
    gb.cpu.f = 0;
    (void)run_op(&gb, 0x39);
    TEST_EQ_U16(((unsigned)gb.cpu.h << 8) | gb.cpu.l, 0x0000);
    TEST_ASSERT(has_flag(&gb, FH));
    TEST_ASSERT(has_flag(&gb, FC));

    gb.cpu.sp = 0xD000;
    gb.cpu.pc = 0xC000;
    gb.mmu.wram[0] = 0xE8;
    gb.mmu.wram[1] = 0x01;
    TEST_EQ_U16(cpu_step(&gb, &gb.cpu), 16);
    TEST_EQ_U16(gb.cpu.sp, 0xD001);
    TEST_ASSERT(!has_flag(&gb, FZ));
    TEST_ASSERT(!has_flag(&gb, FN));
    TEST_ASSERT(!has_flag(&gb, FH));
    TEST_ASSERT(!has_flag(&gb, FC));

    gb.cpu.sp = 0xD0FF;
    gb.cpu.pc = 0xC000;
    gb.mmu.wram[0] = 0xE8;
    gb.mmu.wram[1] = 0x01;
    (void)cpu_step(&gb, &gb.cpu);
    TEST_EQ_U16(gb.cpu.sp, 0xD100);
    TEST_ASSERT(has_flag(&gb, FC));

    gb.cpu.sp = 0xD00F;
    gb.cpu.pc = 0xC000;
    gb.mmu.wram[0] = 0xF8;
    gb.mmu.wram[1] = 0x01;
    TEST_EQ_U16(cpu_step(&gb, &gb.cpu), 12);
    TEST_EQ_U16(((unsigned)gb.cpu.h << 8) | gb.cpu.l, 0xD010);
    TEST_ASSERT(has_flag(&gb, FH));

    gb_destroy(&gb);
}

static void test_cpu_rotates(void)
{
    Gb gb;
    test_gb_init(&gb);

    gb.cpu.a = 0x81;
    gb.cpu.f = 0;
    (void)run_op(&gb, 0x07);
    TEST_EQ_U8(gb.cpu.a, 0x03);
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FZ));

    gb.cpu.a = 0x01;
    gb.cpu.f = 0;
    (void)run_op(&gb, 0x0F);
    TEST_EQ_U8(gb.cpu.a, 0x80);
    TEST_ASSERT(has_flag(&gb, FC));

    gb.cpu.a = 0x80;
    gb.cpu.f = 0;
    (void)run_op(&gb, 0x17);
    TEST_EQ_U8(gb.cpu.a, 0x00);
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FZ));

    gb.cpu.a = 0x01;
    gb.cpu.f = FC;
    (void)run_op(&gb, 0x1F);
    TEST_EQ_U8(gb.cpu.a, 0x80);
    TEST_ASSERT(has_flag(&gb, FC));

    gb.cpu.a = 0x55;
    gb.cpu.f = 0;
    (void)run_op(&gb, 0x2F);
    TEST_EQ_U8(gb.cpu.a, 0xAA);
    TEST_ASSERT(has_flag(&gb, FN));
    TEST_ASSERT(has_flag(&gb, FH));

    gb.cpu.f = 0;
    (void)run_op(&gb, 0x37);
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FN));
    TEST_ASSERT(!has_flag(&gb, FH));

    (void)run_op(&gb, 0x3F);
    TEST_ASSERT(!has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FN));
    TEST_ASSERT(!has_flag(&gb, FH));

    gb_destroy(&gb);
}

static void test_cpu_daa(void)
{
    Gb gb;
    test_gb_init(&gb);

    gb.cpu.a = 0x0A;
    gb.cpu.f = FH;
    (void)run_op(&gb, 0x27);
    TEST_EQ_U8(gb.cpu.a, 0x10);
    TEST_ASSERT(!has_flag(&gb, FH));
    TEST_ASSERT(!has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FZ));

    gb.cpu.a = 0x9A;
    gb.cpu.f = 0;
    (void)run_op(&gb, 0x27);
    TEST_EQ_U8(gb.cpu.a, 0x00);
    TEST_ASSERT(has_flag(&gb, FZ));
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FH));

    gb.cpu.a = 0x00;
    gb.cpu.f = FN | FH | FC;
    (void)run_op(&gb, 0x27);
    TEST_EQ_U8(gb.cpu.a, 0x9A);
    TEST_ASSERT(has_flag(&gb, FN));
    TEST_ASSERT(has_flag(&gb, FC));

    gb_destroy(&gb);
}

static void test_cpu_loads_stack(void)
{
    Gb gb;
    test_gb_init(&gb);

    const uint8_t code1[] = {0x3E, 0x42, 0x47};
    test_install_code(&gb, code1, sizeof(code1));
    test_run_steps(&gb, 2);
    TEST_EQ_U8(gb.cpu.a, 0x42);
    TEST_EQ_U8(gb.cpu.b, 0x42);

    const uint8_t code2[] = {0x01, 0x34, 0x12, 0xC5, 0xD1};
    test_install_code(&gb, code2, sizeof(code2));
    gb.cpu.sp = 0xD000;
    test_run_steps(&gb, 3);
    TEST_EQ_U8(gb.cpu.b, 0x12);
    TEST_EQ_U8(gb.cpu.c, 0x34);
    TEST_EQ_U8(gb.cpu.d, 0x12);
    TEST_EQ_U8(gb.cpu.e, 0x34);
    TEST_EQ_U16(gb.cpu.sp, 0xD000);

    const uint8_t code3[] = {0xCD, 0x06, 0xC0, 0x00, 0x00, 0x00, 0xC9};
    test_install_code(&gb, code3, sizeof(code3));
    gb.cpu.sp = 0xD000;
    TEST_EQ_U16(test_step(&gb), 24);
    TEST_EQ_U16(gb.cpu.pc, 0xC006);
    TEST_EQ_U16(gb.cpu.sp, 0xCFFE);
    TEST_EQ_U16(test_step(&gb), 16);
    TEST_EQ_U16(gb.cpu.pc, 0xC003);
    TEST_EQ_U16(gb.cpu.sp, 0xD000);

    const uint8_t code4[] = {0x18, 0x02, 0x00, 0x00, 0x00};
    test_install_code(&gb, code4, sizeof(code4));
    TEST_EQ_U16(test_step(&gb), 12);
    TEST_EQ_U16(gb.cpu.pc, 0xC004);

    const uint8_t code5[] = {0xFF};
    test_install_code(&gb, code5, sizeof(code5));
    gb.cpu.sp = 0xD000;
    TEST_EQ_U16(test_step(&gb), 16);
    TEST_EQ_U16(gb.cpu.pc, 0x0038);
    TEST_EQ_U16(gb.cpu.sp, 0xCFFE);
    TEST_EQ_U8(gb.mmu.wram[0x0FFE], 0x01);
    TEST_EQ_U8(gb.mmu.wram[0x0FFF], 0xC0);

    const uint8_t code6[] = {0x3E, 0x42, 0xE0, 0x80, 0xF0, 0x80};
    test_install_code(&gb, code6, sizeof(code6));
    test_run_steps(&gb, 3);
    TEST_EQ_U8(gb.mmu.hram[0x00], 0x42);
    TEST_EQ_U8(gb.cpu.a, 0x42);

    const uint8_t code7[] = {0x21, 0x00, 0xC1, 0x3E, 0x55, 0x22, 0x2A};
    test_install_code(&gb, code7, sizeof(code7));
    test_run_steps(&gb, 4);
    TEST_EQ_U8(gb.mmu.wram[0x0100], 0x55);
    TEST_EQ_U16(((unsigned)gb.cpu.h << 8) | gb.cpu.l, 0xC102);
    TEST_EQ_U8(gb.cpu.a, 0x00);

    gb.cpu.sp = 0xD000;
    const uint8_t code8[] = {0x08, 0x00, 0xC1};
    test_install_code(&gb, code8, sizeof(code8));
    TEST_EQ_U16(test_step(&gb), 20);
    TEST_EQ_U8(gb.mmu.wram[0x0100], 0x00);
    TEST_EQ_U8(gb.mmu.wram[0x0101], 0xD0);

    gb_destroy(&gb);
}

static void test_cpu_cb(void)
{
    Gb gb;
    test_gb_init(&gb);

    gb.cpu.b = 0x80;
    gb.cpu.f = 0;
    TEST_EQ_U16(run_cb(&gb, 0x00), 8);
    TEST_EQ_U8(gb.cpu.b, 0x01);
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FZ));

    gb.cpu.b = 0x01;
    gb.cpu.f = 0;
    (void)run_cb(&gb, 0x08);
    TEST_EQ_U8(gb.cpu.b, 0x80);
    TEST_ASSERT(has_flag(&gb, FC));

    gb.cpu.b = 0x80;
    gb.cpu.f = 0;
    (void)run_cb(&gb, 0x10);
    TEST_EQ_U8(gb.cpu.b, 0x00);
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(has_flag(&gb, FZ));

    gb.cpu.b = 0x01;
    gb.cpu.f = FC;
    (void)run_cb(&gb, 0x18);
    TEST_EQ_U8(gb.cpu.b, 0x80);
    TEST_ASSERT(has_flag(&gb, FC));

    gb.cpu.b = 0x80;
    gb.cpu.f = 0;
    (void)run_cb(&gb, 0x20);
    TEST_EQ_U8(gb.cpu.b, 0x00);
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(has_flag(&gb, FZ));

    gb.cpu.b = 0x81;
    gb.cpu.f = 0;
    (void)run_cb(&gb, 0x28);
    TEST_EQ_U8(gb.cpu.b, 0xC0);
    TEST_ASSERT(has_flag(&gb, FC));

    gb.cpu.b = 0x12;
    gb.cpu.f = FC;
    (void)run_cb(&gb, 0x30);
    TEST_EQ_U8(gb.cpu.b, 0x21);
    TEST_ASSERT(!has_flag(&gb, FC));

    gb.cpu.b = 0x01;
    gb.cpu.f = 0;
    (void)run_cb(&gb, 0x38);
    TEST_EQ_U8(gb.cpu.b, 0x00);
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(has_flag(&gb, FZ));

    gb.cpu.b = 0x80;
    gb.cpu.f = FC;
    (void)run_cb(&gb, 0x78); /* BIT 7,B */
    TEST_ASSERT(!has_flag(&gb, FZ));
    TEST_ASSERT(has_flag(&gb, FH));
    TEST_ASSERT(has_flag(&gb, FC));
    TEST_ASSERT(!has_flag(&gb, FN));

    gb.cpu.b = 0x00;
    gb.cpu.f = 0;
    (void)run_cb(&gb, 0x40); /* BIT 0,B */
    TEST_ASSERT(has_flag(&gb, FZ));
    TEST_ASSERT(has_flag(&gb, FH));

    gb.cpu.b = 0xFF;
    gb.cpu.f = FZ;
    (void)run_cb(&gb, 0xB8); /* RES 7,B */
    TEST_EQ_U8(gb.cpu.b, 0x7F);
    TEST_ASSERT(has_flag(&gb, FZ));

    gb.cpu.b = 0x00;
    gb.cpu.f = 0;
    (void)run_cb(&gb, 0xC0); /* SET 0,B */
    TEST_EQ_U8(gb.cpu.b, 0x01);
    TEST_ASSERT(!has_flag(&gb, FZ));

    gb.cpu.h = 0xC1;
    gb.cpu.l = 0x00;
    gb.mmu.wram[0x0100] = 0x80;
    gb.cpu.pc = 0xC000;
    gb.mmu.wram[0] = 0xCB;
    gb.mmu.wram[1] = 0x06; /* RLC (HL) */
    TEST_EQ_U16(cpu_step(&gb, &gb.cpu), 16);
    TEST_EQ_U8(gb.mmu.wram[0x0100], 0x01);
    TEST_ASSERT(has_flag(&gb, FC));

    gb_destroy(&gb);
}

static void test_cpu_interrupts(void)
{
    Gb gb;
    test_gb_init(&gb);

    gb.mmu.ie = 0x01;
    gb.mmu.io[0x0F] = 0x01;
    gb.cpu.ime = true;
    gb.cpu.pc = 0xC000;
    gb.cpu.sp = 0xD000;
    TEST_EQ_U16(test_step(&gb), 20);
    TEST_EQ_U16(gb.cpu.pc, 0x0040);
    TEST_EQ_U16(gb.cpu.sp, 0xCFFE);
    TEST_ASSERT(!gb.cpu.ime);
    TEST_EQ_U8(gb.mmu.io[0x0F] & 0x01, 0x00);
    TEST_EQ_U8(gb.mmu.wram[0x0FFE], 0x00);
    TEST_EQ_U8(gb.mmu.wram[0x0FFF], 0xC0);

    test_gb_init(&gb);
    const uint8_t code[] = {0xFB, 0x00, 0x00};
    test_install_code(&gb, code, sizeof(code));
    gb.mmu.ie = 0x01;
    gb.mmu.io[0x0F] = 0x01;
    gb.cpu.ime = false;
    TEST_EQ_U16(test_step(&gb), 4);
    TEST_ASSERT(!gb.cpu.ime);
    TEST_EQ_U16(test_step(&gb), 4);
    TEST_ASSERT(gb.cpu.ime);
    TEST_EQ_U16(gb.cpu.pc, 0xC002);
    TEST_EQ_U16(test_step(&gb), 20);
    TEST_EQ_U16(gb.cpu.pc, 0x0040);

    test_gb_init(&gb);
    const uint8_t halt_code[] = {0x76, 0x3E, 0x42};
    test_install_code(&gb, halt_code, sizeof(halt_code));
    gb.mmu.ie = 0x01;
    gb.mmu.io[0x0F] = 0x00;
    TEST_EQ_U16(test_step(&gb), 4);
    TEST_ASSERT(gb.cpu.halted);
    TEST_EQ_U16(test_step(&gb), 4);
    TEST_ASSERT(gb.cpu.halted);
    gb.mmu.io[0x0F] = 0x01;
    TEST_EQ_U16(test_step(&gb), 8);
    TEST_ASSERT(!gb.cpu.halted);
    TEST_EQ_U8(gb.cpu.a, 0x3E);
    TEST_EQ_U16(gb.cpu.pc, 0xC002);

    test_gb_init(&gb);
    const uint8_t stop_code[] = {0x10, 0x00};
    test_install_code(&gb, stop_code, sizeof(stop_code));
    TEST_EQ_U16(test_step(&gb), 4);
    TEST_EQ_U16(gb.cpu.pc, 0xC002);

    gb_destroy(&gb);
}

static void test_cpu_cycles(void)
{
    Gb gb;
    test_gb_init(&gb);

    TEST_EQ_U16(run_op(&gb, 0x00), 4);
    TEST_EQ_U16(run_op(&gb, 0x04), 4);
    TEST_EQ_U16(run_op(&gb, 0x3E), 8);
    TEST_EQ_U16(run_op(&gb, 0x01), 12);
    TEST_EQ_U16(run_op(&gb, 0x02), 8);
    TEST_EQ_U16(run_op(&gb, 0xC5), 16);
    TEST_EQ_U16(run_op(&gb, 0xC1), 12);
    TEST_EQ_U16(run_op(&gb, 0xC9), 16);
    TEST_EQ_U16(run_op(&gb, 0xC3), 16);
    TEST_EQ_U16(run_op(&gb, 0x18), 12);
    gb.cpu.f = FZ;
    TEST_EQ_U16(run_op(&gb, 0xC4), 12);
    TEST_EQ_U16(run_op(&gb, 0xE0), 12);
    TEST_EQ_U16(run_op(&gb, 0xF0), 12);
    TEST_EQ_U16(run_cb(&gb, 0x00), 8);
    TEST_EQ_U16(run_cb(&gb, 0x46), 12);

    gb.cpu.h = 0xC1;
    gb.cpu.l = 0x00;
    gb.cpu.pc = 0xC000;
    gb.mmu.wram[0] = 0xCB;
    gb.mmu.wram[1] = 0xC6;
    TEST_EQ_U16(cpu_step(&gb, &gb.cpu), 16);

    gb_destroy(&gb);
}

void test_cpu_all(void)
{
    run_test("cpu/alu", test_cpu_alu);
    run_test("cpu/inc-dec", test_cpu_inc_dec);
    run_test("cpu/16-bit", test_cpu_16bit);
    run_test("cpu/rotates", test_cpu_rotates);
    run_test("cpu/daa", test_cpu_daa);
    run_test("cpu/loads-stack", test_cpu_loads_stack);
    run_test("cpu/cb", test_cpu_cb);
    run_test("cpu/interrupts", test_cpu_interrupts);
    run_test("cpu/cycles", test_cpu_cycles);
}
