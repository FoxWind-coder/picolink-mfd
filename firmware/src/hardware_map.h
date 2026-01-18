#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t pin_number;
    uint8_t uart_id; // 0 or 1, 0xFF if not available
    uint8_t i2c_id;  // 0 or 1
    uint8_t spi_id;  // 0 or 1
    bool has_adc;
} pin_capabilities_t;

// Global matrix for the chip (to be populated in the .c file according to the schematic)
extern const pin_capabilities_t RP2040_PIN_MAP[30];