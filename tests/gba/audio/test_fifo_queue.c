/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static const uint64_t origins[] = {0, INT32_MAX - 48u, UINT32_MAX - 48u, UINT64_C(0x300000000)};
static const struct Access { unsigned width, lane; uint32_t value; } accesses[] = {
    {1, 0, 0xA9}, {1, 1, 0xB8}, {1, 2, 0xC7}, {1, 3, 0xD6},
    {2, 0, 0xE5F4}, {2, 2, 0x9382}, {4, 0, 0x81726354}
};
static unsigned scenes;

static struct CupidAudioFIFO* selected(struct CupidGBA* gba, unsigned id) {
    return id ? &gba->audio.chB : &gba->audio.chA;
}

static uint32_t port(unsigned id) {
    return GBA_BASE_IO + (id ? REG_FIFO_B_LO : REG_FIFO_A_LO);
}

static void store(struct CupidGBA* gba, unsigned id, const struct Access* access) {
    uint32_t address = port(id) + access->lane;
    if (access->width == 1) cupid_memory_store8(&gba->cpu, address, (int8_t)access->value, NULL);
    else if (access->width == 2) cupid_memory_store16(&gba->cpu, address, (int16_t)access->value, NULL);
    else cupid_memory_store32(&gba->cpu, address, (int32_t)access->value, NULL);
}

static uint32_t replace_lanes(uint32_t old, const struct Access* access) {
    unsigned shift = access->lane * 8;
    uint32_t mask = access->width == 4 ? UINT32_MAX
        : ((UINT32_C(1) << (access->width * 8)) - 1) << shift;
    return (old & ~mask) | ((access->value << shift) & mask);
}

static void sample(struct CupidGBA* gba, unsigned id, unsigned expected) {
    CHECK(gba->cpu.cycles == 0);
    cupid_timing_tick(&gba->timing, 1);
    cupid_audio_sample_fifo(&gba->audio, (int)id, cupid_timing_global_time(&gba->timing));
    CHECK(selected(gba, id)->latchedSample == (int8_t)expected);
}

static void play(struct CupidGBA* gba, unsigned id, uint32_t word) {
    for (unsigned byte = 0; byte < 4; ++byte) sample(gba, id, (uint8_t)(word >> (byte * 8)));
}

static struct CupidGBA* machine(unsigned epoch) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.masterCycles = (uint32_t)origins[epoch];
    gba->timing.globalCycles = origins[epoch];
    gba->audio.lastSample = gba->audio.sampleOrigin = origins[epoch];
    gba->audio.nextSample = origins[epoch] + 512;
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    ++scenes;
    cupid_gba_destroy(gba);
    free(gba);
}

/* Populate all seven distinct storage words, drain them, then rotate both
 * pointers without overflow. The next narrow write must reuse the actual
 * seven-word storage slot, even though the queue is empty. */
static uint32_t rotate(struct CupidGBA* gba, unsigned id, unsigned phase) {
    uint32_t words[7];
    for (unsigned i = 0; i < 7; ++i) {
        words[i] = 0x44332211u + i * 0x01010101u;
        cupid_memory_store32(&gba->cpu, port(id), (int32_t)words[i], NULL);
    }
    for (unsigned i = 0; i < 7; ++i) play(gba, id, words[i]);
    for (unsigned i = 0; i < phase; ++i) {
        words[i % 7] = 0xA4938271u + i * 0x01010101u;
        cupid_memory_store32(&gba->cpu, port(id), (int32_t)words[i % 7], NULL);
        play(gba, id, words[i % 7]);
    }
    return words[phase % 7];
}

static void rotations_reuse_seven_storage_words(void) {
    for (unsigned e = 0; e < 4; ++e) for (unsigned id = 0; id < 2; ++id)
    for (unsigned phase = 0; phase < 14; ++phase) for (unsigned a = 0; a < 7; ++a) {
        struct CupidGBA* gba = machine(e);
        uint32_t previous = rotate(gba, id, phase);
        const struct CupidAudioFIFO other = *selected(gba, id ^ 1);
        store(gba, id, &accesses[a]);
        play(gba, id, replace_lanes(previous, &accesses[a]));
        sample(gba, id, 0);
        CHECK(!memcmp(selected(gba, id ^ 1), &other, sizeof(other)));
        destroy(gba);
    }
    CHECK(scenes == 784);
}

/* Load through the public FIFO path so every staged-byte count is reachable.
 * The held output survives queue writes; another timer edge changes it. */
static void stage(struct CupidGBA* gba, unsigned id, unsigned remaining) {
    cupid_memory_store32(&gba->cpu, port(id), (int32_t)0xD4C3B2A1, NULL);
    for (unsigned byte = 0; byte < 4 - remaining; ++byte)
        sample(gba, id, (uint8_t)(UINT32_C(0xD4C3B2A1) >> (byte * 8)));
}

static void require_empty_storage(const struct CupidAudioFIFO* fifo) {
    CHECK(fifo->fifoRead == 0 && fifo->fifoWrite == 0);
    for (unsigned i = 0; i < GBA_AUDIO_FIFO_SIZE; ++i) CHECK(fifo->fifo[i] == 0);
}

static void finish_stage(struct CupidGBA* gba, unsigned id, unsigned remaining) {
    for (unsigned byte = 4 - remaining; byte < 4; ++byte)
        sample(gba, id, (uint8_t)(UINT32_C(0xD4C3B2A1) >> (byte * 8)));
    sample(gba, id, 0);
}

static void overflowing_writes_discard_the_queue_and_keep_the_stage(void) {
    for (unsigned e = 0; e < 4; ++e) for (unsigned id = 0; id < 2; ++id)
    for (unsigned phase = 0; phase < 7; ++phase) for (unsigned left = 0; left < 4; ++left)
    for (unsigned a = 0; a < 7; ++a) {
        struct CupidGBA* gba = machine(e);
        rotate(gba, id, phase);
        stage(gba, id, left);
        for (unsigned i = 0; i < 7; ++i)
            cupid_memory_store32(&gba->cpu, port(id), (int32_t)(0x18395A7Bu + i), NULL);
        const struct CupidAudioFIFO before = *selected(gba, id), other = *selected(gba, id ^ 1);
        store(gba, id, &accesses[a]);
        require_empty_storage(selected(gba, id));
        CHECK(selected(gba, id)->internalSample == before.internalSample);
        CHECK(selected(gba, id)->internalRemaining == before.internalRemaining);
        CHECK(selected(gba, id)->latchedSample == before.latchedSample);
        CHECK(!memcmp(selected(gba, id ^ 1), &other, sizeof(other)));
        finish_stage(gba, id, left);
        /* Overflowing data is discarded, and a following narrow write starts
         * from cleared lanes rather than recovering the discarded payload. */
        store(gba, id, &accesses[a]);
        play(gba, id, replace_lanes(0, &accesses[a]));
        destroy(gba);
    }
    CHECK(scenes == 1568);
}

static void reset_discards_storage_without_restarting_the_stage(void) {
    for (unsigned e = 0; e < 4; ++e) for (unsigned id = 0; id < 2; ++id)
    for (unsigned left = 0; left < 4; ++left) for (unsigned words = 0; words < 8; ++words)
    for (unsigned reset = 0; reset < 4; ++reset) {
        struct CupidGBA* gba = machine(e);
        rotate(gba, id, words);
        stage(gba, id, left);
        for (unsigned i = 0; i < words; ++i)
            cupid_memory_store32(&gba->cpu, port(id), (int32_t)(0x91827364u + i), NULL);
        const struct CupidAudioFIFO before = *selected(gba, id);
        unsigned bit = id ? 0x8000 : 0x800;
        if (reset == 0) cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_SOUNDCNT_H + 1, (int8_t)(bit >> 8), NULL);
        else if (reset == 1) cupid_io_write(gba, REG_SOUNDCNT_H, (uint16_t)bit);
        else if (reset == 2) cupid_memory_store32(&gba->cpu, GBA_BASE_IO + REG_SOUNDCNT_L, (int32_t)(bit << 16), NULL);
        else {
            cupid_io_write(gba, REG_SOUNDCNT_X, 0);
            cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
        }
        require_empty_storage(selected(gba, id));
        CHECK(selected(gba, id)->internalSample == before.internalSample);
        CHECK(selected(gba, id)->internalRemaining == before.internalRemaining);
        CHECK(selected(gba, id)->latchedSample == before.latchedSample);
        finish_stage(gba, id, left);
        store(gba, id, &accesses[2]);
        play(gba, id, replace_lanes(0, &accesses[2]));
        destroy(gba);
    }
    CHECK(scenes == 1024);
}

static void dma_overflow_keeps_every_bus_and_completion_edge(void) {
    for (unsigned e = 0; e < 4; ++e) for (unsigned id = 0; id < 2; ++id)
    for (unsigned word = 0; word < 2; ++word) for (unsigned left = 0; left < 4; ++left) {
        struct CupidGBA* gba = machine(e);
        stage(gba, id, left);
        for (unsigned i = 0; i < 7; ++i)
            cupid_memory_store32(&gba->cpu, port(id), (int32_t)(0x11223344u + i), NULL);
        cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM + 0x400, 0x12345678, NULL);
        cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM + 0x404, (int32_t)0xFEDCBA98, NULL);
        cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x400);
        cupid_dma_write_dad(gba, 3, port(id));
        cupid_dma_write_cnt_lo(gba, 3, 2);
        cupid_dma_write_cnt(gba, 3, (uint16_t)(0xC040 | (word << 10)));
        struct CupidDMA* dma = &gba->memory.dma[3];
        uint64_t start = cupid_timing_global_time(&gba->timing);
        for (unsigned clock = 1; clock <= 9; ++clock) {
            cupid_timing_tick(&gba->timing, 1);
            unsigned writes = (clock >= 5) + (clock >= 7);
            CHECK(dma->nextSource == GBA_BASE_IWRAM + 0x400 + writes * (word ? 4 : 2));
            CHECK(dma->nextDest == port(id));
            CHECK(dma->nextCount == 2 - (int)writes);
            CHECK(gba->memory.dmaRunning == (clock >= 2 && clock < 8));
            CHECK((gba->irqf & IRQ_DMA3) == (clock >= 9 ? IRQ_DMA3 : 0));
            if (clock == 5) require_empty_storage(selected(gba, id));
        }
        CHECK(dma->busyUntil == start + 7 && !gba->cpuBlocked);
        CHECK(selected(gba, id)->internalRemaining == (int)left);
        for (unsigned byte = 4 - left; byte < 4; ++byte)
            sample(gba, id, (uint8_t)(UINT32_C(0xD4C3B2A1) >> (byte * 8)));
        play(gba, id, word ? 0xFEDCBA98 : 0x1234);
        sample(gba, id, 0);
        destroy(gba);
    }
    /* A refill already accepted at a low level still runs after CPU writes
     * fill the queue during its activation interval. Overflow affects FIFO
     * storage, including the first burst word, without cancelling the burst. */
    const uint32_t payload[] = {0x76543210, 0xFEDCBA98, 0x67452301, 0xEFCDAB89};
    for (unsigned e = 0; e < 4; ++e) for (unsigned id = 0; id < 2; ++id)
    for (unsigned channel = 1; channel <= 2; ++channel) for (unsigned word = 0; word < 2; ++word) {
        struct CupidGBA* gba = machine(e);
        cupid_io_write(gba, REG_SOUNDCNT_H, id ? 0x0400 : 0x4000);
        cupid_memory_store32(&gba->cpu, port(id), (int32_t)0xD4C3B2A1, NULL);
        for (unsigned i = 0; i < 2; ++i)
            cupid_memory_store32(&gba->cpu, port(id), (int32_t)(0x10203040u + i), NULL);
        for (unsigned i = 0; i < 4; ++i)
            cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM + 0x400 + i * 4, (int32_t)payload[i], NULL);
        cupid_dma_write_sad(gba, (int)channel, GBA_BASE_IWRAM + 0x400);
        cupid_dma_write_dad(gba, (int)channel, port(id));
        cupid_dma_write_cnt_lo(gba, (int)channel, 99);
        cupid_dma_write_cnt(gba, (int)channel, (uint16_t)(0xF240 | (word << 10)));
        struct CupidDMA* dma = &gba->memory.dma[channel];
        uint64_t start = cupid_timing_global_time(&gba->timing);
        cupid_audio_timer_overflow(&gba->audio, 0, start);
        CHECK(selected(gba, id)->latchedSample == (int8_t)0xA1);
        CHECK(selected(gba, id)->internalRemaining == 3);
        CHECK(cupid_timing_is_scheduled(&gba->timing, &dma->requestEvent));
        CHECK(dma->requestEvent.when == (uint32_t)(start + 3));
        for (unsigned i = 0; i < 5; ++i)
            cupid_memory_store32(&gba->cpu, port(id), (int32_t)(0x91827364u + i), NULL);
        for (unsigned clock = 1; clock <= 14; ++clock) {
            cupid_timing_tick(&gba->timing, 1);
            unsigned writes = 0;
            for (unsigned i = 0; i < 4; ++i) writes += clock >= 6 + 2 * i;
            CHECK(dma->nextSource == GBA_BASE_IWRAM + 0x400 + writes * 4);
            CHECK(dma->nextDest == port(id) && dma->count == 99);
            CHECK(dma->nextCount == (writes == 4 ? 4 : 4 - (int)writes));
            CHECK(gba->memory.dmaRunning == (clock >= 3 && clock < 13));
            CHECK((gba->irqf & (IRQ_DMA0 << channel)) == (clock >= 14 ? IRQ_DMA0 << channel : 0));
            CHECK(selected(gba, id)->internalRemaining == 3);
            CHECK(selected(gba, id)->latchedSample == (int8_t)0xA1);
            if (clock == 6) require_empty_storage(selected(gba, id));
            for (unsigned i = 1; i < writes; ++i) CHECK(selected(gba, id)->fifo[i - 1] == payload[i]);
        }
        CHECK(dma->busyUntil == start + 12 && !gba->cpuBlocked);
        cupid_dma_write_cnt(gba, (int)channel, dma->reg & 0x7FFF);
        sample(gba, id, 0xB2); sample(gba, id, 0xC3); sample(gba, id, 0xD4);
        for (unsigned i = 1; i < 4; ++i) play(gba, id, payload[i]);
        sample(gba, id, 0);
        destroy(gba);
    }
    CHECK(scenes == 96);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "rotations")) RUN(rotations_reuse_seven_storage_words);
    else if (!strcmp(argv[1], "overflow")) RUN(overflowing_writes_discard_the_queue_and_keep_the_stage);
    else if (!strcmp(argv[1], "reset")) RUN(reset_discards_storage_without_restarting_the_stage);
    else { CHECK(!strcmp(argv[1], "dma")); RUN(dma_overflow_keeps_every_bus_and_completion_edge); }
    printf("PASS %u FIFO queue scenes\n", scenes);
    return 0;
}
