#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include "picolink.h"
#include "mfd-adc.h"

static ssize_t adc_value_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct picolink_adc_chan *achan = dev_get_drvdata(dev);
    usb_packet_t *tx_pkt, *rx_pkt;
    int ret;

    // Выделяем память в куче
    tx_pkt = kzalloc(sizeof(*tx_pkt), GFP_KERNEL);
    rx_pkt = kzalloc(sizeof(*rx_pkt), GFP_KERNEL);

    if (!tx_pkt || !rx_pkt) {
        ret = -ENOMEM;
        goto out;
    }

    tx_pkt->header.type = CMD_TYPE_READ;
    tx_pkt->header.iface_idx = IFACE_ADC;
    tx_pkt->header.length = 1;
    tx_pkt->payload[0] = achan->pin;

    // Передаем указатели
    ret = picolink_transfer(achan->mfd, tx_pkt, rx_pkt);
    
    if (ret == 0) {
        uint16_t raw = (rx_pkt->payload[1] << 8) | rx_pkt->payload[2];
        // uint32_t mv = (raw * 3300) / 4095;
        uint32_t mv = raw;
        ret = sprintf(buf, "%u\n", mv);
    }

out:
    kfree(tx_pkt);
    kfree(rx_pkt);
    return ret;
}

static DEVICE_ATTR(value, S_IRUGO, adc_value_show, NULL);

static struct attribute *picolink_adc_attrs[] = {
    &dev_attr_value.attr,
    NULL
};

static const struct attribute_group picolink_adc_group = {
    .attrs = picolink_adc_attrs,
};

const struct attribute_group *picolink_adc_groups[] = {
    &picolink_adc_group,
    NULL
};
EXPORT_SYMBOL_GPL(picolink_adc_groups);

// Драйвер остается пустым, так как мы регистрируем hwmon напрямую в core.c 
// для гибкости именования, но структуру оставим для совместимости MFD.
struct platform_driver picolink_adc_driver = {
    .driver = { .name = "picolink-adc" },
};