// PIO state-machine setup/teardown for the ADB driver. Loads/unloads the
// adb_tx and adb_rx_gated programs and claims the matching state machines.
#ifndef ADB_PIO_INIT_H
#define ADB_PIO_INIT_H

#include "adb.h"

// Ownership bits for adb_t::owned. Each bit is set by the matching
// adb_pio_init_* on success and cleared by adb_pio_deinit_* on release, so
// deinit is safe to call on a partially-initialized adb_t.
#define ADB_OWNS_TX_PROG (1u << 0)
#define ADB_OWNS_TX_SM   (1u << 1)
#define ADB_OWNS_RX_PROG (1u << 2)
#define ADB_OWNS_RX_SM   (1u << 3)

// Mouse Register 0 wire width. The RX PIO program samples bits indefinitely;
// the FIFO push boundary is driven entirely by the C-side autopush threshold
// (= start bit + data bits). Both autopush threshold and post-pop mask are
// derived from ADB_RX_DATA_BITS so changing the width touches one place.
#define ADB_RX_DATA_BITS  16
#define ADB_RX_TOTAL_BITS (ADB_RX_DATA_BITS + 1)            // +1 start bit
#define ADB_RX_DATA_MASK  ((1u << ADB_RX_DATA_BITS) - 1u)

adb_err_t adb_pio_init_tx(adb_t *adb);
adb_err_t adb_pio_init_rx(adb_t *adb);

void adb_pio_deinit_tx(adb_t *adb);
void adb_pio_deinit_rx(adb_t *adb);

#endif