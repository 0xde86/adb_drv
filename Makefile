# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Denis Rodin
#
# Thin wrapper around CMake. Each target reconfigures its build directory
# only if no CMakeCache.txt exists yet.

BUILD_DIR       ?= build
TEST_BUILD_DIR  ?= test/build
PICOTOOL_DIR    ?= $(HOME)/.pico-sdk/picotool/2.2.0-a4
PICOTOOL        ?= $(PICOTOOL_DIR)/picotool/picotool
OPENOCD_DIR     ?= $(HOME)/.pico-sdk/openocd/0.12.0+dev
OPENOCD         ?= $(OPENOCD_DIR)/openocd
OPENOCD_SCRIPTS ?= $(OPENOCD_DIR)/scripts
OPENOCD_TARGET  ?= rp2350
UF2             := $(BUILD_DIR)/adb_drv.uf2
ELF             := $(BUILD_DIR)/adb_drv.elf

.PHONY: all build test static-check flash flash-swd clean help

all: build

build: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) --target adb_drv

$(BUILD_DIR)/CMakeCache.txt:
	cmake -S . -B $(BUILD_DIR)

test: $(TEST_BUILD_DIR)/CMakeCache.txt
	cmake --build $(TEST_BUILD_DIR)
	ctest --test-dir $(TEST_BUILD_DIR) --output-on-failure

$(TEST_BUILD_DIR)/CMakeCache.txt:
	cmake -S test -B $(TEST_BUILD_DIR)

static-check: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) --target clang-tidy

flash: build
	$(PICOTOOL) load $(UF2) -fx

flash-swd: build
	$(OPENOCD) -s $(OPENOCD_SCRIPTS) \
		-f interface/cmsis-dap.cfg \
		-f target/$(OPENOCD_TARGET).cfg \
		-c "adapter speed 5000; program $(ELF) verify reset exit"

clean:
	rm -rf $(BUILD_DIR) $(TEST_BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  build         build firmware ($(UF2))"
	@echo "  test          build & run host unit tests via CTest"
	@echo "  static-check  run clang-tidy on first-party sources"
	@echo "  flash         flash firmware via picotool (USB BOOTSEL)"
	@echo "  flash-swd     flash firmware via OpenOCD/SWD (CMSIS-DAP probe)"
	@echo "  clean         remove build directories"