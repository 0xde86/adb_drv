# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Denis Rodin
#
# Build & run:
#   cmake -S test -B test/build
#   cmake --build test/build
#   ctest --test-dir test/build --output-on-failure

cmake_minimum_required(VERSION 3.13)
project(adb_drv_tests C)

set(CMAKE_C_STANDARD 11)

enable_testing()

add_executable(test_decode
    test_decode.c
    unity.c)

target_include_directories(test_decode PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/..)

target_compile_options(test_decode PRIVATE
    -Wall -Wextra -Wpedantic)

add_test(NAME decode COMMAND test_decode)

# libFuzzer harness
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    add_executable(fuzz_decode fuzz_decode.c)
    target_include_directories(fuzz_decode PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..)
    target_compile_options(fuzz_decode PRIVATE
        -Wall -Wextra -Wpedantic -g
        -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_decode PRIVATE
        -fsanitize=fuzzer,address,undefined)
endif()