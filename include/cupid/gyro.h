/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      gyro.h
 * @author    cupidthecat
 * @brief     Gyroscope converter state and GPIO pin interface.
 * @date      2026-11-16
 *
 * - Retain external input separately from the serial conversion snapshot.
 * - Expose converter reset, sample selection and pin transitions.
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
#ifndef CUPID_GYRO_H
#define CUPID_GYRO_H

#include "cupid/types.h"

#define CUPID_GYRO_NEUTRAL 0x6C0

struct CupidGyro {
    uint16_t sample, shift;
    bool clock, output;
};

void cupid_gyro_init(struct CupidGyro *gyro);
void cupid_gyro_reset_bus(struct CupidGyro *gyro);
bool cupid_gyro_set_sample(struct CupidGyro *gyro, uint16_t sample);
void cupid_gyro_write_pins(struct CupidGyro *gyro, uint8_t pins);
uint8_t cupid_gyro_output_pins(const struct CupidGyro *gyro);

#endif
