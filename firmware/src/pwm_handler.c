#include "hardware/pwm.h"

void picolink_pwm_handle(usb_packet_t *pkt) {
    uint8_t pin = pkt->payload[0];
    
    if (pkt->header.type == CMD_TYPE_CONFIG) {
        gpio_set_function(pin, GPIO_FUNC_PWM);
        uint slice_num = pwm_gpio_to_slice_num(pin);
        pwm_config config = pwm_get_default_config();
        pwm_init(slice_num, &config, true);
        picolink_log("PWM Configured on GP%d", pin);
    } 
    else if (pkt->header.type == CMD_TYPE_DATA) {
        uint8_t val = pkt->payload[1];
        /* Convert 0-255 range to pulse width using quadratic 
           gamma correction for perceived LED brightness */
        pwm_set_gpio_level(pin, val * val); 
    }
}