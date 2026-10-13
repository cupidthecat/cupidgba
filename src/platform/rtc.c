/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/platform/rtc.h"
#include <errno.h>

bool cupid_platform_rtc_load(struct CupidGBA* gba, struct CupidPlatformRTC* config,
                             const struct CupidRTCDateTime* hostDate, uint64_t hostStamp) {
    if (!gba->cartridge.rtcPresent) return true;
    if (config->seed) {
        struct CupidRTC validation = {0};
        if (!cupid_rtc_set_datetime(&validation, config->seed)) { errno = EINVAL; return false; }
    }
    uint64_t previous = 0;
    bool loaded = cupid_cartridge_load_rtc(gba, config->path, &previous);
    if (!loaded && errno != ENOENT) return false;
    if (config->seed) {
        if (!cupid_rtc_set_datetime(&gba->cartridge.rtc, config->seed)) { errno = EINVAL; return false; }
    } else if (config->offlineWall) {
        if (loaded) {
            if (previous && hostStamp > previous)
                cupid_rtc_advance_seconds(&gba->cartridge.rtc, hostStamp - previous);
        } else if (!hostDate || !cupid_rtc_set_datetime(&gba->cartridge.rtc, hostDate)) {
            errno = EINVAL; return false;
        }
    }
    config->hostAnchor = config->offlineWall ? (previous > hostStamp ? previous : hostStamp) : 0;
    errno = 0;
    return true;
}

bool cupid_platform_rtc_save(struct CupidGBA* gba, struct CupidPlatformRTC* config, uint64_t hostStamp) {
    if (!gba->cartridge.rtcPresent) return true;
    if (config->offlineWall) {
        if (hostStamp < config->hostAnchor) hostStamp = config->hostAnchor;
    } else hostStamp = 0;
    if (!cupid_cartridge_save_rtc(gba, config->path, hostStamp)) return false;
    config->hostAnchor = hostStamp;
    return true;
}
