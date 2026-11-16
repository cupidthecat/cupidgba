/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_opcode_regions.c
 * @author    cupidthecat
 * @brief     Physical opcode fetches across adjacent mapped execution regions.
 * @date      2026-11-16
 *
 * - Exercise ARM/Thumb pipeline refills and continued guest execution.
 * - Keep decoded internal-memory aliases and per-device fetch waits intact.
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
#include <string.h>

enum Boundary {
    BOUNDARY_EWRAM_IWRAM,
    BOUNDARY_PALETTE_VRAM,
    BOUNDARY_VRAM_OAM,
    BOUNDARY_OAM_ROM,
    BOUNDARY_COUNT
};

static const uint32_t boundaries[] = {GBA_BASE_IWRAM, GBA_BASE_VRAM, GBA_BASE_OAM, GBA_BASE_ROM0};
static const uint64_t epochs[] = {0, UINT32_MAX - UINT64_C(14), UINT64_C(0x300000000)};

static struct CupidGBA *machine(uint64_t epoch) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 0x40000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 0x40000;
    gba->memory.romMask = 0x3FFFF;
    cupid_gba_reset(gba);
    /* Display bus tests use genuine CPU requests during forced blank. This
     * keeps unrelated pixel-port arbitration out of the fixture's clocks. */
    cupid_io_write(gba, REG_DISPCNT, 0x0080);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    gba->audio.lastSample = gba->audio.sampleOrigin = epoch;
    gba->audio.nextSample = epoch + 512;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void write_opcode(uint8_t *destination, unsigned width, uint32_t opcode) {
    memcpy(destination, &opcode, width);
}

static uint32_t opcode(unsigned width, unsigned slot) {
    static const uint32_t arm[] = {0xE3A00010u, 0xE3A00011u, 0xE3A00022u,
                                   0xE3A01044u, 0xE3A00033u, 0xE3A01055u};
    static const uint16_t thumb[] = {0x2010, 0x2011, 0x2022, 0x2144, 0x2033, 0x2155};
    CHECK(slot < 6);
    return width == 2 ? thumb[slot] : arm[slot];
}

/* Plant different opcodes in both address-decoded devices and the old view's
 * wrapped source bytes. Data reads establish the target value independently. */
static void seed_boundary(struct CupidGBA *gba, unsigned boundary, unsigned width) {
    uint8_t *last, *wrapped, *next;
    switch (boundary) {
        case BOUNDARY_EWRAM_IWRAM:
            last = gba->memory.ewram + GBA_SIZE_EWRAM - width;
            wrapped = gba->memory.ewram;
            next = gba->memory.iwram;
            break;
        case BOUNDARY_PALETTE_VRAM:
            last = (uint8_t *)gba->video.palette + GBA_SIZE_PALETTE - width;
            wrapped = (uint8_t *)gba->video.palette;
            next = (uint8_t *)gba->video.vram;
            break;
        case BOUNDARY_VRAM_OAM:
            last = (uint8_t *)gba->video.vram + GBA_SIZE_VRAM - width;
            wrapped = (uint8_t *)gba->video.vram + 0x10000;
            next = (uint8_t *)gba->video.oam.raw;
            break;
        default:
            CHECK(boundary == BOUNDARY_OAM_ROM);
            last = (uint8_t *)gba->video.oam.raw + GBA_SIZE_OAM - width;
            wrapped = (uint8_t *)gba->video.oam.raw;
            next = gba->memory.rom;
            break;
    }
    write_opcode(last - width, width, opcode(width, 0));
    write_opcode(last, width, opcode(width, 1));
    write_opcode(next, width, opcode(width, 2));
    write_opcode(next + width, width, opcode(width, 3));
    write_opcode(wrapped, width, opcode(width, 4));
    write_opcode(wrapped + width, width, opcode(width, 5));
}

static unsigned prime(struct CupidGBA *gba, uint32_t address, unsigned width, bool timed) {
    _CupidSetMode(&gba->cpu, width == 2 ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = (int32_t)address;
    gba->cpu.memory.timed = timed;
    int cycles;
    if (width == 2)
        cycles = CupidThumbWritePC(&gba->cpu);
    else
        cycles = CupidARMWritePC(&gba->cpu);
    gba->cpu.memory.timed = false;
    CHECK(cycles >= 0);
    return (unsigned)cycles;
}

/* A normal GBA step always enables bus callbacks. Exercise the legacy
 * untimed CPU step separately, with its original fetch callback restored. */
static void issue_guest(struct CupidGBA *gba, bool timed) {
    if (timed) {
        cupid_gba_step(gba);
        return;
    }
    struct CupidARM *cpu = &gba->cpu;
    void (*const previousFetch)(struct CupidARM *, uint32_t, unsigned, bool) = cpu->memory.fetch;
    CHECK(previousFetch);
    cpu->memory.fetch = NULL;
    if (cpu->executionMode == MODE_THUMB)
        cupid_thumb_step(cpu);
    else
        cupid_arm_step(cpu);
    cpu->memory.fetch = previousFetch;
}

static uint32_t mapped_data(struct CupidGBA *gba, uint32_t address, unsigned width) {
    if (width == 2)
        return cupid_memory_load16(&gba->cpu, address, NULL);
    return cupid_memory_load32(&gba->cpu, address, NULL);
}

struct Issued {
    uint32_t address[4], value[4];
    unsigned count;
};

static void observe_issue(const struct CupidARM *cpu, void *context) {
    struct Issued *issued = context;
    CHECK(issued->count < 4);
    issued->address[issued->count] = cupid_arm_instruction_address(cpu);
    issued->value[issued->count] = cpu->prefetch[0];
    issued->count++;
}

static void check_idle_bus(struct CupidGBA *gba) {
    CHECK(!gba->memory.busBusy && !gba->memory.dmaRunning && !gba->memory.busLocked);
    CHECK(!gba->irq.cpuClockPaused);
}

static void adjacent_refills_select_target_opcodes(void) {
    unsigned scenes = 0;
    for (unsigned boundary = 0; boundary < BOUNDARY_COUNT; ++boundary)
        for (unsigned width = 2; width <= 4; width += 2)
            for (unsigned timed = 0; timed < 2; ++timed)
                for (unsigned a1 = 0; a1 < (width == 4 ? 2u : 1u); ++a1)
                    for (unsigned epoch = 0; epoch < (boundary == 0 ? 3u : 1u); ++epoch) {
                        struct CupidGBA *gba = machine(epochs[epoch]);
                        seed_boundary(gba, boundary, width);
                        const uint32_t next = boundaries[boundary];
                        const uint32_t start = next - width + a1 * 2;
                        CHECK(mapped_data(gba, next, width) == opcode(width, 2));
                        prime(gba, start, width, timed != 0);
                        CHECK(gba->cpu.prefetch[0] == opcode(width, 1));
                        CHECK(gba->cpu.prefetch[1] == opcode(width, 2));
                        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == start + width);
                        CHECK(gba->memory.activeRegion == (int)(next >> 24));
                        CHECK(gba->lastJump == start);
                        if (boundary == BOUNDARY_EWRAM_IWRAM) {
                            CHECK(gba->memory.inputBusValid);
                            CHECK(gba->memory.openBus == opcode(width, 2));
                        }
                        check_idle_bus(gba);
                        struct Issued issued = {0};
                        gba->cpu.instructionObserver = observe_issue;
                        gba->cpu.instructionObserverContext = &issued;
                        issue_guest(gba, timed != 0);
                        CHECK(gba->cpu.gprs[0] == 0x11);
                        issue_guest(gba, timed != 0);
                        CHECK(gba->cpu.gprs[0] == 0x22);
                        CHECK(issued.count == 2 && issued.value[0] == opcode(width, 1) &&
                              issued.value[1] == opcode(width, 2));
                        CHECK(issued.address[0] == next - width && issued.address[1] == next);
                        CHECK(gba->lastJump == start);
                        check_idle_bus(gba);
                        destroy(gba);
                        scenes++;
                    }
    CHECK(scenes == 36);
    printf("PASS %u mapped ARM/Thumb first/second refill and guest issue scenes\n", scenes);
}

static void sequential_fetches_cross_mapped_regions(void) {
    unsigned scenes = 0;
    for (unsigned boundary = 0; boundary < BOUNDARY_COUNT; ++boundary)
        for (unsigned width = 2; width <= 4; width += 2)
            for (unsigned timed = 0; timed < 2; ++timed)
                for (unsigned a1 = 0; a1 < (width == 4 ? 2u : 1u); ++a1) {
                    struct CupidGBA *gba = machine(0);
                    seed_boundary(gba, boundary, width);
                    const uint32_t next = boundaries[boundary];
                    const uint32_t start = next - 2 * width + a1 * 2;
                    prime(gba, start, width, timed != 0);
                    CHECK(gba->cpu.prefetch[0] == opcode(width, 0));
                    CHECK(gba->cpu.prefetch[1] == opcode(width, 1));
                    struct Issued issued = {0};
                    gba->cpu.instructionObserver = observe_issue;
                    gba->cpu.instructionObserverContext = &issued;
                    issue_guest(gba, timed != 0);
                    CHECK(gba->cpu.gprs[0] == 0x10);
                    CHECK(gba->cpu.prefetch[0] == opcode(width, 1));
                    CHECK(gba->cpu.prefetch[1] == opcode(width, 2));
                    CHECK(gba->memory.activeRegion == (int)(next >> 24));
                    issue_guest(gba, timed != 0);
                    CHECK(gba->cpu.gprs[0] == 0x11);
                    CHECK(gba->cpu.prefetch[0] == opcode(width, 2));
                    CHECK(gba->cpu.prefetch[1] == opcode(width, 3));
                    issue_guest(gba, timed != 0);
                    CHECK(gba->cpu.gprs[0] == 0x22);
                    issue_guest(gba, timed != 0);
                    CHECK(gba->cpu.gprs[1] == 0x44);
                    CHECK(issued.count == 4);
                    for (unsigned i = 0; i < 4; ++i) {
                        CHECK(issued.address[i] == next - 2 * width + i * width);
                        CHECK(issued.value[i] == opcode(width, i));
                    }
                    CHECK(gba->lastJump == start);
                    check_idle_bus(gba);
                    destroy(gba);
                    scenes++;
                }
    CHECK(scenes == 24);
    printf("PASS %u ordinary ARM/Thumb sequential region-crossing scenes\n", scenes);
}

static void internal_aliases_and_device_mirrors(void) {
    unsigned scenes = 0;
    const uint16_t controls[] = {0x20, 0x21, 0, 1};
    for (unsigned configuration = 0; configuration < 4; ++configuration)
        for (unsigned width = 2; width <= 4; width += 2)
            for (unsigned timed = 0; timed < 2; ++timed)
                for (unsigned epoch = 0; epoch < 2; ++epoch) {
                    struct CupidGBA *gba = machine(epochs[epoch]);
                    cupid_memory_store16(&gba->cpu, GBA_BASE_IO + 0x800,
                                         (int16_t)controls[configuration], NULL);
                    const uint32_t next =
                        (controls[configuration] & 1) ? 0x01000000 : GBA_BASE_IWRAM;
                    const uint32_t start = next - width;
                    uint8_t *before;
                    if (controls[configuration] & 0x20)
                        before = gba->memory.ewram + GBA_SIZE_EWRAM - width;
                    else
                        before = gba->memory.iwram + GBA_SIZE_IWRAM - width;
                    uint32_t first = opcode(width, 1);
                    uint32_t second = opcode(width, 2);
                    memcpy(before, &first, width);
                    memcpy(gba->memory.iwram, &second, width);
                    CHECK(cupid_memory_decode_address(&gba->memory, next) == GBA_BASE_IWRAM);
                    CHECK(mapped_data(gba, next, width) == second);
                    prime(gba, start, width, timed != 0);
                    CHECK(gba->cpu.prefetch[0] == first && gba->cpu.prefetch[1] == second);
                    CHECK(gba->memory.activeRegion == GBA_REGION_IWRAM);
                    CHECK(gba->lastJump == start);
                    issue_guest(gba, timed != 0);
                    CHECK(gba->cpu.gprs[0] == 0x11);
                    issue_guest(gba, timed != 0);
                    CHECK(gba->cpu.gprs[0] == 0x22);
                    check_idle_bus(gba);
                    destroy(gba);
                    scenes++;
                }
    /* Mirroring within an unchanged region must still wrap in that device. */
    for (unsigned region = 0; region < 2; ++region)
        for (unsigned width = 2; width <= 4; width += 2)
            for (unsigned timed = 0; timed < 2; ++timed) {
                struct CupidGBA *gba = machine(0);
                const uint32_t base = region ? GBA_BASE_IWRAM : GBA_BASE_EWRAM;
                const unsigned size = region ? GBA_SIZE_IWRAM : GBA_SIZE_EWRAM;
                uint8_t *memory = region ? gba->memory.iwram : gba->memory.ewram;
                uint32_t first = opcode(width, 1), second = opcode(width, 2);
                write_opcode(memory + size - width, width, first);
                write_opcode(memory, width, second);
                const uint32_t start = base + size - width;
                prime(gba, start, width, timed != 0);
                CHECK(gba->cpu.prefetch[0] == first && gba->cpu.prefetch[1] == second);
                CHECK(gba->memory.activeRegion == (int)(base >> 24));
                issue_guest(gba, timed != 0);
                issue_guest(gba, timed != 0);
                CHECK(gba->cpu.gprs[0] == 0x22);
                destroy(gba);
                scenes++;
            }
    /* Bank selection and upper mirrors are sampled by opcode address. In
     * bitmap mode 3, 0x06018000..0x0601BFFF is an undriven/zero opcode view. */
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned edge = 0; edge < 3; ++edge)
            for (unsigned width = 2; width <= 4; width += 2)
                for (unsigned timed = 0; timed < 2; ++timed) {
                    struct CupidGBA *gba = machine(0);
                    cupid_io_write(gba, REG_DISPCNT, (uint16_t)(0x0080 | (mode ? 3 : 0)));
                    uint8_t *vram = (uint8_t *)gba->video.vram;
                    const unsigned nextOffset = edge == 0 ? 0x10000 : edge == 1 ? 0x18000 : 0x20000;
                    const uint32_t next = GBA_BASE_VRAM + nextOffset;
                    const uint32_t first = opcode(width, 1);
                    const uint32_t second = opcode(width, 2);
                    const bool reserved = mode && edge == 1;
                    uint8_t *source = vram + (edge == 0 ? 0x10000 : 0x18000) - width;
                    uint8_t *target = vram + (edge == 2 ? 0 : 0x10000);
                    write_opcode(source, width, first);
                    write_opcode(target, width, second);
                    if (edge == 0)
                        write_opcode(vram, width, opcode(width, 4));
                    else if (edge == 2)
                        write_opcode(vram + 0x10000, width, opcode(width, 4));
                    const uint32_t expected = reserved ? 0 : second;
                    CHECK(mapped_data(gba, next, width) == expected);
                    prime(gba, next - width, width, timed != 0);
                    CHECK(gba->cpu.prefetch[0] == first);
                    CHECK(gba->cpu.prefetch[1] == expected);
                    CHECK(gba->memory.activeRegion == GBA_REGION_VRAM);
                    if (!reserved) {
                        issue_guest(gba, timed != 0);
                        issue_guest(gba, timed != 0);
                        CHECK(gba->cpu.gprs[0] == 0x22);
                    }
                    check_idle_bus(gba);
                    destroy(gba);
                    scenes++;
                }
    CHECK(scenes == 64);
    printf("PASS %u decoded MEMCNT, mirrors and mode 0/3 VRAM-bank scenes\n", scenes);
}

static void physical_region_selects_existing_wait_tables(void) {
    unsigned scenes = 0;
    for (unsigned width = 2; width <= 4; width += 2)
        for (unsigned timed = 0; timed < 2; ++timed)
            for (unsigned alias = 0; alias < 2; ++alias)
                for (unsigned wait = 0; wait < 2; ++wait)
                    for (unsigned epoch = 0; epoch < 3; ++epoch) {
                        struct CupidGBA *gba = machine(epochs[epoch]);
                        if (alias)
                            cupid_memory_store16(&gba->cpu, GBA_BASE_IO + 0x800, 0x21, NULL);
                        if (wait)
                            cupid_memory_update_ewram_waitstates(gba, 0x0B00);
                        const uint32_t next = alias ? 0x01000000 : GBA_BASE_IWRAM;
                        const uint32_t start = next - width;
                        uint32_t first = opcode(width, 1), second = opcode(width, 2);
                        write_opcode(gba->memory.ewram + GBA_SIZE_EWRAM - width, width, first);
                        write_opcode(gba->memory.iwram, width, second);
                        /* EWRAM defaults to 3 clocks per 16-bit request, and
                         * MEMCNT 0x0B00 changes it to 5. A word takes two
                         * such beats; IWRAM's 32-bit port takes one clock. */
                        const unsigned firstCycles =
                            width == 2 ? (wait ? 5u : 3u) : (wait ? 10u : 6u);
                        const unsigned expected = firstCycles + 1u;
                        const uint64_t before = cupid_timing_global_time(&gba->timing);
                        const unsigned staticCost = prime(gba, start, width, timed != 0);
                        if (timed) {
                            CHECK(staticCost == 0);
                            CHECK(cupid_timing_global_time(&gba->timing) - before == expected);
                        } else {
                            CHECK(cupid_timing_global_time(&gba->timing) == before);
                            CHECK(staticCost == expected);
                        }
                        CHECK(gba->cpu.prefetch[0] == first && gba->cpu.prefetch[1] == second);
                        CHECK(gba->cpu.memory.activeSeqCycles32 == 0);
                        CHECK(gba->memory.activeRegion == GBA_REGION_IWRAM);
                        CHECK(gba->lastJump == start);
                        check_idle_bus(gba);
                        destroy(gba);
                        scenes++;
                    }
    CHECK(scenes == 48);
    printf("PASS %u existing physical wait-table and decoder crossing scenes\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "refill"))
        adjacent_refills_select_target_opcodes();
    else if (!strcmp(argv[1], "sequential"))
        sequential_fetches_cross_mapped_regions();
    else if (!strcmp(argv[1], "aliases"))
        internal_aliases_and_device_mirrors();
    else if (!strcmp(argv[1], "waits"))
        physical_region_selects_existing_wait_tables();
    else
        CHECK(false);
    return 0;
}
