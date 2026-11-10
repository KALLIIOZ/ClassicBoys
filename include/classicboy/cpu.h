/*
 * ClassicBoy - Sharp SM83 (Game Boy CPU).
 */
#ifndef CB_CPU_H
#define CB_CPU_H

#include "classicboy/common.h"

struct Gb;

typedef struct GbCpu {
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint8_t e;
    uint8_t h;
    uint8_t l;
    uint8_t f;
    uint16_t sp;
    uint16_t pc;
    bool ime;
    bool halted;
    bool halt_bug;
    int ei_delay;
    uint64_t instructions;
} GbCpu;

void cpu_reset(GbCpu *cpu);
unsigned cpu_step(struct Gb *gb, GbCpu *cpu);

#endif /* CB_CPU_H */
