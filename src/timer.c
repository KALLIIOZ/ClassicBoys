#include "classicboy/timer.h"

#include "classicboy/gb.h"

static unsigned selected_bit(uint8_t tac)
{
    switch (tac & 0x03u) {
    case 0:
        return 9;
    case 1:
        return 3;
    case 2:
        return 5;
    default:
        return 7;
    }
}

static void tima_increment(struct Gb *gb, GbTimer *timer)
{
    if (timer->reload_delay > 0) {
        return;
    }
    timer->tima++;
    if (timer->tima == 0) {
        timer->tima = 0;
        timer->reload_delay = 4;
        gb_request_interrupt(gb, 2);
    }
}

void timer_reset(GbTimer *timer)
{
    if (!timer) {
        return;
    }
    timer->div = 0xAB00;
    timer->tima = 0;
    timer->tma = 0;
    timer->tac = 0;
    timer->reload_delay = 0;
}

void timer_tick(struct Gb *gb, unsigned cycles)
{
    GbTimer *timer = &gb->timer;
    for (unsigned i = 0; i < cycles; i++) {
        if (timer->reload_delay > 0) {
            timer->reload_delay--;
            if (timer->reload_delay == 0) {
                timer->tima = timer->tma;
            }
        }
        uint16_t old = timer->div;
        timer->div++;
        if (timer->tac & 0x04u) {
            unsigned bit = selected_bit(timer->tac);
            uint16_t mask = (uint16_t)(1u << bit);
            if ((old & mask) != 0 && (timer->div & mask) == 0) {
                tima_increment(gb, timer);
            }
        }
    }
}

uint8_t timer_read(const GbTimer *timer, uint16_t reg)
{
    switch (reg) {
    case 0xFF04u:
        return (uint8_t)(timer->div >> 8);
    case 0xFF05u:
        return timer->tima;
    case 0xFF06u:
        return timer->tma;
    case 0xFF07u:
        return (uint8_t)(0xF8u | (timer->tac & 0x07u));
    default:
        return 0xFF;
    }
}

void timer_write(struct Gb *gb, GbTimer *timer, uint16_t reg, uint8_t value)
{
    switch (reg) {
    case 0xFF04u: {
        uint16_t old = timer->div;
        timer->div = 0;
        if (timer->tac & 0x04u) {
            unsigned bit = selected_bit(timer->tac);
            uint16_t mask = (uint16_t)(1u << bit);
            if ((old & mask) != 0) {
                tima_increment(gb, timer);
            }
        }
        break;
    }
    case 0xFF05u:
        timer->tima = value;
        break;
    case 0xFF06u:
        timer->tma = value;
        break;
    case 0xFF07u: {
        uint8_t old_tac = timer->tac;
        uint16_t old_div = timer->div;
        timer->tac = value & 0x07u;
        bool was_enabled = (old_tac & 0x04u) != 0;
        bool now_enabled = (timer->tac & 0x04u) != 0;
        if (was_enabled) {
            unsigned old_bit = selected_bit(old_tac);
            bool old_level = (old_div & (uint16_t)(1u << old_bit)) != 0;
            bool new_level = now_enabled &&
                             ((old_div & (uint16_t)(1u << selected_bit(timer->tac))) != 0);
            if (old_level && !new_level) {
                tima_increment(gb, timer);
            }
        }
        break;
    }
    default:
        break;
    }
}
