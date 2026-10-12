/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"

static void lz77_backreferences_preserve_high_bits(void) {
    /* Two literals followed by a four-byte copy at distance two. */
    const uint8_t compressed[] = {0x10, 6, 0, 0, 0x20, 0x81, 0xF1, 0x10, 1};
    const uint8_t expected[] = {0x81, 0xF1, 0x81, 0xF1, 0x81, 0xF1};
    const uint32_t destinations[] = {GBA_BASE_EWRAM + 0x100, GBA_BASE_VRAM, GBA_BASE_EWRAM + 0x100};
    const uint8_t calls[] = {0x11, 0x12, 0x12};
    for (unsigned i = 0; i < 3; i++) {
        struct CupidGBA* gba = calloc(1, sizeof(*gba));
        CHECK(gba);
        cupid_gba_create(gba);
        cupid_gba_reset(gba);
        memcpy(gba->memory.ewram, compressed, sizeof(compressed));
        gba->cpu.gprs[0] = GBA_BASE_EWRAM;
        gba->cpu.gprs[1] = destinations[i];
        cupid_bios_swi(gba, calls[i]);
        const uint8_t* output = i == 1 ? (const uint8_t*)gba->video.vram : gba->memory.ewram + 0x100;
        CHECK(!memcmp(output, expected, sizeof(expected)));
        CHECK((uint32_t)gba->cpu.gprs[0] == GBA_BASE_EWRAM + sizeof(compressed));
        CHECK((uint32_t)gba->cpu.gprs[1] == destinations[i] + sizeof(expected));
        CHECK(gba->cpu.gprs[3] == 0);
        cupid_gba_destroy(gba);
        free(gba);
    }
}

int main(void) {
    RUN(lz77_backreferences_preserve_high_bits);
    return 0;
}
