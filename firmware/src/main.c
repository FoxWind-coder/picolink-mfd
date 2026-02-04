// main.c
#include "pico/mutex.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "tusb.h"
#include "protocol.h"
#include "i2c_handler.h"
#include "uart_handler.h"
#include "spi_handler.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/spi.h"

// Mutex to protect USB stack access from concurrent cores
mutex_t usb_mutex;

static queue_t packet_queue;
void picolink_uart_flush_to_usb(void);

/**
 * Sends debug/log strings back to the host via USB vendor interface.
 */
void picolink_log(const char *format, ...) {
    usb_packet_t log_pkt;
    va_list args;
    va_start(args, format);
    
    memset(&log_pkt, 0, sizeof(log_pkt));
    log_pkt.header.type = CMD_TYPE_LOG;
    int len = vsnprintf((char*)log_pkt.payload, 60, format, args);
    log_pkt.header.length = (len > 60) ? 60 : len;
    va_end(args);

    // CRITICAL: Ensure USB is mounted and protect with mutex
    mutex_enter_blocking(&usb_mutex);
    if (tud_vendor_mounted()) {
        tud_vendor_write(&log_pkt, sizeof(log_pkt));
        tud_vendor_write_flush();
    }
    mutex_exit(&usb_mutex);
}

/**
 * CORE 1: Command Execution
 * Handles all peripheral operations (I2C, GPIO, ADC, PWM) 
 * to prevent blocking the USB stack on Core 0.
 */
void core1_entry() {
    usb_packet_t pkt;
    picolink_log("PicoLink: Core 1 Ready\n");
    
    while (1) {
        // Blocking wait for a packet from the USB core
        queue_remove_blocking(&packet_queue, &pkt);
        
        picolink_header_t *hdr = &pkt.header;

        // Handle Interface Deactivation
        if (hdr->type == CMD_TYPE_DISABLE) {
            switch (hdr->iface_idx) {
                case IFACE_I2C:
                    picolink_i2c_disable();
                    break;
                case IFACE_UART:
                    picolink_uart_disable();
                    break;
                case IFACE_PWM:
                    // Reset specific pins back to SIO/GPIO mode if needed
                    break;
            }
        }

        // --- I2C Interface ---
        if (hdr->iface_idx == IFACE_I2C) {
            picolink_i2c_handle(&pkt);
            continue;
        }

        // --- UART Interface ---
        else if (hdr->iface_idx == IFACE_UART) {
            picolink_uart_handle(&pkt);
        }

        // --- ADC Interface ---
        else if (hdr->iface_idx == IFACE_ADC) {
            uint8_t pin = pkt.payload[0]; 

            if (hdr->type == CMD_TYPE_READ) {
                uint8_t input_ch;
                if (pin >= 26 && pin <= 28) input_ch = pin - 26; // ADC0-2
                else if (pin == 29) input_ch = 4; // Internal Temp Sensor
                else continue;

                adc_select_input(input_ch);
                uint16_t result = adc_read();

                usb_packet_t resp_pkt;
                memset(&resp_pkt, 0, sizeof(resp_pkt));
                resp_pkt.header.type = CMD_TYPE_RESP;
                resp_pkt.header.iface_idx = IFACE_ADC;
                resp_pkt.header.length = 3; 
                resp_pkt.payload[0] = pin;
                resp_pkt.payload[1] = (result >> 8) & 0xFF; // MSB
                resp_pkt.payload[2] = result & 0xFF;        // LSB

                mutex_enter_blocking(&usb_mutex);
                tud_vendor_write(&resp_pkt, sizeof(resp_pkt));
                tud_vendor_write_flush();
                mutex_exit(&usb_mutex);
            }
        }

        // --- GPIO Interface ---
        else if (hdr->iface_idx == IFACE_GPIO) {
            uint8_t pin = pkt.payload[0];
            if (pin >= 30) continue; 

            if (hdr->type == CMD_TYPE_CONFIG) {
                uint8_t mode = pkt.payload[1]; 
                gpio_init(pin);
                
                if (mode == GPIO_MODE_OUT) {
                    gpio_set_dir(pin, GPIO_OUT);
                } else {
                    gpio_set_dir(pin, GPIO_IN);
                    gpio_set_pulls(pin, (mode == GPIO_MODE_IN_PULLUP), (mode == GPIO_MODE_IN_PULLDOWN));
                }
                picolink_log("GPIO CFG: Pin %d -> Mode %d\n", pin, mode);
            } 
            else if (hdr->type == CMD_TYPE_DATA) {
                uint8_t value = pkt.payload[1];
                gpio_put(pin, value);
            }
            else if (hdr->type == CMD_TYPE_READ) {
                usb_packet_t resp_pkt;
                memset(&resp_pkt, 0, sizeof(resp_pkt));
                resp_pkt.header.type = CMD_TYPE_RESP;
                resp_pkt.header.iface_idx = IFACE_GPIO;
                resp_pkt.header.length = 2;
                resp_pkt.payload[0] = pin;
                resp_pkt.payload[1] = gpio_get(pin);

                mutex_enter_blocking(&usb_mutex);
                tud_vendor_write(&resp_pkt, sizeof(resp_pkt));
                tud_vendor_write_flush();
                mutex_exit(&usb_mutex);
            }
        } 

        // --- PWM/LED Interface ---
        else if (hdr->iface_idx == IFACE_PWM) {
            uint8_t pin = pkt.payload[0];
            if (pin >= 30) continue;

            if (hdr->type == CMD_TYPE_CONFIG) {
                gpio_set_function(pin, GPIO_FUNC_PWM);
                uint slice_num = pwm_gpio_to_slice_num(pin);
                
                pwm_set_clkdiv(slice_num, 125.0f); 
                pwm_set_wrap(slice_num, 1000); 
                pwm_set_enabled(slice_num, true);
                picolink_log("PWM/LED Init: GP%d\n", pin);
            }
            else if (hdr->type == CMD_TYPE_DATA) {
                uint8_t val8 = pkt.payload[1]; // Linux brightness 0-255
                
                // Scale 0-255 to 0-1000 using quadratic mapping for visual smoothness
                uint32_t duty = (uint32_t)val8 * val8 * 1000 / 65025;
                pwm_set_gpio_level(pin, (uint16_t)duty);
            }
            else if (hdr->type == CMD_TYPE_DISABLE) {
                gpio_set_function(pin, GPIO_FUNC_SIO);
                picolink_log("PWM/LED Released: GP%d\n", pin);
            }
        }
        
        else if (hdr->iface_idx == IFACE_SPI) {
            if (hdr->type == CMD_TYPE_DISABLE) {
                picolink_spi_disable();
            } else {
                picolink_spi_handle(&pkt);
            }
            continue;
        }
    }
}

/**
 * CORE 0: USB Stack and Initialization
 */
int main() {
    stdio_init_all();
    mutex_init(&usb_mutex);
    
    // 128-packet queue. Size is critical to prevent loss during bursts like I2C scans.
    queue_init(&packet_queue, sizeof(usb_packet_t), 128);

    // Initial delay for hardware stability and log viewing
    sleep_ms(1000);
    picolink_log("--- PicoLink MFD Bridge ---\n");
    
    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_gpio_init(28);

    tusb_init();
    multicore_launch_core1(core1_entry);

    while (1) {
        // TinyUSB is mostly thread-safe internally, but we use a mutex for data writes
        tud_task(); 
        
        // Push any asynchronous UART data collected in IRQs to the USB host
        picolink_uart_flush_to_usb();
        
        tight_loop_contents();
    }
    return 0;
}

/**
 * TinyUSB Callback: Triggered when host sends data to the vendor interface
 */
void tud_vendor_rx_cb(uint8_t itf, uint8_t const* buffer, uint16_t bufsize) {
    usb_packet_t rx_pkt;
    // If data was received and the queue has room
    if (bufsize > 0 && !queue_is_full(&packet_queue)) {
        memset(&rx_pkt, 0, sizeof(rx_pkt));
        // Copy received data, capping at the size of the packet structure
        uint16_t to_read = (bufsize > sizeof(rx_pkt)) ? sizeof(rx_pkt) : bufsize;
        tud_vendor_read(&rx_pkt, to_read);
        queue_try_add(&packet_queue, &rx_pkt);
    }
}