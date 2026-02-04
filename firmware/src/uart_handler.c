#include "uart_handler.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware_map.h"
#include "pico/util/queue.h"
#include "pico/mutex.h" 
#include "tusb.h"
#include <string.h>
#include <stdio.h>

// static uart_inst_t *u_inst = uart1;
static uart_inst_t *u_inst = NULL; 
static bool uart_enabled = false;
static queue_t uart_rx_fifo; // Queue for "raw" bytes
extern mutex_t usb_mutex; // Reference to global mutex from main.c
extern void picolink_log(const char *format, ...);

static inline uint get_uart_irq(uart_inst_t *inst) {
    return (inst == uart0) ? UART0_IRQ : UART1_IRQ;
}

void picolink_uart_disable(void) {
    if (uart_enabled && u_inst) {
        uint irq_num = get_uart_irq(u_inst);
        uart_set_irq_enables(u_inst, false, false);
        irq_set_enabled(irq_num, false);
        uart_deinit(u_inst);
        queue_free(&uart_rx_fifo); // Release allocated memory
        uart_enabled = false;
        picolink_log("UART: Disabled");
    }
}

// Interrupt handler: read from UART and store in the local FIFO
void on_uart_rx() {
    if (!uart_enabled || !u_inst) return;

    while (uart_is_readable(u_inst)) {
        uint8_t ch = uart_getc(u_inst);
        queue_try_add(&uart_rx_fifo, &ch);
    }
}

void picolink_uart_flush_to_usb(void) {
    if (!uart_enabled || queue_is_empty(&uart_rx_fifo)) return;

    // Attempt to pull data only if USB is ready for transmission
    if (tud_vendor_mounted() && tud_vendor_write_available() >= sizeof(usb_packet_t)) {
        usb_packet_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.header.type = CMD_TYPE_RESP;
        resp.header.iface_idx = IFACE_UART;
        
        uint16_t count = 0;
        while (count < 60 && queue_try_remove(&uart_rx_fifo, &resp.payload[count])) {
            count++;
        }
        
        if (count > 0) {
            resp.header.length = count;
            mutex_enter_blocking(&usb_mutex);
            tud_vendor_write(&resp, sizeof(usb_packet_t));
            tud_vendor_write_flush();
            mutex_exit(&usb_mutex);
        }
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

        int detected_uart_id = RP2040_PIN_MAP[cfg->tx_pin].uart_id;

        if (detected_uart_id == -1) {
            picolink_log("UART CFG ERR: Pin GP%d is not a UART pin\n", cfg->tx_pin);
            return;
        }

        uart_inst_t *new_inst = (detected_uart_id == 0) ? uart0 : uart1;

        if (uart_enabled) {
            picolink_uart_disable();
        }

        // Initialize the RX FIFO with a 2KB buffer
        queue_init(&uart_rx_fifo, sizeof(uint8_t), 2048);

        u_inst = new_inst;

        uart_init(u_inst, cfg->baudrate);
        uart_set_format(u_inst, cfg->databits, cfg->stopbits, (uart_parity_t)cfg->parity);
        uart_set_fifo_enabled(u_inst, true);

        gpio_set_function(cfg->tx_pin, GPIO_FUNC_UART);
        gpio_set_function(cfg->rx_pin, GPIO_FUNC_UART);

        irq_set_exclusive_handler(get_uart_irq(u_inst), on_uart_rx);
        irq_set_enabled(get_uart_irq(u_inst), true);
        uart_set_irq_enables(u_inst, true, false);
        
        uart_enabled = true;
        picolink_log("UART%d Enabled: TX%d RX%d\n", detected_uart_id, cfg->tx_pin, cfg->rx_pin);
    } 
    else if (hdr->type == CMD_TYPE_DATA && uart_enabled) {
        // Direct pass-through: Write data from USB packet to UART TX
        for (uint16_t i = 0; i < hdr->length; i++) {
            uart_putc(u_inst, pkt->payload[i]);
        }
    }
}