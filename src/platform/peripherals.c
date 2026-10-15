/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/platform/peripherals.h"

bool cupid_platform_peripheral_mode(const char* text, int* presence) {
    if (!strcmp(text, "auto")) *presence = -1;
    else if (!strcmp(text, "off")) *presence = 0;
    else if (!strcmp(text, "on")) *presence = 1;
    else return false;
    return true;
}

bool cupid_platform_solar_level(const char* text, uint8_t* level) {
    if (!*text) return false;
    unsigned value = 0;
    for (const char* c = text; *c; c++) {
        if (*c < '0' || *c > '9') return false;
        value = value * 10 + (unsigned)(*c - '0');
        if (value > 255) return false;
    }
    *level = (uint8_t)value;
    return true;
}

bool cupid_platform_gyro_sample(const char* text, uint16_t* sample) {
    if (!*text) return false;
    unsigned value = 0;
    for (const char* c = text; *c; c++) {
        if (*c < '0' || *c > '9') return false;
        value = value * 10 + (unsigned)(*c - '0');
        if (value > 4095) return false;
    }
    *sample = (uint16_t)value;
    return true;
}

void cupid_platform_peripherals_configure(struct CupidGBA* gba,
    const struct CupidPlatformPeripherals* config) {
    if (config->solarPresence >= 0) cupid_cartridge_enable_solar(gba, config->solarPresence != 0);
    if (config->rumblePresence >= 0) cupid_cartridge_enable_rumble(gba, config->rumblePresence != 0);
    if (config->gyroPresence >= 0) cupid_cartridge_enable_gyro(gba, config->gyroPresence != 0);
    cupid_cartridge_set_solar_level(gba, config->solarLevel);
    cupid_cartridge_set_gyro_sample(gba, config->gyroSample);
}
