/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      local.h
 * @author    cupidthecat
 * @brief     Local-session frame capture and host-device interfaces.
 * @date      2026-11-16
 *
 * - Group endpoint presentation and device state for linked play.
 * - Declare session execution, rendering and save-path helpers.
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
#ifndef CUPID_LOCAL_H
#define CUPID_LOCAL_H
#include "cupid/platform.h"
#include "cupid/platform/key-script.h"
#include "cupid/platform/peripherals.h"
#include "cupid/platform/rtc.h"

struct CupidLocalDevices {
    int rtcPresence;
    struct CupidPlatformRTC rtc;
    struct CupidPlatformPeripherals peripherals;
    const char *const *cardPaths;
    unsigned cardCount;
    struct CupidKeyReplay *keyReplay;
    const char *keyScriptPath;
};

int cupid_platform_run_local(const char *const paths[], unsigned count, const char *bios,
                             const char *saveType, int scale, unsigned frames,
                             const struct CupidLocalDevices *devices);
bool cupid_platform_local_save_path(const char *rom, unsigned player, char *out, size_t capacity);

struct CupidLocalFrames {
    uint32_t *pixels[4];
    struct CupidGBA *replayCore;
    struct CupidKeyReplay *keyReplay;
};

void cupid_platform_capture_local_frame(void *context, unsigned player, const uint32_t *pixels,
                                        uint64_t timestamp);
bool cupid_platform_render_local(struct CupidPlatform *platform, SDL_Texture *const textures[],
                                 const uint32_t *const frames[], unsigned count, unsigned focus);
#endif
