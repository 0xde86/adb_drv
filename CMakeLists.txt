# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.21)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
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

add_executable(adb_drv adb_drv.c adb.c usb_descriptors.c)

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

execute_process(
    COMMAND ${CMAKE_C_COMPILER} -E -Wp,-v -xc -
    INPUT_FILE /dev/null
    ERROR_VARIABLE _cc_verbose
    OUTPUT_QUIET
)
string(REPLACE "\n" ";" _cc_verbose_lines "${_cc_verbose}")
set(_in_search_block FALSE)
set(_tidy_sysincludes "")
foreach(_line IN LISTS _cc_verbose_lines)
    if(_line MATCHES "End of search list")
        set(_in_search_block FALSE)
    endif()
    if(_in_search_block)
        string(STRIP "${_line}" _dir)
        if(_dir)
            list(APPEND _tidy_sysincludes "--extra-arg-before=-isystem${_dir}")
        endif()
    endif()
    if(_line MATCHES "#include <\\.\\.\\.> search starts here:")
        set(_in_search_block TRUE)
    endif()
endforeach()

add_custom_target(clang-tidy
    COMMAND clang-tidy
        --checks=cert-*,bugprone-*,readability-*
        --header-filter=${CMAKE_CURRENT_LIST_DIR}/[^/]+\\.h
        ${_tidy_sysincludes}
        -p ${CMAKE_BINARY_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/adb_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/adb.c
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMENT "Running clang-tidy on first-party sources")

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
        hardware_watchdog
        hardware_clocks
        tinyusb_device
        pico_unique_id
)

pico_add_extra_outputs(adb_drv)

