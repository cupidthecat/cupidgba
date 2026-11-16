/**
 * @file      savedata.c
 * @author    cupidthecat
 * @brief     Save data types: SRAM, SRAM512, Flash 512/1M, EEPROM 512B/8K.
 * @date      2026-11-16
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Initialize, reset, and deinitialize savedata state.
 */
void cupid_savedata_init(struct CupidSavedata *savedata) {
    memset(savedata, 0, sizeof(*savedata));
    savedata->type = SAVE_AUTODETECT;
    savedata->command = FLASH_COMMAND_NONE;
    savedata->flashState = FLASH_STATE_RAW;
    savedata->flashBank = 0;
    savedata->currentBank = NULL;
    savedata->readBitsRemaining = 0;
    savedata->readAddress = 0;
    savedata->writeAddress = 0;
    cupid_savedata_programming_init(savedata);
}

void cupid_savedata_reset(struct CupidSavedata *savedata) {
    savedata->command = FLASH_COMMAND_NONE;
    savedata->flashState = FLASH_STATE_RAW;
    savedata->readBitsRemaining = 0;
    savedata->eepromAddressBits = 0;
    savedata->eepromWriteBits = 0;
    if (savedata->data && (savedata->type == SAVE_FLASH512 || savedata->type == SAVE_FLASH1M)) {
        savedata->flashBank = 0;
        savedata->currentBank = savedata->data;
    }
}

void cupid_savedata_deinit(struct CupidSavedata *savedata) {
    cupid_savedata_cancel_programming(savedata);
    free(savedata->data);
    savedata->data = NULL;
    savedata->currentBank = NULL;
    savedata->type = SAVE_AUTODETECT;
    savedata->size = 0;
    savedata->typeForced = false;
    savedata->flashProfile = FLASH_PROFILE_DEFAULT;
    cupid_savedata_reset(savedata);
}

/**
 * @brief Force a concrete savedata type.
 */
bool cupid_savedata_force_type(struct CupidSavedata *savedata, enum CupidSaveType type) {
    if (type < SAVE_AUTODETECT || type > SAVE_EEPROM512) {
        errno = EINVAL;
        return false;
    }
    if (savedata->type == type &&
        (savedata->data || type == SAVE_AUTODETECT || type == SAVE_NONE)) {
        if (type == SAVE_AUTODETECT)
            cupid_savedata_reset(savedata);
        savedata->typeForced = type != SAVE_AUTODETECT;
        return true;
    }

    /* Allocate the complete replacement before releasing an attached device or
     * its accepted programming event. The candidate is never scheduled. */
    struct CupidSavedata replacement;
    cupid_savedata_init(&replacement);
    if (savedata->type == SAVE_AUTODETECT) {
        replacement.localClock = savedata->localClock;
        replacement.eepromProgramClocks = savedata->eepromProgramClocks;
        replacement.flashProgramClocks = savedata->flashProgramClocks;
        replacement.flashSectorEraseClocks = savedata->flashSectorEraseClocks;
        replacement.flashChipEraseClocks = savedata->flashChipEraseClocks;
    }
    replacement.type = type;
    replacement.typeForced = type != SAVE_AUTODETECT;
    switch (type) {
        case SAVE_FLASH512:
        case SAVE_FLASH1M:
            if (!cupid_savedata_init_flash(&replacement))
                return false;
            break;
        case SAVE_EEPROM:
        case SAVE_EEPROM512:
            if (!cupid_savedata_init_eeprom(&replacement))
                return false;
            break;
        case SAVE_SRAM:
            if (!cupid_savedata_init_sram(&replacement))
                return false;
            break;
        case SAVE_SRAM512:
            if (!cupid_savedata_init_sram512(&replacement))
                return false;
            break;
        case SAVE_NONE:
        case SAVE_AUTODETECT:
        default:
            break;
    }
    struct CupidTiming *timing = savedata->timing;
    cupid_savedata_deinit(savedata);
    *savedata = replacement;
    savedata->readyEvent.context = savedata;
    cupid_savedata_bind_timing(savedata, timing);
    return true;
}

/**
 * @brief Type-specific savedata initialization helpers.
 */
static bool initialize_storage(struct CupidSavedata *savedata, enum CupidSaveType type,
                               size_t size) {
    uint8_t *data = malloc(size);
    if (!data) {
        errno = ENOMEM;
        return false;
    }
    memset(data, 0xFF, size);
    cupid_savedata_cancel_programming(savedata);
    free(savedata->data);
    savedata->data = data;
    savedata->type = type;
    savedata->size = size;
    savedata->currentBank = type == SAVE_EEPROM || type == SAVE_EEPROM512 ? NULL : data;
    savedata->flashBank = 0;
    cupid_savedata_reset(savedata);
    return true;
}

bool cupid_savedata_init_sram(struct CupidSavedata *savedata) {
    if (savedata->data && savedata->type == SAVE_SRAM && savedata->size == GBA_SIZE_SRAM)
        return true;
    if (!initialize_storage(savedata, SAVE_SRAM, GBA_SIZE_SRAM))
        return false;
    printf("CUPID: Savedata initialized as SRAM (32 KiB)\n");
    return true;
}

bool cupid_savedata_init_sram512(struct CupidSavedata *savedata) {
    if (savedata->data && savedata->type == SAVE_SRAM512 && savedata->size == GBA_SIZE_FLASH512)
        return true;
    if (!initialize_storage(savedata, SAVE_SRAM512, GBA_SIZE_FLASH512))
        return false;
    printf("CUPID: Savedata initialized as SRAM512 (64 KiB)\n");
    return true;
}

bool cupid_savedata_init_flash(struct CupidSavedata *savedata) {
    const enum CupidSaveType type = savedata->type == SAVE_FLASH1M ? SAVE_FLASH1M : SAVE_FLASH512;
    const size_t size = type == SAVE_FLASH1M ? GBA_SIZE_FLASH1M : GBA_SIZE_FLASH512;
    if (savedata->data && savedata->type == type && savedata->size == size)
        return true;
    if (!initialize_storage(savedata, type, size))
        return false;
    printf("CUPID: Savedata initialized as Flash %s (%zu KiB)\n",
           savedata->type == SAVE_FLASH1M ? "1M" : "512", size / 1024);
    return true;
}

bool cupid_savedata_init_eeprom(struct CupidSavedata *savedata) {
    const enum CupidSaveType type = savedata->type == SAVE_EEPROM ? SAVE_EEPROM : SAVE_EEPROM512;
    const size_t size = type == SAVE_EEPROM ? GBA_SIZE_EEPROM : GBA_SIZE_EEPROM512;
    if (savedata->data && savedata->type == type && savedata->size == size)
        return true;
    if (!initialize_storage(savedata, type, size))
        return false;
    printf("CUPID: Savedata initialized as EEPROM %s\n",
           savedata->type == SAVE_EEPROM ? "8K" : "512B");
    return true;
}

/**
 * @brief Flash helpers.
 */
static bool _flashSwitchBank(struct CupidSavedata *savedata, int bank) {
    if (bank > 0 && savedata->type == SAVE_FLASH512) {
        /** Auto-upgrade 512K to 1M on bank switch. */
        uint8_t *newdata = malloc(GBA_SIZE_FLASH1M);
        if (!newdata) {
            errno = ENOMEM;
            return false;
        }
        memcpy(newdata, savedata->data, GBA_SIZE_FLASH512);
        memset(newdata + GBA_SIZE_FLASH512, 0xFF, GBA_SIZE_FLASH512);
        free(savedata->data);
        savedata->data = newdata;
        savedata->type = SAVE_FLASH1M;
        savedata->size = GBA_SIZE_FLASH1M;
        printf("CUPID: Upgrading flash from 512K to 1M\n");
    }
    savedata->flashBank = bank;
    savedata->currentBank = &savedata->data[bank << 16];
    return true;
}

static void _flashErase(struct CupidSavedata *savedata) {
    size_t size = (savedata->type == SAVE_FLASH1M) ? GBA_SIZE_FLASH1M : GBA_SIZE_FLASH512;
    cupid_savedata_begin_programming(savedata, 0, (uint32_t)size, NULL,
                                     savedata->flashChipEraseClocks);
}

static void _flashEraseSector(struct CupidSavedata *savedata, uint16_t sectorStart) {
    uint32_t offset = (uint32_t)savedata->flashBank * 0x10000u + (sectorStart & ~0xFFFu);
    cupid_savedata_begin_programming(savedata, offset, 0x1000, NULL,
                                     savedata->flashSectorEraseClocks);
}

/**
 * @brief Read a byte from flash save memory.
 */
uint8_t cupid_savedata_read_flash(struct CupidSavedata *savedata, uint16_t address) {
    uint32_t offset = (uint32_t)savedata->flashBank * 0x10000u + address;
    if (savedata->programming && (savedata->flashProfile == FLASH_PROFILE_SST39VF512 ||
                                  (offset >= savedata->programOffset &&
                                   offset - savedata->programOffset < savedata->programLength))) {
        /* DQ7 complements the final data bit; DQ6 toggles on each read.
         * The success-only model keeps the DQ5 error flag clear. */
        uint8_t result = (savedata->erasing ? 0 : (savedata->programData[0] ^ 0x80)) & 0x80;
        result |= savedata->pollToggle ? 0x40 : 0;
        savedata->pollToggle = !savedata->pollToggle;
        return result;
    }
    if (savedata->command == FLASH_COMMAND_ID) {
        if (savedata->type == SAVE_FLASH512) {
            if (address < 2) {
                unsigned id = savedata->flashProfile == FLASH_PROFILE_SST39VF512
                                  ? FLASH_SST39VF512
                                  : FLASH_PANASONIC_512K;
                return (uint8_t)(id >> (address * 8));
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
bool cupid_savedata_write_flash(struct CupidSavedata *savedata, uint16_t address, uint8_t value) {
    /* Accepted self-timed operations own the chip until their ready edge. */
    if (savedata->programming)
        return true;
    /* This device accepts either ID-exit sequence. F0 remains valid payload
     * after a byte-program command and cannot abort an accepted operation. */
    if (savedata->flashProfile == FLASH_PROFILE_SST39VF512 && value == FLASH_COMMAND_TERMINATE &&
        savedata->command != FLASH_COMMAND_PROGRAM) {
        savedata->command = FLASH_COMMAND_NONE;
        savedata->flashState = FLASH_STATE_RAW;
        return true;
    }
    /* This device decodes A14..A0 during command cycles. Keep the original
     * address for program data and sector selection, where A15 selects storage. */
    const uint16_t commandAddress =
        savedata->flashProfile == FLASH_PROFILE_SST39VF512 ? address & 0x7FFF : address;
    switch (savedata->flashState) {
        case FLASH_STATE_ALLOCATION:
            /* The save bus must obtain storage before forwarding a new access. */
            break;
        case FLASH_STATE_RAW:
            switch (savedata->command) {
                case FLASH_COMMAND_PROGRAM:
                    cupid_savedata_begin_programming(
                        savedata, (uint32_t)savedata->flashBank * 0x10000u + address, 1, &value,
                        savedata->flashProgramClocks);
                    savedata->command = FLASH_COMMAND_NONE;
                    break;
                case FLASH_COMMAND_SWITCH_BANK:
                    if (address == 0 && value < 2) {
                        if (!_flashSwitchBank(savedata, value))
                            return false;
                    }
                    savedata->command = FLASH_COMMAND_NONE;
                    break;
                default:
                    if (commandAddress == FLASH_BASE_HI && value == FLASH_COMMAND_START) {
                        savedata->flashState = FLASH_STATE_START;
                    } else if (savedata->flashProfile == FLASH_PROFILE_SST39VF512 &&
                               savedata->command == FLASH_COMMAND_ERASE) {
                        /* An invalid continuation cancels the whole pending erase sequence. */
                        savedata->command = FLASH_COMMAND_NONE;
                    }
                    break;
            }
            break;

        case FLASH_STATE_START:
            if (commandAddress == FLASH_BASE_LO && value == FLASH_COMMAND_CONTINUE) {
                savedata->flashState = FLASH_STATE_CONTINUE;
            } else {
                savedata->flashState = FLASH_STATE_RAW;
                if (savedata->flashProfile == FLASH_PROFILE_SST39VF512 &&
                    savedata->command == FLASH_COMMAND_ERASE)
                    savedata->command = FLASH_COMMAND_NONE;
            }
            break;

        case FLASH_STATE_CONTINUE:
            savedata->flashState = FLASH_STATE_RAW;
            if (savedata->flashProfile == FLASH_PROFILE_SST39VF512 &&
                savedata->command == FLASH_COMMAND_ERASE) {
                if (value == FLASH_COMMAND_ERASE_SECTOR)
                    _flashEraseSector(savedata, address);
                else if (commandAddress == FLASH_BASE_HI && value == FLASH_COMMAND_ERASE_CHIP)
                    _flashErase(savedata);
                savedata->command = FLASH_COMMAND_NONE;
                break;
            }
            if (commandAddress == FLASH_BASE_HI) {
                switch (savedata->command) {
                    case FLASH_COMMAND_NONE:
                        switch (value) {
                            case FLASH_COMMAND_ERASE:
                            case FLASH_COMMAND_ID:
                            case FLASH_COMMAND_PROGRAM:
                            case FLASH_COMMAND_SWITCH_BANK:
                                if (value != FLASH_COMMAND_SWITCH_BANK ||
                                    savedata->flashProfile != FLASH_PROFILE_SST39VF512)
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
    return true;
}

/**
 * @brief EEPROM helpers.
 */
/**
 * @brief Infer an unforced EEPROM capacity from a complete DMA command.
 */
static bool _selectEepromCapacity(struct CupidSavedata *savedata, uint32_t remaining, bool write) {
    /* At the second prefix bit, 14-bit commands retain 80 write bits or
     * 16 read-request bits. Capacity does not depend on the requested block. */
    if (savedata->type != SAVE_EEPROM512 || savedata->typeForced ||
        remaining != (write ? 80u : 16u))
        return true;
    uint8_t *newdata = malloc(GBA_SIZE_EEPROM);
    if (!newdata) {
        errno = ENOMEM;
        return false;
    }
    memcpy(newdata, savedata->data, GBA_SIZE_EEPROM512);
    memset(newdata + GBA_SIZE_EEPROM512, 0xFF, GBA_SIZE_EEPROM - GBA_SIZE_EEPROM512);
    free(savedata->data);
    savedata->data = newdata;
    savedata->currentBank = NULL;
    savedata->type = SAVE_EEPROM;
    savedata->size = GBA_SIZE_EEPROM;
    return true;
}

/**
 * @brief Write one EEPROM bit through the command state machine.
 *
 * writeSize is the number of bits remaining in the DMA transfer.
 */
bool cupid_savedata_write_eeprom(struct CupidSavedata *savedata, uint16_t value,
                                 uint32_t writeSize) {
    if (savedata->type == SAVE_AUTODETECT && savedata->flashState == FLASH_STATE_ALLOCATION)
        return true;
    /* A failed host allocation rejects this whole transfer. Keep discarding
     * through its original stop boundary even before a type was detected. */
    if (savedata->command == EEPROM_COMMAND_ABORTED) {
        if (writeSize <= 1)
            savedata->command = EEPROM_COMMAND_NULL;
        return true;
    }
    if (savedata->type == SAVE_AUTODETECT && !cupid_savedata_init_eeprom(savedata)) {
        if (writeSize > 1)
            savedata->command = EEPROM_COMMAND_ABORTED;
        return false;
    }
    if (savedata->programming)
        return true;
    switch (savedata->command) {
        /** Read header, bit 0/1 of the 2-bit command prefix. */
        case EEPROM_COMMAND_NULL:
        default:
            savedata->command =
                writeSize > 1 ? (enum CupidSavedataCommand)(value & 0x1) : EEPROM_COMMAND_NULL;
            break;
        case EEPROM_COMMAND_PENDING: {
            if (writeSize <= 1) {
                savedata->command = EEPROM_COMMAND_NULL;
                break;
            }
            const enum CupidSavedataCommand command =
                (enum CupidSavedataCommand)(((int)savedata->command << 1) | (value & 0x1));
            if (!_selectEepromCapacity(savedata, writeSize, command == EEPROM_COMMAND_WRITE)) {
                savedata->command = EEPROM_COMMAND_ABORTED;
                return false;
            }
            savedata->command = command;
            savedata->eepromAddressBits = savedata->type == SAVE_EEPROM ? 14 : 6;
            if (savedata->command == EEPROM_COMMAND_WRITE) {
                savedata->writeAddress = 0;
                savedata->eepromWriteBits = 0;
                memset(savedata->eepromData, 0, sizeof(savedata->eepromData));
            } else {
                savedata->readAddress = 0;
            }
            break;
        }

        /** Write command: address bits, then 64 data bits, then stop bit. */
        case EEPROM_COMMAND_WRITE:
            if (writeSize <= 1) {
                /* Ending DMA cannot turn an address or payload bit into the
                 * missing stop bit of an incomplete command. */
                if (!savedata->eepromAddressBits && savedata->eepromWriteBits == 64) {
                    cupid_savedata_begin_programming(savedata, (savedata->writeAddress - 64) >> 3,
                                                     8, savedata->eepromData,
                                                     savedata->eepromProgramClocks);
                }
                savedata->eepromAddressBits = 0;
                savedata->command = EEPROM_COMMAND_NULL;
            } else if (savedata->eepromAddressBits) {
                /** The selected chip consumes six or fourteen address bits. */
                savedata->eepromAddressBits--;
                savedata->writeAddress <<= 1;
                savedata->writeAddress |= (value & 0x1) << 6;
                /* The physical block decoder retains six or ten address bits;
                 * the 8-KiB protocol still consumes all fourteen wire bits. */
                savedata->writeAddress &= (uint32_t)(savedata->size - 1) << 3;
            } else if (savedata->eepromWriteBits < 64 &&
                       (savedata->writeAddress >> 3) < savedata->size) {
                savedata->eepromData[savedata->eepromWriteBits / 8] |=
                    (value & 1) << (7 - savedata->eepromWriteBits % 8);
                savedata->eepromWriteBits++;
                ++savedata->writeAddress;
            }
            break;

        /** Read command: fixed address width followed by the stop bit. */
        case EEPROM_COMMAND_READ_PENDING:
            if (writeSize <= 1) {
                if (!savedata->eepromAddressBits) {
                    savedata->readBitsRemaining = 68;
                    savedata->command = EEPROM_COMMAND_READ;
                } else {
                    savedata->eepromAddressBits = 0;
                    savedata->command = EEPROM_COMMAND_NULL;
                }
            } else if (savedata->eepromAddressBits) {
                savedata->eepromAddressBits--;
                savedata->readAddress <<= 1;
                if (value & 0x1) {
                    savedata->readAddress |= 0x40;
                }
                savedata->readAddress &= (uint32_t)(savedata->size - 1) << 3;
            }
            break;

        case EEPROM_COMMAND_READ:
            /** Should not be writing during read; ignore. */
            break;
    }
    return true;
}

/**
 * @brief Read one EEPROM bit from the command state machine.
 */
uint16_t cupid_savedata_read_eeprom(struct CupidSavedata *savedata) {
    if (savedata->programming)
        return 0;
    if (savedata->command != EEPROM_COMMAND_READ) {
        /** Not in read mode, return 1 (ready bit). */
        return 1;
    }
    --savedata->readBitsRemaining;
    if (savedata->readBitsRemaining < 64) {
        int step = 63 - savedata->readBitsRemaining;
        uint32_t address = (savedata->readAddress + step) >> 3;
        uint8_t data = savedata->data[address] >> (0x7 - (step & 0x7));
        if (!savedata->readBitsRemaining) {
            savedata->command = EEPROM_COMMAND_NULL;
        }
        return data & 0x1;
    }
    return 0;
}
