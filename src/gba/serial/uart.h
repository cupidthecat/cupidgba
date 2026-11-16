/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      uart.h
 * @author    cupidthecat
 * @brief     Internal UART register and clocked-pin interface.
 * @date      2026-11-16
 *
 * - Declare control, data and receive-ready helpers.
 * - Share UART event handling with serial mode selection.
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
#ifndef CUPID_SERIAL_UART_INTERNAL_H
#define CUPID_SERIAL_UART_INTERNAL_H

#include "cupid/serial.h"

void cupid_serial_refresh_pins(struct CupidSerial *serial);
void cupid_serial_uart_init(struct CupidSerial *serial);
void cupid_serial_uart_reset(struct CupidSerial *serial);
void cupid_serial_uart_control(struct CupidSerial *serial, enum CupidSerialMode before,
                               uint16_t previous);
void cupid_serial_uart_input(struct CupidSerial *serial, uint8_t previous, uint8_t current);
void cupid_serial_uart_resume(struct CupidSerial *serial);
uint16_t cupid_serial_uart_status(struct CupidSerial *serial);
uint8_t cupid_serial_uart_read(struct CupidSerial *serial);
void cupid_serial_uart_write(struct CupidSerial *serial, uint8_t value);
bool cupid_serial_uart_ready(const struct CupidSerial *serial);

#endif
