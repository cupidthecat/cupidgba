/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_flash_addresses.c
 * @brief SST command address decoding with full program/sector targets and mapped bus controls.
 */
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

static const uint64_t epochs[] = {0, INT32_MAX - 19u, UINT32_MAX - 19u, UINT64_C(0x300000000)};
static const uint8_t payloads[] = {0, 0xFF, 0xAA, 0x55, 0xA0, 0x90, 0xF0, 0x80, 0x10, 0x30};

static uint16_t alias(unsigned base, unsigned mask, unsigned bit) {
    return (uint16_t)(base + ((mask >> bit) & 1) * 0x8000);
}

static void command(struct CupidSavedata *save, unsigned value, unsigned mask) {
    cupid_savedata_write_flash(save, alias(0x5555, mask, 0), 0xAA);
    cupid_savedata_write_flash(save, alias(0x2AAA, mask, 1), 0x55);
    cupid_savedata_write_flash(save, alias(0x5555, mask, 2), (uint8_t)value);
}

static void initialize(struct CupidSavedata *save, unsigned epoch) {
    cupid_savedata_init(save);
    cupid_savedata_force_type(save, SAVE_FLASH512);
    CHECK(cupid_savedata_set_flash_profile(save, FLASH_PROFILE_SST39VF512));
    save->localClock = epochs[epoch];
}

static void ids(void) {
    unsigned scenes = 0;
    for (unsigned profile = 0; profile < 3; profile++)
        for (unsigned mask = 0; mask < 8; mask++) {
            struct CupidSavedata save;
            initialize(&save, 0);
            if (profile) {
                CHECK(cupid_savedata_set_flash_profile(&save, FLASH_PROFILE_DEFAULT));
                if (profile == 2)
                    cupid_savedata_force_type(&save, SAVE_FLASH1M);
            }
            memset(save.data, 0x3C, save.size);
            command(&save, 0x90, mask);
            if (!profile || !mask) {
                CHECK(save.command == FLASH_COMMAND_ID);
                CHECK(cupid_savedata_read_flash(&save, 0) == (!profile       ? 0xBF
                                                              : profile == 1 ? 0x32
                                                                             : 0x62));
                CHECK(cupid_savedata_read_flash(&save, 1) == (!profile       ? 0xD4
                                                              : profile == 1 ? 0x1B
                                                                             : 0x13));
            } else {
                /* Other chip profiles keep their existing full-address decoder. */
                CHECK(save.command == FLASH_COMMAND_NONE);
                CHECK(cupid_savedata_read_flash(&save, 0) == 0x3C);
            }
            /* Command aliases must not introduce an ID-read mirror or change
             * the selected array fallback outside the qualified ID addresses. */
            CHECK(cupid_savedata_read_flash(&save, 0x8000) == 0x3C);
            CHECK(save.flashBank == 0 && !save.programming && !save.dirty);
            if (!profile) {
                cupid_savedata_write_flash(&save, 0x8123, 0xF0);
                CHECK(save.command == FLASH_COMMAND_NONE && save.flashState == FLASH_STATE_RAW);
                command(&save, 0xB0, mask);
                cupid_savedata_write_flash(&save, 0, 1);
                CHECK(save.flashBank == 0 && save.size == 65536);
                command(&save, 0x90, mask);
                CHECK(cupid_savedata_read_flash(&save, 0) == 0xBF);
                command(&save, 0xF0, mask);
                CHECK(cupid_savedata_read_flash(&save, 0) == 0x3C);
            }
            for (size_t byte = 0; byte < save.size; byte++)
                CHECK(save.data[byte] == 0x3C);
            cupid_savedata_deinit(&save);
            scenes++;
        }
    CHECK(scenes == 24);
    printf("flash command identity/profile scenes=%u\n", scenes);
}

static void finish(struct CupidSavedata *save, uint32_t duration, const uint8_t before[65536],
                   const uint8_t after[65536]) {
    CHECK(save->programming && !save->dirty);
    CHECK(!memcmp(save->data, before, 65536));
    struct CupidSavedata snapshot;
    memcpy(&snapshot, save, sizeof(snapshot));
    for (unsigned mask = 0; mask < 8; mask++)
        command(save, 0x90, mask);
    cupid_savedata_write_flash(save, 0xD555, 0xF0);
    CHECK(!memcmp(&snapshot, save, sizeof(snapshot)));
    cupid_savedata_advance_clock(save, duration - 1);
    CHECK(save->programming && !save->dirty && !memcmp(save->data, before, 65536));
    cupid_savedata_advance_clock(save, 1);
    CHECK(!save->programming && save->dirty && !memcmp(save->data, after, 65536));
}

static void program(void) {
    const uint16_t addresses[] = {0x0123, 0x5555, 0x2AAA, 0x7FFF, 0x8123, 0xD555, 0xAAAA, 0xFFFF};
    uint8_t before[65536], after[65536];
    memset(before, 0xFF, sizeof(before));
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned mask = 0; mask < 8; mask++)
            for (unsigned address = 0; address < 8; address++)
                for (unsigned byte = 0; byte < sizeof(payloads); byte++) {
                    struct CupidSavedata save;
                    initialize(&save, epoch);
                    command(&save, 0xA0, mask);
                    cupid_savedata_write_flash(&save, addresses[address], payloads[byte]);
                    CHECK(save.programming && save.programOffset == addresses[address] &&
                          save.programLength == 1);
                    CHECK(save.readyClock == epochs[epoch] + 235 &&
                          save.programData[0] == payloads[byte]);
                    memcpy(after, before, sizeof(after));
                    after[addresses[address]] = payloads[byte];
                    finish(&save, 235, before, after);
                    cupid_savedata_deinit(&save);
                    scenes++;
                }
    CHECK(scenes == 2560);
    printf("flash command program/payload scenes=%u\n", scenes);
}

static void erase(bool chip) {
    const uint16_t offsets[] = {0, 0x555, 0xFFF};
    uint8_t before[65536], after[65536];
    memset(before, 0x3C, sizeof(before));
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned mask = 0; mask < (chip ? 64u : 32u); mask++)
            for (unsigned sector = 0; sector < (chip ? 1u : 16u); sector++)
                for (unsigned offset = 0; offset < (chip ? 1u : 3u); offset++) {
                    struct CupidSavedata save;
                    initialize(&save, epoch);
                    memcpy(save.data, before, sizeof(before));
                    command(&save, 0x80, mask);
                    cupid_savedata_write_flash(&save, alias(0x5555, mask, 3), 0xAA);
                    cupid_savedata_write_flash(&save, alias(0x2AAA, mask, 4), 0x55);
                    cupid_savedata_write_flash(&save,
                                               chip ? alias(0x5555, mask, 5)
                                                    : (uint16_t)(sector * 4096 + offsets[offset]),
                                               chip ? 0x10 : 0x30);
                    const unsigned first = chip ? 0 : sector * 4096;
                    const unsigned length = chip ? 65536 : 4096;
                    const uint32_t duration = chip ? 1174406 : 301990;
                    CHECK(save.programming && save.erasing && save.programOffset == first &&
                          save.programLength == length);
                    CHECK(save.readyClock == epochs[epoch] + duration);
                    memcpy(after, before, sizeof(after));
                    memset(after + first, 0xFF, length);
                    finish(&save, duration, before, after);
                    cupid_savedata_deinit(&save);
                    scenes++;
                }
    CHECK(scenes == (chip ? 256 : 6144));
    printf("flash command %s scenes=%u\n", chip ? "chip-erase" : "sector-erase", scenes);
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

static void guest(void) {
    uint8_t expected[65536];
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned mask = 0; mask < 8; mask++)
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
                        cupid_savedata_force_type(&gba->memory.savedata, SAVE_FLASH512);
                        CHECK(cupid_savedata_set_flash_profile(&gba->memory.savedata,
                                                               FLASH_PROFILE_SST39VF512));
                        const uint32_t arm[] = {0xE5C10000, 0xE1A00000, 0xEAFFFFFE};
                        const uint16_t half[] = {0x7008, 0x46C0, 0xE7FE};
                        memcpy(gba->memory.iwram, thumb ? (const void *)half : (const void *)arm,
                               thumb ? sizeof(half) : sizeof(arm));
                        const uint32_t port = mirror ? 0x0F000000 : 0x0E000000;
                        const uint16_t target = (uint16_t)(0x0120 + high * 0x8000);
                        store(gba, thumb, port + alias(0x5555, mask, 0), 0xAA);
                        store(gba, thumb, port + alias(0x2AAA, mask, 1), 0x55);
                        store(gba, thumb, port + alias(0x5555, mask, 2), 0xA0);
                        store(gba, thumb, port + target, 0x5A);
                        struct CupidSavedata *save = &gba->memory.savedata;
                        const uint64_t accepted = cupid_timing_global_time(&gba->timing);
                        CHECK(save->programming && save->programOffset == target &&
                              save->readyClock == accepted + 235);
                        CHECK((dma_read(gba, port + target, 2) & 0x8080) == 0x8080);
                        CHECK((dma_read(gba, port + target, 4) & UINT32_C(0x80808080)) ==
                              UINT32_C(0x80808080));
                        const uint64_t now = cupid_timing_global_time(&gba->timing);
                        CHECK(now < save->readyClock);
                        cupid_timing_tick(&gba->timing, (int32_t)(save->readyClock - now - 1));
                        CHECK(save->programming && !save->dirty && save->data[target] == 0xFF);
                        cupid_timing_tick(&gba->timing, 1);
                        CHECK(!save->programming && save->dirty);
                        CHECK(dma_read(gba, port + target, 2) == 0x5A5A);
                        CHECK(dma_read(gba, port + target, 4) == UINT32_C(0x5A5A5A5A));
                        memset(expected, 0xFF, sizeof(expected));
                        expected[target] = 0x5A;
                        CHECK(!memcmp(save->data, expected, sizeof(expected)));
                        cupid_gba_destroy(gba);
                        free(gba);
                        scenes++;
                    }
    CHECK(scenes == 256);
    printf("flash command ARM/Thumb/DMA scenes=%u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "ids"))
        RUN(ids);
    else if (!strcmp(argv[1], "program"))
        RUN(program);
    else if (!strcmp(argv[1], "sector"))
        erase(false);
    else if (!strcmp(argv[1], "chip"))
        erase(true);
    else if (!strcmp(argv[1], "guest"))
        RUN(guest);
    else
        CHECK(false);
    return 0;
}
