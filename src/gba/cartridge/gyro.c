/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gyro.h"

void cupid_gyro_reset_bus(struct CupidGyro* gyro) {
    gyro->shift = 0;
    gyro->clock = gyro->output = false;
}

void cupid_gyro_init(struct CupidGyro* gyro) {
    gyro->sample = CUPID_GYRO_NEUTRAL;
    cupid_gyro_reset_bus(gyro);
}

bool cupid_gyro_set_sample(struct CupidGyro* gyro, uint16_t sample) {
    if (sample > 0xFFF) return false;
    gyro->sample = sample;
    return true;
}

void cupid_gyro_write_pins(struct CupidGyro* gyro, uint8_t pins) {
    const bool clock = (pins & 2) != 0;
    if (pins & 1) {
        /* Conversion start captures four zero prefix bits and twelve data bits.
         * The first bit is available before the first falling serial edge. */
        gyro->shift = (uint16_t)(gyro->sample << 1);
        gyro->output = false;
    } else if (gyro->clock && !clock) {
        gyro->output = (gyro->shift & 0x8000) != 0;
        gyro->shift = (uint16_t)(gyro->shift << 1);
    }
    gyro->clock = clock;
}

uint8_t cupid_gyro_output_pins(const struct CupidGyro* gyro) {
    return gyro->output ? 4 : 0;
}
