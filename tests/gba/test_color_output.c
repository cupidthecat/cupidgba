/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"

/* Explicit host encodings keep this oracle independent of the converter. */
static uint32_t expected_color(uint16_t color) {
    static const uint8_t component[32] = {
        0, 8, 16, 24, 33, 41, 49, 57, 66, 74, 82, 90, 99, 107, 115, 123,
        132, 140, 148, 156, 165, 173, 181, 189, 198, 206, 214, 222, 231, 239, 247, 255
    };
    return UINT32_C(0xFF000000) | component[color & 31]
        | (uint32_t)component[(color >> 5) & 31] << 8
        | (uint32_t)component[(color >> 10) & 31] << 16;
}

static void every_input_keeps_channels_opaque_alpha_and_full_intensity(void) {
    CHECK(color16_to_rgba(0) == UINT32_C(0xFF000000));
    CHECK(color16_to_rgba(0x7FFF) == UINT32_C(0xFFFFFFFF));
    CHECK(color16_to_rgba(0x001F) == UINT32_C(0xFF0000FF));
    CHECK(color16_to_rgba(0x03E0) == UINT32_C(0xFF00FF00));
    CHECK(color16_to_rgba(0x7C00) == UINT32_C(0xFFFF0000));
    for (unsigned color = 0; color < 65536; ++color) {
        uint32_t rgba = color16_to_rgba((uint16_t)color);
        CHECK(rgba == expected_color((uint16_t)color));
        CHECK(color16_to_rgba((uint16_t)(color ^ 0x8000)) == rgba);
        CHECK(((rgba >> 3) & 31) == (color & 31));
        CHECK(((rgba >> 11) & 31) == ((color >> 5) & 31));
        CHECK(((rgba >> 19) & 31) == ((color >> 10) & 31));
    }
}

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    cupid_video_reset(&gba->video);
    return gba;
}

static void complete_bitmap_frames_cover_every_color_and_unused_high_bit(void) {
    for (unsigned high = 0; high < 2; ++high) {
        struct CupidGBA* gba = machine();
        for (unsigned pixel = 0; pixel < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++pixel)
            gba->video.vram[pixel] = (uint16_t)((pixel & 0x7FFF) | (high << 15));
        cupid_io_write(gba, REG_DISPCNT, 0x0403);
        /* One warm frame settles display-enable latches. The next complete
         * frame checks actual timed scanout, with an independent host oracle. */
        cupid_timing_tick(&gba->timing, 2 * VIDEO_TOTAL_LENGTH);
        CHECK(cupid_timing_global_time(&gba->timing) == 2 * VIDEO_TOTAL_LENGTH);
        for (unsigned pixel = 0; pixel < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++pixel)
            CHECK(gba->video.outputBuffer[pixel] == expected_color(gba->video.vram[pixel]));
        cupid_gba_destroy(gba);
        free(gba);
    }
}

static void forced_blank_publishes_opaque_full_white(void) {
    struct CupidGBA* gba = machine();
    cupid_io_write(gba, REG_DISPCNT, 0x0080);
    cupid_timing_tick(&gba->timing, 2 * VIDEO_TOTAL_LENGTH);
    CHECK(cupid_timing_global_time(&gba->timing) == 2 * VIDEO_TOTAL_LENGTH);
    for (unsigned pixel = 0; pixel < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++pixel)
        CHECK(gba->video.outputBuffer[pixel] == UINT32_C(0xFFFFFFFF));
    cupid_gba_destroy(gba);
    free(gba);
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "unit")) RUN(every_input_keeps_channels_opaque_alpha_and_full_intensity);
    else if (!strcmp(argv[1], "bitmap")) RUN(complete_bitmap_frames_cover_every_color_and_unused_high_bit);
    else if (!strcmp(argv[1], "blank")) RUN(forced_blank_publishes_opaque_full_white);
    else return 2;
    return 0;
}
