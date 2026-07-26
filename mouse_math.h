// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Denis Rodin

#ifndef MOUSE_MATH_H
#define MOUSE_MATH_H

#include <stdint.h>

static inline int8_t mouse_clamp_i8(int32_t value) {
    if (value > INT8_MAX) {
        return INT8_MAX;
    }
    if (value < INT8_MIN) {
        return INT8_MIN;
    }
    return (int8_t)value;
}

// The asymmetric parameter types make clang-tidy mistake the natural
// accumulator/delta order for an easily-swappable callback-like interface.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static inline int32_t mouse_accumulate_i8(int32_t current, int8_t delta) {
    int32_t widened_delta = (int32_t)delta;
    if (widened_delta > 0 && current > INT32_MAX - widened_delta) {
        return INT32_MAX;
    }
    if (widened_delta < 0 && current < INT32_MIN - widened_delta) {
        return INT32_MIN;
    }
    return current + widened_delta;
}

#endif
