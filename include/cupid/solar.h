/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file solar.h
 * @brief Solar converter counter and GPIO pin interface.
 *
 * - Keep the external light level separate from bus-reset state.
 * - Declare input updates, counter clocks and output-pin sampling.
 */
#ifndef CUPID_SOLAR_H
#define CUPID_SOLAR_H

#include "cupid/types.h"

struct CupidSolar {
    uint16_t counter;
    uint8_t threshold;
    bool clockHigh;
};

void cupid_solar_init(struct CupidSolar *solar);
void cupid_solar_reset_bus(struct CupidSolar *solar);
void cupid_solar_set_level(struct CupidSolar *solar, uint8_t level);
void cupid_solar_write_pins(struct CupidSolar *solar, uint8_t pins);
uint8_t cupid_solar_output_pins(const struct CupidSolar *solar);

#endif
