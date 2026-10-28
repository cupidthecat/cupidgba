/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"
#include "mosaic-lines.inc"

static const unsigned dots[] = {0, 39, 40, 45, 46, 47, 48, 49, 50, 51, 52, 55, 56,
    71, 72, 73, 107, 108, 109, 1003, 1004, 1005, 1006};

static void tick(struct CupidGBA* gba, unsigned count, bool single) {
    CHECK(gba->cpu.cycles == 0);
    if (single) for (unsigned i = 0; i < count; ++i) cupid_timing_tick(&gba->timing, 1);
    else cupid_timing_tick(&gba->timing, (int32_t)count);
    CHECK(gba->cpu.cycles == 0);
}

static void prepare(struct CupidGBA* gba, unsigned kind, unsigned width) {
    /* This isolated renderer fixture starts at line zero. The ten warm-up
     * lines establish the display and OBJ latches before the timed write. */
    cupid_timing_clear(&gba->timing);
    cupid_video_reset(&gba->video);
    for (unsigned y = 0; y < 160; ++y) for (unsigned x = 0; x < 240; ++x)
        gba->video.vram[y * 240 + x] = x % 7 == 2 ? 0x7FFF : 0;
    if (kind) {
        uint16_t* oam = (uint16_t*)&gba->video.oam;
        uint8_t* vram = (uint8_t*)gba->video.vram;
        for (unsigned i = 0; i < 128; ++i) oam[i * 4] = 0x0200;
        gba->video.palette[0x101] = 0;
        gba->video.palette[0x102] = 0x7FFF;
        for (unsigned id = 0; id < 4; ++id) {
            oam[id * 4] = 0x3000;
            oam[id * 4 + 1] = (uint16_t)(0xC000 | id * 64);
            oam[id * 4 + 2] = (uint16_t)(id * 128);
            for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x) {
                unsigned tile = (y / 8) * 8 + x / 8;
                vram[0x10000 + id * 4096 + tile * 64 + (y % 8) * 8 + x % 8] =
                    (id * 64 + x) % 7 == 2 ? 2 : 1;
            }
        }
    }
    cupid_io_write(gba, REG_BG2CNT, 0x40);
    cupid_io_write(gba, REG_MOSAIC, (uint16_t)((width - 1) | ((width - 1) << 8)));
    cupid_io_write(gba, REG_DISPCNT, kind ? 0x1040 : 0x0403);
}

static unsigned index_of(unsigned kind, unsigned oldWidth, unsigned newWidth, unsigned position) {
    return ((kind * 16 + oldWidth - 1) * 16 + newWidth - 1) * 23 + position;
}

static void compare_line(const struct CupidVideo* video, unsigned line,
                          const struct MosaicExpected* expected,
                          unsigned kind, unsigned oldWidth, unsigned newWidth, unsigned dot) {
    const uint64_t* bits = mosaicPatterns[expected->pattern];
    for (unsigned x = 0; x < GBA_SCREEN_WIDTH; ++x) {
        bool white = ((bits[x / 64] >> (x % 64)) & 1) != 0;
        uint32_t value = color16_to_rgba(white ? 0x7FFF : 0);
        uint32_t actual = video->outputBuffer[line * GBA_SCREEN_WIDTH + x];
        if (actual != value) fprintf(stderr,
            "kind=%u old=%u new=%u dot=%u line=%u x=%u actual=%08x expected=%08x\n",
            kind, oldWidth, newWidth, dot, line, x, actual, value);
        CHECK(actual == value);
    }
}

static void scene(struct CupidGBA* gba, unsigned kind, unsigned oldWidth,
                   unsigned newWidth, unsigned position, bool single) {
    unsigned dot = dots[position];
    const struct MosaicExpected* expected = &mosaicExpected[index_of(kind, oldWidth, newWidth, position)];
    prepare(gba, kind, oldWidth);
    tick(gba, 10 * 1232 + dot, single);
    CHECK(gba->video.vcount == 10);
    cupid_video_sync(&gba->video);
    CHECK(gba->video.pipeline.bgMosaicX == expected->before);
    CHECK(gba->video.pipeline.objMosaicX == expected->before);
    cupid_io_write(gba, REG_MOSAIC, (uint16_t)((newWidth - 1) | ((newWidth - 1) << 8)));
    tick(gba, 1006 - dot, single);
    cupid_video_sync(&gba->video);
    CHECK(gba->video.pipeline.bgMosaicX == expected->after);
    CHECK(gba->video.pipeline.objMosaicX == expected->after);
    compare_line(&gba->video, 10, expected, kind, oldWidth, newWidth, dot);
    /* Retaining a shortened group's counter must not carry it into the
     * next scanline. Its complete geometry must match the stable new width. */
    tick(gba, 226, single);
    CHECK(gba->video.vcount == 11);
    CHECK(gba->video.pipeline.bgMosaicX == 0 && gba->video.pipeline.objMosaicX == 0);
    tick(gba, 1006, single);
    cupid_video_sync(&gba->video);
    const struct MosaicExpected* next = &mosaicExpected[index_of(kind, newWidth, newWidth, 0)];
    CHECK(gba->video.pipeline.bgMosaicX == next->after);
    CHECK(gba->video.pipeline.objMosaicX == next->after);
    compare_line(&gba->video, 11, next, kind, oldWidth, newWidth, dot);
}

int main(int argc, char** argv) {
    CHECK(argc == 3);
    CHECK(argv[1][0] >= '0' && argv[1][0] <= '1' && !argv[1][1]);
    unsigned kind = (unsigned)(argv[1][0] - '0');
    unsigned oldWidth = (unsigned)strtoul(argv[2], NULL, 10);
    CHECK(oldWidth >= 1 && oldWidth <= 16);
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    unsigned scenes = 0;
    for (unsigned newWidth = 1; newWidth <= 16; ++newWidth)
        for (unsigned p = 0; p < sizeof(dots) / sizeof(*dots); ++p) {
            scene(gba, kind, oldWidth, newWidth, p, false);
            scene(gba, kind, oldWidth, newWidth, p, true);
            ++scenes;
        }
    CHECK(scenes == 368);
    cupid_gba_destroy(gba);
    free(gba);
    printf("Mosaic kind=%u width=%u scenes=%u complete-lines=%u compared-pixels=%u\n",
        kind, oldWidth, scenes, scenes * 4, scenes * 4 * GBA_SCREEN_WIDTH);
    return 0;
}
