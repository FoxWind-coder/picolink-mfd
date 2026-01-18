#ifndef I2C_HANDLER_H
#define I2C_HANDLER_H

#include "protocol.h"

// Handle I2C commands arriving from the queue
void picolink_i2c_handle(usb_packet_t *pkt);

#endif