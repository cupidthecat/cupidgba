/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_PLATFORM_PERIPHERALS_H
#define CUPID_PLATFORM_PERIPHERALS_H
#include "cupid/types.h"
#include "cupid/gyro.h"
#include "cupid/tilt.h"
struct CupidGBA;

struct CupidPlatformPeripherals {
    int solarPresence, rumblePresence; /* -1: cartridge detection, 0: off, 1: on */
    uint8_t solarLevel;
    int gyroPresence;
    uint16_t gyroSample;
    int tiltPresence;
    uint16_t tiltX, tiltY;
};

#define CUPID_PLATFORM_PERIPHERALS_DEFAULT \
    { -1, -1, 128, -1, CUPID_GYRO_NEUTRAL, -1, CUPID_TILT_NEUTRAL, CUPID_TILT_NEUTRAL }

bool cupid_platform_peripheral_mode(const char* text, int* presence);
bool cupid_platform_solar_level(const char* text, uint8_t* level);
bool cupid_platform_gyro_sample(const char* text, uint16_t* sample);
bool cupid_platform_sample12(const char* text, uint16_t* sample);
void cupid_platform_peripherals_configure(struct CupidGBA* gba,
    const struct CupidPlatformPeripherals* config);
#endif
