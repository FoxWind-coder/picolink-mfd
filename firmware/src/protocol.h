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
    CMD_TYPE_CONFIG = 0x01,
    CMD_TYPE_DATA   = 0x02,
    CMD_TYPE_READ   = 0x03, // Read request
    CMD_TYPE_RESP   = 0x04  // Data response
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
    IFACE_PWM  = 0x05
} iface_type_t;

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
    uint8_t type;       
    uint8_t iface_idx;  
    uint16_t length;    
} picolink_header_t;

typedef struct __attribute__((packed)) {
    picolink_header_t header;
    uint8_t payload[60]; // 64 (total size) - 4 (header size)
} usb_packet_t;

#endif