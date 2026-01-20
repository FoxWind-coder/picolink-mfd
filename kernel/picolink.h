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
    struct miscdevice miscdev; // For /dev/picolink
    uint8_t bulk_out_endpointAddr;
    uint8_t bulk_in_endpointAddr;
    struct urb *read_urb;
    void *bulk_in_buffer;
    usb_packet_t i2c_resp;     // Buffer for the response
    struct completion i2c_done; // Signal that the response has been received
    bool disconnected;
};

struct picolink_uart {
    struct picolink_dev *mfd;
    struct tty_port port;
    struct device *dev;
    uint8_t tx_pin;
    uint8_t rx_pin;
};

int picolink_send_packet(struct usb_device *udev, uint8_t endpoint, void *data, int len);
// Prototype added to prevent warnings in core.c
int picolink_transfer(struct picolink_dev *dev, usb_packet_t *tx_pkt, usb_packet_t *rx_pkt);
#endif