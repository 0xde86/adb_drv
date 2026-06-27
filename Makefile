# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Denis Rodin
#
# Thin wrapper around CMake. Each target reconfigures its build directory
# only if no CMakeCache.txt exists yet.

BUILD_DIR       ?= build
TEST_BUILD_DIR  ?= test/build
FUZZ_BUILD_DIR  ?= test/build-fuzz
FUZZ_TIME       ?= 10
# Release strips hot-path DBG() prints; Debug keeps them.
BUILD_TYPE      ?= Release
PICOTOOL_DIR    ?= $(HOME)/.pico-sdk/picotool/2.2.0-a4
PICOTOOL        ?= $(PICOTOOL_DIR)/picotool/picotool
OPENOCD_DIR     ?= $(HOME)/.pico-sdk/openocd/0.12.0+dev
OPENOCD         ?= $(OPENOCD_DIR)/openocd
OPENOCD_SCRIPTS ?= $(OPENOCD_DIR)/scripts
OPENOCD_TARGET  ?= rp2350
STRIP           ?= arm-none-eabi-strip
UF2             := $(BUILD_DIR)/adb_drv.uf2
ELF             := $(BUILD_DIR)/adb_drv.elf

.PHONY: all build strip test fuzz tidy flash flash-swd clean help

all: help

build: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) --target adb_drv

strip: build
	$(STRIP) --strip-debug $(ELF)

$(BUILD_DIR)/CMakeCache.txt:
	cmake -G Ninja -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

test: $(TEST_BUILD_DIR)/CMakeCache.txt
	cmake --build $(TEST_BUILD_DIR)
	ctest --test-dir $(TEST_BUILD_DIR) --output-on-failure

$(TEST_BUILD_DIR)/CMakeCache.txt:
	cmake -S test -B $(TEST_BUILD_DIR)

tidy: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) --target clang-tidy

fuzz: $(FUZZ_BUILD_DIR)/CMakeCache.txt
	cmake --build $(FUZZ_BUILD_DIR) --target fuzz_decode
	$(FUZZ_BUILD_DIR)/fuzz_decode -max_total_time=$(FUZZ_TIME)

$(FUZZ_BUILD_DIR)/CMakeCache.txt:
	CC=clang cmake -S test -B $(FUZZ_BUILD_DIR)

flash: build
	$(PICOTOOL) load $(UF2) -fx

flash-swd: build
	$(OPENOCD) -s $(OPENOCD_SCRIPTS) \
		-f interface/cmsis-dap.cfg \
		-f target/$(OPENOCD_TARGET).cfg \
		-c "adapter speed 5000; program $(ELF) verify reset exit"

clean:
	rm -rf $(BUILD_DIR) $(TEST_BUILD_DIR) $(FUZZ_BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  build         build firmware ($(UF2))"
	@echo "  strip         build firmware, then strip debug info from $(ELF)"
	@echo "  test          build & run host unit tests via CTest"
	@echo "  tidy          run clang-tidy"
	@echo "  fuzz          run libFuzzer harness for $(FUZZ_TIME)s (for fun only)"
	@echo "  flash         flash firmware via picotool (USB BOOTSEL)"
	@echo "  flash-swd     flash firmware via OpenOCD/SWD (CMSIS-DAP probe)"
	@echo "  clean         remove build directories"
	@echo ""
	@echo "Variables: BUILD_TYPE=Release|Debug (default Release). Debug keeps"
	@echo "hot-path DBG() prints; Release compiles them out. Run 'make clean'"
	@echo "before switching BUILD_TYPE"