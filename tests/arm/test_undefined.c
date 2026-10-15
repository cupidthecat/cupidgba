/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static const enum CupidPrivMode modes[] = {MODE_USR, MODE_FIQ, MODE_IRQ, MODE_SVC, MODE_ABT, MODE_UND, MODE_SYS};
static const uint32_t addresses[] = {0x100, 0x02000100, 0x03000100, 0x08000100,
    0x09000100, 0x0A000100, 0x0B000100, 0x0C000100, 0x0D000100};
static const uint64_t anchors[] = {0, UINT32_MAX - 8u, UINT64_C(0x300000000)};
static const uint16_t waits[] = {0, 0x0317, 0x4000, 0x4317};
struct Trap { uint32_t opcode; bool thumb; };
static const struct Trap traps[] = {{0xE7F000F0, false}, {0xDE42, true},
    {0xEE100F10, false}, {0xEE000F10, false}, {0xEE000F00, false}, {0xED910F00, false}, {0xED810F00, false}};
static unsigned observations;

static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    memset(gba->memory.bios, 0, GBA_SIZE_BIOS);
    const uint32_t handler = 0xE1B0F00E;
    memcpy(gba->memory.bios + BASE_UNDEF, &handler, sizeof(handler));
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
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }
static uint8_t* code(struct CupidGBA* gba, uint32_t address) {
    if (address < GBA_SIZE_BIOS) return gba->memory.bios + address;
    if (address < GBA_BASE_IWRAM) return gba->memory.ewram + (address & 0x3FFFF);
    if (address < GBA_BASE_ROM0) return gba->memory.iwram + (address & 0x7FFF);
    return gba->memory.rom + (address & gba->memory.romMask);
}
static void prepare(struct CupidGBA* gba, unsigned a, struct Trap trap,
                    enum CupidPrivMode mode, unsigned flags, unsigned masks, uint16_t wait) {
    gba->memory.prefetchUnit = (struct CupidPrefetch){.epoch = cupid_timing_global_time(&gba->timing)};
    gba->memory.fetchNonseq = false;
    cupid_memory_update_waitstates(gba, wait);
    if (trap.thumb) {
        const uint16_t program[] = {(uint16_t)trap.opcode, 0x275A, 0x46C0, 0x46C0};
        memcpy(code(gba, addresses[a]), program, sizeof(program));
    } else {
        const uint32_t program[] = {trap.opcode, 0xE3A0705A, 0xE1A00000, 0xE1A00000};
        memcpy(code(gba, addresses[a]), program, sizeof(program));
    }
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    for (unsigned r = 0; r < 15; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000u + mode * 256u + r);
    gba->cpu.gprs[ARM_SP] = (int32_t)(GBA_BASE_IWRAM + 0x7000 + mode * 32u);
    gba->cpu.cpsr.packed = (int32_t)((uint32_t)flags << 28 | 0x05000000u | masks << 6 | mode);
    gba->cpu.spsr.packed = 0x6500003F;
    _CupidSetMode(&gba->cpu, trap.thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = (int32_t)addresses[a];
    if (trap.thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
    gba->cpu.cycles = 0;
}
static unsigned source_cycles(struct CupidGBA* gba, unsigned a, bool thumb) {
    const unsigned region = addresses[a] >> 24;
    return 1 + (unsigned)(thumb ? gba->memory.waitstatesSeq16[region] : gba->memory.waitstatesSeq32[region]);
}
static uint32_t entry_status(uint32_t caller) { return (caller & ~63u) | MODE_UND | 0x80u; }

static void timing_matrix(bool timed) {
    for (unsigned c = 0; c < sizeof(anchors) / sizeof(*anchors); c++) {
        struct CupidGBA* gba = machine(anchors[c]);
        if (!timed) gba->cpu.memory.fetch = NULL;
        for (unsigned w = 0; w < sizeof(waits) / sizeof(*waits); w++)
          for (unsigned a = 0; a < sizeof(addresses) / sizeof(*addresses); a++)
            for (unsigned t = 0; t < sizeof(traps) / sizeof(*traps); t++) {
                prepare(gba, a, traps[t], MODE_SYS, 10, 0, waits[w]);
                const unsigned expected = source_cycles(gba, a, traps[t].thumb) + 3;
                const uint64_t start = cupid_timing_global_time(&gba->timing);
                if (traps[t].thumb) cupid_thumb_step(&gba->cpu); else cupid_arm_step(&gba->cpu);
                CHECK(gba->cpu.privilegeMode == MODE_UND && gba->cpu.executionMode == MODE_ARM);
                const uint64_t elapsed = timed ? cupid_timing_global_time(&gba->timing) - start : (uint32_t)gba->cpu.cycles;
                CHECK(elapsed == expected);
                CHECK(gba->cpu.gprs[ARM_PC] == BASE_UNDEF + 4 && gba->cpu.prefetch[0] == 0xE1B0F00E);
                observations++;
            }
        destroy(gba);
    }
    CHECK(observations == 756);
}
static void timed(void) { timing_matrix(true); }
static void untimed(void) { timing_matrix(false); }

static void caller_banks_masks_and_return_are_preserved(void) {
    struct CupidGBA* gba = machine(0);
    for (unsigned a = 0; a < sizeof(addresses) / sizeof(*addresses); a++)
      for (unsigned m = 0; m < sizeof(modes) / sizeof(*modes); m++)
        for (unsigned flags = 0; flags < 16; flags++)
          for (unsigned masks = 0; masks < 4; masks++)
            for (unsigned t = 0; t < sizeof(traps) / sizeof(*traps); t++) {
                prepare(gba, a, traps[t], modes[m], flags, masks, 0);
                const uint32_t caller = (uint32_t)gba->cpu.cpsr.packed;
                int32_t before[15];
                memcpy(before, gba->cpu.gprs, sizeof(before));
                const unsigned width = traps[t].thumb ? 2u : 4u;
                cupid_gba_step(gba);
                CHECK((uint32_t)gba->cpu.cpsr.packed == entry_status(caller));
                CHECK((uint32_t)gba->cpu.spsr.packed == caller && gba->cpu.irqDisableLatch);
                CHECK((uint32_t)gba->cpu.gprs[ARM_LR] == addresses[a] + width);
                CHECK(gba->cpu.gprs[ARM_PC] == BASE_UNDEF + 4 && gba->cpu.executionMode == MODE_ARM);
                cupid_gba_step(gba);
                CHECK((uint32_t)gba->cpu.cpsr.packed == caller && gba->cpu.privilegeMode == modes[m]);
                CHECK(gba->cpu.executionMode == (traps[t].thumb ? MODE_THUMB : MODE_ARM));
                CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == addresses[a] + 2 * width);
                CHECK(gba->cpu.prefetch[0] == (traps[t].thumb ? 0x275Au : 0xE3A0705Au));
                for (unsigned r = 0; r < 15; r++) CHECK(gba->cpu.gprs[r] ==
                    (r == ARM_LR && modes[m] == MODE_UND ? (int32_t)(addresses[a] + width) : before[r]));
                observations++;
            }
    CHECK(observations == 28224);
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
static void false_predicates_do_not_trap_or_charge_internal_cycles(void) {
    struct CupidGBA* gba = machine(0);
    for (unsigned a = 0; a < sizeof(addresses) / sizeof(*addresses); a++)
      for (unsigned condition = 0; condition < 16; condition++)
        for (unsigned flags = 0; flags < 16; flags++)
          for (unsigned t = 0; t < sizeof(traps) / sizeof(*traps); t++) {
            if (traps[t].thumb) continue;
            struct Trap trap = traps[t];
            trap.opcode = (trap.opcode & 0x0FFFFFFFu) | condition << 28;
            prepare(gba, a, trap, MODE_SYS, flags, 0, 0);
            const uint32_t caller = (uint32_t)gba->cpu.cpsr.packed;
            const unsigned source = source_cycles(gba, a, false);
            const uint64_t start = cupid_timing_global_time(&gba->timing);
            const bool execute = predicate(condition, flags);
            cupid_gba_step(gba);
            CHECK(cupid_timing_global_time(&gba->timing) - start == source + (execute ? 3u : 0u));
            CHECK((uint32_t)gba->cpu.cpsr.packed == (execute ? entry_status(caller) : caller));
            CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == (execute ? BASE_UNDEF + 4u : addresses[a] + 8));
            observations++;
          }
    CHECK(observations == 13824);
    destroy(gba);
}

struct Sample { struct CupidTimingEvent event; struct CupidGBA* gba; uint64_t clock; uint32_t status; bool busy; unsigned count; };
static void sample(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct Sample* result = context;
    result->clock = cupid_timing_global_time(timing);
    result->status = (uint32_t)result->gba->cpu.cpsr.packed;
    result->busy = result->gba->memory.busBusy;
    result->count++;
}
static void phase(struct CupidGBA* gba, struct Sample* out, uint64_t when) {
    *out = (struct Sample){.gba = gba};
    out->event = (struct CupidTimingEvent){.context = out, .callback = sample, .name = "Undefined entry phase", .priority = 10};
    cupid_timing_schedule_absolute(&gba->timing, &out->event, (int32_t)(uint32_t)when);
}
static void the_internal_interval_precedes_bank_change_and_vector_fetch(void) {
    for (unsigned a = 0; a < sizeof(addresses) / sizeof(*addresses); a++)
      for (unsigned t = 0; t < sizeof(traps) / sizeof(*traps); t++) {
        struct CupidGBA* gba = machine(UINT32_MAX - 8u);
        prepare(gba, a, traps[t], MODE_SYS, 10, 1, 0);
        const uint32_t caller = (uint32_t)gba->cpu.cpsr.packed;
        const uint64_t start = cupid_timing_global_time(&gba->timing);
        const unsigned source = source_cycles(gba, a, traps[t].thumb);
        struct Sample samples[3];
        for (unsigned i = 0; i < 3; i++) phase(gba, &samples[i], start + source + i + 1);
        cupid_gba_step(gba);
        for (unsigned i = 0; i < 3; i++) CHECK(samples[i].count == 1 && samples[i].clock == start + source + i + 1);
        CHECK(samples[0].status == caller && !samples[0].busy);
        CHECK(samples[1].status == entry_status(caller) && samples[1].busy);
        CHECK(samples[2].status == entry_status(caller) && samples[2].busy);
        observations++;
        destroy(gba);
      }
    CHECK(observations == 63);
}

static void timer_and_dma_run_in_the_internal_interval(void) {
    const unsigned ram[] = {1, 2};
    for (unsigned c = 0; c < sizeof(anchors) / sizeof(*anchors); c++)
      for (unsigned a = 0; a < sizeof(ram) / sizeof(*ram); a++)
        for (unsigned t = 0; t < 2; t++) {
            struct CupidGBA* gba = machine(anchors[c]);
            prepare(gba, ram[a], traps[t], MODE_SYS, 10, 0, 0);
            cupid_io_write(gba, REG_TM0CNT_L, 0);
            cupid_io_write(gba, REG_TM0CNT_H, 0x80);
            cupid_timing_tick(&gba->timing, 2);
            const uint32_t value = 0xB000FACE;
            memcpy(gba->memory.iwram + 0x1000, &value, sizeof(value));
            cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x1000);
            cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 0x2000);
            cupid_dma_write_cnt_lo(gba, 3, 1);
            cupid_dma_write_cnt(gba, 3, 0x8400);
            const uint64_t start = cupid_timing_global_time(&gba->timing);
            const uint16_t timer = cupid_timer_read_counter(gba, 0);
            const uint32_t caller = (uint32_t)gba->cpu.cpsr.packed;
            struct Sample internal;
            phase(gba, &internal, start + source_cycles(gba, ram[a], traps[t].thumb) + 1);
            cupid_gba_step(gba);
            CHECK(internal.count == 1 && internal.status == caller && !internal.busy);
            CHECK((uint32_t)gba->cpu.cpsr.packed == entry_status(caller));
            CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable);
            CHECK(!memcmp(gba->memory.ewram + 0x2000, &value, sizeof(value)));
            CHECK((uint16_t)(cupid_timer_read_counter(gba, 0) - timer) ==
                (uint16_t)(cupid_timing_global_time(&gba->timing) - start));
            observations++;
            destroy(gba);
        }
    CHECK(observations == 12);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "timed")) RUN(timed);
    else if (!strcmp(argv[1], "untimed")) RUN(untimed);
    else if (!strcmp(argv[1], "state")) RUN(caller_banks_masks_and_return_are_preserved);
    else if (!strcmp(argv[1], "conditions")) RUN(false_predicates_do_not_trap_or_charge_internal_cycles);
    else if (!strcmp(argv[1], "phase")) RUN(the_internal_interval_precedes_bank_change_and_vector_fetch);
    else if (!strcmp(argv[1], "dma")) RUN(timer_and_dma_run_in_the_internal_interval);
    else CHECK(false);
    printf("Undefined-entry observations: %u\n", observations);
    return 0;
}
