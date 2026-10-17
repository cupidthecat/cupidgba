/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"

static const uint64_t origins[] = {0, INT32_MAX - 35u, UINT32_MAX - 35u,
                                    UINT64_C(0x300000000) - 35};
struct Store { unsigned width, lane; uint32_t value, expected; };
/* Unwritten lanes retain 44332211h in a previously used, now empty slot. */
static const struct Store stores[] = {
    {1, 0, 0xF0, 0x443322F0}, {1, 1, 0xE1, 0x4433E111},
    {1, 2, 0xD2, 0x44D22211}, {1, 3, 0xC3, 0xC3332211},
    {2, 0, 0x89AB, 0x443389AB}, {2, 2, 0xCDEF, 0xCDEF2211},
    {4, 0, 0xF3E2D1C0, 0xF3E2D1C0}
};
static unsigned scenes;

static struct CupidAudioFIFO* fifo(struct CupidGBA* g, unsigned id) {
    return id ? &g->audio.chB : &g->audio.chA;
}
static uint32_t port(unsigned id) {
    return GBA_BASE_IO + (id ? REG_FIFO_B_LO : REG_FIFO_A_LO);
}
static unsigned count(const struct CupidAudioFIFO* f) {
    return (unsigned)(f->fifoWrite - f->fifoRead + GBA_AUDIO_FIFO_SIZE) % GBA_AUDIO_FIFO_SIZE;
}
static void advance(struct CupidGBA* g, unsigned clocks) {
    uint64_t before = cupid_timing_global_time(&g->timing);
    int32_t pending = g->cpu.cycles;
    g->cpu.cycles = 0;
    g->cpu.nextEvent = cupid_timing_tick(&g->timing, pending + (int32_t)clocks);
    CHECK(cupid_timing_global_time(&g->timing) == before + clocks);
}
static void sample(struct CupidGBA* g, unsigned id, uint8_t expected) {
    advance(g, 1);
    cupid_audio_sample_fifo(&g->audio, id, cupid_timing_global_time(&g->timing));
    CHECK(fifo(g, id)->latchedSample == (int8_t)expected);
}
static void play(struct CupidGBA* g, unsigned id, uint32_t word) {
    for (unsigned byte = 0; byte < 4; ++byte) sample(g, id, (uint8_t)(word >> (byte * 8)));
}
static void store(struct CupidGBA* g, const struct Store* s, uint32_t base) {
    if (s->width == 1) cupid_memory_store8(&g->cpu, base + s->lane, (int8_t)s->value, NULL);
    else if (s->width == 2) cupid_memory_store16(&g->cpu, base + s->lane, (int16_t)s->value, NULL);
    else cupid_memory_store32(&g->cpu, base, (int32_t)s->value, NULL);
}
static struct CupidGBA* machine(uint64_t origin, unsigned id, bool occupied) {
    struct CupidGBA* g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g); cupid_gba_reset(g); cupid_timing_clear(&g->timing);
    g->timing.masterCycles = (uint32_t)origin; g->timing.globalCycles = origin;
    g->audio.lastSample = g->audio.sampleOrigin = origin;
    g->audio.nextSample = origin + 512;
    cupid_io_write(g, REG_SOUNDCNT_X, 0x80);
    /* Reuse every storage slot without overflowing or resetting the FIFO.
     * The stage is drained independently, leaving its most recent output. */
    for (unsigned i = 0; i < 7; ++i)
        cupid_memory_store32(&g->cpu, port(id), 0x44332211, NULL);
    for (unsigned i = 0; i < 7; ++i) play(g, id, 0x44332211);
    cupid_memory_store32(&g->cpu, port(id), 0x44332211, NULL);
    play(g, id, 0x44332211);
    CHECK(count(fifo(g, id)) == 0 && fifo(g, id)->internalRemaining == 0);
    if (occupied) {
        cupid_memory_store32(&g->cpu, port(id), (int32_t)0x88776655, NULL);
        sample(g, id, 0x55);
        CHECK(fifo(g, id)->internalRemaining == 3);
    }
    return g;
}
static void destroy(struct CupidGBA* g) { cupid_gba_destroy(g); free(g); }
static void finish(struct CupidGBA* g, unsigned id, bool occupied, uint32_t expected) {
    if (occupied) { sample(g, id, 0x66); sample(g, id, 0x77); sample(g, id, 0x88); }
    play(g, id, expected);
    CHECK(count(fifo(g, id)) == 0 && fifo(g, id)->internalRemaining == 0);
}

static void every_width_appends_one_word_and_preserves_the_stage(void) {
    for (unsigned e = 0; e < 4; ++e) for (unsigned id = 0; id < 2; ++id)
    for (unsigned occupied = 0; occupied < 2; ++occupied) for (unsigned s = 0; s < 7; ++s) {
        struct CupidGBA* g = machine(origins[e], id, occupied);
        struct CupidAudioFIFO before = *fifo(g, id), other = *fifo(g, id ^ 1);
        uint64_t clock = cupid_timing_global_time(&g->timing);
        store(g, &stores[s], port(id));
        CHECK(cupid_timing_global_time(&g->timing) == clock);
        CHECK(count(fifo(g, id)) == 1);
        CHECK(fifo(g, id)->fifo[before.fifoWrite] == stores[s].expected);
        CHECK(fifo(g, id)->internalSample == before.internalSample);
        CHECK(fifo(g, id)->internalRemaining == before.internalRemaining);
        CHECK(fifo(g, id)->latchedSample == before.latchedSample);
        CHECK(!memcmp(fifo(g, id ^ 1), &other, sizeof(other)));
        finish(g, id, occupied, stores[s].expected);
        scenes++; destroy(g);
    }
}
static void issue_store(struct CupidGBA* g, unsigned mode, const struct Store* s, uint32_t base) {
    const uint32_t arm[] = {0xE5C01000, 0xE1C010B0, 0xE5801000};
    const uint16_t thumb[] = {0x7001, 0x8001, 0x6001};
    unsigned opcode = s->width == 1 ? 0 : s->width == 2 ? 1 : 2;
    _CupidSetMode(&g->cpu, mode ? MODE_THUMB : MODE_ARM);
    if (mode) memcpy(g->memory.iwram, &thumb[opcode], sizeof(*thumb));
    else memcpy(g->memory.iwram, &arm[opcode], sizeof(*arm));
    g->cpu.gprs[0] = base + s->lane; g->cpu.gprs[1] = (int32_t)s->value;
    g->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (mode) { CupidThumbWritePC(&g->cpu); cupid_thumb_step(&g->cpu); }
    else { CupidARMWritePC(&g->cpu); cupid_arm_step(&g->cpu); }
}
/* Reserved I/O is the same bus request class without FIFO publication. */
static void arm_and_thumb_stores_keep_the_existing_bus_clocks(void) {
    for (unsigned e = 0; e < 4; ++e) for (unsigned id = 0; id < 2; ++id)
    for (unsigned mode = 0; mode < 2; ++mode) for (unsigned s = 0; s < 7; ++s) {
        struct CupidGBA* g = machine(origins[e], id, true);
        struct CupidGBA* control = machine(origins[e], id, true);
        struct CupidAudioFIFO before = *fifo(g, id);
        issue_store(g, mode, &stores[s], port(id));
        issue_store(control, mode, &stores[s], GBA_BASE_IO + 0x3F0);
        CHECK(cupid_timing_global_time(&g->timing) == cupid_timing_global_time(&control->timing));
        CHECK(count(fifo(g, id)) == 1 && count(fifo(control, id)) == 0);
        CHECK(fifo(g, id)->internalSample == before.internalSample);
        CHECK(fifo(g, id)->internalRemaining == before.internalRemaining);
        CHECK(fifo(g, id)->latchedSample == before.latchedSample);
        finish(g, id, true, stores[s].expected);
        scenes++; destroy(g); destroy(control);
    }
}
static void start_dma(struct CupidGBA* g, const struct Store* s, uint32_t base) {
    cupid_memory_store32(&g->cpu, GBA_BASE_IWRAM + 0x400, (int32_t)s->value, NULL);
    cupid_dma_write_sad(g, 3, GBA_BASE_IWRAM + 0x400);
    cupid_dma_write_dad(g, 3, base + s->lane);
    cupid_dma_write_cnt_lo(g, 3, 1);
    cupid_dma_write_cnt(g, 3, (uint16_t)(0xC040 | (s->width == 4 ? 0x400 : 0)));
}
static void dma_halfwords_and_words_keep_complete_transfer_histories(void) {
    for (unsigned e = 0; e < 4; ++e) for (unsigned id = 0; id < 2; ++id)
    for (unsigned s = 4; s < 7; ++s) {
        struct CupidGBA* g = machine(origins[e], id, true);
        struct CupidGBA* control = machine(origins[e], id, true);
        struct CupidAudioFIFO before = *fifo(g, id);
        start_dma(g, &stores[s], port(id));
        start_dma(control, &stores[s], GBA_BASE_IO + 0x3F0);
        for (unsigned clock = 0; clock < 32; ++clock) {
            advance(g, 1); advance(control, 1);
            CHECK(cupid_timing_global_time(&g->timing) == cupid_timing_global_time(&control->timing));
            CHECK(g->irqf == control->irqf && g->irq.line == control->irq.line);
            CHECK(g->memory.dmaRunning == control->memory.dmaRunning);
            CHECK(g->memory.dma[3].nextSource == control->memory.dma[3].nextSource);
            CHECK(g->memory.dma[3].nextCount == control->memory.dma[3].nextCount);
            CHECK(g->memory.dma[3].busyUntil == control->memory.dma[3].busyUntil);
        }
        CHECK(!g->memory.dma[3].enable && g->irqf == IRQ_DMA3);
        CHECK(g->memory.dma[3].nextSource == GBA_BASE_IWRAM + 0x400 + stores[s].width);
        CHECK(count(fifo(g, id)) == 1 && fifo(g, id)->internalSample == before.internalSample);
        CHECK(fifo(g, id)->internalRemaining == before.internalRemaining);
        CHECK(fifo(g, id)->latchedSample == before.latchedSample);
        finish(g, id, true, stores[s].expected);
        scenes++; destroy(g); destroy(control);
    }
}
static void mixed_widths_retain_every_complete_output_word(void) {
    for (unsigned e = 0; e < 4; ++e) for (unsigned id = 0; id < 2; ++id) {
        struct CupidGBA* g = machine(origins[e], id, false);
        for (unsigned s = 0; s < 7; ++s) {
            store(g, &stores[s], port(id));
            CHECK(count(fifo(g, id)) == s + 1);
        }
        for (unsigned s = 0; s < 7; ++s) play(g, id, stores[s].expected);
        CHECK(count(fifo(g, id)) == 0 && fifo(g, id)->internalRemaining == 0);
        scenes++; destroy(g);
    }
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "lanes")) { RUN(every_width_appends_one_word_and_preserves_the_stage); CHECK(scenes == 112); }
    else if (!strcmp(argv[1], "cpu")) { RUN(arm_and_thumb_stores_keep_the_existing_bus_clocks); CHECK(scenes == 112); }
    else if (!strcmp(argv[1], "dma")) { RUN(dma_halfwords_and_words_keep_complete_transfer_histories); CHECK(scenes == 24); }
    else if (!strcmp(argv[1], "mixed")) { RUN(mixed_widths_retain_every_complete_output_word); CHECK(scenes == 8); }
    else CHECK(false);
    printf("PASS %u complete FIFO access-width scenes\n", scenes);
    return 0;
}
