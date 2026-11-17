/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_streams.c
 * @author    cupidthecat
 * @brief     Physical Game Pak fetch streams across logical PC alignment and bus ownership.
 * @date      2026-11-17
 *
 * - Check pending and cached words, full-queue restart and mixed fetch widths.
 * - Execute paired ARM/Thumb arrivals while retaining logical PC operands.
 * - Keep RAM activity, DMA and WAITCNT changes on the ordinary bus paths.
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

static const uint64_t clocks[] = {0, INT32_MAX - UINT64_C(4), UINT32_MAX - UINT64_C(4),
                                  UINT64_C(0x300000000)};
static const uint16_t controls[] = {0x4000, 0x4014, 0x4198, 0x47FF};
static const unsigned nonsequential[] = {5, 4, 3, 9};
static const unsigned sequential[] = {3, 5, 9};

static uint32_t pattern(uint32_t offset) {
    return UINT32_C(0x51230000) ^ ((offset & ~3u) * 17u);
}

static struct CupidGBA *machine(uint64_t clock, unsigned bank, uint16_t control) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    gba->cpu.memory.timed = true;
    gba->memory.rom = calloc(1, 0x40000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 0x40000;
    gba->memory.romMask = 0x3FFFF;
    for (uint32_t offset = 0; offset < 0x40000; offset += 4) {
        uint32_t value = pattern(offset);
        memcpy(gba->memory.rom + offset, &value, sizeof(value));
    }
    uint32_t address = GBA_BASE_ROM0 + bank * 0x02000000u + 0x100;
    gba->cpu.gprs[ARM_PC] = address;
    cupid_memory_set_active_region(&gba->cpu, address);
    cupid_memory_update_waitstates(gba, control);
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static unsigned ncycles(unsigned bank, uint16_t control) {
    return nonsequential[(control >> (2 + 3 * bank)) & 3];
}

static unsigned scycles(unsigned bank, uint16_t control) {
    return control & (1u << (4 + 3 * bank)) ? 2 : sequential[bank];
}

/* Observe the sampled opcode as well as its clocks. The arithmetic pattern
 * supplies independent expected bytes for either request width. */
static unsigned fetch(struct CupidGBA *gba, uint32_t address, unsigned width, bool sequential) {
    _CupidSetMode(&gba->cpu, width == 2 ? MODE_THUMB : MODE_ARM);
    uint64_t before = cupid_timing_global_time(&gba->timing);
    cupid_bus_fetch(&gba->cpu, address, width, sequential);
    uint32_t value = CupidReadOpcode(&gba->cpu, address, width);
    uint32_t expected = pattern(address & 0x01FFFFFFu);
    if (width == 2)
        expected = (expected >> ((address & 2) * 8)) & 0xFFFF;
    CHECK(value == expected);
    cupid_bus_finish(&gba->cpu);
    return (unsigned)(cupid_timing_global_time(&gba->timing) - before);
}

static void idle(struct CupidGBA *gba, unsigned cycles) {
    cupid_bus_advance(&gba->cpu, cycles, false);
    cupid_bus_finish(&gba->cpu);
}

static void pending_cached_and_full_streams(void) {
    unsigned scenes = 0;
    for (unsigned bank = 0; bank < 3; bank++)
        for (unsigned setting = 0; setting < 4; setting++) {
            unsigned n = ncycles(bank, controls[setting]);
            unsigned s = scycles(bank, controls[setting]);
            for (unsigned epoch = 0; epoch < 4; epoch++)
                for (unsigned low = 0; low <= 2; low += 2)
                    for (unsigned elapsed = 0; elapsed <= 2 * s + 1; elapsed++) {
                        struct CupidGBA *gba = machine(clocks[epoch], bank, controls[setting]);
                        uint32_t first = GBA_BASE_ROM0 + bank * 0x02000000u + 0x100 + low;
                        gba->cpu.gprs[ARM_PC] = first;
                        CHECK(fetch(gba, first, 4, false) == n + s);
                        idle(gba, elapsed);
                        unsigned expected = elapsed < 2 * s ? 2 * s - elapsed : 1;
                        CHECK(fetch(gba, first + 4, 4, true) == expected);
                        /* A full queue supplies four complete words before the next
                         * cartridge burst pays its nonsequential startup. */
                        idle(gba, 8 * s);
                        for (unsigned word = 0; word < 4; word++)
                            CHECK(fetch(gba, first + 8 + word * 4, 4, true) == 1);
                        CHECK(fetch(gba, first + 24, 4, true) == n + s);
                        CHECK(fetch(gba, first + 28, 4, true) == 2 * s);
                        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == first);
                        destroy(gba);
                        scenes++;
                    }
        }
    printf("physical word streams: %u scenes\n", scenes);
}

static void mixed_halfwords_and_words(void) {
    unsigned scenes = 0;
    for (unsigned bank = 0; bank < 3; bank++)
        for (unsigned setting = 0; setting < 4; setting++) {
            unsigned n = ncycles(bank, controls[setting]);
            unsigned s = scycles(bank, controls[setting]);
            for (unsigned epoch = 0; epoch < 4; epoch++)
                for (unsigned low = 0; low <= 2; low += 2)
                    for (unsigned refill = 0; refill < 2; refill++)
                        for (unsigned elapsed = 0; elapsed <= 2 * s + 1; elapsed++) {
                            struct CupidGBA *gba = machine(clocks[epoch], bank, controls[setting]);
                            uint32_t base = GBA_BASE_ROM0 + bank * 0x02000000u + 0x100;
                            CHECK(fetch(gba, base + 2, 2, false) == n);
                            idle(gba, elapsed);
                            unsigned expected = elapsed >= 2 * s ? 1 : 2 * s - elapsed;
                            if (refill && !elapsed)
                                expected = n + s;
                            CHECK(fetch(gba, base + 4 + low, 4, !refill) == expected);
                            idle(gba, s);
                            CHECK(fetch(gba, base + 8, 2, true) == 1);
                            unsigned progress = elapsed >= 2 * s ? elapsed - 2 * s + 1 : 0;
                            unsigned second = s > progress + 1 ? s - progress - 1 : 1;
                            CHECK(fetch(gba, base + 10, 2, true) == second);
                            idle(gba, 2 * s);
                            CHECK(fetch(gba, base + 12 + low, 4, false) == 1);
                            /* A pending word must preserve the physical cursor when
                             * it finishes, even after earlier cached width changes. */
                            fetch(gba, base + 16 + low, 4, true);
                            idle(gba, 2 * s);
                            CHECK(fetch(gba, base + 20 + low, 4, true) == 1);
                            destroy(gba);
                            scenes++;
                        }
        }
    printf("mixed instruction widths: %u scenes\n", scenes);
}

static void start(struct CupidGBA *gba, uint32_t address, bool thumb) {
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = address;
    if (thumb)
        CupidThumbWritePC(&gba->cpu);
    else
        CupidARMWritePC(&gba->cpu);
}

static void executed_interworking_streams(void) {
    /* ADD r1,pc,#0; LDR r2,[r4]; ADD r3,r3,#1; MUL r5,r6,r7;
     * MOV r8,pc; STR r3,[r9]; BX r10. Memory and multiply internal clocks
     * let the cartridge fill while the logical PC retains bit 1. */
    static const uint32_t program[] = {0xE28F1000, 0xE5942000, 0xE2833001, 0xE0050796, 0xE1A0800F,
                                       0xE5893000, 0xE12FFF1A, 0xE1A00000, 0xE1A00000};
    static const uint32_t entries[] = {0x46C04700, 0xE1A0F000, 0xE590F000};
    static const uint32_t offsets[] = {0x100, 0x1FFF8};
    unsigned pairs = 0;
    for (unsigned bank = 0; bank < 3; bank++)
        for (unsigned setting = 0; setting < 4; setting++)
            for (unsigned epoch = 0; epoch < 4; epoch++)
                for (unsigned arrival = 0; arrival < 3; arrival++)
                    for (unsigned edge = 0; edge < 2; edge++) {
                        struct CupidGBA *twins[2];
                        uint32_t target = GBA_BASE_ROM0 + bank * 0x02000000u + offsets[edge];
                        for (unsigned twin = 0; twin < 2; twin++) {
                            struct CupidGBA *gba = machine(clocks[epoch], bank, controls[setting]);
                            twins[twin] = gba;
                            uint32_t logical = target + twin * 2;
                            const uint32_t data = 0xA59C73E1;
                            const uint32_t nops = 0x46C046C0;
                            memcpy(gba->memory.rom + offsets[edge], program, sizeof(program));
                            memcpy(gba->memory.iwram + 0x1000, &entries[arrival], 4);
                            memcpy(gba->memory.iwram + 0x2000, &logical, 4);
                            memcpy(gba->memory.iwram + 0x3000, &nops, 4);
                            memcpy(gba->memory.ewram + 0x400, &data, 4);
                            gba->cpu.gprs[0] = arrival == 2 ? GBA_BASE_IWRAM + 0x2000 : logical;
                            gba->cpu.gprs[3] = 0x19;
                            gba->cpu.gprs[4] = GBA_BASE_EWRAM + 0x400;
                            gba->cpu.gprs[6] = 7;
                            gba->cpu.gprs[7] = 0x12345678;
                            gba->cpu.gprs[9] = GBA_BASE_EWRAM + 0x408;
                            gba->cpu.gprs[10] = GBA_BASE_IWRAM + 0x3001;
                            start(gba, GBA_BASE_IWRAM + 0x1000, arrival == 0);
                        }
                        for (unsigned step = 0; step < 8; step++) {
                            CHECK(cupid_arm_instruction_address(&twins[0]->cpu) ==
                                  cupid_arm_instruction_address(&twins[1]->cpu));
                            cupid_gba_step(twins[0]);
                            cupid_gba_step(twins[1]);
                            CHECK(cupid_timing_global_time(&twins[0]->timing) ==
                                  cupid_timing_global_time(&twins[1]->timing));
                            CHECK(twins[0]->cpu.prefetch[0] == twins[1]->cpu.prefetch[0]);
                            CHECK(twins[0]->cpu.prefetch[1] == twins[1]->cpu.prefetch[1]);
                            CHECK(twins[0]->cpu.executionMode == twins[1]->cpu.executionMode);
                        }
                        for (unsigned twin = 0; twin < 2; twin++) {
                            struct CupidGBA *gba = twins[twin];
                            CHECK((uint32_t)gba->cpu.gprs[1] == target + 8 + twin * 2);
                            CHECK((uint32_t)gba->cpu.gprs[2] == UINT32_C(0xA59C73E1));
                            CHECK(gba->cpu.gprs[3] == 0x1A);
                            CHECK((uint32_t)gba->cpu.gprs[5] == UINT32_C(0x7F6E5D48));
                            CHECK((uint32_t)gba->cpu.gprs[8] == target + 24 + twin * 2);
                            CHECK(gba->cpu.executionMode == MODE_THUMB);
                            CHECK(cupid_arm_instruction_address(&gba->cpu) ==
                                  GBA_BASE_IWRAM + 0x3000);
                            uint32_t stored;
                            memcpy(&stored, gba->memory.ewram + 0x408, 4);
                            CHECK(stored == 0x1A);
                            destroy(gba);
                        }
                        pairs++;
                    }
    printf("executed ARM/Thumb stream pairs: %u\n", pairs);
}

static void ram_dma_and_wait_control(void) {
    unsigned scenes = 0;
    for (unsigned bank = 0; bank < 3; bank++)
        for (unsigned setting = 0; setting < 4; setting++)
            for (unsigned epoch = 0; epoch < 4; epoch++)
                for (unsigned low = 0; low <= 2; low += 2)
                    for (unsigned activity = 0; activity < 3; activity++) {
                        struct CupidGBA *gba = machine(clocks[epoch], bank, controls[setting]);
                        uint32_t first = GBA_BASE_ROM0 + bank * 0x02000000u + 0x100 + low;
                        unsigned n = ncycles(bank, controls[setting]);
                        unsigned s = scycles(bank, controls[setting]);
                        CHECK(fetch(gba, first, 4, false) == n + s);
                        if (activity == 0) {
                            int cycles = 0;
                            CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM, &cycles) == 0);
                            CHECK(cycles == 1);
                            idle(gba, (unsigned)cycles);
                        } else if (activity == 1) {
                            const uint32_t payload[] = {0x13579BDF, 0x2468ACE0};
                            memcpy(gba->memory.iwram + 0x400, payload, sizeof(payload));
                            cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x400);
                            cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 0x800);
                            cupid_dma_write_cnt_lo(gba, 3, 2);
                            cupid_dma_write_cnt(gba, 3, 0x8400);
                            idle(gba, 2);
                            cupid_bus_wait(&gba->cpu);
                            CHECK(!memcmp(gba->memory.ewram + 0x800, payload, sizeof(payload)));
                            CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked);
                        } else {
                            /* Drain a complete buffered word after WAITCNT disable,
                             * then re-enable and establish a fresh physical stream. */
                            idle(gba, 2 * s);
                            cupid_memory_update_waitstates(gba, controls[setting] & ~0x4000u);
                            CHECK(fetch(gba, first + 4, 4, true) == 1);
                            cupid_memory_update_waitstates(gba, controls[setting]);
                            CHECK(fetch(gba, first + 0x100, 4, false) == n + s + (s == 2));
                            first += 0x100;
                        }
                        idle(gba, 8 * s);
                        CHECK(fetch(gba, first + 4, 4, true) == 1);
                        CHECK(fetch(gba, first + 8, 4, true) == 1);
                        destroy(gba);
                        scenes++;
                    }
    printf("RAM, DMA and WAITCNT stream scenes: %u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "bus"))
        RUN(pending_cached_and_full_streams);
    else if (!strcmp(argv[1], "widths"))
        RUN(mixed_halfwords_and_words);
    else if (!strcmp(argv[1], "execution"))
        RUN(executed_interworking_streams);
    else if (!strcmp(argv[1], "ownership"))
        RUN(ram_dma_and_wait_control);
    else
        CHECK(false);
    return 0;
}
