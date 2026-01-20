#ifndef PICOLINK_ADC_H
#define PICOLINK_ADC_H

#include <linux/list.h>
#include <linux/hwmon.h>

struct picolink_adc_chan {
    struct list_head node;
    struct picolink_dev *mfd;
    struct device *hwmon_dev;
    uint8_t pin;
    char name[32];
};

// Экспортируем группу атрибутов для core.c
extern const struct attribute_group *picolink_adc_groups[];

#endif