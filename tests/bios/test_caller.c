/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../test.h"
#include "caller.h"
#include "cupid/isa-inlines.h"

struct Trace {
    uint32_t pc[16], opcode[16], r0[16];
    unsigned count;
};

static void observe(const struct CupidARM *cpu, void *context) {
    struct Trace *trace = context;
    CHECK(trace->count < 16);
    unsigned slot = trace->count++;
    trace->pc[slot] = cupid_arm_instruction_address(cpu);
    trace->opcode[slot] = cpu->prefetch[0];
    trace->r0[slot] = (uint32_t)cpu->gprs[0];
}

static struct CupidGBA *machine(bool thumb, bool external, struct Trace *trace) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_test_bios_caller(gba);
    /* Add, store, add, then repeat the last branch. Every skipped or extra
     * instruction changes the trace, clock, register or destination checks. */
    const uint32_t arm[] = {0xE2800001, 0xE5810000, 0xE2800002, 0xEAFFFFFE};
    const uint16_t narrow[] = {0x3001, 0x6008, 0x3002, 0xE7FE};
    memcpy(gba->memory.iwram, thumb ? (const void *)narrow : (const void *)arm,
           thumb ? sizeof(narrow) : sizeof(arm));
    gba->cpu.gprs[0] = 0;
    gba->cpu.gprs[1] = GBA_BASE_EWRAM;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb)
        CupidThumbWritePC(&gba->cpu);
    else
        CupidARMWritePC(&gba->cpu);
    gba->cpu.instructionObserver = observe;
    gba->cpu.instructionObserverContext = trace;
    gba->externalExecution = external;
    cupid_io_write(gba, REG_TM0CNT_L, 0xFFF0);
    cupid_io_write(gba, REG_TM0CNT_H, 0x80);
    return gba;
}

static bool repeated(struct CupidGBA *gba, bool thumb, uint32_t pc, unsigned limit) {
    for (unsigned step = 0; step < limit; step++) {
        cupid_gba_step(gba);
        if (gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
            (uint32_t)gba->cpu.gprs[ARM_PC] == pc)
            return true;
    }
    return false;
}

static void same_state(struct CupidGBA *a, struct CupidGBA *b) {
    CHECK(!memcmp(a->cpu.gprs, b->cpu.gprs, sizeof(a->cpu.gprs)));
    CHECK(!memcmp(a->cpu.prefetch, b->cpu.prefetch, sizeof(a->cpu.prefetch)));
    CHECK(a->cpu.cpsr.packed == b->cpu.cpsr.packed);
    CHECK(a->cpu.spsr.packed == b->cpu.spsr.packed);
    CHECK(a->cpu.cycles == b->cpu.cycles && a->cpu.nextEvent == b->cpu.nextEvent);
    CHECK(a->cpu.executionMode == b->cpu.executionMode);
    CHECK(a->cpu.privilegeMode == b->cpu.privilegeMode);
    CHECK(a->cpuInstructionPending == b->cpuInstructionPending);
    CHECK(cupid_timing_global_time(&a->timing) == cupid_timing_global_time(&b->timing));
    CHECK(cupid_io_read(a, REG_TM0CNT_L) == cupid_io_read(b, REG_TM0CNT_L));
    CHECK(!memcmp(a->memory.ewram, b->memory.ewram, GBA_SIZE_EWRAM));
    CHECK(!memcmp(a->memory.iwram, b->memory.iwram, GBA_SIZE_IWRAM));
    CHECK(a->memory.openBus == b->memory.openBus);
    CHECK(a->memory.iwramBus == b->memory.iwramBus);
    CHECK(a->memory.inputBusValid == b->memory.inputBusValid);
    CHECK(a->video.frameCounter == b->video.frameCounter);
    CHECK(a->audio.sampleWritePos == b->audio.sampleWritePos);
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

int main(void) {
    const unsigned offsets[] = {0, 2, 4, 6, 8, 12, 16, 0x100};
    unsigned scenes = 0;
    for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned expectedThumb = 0; expectedThumb < 2; expectedThumb++)
            for (unsigned external = 0; external < 2; external++)
                for (unsigned target = 0; target < sizeof(offsets) / sizeof(*offsets); target++)
                    for (unsigned limit = 0; limit <= 8; limit++) {
                        struct Trace traces[2] = {0};
                        struct CupidGBA *a = machine(thumb, external, &traces[0]);
                        struct CupidGBA *b = machine(thumb, external, &traces[1]);
                        uint32_t pc = GBA_BASE_IWRAM + offsets[target];
                        bool expected = repeated(a, expectedThumb, pc, limit);
                        CHECK(cupid_test_bios_run_to_return(b, expectedThumb, pc, limit) ==
                              expected);
                        CHECK(!memcmp(traces, traces + 1, sizeof(*traces)));
                        same_state(a, b);
                        destroy(a);
                        destroy(b);
                        ++scenes;
                    }
    CHECK(scenes == 576);
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct Trace trace = {0};
        struct CupidGBA *gba = machine(thumb, false, &trace);
        uint32_t initial = (uint32_t)gba->cpu.gprs[ARM_PC];
        CHECK(!cupid_test_bios_run_to_return(gba, thumb, initial, 0));
        CHECK(!trace.count);
        gba->stopped = true;
        CHECK(!cupid_test_bios_run_to_return(gba, thumb, initial, 8));
        CHECK(!trace.count);
        destroy(gba);
    }
    printf("Verified %u BIOS return-boundary pairs and zero-budget/stopped controls\n", scenes);
    return 0;
}
