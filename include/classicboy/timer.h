/*
 * ClassicBoy - DIV/TIMA timer.
 */
#ifndef CB_TIMER_H
#define CB_TIMER_H

#include "classicboy/common.h"

struct Gb;

typedef struct GbTimer {
    uint16_t div;
    uint8_t tima;
    uint8_t tma;
    uint8_t tac;
    int reload_delay;
} GbTimer;

void timer_reset(GbTimer *timer);
void timer_tick(struct Gb *gb, unsigned cycles);
uint8_t timer_read(const GbTimer *timer, uint16_t reg);
void timer_write(struct Gb *gb, GbTimer *timer, uint16_t reg, uint8_t value);

#endif /* CB_TIMER_H */
