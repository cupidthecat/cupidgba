/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static const uint64_t origins[] = {
    0, INT32_MAX - 16u, UINT32_MAX - 16u, UINT64_C(0x300000000) - 16
};
static const uint32_t destinations[] = {0, 4, GBA_BASE_IWRAM + 0x200, GBA_BASE_EWRAM + 0x200};
static const uint32_t payload[] = {
    0x76543210, 0xFEDCBA98, 0x67452301, 0xEFCDAB89,
    0x10293847, 0xA1B2C3D4, 0x89ABCDEF, 0x24681357
};
static unsigned observations;

static struct CupidGBA* machine(uint64_t origin, unsigned channel, unsigned timer,
                                unsigned word, unsigned mode, uint32_t destination) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = origin;
    gba->timing.masterCycles = (uint32_t)origin;
    gba->audio.lastSample = gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + 512;
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    /* Keep the default FIFO A/DMA1 and FIFO B/DMA2 associations. The other
     * FIFO uses the opposite timer, so it cannot supply this request. */
    unsigned timerA = channel == 1 ? timer : timer ^ 1;
    unsigned timerB = channel == 2 ? timer : timer ^ 1;
    cupid_io_write(gba, REG_SOUNDCNT_H, (uint16_t)((timerA << 10) | (timerB << 14)));
    for (unsigned i = 0; i < 8; ++i)
        cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM + 0x400 + 4 * i, (int32_t)payload[i], NULL);
    cupid_io_write(gba, REG_IE, IRQ_DMA0 << channel);
    cupid_io_write(gba, REG_IME, 1);
    cupid_dma_write_sad(gba, (int)channel, GBA_BASE_IWRAM + 0x400);
    cupid_dma_write_dad(gba, (int)channel, destination);
    cupid_dma_write_cnt_lo(gba, (int)channel, 99);
    cupid_dma_write_cnt(gba, (int)channel, (uint16_t)(0xF200 | (word << 10) | (mode << 5)));
    return gba;
}

static void requests_retain_the_programmed_non_fifo_destination(void) {
    for (unsigned epoch = 0; epoch < 4; ++epoch) for (unsigned channel = 1; channel <= 2; ++channel)
    for (unsigned timer = 0; timer < 2; ++timer) for (unsigned word = 0; word < 2; ++word)
    for (unsigned mode = 0; mode < 4; ++mode) for (unsigned target = 0; target < 4; ++target) {
        const uint32_t destination = destinations[target];
        const unsigned storeCycles = destination >> 24 == GBA_REGION_EWRAM ? 6 : 1;
        const unsigned last = 4 + 4 * (1 + storeCycles), release = last + 1;
        const uint16_t bit = IRQ_DMA0 << channel;
        struct CupidGBA* gba = machine(origins[epoch], channel, timer, word, mode, destination);
        struct CupidDMA* dma = &gba->memory.dma[channel];
        unsigned char bios[8];
        memcpy(bios, gba->memory.bios, sizeof(bios));
        for (unsigned burst = 0; burst < 2; ++burst) {
            const uint64_t start = cupid_timing_global_time(&gba->timing);
            cupid_audio_timer_overflow(&gba->audio, timer, start);
            CHECK(cupid_timing_is_scheduled(&gba->timing, &dma->requestEvent));
            CHECK(dma->requestEvent.when == (uint32_t)(start + 3));
            for (unsigned clock = 1; clock <= last + 2; ++clock) {
                cupid_timing_tick(&gba->timing, 1);
                unsigned published = 0;
                for (unsigned beat = 0; beat < 4; ++beat)
                    if (clock >= 4 + (beat + 1) * (1 + storeCycles)) ++published;
                CHECK(gba->memory.dmaRunning == (clock >= 3 && clock < release));
                CHECK(dma->nextSource == GBA_BASE_IWRAM + 0x400 + 16 * burst + 4 * published);
                CHECK(dma->nextDest == destination && dma->dest == destination);
                CHECK(dma->nextCount == (published < 4 ? (int)(4 - published) : 4));
                CHECK(dma->wordTransfer == (word != 0) && dma->destControl == (int)mode);
                CHECK(dma->fifo && dma->enable && dma->count == 99);
                CHECK((gba->irqf & bit) == (clock >= last + 2 ? bit : 0));
                CHECK(!memcmp(bios, gba->memory.bios, sizeof(bios)));
                CHECK(gba->audio.chA.fifoRead == gba->audio.chA.fifoWrite);
                CHECK(gba->audio.chB.fifoRead == gba->audio.chB.fifoWrite);
                CHECK(gba->audio.chA.internalRemaining == 0 && gba->audio.chB.internalRemaining == 0);
                if (target >= 2) {
                    uint32_t expected = published ? payload[burst * 4 + published - 1]
                        : burst ? payload[3] : 0;
                    CHECK(cupid_memory_load32(&gba->cpu, destination, NULL) == expected);
                    CHECK(cupid_memory_load32(&gba->cpu, destination + 4, NULL) == 0);
                }
            }
            CHECK(dma->busyUntil == start + last);
            CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked);
            /* Empty FIFO storage can request the next burst despite the other
             * destination. Acknowledge the first completion before repeating. */
            cupid_io_write(gba, REG_IF, bit);
            cupid_timing_tick(&gba->timing, 1);
            CHECK(!(gba->irqf & bit));
        }
        observations++;
        cupid_gba_destroy(gba);
        free(gba);
    }
}

int main(void) {
    RUN(requests_retain_the_programmed_non_fifo_destination);
    printf("observations=%u\n", observations);
    return 0;
}
