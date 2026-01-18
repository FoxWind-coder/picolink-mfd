//core.c
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/tty.h>
#include "picolink.h"

// Ссылаемся на драйвер GPIO, который лежит в другом файле
extern struct platform_driver picolink_gpio_driver;
extern struct platform_driver picolink_i2c_driver;
extern struct platform_driver picolink_uart_driver; // Добавлено

extern struct picolink_uart *uart_instance;

extern int picolink_tty_init(void); // Добавлено
extern void picolink_tty_exit(void); // Добавлено
extern void picolink_uart_push_data(const u8 *data, size_t size);

// Описание дочерних устройств (клеток MFD)
static struct mfd_cell picolink_cells[] = {
    { .name = "picolink-gpio" },
    { .name = "picolink-i2c"  }, // Теперь добавляем
    { .name = "picolink-uart" },
};

// --- НОВОЕ: Обработка входящих данных от Pico ---
static void picolink_bulk_in_callback(struct urb *urb) {
    struct picolink_dev *dev = urb->context;
    usb_packet_t *pkt = dev->bulk_in_buffer;
    int status = urb->status;
    int len;

    switch (status) {
    case 0:          /* Успех */
        break;
    case -ECONNRESET:
    case -ENOENT:
    case -ESHUTDOWN:
        return;      /* Устройство отключено */
    default:
        goto resubmit; /* Ошибка протокола, пробуем снова */
    }

    // Проверка: получили ли мы хотя бы заголовок?
    if (urb->actual_length >= sizeof(picolink_header_t)) {
        len = pkt->header.length;
        
        // Защита от переполнения: данные не могут быть больше полезной нагрузки пакета
        if (len > 60) len = 60; 

        // Если пакет пришел для UART
        if (pkt->header.iface_idx == IFACE_UART) {
            // Тип RESP обычно используется для данных из UART Pico в компьютер
            if (pkt->header.type == CMD_TYPE_RESP || pkt->header.type == CMD_TYPE_DATA) {
                picolink_uart_push_data(pkt->payload, len);
            }
        }else if (pkt->header.iface_idx == IFACE_I2C || pkt->header.iface_idx == IFACE_GPIO) {
            // Копируем ответ в структуру устройства и "будим" ждущий поток
            // pr_info("PicoLink Debug: Callback got I2C/GPIO packet, type: %d\n", pkt->header.type);
            memcpy(&dev->i2c_resp, pkt, sizeof(usb_packet_t));
            complete(&dev->i2c_done);
        }
        // Здесь можно добавить: else if (pkt->header.iface_idx == IFACE_I2C) ...
    }

resubmit:
    // Перезапуск URB критически важен для непрерывного чтения
    if (usb_submit_urb(dev->read_urb, GFP_ATOMIC)) {
        dev_err(&dev->udev->dev, "Failed to resubmit read urb\n");
    }
}

// Функция отправки пакета (экспортируемая для mfd-gpio.o)
int picolink_send_packet(struct usb_device *udev, uint8_t endpoint, void *data, int len) {
    int actual_length;
    return usb_bulk_msg(udev, usb_sndbulkpipe(udev, endpoint),
                        data, len, &actual_length, 1000);
}
EXPORT_SYMBOL_GPL(picolink_send_packet);

// Обработчик записи в /dev/picolink
static ssize_t picolink_dev_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    struct miscdevice *mdev = file->private_data;
    struct picolink_dev *dev = container_of(mdev, struct picolink_dev, miscdev);
    char kbuf[32];
    int sda, scl;
    int tx, rx;
    usb_packet_t *pkt;

    if (count == 0) return 0;
    if (count >= sizeof(kbuf)) count = sizeof(kbuf) - 1;
    
    if (copy_from_user(kbuf, buf, count)) return -EFAULT;
    kbuf[count] = '\0';

    if (sscanf(kbuf, "i2c %d %d", &sda, &scl) == 2) {
        pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
        if (!pkt) return -ENOMEM;

        pkt->header.type = CMD_TYPE_CONFIG;
        pkt->header.iface_idx = IFACE_I2C;
        pkt->header.length = 2;
        pkt->payload[0] = (uint8_t)sda;
        pkt->payload[1] = (uint8_t)scl;

        picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
        kfree(pkt);
        dev_info(&dev->udev->dev, "Command sent: Set I2C SDA:%d SCL:%d\n", sda, scl);
    } else if (sscanf(kbuf, "uart %d %d", &tx, &rx) == 2) {
        pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
        if (!pkt) return -ENOMEM;

        pkt->header.type = CMD_TYPE_CONFIG;
        pkt->header.iface_idx = IFACE_UART;
        pkt->header.length = sizeof(uart_config_t);

        uart_config_t *ucfg = (uart_config_t *)pkt->payload;
        ucfg->tx_pin = (uint8_t)tx;
        ucfg->rx_pin = (uint8_t)rx;
        ucfg->baudrate = 115200; // Дефолт при смене пинов
        ucfg->databits = 8;
        ucfg->stopbits = 1;

        // Обновляем локальные данные в mfd-uart, чтобы stty их не затер
        if (uart_instance) {
            uart_instance->tx_pin = (uint8_t)tx;
            uart_instance->rx_pin = (uint8_t)rx;
        }

        picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, pkt, sizeof(*pkt));
        kfree(pkt);
        dev_info(&dev->udev->dev, "UART pins changed: TX:%d RX:%d\n", tx, rx);
    }
    return count;
}

int picolink_transfer(struct picolink_dev *dev, usb_packet_t *tx_pkt, usb_packet_t *rx_pkt) {
    int ret;

    reinit_completion(&dev->i2c_done);

    // 1. Отправляем команду
    ret = picolink_send_packet(dev->udev, dev->bulk_out_endpointAddr, tx_pkt, sizeof(usb_packet_t));
    if (ret < 0) {
        dev_err(&dev->udev->dev, "Transfer: USB send failed: %d\n", ret);
        return ret;
    }

    // 2. Ждем ответа от callback (тайм-аут 500мс)
    if (!wait_for_completion_timeout(&dev->i2c_done, msecs_to_jiffies(500))) {
        dev_err(&dev->udev->dev, "Transfer: Timeout waiting for Pico response!\n");
        return -ETIMEDOUT;
    }

    // 3. Копируем результат
    if (rx_pkt) {
        memcpy(rx_pkt, &dev->i2c_resp, sizeof(usb_packet_t));
    }

    return 0;
}
EXPORT_SYMBOL_GPL(picolink_transfer);

static const struct file_operations picolink_fops = {
    .owner = THIS_MODULE,
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

    // Поиск эндпоинтов
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

    // --- НОВОЕ: Настройка чтения ---
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

    // Регистрация символьного устройства
    dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    dev->miscdev.name = "picolink";
    dev->miscdev.fops = &picolink_fops;
    dev->miscdev.parent = &interface->dev;
    ret = misc_register(&dev->miscdev);
    if (ret) goto err_free_urb;

    // Запуск слушателя USB
    ret = usb_submit_urb(dev->read_urb, GFP_KERNEL);
    if (ret) goto err_misc;

    // Регистрация MFD
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
    
    if (dev) {
        usb_kill_urb(dev->read_urb);
        mfd_remove_devices(&interface->dev);
        misc_deregister(&dev->miscdev);
        usb_free_urb(dev->read_urb);
        kfree(dev->bulk_in_buffer);
        usb_put_dev(dev->udev);
        kfree(dev);
    }

    dev_info(&interface->dev, "PicoLink MFD: Disconnected\n");
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

    // 1. Инициализация TTY драйвера (создание структуры)    
    ret = picolink_tty_init();
    if (ret) goto err_tty;    

    // 1. Регистрируем платформенный драйвер GPIO
    ret = platform_driver_register(&picolink_gpio_driver);
    if (ret) goto err_gpio;

    // 2. Регистрируем платформенный драйвер I2C
    ret = platform_driver_register(&picolink_i2c_driver);
    if (ret) goto err_i2c;

    ret = platform_driver_register(&picolink_uart_driver); // Не забудьте эту строку!
    if (ret) goto err_uart;

    // 3. Регистрируем USB драйвер (он создаст устройства, которые подхватят драйверы выше)
    ret = usb_register(&picolink_driver);

    return 0;

err_uart:
    platform_driver_unregister(&picolink_uart_driver);
err_i2c:
    platform_driver_unregister(&picolink_i2c_driver);
err_gpio:
    platform_driver_unregister(&picolink_gpio_driver);
err_tty:
    picolink_tty_exit();
    return ret;
}

static void __exit picolink_exit(void) {
    // Выгружаем в обратном порядке
    usb_deregister(&picolink_driver);
    platform_driver_unregister(&picolink_uart_driver);
    platform_driver_unregister(&picolink_i2c_driver);
    platform_driver_unregister(&picolink_gpio_driver);
    picolink_tty_exit();
}

module_init(picolink_init);
module_exit(picolink_exit);

MODULE_DESCRIPTION("PicoLink MFD Core Driver");
MODULE_AUTHOR("Alderpaw");
MODULE_LICENSE("GPL");