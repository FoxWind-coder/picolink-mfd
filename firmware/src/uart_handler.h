#ifndef UART_HANDLER_H
#define UART_HANDLER_H

#include "protocol.h"

void picolink_uart_handle(usb_packet_t *pkt);

//uart irq handler
void on_uart_rx(void);

#endif