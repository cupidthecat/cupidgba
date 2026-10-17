/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/platform.h"

/* A short host effect is renewed while the emulated motor remains enabled.
 * If the frontend stalls, the controller effect expires after 250 ms. */
void cupid_platform_rumble_refresh(struct CupidPlatform* plat) {
    if (!plat->controller || !plat->rumbleActive) return;
    const uint32_t now = SDL_GetTicks();
    if (plat->rumbleDriving && !SDL_TICKS_PASSED(now, plat->rumbleRefresh)) return;
    SDL_GameControllerRumble(plat->controller, 0x8000, 0x8000, 250);
    plat->rumbleDriving = true;
    plat->rumbleRefresh = now + 100;
}

void cupid_platform_rumble_stop(struct CupidPlatform* plat) {
    if (plat->controller) SDL_GameControllerRumble(plat->controller, 0, 0, 0);
    plat->rumbleDriving = false;
}

void cupid_platform_rumble_changed(void* context, bool active, uint64_t clock) {
    (void)clock;
    struct CupidPlatform* plat = context;
    plat->rumbleActive = active;
    if (active) cupid_platform_rumble_refresh(plat);
    else cupid_platform_rumble_stop(plat);
}
