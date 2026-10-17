/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_SERIAL_UART_INTERNAL_H
#define CUPID_SERIAL_UART_INTERNAL_H

#include "cupid/serial.h"

void cupid_serial_refresh_pins(struct CupidSerial* serial);
void cupid_serial_uart_init(struct CupidSerial* serial);
void cupid_serial_uart_reset(struct CupidSerial* serial);
void cupid_serial_uart_control(struct CupidSerial* serial, enum CupidSerialMode before, uint16_t previous);
void cupid_serial_uart_input(struct CupidSerial* serial, uint8_t previous, uint8_t current);
void cupid_serial_uart_resume(struct CupidSerial* serial);
uint16_t cupid_serial_uart_status(struct CupidSerial* serial);
uint8_t cupid_serial_uart_read(struct CupidSerial* serial);
void cupid_serial_uart_write(struct CupidSerial* serial, uint8_t value);
bool cupid_serial_uart_ready(const struct CupidSerial* serial);

#endif
