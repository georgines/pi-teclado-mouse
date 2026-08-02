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
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(dev_hid_composite C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Vendored SSD1306 (u8g2pico + u8g2), pinned commits — see lib/u8g2pico/VENDOR_COMMIT.txt
add_subdirectory(lib/u8g2pico)

# Add executable. Default name is the project name, version 0.1

cmake_minimum_required(VERSION 3.13)

add_executable(dev_hid_composite)

# board_init() (TinyUSB bsp) pulls in stdio UART by default (PICO_STDIO_UART=1
# globally) and configures UART0 on GP0/GP1 for stdio, colliding with UART0 on
# GP16/GP17 used by the project's own protocol. Disable it so board_init()
# doesn't touch UART0 at all.
pico_enable_stdio_uart(dev_hid_composite 0)

target_sources(dev_hid_composite PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/uart_protocol.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/hid_state.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/shared_state.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/uart_io.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/mode_button.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/hid_usb.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/oled_display.cpp
        )

# Make sure TinyUSB can find tusb_config.h
target_include_directories(dev_hid_composite PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})

# In addition to pico_stdlib required for common PicoSDK functionality, add dependency on tinyusb_device
# for TinyUSB device support and tinyusb_board for the additional board support library used by the example.
# hardware_uart/hardware_i2c/pico_multicore back the UART protocol and dual-core OLED; u8g2pico drives the SSD1306.
target_link_libraries(dev_hid_composite PUBLIC
        pico_stdlib pico_unique_id tinyusb_device tinyusb_board
        hardware_uart hardware_i2c pico_multicore u8g2pico)

# Uncomment this line to enable fix for Errata RP2040-E5 (the fix requires use of GPIO 15)
#target_compile_definitions(dev_hid_composite PUBLIC PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1)

pico_add_extra_outputs(dev_hid_composite)

# add url via pico_set_program_url

# Minimal UART bring-up diagnostic — see uart_echo_test.cpp. Not part of the
# product firmware; only built to isolate wiring/electrical issues from the
# full protocol stack during bench bring-up.
add_executable(uart_echo_test uart_echo_test.cpp)
target_link_libraries(uart_echo_test PUBLIC pico_stdlib hardware_uart)
pico_add_extra_outputs(uart_echo_test)
