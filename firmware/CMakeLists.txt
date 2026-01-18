cmake_minimum_required(VERSION 3.13)

# Подтягиваем SDK
include(pico_sdk_import.cmake)

project(picolink_firmware C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Инициализируем SDK
pico_sdk_init()

add_executable(picolink_mfd
    src/main.c
    src/usb_descriptors.c
    src/hardware_map.c
    src/i2c_handler.c
    src/uart_handler.c
)

# Подключаем TinyUSB и стандартные возможности
# tinyusb_device — это критически важная библиотека для нашего USB-стека
target_link_libraries(picolink_mfd 
    pico_stdlib 
    pico_multicore      # Для работы со вторым ядром
    tinyusb_device 
    tinyusb_board
    hardware_i2c
    hardware_pwm
    hardware_adc
)

# Включаем генерацию .uf2 файла для прошивки
pico_add_extra_outputs(picolink_mfd)

# Настройка TinyUSB (указываем, где искать конфиг)
target_include_directories(picolink_mfd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)