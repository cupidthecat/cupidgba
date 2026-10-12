/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/**
 * @file savedata.c
 * @brief Save data types: SRAM, SRAM512, Flash 512/1M, EEPROM 512B/8K.
 */

#include "cupid/gba.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Initialize, reset, and deinitialize savedata state.
 */

void cupid_savedata_init(struct CupidSavedata* savedata) {
    memset(savedata, 0, sizeof(*savedata));
    savedata->type = SAVE_AUTODETECT;
    savedata->command = FLASH_COMMAND_NONE;
    savedata->flashState = FLASH_STATE_RAW;
    savedata->flashBank = 0;
    savedata->currentBank = NULL;
    savedata->readBitsRemaining = 0;
    savedata->readAddress = 0;
    savedata->writeAddress = 0;
    savedata->settling = 0;
    savedata->settling_active = false;
}

void cupid_savedata_reset(struct CupidSavedata* savedata) {
    savedata->command = FLASH_COMMAND_NONE;
    savedata->flashState = FLASH_STATE_RAW;
    savedata->readBitsRemaining = 0;
    savedata->settling_active = false;
    if (savedata->data && (savedata->type == SAVE_FLASH512 ||
                           savedata->type == SAVE_FLASH1M)) {
        savedata->flashBank = 0;
        savedata->currentBank = savedata->data;
    }
}

void cupid_savedata_deinit(struct CupidSavedata* savedata) {
    free(savedata->data);
    savedata->data = NULL;
    savedata->currentBank = NULL;
    savedata->type = SAVE_AUTODETECT;
    savedata->size = 0;
}

/**
 * @brief Force a concrete savedata type.
 */

void cupid_savedata_force_type(struct CupidSavedata* savedata, enum CupidSaveType type) {
    if (savedata->type == type) {
        return;
    }
    if (savedata->type != SAVE_AUTODETECT) {
        cupid_savedata_deinit(savedata);
        cupid_savedata_init(savedata);
    }
    switch (type) {
    case SAVE_FLASH512:
    case SAVE_FLASH1M:
        savedata->type = type;
        cupid_savedata_init_flash(savedata);
        return;
    case SAVE_EEPROM:
    case SAVE_EEPROM512:
        savedata->type = type;
        cupid_savedata_init_eeprom(savedata);
        return;
    case SAVE_SRAM:
        cupid_savedata_init_sram(savedata);
        return;
    case SAVE_SRAM512:
        cupid_savedata_init_sram512(savedata);
        return;
    case SAVE_NONE:
        savedata->type = SAVE_NONE;
        break;
    case SAVE_AUTODETECT:
    default:
        break;
    }
}

/**
 * @brief Type-specific savedata initialization helpers.
 */

void cupid_savedata_init_sram(struct CupidSavedata* savedata) {
    if (savedata->data && savedata->type == SAVE_SRAM) return;
    savedata->type = SAVE_SRAM;
    savedata->size = GBA_SIZE_SRAM;
    if (!savedata->data) {
        savedata->data = malloc(GBA_SIZE_SRAM);
    }
    memset(savedata->data, 0xFF, GBA_SIZE_SRAM);
    savedata->currentBank = savedata->data;
    printf("CUPID: Savedata initialized as SRAM (32 KiB)\n");
}

void cupid_savedata_init_sram512(struct CupidSavedata* savedata) {
    if (savedata->data && savedata->type == SAVE_SRAM512) return;
    savedata->type = SAVE_SRAM512;
    savedata->size = GBA_SIZE_FLASH512;  /** 64 KiB */
    if (!savedata->data) {
        savedata->data = malloc(GBA_SIZE_FLASH512);
    }
    memset(savedata->data, 0xFF, GBA_SIZE_FLASH512);
    savedata->currentBank = savedata->data;
    printf("CUPID: Savedata initialized as SRAM512 (64 KiB)\n");
}

void cupid_savedata_init_flash(struct CupidSavedata* savedata) {
    size_t size = (savedata->type == SAVE_FLASH1M) ? GBA_SIZE_FLASH1M : GBA_SIZE_FLASH512;
    if (savedata->data && savedata->size == size) {
        /** Already initialized at the right size. */
        savedata->currentBank = savedata->data;
        return;
    }
    if (savedata->type != SAVE_FLASH512 && savedata->type != SAVE_FLASH1M) {
        savedata->type = SAVE_FLASH512;
        size = GBA_SIZE_FLASH512;
    }
    savedata->size = size;
    free(savedata->data);
    savedata->data = malloc(size);
    memset(savedata->data, 0xFF, size);
    savedata->flashBank = 0;
    savedata->currentBank = savedata->data;
    savedata->command = FLASH_COMMAND_NONE;
    savedata->flashState = FLASH_STATE_RAW;
    printf("CUPID: Savedata initialized as Flash %s (%zu KiB)\n",
           savedata->type == SAVE_FLASH1M ? "1M" : "512", size / 1024);
}

void cupid_savedata_init_eeprom(struct CupidSavedata* savedata) {
    size_t size = (savedata->type == SAVE_EEPROM) ? GBA_SIZE_EEPROM : GBA_SIZE_EEPROM512;
    if (savedata->data && savedata->size == size) return;
    savedata->size = size;
    free(savedata->data);
    savedata->data = malloc(size);
    memset(savedata->data, 0xFF, size);
    savedata->command = EEPROM_COMMAND_NULL;
    savedata->readBitsRemaining = 0;
    printf("CUPID: Savedata initialized as EEPROM %s\n",
           savedata->type == SAVE_EEPROM ? "8K" : "512B");
}

/**
 * @brief Flash helpers.
 */

static void _flashSwitchBank(struct CupidSavedata* savedata, int bank) {
    if (bank > 0 && savedata->type == SAVE_FLASH512) {
        /** Auto-upgrade 512K to 1M on bank switch. */
        printf("CUPID: Upgrading flash from 512K to 1M\n");
        savedata->type = SAVE_FLASH1M;
        uint8_t* newdata = malloc(GBA_SIZE_FLASH1M);
        memcpy(newdata, savedata->data, GBA_SIZE_FLASH512);
        memset(newdata + GBA_SIZE_FLASH512, 0xFF, GBA_SIZE_FLASH512);
        free(savedata->data);
        savedata->data = newdata;
        savedata->size = GBA_SIZE_FLASH1M;
    }
    savedata->flashBank = bank;
    savedata->currentBank = &savedata->data[bank << 16];
}

static void _flashErase(struct CupidSavedata* savedata) {
    size_t size = (savedata->type == SAVE_FLASH1M) ? GBA_SIZE_FLASH1M : GBA_SIZE_FLASH512;
    savedata->dirty = true;
    memset(savedata->data, 0xFF, size);
}

static void _flashEraseSector(struct CupidSavedata* savedata, uint16_t sectorStart) {
    savedata->dirty = true;
    memset(&savedata->currentBank[sectorStart & ~0xFFFu], 0xFF, 0x1000);
}

/**
 * @brief Read a byte from flash save memory.
 */

uint8_t cupid_savedata_read_flash(struct CupidSavedata* savedata, uint16_t address) {
    if (savedata->command == FLASH_COMMAND_ID) {
        if (savedata->type == SAVE_FLASH512) {
            if (address < 2) {
                return (uint8_t)(FLASH_PANASONIC_512K >> (address * 8));
            }
        } else if (savedata->type == SAVE_FLASH1M) {
            if (address < 2) {
                return (uint8_t)(FLASH_SANYO_1M >> (address * 8));
            }
        }
    }
    return savedata->currentBank[address];
}

/**
 * @brief Flash write state machine.
 */

void cupid_savedata_write_flash(struct CupidSavedata* savedata, uint16_t address, uint8_t value) {
    switch (savedata->flashState) {
    case FLASH_STATE_RAW:
        switch (savedata->command) {
        case FLASH_COMMAND_PROGRAM:
            savedata->dirty = true;
            savedata->currentBank[address] = value;
            savedata->command = FLASH_COMMAND_NONE;
            break;
        case FLASH_COMMAND_SWITCH_BANK:
            if (address == 0 && value < 2) {
                _flashSwitchBank(savedata, value);
            }
            savedata->command = FLASH_COMMAND_NONE;
            break;
        default:
            if (address == FLASH_BASE_HI && value == FLASH_COMMAND_START) {
                savedata->flashState = FLASH_STATE_START;
            }
            break;
        }
        break;

    case FLASH_STATE_START:
        if (address == FLASH_BASE_LO && value == FLASH_COMMAND_CONTINUE) {
            savedata->flashState = FLASH_STATE_CONTINUE;
        } else {
            savedata->flashState = FLASH_STATE_RAW;
        }
        break;

    case FLASH_STATE_CONTINUE:
        savedata->flashState = FLASH_STATE_RAW;
        if (address == FLASH_BASE_HI) {
            switch (savedata->command) {
            case FLASH_COMMAND_NONE:
                switch (value) {
                case FLASH_COMMAND_ERASE:
                case FLASH_COMMAND_ID:
                case FLASH_COMMAND_PROGRAM:
                case FLASH_COMMAND_SWITCH_BANK:
                    savedata->command = value;
                    break;
                default:
                    break;
                }
                break;
            case FLASH_COMMAND_ERASE:
                if (value == FLASH_COMMAND_ERASE_CHIP) {
                    _flashErase(savedata);
                }
                savedata->command = FLASH_COMMAND_NONE;
                break;
            case FLASH_COMMAND_ID:
                if (value == FLASH_COMMAND_TERMINATE) {
                    savedata->command = FLASH_COMMAND_NONE;
                }
                break;
            default:
                savedata->command = FLASH_COMMAND_NONE;
                break;
            }
        } else if (savedata->command == FLASH_COMMAND_ERASE) {
            if (value == FLASH_COMMAND_ERASE_SECTOR) {
                _flashEraseSector(savedata, address);
                savedata->command = FLASH_COMMAND_NONE;
            }
        }
        break;
    }
}

/**
 * @brief EEPROM helpers.
 */

/**
 * @brief Upgrade EEPROM512 to EEPROM when address exceeds 512B range.
 */
static void _ensureEeprom(struct CupidSavedata* savedata, uint32_t size) {
    if (size < GBA_SIZE_EEPROM512) return;
    if (savedata->type == SAVE_EEPROM) return;

    printf("CUPID: Upgrading EEPROM from 512B to 8K\n");
    savedata->type = SAVE_EEPROM;
    uint8_t* newdata = malloc(GBA_SIZE_EEPROM);
    memcpy(newdata, savedata->data, GBA_SIZE_EEPROM512);
    memset(newdata + GBA_SIZE_EEPROM512, 0xFF, GBA_SIZE_EEPROM - GBA_SIZE_EEPROM512);
    free(savedata->data);
    savedata->data = newdata;
    savedata->size = GBA_SIZE_EEPROM;
}

/**
 * @brief Write one EEPROM bit through the command state machine.
 *
 * writeSize is the number of bits remaining in the DMA transfer.
 */

void cupid_savedata_write_eeprom(struct CupidSavedata* savedata, uint16_t value, uint32_t writeSize) {
    switch (savedata->command) {
    /** Read header, bit 0/1 of the 2-bit command prefix. */
    case EEPROM_COMMAND_NULL:
    default:
        savedata->command = (enum CupidSavedataCommand)(value & 0x1);
        break;
    case EEPROM_COMMAND_PENDING:
        savedata->command = (enum CupidSavedataCommand)(((int)savedata->command << 1) | (value & 0x1));
        if (savedata->command == EEPROM_COMMAND_WRITE) {
            savedata->writeAddress = 0;
        } else {
            savedata->readAddress = 0;
        }
        break;

    /** Write command: address bits, then 64 data bits, then stop bit. */
    case EEPROM_COMMAND_WRITE:
        if (writeSize > 65) {
            /** Still receiving address bits. */
            savedata->writeAddress <<= 1;
            savedata->writeAddress |= (value & 0x1) << 6;
        } else if (writeSize == 1) {
            /** Stop bit, end of write. */
            savedata->command = EEPROM_COMMAND_NULL;
        } else if ((savedata->writeAddress >> 3) < GBA_SIZE_EEPROM) {
            _ensureEeprom(savedata, savedata->writeAddress >> 3);
            uint8_t current = savedata->data[savedata->writeAddress >> 3];
            current &= ~(1 << (0x7 - (savedata->writeAddress & 0x7)));
            current |= (value & 0x1) << (0x7 - (savedata->writeAddress & 0x7));
            savedata->dirty = true;
            savedata->data[savedata->writeAddress >> 3] = current;
            ++savedata->writeAddress;
        }
        break;

    /** Read command: receiving address bits until stop bit. */
    case EEPROM_COMMAND_READ_PENDING:
        if (writeSize > 1) {
            savedata->readAddress <<= 1;
            if (value & 0x1) {
                savedata->readAddress |= 0x40;
            }
        } else {
            /** Stop bit, switch to read mode. */
            savedata->readBitsRemaining = 68;
            savedata->command = EEPROM_COMMAND_READ;
        }
        break;

    case EEPROM_COMMAND_READ:
        /** Should not be writing during read; ignore. */
        break;
    }
}

/**
 * @brief Read one EEPROM bit from the command state machine.
 */

uint16_t cupid_savedata_read_eeprom(struct CupidSavedata* savedata) {
    if (savedata->command != EEPROM_COMMAND_READ) {
        /** Not in read mode, return 1 (ready bit). */
        return 1;
    }
    --savedata->readBitsRemaining;
    if (savedata->readBitsRemaining < 64) {
        int step = 63 - savedata->readBitsRemaining;
        uint32_t address = (savedata->readAddress + step) >> 3;
        _ensureEeprom(savedata, address);
        if (address >= savedata->size) {
            return 0xFF;
        }
        uint8_t data = savedata->data[address] >> (0x7 - (step & 0x7));
        if (!savedata->readBitsRemaining) {
            savedata->command = EEPROM_COMMAND_NULL;
        }
        return data & 0x1;
    }
    return 0;
}

/**
 * @brief File-backed save persistence.
 */

bool cupid_savedata_load_file(struct CupidSavedata* savedata, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;  /** No save file yet, that is fine. */

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize <= 0) {
        fclose(f);
        return false;
    }

    /** If save type is unknown, guess it from file size. */
    if (savedata->type == SAVE_AUTODETECT) {
        if      ((size_t)fileSize == GBA_SIZE_EEPROM512) {
            savedata->type = SAVE_EEPROM512;
            cupid_savedata_init_eeprom(savedata);
        } else if ((size_t)fileSize == GBA_SIZE_EEPROM) {
            savedata->type = SAVE_EEPROM;
            cupid_savedata_init_eeprom(savedata);
        } else if ((size_t)fileSize == GBA_SIZE_SRAM) {
            cupid_savedata_init_sram(savedata);
        } else if ((size_t)fileSize == GBA_SIZE_FLASH512) {
            /** Could be SRAM512 or FLASH512; default to FLASH512. */
            savedata->type = SAVE_FLASH512;
            cupid_savedata_init_flash(savedata);
        } else if ((size_t)fileSize == GBA_SIZE_FLASH1M) {
            savedata->type = SAVE_FLASH1M;
            cupid_savedata_init_flash(savedata);
        } else {
            fprintf(stderr, "CUPID: Unrecognised .sav size %ld, ignoring\n", fileSize);
            fclose(f);
            return false;
        }
    }

    /** Ensure the internal buffer is allocated before copying into it. */
    if (!savedata->data || !savedata->size) {
        fclose(f);
        return false;
    }

    size_t toRead = (size_t)fileSize < savedata->size ? (size_t)fileSize : savedata->size;
    size_t nread  = fread(savedata->data, 1, toRead, f);
    fclose(f);

    savedata->dirty = false;  /** Freshly loaded, not dirty. */
    printf("CUPID: Loaded save: %s (%zu bytes)\n", path, nread);
    return nread == toRead;
}

bool cupid_savedata_save_file(struct CupidSavedata* savedata, const char* path) {
    if (!savedata->data || !savedata->size) return true;  /** Nothing to save. */
    if (!savedata->dirty)                   return true;  /** No changes. */

    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "CUPID: Failed to open save file for writing: %s: %s\n",
                path, strerror(errno));
        return false;
    }

    size_t nwritten = fwrite(savedata->data, 1, savedata->size, f);
    fclose(f);

    if (nwritten != savedata->size) {
        fprintf(stderr, "CUPID: Save write short: %zu / %zu\n", nwritten, savedata->size);
        return false;
    }

    savedata->dirty = false;
    printf("CUPID: Saved: %s (%zu bytes)\n", path, nwritten);
    return true;
}

/**
 * @brief Auto-detect save type from ROM identifier strings.
 */

void cupid_savedata_detect_type(struct CupidSavedata* savedata, const uint8_t* rom, size_t romSize) {
    if (!rom || romSize < 0xC0) return;
    if (savedata->type != SAVE_AUTODETECT) return;

    /** Scan ROM for save-type identifier strings. */
    for (size_t i = 0; i + 8 <= romSize; i += 4) {
        const char* p = (const char*)&rom[i];

        if (memcmp(p, "EEPROM_V", 8) == 0) {
            fprintf(stderr, "CUPID: ROM string detected: EEPROM\n");
            savedata->type = SAVE_EEPROM512;
            cupid_savedata_init_eeprom(savedata);
            return;
        }
        if (memcmp(p, "SRAM_V", 6) == 0 || memcmp(p, "SRAM_F_V", 8) == 0) {
            fprintf(stderr, "CUPID: ROM string detected: SRAM\n");
            cupid_savedata_init_sram(savedata);
            return;
        }
        if (i + 12 <= romSize && memcmp(p, "FLASH1M_V", 9) == 0) {
            fprintf(stderr, "CUPID: ROM string detected: Flash 1M (at 0x%zX)\n", i);
            savedata->type = SAVE_FLASH1M;
            cupid_savedata_init_flash(savedata);
            return;
        }
        if (i + 10 <= romSize &&
            (memcmp(p, "FLASH_V", 7) == 0 || memcmp(p, "FLASH512_V", 10) == 0)) {
            fprintf(stderr, "CUPID: ROM string detected: Flash 512\n");
            savedata->type = SAVE_FLASH512;
            cupid_savedata_init_flash(savedata);
            return;
        }
    }
    /** No match; leave as autodetect and resolve on first access. */
}
