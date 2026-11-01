/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_PLATFORM_SAVE_H
#define CUPID_PLATFORM_SAVE_H

#include "cupid/memory.h"

struct CupidPlatformSave {
    enum CupidSaveType type;
    enum CupidFlashProfile profile;
};

/* Parse a complete name without changing the selection on failure. */
bool cupid_platform_save_parse(const char *name, struct CupidPlatformSave *selection);

/* Apply a validated startup override after ROM detection and before battery loading.
 * Automatic selection leaves the detected hardware and its capacity intact. */
bool cupid_platform_save_configure(struct CupidSavedata *save,
                                   const struct CupidPlatformSave *selection);

#endif
