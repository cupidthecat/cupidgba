/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_ownership.c
 * @brief Cartridge replacement, backup ownership and event cancellation.
 *
 * - Check all backup transitions own fresh storage.
 * - Check replacement cancels old events and rebinds new ones.
 */
#include "../../test.h"
#include "cupid/gba.h"
#include <errno.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

static char filename[1024];

static const struct {
    const char *marker;
    enum CupidSaveType type;
    size_t size;
} devices[] = {{"SRAM_V", SAVE_SRAM, GBA_SIZE_SRAM},
               {"SRAM_F_V", SAVE_SRAM, GBA_SIZE_SRAM},
               {"FLASH_V", SAVE_FLASH512, GBA_SIZE_FLASH512},
               {"FLASH512_V", SAVE_FLASH512, GBA_SIZE_FLASH512},
               {"FLASH1M_V", SAVE_FLASH1M, GBA_SIZE_FLASH1M},
               {"EEPROM_V", SAVE_EEPROM512, GBA_SIZE_EEPROM512},
               {"", SAVE_AUTODETECT, 0}};

static struct CupidGBA *machine(void) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void image(const char *marker, size_t offset, size_t size) {
    uint8_t bytes[512] = {0};
    CHECK(size <= sizeof(bytes) && offset + strlen(marker) <= sizeof(bytes));
    memcpy(bytes + offset, marker, strlen(marker));
    FILE *file = fopen(filename, "wb");
    CHECK(file);
    CHECK(fwrite(bytes, 1, size, file) == size);
    CHECK(fclose(file) == 0);
}

static void fresh(const struct CupidGBA *gba, unsigned device) {
    const struct CupidSavedata *save = &gba->memory.savedata;
    CHECK(save->type == devices[device].type && save->size == devices[device].size);
    CHECK(!save->dirty && !save->typeForced && !save->programming);
    CHECK(save->timing == &gba->timing && save->readyEvent.context == save);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &save->readyEvent));
    CHECK(save->flashProfile == FLASH_PROFILE_DEFAULT && save->flashBank == 0);
    CHECK(save->flashProgramClocks == 650 && save->eepromProgramClocks == 108368);
    CHECK(save->command == FLASH_COMMAND_NONE && save->readBitsRemaining == 0);
    for (size_t i = 0; i < save->size; ++i)
        CHECK(save->data[i] == 0xFF);
    if (save->type == SAVE_AUTODETECT)
        CHECK(!save->data && !save->currentBank);
    else if (save->type == SAVE_EEPROM512)
        CHECK(save->data && !save->currentBank);
    else
        CHECK(save->data && save->currentBank == save->data);
}

static void all_backup_transitions_own_fresh_storage(void) {
    for (unsigned old = 0; old < sizeof(devices) / sizeof(*devices); ++old) {
        for (unsigned next = 0; next < sizeof(devices) / sizeof(*devices); ++next) {
            struct CupidGBA *gba = machine();
            image(devices[old].marker, 256, 512);
            CHECK(cupid_load_rom(gba, filename));
            fresh(gba, old);
            if (gba->memory.savedata.size) {
                memset(gba->memory.savedata.data, 0xA5, gba->memory.savedata.size);
                gba->memory.savedata.dirty = true;
            }
            gba->memory.savedata.typeForced = true;
            CHECK(cupid_savedata_set_programming_clocks(&gba->memory.savedata, 1, 2, 3, 4));
            image(devices[next].marker, 256, 512);
            CHECK(cupid_load_rom(gba, filename));
            fresh(gba, next);
            if (gba->memory.savedata.size) {
                /* Console reset retains battery storage; replacement does not. */
                gba->memory.savedata.data[0] = 0x3C;
                gba->memory.savedata.dirty = true;
                cupid_gba_reset(gba);
                CHECK(gba->memory.savedata.data[0] == 0x3C && gba->memory.savedata.dirty);
            }
            destroy(gba);
        }
    }
    for (int forced = SAVE_NONE; forced <= SAVE_EEPROM512; ++forced) {
        for (unsigned next = 0; next < sizeof(devices) / sizeof(*devices); ++next) {
            struct CupidGBA *initial = machine();
            cupid_savedata_force_type(&initial->memory.savedata, (enum CupidSaveType)forced);
            image("SRAM_V", 256, 512);
            CHECK(cupid_load_rom(initial, filename));
            CHECK(initial->memory.savedata.type == (enum CupidSaveType)forced);
            CHECK(initial->memory.savedata.typeForced);
            image(devices[next].marker, 256, 512);
            CHECK(cupid_load_rom(initial, filename));
            fresh(initial, next);
            destroy(initial);
        }
    }
    struct CupidGBA *gba = machine();
    CHECK(cupid_savedata_set_programming_clocks(&gba->memory.savedata, 11, 22, 33, 44));
    image("EEPROM_V", 256, 512);
    CHECK(cupid_load_rom(gba, filename));
    CHECK(gba->memory.savedata.eepromProgramClocks == 11);
    CHECK(gba->memory.savedata.flashProgramClocks == 22);
    CHECK(gba->memory.savedata.flashSectorEraseClocks == 33);
    CHECK(gba->memory.savedata.flashChipEraseClocks == 44);
    CHECK(cupid_load_rom(gba, filename));
    fresh(gba, 5);
    destroy(gba);
    gba = machine();
    cupid_savedata_force_type(&gba->memory.savedata, SAVE_FLASH512);
    CHECK(cupid_savedata_set_flash_profile(&gba->memory.savedata, FLASH_PROFILE_SST39VF512));
    image("EEPROM_V", 256, 512);
    CHECK(cupid_load_rom(gba, filename));
    CHECK(gba->memory.savedata.type == SAVE_FLASH512);
    CHECK(gba->memory.savedata.flashProfile == FLASH_PROFILE_SST39VF512);
    CHECK(cupid_load_rom(gba, filename));
    fresh(gba, 5);
    destroy(gba);
}

static void replacement_cancels_old_events_and_rebinds_new_ones(void) {
    for (unsigned old = 2; old <= 5; ++old) {
        struct CupidGBA *gba = machine();
        image(devices[old].marker, 256, 512);
        CHECK(cupid_load_rom(gba, filename));
        const uint8_t value = 0x23;
        cupid_savedata_begin_programming(&gba->memory.savedata, 7, 1, &value, 40);
        CHECK(gba->memory.savedata.programming);
        image("EEPROM_V", 256, 512);
        CHECK(cupid_load_rom(gba, filename));
        fresh(gba, 5);
        cupid_timing_tick(&gba->timing, 40);
        fresh(gba, 5);
        cupid_savedata_begin_programming(&gba->memory.savedata, 7, 1, &value, 40);
        cupid_timing_tick(&gba->timing, 39);
        CHECK(gba->memory.savedata.programming && gba->memory.savedata.data[7] == 0xFF);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(!gba->memory.savedata.programming && gba->memory.savedata.dirty);
        CHECK(gba->memory.savedata.data[7] == value);
        destroy(gba);
    }
}

struct OutputOwner {
    struct CupidGBA *gba;
    uint8_t *rom, *backup;
    uint64_t identity;
    unsigned stopped;
    bool armed;
};

static void observe_motor(void *context, bool active, uint64_t clock) {
    struct OutputOwner *owner = context;
    (void)clock;
    if (!owner->armed)
        return;
    CHECK(!active);
    CHECK(owner->gba->memory.rom == owner->rom);
    CHECK(owner->gba->memory.savedata.data == owner->backup);
    CHECK(owner->backup[7] == 0xA5);
    CHECK(owner->gba->cartridge.identity == owner->identity);
    ++owner->stopped;
}

static void board_output_stops_before_old_storage_is_released(void) {
    struct CupidGBA *gba = machine();
    image("SRAM_V", 256, 512);
    CHECK(cupid_load_rom(gba, filename));
    gba->memory.savedata.data[7] = 0xA5;
    struct OutputOwner owner = {.gba = gba,
                                .rom = gba->memory.rom,
                                .backup = gba->memory.savedata.data,
                                .identity = gba->cartridge.identity};
    cupid_cartridge_set_rumble_output(gba, observe_motor, &owner);
    cupid_cartridge_enable_rumble(gba, true);
    CHECK(cupid_cartridge_gpio_write(gba, 0x080000C6, 8));
    CHECK(cupid_cartridge_gpio_write(gba, 0x080000C4, 8));
    CHECK(gba->cartridge.rumbleActive);
    cupid_cartridge_set_solar_level(gba, 57);
    CHECK(cupid_cartridge_set_gyro_sample(gba, 1234));
    CHECK(cupid_cartridge_set_tilt_sample(gba, 321, 654));
    /* Both callbacks belong to the discarded board, even if their deadlines
     * fall after the candidate is installed. */
    gba->cartridge.prefetchScheduled = true;
    cupid_timing_schedule(&gba->timing, &gba->cartridge.prefetchEvent, 30);
    cupid_timing_schedule(&gba->timing, &gba->cartridge.rtcInterruptEvent, 40);
    owner.armed = true;
    image("EEPROM_V", 256, 512);
    CHECK(cupid_load_rom(gba, filename));
    CHECK(owner.stopped == 1);
    owner.armed = false;
    CHECK(gba->cartridge.rumbleChanged == observe_motor && gba->cartridge.rumbleContext == &owner);
    CHECK(!gba->cartridge.rumbleActive && !gba->cartridge.rumblePresent);
    CHECK(gba->cartridge.solar.threshold == 255 - 57);
    CHECK(gba->cartridge.gyro.sample == 1234);
    CHECK(gba->cartridge.tilt.inputX == 321 && gba->cartridge.tilt.inputY == 654);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.prefetchEvent));
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent));
    cupid_timing_tick(&gba->timing, 100);
    fresh(gba, 5);
    destroy(gba);
}

static void complete_aligned_markers_do_not_need_trailing_padding(void) {
    struct CupidGBA *gba = machine();
    for (unsigned d = 0; d < 6; ++d) {
        size_t length = strlen(devices[d].marker);
        image(devices[d].marker, 256, 256 + length);
        CHECK(cupid_load_rom(gba, filename));
        fresh(gba, d);
        image(devices[d].marker, 256, 255 + length);
        CHECK(cupid_load_rom(gba, filename));
        fresh(gba, 6);
        image(devices[d].marker, 257, 512);
        CHECK(cupid_load_rom(gba, filename));
        fresh(gba, 6);
        image(devices[d].marker, 0, length);
        CHECK(cupid_load_rom(gba, filename));
        fresh(gba, 6);
    }
    destroy(gba);
}

static void busy_or_nonregular_input_preserves_attached_device(void) {
    struct CupidGBA *gba = machine();
    image("FLASH_V", 256, 512);
    CHECK(cupid_load_rom(gba, filename));
    const uint8_t value = 0x23;
    cupid_savedata_begin_programming(&gba->memory.savedata, 7, 1, &value, 40);
    const struct CupidSavedata before = gba->memory.savedata;
    uint8_t *rom = gba->memory.rom;
    for (unsigned flag = 0; flag < 4; ++flag) {
        gba->cpuInstructionPending = flag == 0;
        gba->memory.dmaRunning = flag == 1;
        gba->memory.busBusy = flag == 2;
        gba->memory.busLocked = flag == 3;
        errno = 0;
        CHECK(!cupid_load_rom(gba, filename) && errno == EBUSY);
        CHECK(gba->memory.rom == rom);
        CHECK(!memcmp(&before, &gba->memory.savedata, sizeof(before)));
    }
    gba->cpuInstructionPending = false;
    gba->memory.dmaRunning = false;
    gba->memory.busBusy = false;
    gba->memory.busLocked = false;
    CHECK(remove(filename) == 0);
    CHECK(!cupid_load_rom(gba, filename));
    CHECK(!cupid_load_rom(gba, "."));
#ifndef _WIN32
    CHECK(mkfifo(filename, 0600) == 0);
    /* No writer exists: opening this FIFO would hang until the test guard. */
    CHECK(!cupid_load_rom(gba, filename));
    CHECK(remove(filename) == 0);
#endif
    CHECK(gba->memory.rom == rom);
    CHECK(!memcmp(&before, &gba->memory.savedata, sizeof(before)));
    cupid_timing_tick(&gba->timing, 40);
    CHECK(gba->memory.savedata.data[7] == value && gba->memory.savedata.dirty);
    image("SRAM_V", 256, 512);
    CHECK(cupid_load_rom(gba, filename));
    fresh(gba, 0);
    destroy(gba);
}

int main(int argc, char **argv) {
    CHECK(argc == 3);
    int n = snprintf(filename, sizeof(filename), "%s/cartridge-ownership-%s.gba", argv[2], argv[1]);
    CHECK(n > 0 && (size_t)n < sizeof(filename));
    if (!strcmp(argv[1], "transitions"))
        RUN(all_backup_transitions_own_fresh_storage);
    else if (!strcmp(argv[1], "events"))
        RUN(replacement_cancels_old_events_and_rebinds_new_ones);
    else if (!strcmp(argv[1], "board"))
        RUN(board_output_stops_before_old_storage_is_released);
    else if (!strcmp(argv[1], "boundaries"))
        RUN(complete_aligned_markers_do_not_need_trailing_padding);
    else if (!strcmp(argv[1], "busy"))
        RUN(busy_or_nonregular_input_preserves_attached_device);
    else
        CHECK(false);
    CHECK(remove(filename) == 0);
    return 0;
}
