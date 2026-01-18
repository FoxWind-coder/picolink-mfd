///home/sky/picolink-mfd/firmware/src/hardware_map.h
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t pin_number;
    uint8_t uart_id; // 0 или 1, 0xFF если нет
    uint8_t i2c_id;  // 0 или 1
    uint8_t spi_id;  // 0 или 1
    bool has_adc;
} pin_capabilities_t;

// Глобальная матрица для чипа (наполним в .c файле согласно схеме)
extern const pin_capabilities_t RP2040_PIN_MAP[30];