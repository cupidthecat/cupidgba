/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file paths.h
 * @brief Input and output ownership for runner and frontend file paths.
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
