/*
 * ClassicBoy - joypad.
 */
#ifndef CB_JOYPAD_H
#define CB_JOYPAD_H

#include "classicboy/common.h"

struct Gb;

typedef enum GbButton {
    GB_BTN_RIGHT = 0,
    GB_BTN_LEFT,
    GB_BTN_UP,
    GB_BTN_DOWN,
    GB_BTN_A,
    GB_BTN_B,
    GB_BTN_SELECT,
    GB_BTN_START,
    GB_BTN_COUNT
} GbButton;

typedef struct GbJoypad {
    uint8_t select; /* bits 4-5, active low */
    uint8_t pressed; /* 1 = pressed, bit per GbButton */
} GbJoypad;

void joypad_reset(GbJoypad *joypad);
uint8_t joypad_read(const GbJoypad *joypad);
void joypad_write(struct Gb *gb, GbJoypad *joypad, uint8_t value);
void joypad_set_button(struct Gb *gb, GbJoypad *joypad, GbButton button,
                       bool pressed);
void joypad_release_all(struct Gb *gb, GbJoypad *joypad);

#endif /* CB_JOYPAD_H */
