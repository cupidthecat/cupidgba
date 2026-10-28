/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cpu-raster.h"

enum { PIXELS = GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT };

static unsigned scene(unsigned line, unsigned delay, uint16_t initial, uint16_t changed, bool word, const char* bios) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    uint16_t* previous = malloc(PIXELS * sizeof(*previous));
    CHECK(gba && previous);
    cupid_gba_create(gba);
    if (bios) CHECK(cupid_load_bios(gba, bios));
    cupid_gba_reset(gba);
    cupid_io_write(gba, REG_DISPCNT, 0);
    cupid_memory_store16(&gba->cpu, GBA_BASE_PALETTE, initial, NULL);
    program(gba, line, delay, initial, changed, word, GBA_BASE_PALETTE);
    for (unsigned frame = 1; frame <= 6; ++frame) {
        cupid_gba_run_frame(gba);
        if (frame == 5) for (unsigned i = 0; i < PIXELS; ++i) previous[i] = rgb555(gba->video.outputBuffer[i]);
    }
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0x52535452);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM + 4, NULL) >= 4);
    CHECK((cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM + 8, NULL) & 255) == 0xDF);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM + 12, NULL) == line);
    unsigned firstChanged = GBA_SCREEN_WIDTH;
    for (unsigned y = 0; y < GBA_SCREEN_HEIGHT; ++y) for (unsigned x = 0; x < GBA_SCREEN_WIDTH; ++x) {
        unsigned i = y * GBA_SCREEN_WIDTH + x;
        uint16_t value = rgb555(gba->video.outputBuffer[i]);
        CHECK(value == previous[i]);
        if (initial == changed || y < line) CHECK(value == initial);
        else if (y > line) CHECK(value == changed);
        else {
            CHECK(value == initial || value == changed);
            if (value == changed && firstChanged == GBA_SCREEN_WIDTH) firstChanged = x;
            if (firstChanged != GBA_SCREEN_WIDTH) CHECK(value == changed);
        }
    }
    if (initial != changed) CHECK(firstChanged > 0 && firstChanged < GBA_SCREEN_WIDTH);
    free(previous);
    cupid_gba_destroy(gba); free(gba);
    return firstChanged;
}

int main(int argc, char** argv) {
    CHECK((argc == 2 || argc == 3) && (!strcmp(argv[1], "half") || !strcmp(argv[1], "word")));
    bool word = !strcmp(argv[1], "word");
    const char* bios = argc == 3 ? argv[2] : NULL;
    const unsigned lines[] = {1, 79, 80, 159};
    const uint16_t colors[] = {0x001F, 0x03E0, 0x7C00};
    unsigned count = 0;
    for (unsigned line = 0; line < 4; ++line) for (unsigned color = 0; color < 3; ++color) {
        uint16_t initial = colors[color], changed = colors[(color + 1) % 3];
        unsigned early = scene(lines[line], 0, initial, changed, word, bios);
        unsigned late = scene(lines[line], 7, initial, changed, word, bios);
        CHECK(late > early);
        scene(lines[line], 0, initial, initial, word, bios);
        count += 3;
    }
    printf("CPU raster scenes: %u; frames: %u; stable pixel pairs: %u\n", count, count * 6, count * PIXELS);
    return 0;
}
