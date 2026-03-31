cmake_minimum_required(VERSION 3.13)

# IMPORTANT: Set TinyUSB path BEFORE including pico_sdk_import.cmake
set(PICO_TINYUSB_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/tinyusb)

set(PICO_BOARD pico2)

include(pico_sdk_import.cmake)

project(picoamp_hid C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Hardware workarounds for Pico 2
set(PICO_XOSC_STARTUP_DELAY_MULTIPLIER 64)
add_compile_definitions(PICO_FLASH_SPI_CLKDIV=4)

pico_sdk_init()

# Add the pico-i2s-pio library
add_subdirectory(lib/pico-i2s-pio)

add_executable(picoamp_hid
    src/main.c
    src/usb_descriptors.c
)

target_include_directories(picoamp_hid PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(picoamp_hid
    pico_stdlib
    hardware_gpio
    hardware_i2c
    hardware_pwm
    hardware_dma
    hardware_clocks
    pico-i2s-pio
    tinyusb_device
    tinyusb_board
)

pico_enable_stdio_usb(picoamp_hid 0)
pico_enable_stdio_uart(picoamp_hid 1)

pico_add_extra_outputs(picoamp_hid)
