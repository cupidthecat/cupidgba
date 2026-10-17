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
    gba->audio.lastSample = clock;
    gba->audio.sampleOrigin = clock;
    gba->audio.nextSample = clock + 512;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void tick(struct CupidGBA* gba, int cycles) {
    CHECK(gba->cpu.cycles == 0);
    cupid_timing_tick(&gba->timing, cycles);
}

static void start(struct CupidGBA* gba, int ch, uint32_t source, uint32_t dest,
                   uint16_t count, uint16_t control) {
    cupid_dma_write_sad(gba, ch, source);
    cupid_dma_write_dad(gba, ch, dest);
    cupid_dma_write_cnt_lo(gba, ch, count);
    cupid_dma_write_cnt(gba, ch, control);
}

static void program(struct CupidGBA* gba, uint32_t first) {
    const uint32_t code[] = {first, 0xE1A00000, 0xE1A00000, 0xE1A00000};
    memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
    CupidARMWritePC(&gba->cpu);
}

static void phases_sample_reads_and_publish_writes(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    for (unsigned i = 0; i < sizeof(clocks) / sizeof(*clocks); i++) {
        struct CupidGBA* gba = machine(clocks[i]);
        cupid_memory_store32(&gba->cpu, GBA_BASE_EWRAM, 0x12345678, NULL);
        start(gba, 0, GBA_BASE_EWRAM, GBA_BASE_IWRAM, 1, 0x8400);
        tick(gba, 2);
        CHECK(gba->memory.dmaRunning && gba->memory.dma[0].latch == 0);
        tick(gba, 6); /* Handover + five of the six EWRAM read clocks. */
        CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM, NULL) == 0);
        cupid_memory_store32(&gba->cpu, GBA_BASE_EWRAM, (int32_t)0x89ABCDEF, NULL);
        tick(gba, 1);
        CHECK(gba->memory.dma[0].latch == UINT32_C(0x89ABCDEF));
        CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM, NULL) == 0);
        cupid_memory_store32(&gba->cpu, GBA_BASE_EWRAM, 0, NULL);
        tick(gba, 1);
        CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM, NULL) == UINT32_C(0x89ABCDEF));
        CHECK(gba->memory.dma[0].busyUntil == clocks[i] + 10);
        CHECK(!gba->memory.dma[0].enable && gba->memory.dmaRunning);
        CHECK(cupid_io_read(gba, REG_DMA0CNT_HI) == 0x0400);
        tick(gba, 1);
        CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked);
        destroy(gba);
    }
}

static void masks_counts_and_enable_latches(void) {
    for (int ch = 0; ch < 4; ch++) {
        struct CupidGBA* gba = machine(0);
        start(gba, ch, UINT32_MAX, UINT32_MAX, 0xFFFF, 0xFFFF);
        struct CupidDMA* dma = &gba->memory.dma[ch];
        CHECK(dma->source == (ch ? 0x0FFFFFFF : 0x07FFFFFF));
        CHECK(dma->dest == (ch == 3 ? 0x0FFFFFFF : 0x07FFFFFF));
        CHECK(dma->count == (ch == 3 ? 0xFFFF : 0x3FFF));
        CHECK(dma->reg == (ch == 3 ? 0xFFE0 : 0xF7E0));
        CHECK((dma->nextSource & 3) == 0 && (dma->nextDest & 3) == 0);
        cupid_dma_write_cnt(gba, ch, 0);
        /* Zero length runs the maximum count; immediate repeat still disables. */
        cupid_memory_store16(&gba->cpu, GBA_BASE_IWRAM, 0x5A81, NULL);
        start(gba, ch, GBA_BASE_IWRAM + 1, GBA_BASE_EWRAM + 1, 0, 0x8340);
        uint32_t count = ch == 3 ? 0x10000 : 0x4000;
        CHECK(dma->nextCount == (int32_t)count);
        CHECK(dma->nextSource == GBA_BASE_IWRAM && dma->nextDest == GBA_BASE_EWRAM);
        cupid_dma_write_sad(gba, ch, GBA_BASE_IWRAM + 0x100);
        cupid_dma_write_dad(gba, ch, GBA_BASE_EWRAM + 0x100);
        cupid_dma_write_cnt_lo(gba, ch, 1);
        tick(gba, (int)(count * 4 + 4));
        CHECK(!dma->enable && dma->nextCount == 0);
        CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0x5A81);
        CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM + 0x100, NULL) == 0);
        CHECK(dma->nextSource == GBA_BASE_IWRAM && dma->nextDest == GBA_BASE_EWRAM);
        CHECK(!gba->memory.dmaRunning);
        destroy(gba);
    }
}

static void stepping_modes_and_repeat_reload(void) {
    static const int offset[] = {1, -1, 0, 1};
    for (unsigned word = 0; word < 2; word++) {
        unsigned width = word ? 4 : 2;
        for (unsigned src = 0; src < 3; src++) {
            for (unsigned dst = 0; dst < 4; dst++) {
                struct CupidGBA* gba = machine(0);
                uint32_t expected[16] = {0};
                for (unsigned i = 0; i < 16; i++) {
                    cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM + i * 4,
                                         (int32_t)(0x12340000u + i), NULL);
                }
                uint32_t sad = GBA_BASE_IWRAM + 24, dad = GBA_BASE_IWRAM + 0x100 + 24;
                for (unsigned i = 0; i < 3; i++) {
                    uint32_t address = sad + (uint32_t)(offset[src] * (int)width * (int)i);
                    unsigned target = 24 + offset[dst] * (int)width * (int)i;
                    uint32_t value = word ? cupid_memory_load32(&gba->cpu, address, NULL)
                                          : cupid_memory_load16(&gba->cpu, address, NULL);
                    if (word) memcpy((uint8_t*)expected + target, &value, 4);
                    else { uint16_t half = (uint16_t)value; memcpy((uint8_t*)expected + target, &half, 2); }
                }
                start(gba, 3, sad, dad, 3, (uint16_t)(0x8000 | (word << 10) | (src << 7) | (dst << 5)));
                tick(gba, 10);
                CHECK(memcmp(gba->memory.iwram + 0x100, expected, sizeof(expected)) == 0);
                CHECK(gba->memory.dma[3].nextSource == sad + (uint32_t)(offset[src] * (int)width * 3));
                CHECK(gba->memory.dma[3].nextDest == dad + (uint32_t)(offset[dst] * (int)width * 3));
                destroy(gba);
            }
        }
    }
    struct CupidGBA* gba = machine(0);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM, 0x76543210, NULL);
    start(gba, 1, GBA_BASE_IWRAM, GBA_BASE_EWRAM, 2, 0xA260);
    cupid_dma_run_hblank(gba, 0);
    tick(gba, 12);
    CHECK(gba->memory.dma[1].enable && gba->memory.dma[1].nextCount == 2);
    CHECK(gba->memory.dma[1].nextDest == GBA_BASE_EWRAM);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0x76543210);
    cupid_dma_write_dad(gba, 1, GBA_BASE_EWRAM + 0x100);
    cupid_dma_write_cnt_lo(gba, 1, 1);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM + 4, (int32_t)0xABCDEF98, NULL);
    cupid_dma_run_hblank(gba, 0);
    tick(gba, 12);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM, NULL) == UINT32_C(0xABCDEF98));
    CHECK(gba->memory.dma[1].nextDest == GBA_BASE_EWRAM + 0x100);
    CHECK(gba->memory.dma[1].nextCount == 1);
    destroy(gba);
}

static void channel_priority_and_preemption(void) {
    struct CupidGBA* gba = machine(0);
    for (int ch = 3; ch >= 0; ch--) {
        cupid_memory_store16(&gba->cpu, GBA_BASE_IWRAM + ch * 2, (int16_t)(0x1230 + ch), NULL);
        start(gba, ch, GBA_BASE_IWRAM + ch * 2, GBA_BASE_EWRAM, 1, 0x8000);
    }
    tick(gba, 7); /* Head + channel 0 read and EWRAM write. */
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0x1230);
    for (int ch = 1; ch < 4; ch++) {
        tick(gba, 4);
        CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM, NULL) == (uint32_t)(0x1230 + ch));
    }
    tick(gba, 1);
    CHECK(!gba->memory.dmaRunning);
    destroy(gba);

    gba = machine(0);
    gba->memory.rom = calloc(1, 0x400);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 0x400;
    const uint16_t rom[] = {0x1111, 0x2222, 0x3333};
    memcpy(gba->memory.rom + 0x100, rom, sizeof(rom));
    cupid_memory_store16(&gba->cpu, GBA_BASE_IWRAM + 0x100, (int16_t)0xABCD, NULL);
    start(gba, 3, GBA_BASE_ROM0 + 0x100, GBA_BASE_IWRAM, 3, 0xC000);
    start(gba, 0, GBA_BASE_IWRAM + 0x100, GBA_BASE_IWRAM + 0x10, 1, 0xD000);
    tick(gba, 5);
    cupid_dma_transfer(gba, 0); /* Activation at 7, during DMA3's ROM read. */
    tick(gba, 3);
    CHECK(gba->memory.activeDMA == 3 && gba->memory.dma[0].ready);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IWRAM, NULL) == 0);
    tick(gba, 1);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IWRAM, NULL) == 0x1111);
    CHECK(gba->memory.activeDMA == 0);
    tick(gba, 2);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IWRAM + 0x10, NULL) == 0xABCD);
    CHECK(gba->memory.activeDMA == 3 && gba->memory.dma[3].nextCount == 2);
    tick(gba, 5); /* Resuming the ROM burst requires a new nonsequential read. */
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IWRAM + 2, NULL) == 0);
    tick(gba, 1);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IWRAM + 2, NULL) == 0x2222);
    tick(gba, 4);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IWRAM + 4, NULL) == 0x3333);
    CHECK(gba->memory.dma[3].busyUntil == 21 && gba->irqf == IRQ_DMA0);
    tick(gba, 1);
    CHECK(gba->irqf == (IRQ_DMA0 | IRQ_DMA3) && !gba->memory.dmaRunning);
    destroy(gba);
}

static void cpu_bus_stalls_internal_overlap_and_swap_lock(void) {
    for (unsigned longMultiply = 0; longMultiply < 2; longMultiply++) {
        struct CupidGBA* gba = machine(0);
        program(gba, 0xE0000291); /* mul r0, r1, r2 */
        gba->cpu.gprs[1] = 3;
        gba->cpu.gprs[2] = longMultiply ? 0x12345678 : 1;
        cupid_memory_store16(&gba->cpu, GBA_BASE_IWRAM, 0x1234, NULL);
        start(gba, 0, GBA_BASE_IWRAM, GBA_BASE_IWRAM + 0x100, 1, 0x8000);
        cupid_gba_step(gba);
        CHECK(cupid_timing_global_time(&gba->timing) == (longMultiply ? 5 : 2));
        CHECK(gba->memory.dmaRunning);
        cupid_gba_step(gba);
        CHECK(cupid_timing_global_time(&gba->timing) == 7);
        CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IWRAM + 0x100, NULL) == 0x1234);
        destroy(gba);
    }
    struct CupidGBA* gba = machine(0);
    program(gba, 0xE5910000); /* ldr r0, [r1] */
    gba->cpu.gprs[1] = GBA_BASE_EWRAM;
    cupid_memory_store32(&gba->cpu, GBA_BASE_EWRAM, 0x1234, NULL);
    cupid_memory_store16(&gba->cpu, GBA_BASE_IWRAM, 0x5678, NULL);
    start(gba, 0, GBA_BASE_IWRAM, GBA_BASE_EWRAM, 1, 0x8000);
    cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[0] == 0x1234 && cupid_timing_global_time(&gba->timing) == 8);
    CHECK(gba->memory.dmaRunning && gba->memory.dma[0].latch == 0);
    cupid_gba_step(gba);
    CHECK(cupid_timing_global_time(&gba->timing) == 14);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0x5678);
    destroy(gba);

    gba = machine(0);
    program(gba, 0xE1010092); /* swp r0, r2, [r1] */
    gba->cpu.gprs[1] = GBA_BASE_IWRAM + 0x100;
    gba->cpu.gprs[2] = 0x2222;
    cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM + 0x100, 0x1111, NULL);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM, 0x3333, NULL);
    start(gba, 0, GBA_BASE_IWRAM, GBA_BASE_IWRAM + 0x100, 1, 0x8400);
    cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[0] == 0x1111 && !gba->memory.busLocked);
    CHECK(cupid_timing_global_time(&gba->timing) == 4);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM + 0x100, NULL) == 0x2222);
    cupid_gba_step(gba);
    CHECK(cupid_timing_global_time(&gba->timing) == 8);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM + 0x100, NULL) == 0x3333);
    destroy(gba);
}

static void latches_are_independent_and_halfwords_select_destination_lane(void) {
    struct CupidGBA* gba = machine(0);
    for (int ch = 0; ch < 4; ch++) {
        uint32_t value = UINT32_C(0xDEAD1234) + (uint32_t)ch;
        cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM, (int32_t)value, NULL);
        start(gba, ch, GBA_BASE_IWRAM, GBA_BASE_EWRAM, 1, 0x8400);
        tick(gba, 11);
        CHECK(gba->memory.dma[ch].latch == value);
    }
    for (int ch = 0; ch < 4; ch++) {
        start(gba, ch, 0, GBA_BASE_EWRAM + 0x100 + ch * 4, 2, 0x8000);
        tick(gba, 12);
        CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM + 0x100 + ch * 4, NULL)
              == UINT32_C(0xDEAD1234) + (uint32_t)ch);
        CHECK(gba->memory.dma[ch].latch == UINT32_C(0xDEAD1234) + (uint32_t)ch);
    }
    cupid_memory_store16(&gba->cpu, GBA_BASE_IWRAM, 0x5A81, NULL);
    start(gba, 2, GBA_BASE_IWRAM, GBA_BASE_EWRAM, 1, 0x8000);
    tick(gba, 8);
    CHECK(gba->memory.dma[2].latch == UINT32_C(0x5A815A81));
    destroy(gba);
}

static void late_dma_mmio_matches_every_clock(void) {
    struct CupidGBA* late = machine(0);
    struct CupidGBA* punctual = machine(0);
    struct CupidGBA* machines[] = {late, punctual};
    for (unsigned i = 0; i < 2; i++) {
        struct CupidGBA* gba = machines[i];
        cupid_memory_store16(&gba->cpu, GBA_BASE_IWRAM, 0x80, NULL);
        cupid_io_write(gba, REG_TM0CNT_L, 0x1234);
        start(gba, 3, GBA_BASE_IWRAM, GBA_BASE_IO + REG_TM0CNT_H, 1, 0xC000);
    }
    tick(late, 20);
    for (int clock = 0; clock < 20; clock++) tick(punctual, 1);
    CHECK(cupid_io_read(late, REG_TM0CNT_L) == 0x1241);
    CHECK(cupid_io_read(late, REG_TM0CNT_L) == cupid_io_read(punctual, REG_TM0CNT_L));
    CHECK(late->irqf == IRQ_DMA3 && late->irqf == punctual->irqf);
    CHECK(late->timers[0].epoch == punctual->timers[0].epoch);
    CHECK(late->memory.dma[3].busyUntil == punctual->memory.dma[3].busyUntil);
    destroy(late);
    destroy(punctual);
}

static void fifo_refills_and_irqs_match_late_dispatch(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - 10u, UINT64_C(0x200000000)};
    for (unsigned c = 0; c < sizeof(clocks) / sizeof(*clocks); c++) {
        struct CupidGBA* late = machine(clocks[c]);
        struct CupidGBA* punctual = machine(clocks[c]);
        struct CupidGBA* machines[] = {late, punctual};
        for (unsigned i = 0; i < 2; i++) {
            struct CupidGBA* gba = machines[i];
            gba->audio.enable = true;
            gba->audio.chBTimer = true;
            for (unsigned word = 0; word < 64; word++) {
                cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM + word * 4,
                                     (int32_t)(0x04030201u + word * 0x04040404u), NULL);
            }
            start(gba, 1, GBA_BASE_IWRAM + 3, GBA_BASE_IO + REG_FIFO_A_LO + 3, 99, 0xF260);
            /* Width, count and destination mode are forced by FIFO operation,
             * while the programmed control bits remain readable. */
            CHECK(cupid_io_read(gba, REG_DMA1CNT_HI) == 0xF260);
            CHECK(gba->memory.dma[1].nextCount == 4);
            cupid_io_write(gba, REG_IE, IRQ_TIMER0 | IRQ_DMA1);
            cupid_io_write(gba, REG_IME, 1);
            cupid_io_write(gba, REG_TM0CNT_L, 0xFFFF);
            cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
            cupid_io_write(gba, REG_TM1CNT_L, 0);
            cupid_io_write(gba, REG_TM1CNT_H, 0x84);
        }
        tick(late, 100);
        for (int clock = 0; clock < 100; clock++) tick(punctual, 1);
        CHECK(memcmp(&late->audio.chA, &punctual->audio.chA, sizeof(late->audio.chA)) == 0);
        CHECK(late->memory.dma[1].nextSource == punctual->memory.dma[1].nextSource);
        CHECK(late->memory.dma[1].nextDest == GBA_BASE_IO + REG_FIFO_A_LO);
        CHECK(late->memory.dma[1].nextCount == punctual->memory.dma[1].nextCount);
        CHECK(late->memory.dma[1].latch == punctual->memory.dma[1].latch);
        CHECK(late->memory.dma[1].busyUntil == punctual->memory.dma[1].busyUntil);
        CHECK(late->memory.dmaPhase == punctual->memory.dmaPhase);
        CHECK(cupid_io_read(late, REG_TM1CNT_L) == 98);
        CHECK(late->irqf == (IRQ_TIMER0 | IRQ_DMA1));
        CHECK(late->irqf == punctual->irqf && late->irq.line == punctual->irq.line);
        destroy(late);
        destroy(punctual);
    }
}

static void pending_and_preempted_transfers_can_be_cancelled(void) {
    struct CupidGBA* gba = machine(0);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM, 0x12345678, NULL);
    start(gba, 0, GBA_BASE_IWRAM, GBA_BASE_EWRAM, 1, 0xC400);
    tick(gba, 1);
    cupid_dma_write_cnt(gba, 0, 0);
    tick(gba, 10);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0);
    CHECK(!gba->memory.dmaRunning && gba->irqf == 0);
    destroy(gba);

    gba = machine(0);
    cupid_memory_store16(&gba->cpu, GBA_BASE_IWRAM, 0x1234, NULL);
    cupid_memory_store16(&gba->cpu, GBA_BASE_IWRAM + 0x100, 0, NULL);
    start(gba, 3, GBA_BASE_IWRAM, GBA_BASE_EWRAM, 4, 0xC000);
    start(gba, 0, GBA_BASE_IWRAM + 0x100, GBA_BASE_IO + REG_DMA3CNT_HI, 1, 0x9000);
    tick(gba, 4);
    cupid_dma_transfer(gba, 0); /* Ready at 6, before DMA3's first write at 7. */
    tick(gba, 6);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0x1234);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM + 2, NULL) == 0);
    CHECK(!gba->memory.dma[3].enable && !gba->memory.dmaRunning);
    CHECK(gba->irqf == 0);
    destroy(gba);
}

static void waitstates_gamepak_counter_and_burst_restart(void) {
    struct CupidGBA* gba = machine(0);
    gba->memory.rom = calloc(1, 0x40000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 0x40000;
    const uint16_t rom[] = {0x1111, 0x2222, 0x3333};
    memcpy(gba->memory.rom, rom, sizeof(rom));
    /* The first ROM write latches address 0; subsequent sequential reads
     * continue at 2 and 4 when the source crosses from OAM into ROM. */
    start(gba, 3, GBA_BASE_ROM0 - 2, GBA_BASE_ROM0, 3, 0x8020);
    tick(gba, 18); /* Request 2 + head 1 + (1+5) + (3+1) + (3+1) + tail 1. */
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_ROM0 - 2, NULL) == 0x2222);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_ROM0 - 4, NULL) == 0x3333);
    CHECK(gba->memory.dma[3].busyUntil == 17 && !gba->memory.dmaRunning);
    /* WAITCNT WS0 uses two nonsequential and one sequential wait clocks. */
    cupid_memory_set_active_region(&gba->cpu, GBA_BASE_IWRAM);
    cupid_memory_update_waitstates(gba, 0x18);
    memcpy(gba->memory.rom + 0x1FFFC, rom, sizeof(rom));
    start(gba, 3, GBA_BASE_ROM0 + 0x1FFFC, GBA_BASE_IWRAM, 3, 0x8000);
    tick(gba, 15);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IWRAM, NULL) == 0x1111);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IWRAM + 2, NULL) == 0x2222);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IWRAM + 4, NULL) == 0x3333);
    CHECK(gba->memory.dma[3].busyUntil == 18 + 14);
    destroy(gba);
}

static void fixed_rom_sources_keep_their_pointer_and_advance_cartridge_data(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    const uint32_t banks[] = {GBA_BASE_ROM0, GBA_BASE_ROM1, GBA_BASE_ROM2};
    const unsigned sequential[] = {3, 5, 9};
    const uint32_t words[] = {0x12345678, 0x9ABCDEF0, 0x13579BDF, 0x2468ACE0};
    const uint16_t halves[] = {0x5678, 0x1234, 0xDEF0, 0x9ABC};
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned bank = 0; bank < 3; bank++) {
        for (unsigned word = 0; word < 2; word++) {
            struct CupidGBA* gba = machine(clocks[epoch]);
            gba->memory.rom = calloc(1, 0x2000);
            CHECK(gba->memory.rom);
            gba->memory.romSize = 0x2000;
            memcpy(gba->memory.rom + 0x100, words, sizeof(words));
            unsigned width = word ? 4 : 2;
            uint32_t source = banks[bank] + 0x100, dest = GBA_BASE_IWRAM + 0x100;
            start(gba, 3, source, dest, 4, (uint16_t)(0x8100 | (word << 10)));
            unsigned duration = 13 + (word ? 7 : 3) * sequential[bank];
            tick(gba, (int)duration);
            const struct CupidDMA* dma = &gba->memory.dma[3];
            CHECK(!dma->enable && !gba->memory.dmaRunning);
            CHECK(dma->nextSource == source && dma->nextDest == dest + 4 * width);
            CHECK(dma->busyUntil == clocks[epoch] + duration - 1);
            for (unsigned i = 0; i < 4; i++) {
                uint32_t actual = word ? (uint32_t)cupid_memory_load32(&gba->cpu, dest + i * width, NULL)
                                       : cupid_memory_loadU16(&gba->cpu, dest + i * width, NULL);
                CHECK(actual == (word ? words[i] : halves[i]));
            }
            destroy(gba);
        }
    }
}

static void decrementing_rom_sources_can_cross_back_into_oam(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    const unsigned widths[] = {4, 2};
    const uint16_t rom[] = {0x0123, 0x4567, 0x89AB, 0xCDEF, 0x2345,
                            0x6789, 0xABCD, 0xEF01, 0x3456, 0x789A};
    const uint32_t words[] = {0x67892345, 0xEF01ABCD, 0x789A3456, 0x12345678};
    const uint16_t halves[] = {0x89AB, 0xCDEF, 0x2345, 0x1234};
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned size = 0; size < 2; size++) {
        struct CupidGBA* gba = machine(clocks[epoch]);
        gba->memory.rom = calloc(1, 0x2000);
        CHECK(gba->memory.rom);
        gba->memory.romSize = 0x2000;
        memcpy(gba->memory.rom, rom, sizeof(rom));
        cupid_memory_store32(&gba->cpu, GBA_BASE_OAM + 0x3FC, 0x12345678, NULL);
        unsigned width = widths[size];
        uint32_t dest = GBA_BASE_IWRAM + 0x100;
        start(gba, 3, GBA_BASE_ROM0 + 2 * width, dest, 4,
              (uint16_t)(0x8080 | (width == 4 ? 0x0400 : 0)));
        tick(gba, 40);
        const struct CupidDMA* dma = &gba->memory.dma[3];
        uint64_t duration = dma->busyUntil - clocks[epoch] + 1;
        unsigned expected = width == 4 ? 29 : 20;
        if (duration != expected)
            fprintf(stderr, "ROM-to-OAM DMA clocks: observed %u, expected %u\n", (unsigned)duration, expected);
        CHECK(duration == expected && !dma->enable && !gba->memory.dmaRunning);
        CHECK(dma->nextSource == GBA_BASE_ROM0 - 2 * width && dma->nextDest == dest + 4 * width);
        for (unsigned i = 0; i < 4; i++) {
            uint32_t actual = width == 4 ? (uint32_t)cupid_memory_load32(&gba->cpu, dest + i * width, NULL)
                                         : cupid_memory_loadU16(&gba->cpu, dest + i * width, NULL);
            CHECK(actual == (width == 4 ? words[i] : halves[i]));
        }
        destroy(gba);
    }
}

static void display_requests_and_blanking_modes(void) {
    struct CupidGBA* gba = machine(0);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM, 0x12345678, NULL);
    start(gba, 0, GBA_BASE_IWRAM, GBA_BASE_EWRAM, 1, 0x9000);
    start(gba, 1, GBA_BASE_IWRAM + 2, GBA_BASE_EWRAM + 2, 1, 0xA000);
    cupid_dma_run_hblank(gba, 0);
    tick(gba, 8);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM + 2, NULL) == 0x1234);
    CHECK(gba->memory.dma[0].enable && !gba->memory.dma[1].enable);
    cupid_dma_run_vblank(gba, 0);
    tick(gba, 8);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0x5678);
    CHECK(!gba->memory.dma[0].enable);
    start(gba, 3, GBA_BASE_IWRAM, GBA_BASE_EWRAM + 0x100, 1, 0xB200);
    gba->video.vcount = 1;
    cupid_dma_run_display(gba);
    tick(gba, 8);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM + 0x100, NULL) == 0);
    gba->video.vcount = 2;
    cupid_dma_run_display(gba);
    tick(gba, 8);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM + 0x100, NULL) == 0x5678);
    gba->video.vcount = 161;
    cupid_dma_run_display(gba);
    tick(gba, 8);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM + 0x102, NULL) == 0x1234);
    gba->video.vcount = 162;
    cupid_dma_run_display(gba);
    CHECK(!gba->memory.dma[3].enable && cupid_io_read(gba, REG_DMA3CNT_HI) == 0x3200);
    destroy(gba);
}

static void cpu_internal_cycles_restart_fetches_and_precede_branch_refills(void) {
    for (unsigned mode = 0; mode < 2; mode++) {
        for (unsigned shift = 0; shift < 2; shift++) {
            struct CupidGBA* gba = machine(0);
            gba->memory.rom = calloc(1, 0x400);
            CHECK(gba->memory.rom);
            gba->memory.romSize = 0x400;
            gba->memory.romMask = 0x3FF;
            if (mode) {
                const uint16_t code[] = {(uint16_t)(shift ? 0x4088 : 0x4348), 0x46C0, 0x46C0, 0x46C0};
                memcpy(gba->memory.rom + 0x100, code, sizeof(code));
                _CupidSetMode(&gba->cpu, MODE_THUMB);
                gba->cpu.gprs[ARM_PC] = GBA_BASE_ROM0 + 0x100;
                CupidThumbWritePC(&gba->cpu);
            } else {
                const uint32_t code[] = {shift ? 0xE1A00211 : 0xE0000291, 0xE1A00000, 0xE1A00000};
                memcpy(gba->memory.rom + 0x100, code, sizeof(code));
                gba->cpu.gprs[ARM_PC] = GBA_BASE_ROM0 + 0x100;
                CupidARMWritePC(&gba->cpu);
            }
            gba->cpu.gprs[0] = 1;
            gba->cpu.gprs[1] = 1;
            gba->cpu.gprs[2] = 1;
            cupid_gba_step(gba);
            CHECK(cupid_timing_global_time(&gba->timing) == (mode ? 4 : 7));
            cupid_gba_step(gba);
            CHECK(cupid_timing_global_time(&gba->timing) == (mode ? 9 : 15));
            destroy(gba);
        }
    }
    struct CupidGBA* gba = machine(0);
    program(gba, 0xE590F000); /* ldr pc, [r0] */
    gba->cpu.gprs[0] = GBA_BASE_IWRAM + 0x100;
    cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM + 0x100, GBA_BASE_IWRAM + 0x2000, NULL);
    cupid_gba_step(gba);
    CHECK(cupid_timing_global_time(&gba->timing) == 5);
    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + 0x2004);
    destroy(gba);
}

int main(void) {
    RUN(phases_sample_reads_and_publish_writes);
    RUN(masks_counts_and_enable_latches);
    RUN(stepping_modes_and_repeat_reload);
    RUN(channel_priority_and_preemption);
    RUN(cpu_bus_stalls_internal_overlap_and_swap_lock);
    RUN(latches_are_independent_and_halfwords_select_destination_lane);
    RUN(late_dma_mmio_matches_every_clock);
    RUN(fifo_refills_and_irqs_match_late_dispatch);
    RUN(pending_and_preempted_transfers_can_be_cancelled);
    RUN(waitstates_gamepak_counter_and_burst_restart);
    RUN(fixed_rom_sources_keep_their_pointer_and_advance_cartridge_data);
    RUN(decrementing_rom_sources_can_cross_back_into_oam);
    RUN(display_requests_and_blanking_modes);
    RUN(cpu_internal_cycles_restart_fetches_and_precede_branch_refills);
    return 0;
}
