#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

// Controller settings for RP2040
#define CFG_TUSB_MCU                OPT_MCU_RP2040

// Important fix: specify port operating mode (0 - Device for Pico)
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

// Avoid overriding CFG_TUSB_OS if it is already defined via CMake/command-line
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

#define PICO_STDIO_UART_DEFAULT_BIN 0

// Enable Vendor Class
#define CFG_TUD_ENABLED             1
#define CFG_TUD_VENDOR              1

// Buffers
#define CFG_TUD_VENDOR_RX_BUFSIZE   1024
#define CFG_TUD_VENDOR_TX_BUFSIZE   1024
#define CFG_TUD_ENDPOINT0_SIZE      64

#endif