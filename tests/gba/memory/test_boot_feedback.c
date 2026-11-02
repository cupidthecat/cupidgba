/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_boot_feedback.c
 * @brief Protected BIOS input across fast entry, reset, load widths and firmware execution.
 */
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include <string.h>

static struct CupidGBA *machine(bool cartridge) {
    struct CupidGBA *g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    if (cartridge) {
        g->memory.rom = calloc(1, 0x100);
        CHECK(g->memory.rom);
        g->memory.romSize = g->memory.romImageSize = 0x100;
        g->memory.romMask = 0xFF;
    }
    cupid_gba_reset(g);
    return g;
}

static void destroy(struct CupidGBA *g) {
    cupid_gba_destroy(g);
    free(g);
}

static void entry(void) {
    for (unsigned cartridge = 0; cartridge < 2; ++cartridge) {
        struct CupidGBA *g = machine(cartridge != 0);
        CHECK(g->biosHLE);
        CHECK(g->memory.biosPrefetch == 0xE129F000);
        CHECK(cupid_arm_instruction_address(&g->cpu) ==
              (cartridge ? GBA_BASE_ROM0 : GBA_BASE_EWRAM));
        CHECK(cupid_timing_global_time(&g->timing) == 0);
        for (unsigned repeat = 0; repeat < 3; ++repeat) {
            g->memory.biosPrefetch = 0x10203040u + repeat;
            cupid_gba_reset(g);
            CHECK(g->memory.biosPrefetch == 0xE129F000);
            CHECK(cupid_timing_global_time(&g->timing) == 0);
        }
        if (!cartridge) {
            /* Prepare the other deliberate RAM entry while still at reset. */
            g->memory.fullBios = true;
            cupid_gba_reset(g);
            g->memory.ewram[0x30] = 1;
            g->memory.fullBios = false;
            cupid_gba_skip_bios(g);
            CHECK(cupid_arm_instruction_address(&g->cpu) == GBA_BASE_EWRAM + 0xC0);
            CHECK(g->memory.biosPrefetch == 0xE129F000);
            CHECK(cupid_timing_global_time(&g->timing) == 0);
        }
        destroy(g);
    }
}

static void reads(void) {
    static const uint32_t words[] = {0xE129F000, 0x00E129F0, 0xF000E129, 0x29F000E1};
    static const uint32_t halves[] = {0xF000, 0xF0, 0xE129, 0x290000E1};
    static const uint32_t bytes[] = {0x00, 0xF0, 0x29, 0xE1};
    for (unsigned cartridge = 0; cartridge < 2; ++cartridge) {
        struct CupidGBA *g = machine(cartridge != 0);
        for (uint32_t address = 0; address < GBA_SIZE_BIOS; ++address) {
            int32_t cycles = 0;
            CHECK(g->cpu.memory.load8(&g->cpu, address, &cycles) == bytes[address & 3]);
            CHECK(g->cpu.memory.load16(&g->cpu, address, &cycles) == halves[address & 3]);
            CHECK(g->cpu.memory.load32(&g->cpu, address, &cycles) == words[address & 3]);
            CHECK(g->memory.biosPrefetch == 0xE129F000);
        }
        destroy(g);
    }
}

static void guard(void) {
    struct CupidGBA *g = machine(true);
    g->memory.biosPrefetch = 0x87654321;
    struct CupidGBA *before = malloc(sizeof(*before));
    CHECK(before);
    memcpy(before, g, sizeof(*before));
    cupid_gba_skip_bios(g);
    CHECK(!memcmp(before, g, sizeof(*before)));
    free(before);
    destroy(g);
}

static void firmware(void) {
    struct CupidGBA *g = machine(true);
    g->memory.fullBios = true;
    cupid_gba_reset(g);
    CHECK(!g->biosHLE);
    CHECK(cupid_arm_instruction_address(&g->cpu) == 0);
    CHECK(g->memory.biosPrefetch == 0);
    g->cpu.memory.setActiveRegion(&g->cpu, 0);
    g->cpu.prefetch[1] = 0x10293847;
    g->cpu.memory.setActiveRegion(&g->cpu, GBA_BASE_ROM0);
    CHECK(g->memory.biosPrefetch == 0x10293847);
    int32_t cycles = 0;
    CHECK(g->cpu.memory.load32(&g->cpu, 0, &cycles) == 0x10293847);
    destroy(g);
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    if (!strcmp(argv[1], "entry"))
        RUN(entry);
    else if (!strcmp(argv[1], "reads"))
        RUN(reads);
    else if (!strcmp(argv[1], "guard"))
        RUN(guard);
    else if (!strcmp(argv[1], "firmware"))
        RUN(firmware);
    else
        return 2;
    return 0;
}
