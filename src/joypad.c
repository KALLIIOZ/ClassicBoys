#include "classicboy/joypad.h"

#include "classicboy/gb.h"

static uint8_t button_mask(GbButton button)
{
    if (button >= GB_BTN_COUNT) {
        return 0;
    }
    return (uint8_t)(1u << (unsigned)button);
}

void joypad_reset(GbJoypad *joypad)
{
    if (!joypad) {
        return;
    }
    joypad->select = 0x30;
    joypad->pressed = 0;
}

uint8_t joypad_read(const GbJoypad *joypad)
{
    uint8_t directions = (uint8_t)(joypad->pressed & 0x0Fu);
    uint8_t actions = (uint8_t)((joypad->pressed >> 4) & 0x0Fu);
    uint8_t active = 0;
    if ((joypad->select & 0x10u) == 0) {
        active |= directions;
    }
    if ((joypad->select & 0x20u) == 0) {
        active |= actions;
    }
    return (uint8_t)(0xC0u | (joypad->select & 0x30u) | (0x0Fu & ~active));
}

void joypad_write(struct Gb *gb, GbJoypad *joypad, uint8_t value)
{
    uint8_t old = joypad_read(joypad);
    joypad->select = value & 0x30u;
    uint8_t now = joypad_read(joypad);
    if ((old & (uint8_t)~now & 0x0Fu) != 0) {
        gb_request_interrupt(gb, 4);
    }
}

void joypad_set_button(struct Gb *gb, GbJoypad *joypad, GbButton button,
                       bool pressed)
{
    if (!joypad || button >= GB_BTN_COUNT) {
        return;
    }
    uint8_t mask = button_mask(button);
    uint8_t old = joypad_read(joypad);
    if (pressed) {
        joypad->pressed |= mask;
    } else {
        joypad->pressed &= (uint8_t)~mask;
    }
    uint8_t now = joypad_read(joypad);
    if ((old & (uint8_t)~now & 0x0Fu) != 0) {
        gb_request_interrupt(gb, 4);
    }
}

void joypad_release_all(struct Gb *gb, GbJoypad *joypad)
{
    if (!joypad) {
        return;
    }
    uint8_t old = joypad_read(joypad);
    joypad->pressed = 0;
    uint8_t now = joypad_read(joypad);
    if ((old & (uint8_t)~now & 0x0Fu) != 0) {
        gb_request_interrupt(gb, 4);
    }
}
