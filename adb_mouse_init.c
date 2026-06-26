#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/time.h"

#include "adb.h"
#include "adb_mouse_init.h"
#include "adb_pio_init.h"
// auto-generated headers
#include "adb_mouse_init.pio.h"
#include "adb_rx.pio.h"

// Talk Register 3 of the mouse address. Same envelope as Talk R0; only the
// register-select bits differ.
//        | address | command | register |
// 0x3F =    0011       11        11
#define CMD_TALK_ADDR3_REG3   0x3F

// Listen Register 3 (write to R3) of the mouse address.
//        | address | command | register |
// 0x3B =    0011       10        11
//   bits 7-4 = address 3
//   bits 3-2 = 10 (Listen)
//   bits 1-0 = 11 (Register 3)
#define CMD_LISTEN_ADDR3_REG3 0x3B

// Register 3 response layout (16 bits, big-endian as transmitted on the wire):
//   bits 15..12 : flags (SRQ enable, exception, etc.)
//   bits 11..8  : device address (4-bit, currently 3 for a mouse at default)
//   bits  7..0  : handler ID
//
// Handler IDs to probe when a device reports the bare-compatibility 0x02:
//   0x04 - Apple Extended Mouse Protocol (most third-party 2-button mice)
//   0x05 - Logitech "MouseMan" / various Logitech trackballs
//   0x06 - Kensington Turbo Mouse extended
//   0x09 - some Logitech / Lab Tec extended
//   0x2F - Microsoft BusMouse 4-button
// The list is order-sensitive: we stop at the first ID the device accepts.
static const uint8_t HANDLER_PROBES[] = { 0x04u, 0x05u, 0x06u, 0x09u, 0x2Fu };

// Listen R3 payload byte 0 = 0xFE is the conventional "keep current address;
// only update handler ID" sentinel used by hosts that just want to flip
// protocol mode without renumbering the bus.
#define LISTEN_R3_KEEP_ADDR       0xFEu

// Reconfigure the RX SM to run the RX program at adb->rx_off. The state
// machine remains claimed across the load/unload dance in do_listen.
static void configure_rx_sm(adb_t *adb) {
    pio_sm_config c = adb_rx_gated_program_get_default_config(adb->rx_off);
    sm_config_set_in_pins(&c, adb->pin);
    sm_config_set_jmp_pin(&c, adb->pin);
    sm_config_set_in_shift(&c, false /*left*/, true /*autopush*/, ADB_RX_TOTAL_BITS);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) * 2e-6f);
    pio_sm_init(adb->pio, adb->rx_sm, adb->rx_off, &c);
}

// Reconfigure the RX SM to run the mouse-init program at the given offset.
static void configure_mouse_init_sm(adb_t *adb, uint mi_off) {
    pio_sm_config c = adb_mouse_init_program_get_default_config(mi_off);
    sm_config_set_sideset_pins(&c, adb->pin);
    sm_config_set_out_shift(&c, false /*left*/, false /*no autopull*/, 16);
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) * 5e-6f);
    pio_sm_init(adb->pio, adb->rx_sm, mi_off, &c);
}

// Drive a raw Listen transaction with caller-supplied command byte and
// 16-bit payload. Temporarily unloads adb_rx_gated to free PIO instruction
// memory, loads adb_mouse_init in its place, then restores the RX program
// before returning. Returns true if the Listen completed (mouse_init raised
// IRQ 1) within the timeout. Caller must verify any state change with a
// follow-up Talk.
static bool do_listen(adb_t *adb, uint8_t listen_cmd, uint16_t payload) {
    // 1. Quiesce RX and free its program slots (17 instructions).
    pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
    pio_remove_program(adb->pio, &adb_rx_gated_program, adb->rx_off);
    adb->owned &= (uint8_t)~ADB_OWNS_RX_PROG;

    // 2. Load adb_mouse_init in the freed space.
    int mi_off_signed = pio_add_program(adb->pio, &adb_mouse_init_program);
    if (mi_off_signed < 0) {
        // Vanishingly unlikely (17 freed >> 16 needed) but keep the bus sane:
        // put RX back and bail.
        int rx_off = pio_add_program(adb->pio, &adb_rx_gated_program);
        if (rx_off >= 0) {
            adb->rx_off = (uint8_t)rx_off;
            adb->owned |= ADB_OWNS_RX_PROG;
            configure_rx_sm(adb);
        }
        return false;
    }
    uint mi_off = (uint)mi_off_signed;

    // 3. Repoint the (claimed) RX SM at mouse_init and prime its FIFO.
    //    Payload left-justified so first OSR `out` shifts the wire MSB first.
    configure_mouse_init_sm(adb, mi_off);
    pio_sm_put(adb->pio, adb->rx_sm, (uint32_t)payload << 16);

    // 4. Clear coordination IRQs and arm the mouse_init SM. It will wait on
    //    IRQ 0 (raised by adb_tx when the command byte is on the wire).
    pio_interrupt_clear(adb->pio, 0);
    pio_interrupt_clear(adb->pio, 1);
    pio_sm_set_enabled(adb->pio, adb->rx_sm, true);

    // 5. Kick off the Listen command via the existing TX SM.
    pio_sm_put_blocking(adb->pio, adb->tx_sm,
                        (uint32_t)listen_cmd << 24);

    // 6. Budget: TX cmd ~1.74 ms + Tlt 0.16 ms + start+16 data+stop ~1.8 ms
    //    ~= 3.7 ms. 10 ms is generous.
    absolute_time_t deadline = make_timeout_time_ms(10);
    bool done = false;
    while (!time_reached(deadline)) {
        if (pio_interrupt_get(adb->pio, 1)) {
            done = true;
            break;
        }
    }

    // 7. Tear down mouse_init regardless of outcome.
    pio_sm_set_enabled(adb->pio, adb->rx_sm, false);
    pio_remove_program(adb->pio, &adb_mouse_init_program, mi_off);

    // 8. Restore adb_rx_gated. The new offset may differ from the original
    //    (the allocator may not pick the same slot), so capture and reconfig.
    int rx_off = pio_add_program(adb->pio, &adb_rx_gated_program);
    if (rx_off < 0) {
        // Should be impossible: we just freed those slots. If it does happen,
        // the driver is broken from here on; flag failure to caller.
        return false;
    }
    adb->rx_off = (uint8_t)rx_off;
    adb->owned |= ADB_OWNS_RX_PROG;
    configure_rx_sm(adb);
    // Leave the SM disabled at rest. adb_talk re-enables it per transaction.

    return done;
}

// Command bytes built from (address << 4) | (cmd << 2) | reg.
// Talk    = bits 3-2 = 11; Listen = bits 3-2 = 10.
#define CMD_TALK_ADDRD_REG3   0xDF
#define CMD_LISTEN_ADDRD_REG3 0xDB

adb_err_t adb_mouse_init(adb_t *adb) {
    uint16_t r3;

    // ------------------------------------------------------------------
    // Baseline: does the device respond at addr 3, addr D, or both?
    // ------------------------------------------------------------------
    printf("=== adb_mouse_init: address probe ===\n");
    bool resp_3 = adb_talk(adb, CMD_TALK_ADDR3_REG3, &r3);
    if (resp_3) {
        printf("  Talk R3 @3 -> R3=0x%04x  handler=0x%02X\n",
               r3, (uint8_t)(r3 & 0xFFu));
    } else {
        printf("  Talk R3 @3 -> silence\n");
    }

    uint16_t r3_d = 0;
    bool resp_d = adb_talk(adb, CMD_TALK_ADDRD_REG3, &r3_d);
    if (resp_d) {
        printf("  Talk R3 @D -> R3=0x%04x  handler=0x%02X\n",
               r3_d, (uint8_t)(r3_d & 0xFFu));
    } else {
        printf("  Talk R3 @D -> silence\n");
    }

    if (!resp_3 && !resp_d) {
        printf("adb_mouse_init: no device responded; bailing.\n");
        return ADB_OK;
    }

    // ------------------------------------------------------------------
    // If the device answers at @D, try the handler-switch sequence there.
    // ------------------------------------------------------------------
    if (resp_d) {
        printf("=== handler switch via addr D ===\n");
        for (size_t i = 0; i < sizeof(HANDLER_PROBES); i++) {
            uint8_t target = HANDLER_PROBES[i];
            uint16_t payload = (uint16_t)((LISTEN_R3_KEEP_ADDR << 8) | target);
            if (!do_listen(adb, CMD_LISTEN_ADDRD_REG3, payload)) {
                printf("  @D h=0x%02X: Listen incomplete\n", target);
                continue;
            }
            if (!adb_talk(adb, CMD_TALK_ADDRD_REG3, &r3)) {
                printf("  @D h=0x%02X: verify silence\n", target);
                continue;
            }
            uint8_t got = (uint8_t)(r3 & 0xFFu);
            printf("  @D h=0x%02X: R3=0x%04x got=0x%02X%s\n",
                   target, r3, got,
                   got == target ? "  <-- ACCEPTED" : "");
            if (got == target) {
                printf("adb_mouse_init: device accepted handler 0x%02X @D.\n",
                       target);
                return ADB_OK;
            }
        }
    }

    // ------------------------------------------------------------------
    // Bit-meaning probe: Listen R3 @3 with byte 0 walking one-hot patterns,
    // byte 1 fixed at 0x02 (current handler - no protocol change requested).
    // Map which response bits each input bit moves.
    // ------------------------------------------------------------------
    if (resp_3) {
        printf("=== Listen R3 @3 bit probe (b1=02) ===\n");
        static const uint8_t b0_probes[] = {
            0x00u, 0xFFu,
            0x01u, 0x02u, 0x04u, 0x08u,
            0x10u, 0x20u, 0x40u, 0x80u,
            0xFEu, 0x7Eu,
        };
        for (size_t i = 0; i < sizeof(b0_probes); i++) {
            uint8_t b0 = b0_probes[i];
            uint16_t payload = (uint16_t)((b0 << 8) | 0x02u);
            if (!do_listen(adb, CMD_LISTEN_ADDR3_REG3, payload)) {
                printf("  b0=0x%02X: Listen incomplete\n", b0);
                continue;
            }
            if (!adb_talk(adb, CMD_TALK_ADDR3_REG3, &r3)) {
                printf("  b0=0x%02X: verify silence\n", b0);
                continue;
            }
            printf("  b0=0x%02X: R3=0x%04x\n", b0, r3);
        }
    }

    return ADB_OK;
}

void adb_mouse_deinit(adb_t *adb) {
    (void)adb;
}
