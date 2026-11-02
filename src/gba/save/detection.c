/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file detection.c
 * @brief Backup signature detection and fresh image-state preparation.
 *
 * - Identify supported backup families from cartridge image contents.
 * - Prepare backup ownership before a new cartridge is installed.
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

void cupid_savedata_detect_type(struct CupidSavedata *save, const uint8_t *rom, size_t size) {
    if (save->type != SAVE_AUTODETECT)
        return;
    size_t offset = 0;
    switch (identify(rom, size, &offset)) {
        case SAVE_EEPROM512:
            fprintf(stderr, "CUPID: ROM string detected: EEPROM\n");
            save->type = SAVE_EEPROM512;
            cupid_savedata_init_eeprom(save);
            break;
        case SAVE_SRAM:
            fprintf(stderr, "CUPID: ROM string detected: SRAM\n");
            cupid_savedata_init_sram(save);
            break;
        case SAVE_FLASH1M:
            fprintf(stderr, "CUPID: ROM string detected: Flash 1M (at 0x%zX)\n", offset);
            save->type = SAVE_FLASH1M;
            cupid_savedata_init_flash(save);
            break;
        case SAVE_FLASH512:
            fprintf(stderr, "CUPID: ROM string detected: Flash 512\n");
            save->type = SAVE_FLASH512;
            cupid_savedata_init_flash(save);
            break;
        default:
            break;
    }
}
