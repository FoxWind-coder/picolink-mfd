// mfd-adc.c
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/slab.h>
#include "picolink.h"
#include "mfd-adc.h"

static ssize_t adc_value_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct picolink_adc_chan *achan = dev_get_drvdata(dev);
    usb_packet_t *tx_pkt, *rx_pkt;
    int ret;

    // Allocate memory on the heap for USB packets
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

    // Perform atomic USB transfer via MFD core
    ret = picolink_transfer(achan->mfd, tx_pkt, rx_pkt);
    
    if (ret == 0) {
        // Reconstruct 16-bit raw value from payload (Big-Endian)
        uint16_t raw = (rx_pkt->payload[1] << 8) | rx_pkt->payload[2];
        
        // Output raw value. Calibration (e.g., (raw * 3300) / 4095) 
        // can be performed here or in userspace.
        uint32_t val = raw;
        ret = sprintf(buf, "%u\n", val);
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

/* * The driver structure is kept for MFD compatibility. 
 * Registration is handled in core.c for naming flexibility.
 */
struct platform_driver picolink_adc_driver = {
    .driver = { .name = "picolink-adc" },
};