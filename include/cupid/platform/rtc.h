/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      rtc.h
 * @author    cupidthecat
 * @brief     Frontend RTC seed, host-clock and battery-file configuration.
 * @date      2026-11-16
 *
 * - Retain explicit seeds, battery paths and sampled host-clock anchors.
 * - Declare clock loading and persistence at frontend boundaries.
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
#ifndef CUPID_PLATFORM_RTC_H
#define CUPID_PLATFORM_RTC_H
#include "cupid/rtc.h"
struct CupidGBA;

struct CupidPlatformRTC {
    const char *path;
    const struct CupidRTCDateTime *seed;
    uint64_t hostAnchor;
    bool offlineWall;
};

bool cupid_platform_rtc_host_clock(struct CupidRTCDateTime *date, uint64_t *stamp);

/* The frontend samples host time explicitly. Core execution remains clocked
 * by emulated cycles; a saved host anchor can account for powered-off time. */
bool cupid_platform_rtc_load(struct CupidGBA *gba, struct CupidPlatformRTC *config,
                             const struct CupidRTCDateTime *hostDate, uint64_t hostStamp);
bool cupid_platform_rtc_save(struct CupidGBA *gba, struct CupidPlatformRTC *config,
                             uint64_t hostStamp);
#endif
