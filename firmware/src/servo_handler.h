#ifndef SERVO_HANDLER_H
#define SERVO_HANDLER_H

#include "protocol.h"

void picolink_servo_handle(usb_packet_t *pkt);
void picolink_servo_disable(void);

#endif