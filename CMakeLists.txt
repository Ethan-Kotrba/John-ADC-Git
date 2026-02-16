cmake_minimum_required(VERSION 3.13)

# Include Pico SDK
include(pico_sdk_import.cmake)

project(John-ACD-Code C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Add executable
add_executable(John-ACD-Code main.c)

# Enable USB and UART stdio
pico_enable_stdio_usb(John-ACD-Code 1)
pico_enable_stdio_uart(John-ACD-Code 1)

# Generate extra output files (e.g., .uf2)
pico_add_extra_outputs(John-ACD-Code)

# Link libraries
target_link_libraries(John-ACD-Code pico_stdlib hardware_spi hardware_dma tinyusb_device tinyusb_board)

# Add include directory for tusb_config.h
target_include_directories(John-ACD-Code PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})