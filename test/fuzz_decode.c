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

    // Wire-format invariants. If the decoder ever lets a value outside
    // [-64, 63] through, libFuzzer reports the crash with a reproducer
    // input it minimises automatically.
    if (e.dx < -64 || e.dx > 63) __builtin_trap();
    if (e.dy < -64 || e.dy > 63) __builtin_trap();

    // The button bits must mirror the wire bits exactly (active LOW).
    bool expected_left  = (word & 0x8000u) == 0;
    bool expected_right = (word & 0x0080u) == 0;
    if (e.left  != expected_left)  __builtin_trap();
    if (e.right != expected_right) __builtin_trap();

    return 0;
}