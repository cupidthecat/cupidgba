/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_FILE_H
#define CUPID_FILE_H
#include "cupid/types.h"

/* Stage and flush complete bytes before replacing a writable regular file.
 * On failure, preserve the existing destination and report errno. */
bool cupid_file_replace(const char* path, const void* data, size_t size);
bool cupid_file_read_exact(const char* path, void* data, size_t size);
#endif
