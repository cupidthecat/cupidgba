/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      tilt.c
 * @author    cupidthecat
 * @brief     Tilt sensor command decoding and captured axis reads.
 * @date      2026-11-16
 *
 * - Latch configured axis input through the cartridge command sequence.
 * - Return the selected bytes without replacing live external input.
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
#include "cupid/tilt.h"

void cupid_tilt_reset(struct CupidTilt *tilt) {
    tilt->x = tilt->y = 0xFFF;
    tilt->armed = false;
}

void cupid_tilt_init(struct CupidTilt *tilt) {
    tilt->inputX = tilt->inputY = CUPID_TILT_NEUTRAL;
    cupid_tilt_reset(tilt);
}

bool cupid_tilt_set_sample(struct CupidTilt *tilt, uint16_t x, uint16_t y) {
    if (x > 0xFFF || y > 0xFFF)
        return false;
    tilt->inputX = x;
    tilt->inputY = y;
    return true;
}

bool cupid_tilt_read(const struct CupidTilt *tilt, uint32_t address, uint8_t *value) {
    switch (address & 0xFFFFFF) {
        case 0x8200:
            *value = (uint8_t)tilt->x;
            break;
        /* Functional snapshots are ready. Physical busy/PHI timing is unqualified. */
        case 0x8300:
            *value = (uint8_t)(0x80 | (tilt->x >> 8));
            break;
        case 0x8400:
            *value = (uint8_t)tilt->y;
            break;
        case 0x8500:
            *value = (uint8_t)(tilt->y >> 8);
            break;
        default:
            return false;
    }
    return true;
}

bool cupid_tilt_write(struct CupidTilt *tilt, uint32_t address, uint8_t value) {
    switch (address & 0xFFFFFF) {
        case 0x8000:
            if (value == 0x55)
                tilt->armed = true;
            break;
        case 0x8100:
            if (value == 0xAA && tilt->armed) {
                tilt->x = tilt->inputX;
                tilt->y = tilt->inputY;
                tilt->armed = false;
            }
            break;
        default:
            return false;
    }
    return true;
}
