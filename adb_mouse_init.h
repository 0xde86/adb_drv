// One-time mouse handshake on the ADB bus. Queries the device's handler ID
// (Register 3) and, if supported, switches it to Apple Extended Mouse
// Protocol (handler ID 0x04) so two-button replies are exposed.
//
// Standalone PIO program adb_mouse_init.pio is dedicated to driving the
// Listen-Register-3 sequence, which the polling adb_tx program does not
// support. The SM and program load are claimed on entry and released on
// exit (or on failure via adb_mouse_deinit).
#ifndef ADB_MOUSE_INIT_H
#define ADB_MOUSE_INIT_H

#include "adb.h"

adb_err_t adb_mouse_init(adb_t *adb);
void      adb_mouse_deinit(adb_t *adb);

#endif