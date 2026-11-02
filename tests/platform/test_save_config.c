/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_save_config.c
 * @brief Frontend save selections and invalid-configuration isolation.
 *
 * - Check complete names and invalid inputs.
 * - Check every selection after every detected type.
 */
#include "../test.h"
#include "cupid/platform/save.h"
#include <errno.h>

static const char *names[] = {"auto",    "none",       "sram",   "sram512",  "flash512",
                              "flash1m", "sst39vf512", "eeprom", "eeprom512"};

static const enum CupidSaveType types[] = {SAVE_AUTODETECT, SAVE_NONE,     SAVE_SRAM,
                                           SAVE_SRAM512,    SAVE_FLASH512, SAVE_FLASH1M,
                                           SAVE_FLASH512,   SAVE_EEPROM,   SAVE_EEPROM512};

static const size_t sizes[] = {0, 0, 32768, 65536, 65536, 131072, 65536, 8192, 512};

static void complete_names_and_invalid_inputs(void) {
    struct CupidPlatformSave selection = {SAVE_SRAM, FLASH_PROFILE_DEFAULT};
    const struct CupidPlatformSave original = selection;
    const char *invalid[] = {NULL,     "",      "SRAM",       " sram",       "sram ",
                             "sram32", "flash", "eeprom1024", "sst39vf512x", "automatic"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        CHECK(!cupid_platform_save_parse(invalid[i], &selection) && errno == EINVAL);
        CHECK(!memcmp(&selection, &original, sizeof(selection)));
    }
    CHECK(!cupid_platform_save_parse("sram", NULL));
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        CHECK(cupid_platform_save_parse(names[i], &selection));
        CHECK(selection.type == types[i]);
        CHECK(selection.profile == (i == 6 ? FLASH_PROFILE_SST39VF512 : FLASH_PROFILE_DEFAULT));
    }
}

static void every_selection_after_every_detected_type(void) {
    for (unsigned before = 0; before < sizeof(names) / sizeof(*names); ++before) {
        for (unsigned after = 0; after < sizeof(names) / sizeof(*names); ++after) {
            struct CupidSavedata save;
            cupid_savedata_init(&save);
            struct CupidPlatformSave selection;
            CHECK(cupid_platform_save_parse(names[before], &selection));
            CHECK(cupid_platform_save_configure(&save, &selection));
            if (save.size)
                memset(save.data, 0x69, save.size);
            const struct CupidSavedata original = save;
            CHECK(cupid_platform_save_parse(names[after], &selection));
            CHECK(cupid_platform_save_configure(&save, &selection));
            if (!after) {
                CHECK(!memcmp(&original, &save, sizeof(save)));
            } else {
                CHECK(save.type == types[after] && save.size == sizes[after] && save.typeForced);
                if (save.type == SAVE_FLASH512 || save.type == SAVE_FLASH1M)
                    CHECK(save.flashProfile == selection.profile);
                for (size_t i = 0; i < save.size; ++i)
                    CHECK(save.data[i] == (original.type == save.type ? 0x69 : 0xFF));
            }
            cupid_savedata_deinit(&save);
        }
    }
}

static void invalid_configuration_preserves_the_complete_backup(void) {
    struct CupidSavedata save;
    cupid_savedata_init(&save);
    cupid_savedata_force_type(&save, SAVE_SRAM);
    memset(save.data, 0x93, save.size);
    const struct CupidSavedata original = save;
    const struct CupidPlatformSave invalid[] = {{(enum CupidSaveType) - 2, FLASH_PROFILE_DEFAULT},
                                                {(enum CupidSaveType)99, FLASH_PROFILE_DEFAULT},
                                                {SAVE_FLASH1M, FLASH_PROFILE_SST39VF512},
                                                {SAVE_SRAM, FLASH_PROFILE_SST39VF512},
                                                {SAVE_AUTODETECT, FLASH_PROFILE_SST39VF512},
                                                {SAVE_FLASH512, (enum CupidFlashProfile)99}};
    CHECK(!cupid_platform_save_configure(&save, NULL));
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        CHECK(!cupid_platform_save_configure(&save, &invalid[i]) && errno == EINVAL);
        CHECK(!memcmp(&original, &save, sizeof(save)));
        for (size_t j = 0; j < save.size; ++j)
            CHECK(save.data[j] == 0x93);
    }
    cupid_savedata_deinit(&save);
}

int main(void) {
    RUN(complete_names_and_invalid_inputs);
    RUN(every_selection_after_every_detected_type);
    RUN(invalid_configuration_preserves_the_complete_backup);
    return 0;
}
