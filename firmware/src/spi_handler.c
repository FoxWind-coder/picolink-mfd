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

        if (cfg->sck_pin == 0xFF) {
            if (current_spi) {
                // Обновляем скорость и формат на лету
                spi_set_baudrate(current_spi, cfg->baudrate);
                spi_set_format(current_spi, 8, 
                              (cfg->mode & 2) ? 1 : 0, 
                              (cfg->mode & 1) ? 1 : 0, 
                              SPI_MSB_FIRST);
                picolink_log("SPI: Reconfigured to %dHz, mode %d", cfg->baudrate, cfg->mode);
            }

            // Обновляем пины CS, если они переданы (не 0xFF)
            for (int i = 0; i < 4; i++) {
                if (cfg->cs_pins[i] < 30) {
                    active_cs[i] = cfg->cs_pins[i];
                    gpio_init(active_cs[i]);
                    gpio_set_dir(active_cs[i], GPIO_OUT);
                    gpio_put(active_cs[i], 1);
                    picolink_log("SPI: CS%d pin update: %d", i, active_cs[i]);
                }
            }
            return; 
        }
        
        // Проверка валидности основных пинов через hardware_map
        uint8_t s1 = RP2040_PIN_MAP[cfg->sck_pin].spi_id;
        uint8_t s2 = RP2040_PIN_MAP[cfg->mosi_pin].spi_id;
        uint8_t s3 = RP2040_PIN_MAP[cfg->miso_pin].spi_id;

        if (s1 == 0xFF || s1 != s2 || s1 != s3) {
            picolink_log("SPI CFG ERR: Pin mismatch");
            return;
        }

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

        // Настройка CS пинов как обычных GPIO (Manual CS)
        for (int i = 0; i < 4; i++) {
            active_cs[i] = cfg->cs_pins[i];
            if (active_cs[i] < 30) {
                gpio_init(active_cs[i]);
                gpio_set_dir(active_cs[i], GPIO_OUT);
                gpio_put(active_cs[i], 1); // High (Idle)
            }
        }

        picolink_log("SPI Init: %s at %dHz", (s1 == 0 ? "SPI0" : "SPI1"), cfg->baudrate);
    } 
    
    else if (hdr->type == CMD_TYPE_DATA) {
        if (!current_spi) {
            picolink_log("SPI ERR: Not initialized");
            return;
        }

        uint8_t cs_idx = pkt->payload[0];
        uint8_t *data_ptr = &pkt->payload[1];
        uint16_t data_len = hdr->length - 1;

        if (cs_idx < 4 && active_cs[cs_idx] < 30) {
            gpio_put(active_cs[cs_idx], 0); // Select
            
            usb_packet_t resp;
            memset(&resp, 0, sizeof(resp));
            resp.header.type = CMD_TYPE_RESP;
            resp.header.iface_idx = IFACE_SPI;
            resp.header.length = data_len;

            // Логируем, что мы начали обмен
            picolink_log("SPI: Xfer %d bytes, CS %d", data_len, cs_idx);

            spi_write_read_blocking(current_spi, data_ptr, resp.payload, data_len);
            
            gpio_put(active_cs[cs_idx], 1); // Deselect

            mutex_enter_blocking(&usb_mutex);
            // Прямая запись всего пакета (64 байта)
            uint32_t written = tud_vendor_write(&resp, 64);
            tud_vendor_write_flush();
            mutex_exit(&usb_mutex);
            
            // Если в dmesg не появится это сообщение, значит Pico зависла на spi_write_read
            picolink_log("SPI: USB Write: %d", written); 
        } else {
             picolink_log("SPI ERR: Invalid CS %d", cs_idx);
        }
    }
}