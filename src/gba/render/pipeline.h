/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      pipeline.h
 * @author    cupidthecat
 * @brief     Shared pixel-pipeline state accessors and layer helpers.
 * @date      2026-11-16
 *
 * - Decode display control and forced-blank state for rendering stages.
 * - Combine submitted layers through shared pipeline interfaces.
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
#ifndef CUPID_PIXEL_PIPELINE_H
#define CUPID_PIXEL_PIPELINE_H

#include "cupid/gba.h"

#define FETCH_DIRECT 0x10000u
void cupid_pixel_begin_line(struct CupidVideo *video, unsigned initialCycle);
void cupid_pixel_latch(struct CupidVideo *video);
void cupid_pixel_background(struct CupidVideo *video, unsigned cycle, uint64_t time);
void cupid_pixel_merge(struct CupidVideo *video, unsigned cycle, uint64_t time);
void cupid_pixel_object(struct CupidVideo *video, uint64_t time);
void cupid_pixel_object_begin(struct CupidVideo *video, uint64_t time);
uint16_t cupid_pixel_read_background(struct CupidVideo *video, uint32_t address, uint64_t time);

static inline uint16_t cupid_pixel_display(const struct CupidVideo *video) {
    return video->dispcnt & video->pipeline.displayLatch[0];
}

static inline bool cupid_pixel_blank(const struct CupidVideo *video) {
    return (video->dispcnt | video->pipeline.displayLatch[0]) & 0x80;
}

static inline int32_t cupid_pixel_add(int32_t value, int32_t step) {
    /* Affine accumulators wrap in their signed 28-bit register domain. */
    uint32_t raw = ((uint32_t)value + (uint32_t)step) & 0x0FFFFFFF;
    return (int32_t)(raw ^ 0x08000000u) - 0x08000000;
}
#endif
