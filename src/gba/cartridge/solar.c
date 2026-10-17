/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/solar.h"

void cupid_solar_init(struct CupidSolar* solar) {
    solar->threshold = 255;
    cupid_solar_reset_bus(solar);
}

void cupid_solar_reset_bus(struct CupidSolar* solar) {
    solar->counter = 0;
    solar->clockHigh = false;
}

void cupid_solar_set_level(struct CupidSolar* solar, uint8_t level) {
    solar->threshold = (uint8_t)(255 - level);
}

void cupid_solar_write_pins(struct CupidSolar* solar, uint8_t pins) {
    const bool clockHigh = (pins & 1) != 0;
    if (pins & 2) solar->counter = 0;
    else if (solar->clockHigh && !clockHigh) solar->counter = (solar->counter + 1) & 0xFFF;
    solar->clockHigh = clockHigh;
}

uint8_t cupid_solar_output_pins(const struct CupidSolar* solar) {
    /* The low eight counter outputs drive the DAC; the comparator is live. */
    return (solar->counter & 255) > solar->threshold ? 8 : 0;
}
