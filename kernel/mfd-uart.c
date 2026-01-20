//mfd-uart.c
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/serial.h> // Added for driver operations
#include "picolink.h"

#define MAX_ACTIVE_URBS 4

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

    tty->driver_data = pu;
    return tty_port_open(&pu->port, tty, filp);
}

static void p_uart_close(struct tty_struct *tty, struct file *filp) {
    struct picolink_uart *pu = tty->driver_data;
    if (pu) {
        tty_port_close(&pu->port, tty, filp);
    }
}

// static void p_uart_write_bulk_callback(struct urb *urb) {
//     if (urb->transfer_buffer)
//         kfree(urb->transfer_buffer);
//     usb_free_urb(urb);
// }

static void p_uart_write_bulk_callback(struct urb *urb) {
    struct picolink_uart *pu = urb->context;
    
    if (urb->transfer_buffer)
        kfree(urb->transfer_buffer);
    usb_free_urb(urb);
    
    if (pu) {
        atomic_dec(&pu->active_urbs);
        tty_port_tty_wakeup(&pu->port);
    }
}

static ssize_t p_uart_write(struct tty_struct *tty, const u8 *buf, size_t count) {
    struct picolink_uart *pu = tty->driver_data;
    struct urb *urb;
    usb_packet_t *pkt;
    int len = count > 60 ? 60 : count;

    // CRITICAL CHECK
    if (!pu || !pu->mfd || pu->mfd->disconnected || !pu->mfd->udev) {
        pr_err("picolink-uart: Attempt to write to NULL device\n");
        return -ENODEV;
    }

    len = count > 60 ? 60 : count;

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
                      p_uart_write_bulk_callback, pu);

    atomic_inc(&pu->active_urbs);

    if (usb_submit_urb(urb, GFP_ATOMIC)) {
        kfree(pkt);
        usb_free_urb(urb);
        atomic_set(&pu->tx_busy, 0);
        return 0;
    }

    return len;
}

static unsigned int p_uart_write_room(struct tty_struct *tty) {
    struct picolink_uart *pu = tty->driver_data;
    if (atomic_read(&pu->active_urbs) >= MAX_ACTIVE_URBS)
        return 0;
    return 64; 
}

static void p_uart_set_termios(struct tty_struct *tty, const struct ktermios *old) {
    struct picolink_uart *pu = tty->driver_data;
    struct urb *urb;
    usb_packet_t *pkt;
    uart_config_t *cfg;

    if (!pu || !pu->mfd || pu->mfd->disconnected || !pu->mfd->udev) return;

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
static void p_uart_hangup(struct tty_struct *tty) {
    struct picolink_uart *pu = tty->driver_data;
    tty_port_hangup(&pu->port);
}

static int p_uart_tiocmget(struct tty_struct *tty)
{
    return TIOCM_DTR | TIOCM_RTS | TIOCM_CD | TIOCM_CTS;
}

static int p_uart_tiocmset(struct tty_struct *tty, unsigned int set, unsigned int clear)
{
    return 0;
}

static const struct tty_operations p_uart_ops = {
    .open = p_uart_open,
    .close = p_uart_close,
    .hangup = p_uart_hangup, 
    .write = p_uart_write,
    .write_room = p_uart_write_room,
    .tiocmget = p_uart_tiocmget,
    .tiocmset = p_uart_tiocmset,
    .set_termios = p_uart_set_termios,
    .install = tty_standard_install,
};

static int picolink_uart_probe(struct platform_device *pdev) {
    struct picolink_uart *pu;
    int ret;
    pu = devm_kzalloc(&pdev->dev, sizeof(*pu), GFP_KERNEL);
    if (!pu) return -ENOMEM;
    atomic_set(&pu->active_urbs, 0);
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
    if (uart_instance && uart_instance->mfd && !uart_instance->mfd->disconnected) {
        tty_insert_flip_string(&uart_instance->port, data, size);
        tty_flip_buffer_push(&uart_instance->port);
    }
}
EXPORT_SYMBOL_GPL(picolink_uart_push_data);

static int picolink_uart_remove(struct platform_device *pdev) {
    struct picolink_uart *pu = platform_get_drvdata(pdev);
    
    if (pu) {
        tty_port_tty_hangup(&pu->port, false);
        tty_unregister_device(picolink_tty_driver, 0);
        tty_port_destroy(&pu->port);
    }
    uart_instance = NULL;
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