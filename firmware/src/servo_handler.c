// servo_handler.c
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "pico/mutex.h"
#include "tusb.h"
#include "protocol.h"
#include <string.h>

#define US_TO_TICKS(us) ((uint16_t)(((uint32_t)(us) * 25) / 8))

static servo_config_t servo_states[30]; 
extern void picolink_log(const char *format, ...);
extern mutex_t usb_mutex;

void picolink_servo_handle(usb_packet_t *pkt) {
    picolink_header_t *hdr = &pkt->header;
    bool send_reply = false; // Меняем логику
    if (hdr->type == CMD_TYPE_CONFIG) {
        servo_config_t cfg;
        if (hdr->length < sizeof(servo_config_t)) {
            picolink_log("SERVO ERR: Small config pkt");
            return;
        }
        memcpy(&cfg, pkt->payload, sizeof(servo_config_t));
        
        if (cfg.pin >= 30) {
            picolink_log("SERVO ERR: Invalid pin %d", cfg.pin);
            return;
        }

        // Защита: range не может быть 0
        if (cfg.range == 0) cfg.range = 180; 

        servo_states[cfg.pin] = cfg;

        gpio_set_function(cfg.pin, GPIO_FUNC_PWM);
        uint slice_num = pwm_gpio_to_slice_num(cfg.pin);
        
        pwm_config hw_cfg = pwm_get_default_config();
        pwm_config_set_wrap(&hw_cfg, 62499);      // 20ms
        pwm_config_set_clkdiv(&hw_cfg, 40.0f); 
        pwm_init(slice_num, &hw_cfg, true);
        picolink_log("SERVO OK: Pin %d, Range %d", cfg.pin, cfg.range);
        send_reply = true; // Для конфига отвечаем
    } 
    else if (hdr->type == CMD_TYPE_DATA) {
        uint8_t pin = pkt->payload[0];
        uint16_t angle = pkt->payload[1] | (pkt->payload[2] << 8);
        
        if (pin < 30) {
            servo_config_t *s = &servo_states[pin];
            if (s->range == 0) s->range = 180; 
            if (angle > s->range) angle = s->range;

            uint32_t us = s->min_us + ((uint32_t)angle * (s->max_us - s->min_us) / s->range);
            pwm_set_gpio_level(pin, US_TO_TICKS(us));
        }
        send_reply = false; // Для данных ответ НЕ ШЛЕМ, чтобы не забивать буфер
    }

    if (send_reply) {
        usb_packet_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.header.type = CMD_TYPE_RESP;
        resp.header.iface_idx = IFACE_SERVO;
        
        mutex_enter_blocking(&usb_mutex);
        if (tud_vendor_mounted() && tud_vendor_write_available() >= sizeof(resp)) {
            tud_vendor_write(&resp, sizeof(resp));
            tud_vendor_write_flush();
        }
        mutex_exit(&usb_mutex);
    }
}