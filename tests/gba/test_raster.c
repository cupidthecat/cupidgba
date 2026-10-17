/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"
#include "raster-frames.inc"

static const unsigned dots[] = {0, 1, 39, 40, 41, 45, 47, 48, 49, 50, 51, 52, 55, 56, 1006, 1007, 1231, 1232};
static const uint16_t offsets[] = {REG_BG2X_LO, REG_BG2Y_LO, REG_BG2PA, REG_BG2PC, REG_BG2PB, REG_BG2PD,
    REG_DISPCNT, REG_DISPCNT, REG_DISPCNT, REG_DISPCNT, REG_DISPCNT};
static const uint16_t values[] = {0x100, 0x100, 0x200, 0x100, 0x100, 0x200, 3, 0x483, 0x404, 0x403, 0x403};
enum { PIXELS = GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT, SCENES = 11 * 18 };

static void tick(struct CupidGBA* gba, unsigned clocks, bool single) {
    CHECK(!gba->cpu.cycles);
    if (single) for (unsigned i = 0; i < clocks; ++i) cupid_timing_tick(&gba->timing, 1);
    else cupid_timing_tick(&gba->timing, (int32_t)clocks);
}

static uint64_t snapshot(const struct CupidVideo* video, uint8_t pixels[PIXELS]) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < PIXELS; ++i) {
        uint32_t pixel = video->outputBuffer[i];
        unsigned r = pixel & 255, g = (pixel >> 8) & 255, b = (pixel >> 16) & 255;
        /* These owned textures use only black and white endpoints. Compare
         * geometry independently of five/eight-bit channel expansion. */
        CHECK(r == g && g == b && (r == 0 || r == 248 || r == 255));
        pixels[i] = r != 0;
        hash = (hash ^ pixels[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

static void frame(unsigned pattern, unsigned scene, bool single, uint8_t pixels[PIXELS]) {
    unsigned operation = scene / 18, dot = dots[scene % 18];
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_io_write(gba, REG_BG2PA, 0x100);
    cupid_io_write(gba, REG_BG2PD, 0x100);
    for (unsigned i = 0; i < 256; ++i)
        cupid_memory_store16(&gba->cpu, GBA_BASE_PALETTE + i * 2, i ? 0x7FFF : 0, NULL);
    for (unsigned y = 0; y < 160; ++y) for (unsigned x = 0; x < 240; ++x) {
        bool white = pattern ? (x * 3 + y * 2) % 7 < 3 : ((x + y) & 1) != 0;
        cupid_memory_store16(&gba->cpu, GBA_BASE_VRAM + (y * 240 + x) * 2, white ? 0x7FFF : 0, NULL);
    }
    cupid_io_write(gba, REG_DISPCNT, operation == 9 ? 0x483 : operation == 10 ? 3 : 0x403);
    unsigned guard = 0;
    while (gba->video.vcount != 0 && guard++ < 280896) tick(gba, 1, single);
    CHECK(gba->video.vcount == 0);
    tick(gba, 280896, single);
    CHECK(gba->video.vcount == 0);
    tick(gba, 10 * 1232 + dot, single);
    cupid_io_write(gba, offsets[operation], values[operation]);
    unsigned before = gba->video.frameCounter;
    /* Observe the completed frame at the next line zero, before its pixels. */
    tick(gba, 218 * 1232 - dot + 1, single);
    CHECK(gba->video.frameCounter == before + 1);
    uint64_t hash = snapshot(&gba->video, pixels);
    CHECK(hash == rasterFrames[pattern][scene]);
    cupid_gba_destroy(gba);
    free(gba);
}

int main(int argc, char** argv) {
    CHECK(argc == 2 && argv[1][0] >= '0' && argv[1][0] <= '7' && !argv[1][1]);
    unsigned group = (unsigned)(argv[1][0] - '0'), pattern = group / 4, shard = group % 4;
    uint8_t* bulk = malloc(PIXELS);
    uint8_t* single = malloc(PIXELS);
    CHECK(bulk && single);
    unsigned count = 0;
    for (unsigned scene = shard; scene < SCENES; scene += 4) {
        frame(pattern, scene, false, bulk);
        frame(pattern, scene, true, single);
        CHECK(!memcmp(bulk, single, PIXELS));
        ++count;
    }
    CHECK(count == (shard < 2 ? 50 : 49));
    free(bulk); free(single);
    printf("Raster scenes: %u; complete frames: %u; compared pixels: %u\n", count, count * 2, count * 2 * PIXELS);
    return 0;
}
