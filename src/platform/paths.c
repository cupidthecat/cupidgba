/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file paths.c
 * @brief Reject output collisions while retaining ordinary save and RTC writeback.
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
