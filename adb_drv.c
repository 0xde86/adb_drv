// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Denis Rodin

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/watchdog.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "adb.h"
#include "mouse_math.h"

// GPIO pin which will be used to connect to ADB device (via level shifter)
#define ADB_PIN 2

// Watchdog timeout — must be larger than one poll interval so a healthy
// main loop never trips it.
#define ADB_WATCHDOG_TIMEOUT_MS 100

#ifndef NDEBUG
#define DBG(...) printf(__VA_ARGS__)
#else
#define DBG(...) ((void)sizeof(printf(__VA_ARGS__)))
#endif

static bool usb_remote_wakeup_enabled;
static bool usb_remote_wakeup_requested;

// Drain pending mouse state to the HID endpoint. Whatever bytes actually went
// out are subtracted from the accumulators so clamped overflow retries later.
// Does nothing (and does not block) when the endpoint is busy.
static void flush_mouse(int32_t *pend_dx, int32_t *pend_dy,
                        uint8_t cur_btn, uint8_t *sent_btn) {
    if (*pend_dx == 0 && *pend_dy == 0 && cur_btn == *sent_btn) { return; }
    if (!tud_hid_ready()) { return; }

    int8_t delta_x = mouse_clamp_i8(*pend_dx);
    int8_t delta_y = mouse_clamp_i8(*pend_dy);
    if (!tud_hid_mouse_report(0, cur_btn, delta_x, delta_y, 0, 0)) { return; }

    *pend_dx -= delta_x;
    *pend_dy -= delta_y;
    *sent_btn = cur_btn;
}

static void request_remote_wakeup(void) {
    if (tud_suspended() && usb_remote_wakeup_enabled &&
        !usb_remote_wakeup_requested) {
        usb_remote_wakeup_requested = tud_remote_wakeup();
    }
}

static uint8_t adb_buttons_to_hid(const mouse_event_t *e) {
    uint8_t btn = 0;
    if (e->left)  { btn |= MOUSE_BUTTON_LEFT; }
    if (e->right) { btn |= MOUSE_BUTTON_RIGHT; }
    return btn;
}

int main(void) {
    stdio_init_all();
    tud_init(BOARD_TUD_RHPORT);

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

    watchdog_enable(ADB_WATCHDOG_TIMEOUT_MS, true);
    watchdog_update();

    printf("System Clock Frequency is %lu Hz\n", (unsigned long)clock_get_hz(clk_sys));
    printf("USB Clock Frequency is %lu Hz\n", (unsigned long)clock_get_hz(clk_usb));
    // For more examples of clocks use see https://github.com/raspberrypi/pico-examples/tree/master/clocks

    int32_t pend_dx  = 0;
    int32_t pend_dy  = 0;
    uint8_t cur_btn  = 0;
    uint8_t sent_btn = 0;

    while (true) {
        watchdog_update();
        tud_task();
        flush_mouse(&pend_dx, &pend_dy, cur_btn, &sent_btn);

        mouse_event_t e;
        if (adb_poll(&adb, &e)) {
            pend_dx = mouse_accumulate_i8(pend_dx, e.dx);
            pend_dy = mouse_accumulate_i8(pend_dy, e.dy);
            cur_btn = adb_buttons_to_hid(&e);
            request_remote_wakeup();
            DBG("mouse: dx=%+4d dy=%+4d L=%d R=%d pend=%+" PRId32
                ",%+" PRId32 "\n",
                e.dx, e.dy, (int)e.left, (int)e.right,
                pend_dx, pend_dy);
        }
    }
}

void tud_mount_cb(void) {
    usb_remote_wakeup_enabled = false;
    usb_remote_wakeup_requested = false;
    DBG("USB: mounted\n");
}

void tud_umount_cb(void) {
    usb_remote_wakeup_enabled = false;
    usb_remote_wakeup_requested = false;
    DBG("USB: unmounted\n");
}

void tud_suspend_cb(bool remote_wakeup) {
    usb_remote_wakeup_enabled = remote_wakeup;
    usb_remote_wakeup_requested = false;
    DBG("USB: suspended; remote wakeup %s\n", remote_wakeup ? "enabled" : "disabled");
}

void tud_resume_cb(void) {
    usb_remote_wakeup_enabled = false;
    usb_remote_wakeup_requested = false;
    DBG("USB: resumed\n");
}
