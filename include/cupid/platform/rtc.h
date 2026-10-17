/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_PLATFORM_RTC_H
#define CUPID_PLATFORM_RTC_H
#include "cupid/rtc.h"
struct CupidGBA;

struct CupidPlatformRTC {
    const char* path;
    const struct CupidRTCDateTime* seed;
    uint64_t hostAnchor;
    bool offlineWall;
};

bool cupid_platform_rtc_host_clock(struct CupidRTCDateTime* date, uint64_t* stamp);

/* The frontend samples host time explicitly. Core execution remains clocked
 * by emulated cycles; a saved host anchor can account for powered-off time. */
bool cupid_platform_rtc_load(struct CupidGBA* gba, struct CupidPlatformRTC* config,
                             const struct CupidRTCDateTime* hostDate, uint64_t hostStamp);
bool cupid_platform_rtc_save(struct CupidGBA* gba, struct CupidPlatformRTC* config, uint64_t hostStamp);
#endif
