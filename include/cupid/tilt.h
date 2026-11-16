/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      tilt.h
 * @author    cupidthecat
 * @brief     Tilt sensor input and command-latched axis interface.
 * @date      2026-11-16
 *
 * - Retain live axis input separately from captured readings.
 * - Declare cartridge-port reads, commands and reset handling.
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
#ifndef CUPID_TILT_H
#define CUPID_TILT_H

#include "cupid/types.h"

#define CUPID_TILT_NEUTRAL 0x3A0

struct CupidTilt {
    uint16_t inputX, inputY, x, y;
    bool armed;
};

void cupid_tilt_init(struct CupidTilt *tilt);
void cupid_tilt_reset(struct CupidTilt *tilt);
bool cupid_tilt_set_sample(struct CupidTilt *tilt, uint16_t x, uint16_t y);
bool cupid_tilt_read(const struct CupidTilt *tilt, uint32_t address, uint8_t *value);
bool cupid_tilt_write(struct CupidTilt *tilt, uint32_t address, uint8_t value);

#endif
