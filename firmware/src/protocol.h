//protocol.h
#ifndef PICOLINK_PROTOCOL_H
#define PICOLINK_PROTOCOL_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define PICOLINK_VID 0x1D50
#define PICOLINK_PID 0x6150

// Command types
typedef enum {
    CMD_TYPE_CONFIG  = 0x01,
    CMD_TYPE_DATA    = 0x02,
    CMD_TYPE_READ    = 0x03, // Request data from hardware
    CMD_TYPE_RESP    = 0x04, // Response containing data
    CMD_TYPE_LOG     = 0x05, // Debug messages from Pico
    CMD_TYPE_DISABLE = 0x06  // Deactivate hardware interface
} cmd_type_t;

typedef enum {
    GPIO_MODE_IN          = 0x00,
    GPIO_MODE_OUT         = 0x01,
    GPIO_MODE_IN_PULLUP   = 0x02,
    GPIO_MODE_IN_PULLDOWN = 0x03
} gpio_mode_t;

typedef enum {
    IFACE_GPIO = 0x00,
    IFACE_I2C  = 0x01,
    IFACE_SPI  = 0x02,
    IFACE_UART = 0x03,
    IFACE_ADC  = 0x04,
    IFACE_PWM  = 0x05,
    IFACE_SERVO = 0x06
} iface_type_t;

typedef struct __attribute__((packed)) {
    uint8_t pin;
    uint16_t min_us;   // Обычно 500-1000 (0 градусов)
    uint16_t max_us;   // Обычно 2000-2500 (180 градусов)
    uint16_t range;    // Диапазон в градусах (обычно 180 или 270)
} servo_config_t;

typedef struct __attribute__((packed)) {
    uint32_t baudrate;
    uint8_t databits;
    uint8_t stopbits;
    uint8_t parity;
    uint8_t tx_pin;
    uint8_t rx_pin;
} uart_config_t;

typedef struct __attribute__((packed)) {
    uint8_t sda_pin;
    uint8_t scl_pin;
    uint32_t baudrate;
} i2c_config_t;

typedef struct __attribute__((packed)) {
    uint32_t baudrate;
    uint8_t sck_pin;
    uint8_t mosi_pin;
    uint8_t miso_pin;
    uint8_t cs_pins[4]; // Up to 4x cs pins
    uint8_t mode;       // SPI mode 0-3
} spi_config_t;

typedef struct __attribute__((packed)) {
    uint8_t pin;
    uint16_t clkdiv_int;  // Целая часть делителя
    uint8_t clkdiv_frac;   // Дробная часть (0-15)
    uint16_t wrap;         // Значение переполнения
    uint8_t options;       // bit 0: phase_correct, bit 1: invert_A, bit 2: invert_B
} pwm_config_t;

typedef struct __attribute__((packed)) {
    uint8_t type;       // Command type (cmd_type_t)
    uint8_t iface_idx;  // Target interface (iface_type_t)
    uint16_t length;    // Payload data length
} picolink_header_t;

typedef struct __attribute__((packed)) {
    picolink_header_t header;
    uint8_t payload[60]; // 64 bytes total - 4 byte header
} usb_packet_t;

#endif