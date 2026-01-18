#ifndef UART_HANDLER_H
#define UART_HANDLER_H

#include "protocol.h"

// Обработчик команд UART (конфигурация и передача данных)
void picolink_uart_handle(usb_packet_t *pkt);

// Обработчик прерывания приема данных (если выносим в заголовок)
void on_uart_rx(void);

#endif