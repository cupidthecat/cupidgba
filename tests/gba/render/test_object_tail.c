/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../../src/gba/render/pipeline.h"
#include "../../test.h"

static const uint64_t epochs[] = {0, INT32_MAX - 1000u, UINT32_MAX - 1000u, UINT64_C(0x300000000)};
static const unsigned modes[] = {0, 3, 4, 5};

static struct CupidGBA *scene(unsigned epoch, unsigned mode, bool color, bool freeHblank,
                              bool shortTail) {
    struct CupidGBA *g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    cupid_gba_reset(g);
    cupid_timing_clear(&g->timing);
    g->timing.globalCycles = epochs[epoch];
    g->timing.masterCycles = (uint32_t)epochs[epoch];
    g->cpu.cpsr.i = 1;
    g->cpu.irqDisableLatch = true;
    struct CupidVideo *v = &g->video;
    v->dispcnt = (uint16_t)(mode | 0x1000 | (freeHblank ? 0x20 : 0));
    v->vcount = 10;
    v->pipeline.start = epochs[epoch];
    v->pipeline.initialCycle = 0;
    v->pipeline.cycle = 40;
    for (unsigned i = 0; i < 3; i++)
        v->pipeline.displayLatch[i] = v->dispcnt;
    memset(v->vram, 0x11, GBA_SIZE_VRAM);
    for (unsigned i = 0; i < 128; i++)
        v->oam.raw[i * 4] = 0x0200;
    /* Fourteen overlapping 64-pixel objects keep the texture engine busy.
     * The final object exposes its tail at x=64, without earlier priorities
     * hiding those pixels. An eight-pixel tail leaves OAM scanning alone. */
    for (unsigned i = 0; i < 15; i++) {
        v->oam.raw[i * 4] = (uint16_t)(11 | (color ? 0x2000 : 0));
        v->oam.raw[i * 4 + 1] =
            (uint16_t)((i == 14 ? 64 : 0) | (i == 14 && shortTail ? 0 : 0xC000));
        v->oam.raw[i * 4 + 2] = mode < 3 ? 0 : 512;
    }
    cupid_timing_tick(&g->timing, 40);
    cupid_pixel_object_begin(v, cupid_timing_global_time(&g->timing));
    CHECK(v->pipeline.objects.limit == (freeHblank ? 964u : 1232u));
    return g;
}

static void advance(struct CupidGBA *g, unsigned phase) {
    const uint64_t target = g->video.pipeline.objects.start + phase;
    const uint64_t now = cupid_timing_global_time(&g->timing);
    CHECK(target >= now && target - now < 2000);
    cupid_timing_tick(&g->timing, (int32_t)(target - now));
    cupid_video_sync(&g->video);
}

static void destroy(struct CupidGBA *g) {
    cupid_gba_destroy(g);
    free(g);
}

static void ports_and_pixels(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned mode = 0; mode < 4; mode++)
            for (unsigned color = 0; color < 2; color++)
                for (unsigned freeHblank = 0; freeHblank < 2; freeHblank++)
                    for (unsigned changed = 0; changed < 2; changed++)
                        for (unsigned shortTail = 0; shortTail < 2; shortTail++) {
                            struct CupidGBA *g = scene(epoch, modes[mode], color != 0,
                                                       freeHblank != 0, shortTail != 0);
                            advance(g, 956);
                            if (changed)
                                cupid_io_write(g, REG_DISPCNT, g->video.dispcnt ^ 0x20);
                            for (unsigned phase = 957; phase <= 968; phase++) {
                                advance(g, phase);
                                uint32_t address = modes[mode] < 3 ? 0x06010000 : 0x06014000;
                                const bool texture = !shortTail && (phase & 1) &&
                                                     phase <= (freeHblank ? 959u : 963u);
                                CHECK(cupid_video_memory_busy(&g->video, address) == texture);
                                if (shortTail) {
                                    const bool attributes =
                                        (phase & 1) && (!freeHblank || phase <= 963);
                                    CHECK(cupid_video_memory_busy(&g->video, 0x07000000) ==
                                          attributes);
                                }
                            }
                            const struct CupidObjectFetch *f = &g->video.pipeline.objects;
                            unsigned pixels = shortTail ? 72 : (freeHblank ? 124 : 128);
                            for (unsigned x = 0; x < GBA_SCREEN_WIDTH; x++)
                                CHECK(f->pixels[f->buffer ^ 1][x].index ==
                                      (x < pixels ? (color ? 0x11 : 1) : 0));
                            CHECK(f->limit == (freeHblank ? 964u : 1232u));
                            destroy(g);
                            scenes++;
                        }
    CHECK(scenes == 256);
    printf("object texture/attribute tail scenes=%u\n", scenes);
}

static void affine_tail(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned mode = 0; mode < 4; mode++)
            for (unsigned color = 0; color < 2; color++)
                for (unsigned freeHblank = 0; freeHblank < 2; freeHblank++)
                    for (unsigned changed = 0; changed < 2; changed++) {
                        struct CupidGBA *g =
                            scene(epoch, modes[mode], color != 0, freeHblank != 0, false);
                        for (unsigned i = 0; i < 128; i++)
                            g->video.oam.raw[i * 4] = 0x0200;
                        for (unsigned i = 0; i < 8; i++) {
                            g->video.oam.raw[i * 4] =
                                (uint16_t)(11 | 0x0100 | (color ? 0x2000 : 0));
                            g->video.oam.raw[i * 4 + 1] =
                                (uint16_t)(0xC000 | (i < 6 ? 0 : (i - 5) * 64));
                            g->video.oam.raw[i * 4 + 2] = modes[mode] < 3 ? 0 : 512;
                        }
                        g->video.oam.raw[3] = g->video.oam.raw[15] = 0x0100;
                        g->video.oam.raw[7] = g->video.oam.raw[11] = 0;
                        advance(g, 956);
                        if (changed)
                            cupid_io_write(g, REG_DISPCNT, g->video.dispcnt ^ 0x20);
                        advance(g, 960);
                        struct CupidObjectFetch *f = &g->video.pipeline.objects;
                        struct CupidObjectPixel before[GBA_SCREEN_WIDTH];
                        memcpy(before, f->pixels[f->buffer ^ 1], sizeof(before));
                        uint32_t address = modes[mode] < 3 ? 0x06010000 : 0x06014000;
                        for (unsigned phase = 961; phase <= 963; phase += 2) {
                            advance(g, phase);
                            CHECK(cupid_video_memory_busy(&g->video, address) == !freeHblank);
                        }
                        advance(g, 976);
                        CHECK((memcmp(before, f->pixels[f->buffer ^ 1], sizeof(before)) == 0) ==
                              (freeHblank != 0));
                        CHECK(f->pixels[f->buffer ^ 1][122].index == (color ? 0x11 : 1));
                        CHECK(f->pixels[f->buffer ^ 1][127].index ==
                              (freeHblank ? 0 : (color ? 0x11 : 1)));
                        destroy(g);
                        scenes++;
                    }
    CHECK(scenes == 128);
    printf("affine object-tail scenes=%u\n", scenes);
}

static unsigned read_clocks(unsigned start, unsigned width, bool freeHblank) {
    unsigned clock = start;
    for (unsigned beat = 0; beat < (width == 4 ? 2u : 1u); beat++) {
        ++clock;
        /* The dense row fetches on even clocks through 958 or 962.
         * Each access owns the following CPU-ready edge. */
        if ((clock & 1) && clock <= (freeHblank ? 959u : 963u))
            ++clock;
    }
    return clock - start;
}

static void cpu_reads(unsigned mode) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned color = 0; color < 2; color++)
            for (unsigned freeHblank = 0; freeHblank < 2; freeHblank++)
                for (unsigned changed = 0; changed < 2; changed++)
                    for (unsigned width = 1; width <= 4; width *= 2)
                        for (unsigned phase = 956; phase <= 964; phase++) {
                            struct CupidGBA *g =
                                scene(epoch, modes[mode], color != 0, freeHblank != 0, false);
                            advance(g, phase);
                            if (changed)
                                cupid_io_write(g, REG_DISPCNT, g->video.dispcnt ^ 0x20);
                            g->cpu.memory.timed = true;
                            const uint64_t before = cupid_timing_global_time(&g->timing);
                            const uint32_t address = modes[mode] < 3 ? 0x06010000 : 0x06014000;
                            int internal = 0;
                            uint32_t value =
                                width == 4   ? cupid_memory_load32(&g->cpu, address, &internal)
                                : width == 2 ? cupid_memory_load16(&g->cpu, address, &internal)
                                             : cupid_memory_load8(&g->cpu, address, &internal);
                            CHECK(value == (width == 4   ? 0x11111111u
                                            : width == 2 ? 0x1111u
                                                         : 0x11u));
                            CHECK(internal == 1 && !g->memory.busBusy);
                            CHECK(cupid_timing_global_time(&g->timing) - before ==
                                  read_clocks(phase, width, freeHblank != 0));
                            destroy(g);
                            scenes++;
                        }
    CHECK(scenes == 864);
    printf("object-tail CPU read scenes=%u\n", scenes);
}

static void dma_reads(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned mode = 0; mode < 4; mode++)
            for (unsigned freeHblank = 0; freeHblank < 2; freeHblank++)
                for (unsigned changed = 0; changed < 2; changed++)
                    for (unsigned channel = 0; channel < 4; channel++) {
                        struct CupidGBA *g =
                            scene(epoch, modes[mode], true, freeHblank != 0, false);
                        advance(g, 955);
                        if (changed)
                            cupid_io_write(g, REG_DISPCNT, g->video.dispcnt ^ 0x20);
                        const uint32_t source = modes[mode] < 3 ? 0x06010000 : 0x06014000;
                        const unsigned write = freeHblank ? 962 : 963;
                        cupid_dma_write_sad(g, channel, source);
                        cupid_dma_write_dad(g, channel, 0x03000200);
                        cupid_dma_write_cnt_lo(g, channel, 1);
                        cupid_dma_write_cnt(g, channel, 0xC400);
                        for (unsigned phase = 956; phase <= 967; phase++) {
                            advance(g, phase);
                            uint32_t expected = phase < write ? 0 : 0x11111111;
                            CHECK(cupid_memory_load32(&g->cpu, 0x03000200, NULL) == expected);
                            CHECK(g->memory.dma[channel].enable == (phase < write));
                            CHECK((cupid_io_read(g, REG_IF) & (IRQ_DMA0 << channel)) ==
                                  (phase < write + 2 ? 0 : IRQ_DMA0 << channel));
                        }
                        CHECK(!g->memory.dmaRunning && !g->cpuBlocked);
                        CHECK(g->memory.dma[channel].nextCount == 0);
                        CHECK(g->memory.dma[channel].nextSource == source + 4);
                        CHECK(g->memory.dma[channel].nextDest == 0x03000204);
                        CHECK(g->memory.dma[channel].latch == 0x11111111);
                        destroy(g);
                        scenes++;
                    }
    CHECK(scenes == 256);
    printf("object-tail DMA histories=%u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "ports"))
        RUN(ports_and_pixels);
    else if (!strcmp(argv[1], "affine"))
        RUN(affine_tail);
    else if (!strcmp(argv[1], "dma"))
        RUN(dma_reads);
    else if (!strncmp(argv[1], "cpu-", 4) && argv[1][4] >= '0' && argv[1][4] <= '3' && !argv[1][5])
        cpu_reads((unsigned)(argv[1][4] - '0'));
    else
        CHECK(false);
    return 0;
}
