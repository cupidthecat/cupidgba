/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"
#include <inttypes.h>
#include "mosaic-vertical-rows.inc"

static const unsigned dots[] = {1231, 1232, 1233};
static unsigned runs;
static uint64_t comparedLines;

/* Eight thirty-pixel stripes encode each texture row with a unique Gray code.
 * A displaced row changes complete stripe geometry, including opaque black. */
static unsigned stripe(unsigned x, unsigned y) {
    return ((y ^ (y >> 1)) >> (x / 30)) & 1;
}

static void half(void* bytes, unsigned address, uint16_t value) {
    memcpy((uint8_t*)bytes + address, &value, 2);
}

static void populate(struct CupidVideo* video, unsigned kind) {
    uint8_t* vram = (uint8_t*)video->vram;
    uint8_t* palette = (uint8_t*)video->palette;
    uint8_t* oam = (uint8_t*)&video->oam;
    half(palette, 0, 0x7C00);
    half(palette, 2, 0);
    half(palette, 4, 0x7FFF);
    half(palette, 0x202, 0);
    half(palette, 0x204, 0x7FFF);
    for (unsigned i = 0; i < 128; ++i) half(oam, i * 8, 0x0200);
    if (kind == 0) {
        for (unsigned y = 0; y < 160; ++y) for (unsigned x = 0; x < 256; ++x) {
            unsigned tile = (y / 8) * 32 + x / 8;
            vram[tile * 64 + (y % 8) * 8 + x % 8] = (uint8_t)(1 + (x < 240 ? stripe(x, y) : 0));
        }
        for (unsigned y = 0; y < 32; ++y) for (unsigned x = 0; x < 32; ++x)
            half(vram, 0xF800 + y * 64 + x * 2, (uint16_t)(y * 32 + x));
    } else if (kind == 1) {
        for (unsigned y = 0; y < 160; ++y) for (unsigned x = 0; x < 240; ++x)
            half(vram, (y * 240 + x) * 2, stripe(x, y) ? 0x7FFF : 0);
    } else {
        for (unsigned id = 0; id < 4; ++id) {
            half(oam, id * 8, (uint16_t)(0x3000 | (kind == 3 ? 0x100 : 0)));
            half(oam, id * 8 + 2, (uint16_t)(0xC000 | id * 64));
            half(oam, id * 8 + 4, (uint16_t)(id * 128));
            for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 64; ++x) {
                unsigned tile = (y / 8) * 8 + x / 8;
                unsigned gx = id * 64 + x;
                vram[0x10000 + id * 4096 + tile * 64 + (y % 8) * 8 + x % 8] =
                    (uint8_t)(1 + (gx < 240 ? stripe(gx, y) : 0));
            }
        }
        half(oam, 6, 0x100);
        half(oam, 14, 0);
        half(oam, 22, 0);
        half(oam, 30, 0x100);
    }
}

static void advance(struct CupidGBA* gba, unsigned clocks, bool single) {
    CHECK(gba->cpu.cycles == 0);
    if (single) for (unsigned i = 0; i < clocks; ++i) cupid_timing_tick(&gba->timing, 1);
    else cupid_timing_tick(&gba->timing, (int32_t)clocks);
    CHECK(gba->cpu.cycles == 0);
}

static void scene(struct CupidGBA* gba, unsigned kind, unsigned phase,
                  unsigned width, unsigned height, unsigned dotIndex,
                  bool single, bool shortened, unsigned epochKind) {
    unsigned dot = dots[dotIndex];
    unsigned target = (16 + phase) * 1232 + dot;
    unsigned lines = 36 + phase;
    unsigned index = (((kind * 16 + phase) * 3 + width) * 16 + height - 1) * 3 + dotIndex;
    CHECK(index < sizeof(verticalExpected) / sizeof(*verticalExpected));
    const struct VerticalExpected* expected = &verticalExpected[index];
    CHECK(expected->pattern < sizeof(verticalRows) / sizeof(*verticalRows));
    const uint8_t* rows = verticalRows[expected->pattern];
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    uint64_t origin = 0;
    if (epochKind) {
        uint64_t edge = epochKind == 1 ? INT32_MAX : UINT32_MAX;
        origin = edge - (228 * 1232 + target);
    }
    gba->timing.masterCycles = (uint32_t)origin;
    gba->timing.globalCycles = origin;
    cupid_video_reset(&gba->video);
    cupid_io_write(gba, REG_DISPCNT, 0x80);
    /* Configure during VBlank, then let ordinary line-zero callbacks prepare
     * both fetch engines. This fixture does not select a power-on LCD phase. */
    advance(gba, 225 * 1232, false);
    CHECK(gba->video.vcount == 225);
    populate(&gba->video, kind);
    cupid_io_write(gba, REG_BG2CNT, kind == 0 ? 0x1FC0 : 0x40);
    cupid_io_write(gba, REG_BG2PA, 0x100); cupid_io_write(gba, REG_BG2PB, 0);
    cupid_io_write(gba, REG_BG2PC, 0); cupid_io_write(gba, REG_BG2PD, 0x100);
    cupid_io_write(gba, REG_BG2X_LO, 0); cupid_io_write(gba, REG_BG2X_HI, 0);
    cupid_io_write(gba, REG_BG2Y_LO, 0); cupid_io_write(gba, REG_BG2Y_HI, 0);
    cupid_io_write(gba, REG_MOSAIC, 0xF0F0);
    unsigned display = kind == 0 ? 0x400 : kind == 1 ? 0x403 : 0x1040;
    if (shortened) display |= 0x20;
    cupid_io_write(gba, REG_DISPCNT, (uint16_t)display);
    advance(gba, 3 * 1232, false);
    CHECK(gba->video.vcount == 0);
    uint64_t visible = cupid_timing_global_time(&gba->timing);
    CHECK(visible == origin + 228 * 1232);
    advance(gba, target, single);
    CHECK(cupid_timing_global_time(&gba->timing) == visible + target);
    unsigned value = (height - 1) << (kind < 2 ? 4 : 12);
    if (width == 0) cupid_io_write(gba, REG_MOSAIC, (uint16_t)value);
    else if (width == 1)
        cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_MOSAIC + (kind < 2 ? 0 : 1), (int8_t)((height - 1) << 4), NULL);
    else cupid_memory_store32(&gba->cpu, GBA_BASE_IO + REG_MOSAIC, (int32_t)(0xBEEF0000 | value), NULL);
    CHECK(gba->video.mosaic == expected->control);
    advance(gba, lines * 1232 + 2 - target, single);
    cupid_video_sync(&gba->video);
    CHECK(cupid_timing_global_time(&gba->timing) == visible + lines * 1232 + 2);
    CHECK(gba->video.vcount == (int)lines);
    for (unsigned y = 0; y < lines; ++y) for (unsigned x = 0; x < GBA_SCREEN_WIDTH; ++x) {
        bool white = (rows[y] >> (x / 30)) & 1;
        uint32_t color = color16_to_rgba(white ? 0x7FFF : 0);
        uint32_t actual = gba->video.outputBuffer[y * GBA_SCREEN_WIDTH + x];
        if (actual != color) fprintf(stderr,
            "kind=%u phase=%u width=%u height=%u dot=%u epoch=%u short=%u single=%u y=%u x=%u actual=%08x expected=%08x\n",
            kind, phase, width, height, dot, epochKind, shortened, single, y, x, actual, color);
        CHECK(actual == color);
    }
    CHECK(gba->video.pipeline.bgMosaicY == expected->bg);
    CHECK(gba->video.pipeline.objMosaicY == expected->obj);
    comparedLines += lines;
    ++runs;
}

int main(int argc, char** argv) {
    CHECK(argc == 3);
    char* end;
    unsigned kind = (unsigned)strtoul(argv[1], &end, 10);
    CHECK(*argv[1] && !*end && kind < 4);
    unsigned phase = (unsigned)strtoul(argv[2], &end, 10);
    CHECK(*argv[2] && !*end && phase < 16);
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    for (unsigned width = 0; width < 3; ++width)
    for (unsigned height = 1; height <= 16; ++height)
    for (unsigned dot = 0; dot < 3; ++dot)
    for (unsigned single = 0; single < 2; ++single) {
        scene(gba, kind, phase, width, height, dot, single != 0, false, 0);
        /* Constant-height OBJ controls keep their screen-row grouping when
         * HBlank access shortens fetching. The line counter is independent. */
        if (kind >= 2 && height == 16)
            scene(gba, kind, phase, width, height, dot, single != 0, true, 0);
        if (height == 1 && dot == 1) {
            scene(gba, kind, phase, width, height, dot, single != 0, false, 1);
            scene(gba, kind, phase, width, height, dot, single != 0, false, 2);
        }
    }
    CHECK(runs == (kind < 2 ? 300U : 318U));
    cupid_gba_destroy(gba);
    free(gba);
    printf("Vertical mosaic kind=%u phase=%u runs=%u complete-lines=%" PRIu64 " compared-pixels=%" PRIu64 "\n",
        kind, phase, runs, comparedLines, comparedLines * GBA_SCREEN_WIDTH);
    return 0;
}
