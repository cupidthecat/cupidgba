/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      solar.h
 * @author    cupidthecat
 * @brief     Solar converter counter and GPIO pin interface.
 * @date      2026-11-16
 *
 * - Keep the external light level separate from bus-reset state.
 * - Declare input updates, counter clocks and output-pin sampling.
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
#ifndef CUPID_SOLAR_H
#define CUPID_SOLAR_H

#include "cupid/types.h"

struct CupidSolar {
    uint16_t counter;
    uint8_t threshold;
    bool clockHigh;
};

void cupid_solar_init(struct CupidSolar *solar);
void cupid_solar_reset_bus(struct CupidSolar *solar);
void cupid_solar_set_level(struct CupidSolar *solar, uint8_t level);
void cupid_solar_write_pins(struct CupidSolar *solar, uint8_t pins);
uint8_t cupid_solar_output_pins(const struct CupidSolar *solar);

#endif
