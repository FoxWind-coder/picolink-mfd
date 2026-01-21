#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/pinctrl/pinconf-generic.h>
#include "picolink.h"

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
    #define REMOVE_RET_TYPE int
    #define REMOVE_RET_VAL  0
#else
    #define REMOVE_RET_TYPE void
    #define REMOVE_RET_VAL
#endif

struct picolink_gpio {
    struct platform_device *pdev;
    struct gpio_chip chip;
};

/* Prototypes to prevent implicit declaration */
static int picolink_gpio_direction_output(struct gpio_chip *chip, unsigned int offset, int value);
static int picolink_gpio_set_config(struct gpio_chip *chip, unsigned int offset, unsigned long config);

/* READ (INPUT) */
static int picolink_gpio_get(struct gpio_chip *chip, unsigned int offset) {
    struct picolink_gpio *pgpio = gpiochip_get_data(chip);
    struct picolink_dev *mfd_core = dev_get_drvdata(pgpio->pdev->dev.parent);
    usb_packet_t *tx_pkt; 
    usb_packet_t *rx_pkt; 
    int ret, value = 0;

    tx_pkt = kzalloc(sizeof(*tx_pkt), GFP_KERNEL);
    rx_pkt = kzalloc(sizeof(*rx_pkt), GFP_KERNEL);
    if (!tx_pkt || !rx_pkt) {
        kfree(tx_pkt); kfree(rx_pkt);
        return -ENOMEM;
    }

    tx_pkt->header.type = CMD_TYPE_READ;
    tx_pkt->header.iface_idx = IFACE_GPIO;
    tx_pkt->header.length = 1;
    tx_pkt->payload[0] = (uint8_t)offset;

    ret = picolink_transfer(mfd_core, tx_pkt, rx_pkt);

    if (ret == 0) {
        if (rx_pkt->header.type == CMD_TYPE_RESP && rx_pkt->header.iface_idx == IFACE_GPIO) {
            value = rx_pkt->payload[1]; 
        }
    } else {
        dev_err(&pgpio->pdev->dev, "GPIO get failed: %d\n", ret);
    }

    kfree(tx_pkt);
    kfree(rx_pkt);
    return value;
}

/* WRITE (OUTPUT) */
static void picolink_gpio_set(struct gpio_chip *chip, unsigned int offset, int value) {
    struct picolink_gpio *pgpio = gpiochip_get_data(chip);
    struct picolink_dev *mfd_core = dev_get_drvdata(pgpio->pdev->dev.parent);
    usb_packet_t *pkt;

    pkt = kzalloc(sizeof(*pkt), GFP_ATOMIC); 
    if (!pkt) return;

    pkt->header.type = CMD_TYPE_DATA;
    pkt->header.iface_idx = IFACE_GPIO;
    pkt->header.length = 2;
    pkt->payload[0] = (uint8_t)offset;
    pkt->payload[1] = (uint8_t)value;

    picolink_send_packet(mfd_core->udev, mfd_core->bulk_out_endpointAddr, pkt, sizeof(*pkt));
    kfree(pkt);
}

/* CONFIGURATION MANAGEMENT (PULL-UP/DOWN) */
static int picolink_gpio_set_config(struct gpio_chip *chip, unsigned int offset, unsigned long config) {
    struct picolink_gpio *pgpio = gpiochip_get_data(chip);
    struct picolink_dev *mfd_core = dev_get_drvdata(pgpio->pdev->dev.parent);
    usb_packet_t *pkt;
    uint8_t mode;
    int ret;

    switch (pinconf_to_config_param(config)) {
    case PIN_CONFIG_BIAS_PULL_UP:
        mode = 0x02; 
        break;
    case PIN_CONFIG_BIAS_PULL_DOWN:
        mode = 0x03; 
        break;
    case PIN_CONFIG_BIAS_DISABLE:
        mode = 0x00; 
        break;
    case PIN_CONFIG_OUTPUT:
        return picolink_gpio_direction_output(chip, offset, 0);
    default:
        return -ENOTSUPP;
    }

    pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
    if (!pkt) return -ENOMEM;

    pkt->header.type = CMD_TYPE_CONFIG;
    pkt->header.iface_idx = IFACE_GPIO;
    pkt->header.length = 2;
    pkt->payload[0] = (uint8_t)offset;
    pkt->payload[1] = mode;

    ret = picolink_send_packet(mfd_core->udev, mfd_core->bulk_out_endpointAddr, pkt, sizeof(*pkt));
    kfree(pkt);
    return ret;
}

static int picolink_gpio_direction_input(struct gpio_chip *chip, unsigned int offset) {
    return picolink_gpio_set_config(chip, offset, pinconf_to_config_packed(PIN_CONFIG_BIAS_DISABLE, 0));
}

static int picolink_gpio_direction_output(struct gpio_chip *chip, unsigned int offset, int value) {
    struct picolink_gpio *pgpio = gpiochip_get_data(chip);
    struct picolink_dev *mfd_core = dev_get_drvdata(pgpio->pdev->dev.parent);
    usb_packet_t *pkt;
    int ret;

    pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
    if (!pkt) return -ENOMEM;
    
    pkt->header.type = CMD_TYPE_CONFIG;
    pkt->header.iface_idx = IFACE_GPIO;
    pkt->header.length = 2;
    pkt->payload[0] = (uint8_t)offset;
    pkt->payload[1] = 0x01; // GPIO_MODE_OUT

    ret = picolink_send_packet(mfd_core->udev, mfd_core->bulk_out_endpointAddr, pkt, sizeof(*pkt));
    
    if (ret >= 0) {
        kfree(pkt); // Free before calling set
        picolink_gpio_set(chip, offset, value);
        return 0;
    }

    kfree(pkt);
    return ret;
}

static int picolink_gpio_probe(struct platform_device *pdev) {
    struct picolink_gpio *pgpio;
    int ret;

    pgpio = devm_kzalloc(&pdev->dev, sizeof(*pgpio), GFP_KERNEL);
    if (!pgpio) return -ENOMEM;

    pgpio->pdev = pdev;
    pgpio->chip.label = "picolink-gpio";
    pgpio->chip.parent = &pdev->dev;
    pgpio->chip.owner = THIS_MODULE;
    pgpio->chip.base = -1;
    pgpio->chip.ngpio = 30;
    
    pgpio->chip.set = picolink_gpio_set;
    pgpio->chip.get = picolink_gpio_get;
    pgpio->chip.direction_input = picolink_gpio_direction_input;
    pgpio->chip.direction_output = picolink_gpio_direction_output;
    pgpio->chip.set_config = picolink_gpio_set_config;
    pgpio->chip.can_sleep = true; 

    ret = devm_gpiochip_add_data(&pdev->dev, &pgpio->chip, pgpio);
    if (ret) return ret;

    dev_info(&pdev->dev, "Picolink-GPIO subdevice ready with Pull-up/down support\n");
    return 0;
}

static REMOVE_RET_TYPE picolink_gpio_remove(struct platform_device *pdev) { return REMOVE_RET_VAL; }

struct platform_driver picolink_gpio_driver = {
    .driver = { .name = "picolink-gpio" },
    .probe = picolink_gpio_probe,
    .remove = picolink_gpio_remove, 
};
EXPORT_SYMBOL_GPL(picolink_gpio_driver);
MODULE_LICENSE("GPL");