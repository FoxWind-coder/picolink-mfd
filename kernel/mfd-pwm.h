// mfd-pwm.h
#ifndef MFD_PWM_H
#define MFD_PWM_H

#include <linux/types.h>

typedef struct {
    uint8_t pin;
    uint8_t clkdiv_int;
    uint8_t clkdiv_frac;
    uint16_t wrap;
    uint8_t options; // bit0: phase, bit1: invA, bit2: invB
} __attribute__((packed)) picolink_pwm_config_t;

struct picolink_dev;

/* Функции для управления LED из core.c (парсинг строк) */
int picolink_led_cmd_enable(struct picolink_dev *mfd, int pin);
int picolink_led_cmd_disable(struct picolink_dev *mfd, int pin);

/* Очистка ресурсов при отключении USB */
void picolink_leds_cleanup(struct picolink_dev *mfd);

#endif