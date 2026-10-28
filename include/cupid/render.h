/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_RENDER_H
#define CUPID_RENDER_H

#include "cupid/types.h"

/* Color arithmetic follows layer selection. Red/blue use five bits; green
 * retains bit 15 as an extra low bit until the final BGR555 result. */
static inline unsigned cupid_color_factor(int factor) {
    return factor > 16 ? 16u : (unsigned)factor;
}

static inline unsigned cupid_color_component(uint32_t color, unsigned channel) {
    unsigned value = (color >> (channel * 5)) & 31;
    return channel == 1 ? value * 2 + ((color >> 15) & 1) : value;
}

static inline uint32_t cupid_color_pack_component(unsigned value, unsigned channel) {
    if (channel == 1) value /= 2;
    return value << (channel * 5);
}

static inline uint32_t cupid_brighten(uint32_t color, int evy) {
    const unsigned factor = cupid_color_factor(evy);
    uint32_t result = 0;
    for (unsigned channel = 0; channel < 3; channel++) {
        unsigned value = cupid_color_component(color, channel);
        const unsigned maximum = channel == 1 ? 63 : 31;
        value = (value * (16 - factor) + maximum * factor + 8) / 16;
        result |= cupid_color_pack_component(value, channel);
    }
    return result;
}

static inline uint32_t cupid_darken(uint32_t color, int evy) {
    const unsigned factor = cupid_color_factor(evy);
    uint32_t result = 0;
    for (unsigned channel = 0; channel < 3; channel++) {
        unsigned value = cupid_color_component(color, channel);
        value = (value * (16 - factor) + 8) / 16;
        result |= cupid_color_pack_component(value, channel);
    }
    return result;
}

/* Round the combined intensity before saturation and green's final reduction. */
static inline uint32_t cupid_color_mix(int eva, uint32_t colorA, int evb, uint32_t colorB) {
    const unsigned first = cupid_color_factor(eva), second = cupid_color_factor(evb);
    uint32_t result = 0;
    for (unsigned channel = 0; channel < 3; channel++) {
        unsigned value = (cupid_color_component(colorA, channel) * first
            + cupid_color_component(colorB, channel) * second + 8) / 16;
        const unsigned maximum = channel == 1 ? 63 : 31;
        if (value > maximum) value = maximum;
        result |= cupid_color_pack_component(value, channel);
    }
    return result;
}

#endif /* CUPID_RENDER_H */
