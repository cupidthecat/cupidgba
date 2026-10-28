/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"

static unsigned texel(unsigned x, unsigned y) {
    return (x + 2 * y) % 5 ? (x + 3 * y) % 7 + 1 : 0;
}

static uint16_t color(unsigned index) {
    return (uint16_t)((index & 31) | (((index * 3) & 31) << 5) | (((index * 7) & 31) << 10));
}

static struct CupidGBA* scene(uint64_t epoch, unsigned shape, unsigned size, unsigned depth,
                             unsigned mapping, unsigned budget, unsigned transform, unsigned mode) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba); cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch; gba->timing.masterCycles = (uint32_t)epoch;
    cupid_video_reset(&gba->video);
    gba->video.vcount = 225; gba->memory.io[REG_VCOUNT >> 1] = 225;
    gba->video.palette[0] = 0x03E0;
    for (unsigned i = 1; i < 256; ++i) gba->video.palette[256 + i] = color(i);
    for (unsigned i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++i) gba->video.vram[i] = 0x7C00;
    uint8_t* bytes = (uint8_t*)gba->video.vram;
    for (unsigned address = 0x14000; address < 0x18000; ++address) {
        unsigned offset = (address - 0x14000) % (depth ? 64 : 32);
        unsigned y = offset / (depth ? 8 : 4), x = offset % (depth ? 8 : 4);
        bytes[address] = depth ? (uint8_t)texel(x, y)
            : (uint8_t)(texel(x * 2, y) | (texel(x * 2 + 1, y) << 4));
    }
    for (unsigned i = 0; i < 128; ++i) gba->video.oam.obj[i].a = 0x0200;
    bool affine = transform >= 4;
    gba->video.oam.obj[0] = (struct CupidOBJ){
        .a = (uint16_t)((shape << 14) | (depth << 13) | (mode << 10)
            | (affine ? 0x0100 : 0) | (transform == 5 ? 0x0200 : 0) | 19),
        .b = (uint16_t)((size << 14) | (!affine ? transform << 12 : 0) | 17), .c = 512};
    gba->video.oam.mat[0].a = gba->video.oam.mat[0].d = 0x0100;
    gba->video.oam.mat[0].b = gba->video.oam.mat[0].c = 0;
    cupid_io_write(gba, REG_BG2PD, 0);
    cupid_io_write(gba, REG_BLDCNT, 0x2000);
    cupid_io_write(gba, REG_BLDALPHA, 0x0010);
    cupid_io_write(gba, REG_WINOUT, 0x0400);
    cupid_io_write(gba, REG_DISPCNT, (uint16_t)(0x1003 | (mapping ? 0x40 : 0)
        | (budget ? 0x20 : 0) | (mode == OBJ_MODE_OBJWIN ? 0x8400 : 0)));
    /* Establish the display and object pipelines during VBlank. */
    cupid_timing_tick(&gba->timing, 3 * VIDEO_HORIZONTAL_LENGTH);
    CHECK(gba->video.vcount == 0);
    return gba;
}

static void expected_image(const struct CupidGBA* gba, unsigned transform, unsigned mode) {
    int left = transform == 5 ? 21 : 17, top = transform == 5 ? 23 : 19;
    for (int y = 0; y < GBA_SCREEN_HEIGHT; ++y) for (int x = 0; x < GBA_SCREEN_WIDTH; ++x) {
        uint16_t expected = 0x03E0;
        int tx = x - left, ty = y - top;
        if (tx >= 0 && tx < 8 && ty >= 0 && ty < 8 && mode != OBJ_MODE_HIDDEN) {
            if (transform < 4) {
                if (transform & 1) tx = 7 - tx;
                if (transform & 2) ty = 7 - ty;
            }
            unsigned index = texel((unsigned)tx, (unsigned)ty);
            if (index) expected = mode == OBJ_MODE_OBJWIN ? 0x7C00 : color(index);
        }
        unsigned pixel = (unsigned)y * GBA_SCREEN_WIDTH + (unsigned)x;
        if (gba->video.outputBuffer[pixel] != color16_to_rgba(expected)) {
            printf("transform=%u mode=%u pixel=%u actual=%08x expected=%08x\n", transform,
                mode, pixel, gba->video.outputBuffer[pixel], color16_to_rgba(expected));
            CHECK(gba->video.outputBuffer[pixel] == color16_to_rgba(expected));
        }
    }
}

int main(int argc, char** argv) {
    CHECK(argc == 3);
    unsigned mode;
    if (!strcmp(argv[1], "normal")) mode = OBJ_MODE_NORMAL;
    else if (!strcmp(argv[1], "alpha")) mode = OBJ_MODE_SEMITRANSPARENT;
    else if (!strcmp(argv[1], "window")) mode = OBJ_MODE_OBJWIN;
    else { CHECK(!strcmp(argv[1], "hidden")); mode = OBJ_MODE_HIDDEN; }
    bool affine = !strcmp(argv[2], "affine"); CHECK(affine || !strcmp(argv[2], "normal"));
    const uint64_t epochs[] = {0, INT32_MAX - UINT64_C(8192),
        UINT32_MAX - UINT64_C(8192), UINT64_C(0x300000000)};
    unsigned scenes = 0, busClocks = 0;
    for (unsigned size = 0; size < 4; ++size) for (unsigned depth = 0; depth < 2; ++depth)
    for (unsigned mapping = 0; mapping < 2; ++mapping) for (unsigned budget = 0; budget < 2; ++budget)
    for (unsigned transform = affine ? 4 : 0; transform < (affine ? 6u : 4u); ++transform) {
        uint64_t epoch = epochs[(size + depth + mapping + budget + transform) % 4];
        struct CupidGBA* cores[] = {scene(epoch, 0, 0, depth, mapping, budget, transform, mode),
            scene(epoch, 3, size, depth, mapping, budget, transform, mode)};
        /* Compare the CPU-visible bus ownership while OBJ fetches this row.
         * The ordinary 8-by-8 scene is also checked against the full image. */
        for (unsigned i = 0; i < 2; ++i) cupid_timing_tick(&cores[i]->timing, 18 * VIDEO_HORIZONTAL_LENGTH);
        unsigned textureReads = 0;
        for (unsigned clock = 0; clock < VIDEO_HORIZONTAL_LENGTH; ++clock) {
            bool oam[2], vram[2];
            for (unsigned i = 0; i < 2; ++i) {
                cupid_timing_tick(&cores[i]->timing, 1);
                oam[i] = cupid_video_memory_busy(&cores[i]->video, GBA_BASE_OAM);
                vram[i] = cupid_video_memory_busy(&cores[i]->video, GBA_BASE_VRAM + 0x14000);
            }
            CHECK(oam[0] == oam[1] && vram[0] == vram[1]);
            textureReads += vram[0]; ++busClocks;
        }
        /* A double-size identity sprite is transparent above its centered
         * texture, but still performs the same matrix and attribute work. */
        if (transform != 5) CHECK((textureReads != 0) == (mode != OBJ_MODE_HIDDEN));
        for (unsigned i = 0; i < 2; ++i) {
            cupid_timing_tick(&cores[i]->timing, (VIDEO_VDRAW_LINES - 19) * VIDEO_HORIZONTAL_LENGTH);
            CHECK(cores[i]->video.vcount == VIDEO_VDRAW_LINES && cores[i]->video.frameCounter == 1);
            expected_image(cores[i], transform, mode);
        }
        CHECK(!memcmp(cores[0]->video.outputBuffer, cores[1]->video.outputBuffer,
            sizeof(cores[0]->video.outputBuffer)));
        for (unsigned i = 0; i < 2; ++i) { cupid_gba_destroy(cores[i]); free(cores[i]); }
        ++scenes;
    }
    CHECK(scenes == (affine ? 64u : 128u));
    CHECK(busClocks == scenes * VIDEO_HORIZONTAL_LENGTH);
    printf("%s %s: %u scene pairs; %u bus clocks; %u complete pixels\n", argv[1], argv[2],
        scenes, busClocks, scenes * 2 * GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT);
    return 0;
}
