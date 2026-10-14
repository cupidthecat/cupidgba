/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include <errno.h>
#include <inttypes.h>
#include <sys/stat.h>

bool cupid_load_bios(struct CupidGBA* gba, const char* path) {
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
        fprintf(stderr, "CUPID: Invalid BIOS input: %s (%" PRIdMAX " bytes); expected a regular %d-byte file\n",
            path, (intmax_t)info.st_size, GBA_SIZE_BIOS);
        errno = EINVAL;
        return false;
    }

    FILE* file = fopen(path, "rb");
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

    uint8_t* installed = gba->memory.bios;
    if (!installed) {
        installed = malloc(sizeof(candidate));
        if (!installed) return false;
    }
    memcpy(installed, candidate, sizeof(candidate));
    gba->memory.bios = installed;
    if (gba->memory.activeRegion == GBA_REGION_BIOS) {
        gba->cpu.memory.activeRegion = (const uint32_t*)installed;
    }
    gba->memory.fullBios = 1;
    gba->memory.biosLoaded = true;
    gba->hasBios = true;
    gba->biosHLE = false;
    printf("CUPID: Loaded external BIOS: %s (%zu bytes)\n", path, count);
    return true;
}
