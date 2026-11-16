/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_gamepak_request.c
 * @author    cupidthecat
 * @brief     DMA3 Game Pak request gating without an attached request source.
 * @date      2026-11-16
 *
 * - Keep DMA3 armed until an external Game Pak request can begin a transfer.
 * - Verify normal DMA controls, guest writes and pending-state lifecycle.
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
#include <limits.h>
#include <string.h>

enum { SOURCE_OFFSET = 0x400, DEST_OFFSET = 0x800, WINDOW_SIZE = 64 };

static const uint64_t epochs[] = {0, INT32_MAX - 8u, UINT32_MAX - 8u, UINT64_C(0x300000000)};

static struct CupidGBA *machine(unsigned epoch) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba && epoch < sizeof(epochs) / sizeof(*epochs));
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epochs[epoch];
    gba->timing.masterCycles = (uint32_t)epochs[epoch];
    gba->audio.lastSample = epochs[epoch];
    gba->audio.sampleOrigin = epochs[epoch];
    gba->audio.nextSample = epochs[epoch] + 512;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    for (unsigned i = 0; i < WINDOW_SIZE; i++)
        gba->memory.iwram[SOURCE_OFFSET + i] = (uint8_t)(0x21 + i * 3);
    memset(gba->memory.ewram + DEST_OFFSET, 0xA5, WINDOW_SIZE);
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static uint16_t control(unsigned width, bool drq, bool irq) {
    return (uint16_t)(0x8000 | (width == 4 ? 0x0400 : 0) | (drq ? 0x0800 : 0) | (irq ? 0x4000 : 0));
}

static void program(struct CupidGBA *gba, int channel, unsigned width, uint16_t count,
                    uint16_t cntHi, bool unaligned) {
    unsigned offset = unaligned ? width - 1 : 0;
    cupid_dma_write_sad(gba, channel, GBA_BASE_IWRAM + SOURCE_OFFSET + offset);
    cupid_dma_write_dad(gba, channel, GBA_BASE_EWRAM + DEST_OFFSET + offset);
    cupid_dma_write_cnt_lo(gba, channel, count);
    cupid_dma_write_cnt(gba, channel, cntHi);
}

static bool pending(const struct CupidGBA *gba, unsigned channel) {
    return cupid_timing_is_scheduled(&gba->timing, &gba->memory.dma[channel].requestEvent);
}

static void source_unchanged(const struct CupidGBA *gba) {
    for (unsigned i = 0; i < WINDOW_SIZE; i++)
        CHECK(gba->memory.iwram[SOURCE_OFFSET + i] == (uint8_t)(0x21 + i * 3));
}

static void guard_destination(const struct CupidGBA *gba, unsigned written) {
    uint8_t expected[WINDOW_SIZE];
    CHECK(written <= WINDOW_SIZE);
    memset(expected, 0xA5, sizeof(expected));
    for (unsigned i = 0; i < written; i++)
        expected[i] = (uint8_t)(0x21 + i * 3);
    CHECK(memcmp(gba->memory.ewram + DEST_OFFSET, expected, sizeof(expected)) == 0);
    source_unchanged(gba);
}

static void waiting(struct CupidGBA *gba, unsigned width, uint16_t count, uint16_t cntHi) {
    const struct CupidDMA *dma = &gba->memory.dma[3];
    CHECK(cntHi & 0x0800);
    CHECK(dma->enable && !dma->ready && !pending(gba, 3));
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &dma->completionEvent));
    CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked);
    CHECK(gba->memory.activeDMA == -1);
    CHECK(dma->reg == cntHi && cupid_io_read(gba, REG_DMA3CNT_HI) == cntHi);
    CHECK(dma->source == GBA_BASE_IWRAM + SOURCE_OFFSET + width - 1);
    CHECK(dma->dest == GBA_BASE_EWRAM + DEST_OFFSET + width - 1);
    CHECK(dma->count == count);
    CHECK(dma->nextSource == GBA_BASE_IWRAM + SOURCE_OFFSET);
    CHECK(dma->nextDest == GBA_BASE_EWRAM + DEST_OFFSET);
    CHECK(dma->nextCount == (count ? count : 0x10000));
    CHECK(dma->wordTransfer == (width == 4) && dma->transferWord == (width == 4));
    CHECK(dma->sourceOffset == (int32_t)width && dma->destOffset == (int32_t)width);
    CHECK(!dma->fifo && dma->latch == 0);
    CHECK(dma->busyUntil == 0);
    CHECK(gba->irqf == 0 && gba->irq.pendingIF == 0);
    for (unsigned i = 0; i < 0x10; i++)
        CHECK(gba->memory.ewram[DEST_OFFSET + i] == 0xA5);
    source_unchanged(gba);
}

static void completed(struct CupidGBA *gba, unsigned channel, unsigned width, unsigned count,
                      bool irq) {
    struct CupidDMA *dma = &gba->memory.dma[channel];
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    unsigned clocks = 0;
    while (dma->enable || gba->memory.dmaRunning || pending(gba, channel)) {
        CHECK(clocks++ < 128);
        cupid_timing_tick(&gba->timing, 1);
    }
    cupid_timing_tick(&gba->timing, 2);
    CHECK(!dma->enable && !dma->ready && !pending(gba, channel));
    CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked && gba->memory.activeDMA == -1);
    CHECK(dma->nextCount == 0);
    CHECK(dma->busyUntil == start + 3 + count * (width == 4 ? 7 : 4));
    CHECK(dma->nextSource == GBA_BASE_IWRAM + SOURCE_OFFSET + width * count);
    CHECK(dma->nextDest == GBA_BASE_EWRAM + DEST_OFFSET + width * count);
    CHECK(cupid_io_read(gba, REG_DMA0CNT_HI + channel * 12) == (uint16_t)(dma->reg & 0x7FFF));
    CHECK(gba->irqf == (irq ? (IRQ_DMA0 << channel) : 0));
    guard_destination(gba, width * count);
}

static void absent(void) {
    const uint16_t counts[] = {0, 1, 2};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned width = 2; width <= 4; width += 2)
            for (unsigned i = 0; i < 3; i++)
                for (unsigned irq = 0; irq < 2; irq++) {
                    struct CupidGBA *gba = machine(epoch);
                    uint16_t cntHi = control(width, true, irq != 0);
                    program(gba, 3, width, counts[i], cntHi, true);
                    waiting(gba, width, counts[i], cntHi);
                    guard_destination(gba, 0);
                    cupid_timing_tick(&gba->timing, 40);
                    waiting(gba, width, counts[i], cntHi);
                    guard_destination(gba, 0);
                    CHECK(cupid_timing_global_time(&gba->timing) == epochs[epoch] + 40);
                    destroy(gba);
                    scenes++;
                }
    CHECK(scenes == 48);
    printf("Game Pak request absent/armed scenes=%u\n", scenes);
}

static void ordinary(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned width = 2; width <= 4; width += 2)
            for (unsigned count = 1; count <= 2; count++)
                for (unsigned irq = 0; irq < 2; irq++) {
                    struct CupidGBA *gba = machine(epoch);
                    const uint16_t cntHi = control(width, false, irq != 0);
                    program(gba, 3, width, (uint16_t)count, cntHi, true);
                    CHECK(pending(gba, 3));
                    completed(gba, 3, width, count, irq != 0);
                    CHECK(cupid_io_read(gba, REG_DMA3CNT_HI) == (cntHi & 0x7FFF));
                    destroy(gba);
                    scenes++;
                }
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned channel = 0; channel < 3; channel++)
            for (unsigned width = 2; width <= 4; width += 2)
                for (unsigned count = 1; count <= 2; count++) {
                    struct CupidGBA *gba = machine(epoch);
                    const uint16_t cntHi = control(width, true, false);
                    program(gba, (int)channel, width, (uint16_t)count, cntHi, false);
                    CHECK(gba->memory.dma[channel].reg == (cntHi & ~0x0800u));
                    CHECK(cupid_io_read(gba, REG_DMA0CNT_HI + channel * 12) == (cntHi & ~0x0800u));
                    CHECK(pending(gba, channel));
                    completed(gba, channel, width, count, false);
                    destroy(gba);
                    scenes++;
                }
    CHECK(scenes == 80);
    printf("ordinary DMA and masked channel scenes=%u\n", scenes);
}

static void guest(void) {
    static const uint32_t arm[] = {0xE5C10000, 0xE1C100B0, 0xE5810000};
    static const uint16_t thumb[] = {0x7008, 0x8008, 0x6008};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned mode = 0; mode < 2; mode++)
            for (unsigned size = 0; size < 3; size++)
                for (unsigned irq = 0; irq < 2; irq++) {
                    struct CupidGBA *gba = machine(epoch);
                    unsigned width = 1u << size;
                    const uint16_t cntHi = control(4, true, irq != 0);
                    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + SOURCE_OFFSET + 3);
                    cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + DEST_OFFSET + 3);
                    cupid_dma_write_cnt_lo(gba, 3, 2);
                    if (mode) {
                        const uint16_t code[] = {thumb[size], 0x225A, 0x46C0};
                        memcpy(gba->memory.iwram + 0x2000, code, sizeof(code));
                    } else {
                        const uint32_t code[] = {arm[size], 0xE3A0205A, 0xE1A00000};
                        memcpy(gba->memory.iwram + 0x2000, code, sizeof(code));
                    }
                    uint32_t addr = GBA_BASE_IO + REG_DMA3CNT_HI;
                    uint32_t value = cntHi;
                    if (width == 1) {
                        addr++;
                        value >>= 8;
                    } else if (width == 4) {
                        addr -= 2;
                        value = (value << 16) | 2;
                    }
                    gba->cpu.gprs[0] = (int32_t)value;
                    gba->cpu.gprs[1] = (int32_t)addr;
                    gba->cpu.gprs[2] = (int32_t)UINT32_C(0xA1B2C3D4);
                    _CupidSetMode(&gba->cpu, mode ? MODE_THUMB : MODE_ARM);
                    gba->cpu.cpsr.t = mode != 0;
                    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x2000;
                    gba->cpu.memory.timed = true;
                    if (mode)
                        CupidThumbWritePC(&gba->cpu);
                    else
                        CupidARMWritePC(&gba->cpu);
                    cupid_gba_step(gba);
                    cupid_gba_process_events(gba);
                    waiting(gba, 4, 2, cntHi);
                    guard_destination(gba, 0);
                    CHECK((uint32_t)gba->cpu.gprs[2] == UINT32_C(0xA1B2C3D4));
                    uint32_t pc = (uint32_t)gba->cpu.gprs[ARM_PC];
                    cupid_gba_step(gba);
                    CHECK((uint32_t)gba->cpu.gprs[2] == 0x5A);
                    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == pc + (mode ? 2 : 4));
                    cupid_timing_tick(&gba->timing, 24);
                    waiting(gba, 4, 2, cntHi);
                    guard_destination(gba, 0);
                    destroy(gba);
                    scenes++;
                }
    CHECK(scenes == 48);
    printf("ARM/Thumb mapped write and continuation scenes=%u\n", scenes);
}

static void dma_program(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned channel = 0; channel < 3; channel++)
            for (unsigned width = 2; width <= 4; width += 2) {
                struct CupidGBA *gba = machine(epoch);
                cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + SOURCE_OFFSET + 3);
                cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + DEST_OFFSET + 3);
                cupid_dma_write_cnt_lo(gba, 3, 2);
                const uint16_t cntHi = control(4, true, true);
                uint32_t target = GBA_BASE_IO + REG_DMA3CNT_HI;
                uint32_t word = cntHi;
                if (width == 4) {
                    target -= 2;
                    word = ((uint32_t)cntHi << 16) | 2;
                }
                cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM + 0x100, (int32_t)word, NULL);
                cupid_dma_write_sad(gba, (int)channel, GBA_BASE_IWRAM + 0x100);
                cupid_dma_write_dad(gba, (int)channel, target);
                cupid_dma_write_cnt_lo(gba, (int)channel, 1);
                cupid_dma_write_cnt(gba, (int)channel, control(width, false, false));
                CHECK(pending(gba, channel));
                unsigned clocks = 0;
                while (gba->memory.dma[channel].enable || gba->memory.dmaRunning ||
                       pending(gba, channel)) {
                    CHECK(clocks++ < 128);
                    cupid_timing_tick(&gba->timing, 1);
                }
                cupid_timing_tick(&gba->timing, 24);
                waiting(gba, 4, 2, cntHi);
                guard_destination(gba, 0);
                CHECK(!gba->memory.dma[channel].enable);
                CHECK(gba->memory.dma[channel].nextCount == 0);
                destroy(gba);
                scenes++;
            }
    CHECK(scenes == 24);
    printf("DMA-programmed DMA3 enable scenes=%u\n", scenes);
}

static void lifecycle(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned width = 2; width <= 4; width += 2)
            for (unsigned resetting = 0; resetting < 2; resetting++) {
                struct CupidGBA *gba = machine(epoch);
                const uint16_t drq = control(width, true, true);
                program(gba, 3, width, 2, drq, true);
                cupid_timing_tick(&gba->timing, 8);
                waiting(gba, width, 2, drq);
                guard_destination(gba, 0);
                if (resetting) {
                    cupid_gba_reset(gba);
                    CHECK(!gba->memory.dma[3].enable && !gba->memory.dma[3].ready);
                    CHECK(!pending(gba, 3) && !gba->memory.dmaRunning && !gba->cpuBlocked);
                    cupid_timing_clear(&gba->timing);
                    memset(gba->memory.ewram + DEST_OFFSET, 0xA5, WINDOW_SIZE);
                    for (unsigned i = 0; i < WINDOW_SIZE; i++)
                        gba->memory.iwram[SOURCE_OFFSET + i] = (uint8_t)(0x21 + i * 3);
                    program(gba, 3, width, 2, control(width, false, false), true);
                } else {
                    cupid_dma_write_cnt(gba, 3, (uint16_t)(drq & 0x7FFF));
                    CHECK(!gba->memory.dma[3].enable && !gba->memory.dma[3].ready);
                    CHECK(gba->memory.dma[3].nextCount == 0 && !pending(gba, 3));
                    cupid_dma_write_cnt(gba, 3, control(width, false, false));
                }
                CHECK(pending(gba, 3));
                completed(gba, 3, width, 2, false);
                destroy(gba);
                scenes++;
            }
    CHECK(scenes == 16);
    printf("disable/re-enable/reset scenes=%u\n", scenes);
}

static void priority(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned width = 2; width <= 4; width += 2) {
            struct CupidGBA *gba = machine(epoch);
            program(gba, 3, width, 2, control(width, true, false), true);
            cupid_dma_write_sad(gba, 2, GBA_BASE_IWRAM + SOURCE_OFFSET + 4);
            cupid_dma_write_dad(gba, 2, GBA_BASE_EWRAM + DEST_OFFSET + 0x20);
            cupid_dma_write_cnt_lo(gba, 2, 1);
            cupid_dma_write_cnt(gba, 2, control(width, false, false));
            cupid_dma_write_sad(gba, 0, GBA_BASE_IWRAM + SOURCE_OFFSET);
            cupid_dma_write_dad(gba, 0, GBA_BASE_EWRAM + DEST_OFFSET + 0x10);
            cupid_dma_write_cnt_lo(gba, 0, 1);
            cupid_dma_write_cnt(gba, 0, control(width, false, false));
            cupid_timing_tick(&gba->timing, 3);
            CHECK(gba->memory.activeDMA == 0);
            cupid_timing_tick(&gba->timing, 40);
            CHECK(gba->memory.activeDMA == -1 && !gba->memory.dmaRunning);
            CHECK(!gba->memory.dma[0].enable && !gba->memory.dma[2].enable);
            CHECK(memcmp(gba->memory.ewram + DEST_OFFSET + 0x10, gba->memory.iwram + SOURCE_OFFSET,
                         width) == 0);
            CHECK(memcmp(gba->memory.ewram + DEST_OFFSET + 0x20,
                         gba->memory.iwram + SOURCE_OFFSET + 4, width) == 0);
            uint8_t expected[WINDOW_SIZE];
            memset(expected, 0xA5, sizeof(expected));
            for (unsigned byte = 0; byte < width; byte++) {
                expected[0x10 + byte] = (uint8_t)(0x21 + byte * 3);
                expected[0x20 + byte] = (uint8_t)(0x21 + (byte + 4) * 3);
            }
            CHECK(memcmp(gba->memory.ewram + DEST_OFFSET, expected, sizeof(expected)) == 0);
            source_unchanged(gba);
            waiting(gba, width, 2, control(width, true, false));
            destroy(gba);
            scenes++;
        }
    CHECK(scenes == 8);
    printf("other-channel arbitration scenes=%u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "absent"))
        RUN(absent);
    else if (!strcmp(argv[1], "ordinary"))
        RUN(ordinary);
    else if (!strcmp(argv[1], "guest"))
        RUN(guest);
    else if (!strcmp(argv[1], "dma-program"))
        RUN(dma_program);
    else if (!strcmp(argv[1], "lifecycle"))
        RUN(lifecycle);
    else if (!strcmp(argv[1], "priority"))
        RUN(priority);
    else
        CHECK(false);
    return 0;
}
