# adb_drv

ADB (Apple Desktop Bus) driver for the Raspberry Pi Pico 2 (RP2350). Polls an
Apple ADB mouse and re-emits mouse events as USB HID mouse. USB machinery is
handled by TinyUSB SDK.

## Hardware

- **MCU**: Raspberry Pi Pico 2 (`PICO_BOARD=pico2`).
- **ADB pin**: GPIO 2, configured in [adb_drv.c:10](adb_drv.c#L10).
- **Level shifter**: required. ADB is a 5 V open-drain bus; the Pico is 3.3 V.
  Use a bidirectional level shifter between the Pico GPIO and the ADB data
  line, and supply the device with 5 V on the ADB connector's power pin.

## Protocol

Implementation is based on information from:
 - https://developer.apple.com/library/archive/documentation/mac/pdf/Devices/ADB_Manager.pdf
 - https://vintageapple.org/inside_o/pdf/Guide_to_Macintosh_Family_Hardware_2nd_Edition_1990.pdf
 - https://github.com/tmk/tmk_keyboard/wiki/Apple-Desktop-Bus

## PIO programs

- [adb_tx.pio](adb_tx.pio) - drives the command byte with open-drain via
  side-set on `PINDIRS` (5 µs/cycle). Raises IRQ 0 when the stop bit is done.
- [adb_rx.pio](adb_rx.pio) - gates on IRQ 0, hunts for the device's start bit
  within the Tlt window, then samples the start bit + 16 data bits at
  2 µs/cycle. Raises IRQ 1 if the window expires (no response from device).

## Make targets

```bash
make build         # build firmware (build/adb_drv.uf2)
make test          # build & run host unit tests via CTest
make static-check  # run clang-tidy
make fuzz          # run libFuzzer on adb_decode_mouse() just for fun :)
make flash         # flash via picotool over USB (BOOTSEL)
make flash-swd     # flash via OpenOCD/SWD (CMSIS-DAP probe)
make clean         # remove build/ and test/build/
make help          # list the above
```

`make flash` uses the SDK's `picotool` and only needs a USB cable — the
running firmware reboots itself into BOOTSEL. `make flash-swd` uses the
SDK's bundled OpenOCD and a CMSIS-DAP probe (Raspberry Pi Debug Probe or a
second Pico running picoprobe firmware).

Override the SDK paths via env if your install lives elsewhere:
`PICOTOOL_DIR=`, `OPENOCD_DIR=`, `OPENOCD_TARGET=` (defaults to `rp2350`).