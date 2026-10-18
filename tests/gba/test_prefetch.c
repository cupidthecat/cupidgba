/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
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
        unsigned transfer = width == 4 ? 6 : 3;
        CHECK(fetch(gba, GBA_BASE_ROM0 + 0x100, width, false) == (width == 4 ? 8 : 5));
        CHECK(unit->active && unit->count == 0 && unit->duty == 3);
        CHECK(unit->head == GBA_BASE_ROM0 + 0x100 + width && unit->tail == unit->head);
        idle(gba, transfer - 1);
        CHECK(unit->remaining == 1 && unit->count == width / 2 - 1);
        CHECK(fetch(gba, unit->head, width, true) == 1);
        CHECK(unit->count == 0 && unit->remaining == 3);
        idle(gba, transfer * 2 - 1);
        CHECK(unit->count == width - 1 && unit->remaining == 1);
        uint32_t next = unit->head + width;
        CHECK(fetch(gba, unit->head, width, true) == 1);
        CHECK(unit->count == width / 2 && unit->head == next && unit->remaining == 3);
        CHECK(fetch(gba, next, width, true) == 1);
        CHECK(unit->count == 0 && unit->remaining == 2);
        destroy(gba);
    }
}

static void queue_capacity_is_bounded_and_reset_clears_pending_state(void) {
    for (unsigned width = 2; width <= 4; width += 2) {
        struct CupidGBA* gba = machine(0);
        struct CupidPrefetch* unit = &gba->memory.prefetchUnit;
        fetch(gba, GBA_BASE_ROM0 + 0x100, width, false);
        idle(gba, 1000000);
        CHECK(unit->count == 8 && unit->remaining == 0 && unit->paused);
        CHECK(unit->tail - unit->head == 16);
        uint32_t first = unit->head;
        idle(gba, 1000000);
        CHECK(unit->count == 8 && unit->head == first);
        CHECK(fetch(gba, first, width, true) == 1);
        CHECK(unit->count == 8 - width / 2);
        CHECK(unit->remaining == 0 && unit->paused);
        /* Capacity stops the burst until every cached halfword drains. An
         * idle interval after consuming one opcode must not refill that slot. */
        idle(gba, 1000000);
        CHECK(unit->count == 8 - width / 2 && unit->remaining == 0 && unit->paused);
        for (unsigned offset = width; offset < 16; offset += width) {
            CHECK(fetch(gba, first + offset, width, true) == 1);
            CHECK(unit->remaining == 0 && unit->paused);
        }
        CHECK(!unit->count);
        CHECK(fetch(gba, first + 16, width, true) == (width == 4 ? 8 : 5));
        CHECK(!unit->paused && !unit->count && unit->remaining == 3);
        for (unsigned i = 1; i < 32; i++)
            CHECK(fetch(gba, first + 16 + i * width, width, true) == unit->duty * width / 2);
        cupid_gba_reset(gba);
        CHECK(!unit->active && !unit->busy && !unit->disabled && !unit->paused && !unit->engaged && unit->count == 0);
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
    CHECK(cycles == 1 && unit->count == 2 && unit->remaining == 3);
    CHECK(cupid_timing_global_time(&gba->timing) == 14);
    gba->cpu.cycles += cycles;
    cupid_bus_finish(&gba->cpu);
    CHECK(unit->remaining == 2);
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
    /* An unrelated target starts a new stream regardless of fetch width. */
    CHECK(fetch(gba, GBA_BASE_ROM0 + 0x300, 2, false) == 5);
    CHECK(gba->memory.prefetchUnit.width == 2 && gba->memory.prefetchUnit.duty == 3);
    destroy(gba);
}

static void partial_words_and_mixed_instruction_widths(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - 10u, UINT64_C(0x300000000)};
    const unsigned second_halfword[] = {3, 3, 3, 2, 1, 1};
    for (unsigned c = 0; c < sizeof(clocks) / sizeof(*clocks); c++) {
        for (unsigned elapsed = 0; elapsed < 6; elapsed++) {
            struct CupidGBA* gba = machine(clocks[c]);
            CHECK(fetch(gba, GBA_BASE_ROM0 + 0x100, 4, false) == 8);
            idle(gba, elapsed);
            CHECK(gba->memory.prefetchUnit.count == elapsed / 3);
            CHECK(fetch(gba, GBA_BASE_ROM0 + 0x104, 4, true) == 6 - elapsed);
            destroy(gba);

            gba = machine(clocks[c]);
            CHECK(fetch(gba, GBA_BASE_ROM0 + 0x100, 4, false) == 8);
            idle(gba, elapsed);
            CHECK(fetch(gba, GBA_BASE_ROM0 + 0x104, 2, false) == (!elapsed ? 5 : (elapsed < 3 ? 3 - elapsed : 1)));
            CHECK(fetch(gba, GBA_BASE_ROM0 + 0x106, 2, true) == second_halfword[elapsed]);
            CHECK(gba->memory.prefetchUnit.head == GBA_BASE_ROM0 + 0x108);
            destroy(gba);
        }
        struct CupidGBA* gba = machine(clocks[c]);
        CHECK(fetch(gba, GBA_BASE_ROM0 + 0x102, 2, false) == 5);
        idle(gba, 6);
        CHECK(fetch(gba, GBA_BASE_ROM0 + 0x104, 4, false) == 1);
        CHECK(fetch(gba, GBA_BASE_ROM0 + 0x108, 4, true) == 5);
        destroy(gba);
    }
}

static void arm_exception_return_retains_thumb_halfwords(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - 10u, UINT64_C(0x300000000)};
    const uint16_t program[] = {0x2001, 0x2002, 0x2003, 0x2004}; /* MOVS r0, #1..4 */
    const uint32_t nop = 0xE1A00000;
    for (unsigned c = 0; c < sizeof(clocks) / sizeof(*clocks); c++) {
        for (unsigned ram = 0; ram < 2; ram++) {
            struct CupidGBA* gba = machine(clocks[c]);
            struct CupidARM* cpu = &gba->cpu;
            memcpy(gba->memory.rom + 0x104, program, sizeof(program));
            CHECK(fetch(gba, GBA_BASE_ROM0 + 0x100, 4, false) == 8);
            idle(gba, 6);
            /* Fetches outside ROM retain the FIFO. This owned exception
             * epilogue restores Thumb execution at the first buffered halfword. */
            const uint32_t handler = ram ? GBA_BASE_IWRAM + 0x100 : 0x100;
            memcpy((ram ? gba->memory.iwram : gba->memory.bios) + 0x108, &nop, sizeof(nop));
            cupid_arm_set_priv_mode(cpu, MODE_IRQ);
            cpu->cpsr.priv = MODE_IRQ;
            cpu->cpsr.i = 1;
            cpu->spsr.packed = MODE_SYS | 0x20;
            cpu->gprs[ARM_LR] = GBA_BASE_ROM0 + 0x108;
            cpu->gprs[ARM_PC] = handler + 4;
            cpu->prefetch[0] = 0xE25EF004; /* SUBS pc, lr, #4 */
            cpu->prefetch[1] = nop;
            cupid_memory_set_active_region(cpu, handler);
            const uint64_t before = cupid_timing_global_time(&gba->timing);
            cupid_arm_step(cpu);
            CHECK(cupid_timing_global_time(&gba->timing) == before + 3);
            CHECK(cpu->executionMode == MODE_THUMB && cpu->privilegeMode == MODE_SYS);
            CHECK(cpu->prefetch[0] == program[0] && cpu->prefetch[1] == program[1]);
            CHECK((uint32_t)cpu->gprs[ARM_PC] == GBA_BASE_ROM0 + 0x106);
            cupid_thumb_step(cpu);
            CHECK(cpu->gprs[0] == 1 && cupid_timing_global_time(&gba->timing) == before + 4);
            cupid_thumb_step(cpu);
            CHECK(cpu->gprs[0] == 2 && cupid_timing_global_time(&gba->timing) == before + 6);
            destroy(gba);
        }
    }
}

static void disabled_prefetch_drains_and_restarts_the_burst(void) {
    struct CupidGBA* gba = machine(0);
    struct CupidPrefetch* unit = &gba->memory.prefetchUnit;
    fetch(gba, GBA_BASE_ROM0 + 0x100, 4, false);
    idle(gba, 6);
    CHECK(unit->count == 2);
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
    CHECK(gba->memory.prefetchUnit.duty == 2);
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
            CHECK(a->remaining == b->remaining && a->active == b->active && a->busy == b->busy &&
                  a->paused == b->paused && a->engaged == b->engaged);
            CHECK(a->epoch == b->epoch);
            CHECK(late->memory.dma[3].busyUntil == punctual->memory.dma[3].busyUntil);
            CHECK(late->irqf == punctual->irqf && late->irq.line == punctual->irq.line);
            CHECK(cupid_io_read(late, REG_TM0CNT_L) == cupid_io_read(punctual, REG_TM0CNT_L));
            CHECK(!late->memory.dmaRunning && !punctual->memory.dmaRunning);
            CHECK(external ? !a->active : a->count == 8);
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

#include "prefetch-full-kernel.inc"

static void only_started_oldest_reads_can_serve_refills(void) {
    const uint64_t origins[] = {0, UINT32_MAX - 10u, UINT64_C(0x300000000)};
    const uint32_t banks[] = {GBA_BASE_ROM0, GBA_BASE_ROM1, GBA_BASE_ROM2};
    const unsigned sequential[] = {3, 5, 9};
    for (unsigned clock = 0; clock < 3; clock++) for (unsigned bank = 0; bank < 3; bank++) {
        struct CupidGBA* gba = machine(origins[clock]);
        struct CupidPrefetch* unit = &gba->memory.prefetchUnit;
        const uint32_t base = banks[bank] + 0x100;
        CHECK(fetch(gba, base, 2, false) == 5 && !unit->engaged);
        CHECK(fetch(gba, base + 2, 2, true) == sequential[bank] && !unit->engaged);
        /* A sequential CPU request can continue the burst without an idle
         * interval. A branch cannot join a background read that never began. */
        uint64_t refillClocks = fetch(gba, base + 4, 2, false);
        if (refillClocks != 5)
            fprintf(stderr, "Unstarted refill clocks: observed %u, expected 5\n", (unsigned)refillClocks);
        CHECK(refillClocks == 5 && !unit->engaged);
        idle(gba, 1);
        CHECK(unit->engaged && !unit->count);
        CHECK(fetch(gba, base + 6, 2, false) == sequential[bank] - 1);
        idle(gba, 4 * sequential[bank]);
        CHECK(unit->count == 4);
        uint32_t tail = unit->tail;
        /* Skipping older FIFO entries starts a new access even when the new
         * target happens to be the word currently being fetched. */
        CHECK(fetch(gba, tail, 2, false) == 5 && !unit->engaged && !unit->count);
        destroy(gba);
    }
}

int main(void) {
    RUN(wait_controls_before_fetch_and_after_invalid_jump);
    RUN(initial_fetch_pending_completion_and_cache_consumption);
    RUN(queue_capacity_is_bounded_and_reset_clears_pending_state);
    RUN(full_queue_rom_data_flush_kernel);
    RUN(only_started_oldest_reads_can_serve_refills);
    RUN(cpu_ram_data_fills_and_gamepak_data_cancels);
    RUN(completing_halfword_arbitration_and_branch_width_changes);
    RUN(partial_words_and_mixed_instruction_widths);
    RUN(arm_exception_return_retains_thumb_halfwords);
    RUN(disabled_prefetch_drains_and_restarts_the_burst);
    RUN(sequential_costs_waitcnt_and_page_restart);
    RUN(dma_clock_accounting_matches_late_and_punctual_dispatch);
    RUN(dma_external_write_waits_for_completing_prefetch_halfword);
    return 0;
}
