#include "uart_handler.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware_map.h"
#include "tusb.h"
#include <string.h>
#include <stdio.h>

static uart_inst_t *u_inst = uart1;
static bool uart_enabled = false;

extern void picolink_log(const char *format, ...);

void picolink_uart_disable(void) {
    if (uart_enabled) {
        // 1. Disable interrupts at UART and NVIC levels
        uart_set_irq_enables(u_inst, false, false);
        irq_set_enabled(UART1_IRQ, false);

        // 2. Deinitialize the UART controller
        uart_deinit(u_inst);

        // 3. Restore pins to default state
        // Note: Tracking active pins in static variables is recommended 
        // if they need to be reset to a specific mode here.
        
        picolink_log("UART1: Disabled");
        uart_enabled = false;
    }
}

// Interrupt handler: read from UART and forward to USB via TinyUSB
void on_uart_rx() {
    if (!uart_enabled) return;

    uint8_t buffer[60]; // Maximum payload size for usb_packet_t
    int count = 0;

    while (uart_is_readable(u_inst) && count < 60) {
        buffer[count++] = uart_getc(u_inst);
    }
    
    if (count > 0) {
        usb_packet_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.header.type = CMD_TYPE_RESP;
        resp.header.iface_idx = IFACE_UART;
        resp.header.length = count;
        memcpy(resp.payload, buffer, count);
        
        tud_vendor_write(&resp, sizeof(picolink_header_t) + count);
        tud_vendor_write_flush();
    }
}

void picolink_uart_handle(usb_packet_t *pkt) {
    picolink_header_t *hdr = &pkt->header;

    if (hdr->type == CMD_TYPE_DISABLE) {
        picolink_uart_disable();
        return;
    }
    
    if (hdr->type == CMD_TYPE_CONFIG) {
        uart_config_t *cfg = (uart_config_t *)pkt->payload;

        // Hardware map validation (UART1 is used as UART0 is reserved for Pico SDK debug)
        if (RP2040_PIN_MAP[cfg->tx_pin].uart_id != 1 || 
            RP2040_PIN_MAP[cfg->rx_pin].uart_id != 1) {
            picolink_log("UART CFG ERR: Pins must belong to UART1 (At this time UART0 is debug)\n");
            return;
        }

        if (uart_enabled) {
            uart_deinit(u_inst);
            irq_set_enabled(UART1_IRQ, false);
        }

        uart_init(u_inst, cfg->baudrate);
        uart_set_format(u_inst, cfg->databits, cfg->stopbits, (uart_parity_t)cfg->parity);
        
        // Enable Hardware FIFO
        uart_set_fifo_enabled(u_inst, true);

        gpio_set_function(cfg->tx_pin, GPIO_FUNC_UART);
        gpio_set_function(cfg->rx_pin, GPIO_FUNC_UART);

        // Configure RX interrupt logic
        irq_set_exclusive_handler(UART1_IRQ, on_uart_rx);
        irq_set_enabled(UART1_IRQ, true);
        uart_set_irq_enables(u_inst, true, false);
        
        uart_enabled = true;
        // picolink_log("UART1 Enabled: TX%d RX%d @ %d baud\n", cfg->tx_pin, cfg->rx_pin, cfg->baudrate);
    } 
    else if (hdr->type == CMD_TYPE_DATA && uart_enabled) {
        for (uint16_t i = 0; i < hdr->length; i++) {
            uart_putc(u_inst, pkt->payload[i]);
        }
    }
}