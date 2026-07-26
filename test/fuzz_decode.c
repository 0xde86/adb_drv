// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Denis Rodin
//
// libFuzzer harness for adb_decode_mouse

#include <stddef.h>
#include <stdint.h>

#include "adb_decode.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // We need exactly 2 bytes to form the 16-bit wire word. libFuzzer is
    // free to call us with any length; return 0 to discard inputs that
    // don't fit.
    if (size < 2) {
        return 0;
    }
    uint16_t word = (uint16_t)data[0] << 8 | (uint16_t)data[1];

    mouse_event_t e = adb_decode_mouse(word);

    // Compare both axes with an independent two's-complement oracle.
    int expected_x = word & 0x7F;
    int expected_y = (word >> 8) & 0x7F;
    if (expected_x >= 64) expected_x -= 128;
    if (expected_y >= 64) expected_y -= 128;
    if (e.dx != expected_x) __builtin_trap();
    if (e.dy != expected_y) __builtin_trap();

    // The button bits must mirror the wire bits exactly (active LOW).
    bool expected_left  = (word & 0x8000u) == 0;
    bool expected_right = (word & 0x0080u) == 0;
    if (e.left  != expected_left)  __builtin_trap();
    if (e.right != expected_right) __builtin_trap();

    // Every data word must round-trip through a correctly framed packet.
    uint32_t valid_frame = ADB_MOUSE_FRAME_START_MASK |
                           ((uint32_t)word << ADB_MOUSE_FRAME_DATA_SHIFT);
    mouse_event_t framed;
    if (!adb_decode_mouse_frame(valid_frame, &framed)) __builtin_trap();
    if (framed.dx != e.dx || framed.dy != e.dy ||
        framed.left != e.left || framed.right != e.right) {
        __builtin_trap();
    }

    // Invalid framing must be rejected without touching the destination.
    mouse_event_t sentinel = {.dx = 12, .dy = -13, .left = true, .right = false};
    if (adb_decode_mouse_frame(valid_frame & ~ADB_MOUSE_FRAME_START_MASK,
                               &sentinel)) {
        __builtin_trap();
    }
    if (sentinel.dx != 12 || sentinel.dy != -13 ||
        !sentinel.left || sentinel.right) {
        __builtin_trap();
    }
    if (adb_decode_mouse_frame(valid_frame | ADB_MOUSE_FRAME_STOP_MASK,
                               &sentinel)) {
        __builtin_trap();
    }

    return 0;
}
