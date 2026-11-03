/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_live_modes.c
 * @brief Live FIFO mode changes, accepted burst counts and mapped CPU control writes.
 */
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

static const uint64_t epochs[] = {0, INT32_MAX - 19u, UINT32_MAX - 19u, UINT64_C(0x300000000)};

static struct CupidGBA *machine(unsigned epoch) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epochs[epoch];
    gba->timing.masterCycles = (uint32_t)epochs[epoch];
    gba->audio.lastSample = gba->audio.sampleOrigin = epochs[epoch];
    gba->audio.nextSample = epochs[epoch] + 512;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    for (unsigned byte = 0; byte < GBA_SIZE_IWRAM; byte++)
        gba->memory.iwram[byte] = (uint8_t)(byte * 37 + byte / 256);
    memset(gba->memory.ewram, 0xCC, GBA_SIZE_EWRAM);
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void setup(struct CupidGBA *gba, unsigned ch, unsigned count, uint16_t control) {
    cupid_dma_write_sad(gba, (int)ch, GBA_BASE_IWRAM + 0x400);
    cupid_dma_write_dad(gba, (int)ch, GBA_BASE_EWRAM + 0x800);
    cupid_dma_write_cnt_lo(gba, (int)ch, (uint16_t)count);
    cupid_dma_write_cnt(gba, (int)ch, control);
}

static void trigger(struct CupidGBA *gba, unsigned ch, unsigned mode) {
    if (mode == 3)
        cupid_dma_request_fifo(gba, (int)ch, cupid_timing_global_time(&gba->timing));
    else if (mode == 2)
        cupid_dma_run_hblank(gba, 0);
    else
        cupid_dma_run_vblank(gba, 0);
}

static void finish(struct CupidGBA *gba) {
    unsigned clocks;
    for (clocks = 0; clocks < 512; clocks++) {
        cupid_timing_tick(&gba->timing, 1);
        if (clocks >= 3 && !gba->memory.dmaRunning && !gba->cpuBlocked)
            break;
    }
    CHECK(clocks < 512);
    cupid_timing_tick(&gba->timing, 2);
}

static void expected_burst(uint8_t *expected, unsigned *source, unsigned *dest, unsigned width,
                           unsigned count, unsigned srcMode, unsigned destMode, bool fifo) {
    for (unsigned unit = 0; unit < count; unit++) {
        for (unsigned byte = 0; byte < width; byte++)
            expected[*dest + byte] = (uint8_t)((*source + byte) * 37 + (*source + byte) / 256);
        if (srcMode != 2)
            *source = srcMode == 1 ? *source - width : *source + width;
        if (!fifo && destMode != 2)
            *dest = destMode == 1 ? *dest - width : *dest + width;
    }
}

static void check_image(const struct CupidGBA *gba, const uint8_t *expected) {
    CHECK(!memcmp(gba->memory.ewram, expected, GBA_SIZE_EWRAM));
    for (unsigned byte = 0x300; byte < 0x500; byte++)
        CHECK(gba->memory.iwram[byte] == (uint8_t)(byte * 37 + byte / 256));
}

static void leave(void) {
    uint8_t *expected = malloc(GBA_SIZE_EWRAM);
    CHECK(expected);
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned ch = 1; ch <= 2; ch++)
            for (unsigned mode = 1; mode <= 2; mode++)
                for (unsigned word = 0; word < 2; word++)
                    for (unsigned src = 0; src < 3; src++)
                        for (unsigned dest = 0; dest < 4; dest++) {
                            struct CupidGBA *gba = machine(epoch);
                            setup(gba, ch, 4, 0xB200);
                            const uint16_t control =
                                (uint16_t)(0xC000 | mode << 12 | word << 10 | src << 7 | dest << 5);
                            cupid_dma_write_cnt(gba, (int)ch, control);
                            CHECK(cupid_io_read(gba, REG_DMA0CNT_HI + ch * 12) == control);
                            trigger(gba, ch, mode);
                            finish(gba);
                            unsigned source = 0x400, target = 0x800;
                            memset(expected, 0xCC, GBA_SIZE_EWRAM);
                            expected_burst(expected, &source, &target, word ? 4 : 2, 4, src, dest,
                                           false);
                            check_image(gba, expected);
                            CHECK(!gba->memory.dma[ch].enable && !gba->cpuBlocked);
                            CHECK(gba->memory.dma[ch].nextSource == GBA_BASE_IWRAM + source);
                            CHECK(gba->memory.dma[ch].nextDest == GBA_BASE_EWRAM + target);
                            CHECK(gba->irqf == (IRQ_DMA0 << ch));
                            destroy(gba);
                            scenes++;
                        }
    CHECK(scenes == 384);
    printf("live FIFO cancellation scenes=%u\n", scenes);
    free(expected);
}

static void enter(void) {
    const unsigned counts[] = {0, 1, 3, 4, 99, 0x3FFF};
    uint8_t *expected = malloc(GBA_SIZE_EWRAM);
    CHECK(expected);
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned ch = 1; ch <= 2; ch++)
            for (unsigned mode = 1; mode <= 2; mode++)
                for (unsigned word = 0; word < 2; word++)
                    for (unsigned src = 0; src < 3; src++)
                        for (unsigned dest = 0; dest < 4; dest++)
                            for (unsigned count = 0; count < 6; count++) {
                                struct CupidGBA *gba = machine(epoch);
                                setup(gba, ch, counts[count], (uint16_t)(0x8000 | mode << 12));
                                const uint16_t control =
                                    (uint16_t)(0xF000 | word << 10 | src << 7 | dest << 5);
                                cupid_dma_write_cnt(gba, (int)ch, control);
                                trigger(gba, ch, 3);
                                CHECK(gba->memory.dma[ch].nextCount == 4);
                                finish(gba);
                                unsigned source = 0x400, target = 0x800;
                                memset(expected, 0xCC, GBA_SIZE_EWRAM);
                                expected_burst(expected, &source, &target, 4, 4, src, dest, true);
                                check_image(gba, expected);
                                CHECK(!gba->memory.dma[ch].enable && !gba->cpuBlocked);
                                CHECK(gba->memory.dma[ch].nextSource == GBA_BASE_IWRAM + source);
                                CHECK(gba->memory.dma[ch].nextDest == GBA_BASE_EWRAM + target);
                                CHECK(cupid_io_read(gba, REG_DMA0CNT_HI + ch * 12) ==
                                      (control & 0x7FFF));
                                CHECK(gba->irqf == (IRQ_DMA0 << ch));
                                destroy(gba);
                                scenes++;
                            }
    CHECK(scenes == 2304);
    printf("live FIFO selection/count scenes=%u\n", scenes);
    free(expected);
}

static void repeat(void) {
    uint8_t *expected = malloc(GBA_SIZE_EWRAM);
    CHECK(expected);
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned ch = 1; ch <= 2; ch++)
            for (unsigned mode = 1; mode <= 2; mode++)
                for (unsigned word = 0; word < 2; word++)
                    for (unsigned oldSrc = 0; oldSrc < 3; oldSrc++)
                        for (unsigned src = 0; src < 3; src++)
                            for (unsigned dest = 0; dest < 4; dest++) {
                                struct CupidGBA *gba = machine(epoch);
                                setup(gba, ch, 4, (uint16_t)(0xB200 | oldSrc << 7));
                                trigger(gba, ch, 3);
                                unsigned guard = 0;
                                while (gba->memory.dma[ch].nextCount == 4 && guard++ < 128)
                                    cupid_timing_tick(&gba->timing, 1);
                                CHECK(guard < 128 && gba->memory.dma[ch].nextCount == 3);
                                trigger(gba, ch, 3);
                                trigger(gba, ch, 3);
                                CHECK(gba->memory.dma[ch].nextCount == 3);
                                finish(gba);
                                unsigned source = 0x400, target = 0x800;
                                memset(expected, 0xCC, GBA_SIZE_EWRAM);
                                expected_burst(expected, &source, &target, 4, 4, oldSrc, 0, true);
                                check_image(gba, expected);
                                const uint16_t control =
                                    (uint16_t)(0xC000 | mode << 12 | word << 10 | src << 7 |
                                               dest << 5);
                                cupid_dma_write_cnt(gba, (int)ch, control);
                                trigger(gba, ch, mode);
                                finish(gba);
                                expected_burst(expected, &source, &target, word ? 4 : 2, 4, src,
                                               dest, false);
                                check_image(gba, expected);
                                CHECK(!gba->memory.dma[ch].enable && !gba->cpuBlocked);
                                CHECK(gba->memory.dma[ch].nextSource == GBA_BASE_IWRAM + source);
                                CHECK(gba->memory.dma[ch].nextDest == GBA_BASE_EWRAM + target);
                                CHECK(gba->irqf == (IRQ_DMA0 << ch));
                                destroy(gba);
                                scenes++;
                            }
    CHECK(scenes == 1152);
    printf("live FIFO completed-burst/duplicate-request scenes=%u\n", scenes);
    free(expected);
}

static void guest(void) {
    uint8_t *expected = malloc(GBA_SIZE_EWRAM);
    CHECK(expected);
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned ch = 1; ch <= 2; ch++)
            for (unsigned thumb = 0; thumb < 2; thumb++)
                for (unsigned width = 1; width <= 4; width *= 2)
                    for (unsigned fifo = 0; fifo < 2; fifo++)
                        for (unsigned mode = 1; mode <= 2; mode++) {
                            struct CupidGBA *gba = machine(epoch);
                            setup(gba, ch, 4, (uint16_t)(fifo ? 0x8000 | mode << 12 : 0xB000));
                            const uint16_t control = (uint16_t)(0xC000 | (fifo ? 3 : mode) << 12);
                            const uint32_t arm[] = {width == 1   ? 0xE5C10000
                                                    : width == 2 ? 0xE1C100B0
                                                                 : 0xE5810000,
                                                    0xE1A00000, 0xEAFFFFFE};
                            const uint16_t half[] = {(uint16_t)(width == 1   ? 0x7008
                                                                : width == 2 ? 0x8008
                                                                             : 0x6008),
                                                     0x46C0, 0xE7FE};
                            memcpy(gba->memory.iwram + 0x2000,
                                   thumb ? (const void *)half : (const void *)arm,
                                   thumb ? sizeof(half) : sizeof(arm));
                            uint32_t address = GBA_BASE_IO + REG_DMA0CNT_HI + ch * 12;
                            uint32_t value = control;
                            if (width == 1) {
                                address++;
                                value >>= 8;
                            } else if (width == 4) {
                                address -= 2;
                                value = (value << 16) | 4;
                            }
                            gba->cpu.gprs[0] = (int32_t)value;
                            gba->cpu.gprs[1] = (int32_t)address;
                            gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x2000;
                            _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
                            gba->cpu.cpsr.t = thumb;
                            if (thumb)
                                CupidThumbWritePC(&gba->cpu);
                            else
                                CupidARMWritePC(&gba->cpu);
                            cupid_gba_step(gba);
                            cupid_gba_process_events(gba);
                            CHECK(cupid_io_read(gba, REG_DMA0CNT_HI + ch * 12) == control);
                            if (fifo) {
                                gba->audio.enable = true;
                                cupid_audio_timer_overflow(&gba->audio, 0,
                                                           cupid_timing_global_time(&gba->timing));
                            } else
                                trigger(gba, ch, mode);
                            finish(gba);
                            unsigned source = 0x400, target = 0x800;
                            memset(expected, 0xCC, GBA_SIZE_EWRAM);
                            expected_burst(expected, &source, &target, fifo ? 4 : 2, 4, 0, 0, fifo);
                            check_image(gba, expected);
                            CHECK(!gba->memory.dma[ch].enable && !gba->cpuBlocked);
                            CHECK(gba->irqf == (IRQ_DMA0 << ch));
                            destroy(gba);
                            scenes++;
                        }
    CHECK(scenes == 192);
    printf("live FIFO ARM/Thumb byte/halfword/word scenes=%u\n", scenes);
    free(expected);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "leave"))
        RUN(leave);
    else if (!strcmp(argv[1], "enter"))
        RUN(enter);
    else if (!strcmp(argv[1], "repeat"))
        RUN(repeat);
    else if (!strcmp(argv[1], "guest"))
        RUN(guest);
    else
        CHECK(false);
    return 0;
}
