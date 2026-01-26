cmake_minimum_required(VERSION 3.13)

# Import the SDK
include(pico_sdk_import.cmake)

project(picolink_firmware C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

add_executable(picolink_mfd
    src/main.c
    src/usb_descriptors.c
    src/hardware_map.c
    src/i2c_handler.c
    src/uart_handler.c
    src/spi_handler.c
)

# Link TinyUSB and standard libraries
# tinyusb_device is a critical library for the USB stack
target_link_libraries(picolink_mfd 
    pico_stdlib 
    pico_multicore      # Required for multicore support
    tinyusb_device 
    tinyusb_board
    hardware_i2c
    hardware_spi
    hardware_pwm
    hardware_adc
)

# Enable generation of .uf2 binary for flashing
pico_add_extra_outputs(picolink_mfd)

# TinyUSB configuration (set include path for tusb_config.h)
target_include_directories(picolink_mfd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)