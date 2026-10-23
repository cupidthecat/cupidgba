/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static const uint64_t origins[] = {
    0, INT32_MAX - 16u, UINT32_MAX - 16u, UINT64_C(0x300000000) - 16
};
static const uint32_t destinations[] = {
    0, GBA_BASE_IO + REG_FIFO_A_LO, GBA_BASE_IO + REG_FIFO_B_LO,
    GBA_BASE_IWRAM + 0x200, GBA_BASE_IO + REG_FIFO_A_HI,
    GBA_BASE_IO + REG_FIFO_B_HI, 4, GBA_BASE_EWRAM + 0x200
};
static const uint32_t payload[2][4] = {
    {0x76543210, 0xFEDCBA98, 0x67452301, 0xEFCDAB89},
    {0x18395A7B, 0x9CBDEEFF, 0x10293847, 0xA1B2C3D4}
};
static unsigned scenes;

static uint32_t source(unsigned channel) { return GBA_BASE_IWRAM + 0x400 + channel * 0x20; }

static struct CupidGBA* machine(unsigned epoch) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = origins[epoch];
    gba->timing.masterCycles = (uint32_t)origins[epoch];
    gba->audio.lastSample = gba->audio.sampleOrigin = origins[epoch];
    gba->audio.nextSample = origins[epoch] + 512;
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    for (unsigned channel = 1; channel <= 2; ++channel) for (unsigned i = 0; i < 4; ++i)
        cupid_memory_store32(&gba->cpu, source(channel) + i * 4, (int32_t)payload[channel - 1][i], NULL);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    scenes++;
    cupid_gba_destroy(gba);
    free(gba);
}

static void timers(struct CupidGBA* gba, unsigned fifo, unsigned timer) {
    unsigned a = fifo ? timer ^ 1 : timer, b = fifo ? timer : timer ^ 1;
    cupid_io_write(gba, REG_SOUNDCNT_H, (uint16_t)((a << 10) | (b << 14)));
}

static void configure(struct CupidGBA* gba, unsigned channel, uint32_t destination,
                      bool enabled, unsigned mode, unsigned word, unsigned destMode) {
    cupid_dma_write_sad(gba, (int)channel, source(channel));
    cupid_dma_write_dad(gba, (int)channel, destination);
    cupid_dma_write_cnt_lo(gba, (int)channel, 99);
    cupid_dma_write_cnt(gba, (int)channel,
        (uint16_t)((enabled << 15) | 0x4200 | (mode << 12) | (word << 10) | (destMode << 5)));
}

static void requests(struct CupidGBA* gba, unsigned mask, uint64_t deadline) {
    for (unsigned channel = 1; channel <= 2; ++channel) {
        struct CupidDMA* dma = &gba->memory.dma[channel];
        CHECK(cupid_timing_is_scheduled(&gba->timing, &dma->requestEvent) == ((mask & (1u << channel)) != 0));
        if (mask & (1u << channel)) CHECK(dma->requestEvent.when == (uint32_t)deadline);
        CHECK(!dma->ready);
    }
    CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked);
}

static void request_lines_depend_on_source_enable_and_timing(void) {
    for (unsigned epoch = 0; epoch < 4; ++epoch) for (unsigned fifo = 0; fifo < 2; ++fifo)
    for (unsigned timer = 0; timer < 2; ++timer) for (unsigned enables = 0; enables < 4; ++enables)
    for (unsigned mode1 = 0; mode1 < 4; ++mode1) for (unsigned mode2 = 0; mode2 < 4; ++mode2)
    for (unsigned target = 0; target < 8; ++target) {
        struct CupidGBA* gba = machine(epoch);
        timers(gba, fifo, timer);
        unsigned modes[] = {mode1, mode2};
        for (unsigned channel = 1; channel <= 2; ++channel) {
            configure(gba, channel, destinations[(target + (channel - 1) * 3) % 8],
                      (enables >> (channel - 1)) & 1, modes[channel - 1], 1, 2);
            /* Isolate the audio selector from the immediate-startup event.
             * No time passes, and the programmed channel remains unchanged. */
            cupid_timing_deschedule(&gba->timing, &gba->memory.dma[channel].requestEvent);
        }
        cupid_audio_timer_overflow(&gba->audio, timer, origins[epoch]);
        bool enabled = (enables >> fifo) & 1;
        unsigned expected = enabled && modes[fifo] == DMA_TIMING_CUSTOM ? 1u << (fifo + 1) : 0;
        requests(gba, expected, origins[epoch] + 3);
        for (unsigned channel = 1; channel <= 2; ++channel) {
            struct CupidDMA* dma = &gba->memory.dma[channel];
            CHECK(dma->source == source(channel) && dma->count == 99);
            CHECK(dma->dest == destinations[(target + (channel - 1) * 3) % 8]);
            CHECK(dma->timing == (int)modes[channel - 1]);
            CHECK(dma->enable == (((enables >> (channel - 1)) & 1) != 0));
        }
        destroy(gba);
    }
    CHECK(scenes == 8192);
}

static void destination_history_does_not_rewire_a_request(void) {
    for (unsigned epoch = 0; epoch < 4; ++epoch) for (unsigned channel = 1; channel <= 2; ++channel)
    for (unsigned timer = 0; timer < 2; ++timer) for (unsigned word = 0; word < 2; ++word)
    for (unsigned previous = 0; previous < 8; ++previous) for (unsigned next = 0; next < 8; ++next) {
        struct CupidGBA* gba = machine(epoch);
        timers(gba, channel - 1, timer);
        configure(gba, channel, destinations[previous], true, DMA_TIMING_CUSTOM, word, previous % 4);
        struct CupidDMA* dma = &gba->memory.dma[channel];
        cupid_dma_write_dad(gba, (int)channel, destinations[next]);
        CHECK(dma->nextDest == (destinations[previous] & ~3u));
        CHECK(dma->dest == destinations[next]);
        cupid_audio_timer_overflow(&gba->audio, timer ^ 1, origins[epoch]);
        requests(gba, 0, 0);
        cupid_audio_timer_overflow(&gba->audio, timer, origins[epoch]);
        requests(gba, 1u << channel, origins[epoch] + 3);
        cupid_dma_write_cnt(gba, (int)channel, dma->reg & 0x7FFF);
        requests(gba, 0, 0);
        /* Re-enable latches the new address. A sound power cycle clears FIFO
         * storage but supplies no destination-based channel association. */
        cupid_io_write(gba, REG_SOUNDCNT_X, 0);
        cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
        timers(gba, channel - 1, timer);
        configure(gba, channel, destinations[next], true, DMA_TIMING_CUSTOM, word ^ 1, next % 4);
        CHECK(dma->nextDest == (destinations[next] & ~3u));
        CHECK(dma->nextSource == source(channel) && dma->nextCount == 4);
        cupid_audio_timer_overflow(&gba->audio, timer ^ 1, origins[epoch]);
        requests(gba, 0, 0);
        cupid_audio_timer_overflow(&gba->audio, timer, origins[epoch]);
        requests(gba, 1u << channel, origins[epoch] + 3);
        destroy(gba);
    }
    CHECK(scenes == 2048);
}

static unsigned store_cycles(uint32_t destination) {
    return destination >> 24 == GBA_REGION_EWRAM ? 6 : 1;
}

static unsigned publications(unsigned clock, unsigned head, unsigned cost) {
    unsigned count = 0;
    for (unsigned i = 0; i < 4; ++i) count += clock >= head + (i + 1) * (1 + cost);
    return count;
}

static void storage(struct CupidGBA* gba, const uint32_t* target, const unsigned* published) {
    for (unsigned fifo = 0; fifo < 2; ++fifo) {
        struct CupidAudioFIFO* output = fifo ? &gba->audio.chB : &gba->audio.chA;
        uint32_t expected[GBA_AUDIO_FIFO_SIZE] = {0};
        unsigned count = 0;
        for (unsigned channel = 0; channel < 2; ++channel)
            if (target[channel] == GBA_BASE_IO + (fifo ? REG_FIFO_B_LO : REG_FIFO_A_LO))
                for (unsigned i = 0; i < published[channel]; ++i) {
                    /* Eight writes to one empty FIFO overflow its seven-word
                     * queue. Both source bursts must still finish normally. */
                    if (count < 7) expected[count++] = payload[channel][i];
                    else { memset(expected, 0, sizeof(expected)); count = 0; }
                }
        CHECK(output->fifoCount == (int)count && output->fifoRead == 0);
        CHECK(output->fifoWrite == (int)(count % 7));
        CHECK(!memcmp(output->fifo, expected, sizeof(expected)));
        CHECK(output->internalRemaining == 0 && output->internalSample == 0 && output->latchedSample == 0);
    }
    const uint32_t ram[] = {GBA_BASE_IWRAM + 0x200, GBA_BASE_EWRAM + 0x200};
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t expected = 0;
        for (unsigned channel = 0; channel < 2; ++channel)
            if (target[channel] == ram[i] && published[channel]) expected = payload[channel][published[channel] - 1];
        CHECK(cupid_memory_load32(&gba->cpu, ram[i], NULL) == expected);
        CHECK(cupid_memory_load32(&gba->cpu, ram[i] - 4, NULL) == 0);
        CHECK(cupid_memory_load32(&gba->cpu, ram[i] + 4, NULL) == 0);
    }
}

static void both_request_lines_keep_priority_payload_and_completion_edges(void) {
    for (unsigned epoch = 0; epoch < 4; ++epoch) for (unsigned word = 0; word < 2; ++word)
    for (unsigned first = 0; first < 8; ++first) for (unsigned second = 0; second < 8; ++second) {
        struct CupidGBA* gba = machine(epoch);
        /* Both FIFO sources are empty and use timer 0. DAD can point either
         * channel at either port, RAM or a protected address. */
        cupid_io_write(gba, REG_SOUNDCNT_H, 0);
        configure(gba, 1, destinations[first], true, DMA_TIMING_CUSTOM, word, first % 4);
        configure(gba, 2, destinations[second], true, DMA_TIMING_CUSTOM, word ^ 1, second % 4);
        const uint32_t target[] = {destinations[first] & ~3u, destinations[second] & ~3u};
        const unsigned costs[] = {store_cycles(target[0]), store_cycles(target[1])};
        const unsigned ends[] = {4 + 4 * (1 + costs[0]), 4 + 4 * (1 + costs[0]) + 4 * (1 + costs[1])};
        unsigned char bios[8];
        memcpy(bios, gba->memory.bios, sizeof(bios));
        cupid_audio_timer_overflow(&gba->audio, 0, origins[epoch]);
        requests(gba, 6, origins[epoch] + 3);
        for (unsigned clock = 1; clock <= ends[1] + 2; ++clock) {
            cupid_timing_tick(&gba->timing, 1);
            unsigned published[] = {publications(clock, 4, costs[0]), publications(clock, ends[0], costs[1])};
            CHECK(gba->memory.dmaRunning == (clock >= 3 && clock < ends[1] + 1));
            CHECK(gba->cpuBlocked == gba->memory.dmaRunning);
            unsigned expectedIF = 0;
            for (unsigned channel = 1; channel <= 2; ++channel) {
                struct CupidDMA* dma = &gba->memory.dma[channel];
                unsigned count = published[channel - 1];
                CHECK(dma->nextSource == source(channel) + 4 * count);
                CHECK(dma->nextDest == target[channel - 1]);
                CHECK(dma->nextCount == (count < 4 ? 4 - (int)count : 4));
                CHECK(dma->fifo && dma->enable && dma->count == 99);
                CHECK(dma->wordTransfer == ((word ^ (channel - 1)) != 0));
                if (clock >= ends[channel - 1] + 2) expectedIF |= IRQ_DMA0 << channel;
            }
            CHECK(gba->irqf == expectedIF);
            CHECK(!memcmp(bios, gba->memory.bios, sizeof(bios)));
            storage(gba, target, published);
        }
        for (unsigned channel = 1; channel <= 2; ++channel) {
            CHECK(gba->memory.dma[channel].busyUntil == origins[epoch] + ends[channel - 1]);
            cupid_dma_write_cnt(gba, (int)channel, gba->memory.dma[channel].reg & 0x7FFF);
        }
        /* Read every surviving byte through the timer path, after disabling
         * DMA so playback cannot append a new burst to the checked queue. */
        for (unsigned fifo = 0; fifo < 2; ++fifo) {
            timers(gba, fifo, 0);
            unsigned count = 0;
            for (unsigned channel = 0; channel < 2; ++channel)
                count += target[channel] == GBA_BASE_IO + (fifo ? REG_FIFO_B_LO : REG_FIFO_A_LO) ? 4 : 0;
            struct CupidAudioFIFO* output = fifo ? &gba->audio.chB : &gba->audio.chA;
            if (count == 4) for (unsigned channel = 0; channel < 2; ++channel)
                if (target[channel] == GBA_BASE_IO + (fifo ? REG_FIFO_B_LO : REG_FIFO_A_LO))
                    for (unsigned i = 0; i < 4; ++i) for (unsigned byte = 0; byte < 4; ++byte) {
                        cupid_audio_timer_overflow(&gba->audio, 0, cupid_timing_global_time(&gba->timing));
                        CHECK(output->latchedSample == (int8_t)(payload[channel][i] >> (byte * 8)));
                    }
            cupid_audio_timer_overflow(&gba->audio, 0, cupid_timing_global_time(&gba->timing));
            CHECK(output->latchedSample == 0 && output->fifoCount == 0);
        }
        destroy(gba);
    }
    CHECK(scenes == 512);
}

static void pending_requests_keep_their_deadline_and_disable_cancels_them(void) {
    for (unsigned epoch = 0; epoch < 4; ++epoch) for (unsigned channel = 1; channel <= 2; ++channel)
    for (unsigned timer = 0; timer < 2; ++timer) for (unsigned target = 0; target < 8; ++target)
    for (unsigned delay = 0; delay < 3; ++delay) for (unsigned word = 0; word < 2; ++word) {
        struct CupidGBA* gba = machine(epoch);
        timers(gba, channel - 1, timer);
        configure(gba, channel, destinations[target], true, DMA_TIMING_CUSTOM, word, target % 4);
        struct CupidDMA* dma = &gba->memory.dma[channel];
        cupid_audio_timer_overflow(&gba->audio, timer, origins[epoch]);
        requests(gba, 1u << channel, origins[epoch] + 3);
        cupid_timing_tick(&gba->timing, (int32_t)delay);
        cupid_audio_timer_overflow(&gba->audio, timer, origins[epoch] + delay);
        requests(gba, 1u << channel, origins[epoch] + 3);
        cupid_dma_write_cnt(gba, (int)channel, dma->reg & 0x7FFF);
        cupid_timing_tick(&gba->timing, 4);
        requests(gba, 0, 0);
        const uint32_t cancelledTargets[] = {destinations[target] & ~3u, 0};
        const unsigned nothing[] = {0, 0};
        storage(gba, cancelledTargets, nothing);
        CHECK(dma->nextSource == source(channel) && dma->nextCount == 0 && gba->irqf == 0);
        configure(gba, channel, destinations[(target + 1) % 8], true, DMA_TIMING_CUSTOM, word, target % 4);
        uint64_t start = cupid_timing_global_time(&gba->timing);
        cupid_audio_timer_overflow(&gba->audio, timer, start);
        requests(gba, 1u << channel, start + 3);
        cupid_timing_tick(&gba->timing, 1);
        cupid_audio_timer_overflow(&gba->audio, timer, start + 1);
        requests(gba, 1u << channel, start + 3);
        const unsigned end = 4 + 4 * (1 + store_cycles(destinations[(target + 1) % 8]));
        cupid_timing_tick(&gba->timing, (int32_t)(end + 1));
        CHECK(dma->nextSource == source(channel) + 16 && dma->nextCount == 4);
        CHECK(dma->nextDest == (destinations[(target + 1) % 8] & ~3u));
        CHECK(dma->busyUntil == start + end && gba->irqf == (unsigned)(IRQ_DMA0 << channel));
        CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked);
        uint32_t finalTargets[] = {0, 0};
        unsigned published[] = {0, 0};
        finalTargets[channel - 1] = destinations[(target + 1) % 8] & ~3u;
        published[channel - 1] = 4;
        storage(gba, finalTargets, published);
        destroy(gba);
    }
    CHECK(scenes == 768);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "selectors")) RUN(request_lines_depend_on_source_enable_and_timing);
    else if (!strcmp(argv[1], "history")) RUN(destination_history_does_not_rewire_a_request);
    else if (!strcmp(argv[1], "simultaneous")) RUN(both_request_lines_keep_priority_payload_and_completion_edges);
    else { CHECK(!strcmp(argv[1], "pending")); RUN(pending_requests_keep_their_deadline_and_disable_cancels_them); }
    printf("PASS %u FIFO request routing scenes\n", scenes);
    return 0;
}
