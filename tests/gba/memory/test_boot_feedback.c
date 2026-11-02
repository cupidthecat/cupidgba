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
    static const struct {
        uint32_t opcode, value;
    } loads[] = {
        {0xE59F0004, 0x89ABCDEF}, /* LDR r0,[pc,#4] */
        {0xE5DF0004, 0xEF},       /* LDRB */
        {0xE1DF00B4, 0xCDEF},     /* LDRH */
        {0xE1DF00D4, 0xFFFFFFEF}, /* LDRSB */
        {0xE1DF00F4, 0xFFFFCDEF}, /* LDRSH */
        {0xE59F0005, 0xEF89ABCD}, /* Unaligned LDR */
        {0xE1DF00B5, 0xEF0000CD}, /* Unaligned LDRH */
        {0xE1DF00F5, 0xFFFFFFCD}, /* Odd LDRSH reads a signed byte. */
    };

    static const uint16_t controls[] = {0, 1, 0x20, 0x21};
    for (unsigned map = 0; map < sizeof(controls) / sizeof(*controls); ++map) {
        for (unsigned load = 0; load < sizeof(loads) / sizeof(*loads); ++load) {
            struct CupidGBA *first = machine(true);
            const uint32_t code[] = {loads[load].opcode, 0xEAFFFFFE, 0xE1A00000, 0x89ABCDEF};
            memcpy(first->memory.bios, code, sizeof(code));
            /* The previous low-memory decoder must not supply stale RAM
             * instructions to the new firmware's reset pipeline. */
            const uint32_t stale[] = {0xE3A00077, 0xEAFFFFFE};
            memcpy(first->memory.ewram, stale, sizeof(stale));
            memcpy(first->memory.iwram, stale, sizeof(stale));
            first->memory.internalMemoryControl = controls[map];
            first->memory.fullBios = true;
            cupid_gba_reset(first);
            CHECK(!first->biosHLE);
            CHECK(cupid_timing_global_time(&first->timing) == 0);
            CHECK(cupid_arm_instruction_address(&first->cpu) == 0);
            CHECK(first->cpu.prefetch[0] == loads[load].opcode);
            cupid_gba_step(first);
            CHECK((uint32_t)first->cpu.gprs[0] == loads[load].value);
            CHECK(cupid_arm_instruction_address(&first->cpu) == 4);
            CHECK((first->cpu.cpsr.packed & 255) == 0xD3);
            destroy(first);
        }
    }
    struct CupidGBA *g = machine(true);
    g->memory.fullBios = true;
    cupid_gba_reset(g);
    CHECK(!g->biosHLE);
    CHECK(cupid_arm_instruction_address(&g->cpu) == 0);
    CHECK(g->memory.biosPrefetch == 0);
    CHECK(g->memory.activeRegion == GBA_REGION_BIOS);
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
