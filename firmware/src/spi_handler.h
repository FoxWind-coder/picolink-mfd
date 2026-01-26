#ifndef SPI_HANDLER_H
#define SPI_HANDLER_H

#include "protocol.h"

void picolink_spi_handle(usb_packet_t *pkt);
void picolink_spi_disable(void);

#endif