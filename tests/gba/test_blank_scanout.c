/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"

static const unsigned positions[] = {0, 1, 45, 46, 47, 48, 49, 50,
    1004, 1005, 1006, 1020, 1023, 1191, 1192, 1230};
static const unsigned spans[] = {1, 15, 16, 17, 65, 1232};
static uint64_t stateDigest = UINT64_C(14695981039346656037);

static void prepare(struct CupidGBA* gba, unsigned config, unsigned first, bool active) {
    struct CupidVideo* v = &gba->video;
    uint16_t* vram = v->vram;
    memset(v, 0, sizeof(*v));
    v->gba = gba;
    v->vram = vram;
    v->vcount = config & 1 ? 159 : 27;
    v->dispcnt = 0xE080 | (config & 7);
    v->mosaic = (uint16_t)(0x1357 * (config + 1));
    v->win.h[0][0] = 3; v->win.h[0][1] = 2;
    v->win.h[1][0] = config & 1 ? 255 : 0;
    v->win.h[1][1] = config & 1 ? 0 : 255;
    for (unsigned i = 0; i < GBA_SCREEN_WIDTH; ++i) v->scanline[i] = 0x1357;
    for (unsigned i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++i)
        v->outputBuffer[i] = 0x12345678;
    for (unsigned i = 0; i < 128; ++i) v->oam.obj[i].a = 0x0200;
    gba->memory.io[REG_GREENSWAP >> 1] = config & 1;
    struct CupidPixelPipeline* p = &v->pipeline;
    const uint64_t origins[] = {8192, (uint64_t)UINT32_MAX - 100, UINT64_C(0x300000000)};
    const unsigned initial[] = {0, 1, 31};
    uint64_t origin = origins[config / 4];
    p->initialCycle = initial[config % 3];
    p->start = origin + p->initialCycle;
    p->cycle = first;
    p->blank = true;
    p->layer[0] = p->layer[1] = 5;
    p->color[0] = 0x7FFF;
    p->left = 0x1357;
    /* Forced blank retains an earlier OBJ mosaic latch until a later normal
     * selection replaces it, even while OBJ fetching is disabled. */
    p->objectLatch.index = 17;
    p->objectLatch.priority = 2;
    p->objectLatch.semi = p->objectLatch.window = p->objectLatch.mosaic = true;
    p->paletteAccess = p->backgroundAccess = p->objectAccess = p->oamAccess = UINT64_MAX;
    p->bgMosaicX = config & 15; p->objMosaicX = (config * 3) & 15;
    p->bgMosaicY = (config * 5) & 15; p->objMosaicY = (config * 7) & 15;
    p->windowH[0] = config & 1; p->windowH[1] = config & 2;
    p->windowV[0] = config & 4; p->windowV[1] = config & 8;
    memset(p->windows, 0xA5, sizeof(p->windows));
    p->objects.active = active;
    /* An active engine has already received its start latch. A future start
     * belongs to a separate pending latch, not an already active fetcher. */
    const int offsets[] = {0, -1, -32, -1200};
    p->objects.start = origin + offsets[config % 4];
    CHECK(!p->objects.active || p->objects.start <= origin + first);
    p->objects.limit = config & 1 ? 964 : 1232;
    p->objects.cycle = config % 3 == 0 ? 0
        : p->objects.limit - (config % 3 == 1 ? 1 : 0);
    p->objects.line = config & 1 ? 159 : 28;
}

static void digest(const void* memory, size_t length) {
    const uint8_t* bytes = memory;
    for (size_t i = 0; i < length; ++i)
        stateDigest = (stateDigest ^ bytes[i]) * UINT64_C(1099511628211);
}

static void compare(const struct CupidVideo* bulk, const struct CupidVideo* single,
                     unsigned config, unsigned first, unsigned end, const char* phase) {
    /* The pipeline contains no host pointers. Zero-initialized complete state
     * includes fetch markers, OBJ history, windows, mosaics and access clocks. */
    if (memcmp(&bulk->pipeline, &single->pipeline, sizeof(bulk->pipeline))) {
        const uint8_t* left = (const uint8_t*)&bulk->pipeline;
        const uint8_t* right = (const uint8_t*)&single->pipeline;
        for (unsigned i = 0; i < sizeof(bulk->pipeline); ++i) if (left[i] != right[i]) {
            fprintf(stderr, "config=%u first=%u end=%u phase=%s pipeline-byte=%u bulk=%u single=%u obj-cycle=%u/%u mosaic-y=%u/%u\n",
                config, first, end, phase, i, left[i], right[i],
                bulk->pipeline.objects.cycle, single->pipeline.objects.cycle,
                bulk->pipeline.objMosaicY, single->pipeline.objMosaicY);
            break;
        }
    }
    CHECK(!memcmp(&bulk->pipeline, &single->pipeline, sizeof(bulk->pipeline)));
    CHECK(!memcmp(bulk->scanline, single->scanline, sizeof(bulk->scanline)));
    CHECK(!memcmp(bulk->outputBuffer, single->outputBuffer, sizeof(bulk->outputBuffer)));
    CHECK(!memcmp(bulk->bg, single->bg, sizeof(bulk->bg)));
    /* Printed for independent complete-core controls on the same host. The
     * digest contains no pointers; no host-layout hash is an acceptance value. */
    digest(&bulk->pipeline, sizeof(bulk->pipeline));
    digest(bulk->scanline, sizeof(bulk->scanline));
    digest(bulk->bg, sizeof(bulk->bg));
    digest(bulk->outputBuffer + bulk->vcount * GBA_SCREEN_WIDTH,
        GBA_SCREEN_WIDTH * sizeof(bulk->outputBuffer[0]));
}

static void advance(struct CupidVideo* v, unsigned end, bool single) {
    uint64_t origin = v->pipeline.start - v->pipeline.initialCycle;
    if (single) for (unsigned cycle = v->pipeline.cycle + 1; cycle <= end; ++cycle)
        cupid_video_sync_to(v, origin + cycle);
    else cupid_video_sync_to(v, origin + end);
}

int main(void) {
    struct CupidGBA* bulk = calloc(1, sizeof(*bulk));
    struct CupidGBA* single = calloc(1, sizeof(*single));
    CHECK(bulk && single);
    bulk->video.vram = calloc(1, GBA_SIZE_VRAM);
    single->video.vram = calloc(1, GBA_SIZE_VRAM);
    CHECK(bulk->video.vram && single->video.vram);
    unsigned scenes = 0;
    const unsigned initial[] = {0, 1, 31};
    for (unsigned active = 0; active < 2; ++active)
      for (unsigned config = 0; config < 12; ++config)
        for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
            unsigned first = positions[i];
            if (first < initial[config % 3]) continue;
            for (unsigned j = 0; j < sizeof(spans) / sizeof(spans[0]); ++j) {
                unsigned end = first + spans[j];
                if (end > 1232) end = 1232;
                prepare(bulk, config, first, active != 0);
                prepare(single, config, first, active != 0);
                advance(&bulk->video, end, false);
                advance(&single->video, end, true);
                compare(&bulk->video, &single->video, config, first, end, "blank");
                for (unsigned x = 0; x < GBA_SCREEN_WIDTH; ++x) {
                    unsigned output = 48 + x * 4;
                    CHECK(bulk->video.scanline[x] == (output >= first && output < end ? 0x7FFF : 0x1357));
                    unsigned right = x | 1, pairClock = 48 + right * 4;
                    uint32_t expected = 0x12345678;
                    if (pairClock >= first && pairClock < end) {
                        bool coloredLeft = pairClock - 4 < first;
                        uint16_t color = 0x7FFF;
                        if (coloredLeft) color = config & 1
                            ? (x & 1 ? 0x7F5F : 0x13F7) : (x & 1 ? 0x7FFF : 0x1357);
                        expected = color16_to_rgba(color);
                    }
                    CHECK(bulk->video.outputBuffer[bulk->video.vcount * GBA_SCREEN_WIDTH + x] == expected);
                }
                /* A later display enable must see the exact retained fetch,
                 * mosaic and pair state, including an incomplete left pixel. */
                bulk->video.dispcnt = single->video.dispcnt = 0x1403;
                bulk->video.pipeline.displayLatch[0] = single->video.pipeline.displayLatch[0] = 0x1403;
                advance(&bulk->video, 1232, false);
                advance(&single->video, 1232, true);
                compare(&bulk->video, &single->video, config, first, end, "enabled");
                ++scenes;
            }
        }
    CHECK(scenes == 2160);
    /* Cover every four-bit starting phase at every selected horizontal width,
     * including phases retained above a newly shortened period. */
    for (unsigned width = 1; width <= 16; ++width) for (unsigned phase = 0; phase < 16; ++phase) {
        prepare(bulk, 3, 0, true);
        prepare(single, 3, 0, true);
        struct CupidVideo* videos[] = {&bulk->video, &single->video};
        for (unsigned i = 0; i < 2; ++i) {
            struct CupidVideo* v = videos[i];
            v->mosaic = (uint16_t)((width - 1) | ((width - 1) << 8));
            v->pipeline.bgMosaicX = v->pipeline.objMosaicX = phase;
            v->pipeline.objects.start = v->pipeline.start - 1192;
            v->pipeline.objects.cycle = v->pipeline.objects.limit;
        }
        advance(&bulk->video, 1006, false);
        advance(&single->video, 1006, true);
        compare(&bulk->video, &single->video, width, phase, 1006, "counter");
        ++scenes;
    }
    CHECK(scenes == 2416);
    /* Short requests retain modular timestamps; long spans fall back when
     * their origin or object interval cannot be clipped without wrapping. */
    for (unsigned config = 0; config < 8; ++config) for (unsigned span = 1; span <= 16; span += 15) {
        const unsigned firsts[] = {48, 49, 1191, 1192};
        unsigned first = firsts[config % 4], end = first + span;
        prepare(bulk, config, first, true);
        prepare(single, config, first, true);
        struct CupidVideo* videos[] = {&bulk->video, &single->video};
        for (unsigned i = 0; i < 2; ++i) {
            struct CupidPixelPipeline* p = &videos[i]->pipeline;
            p->initialCycle = 31;
            p->start = config & 1 ? UINT64_MAX - (first < 100 ? 100 : 1232) : 0;
            p->objects.start = p->start;
        }
        advance(&bulk->video, end, false);
        advance(&single->video, end, true);
        compare(&bulk->video, &single->video, config, first, end, "timestamp");
        ++scenes;
    }
    CHECK(scenes == 2432);
    /* Vary every eight-bit boundary through full and clipped intervals.
     * Include equal start/end values, retained horizontal latches, both
     * vertical windows and comparator clocks beyond visible pixel 239. */
    for (unsigned boundary = 0; boundary < 256; ++boundary)
    for (unsigned clipped = 0; clipped < 2; ++clipped) {
        unsigned first = clipped ? 49 : 0;
        unsigned end = clipped ? (boundary & 1 ? 1004 : 1023) : 1232;
        prepare(bulk, (boundary % 4) * 3, first, true);
        prepare(single, (boundary % 4) * 3, first, true);
        struct CupidVideo* videos[] = {&bulk->video, &single->video};
        for (unsigned i = 0; i < 2; ++i) {
            struct CupidVideo* v = videos[i];
            v->win.h[0][0] = (uint8_t)boundary;
            v->win.h[0][1] = (uint8_t)(boundary % 4 == 0 ? boundary : boundary * 73 + 17);
            v->win.h[1][0] = (uint8_t)(boundary * 31 + 7);
            v->win.h[1][1] = boundary % 4 == 1 ? v->win.h[1][0] : (uint8_t)(boundary * 11 + 3);
            v->pipeline.windowH[0] = (boundary & 1) != 0;
            v->pipeline.windowH[1] = (boundary & 2) != 0;
            v->pipeline.windowV[0] = (boundary & 4) != 0;
            v->pipeline.windowV[1] = (boundary & 8) != 0;
        }
        advance(&bulk->video, end, false);
        advance(&single->video, end, true);
        compare(&bulk->video, &single->video, boundary, first, end, "windows");
        ++scenes;
    }
    CHECK(scenes == 2944);
    for (unsigned flags = 0; flags < 16; ++flags)
    for (unsigned clipped = 0; clipped < 2; ++clipped) {
        unsigned first = clipped ? 49 : 0;
        prepare(bulk, (flags % 4) * 3, first, (flags & 1) != 0);
        prepare(single, (flags % 4) * 3, first, (flags & 1) != 0);
        struct CupidVideo* videos[] = {&bulk->video, &single->video};
        for (unsigned i = 0; i < 2; ++i) {
            struct CupidVideo* v = videos[i];
            memset(v->win.h, 0, sizeof(v->win.h));
            v->pipeline.windowH[0] = (flags & 1) != 0;
            v->pipeline.windowH[1] = (flags & 2) != 0;
            v->pipeline.windowV[0] = (flags & 4) != 0;
            v->pipeline.windowV[1] = (flags & 8) != 0;
        }
        advance(&bulk->video, 1232, false);
        advance(&single->video, 1232, true);
        compare(&bulk->video, &single->video, flags, first, 1232, "zero-windows");
        ++scenes;
    }
    CHECK(scenes == 2976);
    free(bulk->video.vram); free(single->video.vram);
    free(bulk); free(single);
    printf("PASS 2976 forced-blank bulk/one-clock scenes, retained counters/timestamps, window boundaries and display enables; complete pipeline/frame equality; state=%016" PRIx64 "\n", stateDigest);
    return 0;
}
