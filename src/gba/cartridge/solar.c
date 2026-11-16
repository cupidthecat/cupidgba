/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      solar.c
 * @author    cupidthecat
 * @brief     Solar sensor counter and GPIO conversion.
 * @date      2026-11-16
 *
 * - Advance the sensor counter on accepted clock edges.
 * - Compare the counter against the configured light-level sample.
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
#include "cupid/solar.h"

void cupid_solar_init(struct CupidSolar *solar) {
    solar->threshold = 255;
    cupid_solar_reset_bus(solar);
}

void cupid_solar_reset_bus(struct CupidSolar *solar) {
    solar->counter = 0;
    solar->clockHigh = false;
}

void cupid_solar_set_level(struct CupidSolar *solar, uint8_t level) {
    solar->threshold = (uint8_t)(255 - level);
}

void cupid_solar_write_pins(struct CupidSolar *solar, uint8_t pins) {
    const bool clockHigh = (pins & 1) != 0;
    if (pins & 2)
        solar->counter = 0;
    else if (solar->clockHigh && !clockHigh)
        solar->counter = (solar->counter + 1) & 0xFFF;
    solar->clockHigh = clockHigh;
}

uint8_t cupid_solar_output_pins(const struct CupidSolar *solar) {
    /* The low eight counter outputs drive the DAC; the comparator is live. */
    return (solar->counter & 255) > solar->threshold ? 8 : 0;
}
