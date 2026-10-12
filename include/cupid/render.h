/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_RENDER_H
#define CUPID_RENDER_H

#include "cupid/types.h"

/* Color arithmetic used after the two visible layers have been selected. */
static inline uint32_t cupid_brighten(uint32_t color, int evy) {
    unsigned c = 0;
    unsigned a;
    a = color & 0x1F;
    c |= (a + ((0x1F - a) * evy) / 16) & 0x1F;
    a = color & 0x3E0;
    c |= (a + ((0x3E0 - a) * evy) / 16) & 0x3E0;
    a = color & 0x7C00;
    c |= (a + ((0x7C00 - a) * evy) / 16) & 0x7C00;
    return c;
}

/** Attenuate packed BGR555 fields, then truncate them to their bit positions. */
static inline uint32_t cupid_darken(uint32_t color, int evy) {
    unsigned c = 0;
    unsigned a;
    a = color & 0x1F;
    c |= (a - (a * evy) / 16) & 0x1F;
    a = color & 0x3E0;
    c |= (a - (a * evy) / 16) & 0x3E0;
    a = color & 0x7C00;
    c |= (a - (a * evy) / 16) & 0x7C00;
    return c;
}

/**
 * Alpha blend two 15-bit colors: out = (a * eva + b * evb) / 16, clamped.
 */
static inline uint32_t cupid_color_mix(int eva, uint32_t colorA, int evb, uint32_t colorB) {
    unsigned r = ((colorA & 0x1F) * eva + (colorB & 0x1F) * evb) >> 4;
    unsigned g = (((colorA >> 5) & 0x1F) * eva + ((colorB >> 5) & 0x1F) * evb) >> 4;
    unsigned b = (((colorA >> 10) & 0x1F) * eva + ((colorB >> 10) & 0x1F) * evb) >> 4;
    if (r > 31) r = 31;
    if (g > 31) g = 31;
    if (b > 31) b = 31;
    return r | (g << 5) | (b << 10);
}

#endif /* CUPID_RENDER_H */
