/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      file.c
 * @author    cupidthecat
 * @brief     Validated external BIOS loading and live firmware installation.
 * @date      2026-11-16
 *
 * - Read a complete regular BIOS file before changing core ownership.
 * - Rebind an active BIOS opcode view after successful installation.
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
#include "cupid/gba.h"
#include <errno.h>
#include <inttypes.h>
#include <sys/stat.h>

bool cupid_load_bios(struct CupidGBA *gba, const char *path) {
#ifdef _WIN32
    struct _stat64 info;
    int inspected = _stat64(path, &info);
    bool regular = inspected == 0 && (info.st_mode & _S_IFMT) == _S_IFREG;
#else
    struct stat info;
    int inspected = stat(path, &info);
    bool regular = inspected == 0 && S_ISREG(info.st_mode);
#endif
    if (inspected != 0) {
        fprintf(stderr, "CUPID: Failed to inspect BIOS: %s: %s\n", path, strerror(errno));
        return false;
    }
    if (!regular || info.st_size != GBA_SIZE_BIOS) {
        fprintf(stderr,
                "CUPID: Invalid BIOS input: %s (%" PRIdMAX
                " bytes); expected a regular %d-byte file\n",
                path, (intmax_t)info.st_size, GBA_SIZE_BIOS);
        errno = EINVAL;
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "CUPID: Failed to open BIOS: %s: %s\n", path, strerror(errno));
        return false;
    }
    /* The current firmware and mode stay intact until the complete input is read. */
    uint8_t candidate[GBA_SIZE_BIOS];
    size_t count = fread(candidate, 1, sizeof(candidate), file);
    int extra = fgetc(file);
    bool readFailed = ferror(file) != 0;
    int closeResult = fclose(file);
    if (count != sizeof(candidate) || extra != EOF || readFailed || closeResult != 0) {
        fprintf(stderr, "CUPID: Failed to read complete BIOS: %s\n", path);
        return false;
    }

    uint8_t *installed = gba->memory.bios;
    if (!installed) {
        installed = malloc(sizeof(candidate));
        if (!installed)
            return false;
    }
    memcpy(installed, candidate, sizeof(candidate));
    gba->memory.bios = installed;
    if (gba->memory.activeRegion == GBA_REGION_BIOS) {
        gba->cpu.memory.activeRegion = (const uint32_t *)installed;
    }
    gba->memory.fullBios = 1;
    gba->memory.biosLoaded = true;
    gba->hasBios = true;
    gba->biosHLE = false;
    printf("CUPID: Loaded external BIOS: %s (%zu bytes)\n", path, count);
    return true;
}
