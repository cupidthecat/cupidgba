/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      paths.c
 * @author    cupidthecat
 * @brief     Reject output collisions while retaining ordinary save and RTC writeback.
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
#include "cupid/platform/paths.h"
#include "cupid/file.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static bool writes(enum CupidPathRole role) {
    return role >= CUPID_PATH_SAVE_OUTPUT;
}

static bool writeback(enum CupidPathRole input, enum CupidPathRole output) {
    return (input == CUPID_PATH_SAVE_INPUT && output == CUPID_PATH_SAVE_OUTPUT) ||
           (input == CUPID_PATH_RTC_INPUT && output == CUPID_PATH_RTC_OUTPUT);
}

bool cupid_platform_paths_validate(const struct CupidPlatformPath *paths, size_t count) {
    if (!paths && count) {
        errno = EINVAL;
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if ((unsigned)paths[i].role > CUPID_PATH_OUTPUT) {
            errno = EINVAL;
            return false;
        }
        if (!paths[i].path)
            continue;
        if (writes(paths[i].role) && !cupid_file_is_regular(paths[i].path) && errno != ENOENT) {
            fprintf(stderr, "Cannot use output file %s: %s\n", paths[i].path, strerror(errno));
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (!paths[j].path || (!writes(paths[i].role) && !writes(paths[j].role)) ||
                writeback(paths[i].role, paths[j].role) || writeback(paths[j].role, paths[i].role))
                continue;
            const int same = cupid_file_compare_paths(paths[i].path, paths[j].path);
            if (same < 0) {
                fprintf(stderr, "Cannot verify file paths %s and %s: %s\n", paths[i].path,
                        paths[j].path, strerror(errno));
                return false;
            }
            if (same) {
                fprintf(stderr, "Output path overlaps another input or output: %s and %s\n",
                        paths[i].path, paths[j].path);
                errno = EINVAL;
                return false;
            }
        }
    }
    return true;
}
