/* SPDX-License-Identifier: GPL-3.0-or-later */
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
    cupid_savedata_force_type(save, selection->type);
    if (selection->type == SAVE_FLASH512 || selection->type == SAVE_FLASH1M)
        return cupid_savedata_set_flash_profile(save, selection->profile);
    return true;
}
