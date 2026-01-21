#include "pico/mutex.h"
#include "i2c_handler.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware_map.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

static i2c_inst_t *current_i2c = NULL;
static uint8_t current_sda = 0xFF;
static uint8_t current_scl = 0xFF;

extern void picolink_log(const char *format, ...);
extern mutex_t usb_mutex;

void picolink_i2c_handle(usb_packet_t *pkt) {
    picolink_header_t *hdr = &pkt->header;

    if (hdr->type == CMD_TYPE_DISABLE) {
        picolink_i2c_disable();
        return;
    }

    if (hdr->type == CMD_TYPE_CONFIG) {
        uint8_t sda = pkt->payload[0];
        uint8_t scl = pkt->payload[1];

        picolink_log("I2C CFG RECV: SDA=%d SCL=%d", sda, scl);

        if (sda >= 30 || scl >= 30) {
            picolink_log("I2C CFG ERR: Invalid pins");
            return;
        }

        // Check hardware_map: verify pins belong to the same I2C controller
        uint8_t sda_id = RP2040_PIN_MAP[sda].i2c_id;
        uint8_t scl_id = RP2040_PIN_MAP[scl].i2c_id;
        
        if (sda_id == 0xFF || sda_id != scl_id) {
            picolink_log("I2C CFG ERR: Bus mismatch (SDA_ID:%d SCL_ID:%d)", sda_id, scl_id);
            return;
        }
        
        // Deinitialize if the bus was already active
        if (current_i2c) {
            picolink_i2c_disable();
        }

        current_i2c = (sda_id == 0) ? i2c0 : i2c1;
        current_sda = sda;
        current_scl = scl;

        i2c_init(current_i2c, 100000); // Default 100kHz
        gpio_set_function(sda, GPIO_FUNC_I2C);
        gpio_set_function(scl, GPIO_FUNC_I2C);
        gpio_pull_up(sda);
        gpio_pull_up(scl);
        
        picolink_log("I2C Init Success: %s", (sda_id == 0 ? "I2C0" : "I2C1"));
        return;
    } 

    // Guard: If bus is not configured but a command is received
    else if (!current_i2c) {
        if (hdr->type == CMD_TYPE_DATA || hdr->type == CMD_TYPE_READ) {
            usb_packet_t resp;
            memset(&resp, 0, sizeof(resp));
            resp.header.type = CMD_TYPE_RESP;
            resp.header.iface_idx = IFACE_I2C;
            resp.header.length = 0; // 0 length indicates error/bus not ready

            mutex_enter_blocking(&usb_mutex);
            tud_vendor_write(&resp, sizeof(resp));
            tud_vendor_write_flush();
            mutex_exit(&usb_mutex);
        }
        return;
    }

    else if (hdr->type == CMD_TYPE_DATA) { 
        uint8_t addr = pkt->payload[0];
        uint8_t len = hdr->length - 1;
        int result;

        if (len == 0) {
            // Scan mode (e.g., i2cdetect). 
            // Attempting a 1-byte read is a reliable way to probe on RP2040.
            uint8_t dummy;
            result = i2c_read_blocking(current_i2c, addr, &dummy, 1, false);
            if (result >= 0) picolink_log("I2C: Found 0x%02x", addr);
        } else {
            // Standard data write
            result = i2c_write_blocking(current_i2c, addr, &pkt->payload[1], len, false);
        }
        
        usb_packet_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.header.type = CMD_TYPE_RESP;
        resp.header.iface_idx = IFACE_I2C;
        resp.header.length = (result >= 0) ? 1 : 0; 

        // if (result >= 0) picolink_log("I2C: Found 0x%02x", addr);

        mutex_enter_blocking(&usb_mutex);
        tud_vendor_write(&resp, sizeof(resp));
        tud_vendor_write_flush();
        mutex_exit(&usb_mutex);

    } else if (hdr->type == CMD_TYPE_READ) {
        uint8_t addr = pkt->payload[0];
        uint8_t len = pkt->payload[1];
        
        usb_packet_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.header.type = CMD_TYPE_RESP;
        resp.header.iface_idx = IFACE_I2C;
        
        int result = i2c_read_blocking(current_i2c, addr, resp.payload, len, false);
        resp.header.length = (result >= 0) ? (uint8_t)result : 0;

        mutex_enter_blocking(&usb_mutex);
        tud_vendor_write(&resp, sizeof(resp));
        tud_vendor_write_flush();
        mutex_exit(&usb_mutex);
    }
}

void picolink_i2c_disable(void) {
    if (current_i2c) {
        i2c_deinit(current_i2c);
        
        // Reset pin functions and disable pull-ups
        uint8_t pins[2] = {current_sda, current_scl};
        for (int i = 0; i < 2; i++) {
            if (pins[i] < 30) {
                gpio_set_pulls(pins[i], false, false); 
                gpio_set_function(pins[i], GPIO_FUNC_SIO);
                gpio_set_dir(pins[i], GPIO_IN);
            }
        }
        
        picolink_log("I2C: Bus released (GP%d, GP%d)", current_sda, current_scl);
        
        current_i2c = NULL;
        current_sda = 0xFF;
        current_scl = 0xFF;
    }
}