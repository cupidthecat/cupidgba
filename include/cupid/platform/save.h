/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      save.h
 * @author    cupidthecat
 * @brief     Frontend backup-type selection and configuration interface.
 * @date      2026-11-16
 *
 * - Represent explicit save choices separately from automatic detection.
 * - Declare argument parsing and transactional backup configuration.
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
