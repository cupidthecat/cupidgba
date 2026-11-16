/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      paths.h
 * @author    cupidthecat
 * @brief     Input and output ownership for runner and frontend file paths.
 * @date      2026-11-16
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
#ifndef CUPID_PLATFORM_PATHS_H
#define CUPID_PLATFORM_PATHS_H
#include "cupid/types.h"

enum CupidPathRole {
    CUPID_PATH_INPUT,
    CUPID_PATH_SAVE_INPUT,
    CUPID_PATH_RTC_INPUT,
    CUPID_PATH_SAVE_OUTPUT,
    CUPID_PATH_RTC_OUTPUT,
    CUPID_PATH_OUTPUT
};

struct CupidPlatformPath {
    const char *path;
    enum CupidPathRole role;
};

/* Read-only inputs may share a file. Save/RTC input may share only its matching
 * output; all other output overlaps fail before file creation. NULL paths are
 * ignored. This preflight does not lock names against external replacement. */
bool cupid_platform_paths_validate(const struct CupidPlatformPath *paths, size_t count);
#endif
