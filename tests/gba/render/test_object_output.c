/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_object_output.c
 * @brief Final texture requests, pixel submission and complete guest output.
 */
#include "../../../src/gba/render/pipeline.h"
#include "../../test.h"
#include <string.h>

static void final_request(void) {
    const uint64_t epochs[] = {0, INT32_MAX - 1000u, UINT32_MAX - 1000u, UINT64_C(0x300000000)};
    const unsigned modes[] = {0, 3, 4, 5};
    struct CupidVideo *v = calloc(1, sizeof(*v));
    CHECK(v);
    v->vram = calloc(1, GBA_SIZE_VRAM);
    CHECK(v->vram);
    unsigned cases = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned mode = 0; mode < 4; mode++)
            for (unsigned color = 0; color < 2; color++)
                for (unsigned affine = 0; affine < 2; affine++)
                    for (unsigned kind = 0; kind < 3; kind++)
                        for (unsigned mosaic = 0; mosaic < 2; mosaic++)
                            for (unsigned flip = 0; flip < 2; flip++)
                                for (unsigned opaque = 0; opaque < 2; opaque++)
                                    for (unsigned freeHblank = 0; freeHblank < 2; freeHblank++)
                                        for (unsigned changed = 0; changed < 2; changed++) {
                                            memset(&v->pipeline, 0, sizeof(v->pipeline));
                                            memset(v->vram, opaque ? 0x11 : 0, GBA_SIZE_VRAM);
                                            v->dispcnt =
                                                modes[mode] | 0x1000 | (freeHblank ? 0x20 : 0);
                                            struct CupidObjectFetch *f = &v->pipeline.objects;
                                            f->active = f->drawing = true;
                                            f->start = epochs[epoch];
                                            f->limit = freeHblank ? 964 : 1232;
                                            f->index = 128;
                                            f->step = 6;
                                            unsigned last = freeHblank ? 958 : 1230;
                                            f->cycle = last - 2;
                                            struct CupidObjectDraw *d = &f->draw[0];
                                            *d = (struct CupidObjectDraw){
                                                .width = 8,
                                                .height = 8,
                                                .mode = kind,
                                                .x = 20,
                                                .remaining = affine ? 2 : 4,
                                                .matrix = {256, 0, 0, 256},
                                                .tile = modes[mode] < 3 ? 0 : 512,
                                                .priority = 1,
                                                .affine = affine,
                                                .mosaic = mosaic && kind != OBJ_MODE_OBJWIN,
                                                .fullPalette = color,
                                                .flip = flip};
                                            for (unsigned x = 0; x < 240; x++)
                                                f->pixels[1][x] = (struct CupidObjectPixel){
                                                    .priority = 3, .mosaic = !d->mosaic};
                                            struct CupidObjectPixel initial[240];
                                            memcpy(initial, f->pixels[1], sizeof(initial));
                                            cupid_pixel_object(v, epochs[epoch] + last - 2);
                                            CHECK(v->pipeline.objectAccess ==
                                                  epochs[epoch] + last - 2);
                                            CHECK(memcmp(initial, f->pixels[1], sizeof(initial)) !=
                                                  0);
                                            CHECK(d->x == 20 + (affine ? 1 : 2));
                                            CHECK(f->drawing);
                                            struct CupidObjectPixel before[240];
                                            memcpy(before, f->pixels[1], sizeof(before));
                                            cupid_pixel_object(v, epochs[epoch] + last - 1);
                                            CHECK(memcmp(before, f->pixels[1], sizeof(before)) ==
                                                  0);
                                            if (changed)
                                                v->dispcnt ^= 0x20;
                                            cupid_pixel_object(v, epochs[epoch] + last);
                                            CHECK(v->pipeline.objectAccess == epochs[epoch] + last);
                                            CHECK(v->pipeline.oamAccess == 0);
                                            CHECK(memcmp(before, f->pixels[1], sizeof(before)) ==
                                                  0);
                                            CHECK(d->x == 20 + (affine ? 2 : 4));
                                            CHECK(d->remaining == 0 && !f->drawing);
                                            CHECK(f->limit == (freeHblank ? 964u : 1232u));
                                            cupid_pixel_object(v, epochs[epoch] + last + 2);
                                            CHECK(v->pipeline.objectAccess == epochs[epoch] + last);
                                            CHECK(memcmp(before, f->pixels[1], sizeof(before)) ==
                                                  0);
                                            cases++;
                                        }
    free(v->vram);
    free(v);
    printf("object final-read/output cases: %u cases\n", cases);
}

/* These guest scenes expose the last object's tail beyond the overlapping
 * earlier objects. The program enables the prepared display and writes a
 * completion marker; three frame returns allow a full stable image. */
static void guest_frames(void) {
    static const struct {
        unsigned count, pixels;
        bool affine, freeHblank;
    } scenes[] = {
        {15, 128, false, false}, {15, 122, false, true}, {19, 128, false, false},
        {20, 74, false, false},  {7, 128, true, false},  {7, 122, true, true},
        {8, 128, true, false},   {8, 64, true, true},
    };

    unsigned cases = 0;
    for (unsigned scene = 0; scene < sizeof(scenes) / sizeof(*scenes); ++scene) {
        for (unsigned color = 0; color < 2; ++color) {
            struct CupidGBA *g = calloc(1, sizeof(*g));
            CHECK(g);
            cupid_gba_create(g);
            g->memory.rom = calloc(1, 512);
            CHECK(g->memory.rom);
            g->memory.romSize = g->memory.romImageSize = 512;
            g->memory.romMask = 511;
            const uint32_t entry = 0xEA00002E; /* B 080000C0h */
            memcpy(g->memory.rom, &entry, sizeof(entry));
            const uint32_t program[] = {
                0xE59F0014, 0xE59F1014, 0xE1C010B0, /* Display control write. */
                0xE59F2010, 0xE59F3010, 0xE5823000, /* Completion marker. */
                0xEAFFFFFE, 0x04000000, 0x1443 | (scenes[scene].freeHblank ? 0x20 : 0),
                0x02000000, 0x600D600D,
            };
            memcpy(g->memory.rom + 0xC0, program, sizeof(program));
            cupid_gba_reset(g);
            struct CupidVideo *v = &g->video;
            for (unsigned pixel = 0; pixel < 240 * 160; ++pixel)
                v->vram[pixel] = 0x4000;
            memset((uint8_t *)v->vram + 0x14000, 0x11, 4096);
            v->palette[color ? 0x111 : 0x101] = 16;
            for (unsigned index = 0; index < 128; ++index)
                v->oam.raw[index * 4] = 0x0200;
            for (unsigned index = 0; index < scenes[scene].count; ++index) {
                v->oam.raw[index * 4] =
                    (uint16_t)(8 | (scenes[scene].affine ? 0x100 : 0) | (color ? 0x2000 : 0));
                v->oam.raw[index * 4 + 1] =
                    (uint16_t)(0xC000 | (index + 1 == scenes[scene].count ? 64 : 0));
                v->oam.raw[index * 4 + 2] = 512;
            }
            v->oam.raw[3] = v->oam.raw[15] = 0x100;
            cupid_io_write(g, REG_BG2CNT, 1);
            cupid_io_write(g, REG_BG2PA, 0x100);
            cupid_io_write(g, REG_BG2PD, 0x100);
            for (unsigned frame = 0; frame < 3; ++frame)
                cupid_gba_run_frame(g);
            CHECK(cupid_memory_load32(&g->cpu, 0x02000000, NULL) == 0x600D600D);
            for (unsigned y = 0; y < 160; ++y) {
                for (unsigned x = 0; x < 240; ++x) {
                    const bool object = y >= 8 && y < 72 && x < scenes[scene].pixels;
                    const uint32_t pixel = v->outputBuffer[y * 240 + x];
                    CHECK(((pixel & 255) >> 3) == (object ? 16u : 0u));
                    CHECK(((pixel >> 8) & 255) == 0);
                    CHECK((((pixel >> 16) & 255) >> 3) == (object ? 0u : 16u));
                    CHECK((pixel >> 24) == 255);
                }
            }
            cupid_gba_destroy(g);
            free(g);
            ++cases;
        }
    }
    CHECK(cases == 16);
    printf("object guest frames: %u scenes, 614400 pixels\n", cases);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "ports"))
        final_request();
    else if (!strcmp(argv[1], "frames"))
        guest_frames();
    else
        return 2;
    return 0;
}
