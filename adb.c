// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Denis Rodin

#include <stdbool.h>
#include <stdint.h>

#include "adb_decode.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/time.h"

#include "adb.h"
// auto-generated headers from PIO assembly
#include "adb_rx.pio.h"
#include "adb_tx.pio.h"

// In the book Guide_to_Macintosh_Family_Hardware there are several mentions
// that ADB manager polls devices every 11ms. Documentation from tmk_keyboard repo also
// mentions 11ms as poll interval for active device.
#define ADB_POLL_INTERVAL_MS 11

// PIO state-machine clock periods
#define ADB_TX_CYC_US 5U
#define ADB_RX_CYC_US 2U

// Convert microseconds to seconds for sm_config_set_clkdiv().
#define ADB_US_TO_S 1e-6F

// TX FIFO is 32-bit, command byte is loaded into the top 8 bits so the
// PIO program reads them out first via OUT shifting left.
#define ADB_TX_FIFO_CMD_SHIFT 24U

// Number of Tlt-window iterations the RX state machine waits for the device
// to assert the start bit. Each iteration is 2 PIO instructions.
// 75 * 2 * ADB_RX_CYC_US ~= 300 us - covers Tlt = 140-260 us (Guide/AN591).
#define TIMEOUT_CYC_COUNT 75

// The command byte
//        | address | command | register |
// 0x3C =    0011       11        00
//
// address (bits 7-4): 11 (decimal 3) - Mouse
// command (bits 3-2): 11             - Talk (10 - Listen, 01 - Flush, 00 - Reset 00)
// register(bits 1-0): 00             - Register 0
#define CMD_TALK_ADDR3_REG0 0x3C

// Resource ownership bits for ADB::owned.
#define ADB_OWNS_TX_PROG (1U << 0)
#define ADB_OWNS_TX_SM   (1U << 1)
#define ADB_OWNS_RX_PROG (1U << 2)
#define ADB_OWNS_RX_SM   (1U << 3)

// Mouse Register 0 wire width
#define ADB_RX_DATA_BITS  16
#define ADB_RX_TOTAL_BITS (ADB_RX_DATA_BITS + 1) // +1 start bit
#define ADB_RX_DATA_MASK  ((1U << ADB_RX_DATA_BITS) - 1U)

// Worst-case wire time for one poll. Polls must not overlap.
//   TX command   349 cyc * ADB_TX_CYC_US (see adb_tx.pio: attn 161 + sync 14
//                + 8*20 bits + stop 14 = 349 cyc -> ~1.74 ms)
//   Tlt window   TIMEOUT_CYC_COUNT * 2 PIO instr/iter * ADB_RX_CYC_US
//   RX reply     ADB_RX_TOTAL_BITS * 100 us  (100 us per bit on the wire)
#define ADB_TX_CYCLES        349u
#define ADB_TX_TIME_US       (ADB_TX_CYCLES * ADB_TX_CYC_US)
#define ADB_TLT_MAX_US       (TIMEOUT_CYC_COUNT * 2U * ADB_RX_CYC_US)
#define ADB_RX_REPLY_TIME_US (ADB_RX_TOTAL_BITS * 100u)
#define FALLBACK_TIMEOUT_MS  6u
_Static_assert(ADB_POLL_INTERVAL_MS * 1000U >
                   ADB_TX_TIME_US + ADB_TLT_MAX_US + ADB_RX_REPLY_TIME_US,
    "ADB_POLL_INTERVAL_MS must exceed worst-case TX+Tlt+RX transaction time");
_Static_assert(ADB_POLL_INTERVAL_MS * 1000U >
                   ADB_TX_TIME_US + (FALLBACK_TIMEOUT_MS * 1000U),
    "ADB_POLL_INTERVAL_MS must exceed fallback TX+fallback timeout");

// forward decls
static adb_err_t adb_pio_init_tx(adb_t *adb);
static adb_err_t adb_pio_init_rx(adb_t *adb);

// Release all resources aquired by adb
void adb_deinit(adb_t *adb) {
    if (adb->owned & ADB_OWNS_TX_SM) {
        pio_sm_set_enabled(adb->pio, adb->tx_sm, false);
        pio_sm_unclaim(adb->pio, adb->tx_sm);
    }
    if (adb->owned & ADB_OWNS_TX_PROG) {
        pio_remove_program(adb->pio, &adb_tx_program, adb->tx_off);
    }
    if (adb->owned & ADB_OWNS_RX_SM) {
        pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
        pio_sm_unclaim(adb->pio, adb->rx_sm);
    }
    if (adb->owned & ADB_OWNS_RX_PROG) {
        pio_remove_program(adb->pio, &adb_rx_program, adb->rx_off);
    }
    adb->owned = 0;
}

adb_err_t adb_init(adb_t *adb, PIO pio, uint8_t pin) {
    adb->pio   = pio;
    adb->pin   = pin;
    adb->owned = 0;
    adb->state = ADB_IDLE;
    adb->poll_time = 0;

    adb_err_t err = adb_pio_init_tx(adb);
    if (err != ADB_OK) {
        return err;
    }
    return adb_pio_init_rx(adb);
}

bool adb_poll(adb_t *adb, mouse_event_t *out) {
    absolute_time_t next_poll = delayed_by_ms(adb->poll_time, ADB_POLL_INTERVAL_MS);
    absolute_time_t deadline = delayed_by_ms(adb->poll_time, FALLBACK_TIMEOUT_MS);
    if (adb->state == ADB_IDLE && time_reached(next_poll)) {
        adb->poll_time = get_absolute_time();
        pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
        pio_sm_clear_fifos(adb->pio, adb->rx_sm);
        pio_sm_restart(adb->pio, adb->rx_sm);
        pio_sm_exec(adb->pio, adb->rx_sm, pio_encode_jmp(adb->rx_off)); // PC -> program start
        pio_interrupt_clear(adb->pio, 0);                       // TX-done flag
        pio_interrupt_clear(adb->pio, 1);                       // silence flag
        pio_sm_put(adb->pio, adb->rx_sm, TIMEOUT_CYC_COUNT);             // preload the Tlt window
        pio_sm_set_enabled(adb->pio, adb->rx_sm, true);               // pull->mov x->wait irq0->seek
        pio_sm_put_blocking(adb->pio, adb->tx_sm, (uint32_t)CMD_TALK_ADDR3_REG0 << ADB_TX_FIFO_CMD_SHIFT);
        adb->state = ADB_POLLING;
        return false;
    }
    if (adb->state == ADB_POLLING) {
        if (!pio_sm_is_rx_fifo_empty(adb->pio, adb->rx_sm)) {
            uint32_t word = pio_sm_get(adb->pio, adb->rx_sm);
            pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
            uint16_t res = (uint16_t)(word & ADB_RX_DATA_MASK); // drop the start bit
            *out = adb_decode_mouse(res);
            adb->state = ADB_IDLE;
            return true;
        }
        if (pio_interrupt_get(adb->pio, 1) || time_reached(deadline)) {
            pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
            pio_interrupt_clear(adb->pio, 1);
            adb->state = ADB_IDLE;
            return false;
        }
    }
    return false;
}

const char *adb_error_str(adb_err_t err) {
    switch (err) {
    case ADB_OK:              return "OK";
    case ADB_ERR_ADD_TX:      return "PIO: failed to add TX program";
    case ADB_ERR_CLAIM_TX_SM: return "PIO: failed to claim TX state machine";
    case ADB_ERR_INIT_TX_SM:  return "PIO: failed to init TX state machine";
    case ADB_ERR_ADD_RX:      return "PIO: failed to add RX program";
    case ADB_ERR_CLAIM_RX_SM: return "PIO: failed to claim RX state machine";
    case ADB_ERR_INIT_RX_SM:  return "PIO: failed to init RX state machine";
    }
    return "Unknown ADB error";
}

static adb_err_t adb_pio_init_tx(adb_t *adb) {
    int off = pio_add_program(adb->pio, &adb_tx_program);
    if (off < 0) {
        return ADB_ERR_ADD_TX;
    }
    adb->tx_off = (uint8_t)off;
    adb->owned |= ADB_OWNS_TX_PROG;

    int sm = pio_claim_unused_sm(adb->pio, false);
    if (sm < 0) {
        return ADB_ERR_CLAIM_TX_SM;
    }
    adb->tx_sm = (uint8_t)sm;
    adb->owned |= ADB_OWNS_TX_SM;

    pio_sm_config cfg = adb_tx_program_get_default_config(adb->tx_off);
    sm_config_set_sideset_pins(&cfg, adb->pin);
    sm_config_set_out_shift(&cfg, false /*left*/, false /*no autopull*/, 8);
    sm_config_set_clkdiv(&cfg, (float)clock_get_hz(clk_sys) * (float)ADB_TX_CYC_US * ADB_US_TO_S);
    pio_gpio_init(adb->pio, adb->pin);
    pio_sm_set_pins_with_mask(adb->pio, adb->tx_sm, 0, 1U << adb->pin);   // value latch = 0
    pio_sm_set_pindirs_with_mask(adb->pio, adb->tx_sm, 0, 1U << adb->pin);  // start released
    if (pio_sm_init(adb->pio, adb->tx_sm, adb->tx_off, &cfg) < 0) {
        return ADB_ERR_INIT_TX_SM;
    }
    pio_sm_set_enabled(adb->pio, adb->tx_sm, true);
    return ADB_OK;
}

static adb_err_t adb_pio_init_rx(adb_t *adb) {
    int off = pio_add_program(adb->pio, &adb_rx_program);
    if (off < 0) {
        return ADB_ERR_ADD_RX;
    }
    adb->rx_off = (uint8_t)off;
    adb->owned |= ADB_OWNS_RX_PROG;

    int sm = pio_claim_unused_sm(adb->pio, false);
    if (sm < 0) {
        return ADB_ERR_CLAIM_RX_SM;
    }
    adb->rx_sm = (uint8_t)sm;
    adb->owned |= ADB_OWNS_RX_SM;

    pio_sm_config cfg = adb_rx_program_get_default_config(adb->rx_off);
    sm_config_set_in_pins(&cfg, adb->pin);
    sm_config_set_jmp_pin(&cfg, adb->pin);                 // `jmp pin` tests the ADB line
    sm_config_set_in_shift(&cfg, false /*left*/, true /*autopush*/, ADB_RX_TOTAL_BITS);
    sm_config_set_clkdiv(&cfg, (float)clock_get_hz(clk_sys) * (float)ADB_RX_CYC_US * ADB_US_TO_S);
    if (pio_sm_init(adb->pio, adb->rx_sm, adb->rx_off, &cfg) < 0) {
        return ADB_ERR_INIT_RX_SM;
    }
    pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
    return ADB_OK;
}
