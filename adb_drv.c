// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Denis Rodin

#include <stdio.h>

#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

#include "adb.h"

// GPIO pin which will be used to connect to ADB device (via level shifter)
#define ADB_PIN 2

int main(void) {
    stdio_init_all();

    if (watchdog_caused_reboot()) {
        printf("Rebooted by Watchdog!\n");
    }

    adb_t adb;
    adb_err_t adb_err = adb_init(&adb, pio0, ADB_PIN);
    if (adb_err != ADB_OK) {
        printf("Failed to initialize ADB module: %s\n", adb_error_str(adb_err));
        adb_deinit(&adb);
        watchdog_reboot(0, 0, 0);
        while (1) { tight_loop_contents(); }
    }
    
    watchdog_enable(100, 1);
    watchdog_update();

    printf("System Clock Frequency is %lu Hz\n", (unsigned long)clock_get_hz(clk_sys));
    printf("USB Clock Frequency is %lu Hz\n", (unsigned long)clock_get_hz(clk_usb));
    // For more examples of clocks use see https://github.com/raspberrypi/pico-examples/tree/master/clocks

    absolute_time_t next = make_timeout_time_ms(ADB_POLL_INTERVAL_MS);
    while (true) {
        // tud_task();                                  // drain deferred USB events
        if (time_reached(next)) {
            next = make_timeout_time_ms(ADB_POLL_INTERVAL_MS);
            watchdog_update();
            mouse_event_t e;
            if (adb_poll(&adb, &e)/*&& tud_hid_ready()*/) {
                // uint8_t btn = (uint8_t)((e.left  ? MOUSE_BUTTON_LEFT  : 0)
                //                       | (e.right ? MOUSE_BUTTON_RIGHT : 0));
                // tud_hid_mouse_report(0, btn, e.dx, e.dy, 0, 0);
                printf("mouse: dx=%+4d dy=%+4d L=%d R=%d\n",
                       e.dx, e.dy, e.left, e.right);
            }
        }
    }
}
