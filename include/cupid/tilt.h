/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_TILT_H
#define CUPID_TILT_H

#include "cupid/types.h"

#define CUPID_TILT_NEUTRAL 0x3A0

struct CupidTilt {
    uint16_t inputX, inputY, x, y;
    bool armed;
};

void cupid_tilt_init(struct CupidTilt* tilt);
void cupid_tilt_reset(struct CupidTilt* tilt);
bool cupid_tilt_set_sample(struct CupidTilt* tilt, uint16_t x, uint16_t y);
bool cupid_tilt_read(const struct CupidTilt* tilt, uint32_t address, uint8_t* value);
bool cupid_tilt_write(struct CupidTilt* tilt, uint32_t address, uint8_t value);

#endif
