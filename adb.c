#include <stdbool.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "pico/time.h"

#include "adb.h"
// auto-generated headers from PIO assembly
#include "adb_rx.pio.h"
#include "adb_tx.pio.h"

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

// Resource ownership bits for ADB::owned.
#define ADB_OWNS_TX_PROG (1u << 0)
#define ADB_OWNS_TX_SM   (1u << 1)
#define ADB_OWNS_RX_PROG (1u << 2)
#define ADB_OWNS_RX_SM   (1u << 3)

// forward decls
static adb_err_t adb_pio_init_tx(ADB *adb);
static adb_err_t adb_pio_init_rx(ADB *adb);
static mouse_event decode(uint16_t d);

// Release all resources aquired by adb
void adb_deinit(ADB *adb) {
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
        pio_remove_program(adb->pio, &adb_rx_gated_program, adb->rx_off);
    }
    adb->owned = 0;
}

adb_err_t adb_init(ADB *adb, PIO pio, uint pin) {
    adb->pio   = pio;
    adb->pin   = pin;
    adb->owned = 0;

    adb_err_t err = adb_pio_init_tx(adb);
    if (err != ADB_OK) {
        return err;
    }
    return adb_pio_init_rx(adb);
}

bool adb_poll(ADB *adb, mouse_event *out) {
    pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
    pio_sm_clear_fifos(adb->pio, adb->rx_sm);
    pio_sm_restart(adb->pio, adb->rx_sm);
    pio_sm_exec(adb->pio, adb->rx_sm, pio_encode_jmp(adb->rx_off)); // PC -> program start
    pio_interrupt_clear(adb->pio, 0);                       // TX-done flag
    pio_interrupt_clear(adb->pio, 1);                       // silence flag
    pio_sm_put(adb->pio, adb->rx_sm, TIMEOUT_CYC_COUNT);                     // preload the Tlt window
    pio_sm_set_enabled(adb->pio, adb->rx_sm, true);               // pull->mov x->wait irq0->seek

    pio_sm_put_blocking(adb->pio, adb->tx_sm, (uint32_t)CMD_TALK_ADDR3_REG0 << 24);

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
    uint16_t res = (uint16_t)(word & 0xFFFF);                  // drop the start bit
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

// decode Register 0 -> mouse event
// bit15 = button1 (0=pressed) -> left ; bits14:8 = Y, 7-bit signed
// bit7  = button2 (0=pressed) -> right; bits6:0  = X, 7-bit signed
static int seven_bit_signed(uint16_t v) {
    v &= 0x7F;
    return (v & 0x40) ? (int)v - 128 : (int)v;
}
static mouse_event decode(uint16_t d) {
    mouse_event e;
    e.dx    = (int8_t)seven_bit_signed(d);
    e.dy    = (int8_t)seven_bit_signed(d >> 8);
    e.left  = (d & 0x8000) == 0;
    e.right = (d & 0x0080) == 0;
    return e;
}

static adb_err_t adb_pio_init_tx(ADB *adb) {
    int off = pio_add_program(adb->pio, &adb_tx_program);
    if (off < 0) {
        return ADB_ERR_ADD_TX;
    }
    adb->tx_off = (uint)off;
    adb->owned |= ADB_OWNS_TX_PROG;

    int sm = pio_claim_unused_sm(adb->pio, false);
    if (sm < 0) {
        return ADB_ERR_CLAIM_TX_SM;
    }
    adb->tx_sm = (uint)sm;
    adb->owned |= ADB_OWNS_TX_SM;

    pio_sm_config c = adb_tx_program_get_default_config(adb->tx_off);
    sm_config_set_sideset_pins(&c, adb->pin);
    sm_config_set_out_shift(&c, false /*left*/, false /*no autopull*/, 8);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) * 5e-6f); // 5 us/cyc
    pio_gpio_init(adb->pio, adb->pin);
    pio_sm_set_pins_with_mask(adb->pio, adb->tx_sm, 0, 1u << adb->pin);   // value latch = 0
    pio_sm_set_pindirs_with_mask(adb->pio, adb->tx_sm, 0, 1u << adb->pin);  // start released
    if (pio_sm_init(adb->pio, adb->tx_sm, adb->tx_off, &c) < 0) {
        return ADB_ERR_INIT_TX_SM;
    }
    pio_sm_set_enabled(adb->pio, adb->tx_sm, true);
    return ADB_OK;
}

static adb_err_t adb_pio_init_rx(ADB *adb) {
    int off = pio_add_program(adb->pio, &adb_rx_gated_program);
    if (off < 0) {
        return ADB_ERR_ADD_RX;
    }
    adb->rx_off = (uint)off;
    adb->owned |= ADB_OWNS_RX_PROG;

    int sm = pio_claim_unused_sm(adb->pio, false);
    if (sm < 0) {
        return ADB_ERR_CLAIM_RX_SM;
    }
    adb->rx_sm = (uint)sm;
    adb->owned |= ADB_OWNS_RX_SM;

    pio_sm_config c = adb_rx_gated_program_get_default_config(adb->rx_off);
    sm_config_set_in_pins(&c, adb->pin);
    sm_config_set_jmp_pin(&c, adb->pin);                 // `jmp pin` tests the ADB line
    sm_config_set_in_shift(&c, false /*left*/, true /*autopush*/, 17);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) * 2e-6f); // 2 us/cyc
    if (pio_sm_init(adb->pio, adb->rx_sm, adb->rx_off, &c) < 0) {
        return ADB_ERR_INIT_RX_SM;
    }
    pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
    return ADB_OK;
}
