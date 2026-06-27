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

#include "adb_decode.h"

typedef enum : int8_t {
    ADB_OK              =  0,
    ADB_ERR_ADD_TX      = -1,
    ADB_ERR_CLAIM_TX_SM = -2,
    ADB_ERR_INIT_TX_SM  = -3,
    ADB_ERR_ADD_RX      = -4,
    ADB_ERR_CLAIM_RX_SM = -5,
    ADB_ERR_INIT_RX_SM  = -6,
} adb_err_t;

typedef enum : uint8_t {
    ADB_IDLE    = 0,
    ADB_POLLING = 1,
} adb_state_t;

// Main struct for handling ADB protocol.
// This includes:
//  - managing PIO state machines for communicating with ADB device;
//  - sending commands / receiving reports;
//  - decoding mouse movement
typedef struct {
    absolute_time_t poll_time;
    PIO pio;
    adb_state_t state;
    uint8_t pin;
    uint8_t tx_sm;
    uint8_t tx_off;
    uint8_t rx_sm;
    uint8_t rx_off;
    uint8_t owned;  // bitmask of acquired resources (see ADB_OWNS_* in adb.c)
} adb_t;

/**
 * Initialize the ADB driver on the given PIO and pin.
 * Loads the TX and RX PIO programs and claims one state machine for each.
 *
 * @param adb  Driver instance to initialize. Must not be NULL.
 * @param pio  PIO instance to use (e.g. pio0).
 * @param pin  GPIO pin connected to the ADB data line.
 * @return     ADB_OK on success, or a negative ::adb_err_t on failure.
 *             On failure, partially-claimed resources are recorded in `adb`;
 *             the caller must call adb_deinit() to release them.
 */
adb_err_t adb_init(adb_t *adb, PIO pio, uint8_t pin)
    __attribute__((nonnull(1), warn_unused_result));

/**
 * Release all PIO resources owned by `adb`.
 * Safe to call on a partially-initialized driver and idempotent
 *
 * @param adb  Driver instance. Must not be NULL.
 */
void adb_deinit(adb_t *adb) __attribute__((nonnull));

/**
 * Issue a Talk Register 0 to the mouse at address 3 and decode the reply.
 * Can be called in a tight loop. Proper time intervals for polling
 * ADB hardware are handled automatically via internal state. 
 *
 * @param adb  Initialized driver instance. Must not be NULL.
 * @param out  Destination for the decoded event. Must not be NULL.
 *             Written only when the function returns true.
 * @return     true if a reply was received and decoded into `*out`;
 *             false on silence (no device responded), timeout, or on
 *             waiting for response from device.
 */
bool adb_poll(adb_t *adb, mouse_event_t *out) __attribute__((nonnull));

/**
 * Human-readable description of an ::adb_err_t value.
 *
 * @param err  Error code, returned by adb_init().
 * @return     Pointer to a constant string literal; never NULL.
 */
const char* adb_error_str(adb_err_t err);

#endif