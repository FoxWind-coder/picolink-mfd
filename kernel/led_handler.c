#include <linux/leds.h>

struct picolink_led {
    struct led_classdev cdev;
    struct picolink_dev *mfd;
    uint8_t pin;
    char name[32];
};

/* Brightness control via USB PWM commands */
static void picolink_led_set_brightness(struct led_classdev *led_cdev,
                                      enum led_brightness brightness) {
    struct picolink_led *pled = container_of(led_cdev, struct picolink_led, cdev);
    usb_packet_t *pkt;

    pkt = kzalloc(sizeof(*pkt), GFP_ATOMIC);
    if (!pkt) return;

    /* Use CMD_TYPE_DATA to transmit the PWM duty cycle */
    pkt->header.type = CMD_TYPE_DATA;
    pkt->header.iface_idx = IFACE_PWM; 
    pkt->header.length = 2;
    pkt->payload[0] = pled->pin;
    pkt->payload[1] = (uint8_t)brightness; // Range: 0-255

    picolink_send_packet(pled->mfd->udev, pled->mfd->bulk_out_endpointAddr, pkt, sizeof(*pkt));
    kfree(pkt);
}

/* Custom sysfs attribute to unregister and free the LED instance */
static ssize_t disable_store(struct device *dev, struct device_attribute *attr,
                           const char *buf, size_t count) {
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct picolink_led *pled = container_of(led_cdev, struct picolink_led, cdev);
    int val;

    if (kstrtoint(buf, 10, &val) == 0 && val == 1) {
        led_classdev_unregister(&pled->cdev);
        /* Note: Optional shutdown packet could be sent to Pico here to stop PWM */
        kfree(pled);
    }
    return count;
}
static DEVICE_ATTR_WO(disable);

static struct attribute *picolink_led_attrs[] = {
    &dev_attr_disable.attr,
    NULL,
};
ATTRIBUTE_GROUPS(picolink_led);