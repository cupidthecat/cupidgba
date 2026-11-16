/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_allocation.c
 * @author    cupidthecat
 * @brief     Backup allocation failure isolation, frame recovery and event ownership.
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
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/platform/save.h"
#include <errno.h>

static int failAfter = -1;
static unsigned allocations;

static void *backup_allocation(size_t size) {
    ++allocations;
    if (failAfter == 0) {
        failAfter = -1;
        errno = ENOMEM;
        return NULL;
    }
    if (failAfter > 0)
        --failAfter;
    return malloc(size);
}

/* Compile the production save paths with only their allocation calls replaced.
 * Core memory and DMA callers resolve to these same public implementations. */
#define malloc backup_allocation
#include "../../../src/gba/save/detection.c"
#include "../../../src/gba/savedata.c"
#include "../../../src/platform/save.c"
#undef malloc

static const uint64_t epochs[] = {0, INT32_MAX - 2000u, UINT32_MAX - 2000u, UINT64_C(0x300000000)};

static const enum CupidSaveType types[] = {SAVE_AUTODETECT, SAVE_NONE,     SAVE_SRAM,
                                           SAVE_SRAM512,    SAVE_FLASH512, SAVE_FLASH1M,
                                           SAVE_EEPROM,     SAVE_EEPROM512};

static const uint8_t payload[] = {0x91, 0x72, 0x53, 0x34, 0x15, 0xF6, 0xD7, 0xB8};

struct Fixture {
    struct CupidSavedata save;
    struct CupidTiming timing;
    int32_t cycles, nextEvent;
};

struct Snapshot {
    struct CupidSavedata save;
    struct CupidTiming timing;
    int32_t cycles, nextEvent;
    uint8_t bytes[GBA_SIZE_FLASH1M];
};

static void arm_failure(void) {
    failAfter = 0;
    allocations = 0;
    errno = 0;
}

static void fresh(struct Fixture *fixture, unsigned epoch, enum CupidSaveType type) {
    memset(fixture, 0, sizeof(*fixture));
    failAfter = -1;
    cupid_timing_init(&fixture->timing, &fixture->cycles, &fixture->nextEvent);
    fixture->timing.globalCycles = epochs[epoch];
    fixture->timing.masterCycles = (uint32_t)epochs[epoch];
    cupid_savedata_init(&fixture->save);
    cupid_savedata_bind_timing(&fixture->save, &fixture->timing);
    CHECK(cupid_savedata_force_type(&fixture->save, type));
    fixture->save.typeForced = false;
    CHECK(cupid_savedata_set_programming_clocks(&fixture->save, 97, 31, 43, 59));
    for (size_t i = 0; i < fixture->save.size; i++)
        fixture->save.data[i] = (uint8_t)(0x63 + i * 37);
}

static void capture(struct Snapshot *snapshot, const struct Fixture *fixture) {
    snapshot->save = fixture->save;
    snapshot->timing = fixture->timing;
    snapshot->cycles = fixture->cycles;
    snapshot->nextEvent = fixture->nextEvent;
    CHECK(fixture->save.size <= sizeof(snapshot->bytes));
    if (fixture->save.data)
        memcpy(snapshot->bytes, fixture->save.data, fixture->save.size);
}

static void unchanged(const struct Snapshot *snapshot, const struct Fixture *fixture) {
    CHECK(!memcmp(&snapshot->save, &fixture->save, sizeof(fixture->save)));
    CHECK(!memcmp(&snapshot->timing, &fixture->timing, sizeof(fixture->timing)));
    CHECK(snapshot->cycles == fixture->cycles && snapshot->nextEvent == fixture->nextEvent);
    if (fixture->save.data)
        CHECK(!memcmp(snapshot->bytes, fixture->save.data, fixture->save.size));
}

static void erased(const struct CupidSavedata *save) {
    CHECK(save->data && save->size);
    for (size_t i = 0; i < save->size; i++)
        CHECK(save->data[i] == 0xFF);
}

static bool programmable(enum CupidSaveType type) {
    return type == SAVE_FLASH512 || type == SAVE_FLASH1M || type == SAVE_EEPROM ||
           type == SAVE_EEPROM512;
}

static void initial(void) {
    static const struct {
        enum CupidSaveType type;
        size_t size;
        bool (*initialize)(struct CupidSavedata *);
    } cases[] = {{SAVE_SRAM, GBA_SIZE_SRAM, cupid_savedata_init_sram},
                 {SAVE_SRAM512, GBA_SIZE_FLASH512, cupid_savedata_init_sram512},
                 {SAVE_FLASH512, GBA_SIZE_FLASH512, cupid_savedata_init_flash},
                 {SAVE_FLASH1M, GBA_SIZE_FLASH1M, cupid_savedata_init_flash},
                 {SAVE_EEPROM, GBA_SIZE_EEPROM, cupid_savedata_init_eeprom},
                 {SAVE_EEPROM512, GBA_SIZE_EEPROM512, cupid_savedata_init_eeprom}};

    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
            struct Fixture fixture;
            struct Snapshot snapshot;
            fresh(&fixture, epoch, SAVE_AUTODETECT);
            fixture.save.type = cases[i].type;
            capture(&snapshot, &fixture);
            arm_failure();
            CHECK(!cases[i].initialize(&fixture.save) && errno == ENOMEM && allocations == 1);
            unchanged(&snapshot, &fixture);
            CHECK(cases[i].initialize(&fixture.save));
            CHECK(fixture.save.type == cases[i].type && fixture.save.size == cases[i].size);
            CHECK(fixture.save.readyEvent.context == &fixture.save);
            CHECK(fixture.save.eepromProgramClocks == 97 && fixture.save.flashProgramClocks == 31);
            erased(&fixture.save);
            if (cases[i].type == SAVE_FLASH1M) {
                fixture.save.flashBank = 1;
                fixture.save.currentBank = fixture.save.data + GBA_SIZE_FLASH512;
            }
            if (programmable(cases[i].type))
                cupid_savedata_begin_programming(&fixture.save, 7, 1, payload, 31);
            fixture.save.dirty = true;
            capture(&snapshot, &fixture);
            arm_failure();
            CHECK(cases[i].initialize(&fixture.save) && allocations == 0);
            unchanged(&snapshot, &fixture);
            failAfter = -1;
            if (programmable(cases[i].type)) {
                cupid_timing_tick(&fixture.timing, 31);
                CHECK(fixture.save.data[7] == payload[0] && !fixture.save.programming);
            }
            cupid_savedata_deinit(&fixture.save);
            scenes++;
        }
    printf("PASS backup initial allocation and idempotence scenes=%u\n", scenes);
}

static void replacement(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned before = 0; before < sizeof(types) / sizeof(*types); before++)
            for (unsigned after = 2; after < sizeof(types) / sizeof(*types); after++)
                for (unsigned state = 0; state < 3; state++) {
                    if (before == after)
                        continue;
                    struct Fixture fixture;
                    struct Snapshot snapshot;
                    fresh(&fixture, epoch, types[before]);
                    fixture.save.dirty = fixture.save.data != NULL;
                    if (types[before] == SAVE_FLASH512)
                        CHECK(cupid_savedata_set_flash_profile(&fixture.save,
                                                               FLASH_PROFILE_SST39VF512));
                    if (state == 1 && programmable(types[before])) {
                        if (types[before] == SAVE_EEPROM || types[before] == SAVE_EEPROM512)
                            CHECK(cupid_savedata_write_eeprom(&fixture.save, 1, 9));
                        else
                            CHECK(cupid_savedata_write_flash(&fixture.save, 0x5555, 0xAA));
                    }
                    if (state == 2 && programmable(types[before]))
                        cupid_savedata_begin_programming(&fixture.save, 7, 1, payload, 31);
                    capture(&snapshot, &fixture);
                    arm_failure();
                    CHECK(!cupid_savedata_force_type(&fixture.save, types[after]));
                    CHECK(errno == ENOMEM && allocations == 1);
                    unchanged(&snapshot, &fixture);
                    if (snapshot.save.programming) {
                        cupid_timing_tick(&fixture.timing, 31);
                        CHECK(fixture.save.data[7] == payload[0] && !fixture.save.programming);
                    }
                    CHECK(cupid_savedata_force_type(&fixture.save, types[after]));
                    CHECK(fixture.save.type == types[after] && fixture.save.typeForced);
                    CHECK(fixture.save.readyEvent.context == &fixture.save);
                    CHECK(fixture.save.timing == &fixture.timing && !fixture.save.programming);
                    CHECK(!cupid_timing_is_scheduled(&fixture.timing, &fixture.save.readyEvent));
                    erased(&fixture.save);
                    if (programmable(types[after])) {
                        cupid_savedata_begin_programming(&fixture.save, 5, 1, payload + 1, 17);
                        cupid_timing_tick(&fixture.timing, 17);
                        CHECK(fixture.save.data[5] == payload[1] && !fixture.save.programming);
                    }
                    cupid_savedata_deinit(&fixture.save);
                    scenes++;
                }
    printf("PASS backup replacement failure and retry scenes=%u\n", scenes);
}

static void configuration(void) {
    static const char *names[] = {"sram",       "sram512", "flash512", "flash1m",
                                  "sst39vf512", "eeprom",  "eeprom512"};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++) {
        for (unsigned i = 0; i < sizeof(names) / sizeof(*names); i++) {
            struct Fixture fixture;
            struct Snapshot snapshot;
            struct CupidPlatformSave selection;
            fresh(&fixture, epoch, SAVE_NONE);
            CHECK(cupid_platform_save_parse(names[i], &selection));
            capture(&snapshot, &fixture);
            arm_failure();
            CHECK(!cupid_platform_save_configure(&fixture.save, &selection));
            CHECK(errno == ENOMEM && allocations == 1);
            unchanged(&snapshot, &fixture);
            CHECK(cupid_platform_save_configure(&fixture.save, &selection));
            CHECK(fixture.save.type == selection.type && fixture.save.typeForced);
            if (selection.type == SAVE_FLASH512 || selection.type == SAVE_FLASH1M)
                CHECK(fixture.save.flashProfile == selection.profile);
            cupid_savedata_deinit(&fixture.save);
            scenes++;
        }
        for (unsigned state = 0; state < 2; state++)
            for (unsigned forced = 0; forced < 2; forced++)
                for (unsigned profile = 0; profile < 2; profile++) {
                    struct Fixture fixture;
                    struct Snapshot snapshot;
                    fresh(&fixture, epoch, SAVE_FLASH512);
                    fixture.save.typeForced = forced != 0;
                    if (state)
                        cupid_savedata_begin_programming(&fixture.save, 7, 1, payload, 31);
                    else
                        CHECK(cupid_savedata_write_flash(&fixture.save, 0x5555, 0xAA));
                    const struct CupidPlatformSave selection = {
                        SAVE_FLASH512, profile ? FLASH_PROFILE_SST39VF512 : FLASH_PROFILE_DEFAULT};
                    capture(&snapshot, &fixture);
                    arm_failure();
                    CHECK(!cupid_platform_save_configure(&fixture.save, &selection));
                    CHECK(errno == EBUSY && allocations == 0);
                    unchanged(&snapshot, &fixture);
                    failAfter = -1;
                    cupid_savedata_deinit(&fixture.save);
                    scenes++;
                }
        static const char *markers[] = {"EEPROM_V",  "SRAM_V",  "SRAM_F_V",
                                        "FLASH1M_V", "FLASH_V", "FLASH512_V"};
        for (unsigned i = 0; i < sizeof(markers) / sizeof(*markers); i++) {
            struct Fixture fixture;
            struct Snapshot snapshot;
            uint8_t rom[256] = {0};
            memcpy(rom + 0xC0, markers[i], strlen(markers[i]));
            fresh(&fixture, epoch, SAVE_AUTODETECT);
            capture(&snapshot, &fixture);
            arm_failure();
            CHECK(!cupid_savedata_detect_type(&fixture.save, rom, sizeof(rom)));
            CHECK(errno == ENOMEM && allocations == 1);
            unchanged(&snapshot, &fixture);
            CHECK(cupid_savedata_detect_type(&fixture.save, rom, sizeof(rom)));
            CHECK(fixture.save.type != SAVE_AUTODETECT && !fixture.save.typeForced);
            CHECK(fixture.save.eepromProgramClocks == 97 && fixture.save.flashProgramClocks == 31);
            erased(&fixture.save);
            cupid_savedata_deinit(&fixture.save);
            scenes++;
        }
    }
    printf("PASS backup frontend and signature failure scenes=%u\n", scenes);
}

static void flash(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned forced = 0; forced < 2; forced++) {
            struct Fixture fixture;
            struct Snapshot snapshot;
            fresh(&fixture, epoch, SAVE_FLASH512);
            fixture.save.typeForced = forced != 0;
            fixture.save.dirty = true;
            CHECK(cupid_savedata_write_flash(&fixture.save, 0x5555, 0xAA));
            CHECK(cupid_savedata_write_flash(&fixture.save, 0x2AAA, 0x55));
            CHECK(cupid_savedata_write_flash(&fixture.save, 0x5555, 0xB0));
            capture(&snapshot, &fixture);
            arm_failure();
            CHECK(!cupid_savedata_write_flash(&fixture.save, 0, 1));
            CHECK(errno == ENOMEM && allocations == 1);
            unchanged(&snapshot, &fixture);
            CHECK(cupid_savedata_write_flash(&fixture.save, 0, 1));
            CHECK(fixture.save.type == SAVE_FLASH1M && fixture.save.size == GBA_SIZE_FLASH1M);
            CHECK(fixture.save.flashBank == 1 &&
                  fixture.save.currentBank == fixture.save.data + GBA_SIZE_FLASH512);
            CHECK(fixture.save.typeForced == (forced != 0) && fixture.save.dirty);
            CHECK(!memcmp(fixture.save.data, snapshot.bytes, GBA_SIZE_FLASH512));
            for (size_t i = GBA_SIZE_FLASH512; i < fixture.save.size; i++)
                CHECK(fixture.save.data[i] == 0xFF);
            CHECK(fixture.save.command == FLASH_COMMAND_NONE);
            CHECK(cupid_savedata_write_flash(&fixture.save, 0x5555, 0xAA));
            CHECK(cupid_savedata_write_flash(&fixture.save, 0x2AAA, 0x55));
            CHECK(cupid_savedata_write_flash(&fixture.save, 0x5555, 0xA0));
            CHECK(cupid_savedata_write_flash(&fixture.save, 7, payload[0]));
            CHECK(fixture.save.programming);
            cupid_timing_tick(&fixture.timing, 30);
            CHECK(fixture.save.currentBank[7] == 0xFF);
            cupid_timing_tick(&fixture.timing, 1);
            CHECK(fixture.save.currentBank[7] == payload[0] && !fixture.save.programming);
            cupid_savedata_deinit(&fixture.save);
            scenes++;
        }
    printf("PASS backup Flash growth failure and recovery scenes=%u\n", scenes);
}

static unsigned request(uint16_t *wire, unsigned bits, unsigned block, bool write) {
    unsigned count = 0;
    wire[count++] = 1;
    wire[count++] = write ? 0 : 1;
    for (unsigned bit = bits; bit > 0; bit--)
        wire[count++] = (block >> (bit - 1)) & 1;
    if (write)
        for (unsigned bit = 0; bit < 64; bit++)
            wire[count++] = (payload[bit / 8] >> (7 - bit % 8)) & 1;
    wire[count++] = 0;
    return count;
}

static void transfer(struct CupidSavedata *save, const uint16_t *wire, unsigned count) {
    for (unsigned i = 0; i < count; i++)
        CHECK(cupid_savedata_write_eeprom(save, wire[i], count - i));
}

static void readback(struct CupidSavedata *save, const uint8_t *expected) {
    for (unsigned bit = 0; bit < 4; bit++)
        CHECK(cupid_savedata_read_eeprom(save) == 0);
    for (unsigned bit = 0; bit < 64; bit++)
        CHECK(cupid_savedata_read_eeprom(save) == ((expected[bit / 8] >> (7 - bit % 8)) & 1));
    CHECK(save->command == EEPROM_COMMAND_NULL);
}

static void eeprom(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned initial = 0; initial < 2; initial++)
            for (unsigned write = 0; write < 2; write++) {
                struct Fixture fixture;
                struct Snapshot snapshot;
                uint16_t wire[81];
                const unsigned count = request(wire, 14, 600, write != 0);
                fresh(&fixture, epoch, initial ? SAVE_AUTODETECT : SAVE_EEPROM512);
                if (!initial)
                    CHECK(cupid_savedata_write_eeprom(&fixture.save, wire[0], count));
                capture(&snapshot, &fixture);
                arm_failure();
                const unsigned failedBit = initial ? 0 : 1;
                CHECK(!cupid_savedata_write_eeprom(&fixture.save, wire[failedBit],
                                                   count - failedBit));
                CHECK(errno == ENOMEM && allocations == 1);
                snapshot.save.command = EEPROM_COMMAND_ABORTED;
                unchanged(&snapshot, &fixture);
                for (unsigned i = failedBit + 1; i < count; i++) {
                    CHECK(cupid_savedata_read_eeprom(&fixture.save) == 1);
                    CHECK(cupid_savedata_write_eeprom(&fixture.save, wire[i], count - i));
                    CHECK(!fixture.save.programming && allocations == 1);
                }
                snapshot.save.command = EEPROM_COMMAND_NULL;
                unchanged(&snapshot, &fixture);
                transfer(&fixture.save, wire, count);
                CHECK(fixture.save.type == SAVE_EEPROM && fixture.save.size == GBA_SIZE_EEPROM);
                CHECK(!fixture.save.typeForced);
                if (!initial)
                    CHECK(!memcmp(fixture.save.data, snapshot.bytes, GBA_SIZE_EEPROM512));
                if (write) {
                    CHECK(fixture.save.programming);
                    cupid_timing_tick(&fixture.timing, 96);
                    CHECK(fixture.save.data[600 * 8] == 0xFF);
                    cupid_timing_tick(&fixture.timing, 1);
                    CHECK(!memcmp(fixture.save.data + 600 * 8, payload, sizeof(payload)));
                    CHECK(fixture.save.dirty && !fixture.save.programming);
                    const unsigned readCount = request(wire, 14, 600, false);
                    transfer(&fixture.save, wire, readCount);
                    readback(&fixture.save, payload);
                } else {
                    const uint8_t blank[8] = {255, 255, 255, 255, 255, 255, 255, 255};
                    readback(&fixture.save, blank);
                    CHECK(!fixture.save.dirty);
                }
                cupid_savedata_deinit(&fixture.save);
                scenes++;
            }
    printf("PASS backup EEPROM failed-frame isolation and retry scenes=%u\n", scenes);
}

static struct CupidGBA *machine(unsigned epoch) {
    failAfter = -1;
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epochs[epoch];
    gba->timing.masterCycles = (uint32_t)epochs[epoch];
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    return gba;
}

static void run_dma(struct CupidGBA *gba, const uint16_t *wire, unsigned count) {
    memcpy(gba->memory.iwram + 0x100, wire, count * sizeof(*wire));
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x100);
    cupid_dma_write_dad(gba, 3, GBA_BASE_ROM2_EX);
    cupid_dma_write_cnt_lo(gba, 3, (uint16_t)count);
    cupid_dma_write_cnt(gba, 3, 0x8040);
    unsigned clocks = 0;
    while (gba->memory.dma[3].enable && clocks < 4096) {
        cupid_timing_tick(&gba->timing, 1);
        clocks++;
    }
    CHECK(clocks < 4096 && !gba->memory.dma[3].enable);
}

static void runtime(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++) {
        for (unsigned operation = 0; operation < 4; operation++) {
            struct CupidGBA *gba = machine(epoch);
            struct CupidSavedata *save = &gba->memory.savedata;
            struct CupidSavedata before = *save;
            arm_failure();
            if (operation == 0)
                CHECK(cupid_memory_load8(&gba->cpu, 0x0E001234, NULL) == 0xFF);
            else if (operation == 1)
                cupid_memory_store8(&gba->cpu, 0x0F001234, 0x72, NULL);
            else if (operation == 2)
                cupid_memory_store8(&gba->cpu, 0x0E005555, 0xAA, NULL);
            else
                cupid_memory_store16(&gba->cpu, GBA_BASE_ROM2_EX, 1, NULL);
            CHECK(errno == ENOMEM && allocations == 1);
            if (operation == 2)
                before.flashState = FLASH_STATE_ALLOCATION;
            CHECK(!memcmp(&before, save, sizeof(before)));
            if (operation == 0) {
                CHECK(cupid_memory_load8(&gba->cpu, 0x0E001234, NULL) == 0xFF);
                CHECK(save->type == SAVE_SRAM);
            } else if (operation == 1) {
                cupid_memory_store8(&gba->cpu, 0x0F001234, 0x72, NULL);
                CHECK(save->type == SAVE_SRAM && save->data[0x1234] == 0x72 && save->dirty);
            } else if (operation == 2) {
                cupid_memory_store8(&gba->cpu, 0x0E005555, 0xAA, NULL);
                CHECK(save->type == SAVE_FLASH512 && save->flashState == FLASH_STATE_START);
            } else {
                cupid_memory_store16(&gba->cpu, GBA_BASE_ROM2_EX, 1, NULL);
                CHECK(save->type == SAVE_EEPROM512 && save->command == EEPROM_COMMAND_NULL);
            }
            cupid_gba_destroy(gba);
            free(gba);
            scenes++;
        }
        for (unsigned initial = 0; initial < 2; initial++)
            for (unsigned write = 0; write < 2; write++) {
                struct CupidGBA *gba = machine(epoch);
                struct CupidSavedata *save = &gba->memory.savedata;
                uint16_t wire[81];
                uint8_t original[GBA_SIZE_EEPROM512];
                memset(original, 0x63, sizeof(original));
                if (!initial) {
                    CHECK(cupid_savedata_force_type(save, SAVE_EEPROM512));
                    save->typeForced = false;
                    memcpy(save->data, original, sizeof(original));
                }
                CHECK(cupid_savedata_set_programming_clocks(save, 97, 31, 43, 59));
                const unsigned count = request(wire, 14, 600, write != 0);
                arm_failure();
                run_dma(gba, wire, count);
                CHECK(allocations == 1 && !save->programming && !save->dirty);
                CHECK(save->command == EEPROM_COMMAND_NULL);
                CHECK(save->type == (initial ? SAVE_AUTODETECT : SAVE_EEPROM512));
                if (initial)
                    CHECK(!save->data && !save->size);
                else
                    CHECK(save->size == sizeof(original) &&
                          !memcmp(save->data, original, sizeof(original)));
                run_dma(gba, wire, count);
                CHECK(save->type == SAVE_EEPROM && save->size == GBA_SIZE_EEPROM);
                if (!initial)
                    CHECK(!memcmp(save->data, original, sizeof(original)));
                if (write) {
                    cupid_timing_tick(&gba->timing, 97);
                    CHECK(!memcmp(save->data + 600 * 8, payload, sizeof(payload)));
                    CHECK(save->dirty);
                } else {
                    const uint8_t blank[8] = {255, 255, 255, 255, 255, 255, 255, 255};
                    readback(save, blank);
                }
                cupid_gba_destroy(gba);
                free(gba);
                scenes++;
            }
    }
    printf("PASS backup CPU and DMA allocation recovery scenes=%u\n", scenes);
}

static void reject_flash_detection(struct CupidGBA *gba, uint32_t base) {
    struct CupidSavedata *save = &gba->memory.savedata;
    CHECK(cupid_savedata_set_programming_clocks(save, 97, 31, 43, 59));
    struct CupidSavedata before = *save;
    arm_failure();
    cupid_memory_store8(&gba->cpu, base + 0x5555, 0xAA, NULL);
    CHECK(errno == ENOMEM && allocations == 1);
    before.flashState = FLASH_STATE_ALLOCATION;
    CHECK(!memcmp(&before, save, sizeof(before)));
    CHECK(save->type == SAVE_AUTODETECT && !save->data && !save->size);
    CHECK(!save->currentBank && !save->typeForced && !save->dirty && !save->programming);
}

static void flash_pending(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned mirror = 0; mirror < 2; mirror++) {
            const uint32_t base = mirror ? 0x0F000000 : 0x0E000000;
            for (unsigned recovery = 0; recovery < 3; recovery++) {
                struct CupidGBA *gba = machine(epoch);
                struct CupidSavedata *save = &gba->memory.savedata;
                reject_flash_detection(gba, base);
                const struct CupidSavedata pending = *save;

                /* Reads and continued unlock writes must retain the observed family
                 * through repeated host failures, without accepting the failed AA. */
                arm_failure();
                CHECK(cupid_memory_load8(&gba->cpu, base + 0x1234, NULL) == 0xFF);
                CHECK(errno == ENOMEM && allocations == 1);
                CHECK(!memcmp(&pending, save, sizeof(pending)));
                arm_failure();
                cupid_memory_store8(&gba->cpu, base + 0x2AAA, 0x55, NULL);
                CHECK(errno == ENOMEM && allocations == 1);
                CHECK(!memcmp(&pending, save, sizeof(pending)));

                /* Other inference paths cannot replace a family already observed
                 * on the save bus, even before its backing bytes exist. */
                arm_failure();
                uint8_t rom[0xC8] = {0};
                memcpy(rom + 0xC0, "EEPROM_V", 8);
                CHECK(cupid_savedata_detect_type(save, rom, sizeof(rom)));
                CHECK(cupid_savedata_write_eeprom(save, 1, 81));
                cupid_memory_store16(&gba->cpu, GBA_BASE_ROM2_EX, 1, NULL);
                uint16_t wire[81];
                const unsigned count = request(wire, 14, 600, true);
                run_dma(gba, wire, count);
                CHECK(allocations == 0 && !memcmp(&pending, save, sizeof(pending)));
                failAfter = -1;

                if (recovery == 1)
                    CHECK(cupid_memory_load8(&gba->cpu, base + 0x1234, NULL) == 0xFF);
                if (recovery == 2)
                    cupid_memory_store8(&gba->cpu, base + 0x5555, 0xAA, NULL);
                else
                    cupid_memory_store8(&gba->cpu, base + 0x2AAA, 0x55, NULL);
                CHECK(allocations == 1 && save->type == SAVE_FLASH512);
                CHECK(save->size == GBA_SIZE_FLASH512 && save->currentBank == save->data);
                CHECK(!save->typeForced && save->command == FLASH_COMMAND_NONE);
                erased(save);
                if (recovery == 2) {
                    CHECK(save->flashState == FLASH_STATE_START);
                } else {
                    CHECK(save->flashState == FLASH_STATE_RAW);
                    cupid_memory_store8(&gba->cpu, base + 0x5555, 0xA0, NULL);
                    cupid_memory_store8(&gba->cpu, base + 7, payload[0], NULL);
                    CHECK(!save->programming && !save->dirty && save->data[7] == 0xFF);
                    cupid_memory_store8(&gba->cpu, base + 0x5555, 0xAA, NULL);
                }
                cupid_memory_store8(&gba->cpu, base + 0x2AAA, 0x55, NULL);
                cupid_memory_store8(&gba->cpu, base + 0x5555, 0xA0, NULL);
                cupid_memory_store8(&gba->cpu, base + 7, payload[0], NULL);
                CHECK(save->programming);
                cupid_timing_tick(&gba->timing, 30);
                CHECK(save->data[7] == 0xFF);
                cupid_timing_tick(&gba->timing, 1);
                CHECK(!save->programming && save->dirty && save->data[7] == payload[0]);
                CHECK(cupid_memory_load8(&gba->cpu, base + 7, NULL) == payload[0]);
                cupid_gba_destroy(gba);
                free(gba);
                scenes++;
            }

            for (unsigned action = 0; action < 6; action++) {
                struct CupidGBA *gba = machine(epoch);
                struct CupidSavedata *save = &gba->memory.savedata;
                reject_flash_detection(gba, base);
                const struct CupidSavedata pending = *save;
                for (unsigned type = 2; type < sizeof(types) / sizeof(*types); type++) {
                    arm_failure();
                    CHECK(!cupid_savedata_force_type(save, types[type]));
                    CHECK(errno == ENOMEM && allocations == 1);
                    CHECK(!memcmp(&pending, save, sizeof(pending)));
                }
                struct CupidPlatformSave selection = {SAVE_FLASH512, FLASH_PROFILE_SST39VF512};
                arm_failure();
                CHECK(!cupid_platform_save_configure(save, &selection));
                CHECK(errno == ENOMEM && allocations == 1);
                CHECK(!memcmp(&pending, save, sizeof(pending)));
                failAfter = -1;
                switch (action) {
                    case 0:
                        cupid_savedata_reset(save);
                        break;
                    case 1:
                        cupid_savedata_deinit(save);
                        break;
                    case 2:
                        CHECK(cupid_savedata_force_type(save, SAVE_AUTODETECT));
                        break;
                    case 3:
                        CHECK(cupid_savedata_force_type(save, SAVE_SRAM));
                        break;
                    case 4:
                        CHECK(cupid_platform_save_configure(save, &selection));
                        CHECK(save->flashProfile == FLASH_PROFILE_SST39VF512);
                        break;
                    case 5:
                        selection.type = SAVE_AUTODETECT;
                        selection.profile = FLASH_PROFILE_DEFAULT;
                        CHECK(cupid_platform_save_configure(save, &selection));
                        CHECK(!memcmp(&pending, save, sizeof(pending)));
                        break;
                }
                if (action < 3) {
                    CHECK(save->type == SAVE_AUTODETECT && !save->data && !save->size);
                    CHECK(save->flashState == FLASH_STATE_RAW && !save->typeForced);
                    CHECK(save->command == FLASH_COMMAND_NONE);
                }
                CHECK(cupid_memory_load8(&gba->cpu, base + 0x1234, NULL) == 0xFF);
                CHECK(save->type == (action >= 4 ? SAVE_FLASH512 : SAVE_SRAM));
                CHECK(save->typeForced == (action == 3 || action == 4));
                CHECK(save->flashState == FLASH_STATE_RAW);
                erased(save);
                cupid_gba_destroy(gba);
                free(gba);
                scenes++;
            }
        }
    printf("PASS backup pending Flash allocation ownership and recovery scenes=%u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "initial"))
        RUN(initial);
    else if (!strcmp(argv[1], "replacement"))
        RUN(replacement);
    else if (!strcmp(argv[1], "configuration"))
        RUN(configuration);
    else if (!strcmp(argv[1], "flash"))
        RUN(flash);
    else if (!strcmp(argv[1], "eeprom"))
        RUN(eeprom);
    else if (!strcmp(argv[1], "runtime"))
        RUN(runtime);
    else if (!strcmp(argv[1], "flash-pending"))
        RUN(flash_pending);
    else
        CHECK(false);
    return 0;
}
