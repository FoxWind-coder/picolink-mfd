//core.c
#include <linux/version.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/tty.h>
#include <linux/leds.h>
#include <linux/hwmon.h>
#include "picolink.h"
#include "mfd-adc.h"

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
    #define REMOVE_RET_TYPE int
    #define REMOVE_RET_VAL  0
#else
    #define REMOVE_RET_TYPE void
    #define REMOVE_RET_VAL void
#endif

static LIST_HEAD(picolink_leds_list);
static LIST_HEAD(picolink_adcs_list);
static DEFINE_MUTEX(leds_lock);
static DEFINE_MUTEX(adcs_lock);

struct picolink_led {
    struct led_classdev cdev;
    struct picolink_dev *mfd;
    uint8_t pin;
    char name[32];
    struct list_head node; /* List node for global LED tracking */
};

/* Reference to platform drivers defined in other files */
extern struct platform_driver picolink_gpio_driver;
extern struct platform_driver picolink_i2c_driver;
extern struct platform_driver picolink_uart_driver;
extern struct platform_driver picolink_spi_driver;
extern struct platform_driver picolink_adc_driver;

extern struct picolink_uart *uart_instance;

extern int picolink_tty_init(void);
extern void picolink_tty_exit(void);
extern void picolink_uart_push_data(const u8 *data, size_t size);

/* MFD cells description */
static struct mfd_cell picolink_cells[] = {
    { .name = "picolink-gpio" },
    { .name = "picolink-i2c"  },
    { .name = "picolink-uart" },
    { .name = "picolink-adc"  },
    { .name = "picolink-spi"  },
};

static void picolink_led_urb_complete(struct urb *urb) {
    /* Release packet buffer and URB after transmission */
    kfree(urb->context); 
    usb_free_urb(urb);
}

static void picolink_led_set_brightness(struct led_classdev *led_cdev,
                                      enum led_brightness brightness) {
    struct picolink_led *pled = container_of(led_cdev, struct picolink_led, cdev);
    struct picolink_dev *dev = pled->mfd;
    usb_packet_t *pkt;
    struct urb *urb;
    int ret;

    if (!dev || dev->disconnected)
        return;

    pkt = kzalloc(sizeof(*pkt), GFP_ATOMIC);
    if (!pkt) return;

    urb = usb_alloc_urb(0, GFP_ATOMIC);
    if (!urb) {
        kfree(pkt);
        return;
    }

    pkt->header.type = CMD_TYPE_DATA;
    pkt->header.iface_idx = IFACE_PWM;
    pkt->header.length = 2;
    pkt->payload[0] = pled->pin;
    pkt->payload[1] = (uint8_t)brightness;

    usb_fill_bulk_urb(urb, dev->udev,
                      usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
                      pkt, sizeof(*pkt),
                      picolink_led_urb_complete, pkt);

    ret = usb_submit_urb(urb, GFP_ATOMIC);
    if (ret) {
        /* If device was unplugged during submission, it's expected now */
        if (ret != -ENODEV && ret != -ESHUTDOWN)
            dev_err(&dev->udev->dev, "Failed to submit LED URB: %d\n", ret);
        usb_free_urb(urb);
        kfree(pkt);
    }
}

static ssize_t disable_store(struct device *dev, struct device_attribute *attr,
                           const char *buf, size_t count) {
    struct led_classdev *led_cdev = dev_get_drvdata(dev);
    struct picolink_led *pled = container_of(led_cdev, struct picolink_led, cdev);
    int val;

    if (kstrtoint(buf, 10, &val) == 0 && val == 1) {
        dev_info(dev, "LED on pin %d disabled\n", pled->pin);
        led_classdev_unregister(&pled->cdev);
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

/* Handle incoming data from Pico */
static void picolink_bulk_in_callback(struct urb *urb) {
    struct picolink_dev *dev = urb->context;
    usb_packet_t *pkt = dev->bulk_in_buffer;
    int status = urb->status;
    int len;

    switch (status) {
    case 0:          /* Success */
        break;
    case -ECONNRESET:
    case -ENOENT:
    case -ESHUTDOWN:
        return;      /* Device disconnected */
    default:
        goto resubmit; /* Protocol error, try again */
    }

    if (urb->actual_length >= sizeof(picolink_header_t)) {
        len = pkt->header.length;
        if (len > 60) len = 60; 

        /* Forward Pico logs to dmesg */
        if (pkt->header.type == CMD_TYPE_LOG) {
            char log_msg[61];
            memcpy(log_msg, pkt->payload, len);
            log_msg[len] = '\0';
            dev_info(&dev->udev->dev, "Pico: %s\n", log_msg);
        } 
        /* UART Data handling */
        else if (pkt->header.iface_idx == IFACE_UART) {
            if (pkt->header.type == CMD_TYPE_RESP || pkt->header.type == CMD_TYPE_DATA) {
                picolink_uart_push_data(pkt->payload, len);
            }
        } 
        /* Sync responses for I2C / GPIO / ADC / SPI */
        else if (pkt->header.iface_idx == IFACE_I2C || pkt->header.iface_idx == IFACE_GPIO) {
            memcpy(&dev->i2c_resp, pkt, sizeof(usb_packet_t));
            complete(&dev->i2c_done);
        } else if (pkt->header.iface_idx == IFACE_ADC) {
            memcpy(&dev->i2c_resp, pkt, sizeof(usb_packet_t));
            complete(&dev->i2c_done);
        } else if (pkt->header.iface_idx == IFACE_SPI) {
            memcpy(&dev->i2c_resp, pkt, sizeof(usb_packet_t));
            complete(&dev->i2c_done);
        }
    }

resubmit:
    if (usb_submit_urb(dev->read_urb, GFP_ATOMIC)) {
        dev_err(&dev->udev->dev, "Failed to resubmit read urb\n");
    }
}

/* Packet transmission function */
int picolink_send_packet(struct usb_device *udev, uint8_t endpoint, void *data, int len) {
    int actual_length;
    return usb_bulk_msg(udev, usb_sndbulkpipe(udev, endpoint),
                        data, len, &actual_length, 1000);
}
EXPORT_SYMBOL_GPL(picolink_send_packet);

/* Handler for /dev/picolink writes */
static ssize_t picolink_dev_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    // struct miscdevice *mdev = file->private_data;
    // struct picolink_dev *dev = container_of(mdev, struct picolink_dev, miscdev);
    struct picolink_dev *dev = file->private_data;
    char kbuf[64];
    int sda, scl, tx, rx;
    usb_packet_t *pkt;
    int led_pin;

    if (!dev) return -EIO;

    // dev_info(&dev->udev->dev, "picolink_write called: count=%zu\n", count);
    pr_info("picolink: write called, count=%zu\n", count);

    if (count == 0) return 0;
    if (count >= sizeof(kbuf)) count = sizeof(kbuf) - 1;
    if (copy_from_user(kbuf, buf, count)) return -EFAULT;
    kbuf[count] = '\0';

    pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
    if (!pkt) return -ENOMEM;

    if (strncmp(kbuf, "led ", 4) == 0) {
        /* Handle LED removal */
        if (strstr(kbuf, "disable")) {
            if (sscanf(kbuf, "led %d disable", &led_pin) == 1) {
                struct picolink_led *pled, *tmp;
                bool found = false;

                mutex_lock(&leds_lock);
                list_for_each_entry_safe(pled, tmp, &picolink_leds_list, node) {
                    if (pled->pin == (uint8_t)led_pin) {
                        led_classdev_unregister(&pled->cdev);
                        list_del(&pled->node);
                        kfree(pled);
                        found = true;
                        break;
                    }
                }
                mutex_unlock(&leds_lock);
                
                if (found) {
                    dev_info(&dev->udev->dev, "LED on pin %d removed\n", led_pin);
                } else {
                    dev_warn(&dev->udev->dev, "LED on pin %d not found for disable\n", led_pin);
                }
            }
        } else {
            /* Handle LED creation */
            if (sscanf(kbuf, "led %d", &led_pin) == 1) {
                struct picolink_led *curr_led;
                bool exists = false;

                mutex_lock(&leds_lock);
                list_for_each_entry(curr_led, &picolink_leds_list, node) {
                    if (curr_led->pin == (uint8_t)led_pin) {
                        exists = true;
                        break;
                    }
                }
                mutex_unlock(&leds_lock);

                if (exists) {
                    dev_warn(&dev->udev->dev, "LED on pin %d already exists\n", led_pin);
                } else {
                    struct picolink_led *pled = kzalloc(sizeof(*pled), GFP_KERNEL);
                    if (!pled) { kfree(pkt); return -ENOMEM; }

                    pled->mfd = dev;
                    pled->pin = (uint8_t)led_pin;
                    snprintf(pled->name, sizeof(pled->name), "picolink_led_%d", led_pin);
                    
                    pled->cdev.name = pled->name;
                    pled->cdev.brightness_set = picolink_led_set_brightness;
                    pled->cdev.max_brightness = 255;
                    pled->cdev.groups = picolink_led_groups;

                    mutex_lock(&leds_lock);
                    list_add(&pled->node, &picolink_leds_list);
                    mutex_unlock(&leds_lock);

                    if (led_classdev_register(&dev->udev->dev, &pled->cdev) < 0) {
                        mutex_lock(&leds_lock);
                        list_del(&pled->node);
                        mutex_unlock(&leds_lock);
                        kfree(pled);
                    } else {
                        /* Send configuration to Pico hardware */
                        pkt->header.type = CMD_TYPE_CONFIG;
                        pkt->header.iface_idx = IFACE_PWM;
                        pkt->header.length = 1;
                        pkt->payload[0] = (uint8_t)led_pin;
                        picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
                        dev_info(&dev->udev->dev, "Created LED device: /sys/class/leds/%s\n", pled->name);
                    }
                }
            }
        }
        kfree(pkt);
        return count;
    }
     else if (sscanf(kbuf, "i2c %d %d", &sda, &scl) == 2) {
        pkt->header.type = CMD_TYPE_CONFIG;
        pkt->header.iface_idx = IFACE_I2C;
        pkt->header.length = 2;
        pkt->payload[0] = (uint8_t)sda;
        pkt->payload[1] = (uint8_t)scl;
        int ret = picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
        dev_info(&dev->udev->dev, "I2C CFG Raw Send: ret=%d, type=%d, iface=%d\n", ret, pkt->header.type, pkt->header.iface_idx);
        // picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
    } 
    else if (strncmp(kbuf, "i2c disable", 11) == 0) {
        pkt->header.type = CMD_TYPE_DISABLE;
        pkt->header.iface_idx = IFACE_I2C;
        int ret = picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
        dev_info(&dev->udev->dev, "I2C CFG Raw Send: ret=%d, type=%d, iface=%d\n", ret, pkt->header.type, pkt->header.iface_idx);
    }
    /* UART configuration or disable */
    else if (sscanf(kbuf, "uart %d %d", &tx, &rx) == 2) {
        pkt->header.type = CMD_TYPE_CONFIG;
        pkt->header.iface_idx = IFACE_UART;
        pkt->header.length = sizeof(uart_config_t);
        uart_config_t *ucfg = (uart_config_t *)pkt->payload;
        ucfg->tx_pin = (uint8_t)tx; 
        ucfg->rx_pin = (uint8_t)rx;
        ucfg->baudrate = 115200; 
        ucfg->databits = 8; 
        ucfg->stopbits = 1;

        int ret = picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
        dev_info(&dev->udev->dev, "UART CFG Send: TX=%d RX=%d, ret=%d\n", tx, rx, ret);
        
        if (uart_instance) {
            uart_instance->tx_pin = (uint8_t)tx;
            uart_instance->rx_pin = (uint8_t)rx;
        }
    } else if (strncmp(kbuf, "uart disable", 12) == 0) {
        pkt->header.type = CMD_TYPE_DISABLE;
        pkt->header.iface_idx = IFACE_UART;
        picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
    } else if (strncmp(kbuf, "adc", 3) == 0) {
        int pin;
        char adc_name[32];
        
        if (strstr(kbuf, "disable")) {
            if (sscanf(kbuf, "adc%d disable", &pin) == 1) {
                struct picolink_adc_chan *achan, *tmp;
                mutex_lock(&adcs_lock);
                list_for_each_entry_safe(achan, tmp, &picolink_adcs_list, node) {
                    if (achan->pin == (uint8_t)pin) {
                        hwmon_device_unregister(achan->hwmon_dev);
                        list_del(&achan->node);
                        kfree(achan);
                        dev_info(&dev->udev->dev, "ADC pin %d disabled\n", pin);
                    }
                }
                mutex_unlock(&adcs_lock);
            }
        } else if (sscanf(kbuf, "adc%d %31s", &pin, adc_name) == 2) {
            struct picolink_adc_chan *achan = kzalloc(sizeof(*achan), GFP_KERNEL);
            if (!achan) return -ENOMEM;

            achan->mfd = dev;
            achan->pin = (uint8_t)pin;
            strncpy(achan->name, adc_name, 31);

            achan->hwmon_dev = hwmon_device_register_with_groups(&dev->udev->dev, 
                                achan->name, achan, picolink_adc_groups);
            
            if (IS_ERR(achan->hwmon_dev)) {
                kfree(achan);
                return PTR_ERR(achan->hwmon_dev);
            }

            mutex_lock(&adcs_lock);
            list_add(&achan->node, &picolink_adcs_list);
            mutex_unlock(&adcs_lock);
            dev_info(&dev->udev->dev, "ADC pin %d enabled as %s\n", pin, adc_name);
        }
    } else if (strncmp(kbuf, "spi ", 4) == 0) {
        int sck, mosi, miso, cs_pin, cs_idx;
        
        /* 1. Main bus config: "spi sck mosi miso" */
        if (sscanf(kbuf, "spi %d %d %d", &sck, &mosi, &miso) == 3) {
            pkt->header.type = CMD_TYPE_CONFIG;
            pkt->header.iface_idx = IFACE_SPI;
            pkt->header.length = sizeof(spi_config_t);
            spi_config_t *scfg = (spi_config_t *)pkt->payload;
            
            scfg->sck_pin = (uint8_t)sck;
            scfg->mosi_pin = (uint8_t)mosi;
            scfg->miso_pin = (uint8_t)miso;
            scfg->baudrate = 1000000; /* 1MHz default */
            memset(scfg->cs_pins, 0xFF, 4); 
            
            picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
            dev_info(&dev->udev->dev, "SPI Bus Configured: SCK:%d MOSI:%d MISO:%d\n", sck, mosi, miso);
        }
        /* 2. Adding CS (child device): "spi cs index pin" */
        else if (sscanf(kbuf, "spi cs %d %d", &cs_idx, &cs_pin) == 2) {
            if (cs_idx >= 0 && cs_idx < 4) {
                /* Notify Pico about new CS pin */
                pkt->header.type = CMD_TYPE_CONFIG;
                pkt->header.iface_idx = IFACE_SPI;
                pkt->header.length = sizeof(spi_config_t);
                spi_config_t *scfg = (spi_config_t *)pkt->payload;
                
                /* Mark other pins as unchanged */
                scfg->sck_pin = 0xFF; 
                memset(scfg->cs_pins, 0xFF, 4);
                scfg->cs_pins[cs_idx] = (uint8_t)cs_pin;

                picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
                
                /* Register in Linux */
                extern int picolink_spi_add_device(struct picolink_dev *mfd, int index, int pin);
                picolink_spi_add_device(dev, cs_idx, cs_pin);
                
                dev_info(&dev->udev->dev, "SPI: Sent CS%d (pin %d) config to Pico\n", cs_idx, cs_pin);
            }
        }
        /* 3. Disable: "spi disable" or "spi cs index disable" */
        else if (strstr(kbuf, "disable")) {
             if (sscanf(kbuf, "spi cs %d disable", &cs_idx) == 1) {
                 extern void picolink_spi_remove_device(int index);
                 picolink_spi_remove_device(cs_idx);
             } else {
                 pkt->header.type = CMD_TYPE_DISABLE;
                 pkt->header.iface_idx = IFACE_SPI;
                 picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
                 dev_info(&dev->udev->dev, "SPI Bus Disabled\n");
             }
        }
    }

    kfree(pkt);
    return count;
}

int picolink_transfer(struct picolink_dev *dev, usb_packet_t *tx_pkt, usb_packet_t *rx_pkt) {
    int ret;

    reinit_completion(&dev->i2c_done);

    /* 1. Send command */
    ret = picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, tx_pkt, sizeof(usb_packet_t));
    if (ret < 0) {
        dev_err(&dev->udev->dev, "Transfer: USB send failed: %d\n", ret);
        return ret;
    }

    /* 2. Wait for callback response (500ms timeout) */
    if (!wait_for_completion_timeout(&dev->i2c_done, msecs_to_jiffies(500))) {
        dev_err(&dev->udev->dev, "Transfer: Timeout waiting for Pico response!\n");
        return -ETIMEDOUT;
    }

    /* 3. Copy result */
    if (rx_pkt) {
        memcpy(rx_pkt, &dev->i2c_resp, sizeof(usb_packet_t));
    }

    return 0;
}
EXPORT_SYMBOL_GPL(picolink_transfer);

static int picolink_dev_open(struct inode *inode, struct file *file) {
    /* Extract dev pointer via miscdevice struct */
    struct miscdevice *mdev = file->private_data;
    struct picolink_dev *dev = container_of(mdev, struct picolink_dev, miscdev);
    
    /* Set private_data so write() can access dev directly */
    file->private_data = dev; 
    
    if (!dev->udev) return -ENODEV;
    return 0;
}

static const struct file_operations picolink_fops = {
    .owner = THIS_MODULE,
    .open  = picolink_dev_open,
    .write = picolink_dev_write,
};

static int picolink_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct picolink_dev *dev;
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    int i, ret;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) return -ENOMEM;

    dev->udev = usb_get_dev(interface_to_usbdev(interface));
    dev->interface = interface;

    dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    dev->miscdev.name = "picolink";
    dev->miscdev.fops = &picolink_fops;
    dev->miscdev.parent = &interface->dev;

    /* Search for endpoints */
    iface_desc = interface->cur_altsetting;
    for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
        endpoint = &iface_desc->endpoint[i].desc;
        if (!dev->bulk_out_endpointAddr && usb_endpoint_is_bulk_out(endpoint)) {
            dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
        }
        if (!dev->bulk_in_endpointAddr && usb_endpoint_is_bulk_in(endpoint)) {
            dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
        }
    }

    if (!dev->bulk_out_endpointAddr || !dev->bulk_in_endpointAddr) {
        ret = -ENODEV;
        goto err_put;
    }

    /* Setup read URB */
    dev->bulk_in_buffer = kmalloc(64, GFP_KERNEL);
    dev->read_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->read_urb || !dev->bulk_in_buffer) {
        ret = -ENOMEM;
        goto err_put;
    }

    usb_fill_bulk_urb(dev->read_urb, dev->udev,
                      usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr),
                      dev->bulk_in_buffer, 64,
                      picolink_bulk_in_callback, dev);
    
    usb_set_intfdata(interface, dev);

    /* Register misc device */
    dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    dev->miscdev.name = "picolink";
    dev->miscdev.fops = &picolink_fops;
    dev->miscdev.parent = &interface->dev;
    dev->miscdev.this_device = &interface->dev;
    
    ret = misc_register(&dev->miscdev);
    if (ret == -EEXIST) {
        dev_err(&interface->dev, "CRITICAL: /dev/picolink already exists as a file. Please 'sudo rm /dev/picolink'\n");
        goto err_free_urb;
    }

    if (ret) {
        dev_err(&interface->dev, "Failed to register misc dev, error %d.\n", ret);
        goto err_free_urb;
    }

    /* Start USB listener */
    ret = usb_submit_urb(dev->read_urb, GFP_KERNEL);
    if (ret) goto err_misc;

    /* Register MFD cells */
    ret = mfd_add_devices(&interface->dev, PLATFORM_DEVID_AUTO,
                          picolink_cells, ARRAY_SIZE(picolink_cells),
                          NULL, 0, NULL);
    if (ret) goto err_misc;

    dev_info(&interface->dev, "PicoLink MFD Ready (UART/I2C/GPIO)\n");
    init_completion(&dev->i2c_done);
    return 0;

err_misc:
    misc_deregister(&dev->miscdev);
err_free_urb:
    usb_free_urb(dev->read_urb);
    kfree(dev->bulk_in_buffer);
err_put:
    usb_put_dev(dev->udev);
    kfree(dev);
    return ret;
}

static void picolink_disconnect(struct usb_interface *interface) {
    struct picolink_dev *dev = usb_get_intfdata(interface);
    struct picolink_led *pled, *tmp;
    struct picolink_adc_chan *achan, *atmp;
    
    if (!dev)
        return;

    /* 1. Mark as disconnected immediately to block new URBs */
    dev->disconnected = true;

    /* 2. Kill the main reader URB first */
    if (dev->read_urb)
        usb_kill_urb(dev->read_urb);

    /* 3. Unregister LEDs while mutex is held */
    mutex_lock(&leds_lock);
    list_for_each_entry_safe(pled, tmp, &picolink_leds_list, node) {
        if (pled->mfd == dev) {
            led_classdev_unregister(&pled->cdev);
            list_del(&pled->node);
            kfree(pled);
        }
    }
    mutex_unlock(&leds_lock);

    mutex_lock(&adcs_lock);
    list_for_each_entry_safe(achan, atmp, &picolink_adcs_list, node) {
        if (achan->mfd == dev) {
            hwmon_device_unregister(achan->hwmon_dev);
            list_del(&achan->node);
            kfree(achan);
        }
    }
    mutex_unlock(&adcs_lock);

    /* 4. Remove child MFD devices (UART, I2C, GPIO) */
    mfd_remove_devices(&interface->dev);

    /* 5. Unregister the control node /dev/picolink */
    misc_deregister(&dev->miscdev);

    /* 6. Final cleanup of device resources */
    usb_set_intfdata(interface, NULL);
    
    if (dev->read_urb)
        usb_free_urb(dev->read_urb);
    
    kfree(dev->bulk_in_buffer);
    
    if (dev->udev)
        usb_put_dev(dev->udev);

    complete_all(&dev->i2c_done);
    kfree(dev);

    dev_info(&interface->dev, "PicoLink MFD: Disconnected safely\n");
}

static const struct usb_device_id picolink_table[] = {
    { USB_DEVICE(PICOLINK_VID, PICOLINK_PID) },
    { }
};
MODULE_DEVICE_TABLE(usb, picolink_table);

static struct usb_driver picolink_driver = {
    .name = "picolink_mfd",
    .probe = picolink_probe,
    .disconnect = picolink_disconnect,
    .id_table = picolink_table,
};

static int __init picolink_init(void) {
    int ret;

    /* 1. Initialize TTY driver structure */
    ret = picolink_tty_init();
    if (ret) goto err_tty;    

    /* 2. Register platform drivers */
    ret = platform_driver_register(&picolink_gpio_driver);
    if (ret) goto err_gpio;

    ret = platform_driver_register(&picolink_i2c_driver);
    if (ret) goto err_i2c;

    ret = platform_driver_register(&picolink_uart_driver);
    if (ret) goto err_uart;

    ret = platform_driver_register(&picolink_spi_driver);
    if (ret) goto err_spi;

    ret = platform_driver_register(&picolink_adc_driver);
    if (ret) goto err_adc;

    /* 3. Register USB driver */
    // ret = usb_register(&picolink_driver);
    return usb_register(&picolink_driver);

err_uart:
    platform_driver_unregister(&picolink_uart_driver);
err_i2c:
    platform_driver_unregister(&picolink_i2c_driver);
err_gpio:
    platform_driver_unregister(&picolink_gpio_driver);
err_adc:
    platform_driver_unregister(&picolink_adc_driver);
err_spi:
    platform_driver_unregister(&picolink_spi_driver);
err_tty:
    picolink_tty_exit();
    return ret;
}

static void __exit picolink_exit(void) {
    /* Unload in reverse order */
    usb_deregister(&picolink_driver);
    platform_driver_unregister(&picolink_uart_driver);
    platform_driver_unregister(&picolink_i2c_driver);
    platform_driver_unregister(&picolink_gpio_driver);
    platform_driver_unregister(&picolink_adc_driver);
    platform_driver_unregister(&picolink_spi_driver);
    picolink_tty_exit();
}

module_init(picolink_init);
module_exit(picolink_exit);

MODULE_DESCRIPTION("PicoLink MFD Core Driver");
MODULE_AUTHOR("Alderpaw");
MODULE_LICENSE("GPL");