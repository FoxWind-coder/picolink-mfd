// mfd-pwm.c
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include "picolink.h"
#include "mfd-pwm.h"

/* Структура для управления одним LED */
struct picolink_led {
    struct led_classdev cdev;
    struct picolink_dev *mfd;
    uint8_t pin;
    char name[32];
    struct list_head node;
};


/* Структура драйвера PWM */
struct picolink_pwm_chip {
    struct pwm_chip chip;
    struct picolink_dev *mfd;
};

static LIST_HEAD(picolink_leds_list);
static DEFINE_MUTEX(leds_lock);

/* * ==========================================
 * Helper: USB Transmission for PWM/LED 
 * ==========================================
 */
static void picolink_pwm_urb_complete(struct urb *urb) {
    kfree(urb->context);
    usb_free_urb(urb);
}

static int picolink_send_pwm_cmd(struct picolink_dev *dev, uint8_t pin, uint16_t value) {
    usb_packet_t *pkt;
    struct urb *urb;
    int ret;

    if (!dev || dev->disconnected) return -ENODEV;

    pkt = kzalloc(sizeof(*pkt), GFP_ATOMIC);
    if (!pkt) return -ENOMEM;

    urb = usb_alloc_urb(0, GFP_ATOMIC);
    if (!urb) {
        kfree(pkt);
        return -ENOMEM;
    }

    pkt->header.type = CMD_TYPE_DATA;
    pkt->header.iface_idx = IFACE_PWM;
    pkt->header.length = 3; // pin + value (2 bytes)
    pkt->payload[0] = pin;
    pkt->payload[1] = value & 0xFF;        // LSB
    pkt->payload[2] = (value >> 8) & 0xFF; // MSB

    usb_fill_bulk_urb(urb, dev->udev,
                      usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
                      pkt, sizeof(*pkt),
                      picolink_pwm_urb_complete, pkt);

    ret = usb_submit_urb(urb, GFP_ATOMIC);
    if (ret) {
        usb_free_urb(urb);
        kfree(pkt);
    }
    return ret;
}

/* * ==========================================
 * LED Class Implementation (Moved from core)
 * ==========================================
 */

static void picolink_led_set_brightness(struct led_classdev *led_cdev,
                                      enum led_brightness brightness) {
    struct picolink_led *pled = container_of(led_cdev, struct picolink_led, cdev);
    // Масштабируем 0-255 в 0-65535
    uint16_t val16 = (uint32_t)brightness * 65535 / 255;
    picolink_send_pwm_cmd(pled->mfd, pled->pin, val16);
}

/* Attributes for manual disable via sysfs */
static ssize_t disable_store(struct device *dev, struct device_attribute *attr,
                           const char *buf, size_t count) {
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct picolink_led *pled = container_of(led_cdev, struct picolink_led, cdev);
    int val;

    if (kstrtoint(buf, 10, &val) == 0 && val == 1) {
        dev_info(dev, "LED on pin %d disabled via sysfs\n", pled->pin);
        led_classdev_unregister(&pled->cdev);
        
        mutex_lock(&leds_lock);
        list_del(&pled->node);
        mutex_unlock(&leds_lock);
        
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

/* * ==========================================
 * Core Interface Functions (Called by core.c)
 * ==========================================
 */

int picolink_led_cmd_enable(struct picolink_dev *mfd, int pin) {
    struct picolink_led *pled;
    bool exists = false;

    mutex_lock(&leds_lock);
    list_for_each_entry(pled, &picolink_leds_list, node) {
        if (pled->mfd == mfd && pled->pin == (uint8_t)pin) {
            exists = true;
            break;
        }
    }
    mutex_unlock(&leds_lock);

    if (exists) return -EEXIST;

    pled = kzalloc(sizeof(*pled), GFP_KERNEL);
    if (!pled) return -ENOMEM;

    pled->mfd = mfd;
    pled->pin = (uint8_t)pin;
    snprintf(pled->name, sizeof(pled->name), "picolink_led_%d", pin);

    pled->cdev.name = pled->name;
    pled->cdev.brightness_set = picolink_led_set_brightness;
    pled->cdev.max_brightness = 255;
    pled->cdev.groups = picolink_led_groups;

    mutex_lock(&leds_lock);
    list_add(&pled->node, &picolink_leds_list);
    mutex_unlock(&leds_lock);

    if (led_classdev_register(&mfd->udev->dev, &pled->cdev) < 0) {
        mutex_lock(&leds_lock);
        list_del(&pled->node);
        mutex_unlock(&leds_lock);
        kfree(pled);
        return -EINVAL;
    }

    /* Send Initial Config to Hardware */
    {
        usb_packet_t *pkt;
        picolink_pwm_config_t *cfg;

        pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
        if (!pkt) return -ENOMEM;

        pkt->header.type = CMD_TYPE_CONFIG;
        pkt->header.iface_idx = IFACE_PWM;
        pkt->header.length = sizeof(picolink_pwm_config_t);
        
        cfg = (picolink_pwm_config_t *)pkt->payload;
        cfg->pin = (uint8_t)pin;
        cfg->clkdiv_int = 1;  // Без делителя
        cfg->clkdiv_frac = 0;
        cfg->wrap = 255;      // Чтобы 255 яркости соответствовало 100% заполнению
        cfg->options = 0;

        picolink_send_packet(mfd->udev, mfd->bulk_out_endpointAddr, pkt, sizeof(*pkt));
        kfree(pkt);
    }

    return 0;
}
EXPORT_SYMBOL_GPL(picolink_led_cmd_enable);

int picolink_led_cmd_disable(struct picolink_dev *mfd, int pin) {
    struct picolink_led *pled, *tmp;
    bool found = false;

    mutex_lock(&leds_lock);
    list_for_each_entry_safe(pled, tmp, &picolink_leds_list, node) {
        if (pled->mfd == mfd && pled->pin == (uint8_t)pin) {
            led_classdev_unregister(&pled->cdev);
            list_del(&pled->node);
            kfree(pled);
            found = true;
            break;
        }
    }
    mutex_unlock(&leds_lock);
    return found ? 0 : -ENODEV;
}
EXPORT_SYMBOL_GPL(picolink_led_cmd_disable);

void picolink_leds_cleanup(struct picolink_dev *mfd) {
    struct picolink_led *pled, *tmp;
    
    mutex_lock(&leds_lock);
    list_for_each_entry_safe(pled, tmp, &picolink_leds_list, node) {
        if (pled->mfd == mfd) {
            led_classdev_unregister(&pled->cdev);
            list_del(&pled->node);
            kfree(pled);
        }
    }
    mutex_unlock(&leds_lock);
}
EXPORT_SYMBOL_GPL(picolink_leds_cleanup);

/* * ==========================================
 * Linux PWM Subsystem Implementation
 * ==========================================
 */

static int picolink_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
                              const struct pwm_state *state)
{
    struct picolink_pwm_chip *pchip = pwmchip_get_drvdata(chip);
    struct picolink_dev *mfd = pchip->mfd;
    int pin = pwm->hwpwm; /* Mapping channel index directly to pin is simplistic but fits here */
    
    if (!state->enabled) {
        return picolink_send_pwm_cmd(mfd, pin, 0);
    }

    /* * Convert ns period/duty_cycle to 0-255 brightness 
     * Assuming simple mapping where duty/period ratio defines the 8-bit value
     */
    u64 duty = state->duty_cycle;
    u64 period = state->period;
    u32 val;

    if (period == 0) return -EINVAL;
    
    val = (duty * 65535) / period;
    if (val > 65535) val = 65535;

    return picolink_send_pwm_cmd(mfd, pin, (uint8_t)val);
}

static const struct pwm_ops picolink_pwm_ops = {
    .apply = picolink_pwm_apply,
    // .owner = THIS_MODULE,
};

static int picolink_pwm_probe(struct platform_device *pdev) {
    struct picolink_dev *mfd = dev_get_drvdata(pdev->dev.parent);
    struct pwm_chip *chip;
    struct picolink_pwm_chip *pchip;
    int ret;

    /* 1. Выделяем чип вместе с нашими приватными данными */
    chip = devm_pwmchip_alloc(&pdev->dev, 30, sizeof(*pchip));
    if (IS_ERR(chip))
        return PTR_ERR(chip);

    /* 2. Получаем указатель на наши данные внутри выделенной памяти */
    pchip = pwmchip_get_drvdata(chip);
    pchip->mfd = mfd;
    // pchip->chip = *chip; // УДАЛИ ЭТУ СТРОКУ

    /* 3. Настраиваем только необходимые поля */
    chip->ops = &picolink_pwm_ops;

    /* 4. Регистрируем */
    ret = devm_pwmchip_add(&pdev->dev, chip);
    if (ret < 0) {
        dev_err(&pdev->dev, "Failed to add PWM chip: %d\n", ret);
        return ret;
    }

    platform_set_drvdata(pdev, chip);
    return 0;
}

static int picolink_pwm_remove(struct platform_device *pdev) {
    // struct picolink_pwm_chip *pchip = platform_get_drvdata(pdev);
    // pwmchip_remove(&pchip->chip);
    // struct pwm_chip *chip = platform_get_drvdata(pdev);
    return 0;
}

struct platform_driver picolink_pwm_driver = {
    .probe = picolink_pwm_probe,
    .remove = picolink_pwm_remove,
    .driver = {
        .name = "picolink-pwm",
    },
};