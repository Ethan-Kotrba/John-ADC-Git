# CMakeLists.txt
cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
project(John-ADC-Git C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(John-ADC-Git main.c)

# Enable UART for debugging, disable stdio USB to use TinyUSB
pico_enable_stdio_usb(John-ADC-Git 0)
pico_enable_stdio_uart(John-ADC-Git 1)

# Link Pico SDK and TinyUSB libraries
target_link_libraries(John-ADC-Git
    pico_stdlib
    pico_multicore
    hardware_adc
    hardware_pwm
    hardware_dma
    hardware_gpio
    hardware_spi
    hardware_clocks
    hardware_sync
    tinyusb_device
    tinyusb_board
)

# Include directories for TinyUSB
target_include_directories(John-ADC-Git PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} # For tusb_config.h
)

# Define TUP_DCD_ENDPOINT_MAX to suppress warning
target_compile_definitions(John-ADC-Git PRIVATE
    TUP_DCD_ENDPOINT_MAX=8
)

pico_add_extra_outputs(John-ADC-Git)

# Force Thumb mode
target_compile_options(John-ADC-Git PRIVATE -mcpu=cortex-m0plus -mthumb)
target_link_options(John-ADC-Git PRIVATE -mcpu=cortex-m0plus -mthumb)