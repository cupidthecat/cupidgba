/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cpu-raster.h"

enum { PIXELS = GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT };

static unsigned scene(unsigned line, unsigned delay, unsigned initial, unsigned changed,
                      bool word, bool outside, const char* bios) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    uint16_t* previous = malloc(PIXELS * sizeof(*previous));
    CHECK(gba && previous);
    cupid_gba_create(gba);
    if (bios) CHECK(cupid_load_bios(gba, bios));
    cupid_gba_reset(gba);
    cupid_io_write(gba, REG_DISPCNT, 0x0080);
    cupid_memory_store16(&gba->cpu, GBA_BASE_PALETTE, 0x7C00, NULL);
    for (unsigned i = 0; i < PIXELS; ++i) gba->video.vram[i] = 0x001F;
    cupid_io_write(gba, REG_BG2PA, 0x0100);
    cupid_io_write(gba, REG_BG2PD, 0x0100);
    cupid_io_write(gba, REG_WIN0H, 0x00F0);
    /* The outside case places the window entirely in VBlank. Its horizontal
     * bounds remain nonempty, so zero-width comparator rules cannot hide a
     * WINOUT failure. The inside case covers every visible pixel. */
    cupid_io_write(gba, REG_WIN0V, outside ? 0xA0A1 : 0x00A0);
    cupid_io_write(gba, REG_WININ, initial);
    cupid_io_write(gba, REG_WINOUT, initial);
    cupid_io_write(gba, REG_DISPCNT, 0x2403); /* Mode 3, BG2 and window 0. */
    uint32_t address = GBA_BASE_IO + (outside && !word ? REG_WINOUT : REG_WININ);
    /* Word writes cover WININ and WINOUT together. Both halves receive the
     * same mask; window geometry selects the half that must affect output. */
    program(gba, line, delay, (uint16_t)initial, (uint16_t)changed, word, address);
    for (unsigned frame = 1; frame <= 6; ++frame) {
        cupid_gba_run_frame(gba);
        if (frame == 5) for (unsigned i = 0; i < PIXELS; ++i) previous[i] = rgb555(gba->video.outputBuffer[i]);
    }
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0x52535452);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM + 4, NULL) >= 4);
    CHECK((cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM + 8, NULL) & 255) == 0xDF);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM + 12, NULL) == line);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IO + (outside ? REG_WINOUT : REG_WININ), NULL) == changed);
    uint16_t before = initial ? 0x001F : 0x7C00;
    uint16_t after = changed ? 0x001F : 0x7C00;
    unsigned firstChanged = GBA_SCREEN_WIDTH;
    for (unsigned y = 0; y < GBA_SCREEN_HEIGHT; ++y) for (unsigned x = 0; x < GBA_SCREEN_WIDTH; ++x) {
        unsigned i = y * GBA_SCREEN_WIDTH + x;
        uint16_t value = rgb555(gba->video.outputBuffer[i]);
        CHECK(value == previous[i]);
        if (before == after || y < line) CHECK(value == before);
        else if (y > line) CHECK(value == after);
        else {
            CHECK(value == before || value == after);
            if (value == after && firstChanged == GBA_SCREEN_WIDTH) firstChanged = x;
            if (firstChanged != GBA_SCREEN_WIDTH) CHECK(value == after);
        }
    }
    if (before != after) CHECK(firstChanged > 0 && firstChanged < GBA_SCREEN_WIDTH);
    free(previous);
    cupid_gba_destroy(gba); free(gba);
    return firstChanged;
}

int main(int argc, char** argv) {
    CHECK((argc == 2 || argc == 3) && (!strcmp(argv[1], "half") || !strcmp(argv[1], "word")));
    bool word = !strcmp(argv[1], "word");
    const char* bios = argc == 3 ? argv[2] : NULL;
    const unsigned lines[] = {1, 79, 80, 159};
    unsigned count = 0;
    for (unsigned outside = 0; outside < 2; ++outside) for (unsigned line = 0; line < 4; ++line) {
        for (unsigned initial = 0; initial <= 4; initial += 4) {
            unsigned changed = initial ^ 4;
            unsigned early = scene(lines[line], 0, initial, changed, word, outside != 0, bios);
            unsigned late = scene(lines[line], 7, initial, changed, word, outside != 0, bios);
            CHECK(late > early);
            scene(lines[line], 0, initial, initial, word, outside != 0, bios);
            count += 3;
        }
    }
    printf("CPU window scenes: %u; frames: %u; stable pixel pairs: %u\n", count, count * 6, count * PIXELS);
    return 0;
}
