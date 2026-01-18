//mfd-uart.c
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/serial.h> // Added for driver operations
#include "picolink.h"

// Prototypes to prevent "no previous prototype" warnings
void picolink_uart_push_data(const u8 *data, size_t size);
int picolink_tty_init(void);
void picolink_tty_exit(void);

static struct tty_driver *picolink_tty_driver;
struct picolink_uart *uart_instance;
static const struct tty_port_operations p_port_ops = { };

static int p_uart_open(struct tty_struct *tty, struct file *filp) {
    struct picolink_uart *pu = uart_instance;

    if (!pu) return -ENODEV;

    // Note: tty_port_open expects tty->port to be already set
    tty->port = &pu->port;
    tty->driver_data = pu;

    return tty_port_open(tty->port, tty, filp);
}

static void p_uart_close(struct tty_struct *tty, struct file *filp) {
    tty_port_close(tty->port, tty, filp);
}

static void p_uart_write_bulk_callback(struct urb *urb) {
    if (urb->transfer_buffer)
        kfree(urb->transfer_buffer);
    usb_free_urb(urb);
}

static ssize_t p_uart_write(struct tty_struct *tty, const u8 *buf, size_t count) {
    struct picolink_uart *pu = tty->driver_data;
    struct urb *urb;
    usb_packet_t *pkt;
    int len = count > 60 ? 60 : count;

    // CRITICAL CHECK
    if (!pu || !pu->mfd || !pu->mfd->udev) {
        pr_err("picolink-uart: Attempt to write to NULL device\n");
        return -ENODEV;
    }

    urb = usb_alloc_urb(0, GFP_ATOMIC);
    if (!urb) return 0;

    pkt = kzalloc(sizeof(*pkt), GFP_ATOMIC);
    if (!pkt) {
        usb_free_urb(urb);
        return 0;
    }

    pkt->header.type = CMD_TYPE_DATA;
    pkt->header.iface_idx = IFACE_UART;
    pkt->header.length = len;
    memcpy(pkt->payload, buf, len);

    usb_fill_bulk_urb(urb, pu->mfd->udev,
                      usb_sndbulkpipe(pu->mfd->udev, pu->mfd->bulk_out_endpointAddr),
                      pkt, sizeof(*pkt),
                      p_uart_write_bulk_callback, NULL);

    if (usb_submit_urb(urb, GFP_ATOMIC)) {
        kfree(pkt);
        usb_free_urb(urb);
        return 0;
    }

    return len;
}

static unsigned int p_uart_write_room(struct tty_struct *tty) {
    return 64; // Size of USB packet buffer
}

static void p_uart_set_termios(struct tty_struct *tty, const struct ktermios *old) {
    struct picolink_uart *pu = tty->driver_data;
    struct urb *urb;
    usb_packet_t *pkt;
    uart_config_t *cfg;

    if (!pu || !pu->mfd || !pu->mfd->udev) return;

    // Allocate memory for URB and packet
    urb = usb_alloc_urb(0, GFP_ATOMIC);
    if (!urb) return;

    pkt = kzalloc(sizeof(*pkt), GFP_ATOMIC);
    if (!pkt) {
        usb_free_urb(urb);
        return;
    }

    pkt->header.type = CMD_TYPE_CONFIG;
    pkt->header.iface_idx = IFACE_UART;
    pkt->header.length = sizeof(uart_config_t);

    cfg = (uart_config_t *)pkt->payload;
    cfg->baudrate = tty_get_baud_rate(tty);
    cfg->tx_pin = pu->tx_pin;
    cfg->rx_pin = pu->rx_pin;
    cfg->databits = (C_CSIZE(tty) == CS7) ? 7 : 8;
    cfg->stopbits = (C_CSTOPB(tty)) ? 2 : 1;
    cfg->parity = (C_PARENB(tty)) ? 1 : 0;

    // Use the same callback as write, since it simply cleans up memory
    usb_fill_bulk_urb(urb, pu->mfd->udev,
                      usb_sndbulkpipe(pu->mfd->udev, pu->mfd->bulk_out_endpointAddr),
                      pkt, sizeof(*pkt),
                      p_uart_write_bulk_callback, NULL);

    if (usb_submit_urb(urb, GFP_ATOMIC)) {
        kfree(pkt);
        usb_free_urb(urb);
    }
}

static const struct tty_operations p_uart_ops = {
    .open = p_uart_open,
    .close = p_uart_close,
    .write = p_uart_write,
    .write_room = p_uart_write_room,
    .set_termios = p_uart_set_termios,
    .install = tty_standard_install, // Recommended to include
};

static int picolink_uart_probe(struct platform_device *pdev) {
    struct picolink_uart *pu;
    int ret;

    pu = devm_kzalloc(&pdev->dev, sizeof(*pu), GFP_KERNEL);
    if (!pu) return -ENOMEM;

    pu->tx_pin = 8;
    pu->rx_pin = 9;

    pu->mfd = dev_get_drvdata(pdev->dev.parent);
    if (!pu->mfd) return -EINVAL;

    tty_port_init(&pu->port);
    pu->port.ops = &p_port_ops; // Bind port operations

    // Register device
    pu->dev = tty_port_register_device(&pu->port, picolink_tty_driver, 0, &pdev->dev);
    if (IS_ERR(pu->dev)) {
        ret = PTR_ERR(pu->dev);
        tty_port_destroy(&pu->port);
        return ret;
    }

    platform_set_drvdata(pdev, pu);
    uart_instance = pu;
    
    dev_info(&pdev->dev, "TTY Pico device registered at /dev/ttyPico0\n");
    return 0;
}

// This function should be called by core.c when receiving RESP data from USB
void picolink_uart_push_data(const u8 *data, size_t size) {
    if (uart_instance) {
        tty_insert_flip_string(&uart_instance->port, data, size);
        tty_flip_buffer_push(&uart_instance->port);
    }
}
EXPORT_SYMBOL_GPL(picolink_uart_push_data);

static int picolink_uart_remove(struct platform_device *pdev) {
    struct picolink_uart *pu = platform_get_drvdata(pdev);
    uart_instance = NULL;
    tty_unregister_device(picolink_tty_driver, 0);
    tty_port_destroy(&pu->port);
    return 0;
}

struct platform_driver picolink_uart_driver = {
    .driver = { .name = "picolink-uart" },
    .probe = picolink_uart_probe,
    .remove = picolink_uart_remove,
};

// TTY driver initialization (called once during module load)
int __init picolink_tty_init(void) {
    picolink_tty_driver = tty_alloc_driver(1, TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV);
    if (IS_ERR(picolink_tty_driver))
        return PTR_ERR(picolink_tty_driver);

    picolink_tty_driver->driver_name = "picolink_tty";
    picolink_tty_driver->name = "ttyPico";
    picolink_tty_driver->major = 0;
    picolink_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
    picolink_tty_driver->subtype = SERIAL_TYPE_NORMAL;
    picolink_tty_driver->init_termios = tty_std_termios;
    
    tty_set_operations(picolink_tty_driver, &p_uart_ops);
    
    return tty_register_driver(picolink_tty_driver);
}

void picolink_tty_exit(void) {
    if (picolink_tty_driver) {
        tty_unregister_driver(picolink_tty_driver);
        tty_driver_kref_put(picolink_tty_driver); // Replacement for put_tty_driver in modern kernels
    }
}