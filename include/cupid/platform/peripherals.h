/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_PLATFORM_PERIPHERALS_H
#define CUPID_PLATFORM_PERIPHERALS_H
#include "cupid/types.h"
struct CupidGBA;

struct CupidPlatformPeripherals {
    int solarPresence, rumblePresence; /* -1: cartridge detection, 0: off, 1: on */
    uint8_t solarLevel;
};

bool cupid_platform_peripheral_mode(const char* text, int* presence);
bool cupid_platform_solar_level(const char* text, uint8_t* level);
void cupid_platform_peripherals_configure(struct CupidGBA* gba,
    const struct CupidPlatformPeripherals* config);
#endif
