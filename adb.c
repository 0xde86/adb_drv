#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"
#include "pico/time.h"

#include "adb.h"
#include "adb_mouse_init.h"
#include "adb_pio_init.h"

// Number of PIO cycles to wait for response from ADB device
// 75 * 2 * 2us/cyc ~= 304us - Covers Tlt = 140-260 us (Guide/AN591)
#define TIMEOUT_CYC_COUNT 75

// The command byte
//        | address | command | register |
// 0x3C =    0011       11        00
//
// address (bits 7-4): 11 (decimal 3) - Mouse
// command (bits 3-2): 11             - Talk (10 - Listen, 01 - Flush, 00 - Reset 00)
// register(bits 1-0): 00             - Register 0
#define CMD_TALK_ADDR3_REG0 0x3C

// Mouse Register 0 reply layout. Each axis is a 7-bit signed delta; the
// button bits are active-LOW (0 == pressed).
//
//   bit  15 : button1 -> left   (ADB_BTN1_MASK)
//   bits 14..8 : Y delta, 7-bit signed
//   bit  7  : button2 -> right  (ADB_BTN2_MASK)
//   bits 6..0  : X delta, 7-bit signed
#define ADB_MOUSE_AXIS_BITS 7
#define ADB_MOUSE_AXIS_MASK ((1u << ADB_MOUSE_AXIS_BITS) - 1u)
#define ADB_MOUSE_AXIS_SIGN (1u << (ADB_MOUSE_AXIS_BITS - 1))
#define ADB_MOUSE_AXIS_BIAS (1u <<  ADB_MOUSE_AXIS_BITS)
#define ADB_BTN1_MASK       0x8000u
#define ADB_BTN2_MASK       0x0080u

static mouse_event_t decode(uint16_t d);

adb_err_t adb_init(adb_t *adb, PIO pio, uint8_t pin) {
    adb->pio   = pio;
    adb->pin   = pin;
    adb->owned = 0;

    adb_err_t err = adb_pio_init_tx(adb);
    if (err != ADB_OK) {
        adb_deinit(adb);
        return err;
    }
    err = adb_pio_init_rx(adb);
    if (err != ADB_OK) {
        adb_deinit(adb);
        return err;
    }
    err = adb_mouse_init(adb);
    if (err != ADB_OK) {
        adb_deinit(adb);
        return err;
    }
    return ADB_OK;
}

void adb_deinit(adb_t *adb) {
    adb_mouse_deinit(adb);
    adb_pio_deinit_rx(adb);
    adb_pio_deinit_tx(adb);
}

bool adb_talk(adb_t *adb, uint8_t cmd, uint16_t *response) {
    pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
    pio_sm_clear_fifos(adb->pio, adb->rx_sm);
    pio_sm_restart(adb->pio, adb->rx_sm);
    pio_sm_exec(adb->pio, adb->rx_sm, pio_encode_jmp(adb->rx_off)); // PC -> program start
    pio_interrupt_clear(adb->pio, 0);                       // TX-done flag
    pio_interrupt_clear(adb->pio, 1);                       // silence flag
    pio_sm_put(adb->pio, adb->rx_sm, TIMEOUT_CYC_COUNT);                     // preload the Tlt window
    pio_sm_set_enabled(adb->pio, adb->rx_sm, true);               // pull->mov x->wait irq0->seek

    pio_sm_put_blocking(adb->pio, adb->tx_sm, (uint32_t)cmd << 24);

    // Safety net only (silence is signalled by IRQ 1 long before this). Sized
    // to the full transaction: TX ~1.74 ms + Tlt <=0.26 ms + reply sampling
    // ~1.7 ms => word arrives ~3.7 ms after the command push. 6 ms >> that.
    absolute_time_t deadline = make_timeout_time_ms(6);
    while (pio_sm_is_rx_fifo_empty(adb->pio, adb->rx_sm)) {
        if (pio_interrupt_get(adb->pio, 1)) { // RX reported SILENCE
            pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
            return false;
        }
        if (time_reached(deadline)) {
            pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
            return false;
        }
    }
    uint32_t word = pio_sm_get(adb->pio, adb->rx_sm);
    pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
    *response = (uint16_t)(word & ADB_RX_DATA_MASK);        // drop the start bit
    return true;
}

bool adb_poll(adb_t *adb, mouse_event_t *out) {
    uint16_t res;
    if (!adb_talk(adb, CMD_TALK_ADDR3_REG0, &res)) {
        return false;
    }
    *out = decode(res);
    return true;
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

static int seven_bit_signed(uint16_t v) {
    v &= ADB_MOUSE_AXIS_MASK;
    return (v & ADB_MOUSE_AXIS_SIGN) ? (int)v - (int)ADB_MOUSE_AXIS_BIAS : (int)v;
}

static mouse_event_t decode(uint16_t d) {
    mouse_event_t e;
    e.dx    = (int8_t)seven_bit_signed(d);
    e.dy    = (int8_t)seven_bit_signed(d >> 8);
    e.left  = (d & ADB_BTN1_MASK) == 0;
    e.right = (d & ADB_BTN2_MASK) == 0;
    return e;
}