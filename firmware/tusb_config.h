///home/sky/picolink-mfd/firmware/tusb_config.h
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

// Настройки контроллера для RP2040
#define CFG_TUSB_MCU                OPT_MCU_RP2040

// Важное исправление: указываем режим работы порта (0 - Device для Pico)
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

// Убираем переопределение CFG_TUSB_OS, так как оно задается в CMake/command-line
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

// Включаем Vendor Class
#define CFG_TUD_ENABLED             1
#define CFG_TUD_VENDOR              1

// Буферы
#define CFG_TUD_VENDOR_RX_BUFSIZE   256
#define CFG_TUD_VENDOR_TX_BUFSIZE   256
#define CFG_TUD_ENDPOINT0_SIZE      64

#endif