/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      save.c
 * @author    cupidthecat
 * @brief     Frontend backup-selection parsing and transactional configuration.
 * @date      2026-11-16
 *
 * - Map accepted save names to supported backup selections.
 * - Apply validated choices without changing state on invalid input.
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
#include "cupid/platform/save.h"
#include <errno.h>

bool cupid_platform_save_parse(const char *name, struct CupidPlatformSave *selection) {
    static const struct {
        const char *name;
        enum CupidSaveType type;
        enum CupidFlashProfile profile;
    } choices[] = {{"auto", SAVE_AUTODETECT, FLASH_PROFILE_DEFAULT},
                   {"none", SAVE_NONE, FLASH_PROFILE_DEFAULT},
                   {"sram", SAVE_SRAM, FLASH_PROFILE_DEFAULT},
                   {"sram512", SAVE_SRAM512, FLASH_PROFILE_DEFAULT},
                   {"flash512", SAVE_FLASH512, FLASH_PROFILE_DEFAULT},
                   {"flash1m", SAVE_FLASH1M, FLASH_PROFILE_DEFAULT},
                   {"sst39vf512", SAVE_FLASH512, FLASH_PROFILE_SST39VF512},
                   {"eeprom", SAVE_EEPROM, FLASH_PROFILE_DEFAULT},
                   {"eeprom512", SAVE_EEPROM512, FLASH_PROFILE_DEFAULT}};

    if (name && selection) {
        for (unsigned i = 0; i < sizeof(choices) / sizeof(*choices); ++i) {
            if (!strcmp(name, choices[i].name)) {
                *selection = (struct CupidPlatformSave){choices[i].type, choices[i].profile};
                return true;
            }
        }
    }
    errno = EINVAL;
    return false;
}

bool cupid_platform_save_configure(struct CupidSavedata *save,
                                   const struct CupidPlatformSave *selection) {
    if (!save || !selection || selection->type < SAVE_AUTODETECT ||
        selection->type > SAVE_EEPROM512 ||
        (selection->profile != FLASH_PROFILE_DEFAULT &&
         (selection->profile != FLASH_PROFILE_SST39VF512 || selection->type != SAVE_FLASH512))) {
        errno = EINVAL;
        return false;
    }
    if (selection->type == SAVE_AUTODETECT)
        return true;
    /* Same-type selection retains storage and programming state. Validate its
     * profile before force_type can mark an inferred device as explicit. */
    if ((selection->type == SAVE_FLASH512 || selection->type == SAVE_FLASH1M) &&
        save->type == selection->type && save->data) {
        if (!cupid_savedata_set_flash_profile(save, selection->profile)) {
            errno = EBUSY;
            return false;
        }
        return cupid_savedata_force_type(save, selection->type);
    }
    if (!cupid_savedata_force_type(save, selection->type))
        return false;
    if (selection->type == SAVE_FLASH512 || selection->type == SAVE_FLASH1M)
        return cupid_savedata_set_flash_profile(save, selection->profile);
    return true;
}
