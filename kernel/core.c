// core.c
#include <linux/version.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/tty.h>
#include <linux/hwmon.h>
#include "picolink.h"
#include "mfd-adc.h"
#include "mfd-pwm.h" /* NEW: Include header for PWM/LEDs */

#if KERNEL_VERSION(6, 11, 0) > LINUX_VERSION_CODE
    #define REMOVE_RET_TYPE int
    #define REMOVE_RET_VAL  0
#else
    #define REMOVE_RET_TYPE void
    #define REMOVE_RET_VAL void
#endif

static LIST_HEAD(picolink_adcs_list);
static DEFINE_MUTEX(adcs_lock);
struct class *picolink_servo_class;
EXPORT_SYMBOL_GPL(picolink_servo_class);

LIST_HEAD(picolink_servos_list);
DEFINE_MUTEX(servos_lock);
EXPORT_SYMBOL_GPL(picolink_servos_list);
EXPORT_SYMBOL_GPL(servos_lock);

/* Reference to platform drivers */
extern struct platform_driver picolink_gpio_driver;
extern struct platform_driver picolink_i2c_driver;
extern struct platform_driver picolink_uart_driver;
extern struct platform_driver picolink_spi_driver;
extern struct platform_driver picolink_adc_driver;
extern struct platform_driver picolink_pwm_driver; /* NEW */

extern struct picolink_uart *uart_instance;

extern int picolink_tty_init(void);
extern void picolink_tty_exit(void);
extern void picolink_uart_push_data(const u8 *data, size_t size);

/* MFD cells description */
static struct mfd_cell i2c_cell = { .name = "picolink-i2c" };
static struct mfd_cell uart_cell = { .name = "picolink-uart" };
static struct mfd_cell spi_cell = { .name = "picolink-spi" };

/* * LED logic removed from here and moved to mfd-pwm.c 
 * URB completion, set_brightness, attribute groups -> GONE
 */

/* Handle incoming data from Pico via Bulk Input endpoint */
static void picolink_bulk_in_callback(struct urb *urb) {
    struct picolink_dev *dev = urb->context;
    usb_packet_t *pkt = dev->bulk_in_buffer;
    int status = urb->status;
    int len;

    switch (status) {
    case 0:          break;
    case -ECONNRESET:
    case -ENOENT:
    case -ESHUTDOWN: return;
    default:         goto resubmit;
    }

    if (urb->actual_length >= sizeof(picolink_header_t)) {
        len = pkt->header.length;
        if (len > 60) len = 60; 

        if (pkt->header.type == CMD_TYPE_LOG) {
            char log_msg[61];
            memcpy(log_msg, pkt->payload, len);
            log_msg[len] = '\0';
            dev_info(&dev->udev->dev, "Pico: %s\n", log_msg);
        } 
        else if (pkt->header.iface_idx == IFACE_UART) {
            if (pkt->header.type == CMD_TYPE_RESP || pkt->header.type == CMD_TYPE_DATA) {
                if (len > 0) picolink_uart_push_data(pkt->payload, len);
            }
        }else if (pkt->header.iface_idx == IFACE_SERVO) {
            // Просто подтверждаем выполнение, чтобы разблокировать ожидающих (если они есть)
            usb_packet_t *dest = dev->current_rx_buf; 
            if (dest) {
                memcpy(dest, pkt, sizeof(usb_packet_t));
                dev->current_rx_buf = NULL; 
                complete(&dev->i2c_done);
            }
        }
        else if (pkt->header.iface_idx == IFACE_I2C || 
            pkt->header.iface_idx == IFACE_GPIO || 
            pkt->header.iface_idx == IFACE_ADC ||
            pkt->header.iface_idx == IFACE_SPI) { // Added SPI check here to catch it properly
            
            usb_packet_t *dest = dev->current_rx_buf; 
            if (dest) {
                memcpy(dest, pkt, sizeof(usb_packet_t));
                dev->current_rx_buf = NULL; 
                complete(&dev->i2c_done);
            }
        }
    }

resubmit:
    if (usb_submit_urb(dev->read_urb, GFP_ATOMIC)) {
        dev_err(&dev->udev->dev, "Failed to resubmit read urb\n");
    }
}

/* Helper for one-way packet transmission */
int picolink_send_packet(struct usb_device *udev, uint8_t endpoint, void *data, int len) {
    int actual_length;
    return usb_bulk_msg(udev, usb_sndbulkpipe(udev, endpoint),
                        data, len, &actual_length, 1000);
}
EXPORT_SYMBOL_GPL(picolink_send_packet);

static int is_named_device(struct device *dev, void *data) {
    const char *name = data;
    return strcmp(dev_name(dev), name) == 0 || 
           (dev->init_name && strcmp(dev->init_name, name) == 0) ||
           strstr(dev_name(dev), name) != NULL;
}

static void picolink_remove_subdev(struct device *parent, const char *name) {
    struct device *child = device_find_child(parent, (void *)name, is_named_device);
    if (child) {
        platform_device_unregister(to_platform_device(child));
        put_device(child);
    }
}

/* Handler for /dev/picolink writes */
static ssize_t picolink_dev_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    struct picolink_dev *dev = file->private_data;
    char kbuf[64];
    int sda, scl, tx, rx;
    usb_packet_t *pkt;
    int ret;
    int led_pin;

    if (!dev) return -EIO;

    if (count == 0) return 0;
    if (count >= sizeof(kbuf)) count = sizeof(kbuf) - 1;
    if (copy_from_user(kbuf, buf, count)) return -EFAULT;
    kbuf[count] = '\0';

    pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
    if (!pkt) return -ENOMEM;

    /* UPDATED LED HANDLING */
    if (strncmp(kbuf, "led ", 4) == 0) {
        if (strstr(kbuf, "disable")) {
            if (sscanf(kbuf, "led %d disable", &led_pin) == 1) {
                if (picolink_led_cmd_disable(dev, led_pin) == 0)
                    dev_info(&dev->udev->dev, "LED on pin %d removed\n", led_pin);
                else
                    dev_warn(&dev->udev->dev, "LED on pin %d not found\n", led_pin);
            }
        } else {
            if (sscanf(kbuf, "led %d", &led_pin) == 1) {
                ret = picolink_led_cmd_enable(dev, led_pin);
                if (ret == 0)
                    dev_info(&dev->udev->dev, "Created LED device: picolink_led_%d\n", led_pin);
                else if (ret == -EEXIST)
                    dev_warn(&dev->udev->dev, "LED on pin %d already exists\n", led_pin);
                else
                    dev_err(&dev->udev->dev, "Failed to create LED on pin %d\n", led_pin);
            }
        }
        /* No manual packet sending here anymore, mfd-pwm.c handles config */
    }
    else if (sscanf(kbuf, "i2c %d %d", &sda, &scl) == 2) {
        pkt->header.type = CMD_TYPE_CONFIG;
        pkt->header.iface_idx = IFACE_I2C;
        pkt->header.length = 2;
        pkt->payload[0] = (uint8_t)sda;
        pkt->payload[1] = (uint8_t)scl;
        
        picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
        
        if (!dev->i2c_registered) {
            ret = mfd_add_devices(&dev->interface->dev, PLATFORM_DEVID_AUTO, &i2c_cell, 1, NULL, 0, NULL);
            if (!ret) dev->i2c_registered = true;
        }
    } else if (strncmp(kbuf, "i2c disable", 11) == 0) {
        pkt->header.type = CMD_TYPE_DISABLE;
        pkt->header.iface_idx = IFACE_I2C;
        picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));

        if (dev->i2c_registered) {
            picolink_remove_subdev(&dev->interface->dev, "picolink-i2c");
            dev->i2c_registered = false;
        }
    }
    /* UART configuration */
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

        picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));

        if (!dev->uart_registered) {
            ret = mfd_add_devices(&dev->interface->dev, PLATFORM_DEVID_AUTO, &uart_cell, 1, NULL, 0, NULL);
            if (!ret) dev->uart_registered = true;
        }
        
        if (uart_instance) {
            uart_instance->tx_pin = (uint8_t)tx;
            uart_instance->rx_pin = (uint8_t)rx;
        }
    } else if (strncmp(kbuf, "uart disable", 12) == 0) {
        pkt->header.type = CMD_TYPE_DISABLE;
        pkt->header.iface_idx = IFACE_UART;
        picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));

        if (dev->uart_registered) {
            picolink_remove_subdev(&dev->interface->dev, "picolink-uart");
            dev->uart_registered = false;
        }
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
            if (!achan) { kfree(pkt); return -ENOMEM; }

            achan->mfd = dev;
            achan->pin = (uint8_t)pin;
            strncpy(achan->name, adc_name, 31);

            achan->hwmon_dev = hwmon_device_register_with_groups(&dev->udev->dev, 
                                            achan->name, achan, picolink_adc_groups);
            
            if (IS_ERR(achan->hwmon_dev)) {
                kfree(achan);
                kfree(pkt);
                return PTR_ERR(achan->hwmon_dev);
            }

            mutex_lock(&adcs_lock);
            list_add(&achan->node, &picolink_adcs_list);
            mutex_unlock(&adcs_lock);
        }
    } else if (strncmp(kbuf, "spi ", 4) == 0) {
        int sck, mosi, miso, cs_pin, cs_idx;
        
        if (sscanf(kbuf, "spi %d %d %d", &sck, &mosi, &miso) == 3) {
            pkt->header.type = CMD_TYPE_CONFIG;
            pkt->header.iface_idx = IFACE_SPI;
            pkt->header.length = sizeof(spi_config_t);
            spi_config_t *scfg = (spi_config_t *)pkt->payload;
            
            scfg->sck_pin = (uint8_t)sck;
            scfg->mosi_pin = (uint8_t)mosi;
            scfg->miso_pin = (uint8_t)miso;
            scfg->baudrate = 1000000;
            memset(scfg->cs_pins, 0xFF, 4); 
            
            picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));

            if (!dev->spi_registered) {
                ret = mfd_add_devices(&dev->interface->dev, PLATFORM_DEVID_AUTO, &spi_cell, 1, NULL, 0, NULL);
                if (!ret) dev->spi_registered = true;
            }
        }
        else if (sscanf(kbuf, "spi cs %d %d", &cs_idx, &cs_pin) == 2) {
            if (!dev->spi_registered) {
                kfree(pkt);
                return -EINVAL;
            }
            if (cs_idx >= 0 && cs_idx < 4) {
                pkt->header.type = CMD_TYPE_CONFIG;
                pkt->header.iface_idx = IFACE_SPI;
                pkt->header.length = sizeof(spi_config_t);
                
                spi_config_t *scfg = (spi_config_t *)pkt->payload;
                scfg->sck_pin = 0xFF;
                scfg->mosi_pin = 0xFF;
                scfg->miso_pin = 0xFF;
                memset(scfg->cs_pins, 0xFF, 4);
                scfg->cs_pins[cs_idx] = (uint8_t)cs_pin;

                picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
                
                extern int picolink_spi_add_device(struct picolink_dev *mfd, int index, int pin);
                picolink_spi_add_device(dev, cs_idx, cs_pin);
            }
        }
    }
        else if (strncmp(kbuf, "servo", 5) == 0) {
            int pin, val;
            
            // Обработка "servoX disable"
            if (strstr(kbuf, "disable")) {
                if (sscanf(kbuf, "servo%d disable", &pin) == 1) {
                    struct picolink_servo *servo, *tmp;
                    
                    pkt->header.type = CMD_TYPE_DISABLE;
                    pkt->header.iface_idx = IFACE_SERVO;
                    pkt->payload[0] = (uint8_t)pin;
                    picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));

                    mutex_lock(&servos_lock);
                    list_for_each_entry_safe(servo, tmp, &picolink_servos_list, node) {
                        if (servo->pin == (uint8_t)pin) {
                            device_unregister(servo->dev);
                            list_del(&servo->node);
                            kfree(servo);
                        }
                    }
                    mutex_unlock(&servos_lock);
                    dev_info(&dev->udev->dev, "Servo on pin %d disabled\n", pin);
                }
            } 
            // Обработка "servoX Y" (Установка угла или инициализация)
            else if (sscanf(kbuf, "servo%d %d", &pin, &val) == 2) {
                struct picolink_servo *servo;
                bool found = false;

                mutex_lock(&servos_lock);
                list_for_each_entry(servo, &picolink_servos_list, node) {
                    if (servo->pin == (uint8_t)pin) {
                        found = true;
                        break;
                    }
                }
                mutex_unlock(&servos_lock);

                if (!found) {
                    // Если серво еще не инициализирован, считаем Y за Range (диапазон)
                    picolink_servo_cmd_config(dev, (uint8_t)pin, (uint16_t)val);
                    dev_info(&dev->udev->dev, "Servo initialized on pin %d (Range: %d)\n", pin, val);
                } else {
                    // Если уже есть в списке, Y - это угол
                    picolink_servo_cmd_set(dev, (uint8_t)pin, (uint16_t)val);
                }
            }
        }
    kfree(pkt);
    return count;
}

int picolink_transfer(struct picolink_dev *dev, usb_packet_t *tx_pkt, usb_packet_t *rx_pkt_out) {
    int ret;
    int actual_length;

    if (mutex_lock_interruptible(&dev->i2c_lock))
        return -ERESTARTSYS;

    memcpy(dev->transfer_tx_buf, tx_pkt, sizeof(usb_packet_t));
    reinit_completion(&dev->i2c_done);
    dev->current_rx_buf = dev->transfer_rx_buf; 

    ret = usb_bulk_msg(dev->udev, 
                       usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
                       dev->transfer_tx_buf, sizeof(usb_packet_t), 
                       &actual_length, 500);

    if (ret == 0) {
        if (wait_for_completion_timeout(&dev->i2c_done, msecs_to_jiffies(500))) {
            memcpy(rx_pkt_out, dev->transfer_rx_buf, sizeof(usb_packet_t));
            ret = 0;
        } else {
            ret = -ETIMEDOUT;
        }
    }

    dev->current_rx_buf = NULL;
    mutex_unlock(&dev->i2c_lock);
    return ret;
}
EXPORT_SYMBOL_GPL(picolink_transfer);

static int picolink_dev_open(struct inode *inode, struct file *file) {
    struct miscdevice *mdev = file->private_data;
    struct picolink_dev *dev = container_of(mdev, struct picolink_dev, miscdev);
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

    /* Base MFD cells registered upon device discovery */
    static struct mfd_cell base_cells[] = {
        { .name = "picolink-gpio" },
        { .name = "picolink-adc"  },
        { .name = "picolink-pwm"  }, /* Added PWM cell */
    };

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) return -ENOMEM;

    dev->udev = usb_get_dev(interface_to_usbdev(interface));
    dev->interface = interface;
    init_completion(&dev->i2c_done);
    mutex_init(&dev->i2c_lock);

    dev->transfer_tx_buf = kzalloc(sizeof(usb_packet_t), GFP_KERNEL);
    dev->transfer_rx_buf = kzalloc(sizeof(usb_packet_t), GFP_KERNEL);
    if (!dev->transfer_tx_buf || !dev->transfer_rx_buf) {
        ret = -ENOMEM;
        goto err_put;
    }

    iface_desc = interface->cur_altsetting;
    for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
        endpoint = &iface_desc->endpoint[i].desc;
        if (!dev->bulk_out_endpointAddr && usb_endpoint_is_bulk_out(endpoint))
            dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
        if (!dev->bulk_in_endpointAddr && usb_endpoint_is_bulk_in(endpoint))
            dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
    }

    if (!dev->bulk_out_endpointAddr || !dev->bulk_in_endpointAddr) {
        ret = -ENODEV;
        goto err_free_tx;
    }

    dev->bulk_in_buffer = kmalloc(64, GFP_KERNEL);
    dev->read_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!dev->read_urb || !dev->bulk_in_buffer) {
        ret = -ENOMEM;
        goto err_free_tx;
    }

    usb_fill_bulk_urb(dev->read_urb, dev->udev,
                      usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr),
                      dev->bulk_in_buffer, 64,
                      picolink_bulk_in_callback, dev);
    
    usb_set_intfdata(interface, dev);

    dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    dev->miscdev.name = "picolink";
    dev->miscdev.fops = &picolink_fops;
    dev->miscdev.parent = &interface->dev;
    
    ret = misc_register(&dev->miscdev);
    if (ret) goto err_free_urb;

    /* Registers gpio, adc, AND pwm now */
    ret = mfd_add_devices(&interface->dev, PLATFORM_DEVID_AUTO, base_cells, ARRAY_SIZE(base_cells), NULL, 0, NULL);
    if (ret) goto err_misc;

    ret = usb_submit_urb(dev->read_urb, GFP_KERNEL);
    if (ret) goto err_misc;

    dev_info(&interface->dev, "PicoLink MFD Ready.\n");
    return 0;

err_misc:
    misc_deregister(&dev->miscdev);
err_free_urb:
    usb_free_urb(dev->read_urb);
    kfree(dev->bulk_in_buffer);
err_free_tx:
    kfree(dev->transfer_tx_buf);
err_put:
    usb_put_dev(dev->udev);
    kfree(dev);
    return ret;
}

static void picolink_disconnect(struct usb_interface *interface) {
    struct picolink_dev *dev = usb_get_intfdata(interface);
    struct picolink_adc_chan *achan, *atmp;
    
    if (!dev) return;

    dev->disconnected = true;

    if (dev->read_urb) usb_kill_urb(dev->read_urb);

    /* * CLEANUP MOVED: LED cleanup is now handled via picolink_leds_cleanup 
     * However, since mfd_remove_devices removes the platform driver, 
     * and picolink-pwm driver's remove() handles chip removal,
     * we explicitly clean up manually created LEDs here to be safe.
     */
    picolink_servos_cleanup(dev);
    picolink_leds_cleanup(dev);

    /* Manual ADC cleanup (still here as per instructions to only move LED/PWM) */
    mutex_lock(&adcs_lock);
    list_for_each_entry_safe(achan, atmp, &picolink_adcs_list, node) {
        if (achan->mfd == dev) {
            hwmon_device_unregister(achan->hwmon_dev);
            list_del(&achan->node);
            kfree(achan);
        }
    }
    mutex_unlock(&adcs_lock);

    mfd_remove_devices(&interface->dev);
    misc_deregister(&dev->miscdev);

    usb_set_intfdata(interface, NULL);
    
    if (dev->read_urb) usb_free_urb(dev->read_urb);
    kfree(dev->bulk_in_buffer);
    if (dev->udev) usb_put_dev(dev->udev);
    
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

    picolink_servo_class = class_create("pico-servo"); // Для ядра 6.4+ аргумент один
    if (IS_ERR(picolink_servo_class)) {
        pr_err("Failed to create servo class\n");
        return PTR_ERR(picolink_servo_class); // <--- ОБЯЗАТЕЛЬНО вернуть код ошибки
    }

    ret = picolink_tty_init();
    if (ret) goto err_tty;    

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

    /* Register the NEW PWM driver */
    ret = platform_driver_register(&picolink_pwm_driver);
    if (ret) goto err_pwm;

    ret = usb_register(&picolink_driver);
    if (ret) goto err_usb;

    return 0;
    
err_usb:
    platform_driver_unregister(&picolink_pwm_driver);
err_pwm:
    platform_driver_unregister(&picolink_adc_driver);
err_adc:
    platform_driver_unregister(&picolink_spi_driver);
err_spi: /* Fixed label order logic */
    platform_driver_unregister(&picolink_uart_driver);
err_uart:
    platform_driver_unregister(&picolink_i2c_driver);
err_i2c:
    platform_driver_unregister(&picolink_gpio_driver);
err_gpio:
    picolink_tty_exit();
err_tty:
    return ret;
}

static void __exit picolink_exit(void) {
    usb_deregister(&picolink_driver);
    platform_driver_unregister(&picolink_pwm_driver); /* Unregister PWM */
    platform_driver_unregister(&picolink_uart_driver);
    platform_driver_unregister(&picolink_i2c_driver);
    platform_driver_unregister(&picolink_gpio_driver);
    platform_driver_unregister(&picolink_adc_driver);
    platform_driver_unregister(&picolink_spi_driver);
    // class_destroy(picolink_servo_class);
    if (picolink_servo_class)
        class_destroy(picolink_servo_class);
    picolink_tty_exit();
}

module_init(picolink_init);
module_exit(picolink_exit);

MODULE_DESCRIPTION("PicoLink MFD Core Driver");
MODULE_AUTHOR("Alderpaw");
MODULE_LICENSE("GPL");