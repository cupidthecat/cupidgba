/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file image.c
 * @brief ROM loading, cartridge replacement and image ownership.
 *
 * - Prepare image storage and board configuration before installation.
 * - Transfer ownership and refresh memory views after a successful load.
 */
#include "../save/detection.h"
#include "cupid/file.h"
#include "cupid/gba.h"
#include <errno.h>

/**
 * @brief Round up to next power of two.
 */
static uint32_t cupid_to_pow2(uint32_t v) {
    if (!v)
        return 0;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

/**
 * @brief Load a ROM image into memory.
 */
bool cupid_load_rom(struct CupidGBA *gba, const char *path) {
    /* A suspended CPU or DMA may retain pointers and sampled cartridge data.
     * Finish or reset that request before replacing its storage. */
    if (gba->cpuInstructionPending || gba->memory.dmaRunning || gba->memory.busBusy ||
        gba->memory.busLocked) {
        errno = EBUSY;
        return false;
    }
    if (!cupid_file_is_regular(path))
        return false;
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "CUPID: Failed to open ROM: %s: %s\n", path, strerror(errno));
        return false;
    }

    /* Read a candidate before changing the currently attached cartridge. */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    long fileSize = ftell(f);

    if (fileSize <= 0 || (size_t)fileSize > GBA_SIZE_PAGED_ROM) {
        fprintf(stderr, "CUPID: Invalid ROM size: %ld\n", fileSize);
        fclose(f);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }

    size_t pristineSize = (size_t)fileSize;
    const bool paged = pristineSize > GBA_SIZE_ROM;
    if (paged) {
        uint8_t header[0xC0];
        if (fread(header, 1, sizeof(header), f) != sizeof(header) || header[0xAC] != 'M' ||
            fseek(f, 0, SEEK_SET) != 0) {
            fclose(f);
            return false;
        }
    }
    uint8_t *candidate = calloc(1, GBA_SIZE_ROM);
    if (!candidate) {
        fclose(f);
        return false;
    }
    struct CupidPagedROM page = {0};
    if (paged) {
        page.image = malloc(pristineSize);
        if (!page.image) {
            free(candidate);
            fclose(f);
            return false;
        }
        page.imageSize = pristineSize;
    }
    uint8_t *image = paged ? page.image : candidate;
    const size_t readSize = fread(image, 1, pristineSize, f);
    const int trailingByte = fgetc(f);
    const bool readFailed = ferror(f) != 0 || trailingByte != EOF;
    const int closeResult = fclose(f);
    /* Identify the bytes actually read, not only the earlier header probe. */
    if (readSize != pristineSize || readFailed || closeResult != 0 ||
        (paged && image[0xAC] != 'M')) {
        fprintf(stderr, "CUPID: Failed ROM read: %zu / %zu\n", readSize, pristineSize);
        free(page.image);
        free(candidate);
        return false;
    }

    /* An explicit device configured before the first image remains supported.
     * Every replacement owns a new device, even when the image is unchanged. */
    const bool freshSave = gba->memory.rom != NULL || gba->memory.savedata.type == SAVE_AUTODETECT;
    struct CupidSavedata prepared;
    if (freshSave && !cupid_savedata_prepare_image(&prepared, image, pristineSize)) {
        free(page.image);
        free(candidate);
        return false;
    }
    if (freshSave && !gba->memory.rom) {
        /* Initial caller-supplied programming durations configure this image.
         * They must not become a default for a later cartridge replacement. */
        prepared.eepromProgramClocks = gba->memory.savedata.eepromProgramClocks;
        prepared.flashProgramClocks = gba->memory.savedata.flashProgramClocks;
        prepared.flashSectorEraseClocks = gba->memory.savedata.flashSectorEraseClocks;
        prepared.flashChipEraseClocks = gba->memory.savedata.flashChipEraseClocks;
    }
    size_t mappedSize = paged ? GBA_SIZE_PAGED_VIEW : pristineSize;
    if (paged)
        cupid_paged_rom_reset(&page, candidate);

    /** 1 MiB ROMs (Classic NES series) appear as 4x mirrored. */
    if (pristineSize == 0x00100000) {
        mappedSize = 0x00400000;
        /** Mirror 4x: copy the 1 MiB image three more times. */
        memcpy(candidate + 0x100000, candidate, 0x100000);
        memcpy(candidate + 0x200000, candidate, 0x100000);
        memcpy(candidate + 0x300000, candidate, 0x100000);
    }

    /* Notify the old board's output owner before publishing new cartridge
     * state. No fallible operation follows this commit point. */
    cupid_cartridge_attach(gba, image, pristineSize);
    if (freshSave) {
        cupid_savedata_deinit(&gba->memory.savedata);
        gba->memory.savedata = prepared;
        /* The prepared event has not been scheduled. Relocate its context
         * before binding this new device to the console scheduler. */
        gba->memory.savedata.readyEvent.context = &gba->memory.savedata;
        cupid_savedata_bind_timing(&gba->memory.savedata, &gba->timing);
    }
    free(gba->memory.pagedROM.image);
    gba->memory.pagedROM = page;
    free(gba->memory.rom);
    gba->memory.rom = candidate;
    gba->memory.romSize = mappedSize;
    gba->memory.romImageSize = pristineSize;

    /** Compute romMask as next power-of-two minus one. */
    gba->memory.romMask = cupid_to_pow2((uint32_t)gba->memory.romSize) - 1;

    /**
     * If ROM size is not a power of 2, treat as full 32 MiB to match
     * flash-cart-like behavior.
     */
    if (__builtin_popcount((unsigned)gba->memory.romSize) != 1) {
        gba->memory.romSize = GBA_SIZE_ROM;
        gba->memory.romMask = GBA_SIZE_ROM - 1;
    }

    /**
     * Extract ROM header info for identification.
     * GBA header: title at 0xA0 (12 bytes), game code at 0xAC (4 bytes).
     */
    gba->memory.romID = 0;
    char title[13] = {0};
    char gameCode[5] = {0};
    if (pristineSize >= 0xB0) {
        memcpy(title, gba->memory.rom + 0xA0, 12);
        memcpy(gameCode, gba->memory.rom + 0xAC, 4);
        /** Store 2-byte ROM ID from game code (bytes 0xAC-0xAD). */
        gba->memory.romID = (uint16_t)(gba->memory.rom[0xAC] | (gba->memory.rom[0xAD] << 8));
    }

    printf("CUPID: Loaded ROM: %s (%zu bytes, mask=0x%08X)\n", path, pristineSize,
           gba->memory.romMask);
    printf("CUPID: Title: \"%.12s\"  Code: \"%.4s\"\n", title, gameCode);

    /** If CPU is already initialized and PC is in ROM, re-arm active region. */
    if (gba->memory.activeRegion >= GBA_REGION_ROM0) {
        /* The same-region shortcut still refers to the previous allocation. */
        gba->memory.activeRegion = -1;
        gba->cpu.memory.setActiveRegion(&gba->cpu, gba->cpu.gprs[15]);
    }

    /** Detect save type from ROM strings. */
    cupid_savedata_detect_type(&gba->memory.savedata, image, pristineSize);

    return true;
}
