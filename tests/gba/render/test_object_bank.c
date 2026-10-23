/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"
#include <inttypes.h>
#include "object-bank-rows.inc"

static uint64_t scenes, pixels;

static uint8_t texture(unsigned address) {
    uint32_t hash = address + 0x91E10DA5u;
    hash ^= hash >> 16;
    hash *= 0x7FEB352Du;
    hash ^= hash >> 15;
    hash *= 0x846CA68Bu;
    hash ^= hash >> 16;
    static const uint8_t values[] = {0x00, 0x12, 0x21, 0x01, 0x10};
    return values[hash % 5];
}

static void half(void* bytes, unsigned address, uint16_t value) {
    memcpy((uint8_t*)bytes + address, &value, 2);
}

static void advance(struct CupidGBA* gba, unsigned clocks, bool single) {
    CHECK(gba->cpu.cycles == 0);
    if (single) for (unsigned i = 0; i < clocks; ++i) cupid_timing_tick(&gba->timing, 1);
    else cupid_timing_tick(&gba->timing, (int32_t)clocks);
    CHECK(gba->cpu.cycles == 0);
}

static void scene(struct CupidGBA* gba, unsigned mode, bool affine,
                  unsigned transform, unsigned full, unsigned mapping,
                  unsigned base, unsigned origin, unsigned epoch, bool single) {
    static const unsigned tiles[] = {0, 1022, 1023};
    static const int positions[][2] = {{0, 0}, {13, 5}, {-3, -3}, {235, 155}, {0, 253}};
    int sx = positions[origin][0], sy = positions[origin][1];
    unsigned tile = tiles[base];
    bool doubled = affine && transform != 0;
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    uint64_t start = 0;
    if (epoch) start = (epoch == 1 ? (uint64_t)INT32_MAX : UINT32_MAX) - (228 * 1232 + 4 * 1232 + 44);
    gba->timing.masterCycles = (uint32_t)start;
    gba->timing.globalCycles = start;
    cupid_video_reset(&gba->video);
    cupid_io_write(gba, REG_DISPCNT, 0x80);
    advance(gba, 225 * 1232, false);
    CHECK(gba->video.vcount == 225);
    uint8_t* vram = (uint8_t*)gba->video.vram;
    uint8_t* palette = (uint8_t*)gba->video.palette;
    uint8_t* oam = (uint8_t*)&gba->video.oam;
    memset(vram, 0, GBA_SIZE_VRAM);
    memset(palette, 0, 0x400);
    memset(oam, 0, 0x400);
    half(palette, 0, 0x001F);
    for (unsigned i = 1; i < 256; ++i) half(palette, 0x200 + i * 2, (i & 1) ? 0 : 0x7FFF);
    for (unsigned i = 0; i < 128; ++i) half(oam, i * 8, 0x0200);
    for (unsigned i = 0; i < 0x8000; ++i) vram[0x10000 + i] = texture(i);
    unsigned a = ((unsigned)sy & 255) | (full << 13);
    unsigned b = (unsigned)sx & 511;
    if (affine) {
        a |= 0x100 | (doubled ? 0x200 : 0);
        b |= 31 << 9;
        half(oam, 31 * 32 + 6, 0x100);
        half(oam, 31 * 32 + 14, 0);
        half(oam, 31 * 32 + 22, 0);
        half(oam, 31 * 32 + 30, 0x100);
    } else b |= transform << 12;
    half(oam, 0, (uint16_t)a);
    half(oam, 2, (uint16_t)b);
    half(oam, 4, (uint16_t)(tile | (5U << 12)));
    cupid_io_write(gba, REG_DISPCNT, (uint16_t)(mode | 0x1000 | (mapping << 6)));
    advance(gba, 3 * 1232, false);
    CHECK(gba->video.vcount == 0);
    uint64_t visible = cupid_timing_global_time(&gba->timing);
    CHECK(visible == start + 228 * 1232);
    advance(gba, 160 * 1232 + 2, single);
    cupid_video_sync(&gba->video);
    CHECK(gba->video.vcount == 160);
    CHECK(cupid_timing_global_time(&gba->timing) == visible + 160 * 1232 + 2);
    for (unsigned y = 0; y < GBA_SCREEN_HEIGHT; ++y) for (unsigned x = 0; x < GBA_SCREEN_WIDTH; ++x) {
        int localX = (int)x - sx;
        unsigned localY = (y - (unsigned)sy) & 255;
        unsigned kind = 0;
        int tx = localX - (doubled ? 4 : 0);
        int ty = (int)localY - (doubled ? 4 : 0);
        if (tx >= 0 && tx < 8 && ty >= 0 && ty < 8) {
            if (!affine) {
                if (transform & 1) tx = 7 - tx;
                if (transform & 2) ty = 7 - ty;
            }
            kind = (bankRows[full][mapping][base][ty] >> (tx * 2)) & 3;
            /* Bitmap modes keep the lower OBJ bank unavailable after wrap. */
            if (mode >= 3 && (tile == 0 || (full && mapping && tile == 1023 && ty >= 4))) kind = 0;
        }
        CHECK(kind <= 2);
        uint32_t color = color16_to_rgba(kind == 0 ? 0x001F : kind == 1 ? 0 : 0x7FFF);
        uint32_t actual = gba->video.outputBuffer[y * GBA_SCREEN_WIDTH + x];
        if (actual != color) fprintf(stderr,
            "mode=%u affine=%u transform=%u full=%u mapping=%u tile=%u origin=%u epoch=%u single=%u x=%u y=%u actual=%08x expected=%08x\n",
            mode, (unsigned)affine, transform, full, mapping, tile, origin, epoch, (unsigned)single, x, y, actual, color);
        CHECK(actual == color);
        ++pixels;
    }
    ++scenes;
}

int main(int argc, char** argv) {
    CHECK(argc == 3);
    char* end;
    unsigned mode = (unsigned)strtoul(argv[1], &end, 10);
    CHECK(*argv[1] && !*end && mode < 8);
    bool affine = !strcmp(argv[2], "affine");
    CHECK(affine || !strcmp(argv[2], "normal"));
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    for (unsigned full = 0; full < 2; ++full)
    for (unsigned mapping = 0; mapping < 2; ++mapping)
    for (unsigned base = 0; base < 3; ++base)
    for (unsigned transform = 0; transform < (affine ? 2U : 4U); ++transform)
    for (unsigned origin = 0; origin < 5; ++origin) {
        scene(gba, mode, affine, transform, full, mapping, base, origin, 0, false);
        if (full && mapping && base == 2 && transform == 0 && origin == 0)
            for (unsigned epoch = 0; epoch < 3; ++epoch)
                scene(gba, mode, affine, transform, full, mapping, base, origin, epoch, true);
    }
    CHECK(scenes == (affine ? 123U : 243U));
    CHECK(pixels == scenes * GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT);
    cupid_gba_destroy(gba);
    free(gba);
    printf("Object bank mode=%u affine=%u scenes=%" PRIu64 " complete-pixels=%" PRIu64 "\n", mode, (unsigned)affine, scenes, pixels);
    return 0;
}
