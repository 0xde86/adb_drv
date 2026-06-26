# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(adb_drv C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(adb_drv adb_drv.c adb.c)

pico_set_program_name(adb_drv "adb_drv")
pico_set_program_version(adb_drv "0.1")

# Generate PIO headers
pico_generate_pio_header(adb_drv ${CMAKE_CURRENT_LIST_DIR}/adb_tx.pio)
pico_generate_pio_header(adb_drv ${CMAKE_CURRENT_LIST_DIR}/adb_rx.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(adb_drv 1)
pico_enable_stdio_usb(adb_drv 0)

# Strict warnings on our sources only; SDK headers stay quiet.
set_source_files_properties(adb_drv.c adb.c
    PROPERTIES COMPILE_OPTIONS
    "-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Wformat=2;-Wundef;-Wnull-dereference;-Wstrict-prototypes")

# Dedicated `clang-tidy` target that runs on our sources only.
# Run with: cmake --build build --target clang-tidy
# (Not wired into the default build because pico_sdk_init() adds SDK sources
#  directly into adb_drv; the target-level C_CLANG_TIDY would scan them too.)
# find_program(CLANG_TIDY clang-tidy)
# if(CLANG_TIDY)
    add_custom_target(clang-tidy
        COMMAND clang-tidy
            --checks=cert-*,bugprone-*,readability-*
            --header-filter=${CMAKE_CURRENT_LIST_DIR}/.*
            -p ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_LIST_DIR}/adb_drv.c
            ${CMAKE_CURRENT_LIST_DIR}/adb.c
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        COMMENT "Running clang-tidy on first-party sources")
# endif()

# Add the standard library to the build
target_link_libraries(adb_drv
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(adb_drv PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(adb_drv 
        hardware_pio
        hardware_timer
        hardware_watchdog
        hardware_clocks
        )

pico_add_extra_outputs(adb_drv)

