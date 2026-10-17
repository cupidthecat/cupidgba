/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_GYRO_H
#define CUPID_GYRO_H

#include "cupid/types.h"

#define CUPID_GYRO_NEUTRAL 0x6C0

struct CupidGyro {
    uint16_t sample, shift;
    bool clock, output;
};

void cupid_gyro_init(struct CupidGyro* gyro);
void cupid_gyro_reset_bus(struct CupidGyro* gyro);
bool cupid_gyro_set_sample(struct CupidGyro* gyro, uint16_t sample);
void cupid_gyro_write_pins(struct CupidGyro* gyro, uint8_t pins);
uint8_t cupid_gyro_output_pins(const struct CupidGyro* gyro);

#endif
