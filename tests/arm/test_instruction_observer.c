/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "instruction_probe.h"
#include "../test.h"

enum { CODE = GBA_BASE_IWRAM + 0x100 };
static unsigned observations;

static struct CupidGBA* machine(bool timed) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    memset(gba->memory.bios, 0, GBA_SIZE_BIOS);
    gba->memory.biosLoaded = gba->hasBios = true;
    gba->memory.fullBios = 1;
    gba->biosHLE = false;
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SYS);
    gba->cpu.cpsr.packed = MODE_SYS;
    gba->cpu.irqDisableLatch = false;
    gba->cpu.gprs[ARM_SP] = GBA_BASE_IWRAM + 0x7000;
    if (!timed) gba->cpu.memory.fetch = NULL;
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }
static void program(struct CupidGBA* gba, const void* code, size_t size, bool thumb) {
    memcpy(gba->memory.iwram + 0x100, code, size);
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = CODE;
    if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
    gba->cpu.cycles = 0;
}
struct Count { unsigned calls; uint32_t pc, opcode, r2; enum CupidExecMode mode; };
static void count(const struct CupidARM* cpu, void* context) {
    struct Count* record = context;
    ++record->calls;
    record->pc = cupid_arm_instruction_address(cpu);
    record->opcode = cpu->prefetch[0];
    record->r2 = (uint32_t)cpu->gprs[2];
    record->mode = cpu->executionMode;
}
static void lifecycle(void) {
    struct CupidARM cpu;
    memset(&cpu, 0xA5, sizeof(cpu));
    cupid_arm_init(&cpu);
    CHECK(!cpu.instructionObserver && !cpu.instructionObserverContext);
    for (unsigned timed = 0; timed < 2; ++timed) {
        struct CupidGBA* gba = machine(timed != 0);
        struct Count record = {0};
        gba->cpu.instructionObserver = count;
        gba->cpu.instructionObserverContext = &record;
        cupid_gba_reset(gba);
        CHECK(gba->cpu.instructionObserver == count && gba->cpu.instructionObserverContext == &record);
        CHECK(record.calls == 0);
        const uint32_t code[] = {0xE3A02007, 0xE1A00000, 0xEAFFFFFE};
        program(gba, code, sizeof(code), false);
        gba->cpu.gprs[2] = 4;
        cupid_gba_step(gba);
        CHECK(record.calls == 1 && record.pc == CODE && record.opcode == code[0] && record.r2 == 4);
        CHECK(gba->cpu.gprs[2] == 7);
        gba->cpu.instructionObserver = NULL;
        cupid_gba_step(gba);
        CHECK(record.calls == 1);
        ++observations;
        destroy(gba);
    }
    CHECK(observations == 2);
}
static bool predicate(unsigned condition, unsigned flags) {
    bool n = (flags & 8) != 0, z = (flags & 4) != 0, c = (flags & 2) != 0, v = (flags & 1) != 0;
    switch (condition) {
    case 0: return z; case 1: return !z; case 2: return c; case 3: return !c;
    case 4: return n; case 5: return !n; case 6: return v; case 7: return !v;
    case 8: return c && !z; case 9: return !c || z; case 10: return n == v;
    case 11: return n != v; case 12: return !z && n == v; case 13: return z || n != v;
    case 14: return true; default: return false;
    }
}
static void execution(void) {
    for (unsigned timed = 0; timed < 2; ++timed) {
        struct CupidGBA* gba = machine(timed != 0);
        struct Count record = {0};
        gba->cpu.instructionObserver = count;
        gba->cpu.instructionObserverContext = &record;
        for (unsigned flags = 0; flags < 16; ++flags) for (unsigned cond = 0; cond < 16; ++cond) {
            const uint32_t code[] = {cond << 28 | 0x03A02007, 0xE1A00000, 0xEAFFFFFE};
            program(gba, code, sizeof(code), false);
            gba->cpu.cpsr.packed = (int32_t)(flags << 28 | MODE_SYS);
            gba->cpu.gprs[2] = 4;
            unsigned before = record.calls;
            cupid_gba_step(gba);
            CHECK(record.calls == before + 1 && record.pc == CODE && record.mode == MODE_ARM);
            CHECK(record.opcode == code[0] && record.r2 == 4);
            CHECK(gba->cpu.gprs[2] == (predicate(cond, flags) ? 7 : 4));
            ++observations;
        }
        const uint16_t thumb[] = {0x2207, 0x46C0, 0xE7FE};
        program(gba, thumb, sizeof(thumb), true);
        gba->cpu.gprs[2] = 4;
        unsigned before = record.calls;
        cupid_gba_step(gba);
        CHECK(record.calls == before + 1 && record.pc == CODE && record.mode == MODE_THUMB);
        CHECK(record.opcode == 0x2207 && record.r2 == 4 && gba->cpu.gprs[2] == 7);
        ++observations;
        /* Physical alignment is independent of raw R15's logical low bits. */
        for (unsigned thumbMode = 0; thumbMode < 2; ++thumbMode) for (unsigned low = 0; low < 4; ++low) {
            gba->cpu.executionMode = thumbMode ? MODE_THUMB : MODE_ARM;
            gba->cpu.gprs[ARM_PC] = (int32_t)(CODE + (thumbMode ? 2 : 4) + low);
            CHECK(cupid_arm_instruction_address(&gba->cpu) == ((CODE + low) & ~(thumbMode ? 1u : 3u)));
            ++observations;
        }
        destroy(gba);
    }
    CHECK(observations == 530);
}
static void timer_irq(struct CupidGBA* gba) {
    cupid_io_write(gba, REG_IE, IRQ_TIMER0);
    cupid_io_write(gba, REG_IME, 1);
    cupid_io_write(gba, REG_TM0CNT_L, 0xFFFF);
    cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
    for (unsigned clocks = 0; clocks < 32 && !gba->irq.line; ++clocks) cupid_timing_tick(&gba->timing, 1);
    CHECK(gba->irq.line && (gba->irqf & IRQ_TIMER0));
}
static void arbitration(void) {
    for (unsigned timed = 0; timed < 2; ++timed) for (unsigned thumb = 0; thumb < 2; ++thumb) {
        struct CupidGBA* gba = machine(timed != 0);
        const uint32_t arm[] = {0xE3A02007, 0xE1A00000, 0xEAFFFFFE};
        const uint16_t small[] = {0x2207, 0x46C0, 0xE7FE};
        program(gba, thumb ? (const void*)small : (const void*)arm, thumb ? sizeof(small) : sizeof(arm), thumb != 0);
        struct Count record = {0};
        gba->cpu.instructionObserver = count;
        gba->cpu.instructionObserverContext = &record;
        gba->stopped = true;
        cupid_gba_step(gba);
        CHECK(!record.calls);
        gba->stopped = false;
        gba->cpu.halted = 1;
        gba->cpu.nextEvent = 8;
        cupid_gba_step(gba);
        CHECK(!record.calls && gba->cpu.halted);
        gba->cpu.halted = 0;
        const uint32_t handler[] = {0xE3A03063, 0xEAFFFFFE, 0xE1A00000};
        memcpy(gba->memory.bios + BASE_IRQ, handler, sizeof(handler));
        timer_irq(gba);
        cupid_gba_step(gba);
        CHECK(record.calls == 1 && record.pc == BASE_IRQ && record.mode == MODE_ARM && record.opcode == handler[0]);
        CHECK(gba->cpu.privilegeMode == MODE_IRQ && gba->cpu.gprs[3] == 99 && gba->cpu.gprs[2] != 7);
        ++observations;
        destroy(gba);
    }
    CHECK(observations == 4);
}

static void expectations(struct CupidIssueExpectation* expected, bool thumb) {
    const uint32_t addresses[] = {CODE, CODE + (thumb ? 4u : 8u), CODE + (thumb ? 8u : 16u), CODE + (thumb ? 10u : 20u)};
    const uint32_t opcodes[] = {thumb ? 0x2903u : 0xE3510003u, thumb ? 0x46C0u : 0xE1A00000u,
        thumb ? 0x46C0u : 0xE92D0003u, thumb ? 0xE7FEu : 0xEAFFFFFEu};
    for (unsigned i = 0; i < 4; ++i) expected[i] = (struct CupidIssueExpectation){
        .address=addresses[i], .opcode=opcodes[i], .value=i < 2 ? 3u : 0, .reg=i < 2 ? 1u : 12u,
        .mode=thumb ? MODE_THUMB : MODE_ARM, .privilege=MODE_SYS};
}
static struct CupidGBA* owned(bool timed, bool thumb, bool changed) {
    struct CupidGBA* gba = machine(timed);
    const uint32_t arm[] = {0xE3510003, 0x1A000000, 0xE1A00000, 0xE3A0C000, 0xE92D0003, 0xEAFFFFFE, 0xE1A00000};
    const uint16_t small[] = {0x2903, 0xD100, 0x46C0, 0x2200, 0x46C0, 0xE7FE, 0x46C0};
    program(gba, thumb ? (const void*)small : (const void*)arm, thumb ? sizeof(small) : sizeof(arm), thumb);
    gba->cpu.gprs[1] = changed ? 2 : 3;
    gba->cpu.gprs[12] = 0;
    return gba;
}
static unsigned run_owned(struct CupidGBA* gba, bool thumb) {
    uint32_t terminal = CODE + (thumb ? 10u : 20u);
    for (unsigned steps = 1; steps <= 32; ++steps) {
        uint32_t before = cupid_arm_instruction_address(&gba->cpu);
        cupid_gba_step(gba);
        if (before == terminal && cupid_arm_instruction_address(&gba->cpu) == terminal) {
            CHECK(gba->cpu.prefetch[0] == (thumb ? 0xE7FEu : 0xEAFFFFFEu) && gba->cpu.gprs[12] == 0);
            return steps;
        }
    }
    CHECK(false);
    return 0;
}
static void guards(void) {
    for (unsigned timed = 0; timed < 2; ++timed) for (unsigned thumb = 0; thumb < 2; ++thumb)
      for (unsigned variant = 0; variant < 10; ++variant) {
        struct CupidGBA* gba = owned(timed != 0, thumb != 0, variant == 1);
        struct CupidIssueExpectation expected[4];
        expectations(expected, thumb != 0);
        if (variant == 2) expected[0].value ^= 1;
        if (variant == 3) expected[0].opcode ^= 1;
        if (variant == 4) expected[0].mode = thumb ? MODE_ARM : MODE_THUMB;
        if (variant == 5) expected[0].privilege = MODE_IRQ;
        if (variant == 6) expected[0].address += 0x200; /* Missing comparison with a zero final result. */
        if (variant == 7) { struct CupidIssueExpectation swap = expected[0]; expected[0] = expected[1]; expected[1] = swap; }
        if (variant == 8) expected[2].opcode ^= 1;
        if (variant == 9) expected[3].value = 1;
        struct CupidIssueProbe probe = {.expected=expected, .count=4};
        gba->cpu.instructionObserver = cupid_test_observe_issue;
        gba->cpu.instructionObserverContext = &probe;
        run_owned(gba, thumb != 0);
        CHECK(probe.checkpoint && probe.terminal);
        CHECK(cupid_test_issue_probe_passed(&probe) == (variant == 0));
        if (!variant) {
            cupid_gba_step(gba); /* A repeated terminal cannot satisfy an ordered record twice. */
            CHECK(!cupid_test_issue_probe_passed(&probe));
        }
        ++observations;
        destroy(gba);
      }
    for (unsigned timed = 0; timed < 2; ++timed) {
        struct CupidGBA* gba = owned(timed != 0, false, false);
        struct CupidIssueExpectation expected[4];
        expectations(expected, false);
        struct CupidIssueProbe probe = {.expected=expected, .count=4};
        gba->cpu.instructionObserver = cupid_test_observe_issue;
        gba->cpu.instructionObserverContext = &probe;
        /* IRQ LR points past the interrupted instruction. This owned handler
         * masks further IRQs and returns through MOVS PC,LR, bypassing CMP.
         * Both original zero-result boundaries remain reachable. */
        const uint32_t handler[] = {0xE3833080, 0xE16FF003, 0xE1B0F00E, 0xE1A00000};
        memcpy(gba->memory.bios + BASE_IRQ, handler, sizeof(handler));
        gba->cpu.cpsr.z = 1;
        gba->cpu.gprs[3] = gba->cpu.cpsr.packed;
        timer_irq(gba);
        run_owned(gba, false);
        CHECK(probe.checkpoint && probe.terminal && probe.seen == 3 && !cupid_test_issue_probe_passed(&probe));
        ++observations;
        destroy(gba);
    }
    CHECK(observations == 42);
}

static void equal_sample(const struct CupidGBA* a, const struct CupidGBA* b) {
    CHECK(!memcmp(a->cpu.gprs, b->cpu.gprs, sizeof(a->cpu.gprs)));
    CHECK(!memcmp(a->cpu.bankedRegisters, b->cpu.bankedRegisters, sizeof(a->cpu.bankedRegisters)));
    CHECK(!memcmp(a->cpu.bankedSPSRs, b->cpu.bankedSPSRs, sizeof(a->cpu.bankedSPSRs)));
    CHECK(a->cpu.cpsr.packed == b->cpu.cpsr.packed && a->cpu.spsr.packed == b->cpu.spsr.packed);
    CHECK(!memcmp(a->cpu.prefetch, b->cpu.prefetch, sizeof(a->cpu.prefetch)));
    CHECK(a->cpu.cycles == b->cpu.cycles && a->cpu.nextEvent == b->cpu.nextEvent);
    CHECK(a->cpu.executionMode == b->cpu.executionMode && a->cpu.privilegeMode == b->cpu.privilegeMode);
    CHECK(a->cpu.irqDisableLatch == b->cpu.irqDisableLatch && a->cpu.halted == b->cpu.halted);
    CHECK(!memcmp(a->memory.iwram, b->memory.iwram, GBA_SIZE_IWRAM));
    CHECK(!memcmp(a->memory.ewram, b->memory.ewram, GBA_SIZE_EWRAM));
    CHECK(!memcmp(a->memory.io, b->memory.io, GBA_SIZE_IO));
    CHECK(cupid_timing_global_time(&a->timing) == cupid_timing_global_time(&b->timing));
}
static void passivity(void) {
    for (unsigned timed = 0; timed < 2; ++timed) for (unsigned thumb = 0; thumb < 2; ++thumb)
      for (unsigned changed = 0; changed < 2; ++changed) {
        struct CupidGBA* passive = owned(timed != 0, thumb != 0, changed != 0);
        struct CupidGBA* observed = owned(timed != 0, thumb != 0, changed != 0);
        struct CupidIssueExpectation expected[4];
        expectations(expected, thumb != 0);
        struct CupidIssueProbe probe = {.expected=expected, .count=4};
        observed->cpu.instructionObserver = cupid_test_observe_issue;
        observed->cpu.instructionObserverContext = &probe;
        for (unsigned steps = 0; steps < 16; ++steps) {
            equal_sample(passive, observed);
            cupid_gba_step(passive);
            cupid_gba_step(observed);
            equal_sample(passive, observed);
            ++observations;
        }
        destroy(passive); destroy(observed);
      }
    CHECK(observations == 128);
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "lifecycle")) RUN(lifecycle);
    else if (!strcmp(argv[1], "execution")) RUN(execution);
    else if (!strcmp(argv[1], "arbitration")) RUN(arbitration);
    else if (!strcmp(argv[1], "guards")) RUN(guards);
    else if (!strcmp(argv[1], "passivity")) RUN(passivity);
    else CHECK(false);
    printf("Observer observations: %u\n", observations);
    return 0;
}
