//spi_handler.c
#include "spi_handler.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware_map.h"
#include "pico/mutex.h"
#include "tusb.h"
#include <string.h>

static spi_inst_t *current_spi = NULL;
static uint8_t active_sck, active_mosi, active_miso;
static uint8_t active_cs[4] = {0xFF, 0xFF, 0xFF, 0xFF};
static bool cs_was_low = false;

extern void picolink_log(const char *format, ...);
extern mutex_t usb_mutex;

void picolink_spi_disable(void) {
    if (current_spi) {
        spi_deinit(current_spi);
        uint8_t pins[] = {active_sck, active_mosi, active_miso};
        for(int i=0; i<3; i++) {
            if(pins[i] < 30) gpio_set_function(pins[i], GPIO_FUNC_SIO);
        }
        for(int i=0; i<4; i++) {
            if(active_cs[i] < 30) {
                gpio_set_function(active_cs[i], GPIO_FUNC_SIO);
                gpio_set_dir(active_cs[i], GPIO_IN);
            }
        }
        current_spi = NULL;
        picolink_log("SPI: Disabled");
    }
}

void picolink_spi_handle(usb_packet_t *pkt) {
    picolink_header_t *hdr = &pkt->header;

    if (hdr->type == CMD_TYPE_CONFIG) {
        spi_config_t *cfg = (spi_config_t *)pkt->payload;
        bool success = true;

        if (cfg->sck_pin == 0xFF) {
            /* Partial re-configuration (baudrate/mode only) */
            if (current_spi) {
                spi_set_baudrate(current_spi, cfg->baudrate);
                spi_set_format(current_spi, 8, 
                              (cfg->mode & 2) ? 1 : 0, 
                              (cfg->mode & 1) ? 1 : 0, 
                              SPI_MSB_FIRST);
                picolink_log("SPI: Reconfigured to %dHz, mode %d", cfg->baudrate, cfg->mode);
            }

            for (int i = 0; i < 4; i++) {
                if (cfg->cs_pins[i] < 30) {
                    active_cs[i] = cfg->cs_pins[i];
                    gpio_init(active_cs[i]);
                    gpio_set_dir(active_cs[i], GPIO_OUT);
                    gpio_put(active_cs[i], 1);
                    picolink_log("SPI: CS%d pin update: %d", i, active_cs[i]);
                }
            }
        } else {
            /* Full initialization */
            uint8_t s1 = RP2040_PIN_MAP[cfg->sck_pin].spi_id;
            uint8_t s2 = RP2040_PIN_MAP[cfg->mosi_pin].spi_id;
            uint8_t s3 = RP2040_PIN_MAP[cfg->miso_pin].spi_id;

            if (s1 == 0xFF || s1 != s2 || s1 != s3) {
                picolink_log("SPI CFG ERR: Pin mismatch");
                success = false;
            } else {
                picolink_spi_disable();

                current_spi = (s1 == 0) ? spi0 : spi1;
                active_sck = cfg->sck_pin;
                active_mosi = cfg->mosi_pin;
                active_miso = cfg->miso_pin;

                spi_init(current_spi, cfg->baudrate);
                spi_set_format(current_spi, 8, (cfg->mode & 2) ? 1 : 0, (cfg->mode & 1) ? 1 : 0, SPI_MSB_FIRST);
                
                gpio_set_function(active_sck, GPIO_FUNC_SPI);
                gpio_set_function(active_mosi, GPIO_FUNC_SPI);
                gpio_set_function(active_miso, GPIO_FUNC_SPI);
                gpio_pull_up(active_miso);

                for (int i = 0; i < 4; i++) {
                    active_cs[i] = cfg->cs_pins[i];
                    if (active_cs[i] < 30) {
                        gpio_init(active_cs[i]);
                        gpio_set_dir(active_cs[i], GPIO_OUT);
                        gpio_put(active_cs[i], 1);
                    }
                }
                picolink_log("SPI Init: %s at %dHz", (s1 == 0 ? "SPI0" : "SPI1"), cfg->baudrate);
            }
        }

        /* --- CRITICAL ADDITION: Send response back to Linux --- */
        usb_packet_t resp;
        memset(&resp, 0, sizeof(resp));
        // Use LOG type for errors, RESP for success
        resp.header.type = success ? CMD_TYPE_RESP : CMD_TYPE_LOG; 
        resp.header.iface_idx = IFACE_SPI;
        resp.header.length = 0;

        mutex_enter_blocking(&usb_mutex);
        tud_vendor_write(&resp, sizeof(resp));
        tud_vendor_write_flush();
        mutex_exit(&usb_mutex);
        /* ------------------------------------------------------ */
    }
    
    else if (hdr->type == CMD_TYPE_DATA) {
        // Note: cs_was_low tracks the CS state between USB packets

        if (!current_spi) return;

        uint8_t raw_cs = pkt->payload[0];
        uint8_t cs_idx = raw_cs & 0x0F;      // Clean index (0-3)
        bool keep_cs = (raw_cs & 0x80) != 0; // Flag: leave CS low after transfer
        
        uint8_t *data_ptr = &pkt->payload[1]; 
        uint16_t data_len = hdr->length - 1;

        if (cs_idx < 4 && active_cs[cs_idx] < 30) {
            usb_packet_t resp;
            memset(&resp, 0, sizeof(resp));
            resp.header.type = CMD_TYPE_RESP;
            resp.header.iface_idx = IFACE_SPI;

            if (data_len > 60) data_len = 60; 

            // Pull CS low only if it isn't already low
            if (!cs_was_low) {
                gpio_put(active_cs[cs_idx], 0);
            }
            
            spi_write_read_blocking(current_spi, data_ptr, resp.payload, data_len);
            
            // Raise CS only if this is the last chunk (keep_cs flag == false)
            if (!keep_cs) {
                gpio_put(active_cs[cs_idx], 1);
                cs_was_low = false;
            } else {
                cs_was_low = true; // Remember that CS remains asserted
            }

            resp.header.length = (uint8_t)data_len;

            mutex_enter_blocking(&usb_mutex);
            if (tud_vendor_mounted()) {
                // Wait until there is space in the USB buffer for the full packet
                while (tud_vendor_write_available() < sizeof(resp)) {
                    mutex_exit(&usb_mutex);
                    sleep_us(10); // Allow Core 0 to process tasks
                    mutex_enter_blocking(&usb_mutex);
                }
                tud_vendor_write(&resp, sizeof(resp));
                tud_vendor_write_flush();
            }
            mutex_exit(&usb_mutex);
        } else {
            picolink_log("SPI ERR: Invalid CS %d (Pin: %d)", cs_idx, active_cs[cs_idx]);
            
            // CRITICAL: Respond to driver even on error with 0 bytes.
            // Failure to do so causes Linux to hit a -110 timeout.
            usb_packet_t resp;
            memset(&resp, 0, sizeof(resp));
            resp.header.type = CMD_TYPE_RESP;
            resp.header.iface_idx = IFACE_SPI;
            resp.header.length = 0; 

            mutex_enter_blocking(&usb_mutex);
            tud_vendor_write(&resp, sizeof(resp));
            tud_vendor_write_flush();
            mutex_exit(&usb_mutex);
        }
    }
}