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

void picolink_i2c_handle(usb_packet_t *pkt) {
    picolink_header_t *hdr = &pkt->header;

    if (hdr->type == CMD_TYPE_CONFIG) {
        uint8_t sda = pkt->payload[0];
        uint8_t scl = pkt->payload[1];

        if (sda >= 30 || scl >= 30) return;

        // Check hardware_map: do these pins belong to the same bus?
        uint8_t sda_id = RP2040_PIN_MAP[sda].i2c_id;
        uint8_t scl_id = RP2040_PIN_MAP[scl].i2c_id;
        
        if (sda_id == 0xFF || sda_id != scl_id) {
            printf("I2C CFG ERR: Pins GP%d and GP%d are not on the same I2C bus!\n", sda, scl);
            return;
        }
        
        // If the bus was already active, deinitialize it and reset previous pins
        if (current_i2c) {
            i2c_deinit(current_i2c);
            gpio_set_function(current_sda, GPIO_FUNC_SIO);
            gpio_set_function(current_scl, GPIO_FUNC_SIO);
            gpio_set_dir(current_sda, GPIO_IN);
            gpio_set_dir(current_scl, GPIO_IN);
        }

        current_i2c = (sda_id == 0) ? i2c0 : i2c1;
        current_sda = sda;
        current_scl = scl;

        i2c_init(current_i2c, 100000); 
        gpio_set_function(sda, GPIO_FUNC_I2C);
        gpio_set_function(scl, GPIO_FUNC_I2C);
        gpio_pull_up(sda);
        gpio_pull_up(scl);
        printf("I2C Bus Init: %s (SDA:GP%d, SCL:GP%d)\n", 
               (sda_id == 0 ? "I2C0" : "I2C1"), sda, scl);
    } 
    else if (!current_i2c) return;

    else if (hdr->type == CMD_TYPE_DATA) { 
        uint8_t addr = pkt->payload[0];
        uint8_t len = hdr->length - 1;
        int result;

        if (len == 0) {
            // Scan mode (i2cdetect)
            // Reading 1 byte is the most reliable way to check for a device on RP2040
            uint8_t dummy;
            result = i2c_read_blocking(current_i2c, addr, &dummy, 1, false);
        } else {
            // Standard data write
            result = i2c_write_blocking(current_i2c, addr, &pkt->payload[1], len, false);
        }
        
        usb_packet_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.header.type = CMD_TYPE_RESP;
        resp.header.iface_idx = IFACE_I2C;
        resp.header.length = (result >= 0) ? 1 : 0; 

        if (result >= 0) printf("I2C ADDR FOUND: 0x%02x\n", addr);

        // Small delay before USB response to maintain Bulk endpoint stability
        // sleep_us(100); 
        tud_vendor_write(&resp, sizeof(resp));
        tud_vendor_write_flush();
    }
    else if (hdr->type == CMD_TYPE_READ) {
        uint8_t addr = pkt->payload[0];
        uint8_t len = pkt->payload[1];
        
        usb_packet_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.header.type = CMD_TYPE_RESP;
        resp.header.iface_idx = IFACE_I2C;
        
        int result = i2c_read_blocking(current_i2c, addr, resp.payload, len, false);
        resp.header.length = (result >= 0) ? (uint8_t)result : 0;

        tud_vendor_write(&resp, sizeof(resp));
        tud_vendor_write_flush();
    }
}