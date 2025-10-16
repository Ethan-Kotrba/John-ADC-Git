cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)

# include(~/Clones/pico/pico-sdk/pico_sdk_init.cmake)
project(John-ADC-Git C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()
add_executable(John-ADC-Git main.c)
pico_enable_stdio_usb(John-ADC-Git 0)
pico_enable_stdio_uart(John-ADC-Git 0)

# Add the directory containing tusb_config.h to the include path
target_include_directories(John-ADC-Git PRIVATE ${CMAKE_CURRENT_LIST_DIR})


target_link_libraries(John-ADC-Git 

    pico_stdlib 
    pico_unique_id 
    tusb
    tinyusb_device
    tinyusb_board
    hardware_spi 
    hardware_dma 
    hardware_gpio 
    hardware_sync 
    pico_multicore 
    hardware_clocks 
    hardware_pwm 
)

pico_add_extra_outputs(John-ADC-Git)
# Force Thumb mode
target_compile_options(John-ADC-Git PRIVATE -mcpu=cortex-m0plus -mthumb)

# Ensure TinyUSB uses our custom configuration and exclude pico_stdio_usb
target_compile_definitions(John-ADC-Git PRIVATE
    PICO_STDIO_USB=0
    PICO_STDIO_UART=0
    TUSB_HOST=0
    TUSB_DEVICE=1
)

target_link_options(John-ADC-Git PRIVATE -mcpu=cortex-m0plus -mthumb)