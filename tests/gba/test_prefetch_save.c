/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_prefetch_save.c
 * @author    cupidthecat
 * @brief     Game Pak prefetch ownership when instruction requests enter save windows.
 * @date      2026-11-16
 *
 * - Exercise real ARM/Thumb branch refills and direct bus requests.
 * - Verify that mutable cartridge-port samples cannot survive a save fetch.
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
#include "../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

static const uint64_t clocks[] = {0, UINT32_MAX - UINT64_C(5), UINT64_C(0x300000000)};

static struct CupidGBA *machine(uint64_t origin, uint16_t waitcnt) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = origin;
    gba->timing.masterCycles = (uint32_t)origin;
    gba->audio.lastSample = origin;
    gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + 512;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    gba->memory.rom = calloc(1, 0x40000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 0x40000;
    gba->memory.romMask = 0x3FFFF;
    cupid_memory_update_waitstates(gba, waitcnt);
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static uint64_t fetch(struct CupidGBA *gba, uint32_t address, unsigned width, bool sequential) {
    uint64_t before = cupid_timing_global_time(&gba->timing);
    cupid_bus_fetch(&gba->cpu, address, width, sequential);
    cupid_bus_finish(&gba->cpu);
    return cupid_timing_global_time(&gba->timing) - before;
}

static void idle(struct CupidGBA *gba, unsigned cycles) {
    cupid_bus_advance(&gba->cpu, cycles, false);
    cupid_bus_finish(&gba->cpu);
}

static void check_stopped(struct CupidGBA *gba) {
    const struct CupidPrefetch *unit = &gba->memory.prefetchUnit;
    CHECK(!unit->active && !unit->busy && !unit->paused && !unit->engaged);
    CHECK(!unit->count && !unit->remaining && unit->fetchKind == CUPID_PREFETCH_NONE);
    CHECK(!gba->cartridge.prefetchScheduled && !gba->cartridge.prefetchMask &&
          !gba->cartridge.cardPrefetchMask);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.prefetchEvent));
}

struct BranchTrace {
    uint32_t target;
    unsigned width;
    unsigned fetches;
    bool save;
};

static void observe_branch_fetch(struct CupidARM *cpu, uint32_t address, unsigned width,
                                 bool sequential) {
    struct BranchTrace *trace = cpu->instructionObserverContext;
    bool target = address >= trace->target && address < trace->target + 2 * trace->width;
    if (target) {
        CHECK(width == trace->width && trace->fetches < 2);
        if (!trace->fetches)
            CHECK(cpu->master->memory.prefetchUnit.active);
    }
    cupid_bus_fetch(cpu, address, width, sequential);
    if (target) {
        /* Capture the first refill before a second save request can hide
         * an incorrectly surviving ROM queue. */
        if (trace->save)
            check_stopped(cpu->master);
        else
            CHECK(cpu->master->memory.prefetchUnit.active);
        trace->fetches++;
    }
}

static void guest_branch_to_save_or_internal(void) {
    const uint32_t targets[] = {GBA_BASE_SRAM + 0x100, GBA_BASE_SRAM_MIRROR + 0x100,
                                GBA_BASE_EWRAM + 0x100, GBA_BASE_IWRAM + 0x100};
    unsigned scenes = 0;
    for (unsigned sourceThumb = 0; sourceThumb < 2; sourceThumb++)
        for (unsigned targetThumb = 0; targetThumb < 2; targetThumb++)
            for (unsigned targetIndex = 0; targetIndex < 4; targetIndex++)
                for (unsigned epoch = 0; epoch < 2; epoch++) {
                    struct CupidGBA *gba = machine(clocks[epoch], 0x4000);
                    struct CupidARM *cpu = &gba->cpu;
                    uint32_t target = targets[targetIndex];
                    unsigned nop = sourceThumb ? 0x46C0 : 0xE1A00000;
                    unsigned branch = sourceThumb ? 0x4700 : 0xE12FFF10;
                    uint32_t armNop = 0xE1A00000;
                    uint16_t thumbNop = 0x46C0;
                    /* ROM contains a real NOP and BX r0. Target RAM holds a
                     * normal opcode; save opcode bytes are intentionally unconstrained. */
                    if (sourceThumb) {
                        STORE_16(nop, 0x100, gba->memory.rom);
                        STORE_16(branch, 0x102, gba->memory.rom);
                        STORE_16(nop, 0x104, gba->memory.rom);
                    } else {
                        STORE_32(nop, 0x100, gba->memory.rom);
                        STORE_32(branch, 0x104, gba->memory.rom);
                        STORE_32(nop, 0x108, gba->memory.rom);
                    }
                    if (targetIndex >= 2) {
                        uint8_t *ram = targetIndex == 2 ? gba->memory.ewram : gba->memory.iwram;
                        if (targetThumb)
                            STORE_16(thumbNop, 0x100, ram);
                        else
                            STORE_32(armNop, 0x100, ram);
                    }
                    struct BranchTrace trace = {
                        .target = target, .width = targetThumb ? 2 : 4, .save = targetIndex < 2};
                    cpu->instructionObserverContext = &trace;
                    cpu->memory.fetch = observe_branch_fetch;
                    cpu->gprs[0] = (int32_t)(target | targetThumb);
                    _CupidSetMode(cpu, sourceThumb ? MODE_THUMB : MODE_ARM);
                    cpu->memory.timed = true;
                    cpu->gprs[ARM_PC] = GBA_BASE_ROM0 + 0x100;
                    if (sourceThumb)
                        CupidThumbWritePC(cpu);
                    else
                        CupidARMWritePC(cpu);
                    CHECK(cpu->prefetch[0] == nop && cpu->prefetch[1] == branch);
                    CHECK(gba->memory.prefetchUnit.active);
                    cupid_gba_step(gba);
                    CHECK(cpu->prefetch[0] == branch && gba->memory.prefetchUnit.active);
                    cupid_gba_step(gba);
                    CHECK(cpu->executionMode == (targetThumb ? MODE_THUMB : MODE_ARM));
                    CHECK((uint32_t)cpu->gprs[ARM_PC] == target + trace.width);
                    CHECK(trace.fetches == 2);
                    if (targetIndex < 2)
                        check_stopped(gba);
                    else {
                        CHECK(gba->memory.activeRegion == (int)(target >> 24));
                        CHECK(gba->memory.prefetchUnit.active);
                    }
                    destroy(gba);
                    scenes++;
                }
    printf("PASS %u ROM NOP/BX save and internal-memory branch scenes\n", scenes);
}

static void prepare_stream(struct CupidGBA *gba, unsigned width, unsigned phase, uint16_t waitcnt) {
    struct CupidARM *cpu = &gba->cpu;
    cpu->cpsr.t = width == 2;
    cpu->executionMode = width == 2 ? MODE_THUMB : MODE_ARM;
    cpu->gprs[ARM_PC] = GBA_BASE_ROM0 + 0x108;
    cupid_memory_set_active_region(cpu, GBA_BASE_ROM0 + 0x100);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x100, width, false) > 0);
    struct CupidPrefetch *unit = &gba->memory.prefetchUnit;
    CHECK(unit->active && !unit->count && unit->remaining == unit->duty);
    if (phase == 1) {
        idle(gba, unit->duty + 1);
        CHECK(unit->count == 1 && unit->remaining < unit->duty);
    } else if (phase == 2) {
        idle(gba, 9 * unit->duty);
        CHECK(unit->count == 8 && unit->paused && !unit->remaining);
    } else if (phase == 3) {
        idle(gba, 2 * unit->duty);
        CHECK(unit->count == 2);
        cupid_memory_update_waitstates(gba, waitcnt & ~UINT16_C(0x4000));
        CHECK(unit->disabled && !gba->memory.prefetch && unit->active);
    } else
        CHECK(phase == 0 && !unit->count && unit->remaining);
}

static void direct_save_fetch_cancels_and_rom_restarts(void) {
    const uint32_t saveRegions[] = {GBA_BASE_SRAM, GBA_BASE_SRAM_MIRROR};
    const uint16_t waits[] = {0x4000, 0x4317};
    unsigned scenes = 0;
    /* Four naturally reached FIFO states: pending, partially filled, full,
     * and disabled with retained entries. Both CPU request kinds exercise
     * each width, with representative waits and clock-wrap origins. */
    for (unsigned width = 2; width <= 4; width += 2)
        for (unsigned sequential = 0; sequential < 2; sequential++)
            for (unsigned phase = 0; phase < 4; phase++)
                for (unsigned save = 0; save < 2; save++) {
                    unsigned sample = width / 2 + sequential + phase + save;
                    uint16_t waitcnt = waits[sample % 2];
                    struct CupidGBA *gba = machine(clocks[sample % 3], waitcnt);
                    prepare_stream(gba, width, phase, waitcnt);
                    uint32_t address = saveRegions[save] + 0x100;
                    cupid_memory_set_active_region(&gba->cpu, address);
                    CHECK(fetch(gba, address, width, sequential != 0) > 0);
                    check_stopped(gba);
                    idle(gba, 20);
                    check_stopped(gba);
                    /* A later ordinary ROM fetch begins its own real stream. */
                    cupid_memory_update_waitstates(gba, waitcnt);
                    cupid_memory_set_active_region(&gba->cpu, GBA_BASE_ROM0 + 0x200);
                    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x200, width, false) > 0);
                    struct CupidPrefetch *unit = &gba->memory.prefetchUnit;
                    CHECK(unit->active && !unit->busy && !unit->count && !unit->disabled);
                    CHECK(unit->head == GBA_BASE_ROM0 + 0x200 + width && unit->tail == unit->head);
                    idle(gba, unit->duty);
                    CHECK(unit->active && unit->count == 1);
                    destroy(gba);
                    scenes++;
                }

    for (unsigned internal = 0; internal < 3; internal++) {
        struct CupidGBA *gba = machine(clocks[internal], 0x4000);
        prepare_stream(gba, internal == 1 ? 2 : 4, internal == 2 ? 2 : 1, 0x4000);
        const uint32_t address[] = {GBA_BASE_EWRAM + 0x100, GBA_BASE_IWRAM + 0x100, 0x100};
        cupid_memory_set_active_region(&gba->cpu, address[internal]);
        CHECK(fetch(gba, address[internal], internal == 1 ? 2 : 4, false) > 0);
        CHECK(gba->memory.prefetchUnit.active);
        destroy(gba);
    }
    printf("PASS %u direct save cancellation/restart scenes and three internal controls\n", scenes);
}

static uint8_t input_pins(void *context) {
    return *(uint8_t *)context;
}

static void start_gpio_stream(struct CupidGBA *gba, uint8_t *input) {
    cupid_cartridge_set_input(gba, input_pins, input);
    CHECK(cupid_cartridge_gpio_write(gba, GBA_BASE_ROM0 + 0xC6, 4));
    CHECK(cupid_cartridge_gpio_write(gba, GBA_BASE_ROM0 + 0xC4, 2));
    CHECK(cupid_cartridge_gpio_write(gba, GBA_BASE_ROM0 + 0xC8, 1));
    gba->cpu.gprs[ARM_PC] = GBA_BASE_ROM0 + 0xC8;
    cupid_memory_set_active_region(&gba->cpu, GBA_BASE_ROM0 + 0xC0);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0xC0, 4, false) > 0);
    CHECK(gba->memory.prefetchUnit.active && gba->cartridge.prefetchScheduled);
    CHECK(gba->cartridge.prefetchAddress == GBA_BASE_ROM0 + 0xC4);
    CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.prefetchEvent));
}

static void save_fetch_cancels_mutable_port_samples(void) {
    unsigned scenes = 0;
    for (unsigned captured = 0; captured < 2; captured++)
        for (unsigned save = 0; save < 2; save++)
            for (unsigned epoch = 0; epoch < 2; epoch++) {
                struct CupidGBA *gba = machine(clocks[epoch], 0x4000);
                uint8_t input = 1;
                start_gpio_stream(gba, &input);
                struct CupidPrefetch *unit = &gba->memory.prefetchUnit;
                if (captured) {
                    idle(gba, unit->duty);
                    CHECK(gba->cartridge.prefetchMask & 1);
                    CHECK(gba->cartridge.prefetchPorts[0] == 1);
                } else
                    CHECK(!gba->cartridge.prefetchMask);
                input = 9;
                uint32_t target = (save ? GBA_BASE_SRAM_MIRROR : GBA_BASE_SRAM) + 0x100;
                cupid_memory_set_active_region(&gba->cpu, target);
                CHECK(fetch(gba, target, save ? 2 : 4, false) > 0);
                check_stopped(gba);
                idle(gba, 32);
                check_stopped(gba);
                /* The old sample must not reappear when a new ROM stream
                 * visits the same live input port with its new value. */
                cupid_memory_set_active_region(&gba->cpu, GBA_BASE_ROM0 + 0xC0);
                CHECK(fetch(gba, GBA_BASE_ROM0 + 0xC0, 4, false) > 0);
                idle(gba, unit->duty);
                CHECK((gba->cartridge.prefetchMask & 1) != 0);
                CHECK(gba->cartridge.prefetchPorts[0] == 9);
                destroy(gba);
                scenes++;
            }
    for (unsigned internal = 0; internal < 2; internal++) {
        struct CupidGBA *gba = machine(clocks[internal], 0x4000);
        uint8_t input = 1;
        start_gpio_stream(gba, &input);
        uint32_t address = (internal ? GBA_BASE_IWRAM : GBA_BASE_EWRAM) + 0x100;
        cupid_memory_set_active_region(&gba->cpu, address);
        CHECK(fetch(gba, address, 4, false) > 0);
        idle(gba, gba->memory.prefetchUnit.duty);
        CHECK(gba->memory.prefetchUnit.active && (gba->cartridge.prefetchMask & 1));
        CHECK(gba->cartridge.prefetchPorts[0] == 1);
        destroy(gba);
    }
    printf("PASS %u pending/captured GPIO save cancellation scenes and internal controls\n",
           scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "branches"))
        guest_branch_to_save_or_internal();
    else if (!strcmp(argv[1], "bus"))
        direct_save_fetch_cancels_and_rom_restarts();
    else if (!strcmp(argv[1], "ports"))
        save_fetch_cancels_mutable_port_samples();
    else
        CHECK(false);
    return 0;
}
