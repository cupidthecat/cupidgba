/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      file.h
 * @author    cupidthecat
 * @brief     Regular-file reads, path identity and staged replacement interface.
 * @date      2026-11-16
 *
 * - Validate regular-file inputs before reading complete payloads.
 * - Replace output files through an exclusively created staging file.
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
#ifndef CUPID_FILE_H
#define CUPID_FILE_H
#include "cupid/types.h"

/* Stage and flush complete bytes before replacing a writable regular file.
 * On failure, preserve the existing destination and report errno. */
bool cupid_file_replace(const char *path, const void *data, size_t size);
/* Reject directories and device/pipe inputs without opening a blocking stream. */
bool cupid_file_is_regular(const char *path);
bool cupid_file_read_exact(const char *path, void *data, size_t size);
/* Compare regular-file identities or normalized names in existing directories.
 * Return 1 for the same path, 0 for distinct paths, or -1 with errno on error.
 * A missing final file is allowed; a dangling link or missing parent is not. */
int cupid_file_compare_paths(const char *first, const char *second);
#endif
