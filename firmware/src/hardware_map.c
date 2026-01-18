#include "hardware_map.h"

// Константа 0xFF означает отсутствие функции на пине
const pin_capabilities_t RP2040_PIN_MAP[30] = {
    // Pin, UART, I2C, SPI, ADC
    {0,  0,    0,    0,    false}, // GP0:  U0_TX,  I0_SDA, S0_RX
    {1,  0,    0,    0,    false}, // GP1:  U0_RX,  I0_SCL, S0_CSn
    {2,  1,    1,    0,    false}, // GP2:  U1_TX,  I1_SDA, S0_SCK
    {3,  1,    1,    0,    false}, // GP3:  U1_RX,  I1_SCL, S0_TX
    {4,  1,    0,    0,    false}, // GP4:  U1_TX,  I0_SDA, S0_RX
    {5,  1,    0,    0,    false}, // GP5:  U1_RX,  I0_SCL, S0_CSn
    {6,  0,    1,    0,    false}, // GP6:  U0_TX,  I1_SDA, S0_SCK
    {7,  0,    1,    0,    false}, // GP7:  U0_RX,  I1_SCL, S0_TX
    {8,  1,    0,    1,    false}, // GP8:  U1_TX,  I0_SDA, S1_RX
    {9,  1,    0,    1,    false}, // GP9:  U1_RX,  I0_SCL, S1_CSn
    {10, 1,    1,    1,    false}, // GP10: U1_TX,  I1_SDA, S1_SCK
    {11, 1,    1,    1,    false}, // GP11: U1_RX,  I1_SCL, S1_TX
    {12, 0,    0,    1,    false}, // GP12: U0_TX,  I0_SDA, S1_RX
    {13, 0,    0,    1,    false}, // GP13: U0_RX,  I0_SCL, S1_CSn
    {14, 0,    1,    1,    false}, // GP14: U0_TX,  I1_SDA, S1_SCK
    {15, 0,    1,    1,    false}, // GP15: U0_RX,  I1_SCL, S1_TX
    {16, 0,    0,    0,    false}, // GP16: U0_TX,  I0_SDA, S0_RX
    {17, 0,    0,    0,    false}, // GP17: U0_RX,  I0_SCL, S0_CSn
    {18, 1,    1,    0,    false}, // GP18: U1_TX,  I1_SDA, S0_SCK
    {19, 1,    1,    0,    false}, // GP19: U1_RX,  I1_SCL, S0_TX
    {20, 1,    0,    0,    false}, // GP20: U1_TX,  I0_SDA, S0_RX
    {21, 1,    0,    0,    false}, // GP21: U1_RX,  I0_SCL, S0_CSn
    {22, 0,    1,    0,    false}, // GP22: U0_TX,  I1_SDA, S0_SCK
    {23, 0xFF, 0xFF, 0xFF, false}, // GP23: RT6150 PS (Internal)
    {24, 0xFF, 0xFF, 0xFF, false}, // GP24: VBUS Sense (Internal)
    {25, 0xFF, 0xFF, 0xFF, false}, // GP25: LED (Internal)
    {26, 1,    1,    1,    true }, // GP26: U1_TX,  I1_SDA, S1_SCK, ADC0
    {27, 1,    1,    1,    true }, // GP27: U1_RX,  I1_SCL, S1_TX,  ADC1
    {28, 0,    0,    1,    true }, // GP28: U0_TX,  I0_SDA, S1_RX,  ADC2
    {29, 0xFF, 0xFF, 0xFF, false}  // GP29: VSYS Sense (Internal)
};