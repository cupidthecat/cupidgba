/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"

static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    gba->memory.rom = calloc(1, 0x40000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 0x40000;
    gba->memory.romMask = 0x3FFFF;
    gba->cpu.gprs[ARM_PC] = GBA_BASE_ROM0 + 0x100;
    cupid_memory_set_active_region(&gba->cpu, GBA_BASE_ROM0 + 0x100);
    cupid_memory_update_waitstates(gba, 0x4000);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static uint64_t fetch(struct CupidGBA* gba, uint32_t address, unsigned width, bool sequential) {
    uint64_t before = cupid_timing_global_time(&gba->timing);
    cupid_bus_fetch(&gba->cpu, address, width, sequential);
    cupid_bus_finish(&gba->cpu);
    return cupid_timing_global_time(&gba->timing) - before;
}

static void idle(struct CupidGBA* gba, unsigned cycles) {
    cupid_bus_advance(&gba->cpu, cycles, false);
    cupid_bus_finish(&gba->cpu);
}

static void wait_controls_before_fetch_and_after_invalid_jump(void) {
    for (unsigned configuration = 0; configuration < 3; configuration++) {
        struct CupidGBA* gba = calloc(1, sizeof(*gba));
        CHECK(gba);
        cupid_gba_create(gba);
        if (configuration == 1) {
            /* Reset an attached firmware path without fetching an instruction. */
            gba->memory.fullBios = 1;
            cupid_gba_reset(gba);
        } else if (configuration == 2) {
            cupid_memory_set_active_region(&gba->cpu, GBA_BASE_IWRAM);
            cupid_memory_set_active_region(&gba->cpu, 0x01000000);
        }
        CHECK(gba->memory.activeRegion == -1);
        gba->cpu.memory.activeSeqCycles32 = 7;
        gba->cpu.memory.activeSeqCycles16 = 11;
        gba->cpu.memory.activeNonseqCycles32 = 13;
        gba->cpu.memory.activeNonseqCycles16 = 17;
        cupid_io_write(gba, REG_WAITCNT, 0x4317);
        cupid_memory_update_ewram_waitstates(gba, 0x0B00);
        CHECK(gba->memory.activeRegion == -1);
        CHECK(gba->cpu.memory.activeSeqCycles32 == 7 && gba->cpu.memory.activeSeqCycles16 == 11);
        CHECK(gba->cpu.memory.activeNonseqCycles32 == 13 && gba->cpu.memory.activeNonseqCycles16 == 17);
        cupid_memory_set_active_region(&gba->cpu, GBA_BASE_EWRAM + 0x100);
        CHECK(fetch(gba, GBA_BASE_EWRAM + 0x100, 2, false) == 5);
        CHECK(fetch(gba, GBA_BASE_EWRAM + 0x104, 4, false) == 10);
        cupid_memory_update_ewram_waitstates(gba, 0x0C00);
        CHECK(fetch(gba, GBA_BASE_EWRAM + 0x108, 2, false) == 4);
        gba->memory.rom = calloc(1, 0x4000);
        CHECK(gba->memory.rom);
        gba->memory.romSize = 0x4000;
        gba->memory.romMask = 0x3FFF;
        cupid_memory_set_active_region(&gba->cpu, GBA_BASE_ROM0 + 0x100);
        CHECK(fetch(gba, GBA_BASE_ROM0 + 0x100, 4, false) == 6);
        cupid_io_write(gba, REG_WAITCNT, 0);
        CHECK(fetch(gba, GBA_BASE_ROM0 + 0x200, 4, false) == 8);
        destroy(gba);
    }
}

static void initial_fetch_pending_completion_and_cache_consumption(void) {
    for (unsigned width = 2; width <= 4; width += 2) {
        struct CupidGBA* gba = machine(0);
        struct CupidPrefetch* unit = &gba->memory.prefetchUnit;
        unsigned duty = width == 4 ? 6 : 3;
        CHECK(fetch(gba, GBA_BASE_ROM0 + 0x100, width, false) == (width == 4 ? 8 : 5));
        CHECK(unit->active && unit->count == 0 && unit->duty == duty);
        CHECK(unit->head == GBA_BASE_ROM0 + 0x100 + width && unit->tail == unit->head);
        idle(gba, duty - 1);
        CHECK(unit->remaining == 1 && unit->count == 0);
        CHECK(fetch(gba, unit->head, width, true) == 1);
        CHECK(unit->count == 0 && unit->remaining == duty);
        idle(gba, duty * 2 - 1);
        CHECK(unit->count == 1 && unit->remaining == 1);
        uint32_t next = unit->head + width;
        CHECK(fetch(gba, unit->head, width, true) == 1);
        CHECK(unit->count == 1 && unit->head == next && unit->remaining == duty);
        CHECK(fetch(gba, next, width, true) == 1);
        CHECK(unit->count == 0 && unit->remaining == duty - 1);
        destroy(gba);
    }
}

static void queue_capacity_is_bounded_and_reset_clears_pending_state(void) {
    for (unsigned width = 2; width <= 4; width += 2) {
        struct CupidGBA* gba = machine(0);
        struct CupidPrefetch* unit = &gba->memory.prefetchUnit;
        fetch(gba, GBA_BASE_ROM0 + 0x100, width, false);
        idle(gba, 1000000);
        CHECK(unit->count == 16 / width && unit->remaining == 0);
        CHECK(unit->tail - unit->head == 16);
        uint32_t first = unit->head;
        idle(gba, 1000000);
        CHECK(unit->count == 16 / width && unit->head == first);
        CHECK(fetch(gba, first, width, true) == 1);
        CHECK(unit->count == 16 / width - 1);
        CHECK(unit->remaining == unit->duty - 1);
        /* A full buffer cannot supply an unlimited stream at one clock per
         * opcode: newly fetched opcodes still use the cartridge bandwidth. */
        for (unsigned i = 1; i < 32; i++) {
            uint64_t clocks = fetch(gba, first + i * width, width, true);
            CHECK(unit->count <= 16 / width);
            if (i >= 16) CHECK(clocks == unit->duty);
        }
        cupid_gba_reset(gba);
        CHECK(!unit->active && !unit->busy && !unit->disabled && unit->count == 0);
        CHECK(!gba->memory.prefetch);
        destroy(gba);
    }
}

static void cpu_ram_data_fills_and_gamepak_data_cancels(void) {
    struct CupidGBA* gba = machine(0);
    struct CupidPrefetch* unit = &gba->memory.prefetchUnit;
    fetch(gba, GBA_BASE_ROM0 + 0x100, 4, false);
    gba->cpu.memory.timed = true;
    int cycles = 0;
    cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM, &cycles);
    CHECK(cycles == 1 && unit->count == 1 && unit->remaining == 6);
    CHECK(cupid_timing_global_time(&gba->timing) == 14);
    gba->cpu.cycles += cycles;
    cupid_bus_finish(&gba->cpu);
    CHECK(unit->remaining == 5);
    cycles = 0;
    cupid_memory_load16(&gba->cpu, GBA_BASE_ROM0 + 0x200, &cycles);
    CHECK(!unit->active && unit->count == 0 && !unit->busy);
    CHECK(cupid_timing_global_time(&gba->timing) == 20);
    gba->cpu.memory.timed = false;
    destroy(gba);
}

static void completing_halfword_arbitration_and_branch_width_changes(void) {
    for (unsigned width = 2; width <= 4; width += 2) {
        unsigned duty = width == 4 ? 6 : 3;
        for (unsigned offset = 0; offset < duty; offset++) {
            struct CupidGBA* gba = machine(0);
            fetch(gba, GBA_BASE_ROM0 + 0x100, width, false);
            idle(gba, offset);
            unsigned remaining = duty - offset;
            unsigned expected = remaining == 1 || (width == 4 && remaining == 4);
            CHECK(cupid_prefetch_before_data(gba, GBA_BASE_ROM0 + 0x200) == expected);
            CHECK(!gba->memory.prefetchUnit.active);
            cupid_prefetch_after_data(gba);
            destroy(gba);
        }
    }
    struct CupidGBA* gba = machine(0);
    fetch(gba, GBA_BASE_ROM0 + 0x100, 4, false);
    idle(gba, 12);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x200, 4, false) == 8);
    CHECK(gba->memory.prefetchUnit.head == GBA_BASE_ROM0 + 0x204);
    /* Switching ARM/Thumb refills from a new stream, not a mismatched entry. */
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x300, 2, false) == 5);
    CHECK(gba->memory.prefetchUnit.width == 2 && gba->memory.prefetchUnit.duty == 3);
    destroy(gba);
}

static void disabled_prefetch_drains_and_restarts_the_burst(void) {
    struct CupidGBA* gba = machine(0);
    struct CupidPrefetch* unit = &gba->memory.prefetchUnit;
    fetch(gba, GBA_BASE_ROM0 + 0x100, 4, false);
    idle(gba, 6);
    CHECK(unit->count == 1);
    cupid_memory_update_waitstates(gba, 0);
    CHECK(unit->disabled && !gba->memory.prefetch);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x104, 4, true) == 1);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x108, 4, true) == 5);
    CHECK(!unit->active && unit->disabled);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x10C, 4, true) == 8);
    CHECK(!unit->disabled);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x110, 4, true) == 6);
    destroy(gba);
}

static void sequential_costs_waitcnt_and_page_restart(void) {
    struct CupidGBA* gba = machine(0);
    cupid_memory_update_waitstates(gba, 0);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x100, 4, false) == 8);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x1FFF8, 4, true) == 6);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x1FFFC, 4, true) == 6);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x20000, 4, true) == 8);
    CHECK(fetch(gba, GBA_BASE_ROM1 + 0x100, 4, false) == 10);
    CHECK(fetch(gba, GBA_BASE_ROM1 + 0x104, 4, true) == 10);
    CHECK(fetch(gba, GBA_BASE_ROM2 + 0x100, 4, false) == 14);
    CHECK(fetch(gba, GBA_BASE_ROM2 + 0x104, 4, true) == 18);
    cupid_memory_update_waitstates(gba, 0x4018);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x100, 4, false) == 5);
    CHECK(gba->memory.prefetchUnit.duty == 4);
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x104, 4, true) == 4);
    destroy(gba);
}

static void dma_clock_accounting_matches_late_and_punctual_dispatch(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - 10u, UINT64_C(0x300000000)};
    for (unsigned c = 0; c < sizeof(clocks) / sizeof(*clocks); c++) {
        for (unsigned external = 0; external < 2; external++) {
            struct CupidGBA* late = machine(clocks[c]);
            struct CupidGBA* punctual = machine(clocks[c]);
            struct CupidGBA* machines[] = {late, punctual};
            for (unsigned i = 0; i < 2; i++) {
                struct CupidGBA* gba = machines[i];
                fetch(gba, GBA_BASE_ROM0 + 0x100, 4, false);
                cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM, 0x12345678, NULL);
                cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM);
                cupid_dma_write_dad(gba, 3, external ? GBA_BASE_ROM0 + 0x300 : GBA_BASE_EWRAM);
                cupid_dma_write_cnt_lo(gba, 3, 3);
                cupid_dma_write_cnt(gba, 3, 0xC400);
                cupid_io_write(gba, REG_TM0CNT_L, 0xFFF0);
                cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
            }
            idle(late, 40);
            for (unsigned clock = 0; clock < 40; clock++) idle(punctual, 1);
            const struct CupidPrefetch* a = &late->memory.prefetchUnit;
            const struct CupidPrefetch* b = &punctual->memory.prefetchUnit;
            CHECK(a->count == b->count && a->head == b->head && a->tail == b->tail);
            CHECK(a->remaining == b->remaining && a->active == b->active && a->busy == b->busy);
            CHECK(a->epoch == b->epoch);
            CHECK(late->memory.dma[3].busyUntil == punctual->memory.dma[3].busyUntil);
            CHECK(late->irqf == punctual->irqf && late->irq.line == punctual->irq.line);
            CHECK(cupid_io_read(late, REG_TM0CNT_L) == cupid_io_read(punctual, REG_TM0CNT_L));
            CHECK(!late->memory.dmaRunning && !punctual->memory.dmaRunning);
            CHECK(external ? !a->active : a->count == 4);
            destroy(late);
            destroy(punctual);
        }
    }
}

static void dma_external_write_waits_for_completing_prefetch_halfword(void) {
    struct CupidGBA* gba = machine(0);
    fetch(gba, GBA_BASE_ROM0 + 0x100, 4, false);
    idle(gba, 1);
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM);
    cupid_dma_write_dad(gba, 3, GBA_BASE_ROM0 + 0x300);
    cupid_dma_write_cnt_lo(gba, 3, 1);
    cupid_dma_write_cnt(gba, 3, 0xC400);
    /* Activation at 11, handover at 12 and source read at 13 leave one
     * prefetch clock. The ROM write takes the arbitration clock plus 8. */
    idle(gba, 12);
    CHECK(cupid_timing_global_time(&gba->timing) == 21);
    CHECK(gba->memory.dma[3].enable && gba->irqf == 0);
    idle(gba, 1);
    CHECK(gba->memory.dma[3].busyUntil == 22 && !gba->memory.dma[3].enable);
    CHECK(gba->memory.dmaRunning && !gba->memory.prefetchUnit.active);
    idle(gba, 1);
    CHECK(!gba->memory.dmaRunning && gba->irqf == IRQ_DMA3);
    destroy(gba);
}

int main(void) {
    RUN(wait_controls_before_fetch_and_after_invalid_jump);
    RUN(initial_fetch_pending_completion_and_cache_consumption);
    RUN(queue_capacity_is_bounded_and_reset_clears_pending_state);
    RUN(cpu_ram_data_fills_and_gamepak_data_cancels);
    RUN(completing_halfword_arbitration_and_branch_width_changes);
    RUN(disabled_prefetch_drains_and_restarts_the_burst);
    RUN(sequential_costs_waitcnt_and_page_restart);
    RUN(dma_clock_accounting_matches_late_and_punctual_dispatch);
    RUN(dma_external_write_waits_for_completing_prefetch_halfword);
    return 0;
}
