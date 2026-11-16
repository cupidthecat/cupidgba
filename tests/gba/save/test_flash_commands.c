/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_flash_commands.c
 * @author    cupidthecat
 * @brief     SST erase command rejection, recovery and mapped CPU/DMA transactions.
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
#include "cupid/isa-inlines.h"

static const uint64_t epochs[] = {0, INT32_MAX - 19u, UINT32_MAX - 19u, UINT64_C(0x300000000)};
static const uint16_t locations[] = {0, 1, 0x2AAA, 0x5555, 0x8000, 0xAAAA, 0xD555, 0xFFFF};

static uint16_t alias(unsigned address, unsigned mask, unsigned bit) {
    return (uint16_t)(address | (((mask >> bit) & 1) << 15));
}

static void command(struct CupidSavedata *save, uint8_t value, unsigned mask) {
    cupid_savedata_write_flash(save, alias(0x5555, mask, 0), 0xAA);
    cupid_savedata_write_flash(save, alias(0x2AAA, mask, 1), 0x55);
    cupid_savedata_write_flash(save, alias(0x5555, mask, 2), value);
}

static void initialize(struct CupidSavedata *save) {
    cupid_savedata_init(save);
    cupid_savedata_force_type(save, SAVE_FLASH512);
    CHECK(cupid_savedata_set_flash_profile(save, FLASH_PROFILE_SST39VF512));
}

static void prepare(struct CupidSavedata *save, unsigned phase, unsigned mask) {
    cupid_savedata_write_flash(save, 0xFFFF, 0xF0);
    command(save, 0x80, mask);
    if (phase >= 1)
        cupid_savedata_write_flash(save, alias(0x5555, mask, 3), 0xAA);
    if (phase >= 2)
        cupid_savedata_write_flash(save, alias(0x2AAA, mask, 4), 0x55);
    CHECK(save->command == FLASH_COMMAND_ERASE);
}

static bool valid(unsigned phase, unsigned address, unsigned value) {
    /* Table 4's six bus writes define the accepted erase sequence. */
    if (value == 0xF0)
        return false;
    if (phase == 0)
        return value == 0xAA && (address == 0x5555 || address == 0xD555);
    if (phase == 1)
        return value == 0x55 && (address == 0x2AAA || address == 0xAAAA);
    return value == 0x30 || (value == 0x10 && (address == 0x5555 || address == 0xD555));
}

static void rejected(struct CupidSavedata *save, unsigned phase, unsigned mask, uint16_t address,
                     uint8_t value) {
    CHECK(!valid(phase, address, value));
    prepare(save, phase, mask);
    cupid_savedata_write_flash(save, address, value);
    CHECK(save->command == FLASH_COMMAND_NONE && save->flashState == FLASH_STATE_RAW);
    CHECK(!save->programming && !save->dirty);
}

static void rejection(unsigned phase) {
    struct CupidSavedata save;
    initialize(&save);
    uint8_t expected[65536];
    for (unsigned address = 0; address < sizeof(expected); address++)
        expected[address] = (uint8_t)(address * 13 + (address >> 8));
    memcpy(save.data, expected, sizeof(expected));
    const uint8_t next[] = {0xAA, 0x55, 0x10};
    unsigned scenes = 0;
    /* Sweep every address, including every possible ignored-bit prefix. */
    for (unsigned address = 0; address < 65536; address++) {
        if (valid(phase, address, next[phase]))
            continue;
        rejected(&save, phase, address >> 11, (uint16_t)address, next[phase]);
        scenes++;
    }
    /* Sweep every data byte at both command aliases and unrelated addresses. */
    for (unsigned mask = 0; mask < 32; mask++)
        for (unsigned location = 0; location < sizeof(locations) / sizeof(*locations); location++)
            for (unsigned value = 0; value < 256; value++) {
                if (valid(phase, locations[location], value))
                    continue;
                rejected(&save, phase, mask, locations[location], (uint8_t)value);
                scenes++;
            }
    CHECK(scenes == (phase == 2 ? 130750u : 131006u));
    CHECK(!memcmp(save.data, expected, sizeof(expected)));
    CHECK(save.flashBank == 0 && save.currentBank == save.data && save.size == sizeof(expected));
    for (unsigned address = 0; address < sizeof(expected); address++)
        CHECK(cupid_savedata_read_flash(&save, (uint16_t)address) == expected[address]);
    cupid_savedata_deinit(&save);
    printf("flash invalid erase cycle %u scenes=%u\n", phase + 4, scenes);
}

static void invalid_cycle(unsigned phase, unsigned error, uint16_t *address, uint8_t *value) {
    const uint16_t wrongAddress[] = {0x5554, 0x2AAB, 0xD554};
    const uint8_t rightValue[] = {0xAA, 0x55, 0x10};
    const uint16_t rightAddress[] = {0xD555, 0xAAAA, 0xD555};
    *address = error ? rightAddress[phase] : wrongAddress[phase];
    *value = error ? 0x81 : rightValue[phase];
}

static void recovery(void) {
    struct CupidSavedata save;
    initialize(&save);
    uint8_t original[65536], expected[65536];
    for (unsigned byte = 0; byte < sizeof(original); byte++)
        original[byte] = (uint8_t)(byte * 7 + (byte >> 8));
    /* Byte programming requires an erased target sector. */
    memset(original + 0xA000, 0xFF, 0x1000);
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned mask = 0; mask < 32; mask++)
            for (unsigned phase = 0; phase < 3; phase++)
                for (unsigned error = 0; error < 2; error++)
                    for (unsigned operation = 0; operation < 4; operation++) {
                        save.localClock = epochs[epoch];
                        memcpy(expected, original, sizeof(expected));
                        memcpy(save.data, expected, sizeof(expected));
                        save.dirty = false;
                        uint16_t address;
                        uint8_t value;
                        invalid_cycle(phase, error, &address, &value);
                        rejected(&save, phase, mask, address, value);
                        CHECK(!memcmp(save.data, expected, sizeof(expected)));
                        if (!operation) {
                            command(&save, 0x90, mask);
                            CHECK(cupid_savedata_read_flash(&save, 0) == 0xBF);
                            CHECK(cupid_savedata_read_flash(&save, 1) == 0xD4);
                            CHECK(cupid_savedata_read_flash(&save, 0x8000) == expected[0x8000]);
                            cupid_savedata_write_flash(&save, 0x8123, 0xF0);
                            CHECK(save.command == FLASH_COMMAND_NONE && !save.dirty);
                        } else {
                            const uint32_t duration = operation == 1   ? 235
                                                      : operation == 2 ? 301990
                                                                       : 1174406;
                            if (operation == 1) {
                                command(&save, 0xA0, mask);
                                cupid_savedata_write_flash(&save, 0xA123, 0xF0);
                            } else {
                                prepare(&save, 2, mask);
                                cupid_savedata_write_flash(
                                    &save, operation == 2 ? 0x5234 : alias(0x5555, mask, 0),
                                    operation == 2 ? 0x30 : 0x10);
                            }
                            CHECK(save.programming && !save.dirty);
                            CHECK(save.readyClock == epochs[epoch] + duration);
                            CHECK(!memcmp(save.data, expected, sizeof(expected)));
                            struct CupidSavedata before;
                            memcpy(&before, &save, sizeof(before));
                            /* Malformed commands and F0 cannot abort an accepted operation. */
                            command(&save, 0x80, mask);
                            cupid_savedata_write_flash(&save, address, value);
                            cupid_savedata_write_flash(&save, 0xFFFF, 0xF0);
                            CHECK(!memcmp(&save, &before, sizeof(save)));
                            cupid_savedata_advance_clock(&save, duration - 1);
                            CHECK(save.programming && !save.dirty);
                            CHECK(!memcmp(save.data, expected, sizeof(expected)));
                            cupid_savedata_advance_clock(&save, 1);
                            CHECK(!save.programming && save.dirty);
                            if (operation == 1)
                                expected[0xA123] = 0xF0;
                            else if (operation == 2)
                                memset(expected + 0x5000, 0xFF, 0x1000);
                            else
                                memset(expected, 0xFF, sizeof(expected));
                        }
                        CHECK(!memcmp(save.data, expected, sizeof(expected)));
                        scenes++;
                    }
    CHECK(scenes == 3072);
    cupid_savedata_deinit(&save);
    printf("flash invalid erase recovery scenes=%u\n", scenes);
}

static void store(struct CupidGBA *gba, bool thumb, uint32_t address, uint8_t value) {
    gba->cpu.gprs[0] = value;
    gba->cpu.gprs[1] = address;
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.cpsr.t = thumb;
    if (thumb)
        CupidThumbWritePC(&gba->cpu);
    else
        CupidARMWritePC(&gba->cpu);
    cupid_gba_step(gba);
    cupid_gba_process_events(gba);
}

static uint32_t dma_read(struct CupidGBA *gba, uint32_t address, unsigned width) {
    memset(gba->memory.iwram + 0xF0, 0xA5, 48);
    cupid_dma_write_sad(gba, 3, address);
    cupid_dma_write_dad(gba, 3, GBA_BASE_IWRAM + 0x100);
    cupid_dma_write_cnt_lo(gba, 3, 1);
    cupid_dma_write_cnt(gba, 3, width == 4 ? 0x8400 : 0x8000);
    unsigned clocks = 0;
    while ((gba->memory.dma[3].enable || gba->memory.dmaRunning || gba->cpuBlocked) &&
           clocks < 512) {
        cupid_timing_tick(&gba->timing, 1);
        clocks++;
    }
    CHECK(clocks < 512 && !gba->memory.dma[3].enable && !gba->cpuBlocked);
    uint32_t result = 0;
    for (unsigned byte = 0; byte < width; byte++)
        result |= (uint32_t)gba->memory.iwram[0x100 + byte] << (byte * 8);
    for (unsigned byte = 0; byte < 48; byte++)
        if (byte < 16 || byte >= 16 + width)
            CHECK(gba->memory.iwram[0xF0 + byte] == 0xA5);
    return result;
}

static void guest_command(struct CupidGBA *gba, bool thumb, uint32_t port, unsigned mask,
                          uint8_t value) {
    store(gba, thumb, port + alias(0x5555, mask, 0), 0xAA);
    store(gba, thumb, port + alias(0x2AAA, mask, 1), 0x55);
    store(gba, thumb, port + alias(0x5555, mask, 2), value);
}

static void guest(void) {
    uint8_t expected[65536];
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned phase = 0; phase < 3; phase++)
            for (unsigned error = 0; error < 2; error++)
                for (unsigned thumb = 0; thumb < 2; thumb++)
                    for (unsigned mirror = 0; mirror < 2; mirror++)
                        for (unsigned high = 0; high < 2; high++) {
                            struct CupidGBA *gba = calloc(1, sizeof(*gba));
                            CHECK(gba);
                            cupid_gba_create(gba);
                            cupid_gba_reset(gba);
                            cupid_timing_clear(&gba->timing);
                            gba->timing.globalCycles = epochs[epoch];
                            gba->timing.masterCycles = (uint32_t)epochs[epoch];
                            gba->cpu.cpsr.i = 1;
                            gba->cpu.irqDisableLatch = true;
                            struct CupidSavedata *save = &gba->memory.savedata;
                            cupid_savedata_force_type(save, SAVE_FLASH512);
                            CHECK(cupid_savedata_set_flash_profile(save, FLASH_PROFILE_SST39VF512));
                            memset(expected, 0x5A, sizeof(expected));
                            memset(expected + 0xA000, 0xFF, 0x1000);
                            memcpy(save->data, expected, sizeof(expected));
                            const uint32_t arm[] = {0xE5C10000, 0xE1A00000, 0xEAFFFFFE};
                            const uint16_t half[] = {0x7008, 0x46C0, 0xE7FE};
                            memcpy(gba->memory.iwram,
                                   thumb ? (const void *)half : (const void *)arm,
                                   thumb ? sizeof(half) : sizeof(arm));
                            const uint32_t port = mirror ? 0x0F000000 : 0x0E000000;
                            const unsigned mask = high ? 31 : 0;
                            guest_command(gba, thumb, port, mask, 0x80);
                            if (phase >= 1)
                                store(gba, thumb, port + alias(0x5555, mask, 3), 0xAA);
                            if (phase >= 2)
                                store(gba, thumb, port + alias(0x2AAA, mask, 4), 0x55);
                            uint16_t address;
                            uint8_t value;
                            invalid_cycle(phase, error, &address, &value);
                            store(gba, thumb, port + address, value);
                            CHECK(!save->programming && !save->dirty);
                            CHECK(!memcmp(save->data, expected, sizeof(expected)));
                            /* A fresh ID command must be accepted after rejection. */
                            guest_command(gba, thumb, port, mask, 0x90);
                            CHECK(dma_read(gba, port, 2) == 0xBFBF);
                            CHECK(cupid_memory_load8(&gba->cpu, port + 1, NULL) == 0xD4);
                            CHECK(dma_read(gba, port + 0x8000, 4) == UINT32_C(0x5A5A5A5A));
                            store(gba, thumb, port + 0x8123, 0xF0);
                            guest_command(gba, thumb, port, mask, 0xA0);
                            store(gba, thumb, port + 0xA120, 0x55);
                            const uint64_t accepted = cupid_timing_global_time(&gba->timing);
                            CHECK(save->programming && save->readyClock == accepted + 235);
                            CHECK((dma_read(gba, port + 0xA120, 2) & 0x8080) == 0x8080);
                            const uint64_t now = cupid_timing_global_time(&gba->timing);
                            CHECK(now < save->readyClock);
                            cupid_timing_tick(&gba->timing, (int32_t)(save->readyClock - now - 1));
                            CHECK(save->programming && !save->dirty);
                            CHECK(!memcmp(save->data, expected, sizeof(expected)));
                            cupid_timing_tick(&gba->timing, 1);
                            CHECK(!save->programming && save->dirty);
                            CHECK(dma_read(gba, port + 0xA120, 4) == UINT32_C(0x55555555));
                            expected[0xA120] = 0x55;
                            CHECK(!memcmp(save->data, expected, sizeof(expected)));
                            cupid_gba_destroy(gba);
                            free(gba);
                            scenes++;
                        }
    CHECK(scenes == 192);
    printf("flash invalid erase ARM/Thumb/DMA scenes=%u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "fourth"))
        rejection(0);
    else if (!strcmp(argv[1], "fifth"))
        rejection(1);
    else if (!strcmp(argv[1], "sixth"))
        rejection(2);
    else if (!strcmp(argv[1], "recovery"))
        recovery();
    else if (!strcmp(argv[1], "guest"))
        guest();
    else
        CHECK(false);
    return 0;
}
