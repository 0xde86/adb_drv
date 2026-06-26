#include "hardware/clocks.h"
#include "hardware/pio.h"

#include "adb_pio_init.h"
// auto-generated headers from PIO assembly
#include "adb_rx.pio.h"
#include "adb_tx.pio.h"

adb_err_t adb_pio_init_tx(adb_t *adb) {
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

adb_err_t adb_pio_init_rx(adb_t *adb) {
    int off = pio_add_program(adb->pio, &adb_rx_gated_program);
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

    pio_sm_config c = adb_rx_gated_program_get_default_config(adb->rx_off);
    sm_config_set_in_pins(&c, adb->pin);
    sm_config_set_jmp_pin(&c, adb->pin);                 // `jmp pin` tests the ADB line
    sm_config_set_in_shift(&c, false /*left*/, true /*autopush*/, ADB_RX_TOTAL_BITS);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) * 2e-6f); // 2 us/cyc
    if (pio_sm_init(adb->pio, adb->rx_sm, adb->rx_off, &c) < 0) {
        return ADB_ERR_INIT_RX_SM;
    }
    pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
    return ADB_OK;
}

void adb_pio_deinit_tx(adb_t *adb) {
    if (adb->owned & ADB_OWNS_TX_SM) {
        pio_sm_set_enabled(adb->pio, adb->tx_sm, false);
        pio_sm_unclaim(adb->pio, adb->tx_sm);
        adb->owned &= (uint8_t)~ADB_OWNS_TX_SM;
    }
    if (adb->owned & ADB_OWNS_TX_PROG) {
        pio_remove_program(adb->pio, &adb_tx_program, adb->tx_off);
        adb->owned &= (uint8_t)~ADB_OWNS_TX_PROG;
    }
}

void adb_pio_deinit_rx(adb_t *adb) {
    if (adb->owned & ADB_OWNS_RX_SM) {
        pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
        pio_sm_unclaim(adb->pio, adb->rx_sm);
        adb->owned &= (uint8_t)~ADB_OWNS_RX_SM;
    }
    if (adb->owned & ADB_OWNS_RX_PROG) {
        pio_remove_program(adb->pio, &adb_rx_gated_program, adb->rx_off);
        adb->owned &= (uint8_t)~ADB_OWNS_RX_PROG;
    }
}