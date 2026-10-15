/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static const enum CupidPrivMode modes[] = {MODE_USR, MODE_FIQ, MODE_IRQ,
    MODE_SVC, MODE_ABT, MODE_UND, MODE_SYS};
static const uint32_t addresses[] = {0x00000100, 0x02000100, 0x03000100,
    0x08000100, 0x09000100, 0x0A000100, 0x0B000100, 0x0C000100, 0x0D000100};
static unsigned observations;

static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    memset(gba->memory.bios, 0, GBA_SIZE_BIOS);
    const uint32_t handler = 0xE1B0F00E; /* MOVS pc,lr */
    memcpy(gba->memory.bios + BASE_UNDEF, &handler, sizeof(handler));
    gba->memory.fullBios = 1;
    gba->memory.biosLoaded = gba->hasBios = true;
    gba->biosHLE = false;
    /* Both halves of each ROM window must contain actual executable bytes. */
    gba->memory.rom = calloc(1, 0x02000000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 0x02000000;
    gba->memory.romMask = 0x01FFFFFF;
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    return gba;
}

static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

static uint8_t* code(struct CupidGBA* gba, uint32_t address) {
    if (address < GBA_SIZE_BIOS) return gba->memory.bios + address;
    if (address < GBA_BASE_IWRAM) return gba->memory.ewram + (address & 0x3FFFF);
    if (address < GBA_BASE_ROM0) return gba->memory.iwram + (address & 0x7FFF);
    return gba->memory.rom + (address & gba->memory.romMask);
}

static uint32_t instruction(bool read, unsigned selector, unsigned rd) {
    return (read ? 0xEE100E10u : 0xEE000E10u) | (selector >> 11) << 21 |
        ((selector >> 7) & 15) << 16 | rd << 12 | ((selector >> 4) & 7) << 5 | (selector & 15);
}

static void prepare(struct CupidGBA* gba, uint32_t address, uint32_t opcode,
                    uint32_t value, enum CupidPrivMode mode, unsigned flags, unsigned masks) {
    const uint32_t program[] = {opcode, 0xE3A0705A, value, 0xEAFFFFFE};
    memcpy(code(gba, address), program, sizeof(program));
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    for (unsigned r = 0; r < 15; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000u + mode * 256u + r);
    gba->cpu.gprs[ARM_SP] = (int32_t)(GBA_BASE_IWRAM + 0x7000 + mode * 32u);
    gba->cpu.cpsr.packed = (int32_t)((uint32_t)flags << 28 | 0x05000000u | masks << 6 | mode);
    gba->cpu.spsr.packed = 0x6500003F;
    gba->cpu.irqDisableLatch = (masks & 2) != 0;
    _CupidSetMode(&gba->cpu, MODE_ARM);
    gba->cpu.gprs[ARM_PC] = (int32_t)address;
    CupidARMWritePC(&gba->cpu);
}

static void normal_state(struct CupidGBA* gba, uint32_t address,
                         const int32_t before[15], uint32_t status, unsigned changed) {
    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == address + 8);
    CHECK(gba->cpu.executionMode == MODE_ARM && gba->cpu.privilegeMode == (enum CupidPrivMode)(status & 31));
    CHECK((uint32_t)gba->cpu.cpsr.packed == status && gba->cpu.spsr.packed == 0x6500003F);
    CHECK(gba->cpu.irqDisableLatch == ((status & 0x80) != 0));
    for (unsigned r = 0; r < 15; r++) if (r != changed) CHECK(gba->cpu.gprs[r] == before[r]);
    CHECK(!gba->cpu.halted && !gba->stopped);
}

static void selectors(bool read) {
    struct CupidGBA* gba = machine(0);
    for (unsigned a = 0; a < sizeof(addresses) / sizeof(*addresses); a++) {
        for (unsigned selector = 0; selector < 16384; selector++) {
            const unsigned rd = selector % (read ? 15u : 16u);
            const uint32_t value = 0x81234567u ^ selector * 0x01010101u;
            prepare(gba, addresses[a], instruction(read, selector, rd), value,
                modes[selector % 7], selector & 15, (selector >> 4) & 3);
            int32_t before[15];
            memcpy(before, gba->cpu.gprs, sizeof(before));
            const uint32_t status = (uint32_t)gba->cpu.cpsr.packed;
            cupid_gba_step(gba);
            normal_state(gba, addresses[a], before, status, read ? rd : 16);
            if (read) CHECK((uint32_t)gba->cpu.gprs[rd] == value);
            CHECK(!memcmp(code(gba, addresses[a]) + 8, &value, sizeof(value)));
            observations++;
        }
    }
    CHECK(observations == 147456);
    destroy(gba);
}

static void reads(void) { selectors(true); }
static void writes(void) { selectors(false); }

static void r15_updates_only_condition_flags(void) {
    struct CupidGBA* gba = machine(0);
    for (unsigned a = 0; a < sizeof(addresses) / sizeof(*addresses); a++)
      for (unsigned m = 0; m < sizeof(modes) / sizeof(*modes); m++)
        for (unsigned masks = 0; masks < 4; masks++)
          for (unsigned old = 0; old < 16; old++)
            for (unsigned result = 0; result < 16; result++) {
                const uint32_t value = result << 28 | 0x0ABC9876u;
                prepare(gba, addresses[a], instruction(true, old * 911u, ARM_PC), value, modes[m], old, masks);
                const uint32_t expected = ((uint32_t)gba->cpu.cpsr.packed & 0x0FFFFFFFu) | (value & 0xF0000000u);
                int32_t before[15];
                memcpy(before, gba->cpu.gprs, sizeof(before));
                cupid_gba_step(gba);
                normal_state(gba, addresses[a], before, expected, 16);
                cupid_gba_step(gba);
                CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == addresses[a] + 12 && gba->cpu.gprs[7] == 0x5A);
                CHECK((uint32_t)gba->cpu.cpsr.packed == expected && gba->cpu.privilegeMode == modes[m]);
                observations++;
            }
    CHECK(observations == 64512);
    destroy(gba);
}

static bool predicate(unsigned condition, unsigned flags) {
    const bool n = flags & 8, z = flags & 4, c = flags & 2, v = flags & 1;
    switch (condition) {
    case 0: return z; case 1: return !z; case 2: return c; case 3: return !c;
    case 4: return n; case 5: return !n; case 6: return v; case 7: return !v;
    case 8: return c && !z; case 9: return !c || z; case 10: return n == v;
    case 11: return n != v; case 12: return !z && n == v; case 13: return z || n != v;
    case 14: return true; default: return false;
    }
}

static void predicates_gate_all_register_effects(void) {
    struct CupidGBA* gba = machine(0);
    for (unsigned a = 0; a < sizeof(addresses) / sizeof(*addresses); a++)
      for (unsigned condition = 0; condition < 16; condition++)
        for (unsigned flags = 0; flags < 16; flags++)
          for (unsigned kind = 0; kind < 3; kind++) {
            const unsigned rd = kind == 2 ? ARM_PC : 3;
            const uint32_t value = 0x91234567;
            const uint32_t opcode = (instruction(kind != 0, 12345, rd) & 0x0FFFFFFFu) | condition << 28;
            prepare(gba, addresses[a], opcode, value, MODE_SYS, flags, 0);
            int32_t before[15];
            memcpy(before, gba->cpu.gprs, sizeof(before));
            uint32_t status = (uint32_t)gba->cpu.cpsr.packed;
            const bool execute = predicate(condition, flags);
            if (execute && kind == 2) status = (status & 0x0FFFFFFFu) | (value & 0xF0000000u);
            cupid_gba_step(gba);
            normal_state(gba, addresses[a], before, status, execute && kind == 1 ? rd : 16);
            if (execute && kind == 1) CHECK((uint32_t)gba->cpu.gprs[rd] == value);
            observations++;
          }
    CHECK(observations == 6912);
    destroy(gba);
}

static void unavailable_coprocessors_still_take_the_exception(void) {
    const uint32_t opcodes[] = {0xEE100010, 0xEE000010, 0xEE000000, 0xED910000, 0xED810000};
    struct CupidGBA* gba = machine(0);
    for (unsigned a = 0; a < sizeof(addresses) / sizeof(*addresses); a++)
      for (unsigned m = 0; m < sizeof(modes) / sizeof(*modes); m++)
        for (unsigned masks = 0; masks < 4; masks++)
          for (unsigned cp = 0; cp < 16; cp++)
            for (unsigned op = 0; op < sizeof(opcodes) / sizeof(*opcodes); op++) {
                if (cp == 14 && op < 2) continue;
                prepare(gba, addresses[a], opcodes[op] | cp << 8, 0x81234567, modes[m], 10, masks);
                int32_t before[15];
                memcpy(before, gba->cpu.gprs, sizeof(before));
                const uint32_t status = (uint32_t)gba->cpu.cpsr.packed;
                const uint32_t guard = 0x89ABCDEF;
                memcpy(gba->memory.ewram + 0x1000, &guard, sizeof(guard));
                cupid_gba_step(gba);
                CHECK(gba->cpu.privilegeMode == MODE_UND && gba->cpu.executionMode == MODE_ARM);
                CHECK((uint32_t)gba->cpu.cpsr.packed == ((status & ~31u) | MODE_UND | 0x80u));
                CHECK((uint32_t)gba->cpu.spsr.packed == status && (uint32_t)gba->cpu.gprs[ARM_LR] == addresses[a] + 4);
                CHECK(gba->cpu.gprs[ARM_PC] == BASE_UNDEF + 4 && gba->cpu.prefetch[0] == 0xE1B0F00E);
                CHECK(!memcmp(gba->memory.ewram + 0x1000, &guard, sizeof(guard)));
                cupid_gba_step(gba);
                CHECK(gba->cpu.privilegeMode == modes[m] && (uint32_t)gba->cpu.cpsr.packed == status);
                CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == addresses[a] + 8 && gba->cpu.prefetch[0] == 0xE3A0705A);
                for (unsigned r = 0; r < 15; r++) CHECK(gba->cpu.gprs[r] ==
                    (r == ARM_LR && modes[m] == MODE_UND ? (int32_t)(addresses[a] + 4) : before[r]));
                observations++;
            }
    CHECK(observations == 19656);
    destroy(gba);
}

static void dma_completion_supplies_the_fetched_value_across_clock_wrap(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - 16u, UINT64_C(0x300000000)};
    const uint32_t ram[] = {GBA_BASE_EWRAM + 0x100, GBA_BASE_IWRAM + 0x100};
    for (unsigned c = 0; c < sizeof(clocks) / sizeof(*clocks); c++)
      for (unsigned a = 0; a < sizeof(ram) / sizeof(*ram); a++) {
        struct CupidGBA* gba = machine(clocks[c]);
        prepare(gba, ram[a], instruction(true, 0, 0), 0x12345678, MODE_SYS, 0, 0);
        const uint32_t replacement = 0xB000FACE;
        memcpy(gba->memory.iwram + 0x1000, &replacement, sizeof(replacement));
        cupid_io_write(gba, REG_TM0CNT_L, 0);
        cupid_io_write(gba, REG_TM0CNT_H, 0x80);
        cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x1000);
        cupid_dma_write_dad(gba, 3, ram[a] + 8);
        cupid_dma_write_cnt_lo(gba, 3, 1);
        cupid_dma_write_cnt(gba, 3, 0x8400);
        cupid_timing_tick(&gba->timing, 2);
        const uint64_t start = cupid_timing_global_time(&gba->timing);
        const uint16_t timer = cupid_timer_read_counter(gba, 0);
        cupid_gba_step(gba);
        CHECK((uint32_t)gba->cpu.gprs[0] == replacement && gba->cpu.privilegeMode == MODE_SYS);
        CHECK(!gba->memory.dma[3].enable && !gba->memory.dmaRunning);
        CHECK(!memcmp(code(gba, ram[a]) + 8, &replacement, sizeof(replacement)));
        CHECK((uint16_t)(cupid_timer_read_counter(gba, 0) - timer) ==
            (uint16_t)(cupid_timing_global_time(&gba->timing) - start));
        observations++;
        destroy(gba);
      }
    CHECK(observations == 6);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "read")) RUN(reads);
    else if (!strcmp(argv[1], "write")) RUN(writes);
    else if (!strcmp(argv[1], "flags")) RUN(r15_updates_only_condition_flags);
    else if (!strcmp(argv[1], "conditions")) RUN(predicates_gate_all_register_effects);
    else if (!strcmp(argv[1], "undefined")) RUN(unavailable_coprocessors_still_take_the_exception);
    else if (!strcmp(argv[1], "pipeline")) RUN(dma_completion_supplies_the_fetched_value_across_clock_wrap);
    else CHECK(false);
    printf("CP14 observations: %u\n", observations);
    return 0;
}
