/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      rumble.c
 * @author    cupidthecat
 * @brief     SDL controller rumble driven by cartridge motor output.
 * @date      2026-11-16
 *
 * - Renew short host effects while the emulated motor is enabled.
 * - Stop host output on motor disable or frontend shutdown.
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
#include "cupid/platform.h"

/* A short host effect is renewed while the emulated motor remains enabled.
 * If the frontend stalls, the controller effect expires after 250 ms. */
void cupid_platform_rumble_refresh(struct CupidPlatform *plat) {
    if (!plat->controller || !plat->rumbleActive)
        return;
    const uint32_t now = SDL_GetTicks();
    if (plat->rumbleDriving && !SDL_TICKS_PASSED(now, plat->rumbleRefresh))
        return;
    SDL_GameControllerRumble(plat->controller, 0x8000, 0x8000, 250);
    plat->rumbleDriving = true;
    plat->rumbleRefresh = now + 100;
}

void cupid_platform_rumble_stop(struct CupidPlatform *plat) {
    if (plat->controller)
        SDL_GameControllerRumble(plat->controller, 0, 0, 0);
    plat->rumbleDriving = false;
}

void cupid_platform_rumble_changed(void *context, bool active, uint64_t clock) {
    (void)clock;
    struct CupidPlatform *plat = context;
    plat->rumbleActive = active;
    if (active)
        cupid_platform_rumble_refresh(plat);
    else
        cupid_platform_rumble_stop(plat);
}
