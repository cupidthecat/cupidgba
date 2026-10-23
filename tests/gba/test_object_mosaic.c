/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"

#define FRAME_CLOCKS (VIDEO_HORIZONTAL_LENGTH * VIDEO_VERTICAL_TOTAL)

static unsigned texel(unsigned x, unsigned y) {
    return 1 + (x * 3 + y * 5) % 15;
}

static struct CupidGBA* scene(bool freeBlank, bool affine, bool fullPalette,
                             unsigned width, unsigned height, unsigned x, unsigned y, uint64_t epoch) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    cupid_video_reset(&gba->video);
    for (unsigned i = 0; i < 128; ++i) gba->video.oam.obj[i].a = 0x0200;
    gba->video.oam.obj[0] = (struct CupidOBJ){
        .a = (uint16_t)(y | 0x1000 | (affine ? 0x0100 : 0) | (fullPalette ? 0x2000 : 0)),
        .b = (uint16_t)(x | 0xC000), .c = 512};
    gba->video.oam.raw[3] = 0x0100;
    gba->video.oam.raw[15] = 0x0100;
    for (unsigned i = 0; i < 16; ++i) gba->video.palette[256 + i] = (uint16_t)(i * 0x0421);
    for (unsigned ty = 0; ty < 64; ++ty) {
        for (unsigned tx = 0; tx < 64; ++tx) {
            unsigned tile = (ty / 8) * 8 + tx / 8;
            unsigned address = 0x14000 + tile * (fullPalette ? 64 : 32)
                + (ty % 8) * (fullPalette ? 8 : 4) + (fullPalette ? tx % 8 : (tx % 8) / 2);
            unsigned shift = (address & 1) * 8 + (fullPalette ? 0 : (tx & 1) * 4);
            gba->video.vram[address / 2] |= (uint16_t)(texel(tx, ty) << shift);
        }
    }
    cupid_io_write(gba, REG_MOSAIC, (uint16_t)(((height - 1) << 12) | ((width - 1) << 8)));
    cupid_io_write(gba, REG_DISPCNT, (uint16_t)(0x1043 | (freeBlank ? 0x20 : 0)));
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void require_pixels(const struct CupidGBA* gba, unsigned width, unsigned height,
                           unsigned left, unsigned top) {
    for (unsigned y = 0; y < GBA_SCREEN_HEIGHT; ++y) {
        for (unsigned x = 0; x < GBA_SCREEN_WIDTH; ++x) {
            unsigned color = 0;
            if (x >= left && x < left + 64 && y >= top && y < top + 64) {
                unsigned mx = x - x % width;
                unsigned my = y - y % height;
                unsigned tx = mx < left ? 0 : mx - left;
                unsigned ty = my < top ? 0 : my - top;
                color = texel(tx, ty) * 0x0421;
            }
            uint32_t actual = gba->video.outputBuffer[y * GBA_SCREEN_WIDTH + x];
            uint32_t expected = color16_to_rgba((uint16_t)color);
            if (actual != expected) {
                fprintf(stderr, "OBJ mosaic %ux%u at (%u,%u), pixel (%u,%u): %08" PRIx32 " != %08" PRIx32 "\n",
                    width, height, left, top, x, y, actual, expected);
            }
            CHECK(actual == expected);
        }
    }
}

static void complete_pixels(bool affine) {
    unsigned observations = 0;
    for (unsigned height = 1; height <= 16; ++height) {
        unsigned width = 1 + (height * 7) % 16;
        for (unsigned full = 0; full < 2; ++full) {
            for (unsigned freeBlank = 0; freeBlank < 2; ++freeBlank) {
                struct CupidGBA* gba = scene(freeBlank != 0, affine, full != 0, width, height, 13, 19, 0);
                /* A complete frame settles the existing reset/display phases. */
                cupid_timing_tick(&gba->timing, 2 * FRAME_CLOCKS);
                require_pixels(gba, width, height, 13, 19);
                observations += GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT;
                destroy(gba);
            }
        }
    }
    printf("PASS %s: %u complete screen pixels, all 16 vertical sizes, both color depths and fetch budgets\n",
        affine ? "affine OBJ mosaic" : "normal OBJ mosaic", observations);
}

static void counters_outlive_fetches_and_reset_at_frame_end(void) {
    struct CupidGBA* gba = scene(true, false, true, 1, 6, 0, 0, 0);
    /* Line 0 has latched phase 1 for the sprite engine preparing line 1. */
    cupid_timing_tick(&gba->timing, FRAME_CLOCKS + 41);
    CHECK(gba->video.pipeline.objMosaicY == 1);
    CHECK(gba->video.pipeline.objects.mosaicY == 1);
    uint64_t start = gba->video.pipeline.objects.start;
    CHECK(gba->video.pipeline.objects.limit == 964);
    cupid_timing_tick(&gba->timing, 964);
    cupid_video_sync(&gba->video);
    uint64_t oam = gba->video.pipeline.oamAccess;
    uint64_t vram = gba->video.pipeline.objectAccess;
    CHECK(oam < start + 964 && vram < start + 964);
    cupid_timing_tick(&gba->timing, 1192 - 964 - 1);
    cupid_video_sync(&gba->video);
    CHECK(gba->video.pipeline.objMosaicY == 1);
    cupid_timing_tick(&gba->timing, 1);
    cupid_video_sync(&gba->video);
    CHECK(gba->video.pipeline.objMosaicY == 2);
    CHECK(gba->video.pipeline.oamAccess == oam);
    CHECK(gba->video.pipeline.objectAccess == vram);
    /* A changed size retains the existing counter until equality or wrap. */
    cupid_io_write(gba, REG_MOSAIC, 1 << 12);
    for (unsigned expected = 3; expected <= 15; ++expected) {
        cupid_timing_tick(&gba->timing, 1232);
        cupid_video_sync(&gba->video);
        CHECK(gba->video.pipeline.objMosaicY == expected);
    }
    cupid_timing_tick(&gba->timing, 1232);
    cupid_video_sync(&gba->video);
    CHECK(gba->video.pipeline.objMosaicY == 0);
    cupid_io_write(gba, REG_MOSAIC, 5 << 12);
    uint64_t now = cupid_timing_global_time(&gba->timing);
    uint64_t end = FRAME_CLOCKS + 159 * 1232 + 1;
    CHECK(now < end);
    cupid_timing_tick(&gba->timing, (uint32_t)(end - now));
    cupid_video_sync(&gba->video);
    CHECK(gba->video.pipeline.objMosaicY == 0);
    cupid_timing_tick(&gba->timing, FRAME_CLOCKS - 159 * 1232);
    cupid_video_sync(&gba->video);
    CHECK(gba->video.vcount == 0);
    CHECK(gba->video.pipeline.objects.mosaicY == 0);
    CHECK(gba->video.pipeline.objMosaicY == 1);
    destroy(gba);
}

static void budget_changes_retain_phase_and_clock_wrap(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - 1000u, UINT64_C(0x300000000)};
    for (unsigned c = 0; c < 3; ++c) {
        struct CupidGBA* machines[] = {scene(false, true, true, 4, 6, 0, 0, clocks[c]),
                                      scene(true, true, true, 4, 6, 0, 0, clocks[c])};
        for (unsigned line = 0; line < 30; ++line) {
            unsigned control = (line / 3) & 1;
            for (unsigned i = 0; i < 2; ++i) {
                cupid_io_write(machines[i], REG_DISPCNT, (uint16_t)(0x1043 | ((control ^ i) ? 0x20 : 0)));
                if (i == 0) cupid_timing_tick(&machines[i]->timing, 1232);
                else for (unsigned tick = 0; tick < 1232; ++tick) cupid_timing_tick(&machines[i]->timing, 1);
                cupid_video_sync(&machines[i]->video);
            }
            CHECK(machines[0]->video.pipeline.objMosaicY == machines[1]->video.pipeline.objMosaicY);
            CHECK(machines[0]->video.pipeline.objects.mosaicY == machines[1]->video.pipeline.objects.mosaicY);
            CHECK(!memcmp(machines[0]->video.outputBuffer, machines[1]->video.outputBuffer, sizeof(machines[0]->video.outputBuffer)));
            CHECK(machines[0]->video.event.when == machines[1]->video.event.when);
        }
        destroy(machines[0]);
        destroy(machines[1]);
    }
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "normal")) complete_pixels(false);
    else if (!strcmp(argv[1], "affine")) complete_pixels(true);
    else if (!strcmp(argv[1], "phase")) RUN(counters_outlive_fetches_and_reset_at_frame_end);
    else if (!strcmp(argv[1], "clock")) RUN(budget_changes_retain_phase_and_clock_wrap);
    else CHECK(false);
    return 0;
}
