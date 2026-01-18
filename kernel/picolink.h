//picolink.h
#ifndef PICOLINK_H
#define PICOLINK_H

#include <linux/usb.h>
#include <linux/mfd/core.h>
#include <linux/miscdevice.h>
#include <linux/tty.h>
#include <linux/completion.h>
#include "../firmware/src/protocol.h"

struct picolink_dev {
    struct usb_device *udev;
    struct usb_interface *interface;
    struct miscdevice miscdev; // Для /dev/picolink
    // struct mfd_cell cells[1]; 
    uint8_t bulk_out_endpointAddr;
    uint8_t bulk_in_endpointAddr;     // Добавить
    struct urb *read_urb;             // Добавить
    void *bulk_in_buffer;             // Добавить
    usb_packet_t i2c_resp; // Буфер для ответа
    struct completion i2c_done;   // Сигнал о получении ответа
};

struct picolink_uart {
    struct picolink_dev *mfd;
    struct tty_port port;
    struct device *dev;
    uint8_t tx_pin;
    uint8_t rx_pin;
};

int picolink_send_packet(struct usb_device *udev, uint8_t endpoint, void *data, int len);
// Добавляем прототип здесь, чтобы убрать warning в core.c
int picolink_transfer(struct picolink_dev *dev, usb_packet_t *tx_pkt, usb_packet_t *rx_pkt);
#endif