// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Denis Rodin
//
// ADB mouse Register 0 wire-format decode

#ifndef ADB_DECODE_H
#define ADB_DECODE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int8_t dx, dy;
    bool left, right;
} mouse_event_t;

// Mouse Register 0 reply layout. Each axis is a 7-bit signed delta; the
// button bits are active-LOW (0 == pressed).
//
//   bit  15 : button1 -> left   (ADB_BTN1_MASK)
//   bits 14..8 : Y delta, 7-bit signed
//   bit  7  : button2 -> right  (ADB_BTN2_MASK)
//   bits 6..0  : X delta, 7-bit signed
#define ADB_MOUSE_AXIS_BITS 7
#define ADB_MOUSE_AXIS_MASK ((1U << ADB_MOUSE_AXIS_BITS) - 1U)
#define ADB_MOUSE_AXIS_SIGN (1U << (ADB_MOUSE_AXIS_BITS - 1))
#define ADB_MOUSE_AXIS_BIAS (1U <<  ADB_MOUSE_AXIS_BITS)
#define ADB_BTN1_MASK       0x8000U
#define ADB_BTN2_MASK       0x0080U

static inline int adb_seven_bit_signed(uint16_t val) {
    val &= ADB_MOUSE_AXIS_MASK;
    return (val & ADB_MOUSE_AXIS_SIGN) ? (int)val - (int)ADB_MOUSE_AXIS_BIAS : (int)val;
}

static inline mouse_event_t adb_decode_mouse(uint16_t data) {
    mouse_event_t e;
    e.dx    = (int8_t)adb_seven_bit_signed(data);
    e.dy    = (int8_t)adb_seven_bit_signed(data >> 8);
    e.left  = (bool)((data & ADB_BTN1_MASK) == 0);
    e.right = (bool)((data & ADB_BTN2_MASK) == 0);
    return e;
}

#endif