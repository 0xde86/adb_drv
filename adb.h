// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Denis Rodin

// Implementation is based on information from:
// - https://developer.apple.com/library/archive/documentation/mac/pdf/Devices/ADB_Manager.pdf
// - https://vintageapple.org/inside_o/pdf/Guide_to_Macintosh_Family_Hardware_2nd_Edition_1990.pdf
// - https://github.com/tmk/tmk_keyboard/wiki/Apple-Desktop-Bus
#ifndef ADB_H
#define ADB_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"

// In the book Guide_to_Macintosh_Family_Hardware there are several mentions
// that ADB manager polls devices every 11ms. Documentation from tmk_keyboard repo also
// mentions 11ms as poll interval for active device.
#define ADB_POLL_INTERVAL_MS 11

typedef enum {
    ADB_OK              =  0,
    ADB_ERR_ADD_TX      = -1,
    ADB_ERR_CLAIM_TX_SM = -2,
    ADB_ERR_INIT_TX_SM  = -3,
    ADB_ERR_ADD_RX      = -4,
    ADB_ERR_CLAIM_RX_SM = -5,
    ADB_ERR_INIT_RX_SM  = -6,
} adb_err_t;

// Main struct for handling ADB protocol.
// This includes:
//  - managing PIO state machines for communicating with ADB device;
//  - sending commands / receiving reports;
//  - decoding mouse movement
typedef struct {
    PIO pio;
    uint8_t pin;
    uint8_t tx_sm;
    uint8_t tx_off;
    uint8_t rx_sm;
    uint8_t rx_off;
    uint8_t owned;  // bitmask of acquired resources (see ADB_OWNS_* in adb.c)
} adb_t;

typedef struct {
    int8_t dx, dy;
    bool left, right;
} mouse_event_t;

// Init ADB module. Returns ADB_OK on success, adb_err_t < 0 on failure.
adb_err_t adb_init(adb_t *adb, PIO pio, uint8_t pin);

// Release all resources owned by `adb`. Safe to call on a partially-
// initialized ADB and idempotent (a second call is a no-op).
void adb_deinit(adb_t *adb);

// Poll mouse event from ADB device
bool adb_poll(adb_t *adb, mouse_event_t *out);

// Human-readable description of an adb_err_t.
const char* adb_error_str(adb_err_t err);

#endif