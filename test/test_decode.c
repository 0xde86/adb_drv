// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Denis Rodin
//
// Host-side unit tests for the ADB mouse Register 0 decoder.
//
// Wire layout (see adb_decode.h):
//   bit 15 : btn1 (active LOW) -> mouse_event_t.left
//   bits 14..8 : Y delta, 7-bit signed
//   bit 7  : btn2 (active LOW) -> mouse_event_t.right
//   bits 6..0 : X delta, 7-bit signed
//
// Bit layouts in each case are spelled out in the comment to keep them
// readable without a calculator.

#include "unity.h"

#include "adb_decode.h"

void setUp(void) {}
void tearDown(void) {}

// Both buttons released, deltas zero.
static void test_idle(void) {
    // 0x8080 = 1 0000000 1 0000000
    mouse_event_t e = adb_decode_mouse(0x8080);
    TEST_ASSERT_EQUAL_INT8(0, e.dx);
    TEST_ASSERT_EQUAL_INT8(0, e.dy);
    TEST_ASSERT_FALSE(e.left);
    TEST_ASSERT_FALSE(e.right);
}

// Max positive X delta = +63 (0x3F = 0b0111111).
static void test_dx_max_positive(void) {
    // 0x80BF = 1 0000000 1 0111111
    mouse_event_t e = adb_decode_mouse(0x80BF);
    TEST_ASSERT_EQUAL_INT8(63, e.dx);
    TEST_ASSERT_EQUAL_INT8(0,  e.dy);
}

// Min negative X delta = -64 (0x40 = 0b1000000, sign bit set, value = -64).
static void test_dx_min_negative(void) {
    // 0x80C0 = 1 0000000 1 1000000
    mouse_event_t e = adb_decode_mouse(0x80C0);
    TEST_ASSERT_EQUAL_INT8(-64, e.dx);
    TEST_ASSERT_EQUAL_INT8(0,   e.dy);
}

// X delta = -1 — exercises sign extension for the "all bits set" axis.
static void test_dx_minus_one(void) {
    // 0x80FF = 1 0000000 1 1111111
    mouse_event_t e = adb_decode_mouse(0x80FF);
    TEST_ASSERT_EQUAL_INT8(-1, e.dx);
    TEST_ASSERT_EQUAL_INT8(0,  e.dy);
}

// Symmetric tests for the Y axis (high byte, bits 14..8).
static void test_dy_max_positive(void) {
    // 0xBF80 = 1 0111111 1 0000000
    mouse_event_t e = adb_decode_mouse(0xBF80);
    TEST_ASSERT_EQUAL_INT8(0,  e.dx);
    TEST_ASSERT_EQUAL_INT8(63, e.dy);
}

static void test_dy_min_negative(void) {
    // 0xC080 = 1 1000000 1 0000000
    mouse_event_t e = adb_decode_mouse(0xC080);
    TEST_ASSERT_EQUAL_INT8(0,   e.dx);
    TEST_ASSERT_EQUAL_INT8(-64, e.dy);
}

// Left button only (btn1 = 0, btn2 = 1).
static void test_left_button_pressed(void) {
    // 0x0080 = 0 0000000 1 0000000
    mouse_event_t e = adb_decode_mouse(0x0080);
    TEST_ASSERT_TRUE(e.left);
    TEST_ASSERT_FALSE(e.right);
    TEST_ASSERT_EQUAL_INT8(0, e.dx);
    TEST_ASSERT_EQUAL_INT8(0, e.dy);
}

// Right button only (btn1 = 1, btn2 = 0).
static void test_right_button_pressed(void) {
    // 0x8000 = 1 0000000 0 0000000
    mouse_event_t e = adb_decode_mouse(0x8000);
    TEST_ASSERT_FALSE(e.left);
    TEST_ASSERT_TRUE(e.right);
}

// Both buttons pressed with diagonal motion (+10, -5).
static void test_both_buttons_and_diagonal(void) {
    // X = +10 = 0x0A, Y = -5 = 0x7B (0b1111011, sign-extended -> -5)
    // 0x7B0A = 0 1111011 0 0001010
    mouse_event_t e = adb_decode_mouse(0x7B0A);
    TEST_ASSERT_TRUE(e.left);
    TEST_ASSERT_TRUE(e.right);
    TEST_ASSERT_EQUAL_INT8(10, e.dx);
    TEST_ASSERT_EQUAL_INT8(-5, e.dy);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_idle);
    RUN_TEST(test_dx_max_positive);
    RUN_TEST(test_dx_min_negative);
    RUN_TEST(test_dx_minus_one);
    RUN_TEST(test_dy_max_positive);
    RUN_TEST(test_dy_min_negative);
    RUN_TEST(test_left_button_pressed);
    RUN_TEST(test_right_button_pressed);
    RUN_TEST(test_both_buttons_and_diagonal);
    return UNITY_END();
}