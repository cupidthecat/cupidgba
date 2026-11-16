/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      detection.c
 * @author    cupidthecat
 * @brief     Backup signature detection and fresh image-state preparation.
 * @date      2026-11-16
 *
 * - Identify supported backup families from cartridge image contents.
 * - Prepare backup ownership before a new cartridge is installed.
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
#include "detection.h"
#include <errno.h>

static enum CupidSaveType identify(const uint8_t *rom, size_t size, size_t *offset) {
    static const struct {
        const char *marker;
        size_t length;
        enum CupidSaveType type;
    } markers[] = {{"EEPROM_V", 8, SAVE_EEPROM512}, {"SRAM_V", 6, SAVE_SRAM},
                   {"SRAM_F_V", 8, SAVE_SRAM},      {"FLASH1M_V", 9, SAVE_FLASH1M},
                   {"FLASH_V", 7, SAVE_FLASH512},   {"FLASH512_V", 10, SAVE_FLASH512}};

    if (!rom || size < 0xC0)
        return SAVE_AUTODETECT;
    /* These scanner boards contain their calibration and saved applications
     * in flash; an unrelated library signature must not select EEPROM/SRAM. */
    if (!memcmp(rom + 0xAC, "PEAJ", 4)) {
        *offset = 0xAC;
        return SAVE_FLASH512;
    }
    if (!memcmp(rom + 0xAC, "PSAJ", 4) || !memcmp(rom + 0xAC, "PSAE", 4)) {
        *offset = 0xAC;
        return SAVE_FLASH1M;
    }
    for (size_t i = 0; i < size; i += 4) {
        if (rom[i] != 'E' && rom[i] != 'S' && rom[i] != 'F')
            continue;
        for (unsigned m = 0; m < sizeof(markers) / sizeof(*markers); ++m) {
            if (markers[m].length <= size - i &&
                !memcmp(rom + i, markers[m].marker, markers[m].length)) {
                *offset = i;
                return markers[m].type;
            }
        }
    }
    return SAVE_AUTODETECT;
}

bool cupid_savedata_prepare_image(struct CupidSavedata *save, const uint8_t *rom, size_t size) {
    cupid_savedata_init(save);
    size_t offset = 0;
    save->type = identify(rom, size, &offset);
    switch (save->type) {
        case SAVE_SRAM:
            save->size = GBA_SIZE_SRAM;
            break;
        case SAVE_FLASH512:
            save->size = GBA_SIZE_FLASH512;
            break;
        case SAVE_FLASH1M:
            save->size = GBA_SIZE_FLASH1M;
            break;
        case SAVE_EEPROM512:
            save->size = GBA_SIZE_EEPROM512;
            break;
        default:
            return true;
    }
    save->data = malloc(save->size);
    if (!save->data) {
        errno = ENOMEM;
        return false;
    }
    memset(save->data, 0xFF, save->size);
    if (save->type != SAVE_EEPROM512)
        save->currentBank = save->data;
    return true;
}

bool cupid_savedata_detect_type(struct CupidSavedata *save, const uint8_t *rom, size_t size) {
    if (save->type != SAVE_AUTODETECT || save->flashState == FLASH_STATE_ALLOCATION)
        return true;
    size_t offset = 0;
    const enum CupidSaveType type = identify(rom, size, &offset);
    if (type == SAVE_AUTODETECT)
        return true;
    if (!cupid_savedata_force_type(save, type))
        return false;
    save->typeForced = false;
    switch (type) {
        case SAVE_EEPROM512:
            fprintf(stderr, "CUPID: ROM string detected: EEPROM\n");
            break;
        case SAVE_SRAM:
            fprintf(stderr, "CUPID: ROM string detected: SRAM\n");
            break;
        case SAVE_FLASH1M:
            fprintf(stderr, "CUPID: ROM string detected: Flash 1M (at 0x%zX)\n", offset);
            break;
        case SAVE_FLASH512:
            fprintf(stderr, "CUPID: ROM string detected: Flash 512\n");
            break;
        default:
            break;
    }
    return true;
}
