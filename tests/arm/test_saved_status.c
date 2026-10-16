/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static const enum CupidPrivMode modes[] = {MODE_USR, MODE_FIQ, MODE_IRQ, MODE_SVC, MODE_ABT, MODE_UND, MODE_SYS};
static const uint32_t addresses[] = {0x100, 0x02000100, 0x03000100, 0x08000100,
    0x09000100, 0x0A000100, 0x0B000100, 0x0C000100, 0x0D000100};
static const uint16_t waits[] = {0, 0x0317, 0x4000, 0x4317};
static const uint64_t anchors[] = {0, UINT32_MAX - 8u, UINT64_C(0x300000000)};
static const uint32_t operands[] = {0, UINT32_MAX, 0x10, 0x1F, 0x20, 0x40, 0x80, 0xC0,
    0xF0000013, 0x0500001F, 0xA50000DF, 0x5A00003F, 0x12345678, 0x87654321, 0x80000017, 0x4000001B};
static unsigned observations;

struct Snapshot {
    int32_t registers[15], banks[6][7], saved[6];
    uint32_t cpsr, spsr;
};

static bool bankless(enum CupidPrivMode mode) { return mode == MODE_USR || mode == MODE_SYS; }
static uint32_t saved_after(uint32_t before, uint32_t value, unsigned fields, enum CupidPrivMode mode) {
    if (bankless(mode)) return before;
    /* ARM7TDMI transfers expose the flags, state and control bits. The
     * reserved bytes stay stored; mode bit 4 remains set on a saved write. */
    uint32_t mask = ((fields & 1) ? 0x000000EFu : 0) | ((fields & 8) ? 0xF0000000u : 0);
    return (before & ~mask) | (value & mask) | 0x10u;
}
static struct CupidGBA* machine(uint64_t clock, bool timed) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    memset(gba->memory.bios, 0, GBA_SIZE_BIOS);
    const uint32_t handler = 0xEAFFFFFE;
    memcpy(gba->memory.bios + BASE_IRQ, &handler, sizeof(handler));
    gba->memory.fullBios = 1;
    gba->memory.biosLoaded = gba->hasBios = true;
    gba->biosHLE = false;
    gba->memory.rom = calloc(1, 0x02000000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 0x02000000;
    gba->memory.romMask = 0x01FFFFFF;
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    if (!timed) gba->cpu.memory.fetch = NULL;
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }
static uint8_t* code(struct CupidGBA* gba, unsigned region) {
    uint32_t address = addresses[region];
    if (address < GBA_SIZE_BIOS) return gba->memory.bios + address;
    if (address < GBA_BASE_IWRAM) return gba->memory.ewram + (address & 0x3FFFF);
    if (address < GBA_BASE_ROM0) return gba->memory.iwram + (address & 0x7FFF);
    return gba->memory.rom + (address & gba->memory.romMask);
}
static void instruction(struct CupidGBA* gba, unsigned region, uint32_t opcode) {
    const uint32_t program[] = {opcode, 0xE1A00000, 0xE1A00000, 0xE1A00000};
    memcpy(code(gba, region), program, sizeof(program));
    gba->memory.prefetchUnit = (struct CupidPrefetch){.epoch = cupid_timing_global_time(&gba->timing)};
    gba->memory.fetchNonseq = false;
    gba->cpu.gprs[ARM_PC] = (int32_t)addresses[region];
    CupidARMWritePC(&gba->cpu);
    gba->cpu.cycles = 0;
}
static void prepare(struct CupidGBA* gba, unsigned region, uint32_t opcode,
                    enum CupidPrivMode mode, unsigned flags, unsigned masks, uint16_t wait) {
    cupid_memory_update_waitstates(gba, wait);
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    for (unsigned r = 0; r < 15; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000u + mode * 256u + r);
    gba->cpu.cpsr.packed = (int32_t)((uint32_t)flags << 28 | 0x05000000u | masks << 6 | mode);
    gba->cpu.spsr.packed = (int32_t)(((flags ^ 15u) << 28) | 0x05000033u);
    gba->cpu.irqDisableLatch = gba->cpu.cpsr.i;
    gba->cpu.userBankActive = gba->cpu.userBankPending = false;
    _CupidSetMode(&gba->cpu, MODE_ARM);
    instruction(gba, region, opcode);
}
static struct Snapshot snapshot(const struct CupidARM* cpu) {
    struct Snapshot state = {.cpsr=(uint32_t)cpu->cpsr.packed, .spsr=(uint32_t)cpu->spsr.packed};
    memcpy(state.registers, cpu->gprs, sizeof(state.registers));
    memcpy(state.banks, cpu->bankedRegisters, sizeof(state.banks));
    memcpy(state.saved, cpu->bankedSPSRs, sizeof(state.saved));
    return state;
}
static void check(struct CupidGBA* gba, const struct Snapshot* state, unsigned region,
                  int destination, uint32_t value, uint32_t saved) {
    CHECK((uint32_t)gba->cpu.cpsr.packed == state->cpsr);
    CHECK((uint32_t)gba->cpu.spsr.packed == saved);
    CHECK(gba->cpu.privilegeMode == (enum CupidPrivMode)(state->cpsr & 31) && gba->cpu.executionMode == MODE_ARM);
    CHECK(gba->cpu.irqDisableLatch == ((state->cpsr & 0x80u) != 0));
    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == addresses[region] + 8 && gba->cpu.prefetch[0] == 0xE1A00000);
    for (unsigned r = 0; r < 15; r++) CHECK((uint32_t)gba->cpu.gprs[r] ==
        ((int)r == destination ? value : (uint32_t)state->registers[r]));
    CHECK(!memcmp(state->banks, gba->cpu.bankedRegisters, sizeof(state->banks)));
    CHECK(!memcmp(state->saved, gba->cpu.bankedSPSRs, sizeof(state->saved)));
}
static void run_case(struct CupidGBA* gba, unsigned region, int destination, uint32_t value, uint32_t saved) {
    struct Snapshot state = snapshot(&gba->cpu);
    const unsigned cycles = 1u + (unsigned)gba->memory.waitstatesSeq32[addresses[region] >> 24];
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    cupid_gba_step(gba);
    check(gba, &state, region, destination, value, saved);
    CHECK(cupid_timing_global_time(&gba->timing) - start == cycles);
    observations++;
}

static void reads(void) {
    for (unsigned c = 0; c < 3; c++) for (unsigned timed = 0; timed < 2; timed++) {
        struct CupidGBA* gba = machine(anchors[c], timed != 0);
        for (unsigned m = 0; m < 7; m++) for (unsigned flags = 0; flags < 16; flags++)
          for (unsigned masks = 0; masks < 4; masks++) for (unsigned rd = 0; rd < 15; rd++) {
            prepare(gba, 2, 0xE14F0000u | rd << 12, modes[m], flags, masks, 0);
            uint32_t value = bankless(modes[m]) ? (uint32_t)gba->cpu.cpsr.packed : (uint32_t)gba->cpu.spsr.packed;
            run_case(gba, 2, (int)rd, value, (uint32_t)gba->cpu.spsr.packed);
          }
        destroy(gba);
    }
    CHECK(observations == 40320);
}
static void registers(void) {
    for (unsigned c = 0; c < 3; c++) for (unsigned timed = 0; timed < 2; timed++) {
        struct CupidGBA* gba = machine(anchors[c], timed != 0);
        for (unsigned m = 0; m < 7; m++) for (unsigned fields = 0; fields < 16; fields++)
          for (unsigned source = 0; source < 15; source++) for (unsigned v = 0; v < 16; v++) {
            prepare(gba, 2, 0xE160F000u | fields << 16 | source, modes[m], (source + v) & 15, fields >> 2, 0);
            gba->cpu.gprs[source] = (int32_t)operands[v];
            run_case(gba, 2, -1, 0, saved_after((uint32_t)gba->cpu.spsr.packed, operands[v], fields, modes[m]));
          }
        destroy(gba);
    }
    CHECK(observations == 161280);
}
static uint32_t rotated(unsigned immediate, unsigned rotate) {
    unsigned shift = rotate * 2;
    return shift ? (uint32_t)immediate >> shift | (uint32_t)immediate << (32 - shift) : immediate;
}
static void immediates(unsigned first) {
    for (unsigned timed = 0; timed < 2; timed++) {
        struct CupidGBA* gba = machine(UINT32_MAX - 8u, timed != 0);
        for (unsigned m = 0; m < 7; m++) for (unsigned fields = 0; fields < 16; fields++)
          for (unsigned rotate = first; rotate < first + 4; rotate++) for (unsigned value = 0; value < 256; value++) {
            prepare(gba, 2, 0xE360F000u | fields << 16 | rotate << 8 | value, modes[m], (rotate + value) & 15, (value >> 4) & 3, 0);
            run_case(gba, 2, -1, 0, saved_after((uint32_t)gba->cpu.spsr.packed, rotated(value, rotate), fields, modes[m]));
          }
        destroy(gba);
    }
    CHECK(observations == 229376);
}
static void immediate_0(void) { immediates(0); }
static void immediate_4(void) { immediates(4); }
static void immediate_8(void) { immediates(8); }
static void immediate_12(void) { immediates(12); }

static void timing(void) {
    const uint32_t opcodes[] = {0xE14F7000, 0xE16FF005, 0xE36FF0FF};
    for (unsigned c = 0; c < 3; c++) for (unsigned timed = 0; timed < 2; timed++) {
        struct CupidGBA* gba = machine(anchors[c], timed != 0);
        for (unsigned a = 0; a < 9; a++) for (unsigned w = 0; w < 4; w++)
          for (unsigned m = 0; m < 7; m++) for (unsigned op = 0; op < 3; op++) {
            prepare(gba, a, opcodes[op], modes[m], 10, (a + w) & 3, waits[w]);
            gba->cpu.gprs[5] = (int32_t)0xFA000013u;
            uint32_t saved = (uint32_t)gba->cpu.spsr.packed;
            uint32_t value = bankless(modes[m]) ? (uint32_t)gba->cpu.cpsr.packed : saved;
            run_case(gba, a, op ? -1 : 7, value, op ? saved_after(saved, op == 1 ? 0xFA000013u : 255u, 15, modes[m]) : saved);
          }
        destroy(gba);
    }
    CHECK(observations == 4536);
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
static void predicates(void) {
    const uint32_t opcodes[] = {0x014F7000, 0x016FF005, 0x036FF0FF};
    for (unsigned timed = 0; timed < 2; timed++) {
        struct CupidGBA* gba = machine(0, timed != 0);
        for (unsigned m = 0; m < 7; m++) for (unsigned flags = 0; flags < 16; flags++)
          for (unsigned condition = 0; condition < 16; condition++) for (unsigned op = 0; op < 3; op++) {
            prepare(gba, 2, condition << 28 | opcodes[op], modes[m], flags, condition & 3, 0);
            gba->cpu.gprs[5] = (int32_t)0xFA000013u;
            uint32_t saved = (uint32_t)gba->cpu.spsr.packed;
            uint32_t value = bankless(modes[m]) ? (uint32_t)gba->cpu.cpsr.packed : saved;
            bool execute = predicate(condition, flags);
            run_case(gba, 2, !op && execute ? 7 : -1, value,
                op && execute ? saved_after(saved, op == 1 ? 0xFA000013u : 255u, 15, modes[m]) : saved);
          }
        destroy(gba);
    }
    CHECK(observations == 10752);
}

static void banks(void) {
    for (unsigned c = 0; c < 3; c++) for (unsigned timed = 0; timed < 2; timed++) {
        struct CupidGBA* gba = machine(anchors[c], timed != 0);
        for (unsigned round = 0; round < 64; round++) {
            uint32_t expected[5];
            for (unsigned m = 1; m <= 5; m++) {
                prepare(gba, 2, 0xE16FF005, modes[m], round & 15, round >> 4, 0);
                uint32_t value = ((round + m) & 15u) << 28 | (round >> 4) << 6 | MODE_SYS;
                gba->cpu.gprs[5] = (int32_t)value;
                expected[m-1] = saved_after((uint32_t)gba->cpu.spsr.packed, value, 15, modes[m]);
                run_case(gba, 2, -1, 0, expected[m-1]);
            }
            /* Switching banks commits each active saved value. Bankless
             * transfers must not replace any of those exception values. */
            for (unsigned m = 0; m < 2; m++) {
                enum CupidPrivMode mode = m ? MODE_USR : MODE_SYS;
                cupid_arm_set_priv_mode(&gba->cpu, mode);
                gba->cpu.cpsr.priv = mode;
                for (unsigned immediate = 0; immediate < 2; immediate++) {
                    gba->cpu.gprs[5] = -1;
                    instruction(gba, 2, immediate ? 0xE36FF0FF : 0xE16FF005);
                    run_case(gba, 2, -1, 0, (uint32_t)gba->cpu.spsr.packed);
                }
            }
            for (unsigned m = 1; m <= 5; m++) {
                cupid_arm_set_priv_mode(&gba->cpu, modes[m]);
                gba->cpu.cpsr.priv = modes[m];
                instruction(gba, 2, 0xE14F7000);
                run_case(gba, 2, 7, expected[m-1], expected[m-1]);
            }
        }
        destroy(gba);
    }
    CHECK(observations == 5376);
}
static void events(void) {
    const unsigned ram[] = {1, 2};
    for (unsigned c = 0; c < 3; c++) for (unsigned timed = 0; timed < 2; timed++)
      for (unsigned a = 0; a < 2; a++) for (unsigned w = 0; w < 4; w++) for (unsigned immediate = 0; immediate < 2; immediate++) {
        struct CupidGBA* gba = machine(anchors[c], timed != 0);
        prepare(gba, ram[a], immediate ? 0xE361F01F : 0xE161F005, MODE_SYS, 10, 2, waits[w]);
        gba->cpu.gprs[5] = MODE_SYS;
        uint32_t caller = (uint32_t)gba->cpu.cpsr.packed, saved = (uint32_t)gba->cpu.spsr.packed;
        cupid_io_write(gba, REG_IE, 1u << GBA_IRQ_TIMER0);
        cupid_io_write(gba, REG_IME, 1);
        cupid_io_write(gba, REG_TM0CNT_L, 0xFFFF);
        cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
        cupid_timing_tick(&gba->timing, 2);
        const uint32_t payload = 0xA57B4E91;
        memcpy(gba->memory.iwram + 0x1000, &payload, sizeof(payload));
        cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x1000);
        cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 0x2000);
        cupid_dma_write_cnt_lo(gba, 3, 1);
        cupid_dma_write_cnt(gba, 3, 0xC400);
        /* Activate the request before issue so both static and timed paths
         * must resume the pending instruction through real DMA ownership. */
        cupid_timing_tick(&gba->timing, 2);
        uint64_t start = cupid_timing_global_time(&gba->timing);
        /* A static CPU client supplies its bus-wait boundary explicitly. */
        if (!timed) cupid_bus_wait(&gba->cpu);
        cupid_gba_step(gba);
        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == addresses[ram[a]] + 8);
        CHECK((uint32_t)gba->cpu.cpsr.packed == caller && (uint32_t)gba->cpu.spsr.packed == saved);
        CHECK(gba->cpu.privilegeMode == MODE_SYS && gba->cpu.irqDisableLatch);
        CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable);
        CHECK(!memcmp(gba->memory.ewram + 0x2000, &payload, sizeof(payload)));
        CHECK((gba->irqf & ((1u << GBA_IRQ_TIMER0) | (1u << GBA_IRQ_DMA3))) == ((1u << GBA_IRQ_TIMER0) | (1u << GBA_IRQ_DMA3)));
        CHECK(cupid_timing_global_time(&gba->timing) > start);
        instruction(gba, ram[a], 0xE14F7000);
        cupid_gba_step(gba);
        CHECK((uint32_t)gba->cpu.gprs[7] == caller && gba->cpu.privilegeMode == MODE_SYS);
        /* Clear I through CPSR, then require the retained real timer request
         * to enter the normal IRQ path. The SPSR write never unmasks it. */
        gba->cpu.gprs[5] = (int32_t)(caller & ~0x80u);
        instruction(gba, ram[a], 0xE121F005);
        cupid_gba_step(gba);
        for (unsigned step = 0; step < 16 && gba->cpu.privilegeMode != MODE_IRQ; step++) cupid_gba_step(gba);
        CHECK(gba->cpu.privilegeMode == MODE_IRQ && (uint32_t)gba->cpu.spsr.packed == (caller & ~0x80u));
        observations++;
        destroy(gba);
      }
    CHECK(observations == 96);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "reads")) RUN(reads);
    else if (!strcmp(argv[1], "registers")) RUN(registers);
    else if (!strcmp(argv[1], "immediate-0")) RUN(immediate_0);
    else if (!strcmp(argv[1], "immediate-4")) RUN(immediate_4);
    else if (!strcmp(argv[1], "immediate-8")) RUN(immediate_8);
    else if (!strcmp(argv[1], "immediate-12")) RUN(immediate_12);
    else if (!strcmp(argv[1], "timing")) RUN(timing);
    else if (!strcmp(argv[1], "predicates")) RUN(predicates);
    else if (!strcmp(argv[1], "banks")) RUN(banks);
    else if (!strcmp(argv[1], "events")) RUN(events);
    else CHECK(false);
    printf("Saved-status observations: %u\n", observations);
    return 0;
}
