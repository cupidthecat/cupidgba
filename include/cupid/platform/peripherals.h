/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      peripherals.h
 * @author    cupidthecat
 * @brief     Frontend cartridge-peripheral configuration and sample parsing.
 * @date      2026-11-16
 *
 * - Keep device-presence choices separate from external sample values.
 * - Declare strict argument parsing and core configuration helpers.
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
#ifndef CUPID_PLATFORM_PERIPHERALS_H
#define CUPID_PLATFORM_PERIPHERALS_H
#include "cupid/card-reader.h"
#include "cupid/gyro.h"
#include "cupid/tilt.h"
#include "cupid/types.h"
struct CupidGBA;

struct CupidPlatformPeripherals {
    int solarPresence, rumblePresence; /* -1: cartridge detection, 0: off, 1: on */
    uint8_t solarLevel;
    int gyroPresence;
    uint16_t gyroSample;
    int tiltPresence;
    uint16_t tiltX, tiltY;
    int cardReaderPresence;
};

#define CUPID_PLATFORM_PERIPHERALS_DEFAULT                                                         \
    {-1, -1, 128, -1, CUPID_GYRO_NEUTRAL, -1, CUPID_TILT_NEUTRAL, CUPID_TILT_NEUTRAL, -1}

bool cupid_platform_peripheral_mode(const char *text, int *presence);
bool cupid_platform_solar_level(const char *text, uint8_t *level);
bool cupid_platform_gyro_sample(const char *text, uint16_t *sample);
bool cupid_platform_sample12(const char *text, uint16_t *sample);
void cupid_platform_peripherals_configure(struct CupidGBA *gba,
                                          const struct CupidPlatformPeripherals *config);
/* Input must be initialized. Failed validation preserves its existing queue. */
bool cupid_platform_cards_read(struct CupidCardReader *input, const char *const paths[],
                               unsigned count);
/* Transfer an already validated queue to the selected board without allocation. */
bool cupid_platform_cards_attach(struct CupidGBA *gba, struct CupidCardReader *input);
#endif
