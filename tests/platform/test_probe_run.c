/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../support/probe_run.h"
#include "../test.h"
#include "cupid/isa-inlines.h"

enum { TERMINAL = GBA_BASE_IWRAM + 0x1000 };

static const uint64_t epochs[] = {0, UINT64_C(0x7FFFFFFE), UINT64_C(0xFFFFFFFE),
                                  UINT64_C(0x2FFFFFFFE)};

static const enum CupidPrivMode modes[] = {MODE_USR, MODE_FIQ, MODE_IRQ, MODE_SVC,
                                           MODE_ABT, MODE_UND, MODE_SYS};

struct Fixture {
    struct CupidTestProbeRun run;
    unsigned issues;
};

static void record(const struct CupidARM *cpu, void *context) {
    struct Fixture *fixture = context;
    ++fixture->issues;
    if (cupid_arm_instruction_address(cpu) == TERMINAL)
        fixture->run.terminalIssued = true;
}

static struct CupidGBA *machine(void) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    return gba;
}

static void reset(struct CupidGBA *gba, struct Fixture *fixture, uint64_t epoch, unsigned a1,
                  enum CupidPrivMode mode, unsigned flags, unsigned masks) {
    gba->cpu.instructionObserver = NULL;
    gba->cpu.instructionObserverContext = NULL;
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    cupid_video_reset(&gba->video);
    cupid_audio_reset(&gba->audio);
    gba->audio.sampleOrigin = gba->audio.lastSample = epoch;
    gba->audio.nextSample = epoch + 512;
    cupid_irq_init(gba);
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    gba->cpu.cpsr.packed = (int32_t)((flags << 28) | (masks << 6) | mode);
    gba->cpu.irqDisableLatch = (masks & 2) != 0;
    const uint32_t code[] = {0xEAFFFFFE, 0xE1A00000, 0xE1A00000};
    memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
    _CupidSetMode(&gba->cpu, MODE_ARM);
    gba->cpu.gprs[ARM_PC] = TERMINAL + (int32_t)a1;
    CupidARMWritePC(&gba->cpu);
    memset(fixture, 0, sizeof(*fixture));
    fixture->run = cupid_test_probe_begin(gba, TERMINAL, 3, epoch + 3 * VIDEO_TOTAL_LENGTH);
    gba->cpu.instructionObserver = record;
    gba->cpu.instructionObserverContext = fixture;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void predicates(void) {
    struct CupidGBA *gba = machine();
    struct Fixture fixture;
    unsigned scenes = 0;
    for (unsigned e = 0; e < 4; ++e) {
        reset(gba, &fixture, epochs[e], 0, MODE_SYS, 0, 3);
        uint64_t clock = cupid_timing_global_time(&gba->timing);
        for (unsigned invalid = 0; invalid < 256; ++invalid) {
            /* Independent host-gate controls complement the reachable execution
             * cases below. Keep each other predicate valid when toggling one. */
            bool unissued = invalid & 1, pending = invalid & 2, thumb = invalid & 4;
            bool wrongPC = invalid & 8, wrongOpcode = invalid & 16, rejected = invalid & 32;
            bool frameLimit = invalid & 64, clockLimit = invalid & 128;
            gba->video.frameCounter = UINT32_MAX;
            struct CupidTestProbeRun run = cupid_test_probe_begin(gba, TERMINAL, 2, clock + 1);
            run.terminalIssued = !unissued;
            run.rejected = rejected;
            if (frameLimit)
                gba->video.frameCounter += 2;
            if (clockLimit)
                run.clockLimit = clock;
            gba->cpuInstructionPending = pending;
            gba->cpu.executionMode = thumb ? MODE_THUMB : MODE_ARM;
            gba->cpu.gprs[ARM_PC] = TERMINAL + (thumb ? 2 : 4) + (wrongPC ? 4 : 0);
            gba->cpu.prefetch[0] = wrongOpcode ? 0xE1A00000 : 0xEAFFFFFE;
            gba->frameReady = !frameLimit;
            bool more = cupid_test_probe_boundary(gba, 0, &run);
            CHECK(run.terminalCompleted == (invalid == 0));
            CHECK(more == (!rejected && !frameLimit && !clockLimit && (unissued || pending)));
            CHECK(run.frames == (frameLimit ? 2u : 0u) && !run.steps && !gba->frameReady);
            ++scenes;
        }
    }
    CHECK(scenes == 1024);
    destroy(gba);
    printf("PASS %u terminal/deadline predicate combinations and wrapped frame counts\n", scenes);
}

static void execution(void) {
    struct CupidGBA *gba = machine();
    struct Fixture fixture;
    unsigned scenes = 0;
    for (unsigned e = 0; e < 4; ++e)
        for (unsigned a1 = 0; a1 < 4; a1 += 2)
            for (unsigned m = 0; m < 7; ++m)
                for (unsigned flags = 0; flags < 16; ++flags)
                    for (unsigned masks = 0; masks < 4; ++masks) {
                        reset(gba, &fixture, epochs[e], a1, modes[m], flags, masks);
                        CHECK(cupid_test_probe_boundary(gba, 0, &fixture.run));
                        CHECK(!fixture.run.terminalIssued && !fixture.run.terminalCompleted);
                        CHECK(cupid_gba_run_steps(gba, 1, cupid_test_probe_boundary,
                                                  &fixture.run) == 1);
                        CHECK(fixture.issues == 1 && fixture.run.steps == 1);
                        CHECK(fixture.run.terminalIssued && fixture.run.terminalCompleted);
                        CHECK(!gba->cpuInstructionPending && gba->cpu.prefetch[0] == 0xEAFFFFFE);
                        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == TERMINAL + a1 + 4);
                        CHECK((uint32_t)gba->cpu.cpsr.packed ==
                              ((flags << 28) | (masks << 6) | modes[m]));
                        ++scenes;
                    }
    CHECK(scenes == 3584);
    reset(gba, &fixture, 0, 0, MODE_SYS, 0, 3);
    uint64_t clock = cupid_timing_global_time(&gba->timing);
    CHECK(cupid_gba_run_steps(gba, 0, cupid_test_probe_boundary, &fixture.run) == 0);
    CHECK(!fixture.issues && !fixture.run.steps && !fixture.run.terminalCompleted);
    CHECK(cupid_timing_global_time(&gba->timing) == clock);
    fixture.run.frameLimit = 0;
    CHECK(cupid_gba_run_steps(gba, 1, cupid_test_probe_boundary, &fixture.run) == 0);
    CHECK(!fixture.issues && !fixture.run.terminalCompleted);
    reset(gba, &fixture, 0, 0, MODE_SYS, 0, 3);
    fixture.run.clockLimit = cupid_timing_global_time(&gba->timing);
    CHECK(cupid_gba_run_steps(gba, 1, cupid_test_probe_boundary, &fixture.run) == 0);
    CHECK(!fixture.issues && !fixture.run.terminalCompleted);
    destroy(gba);
    printf("PASS %u issued terminal scenes, final-step acceptance and empty budgets\n", scenes);
}

static void prepare_dma(struct CupidGBA *gba, struct Fixture *fixture, uint64_t epoch,
                        unsigned wrappedCounter) {
    reset(gba, fixture, epoch, 0, MODE_SYS, 0, 3);
    cupid_timing_tick(&gba->timing, 100000);
    gba->video.frameCounter = wrappedCounter ? UINT32_MAX - 1 : 0;
    fixture->run = cupid_test_probe_begin(gba, TERMINAL, 3, epoch + 3 * VIDEO_TOTAL_LENGTH);
    const uint32_t pattern = UINT32_C(0x19283746);
    memcpy(gba->memory.iwram, &pattern, sizeof(pattern));
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM);
    cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM);
    cupid_dma_write_cnt_lo(gba, 3, 0);
    cupid_dma_write_cnt(gba, 3, 0x8500);
    cupid_timing_tick(&gba->timing, 3);
    CHECK(gba->memory.dmaRunning);
}

static void dma(void) {
    struct CupidGBA *gba = machine();
    struct Fixture fixture;
    unsigned scenes = 0;
    for (unsigned e = 0; e < 4; ++e)
        for (unsigned wrap = 0; wrap < 2; ++wrap) {
            prepare_dma(gba, &fixture, epochs[e], wrap);
            cupid_gba_step(gba);
            uint64_t completedClock = cupid_timing_global_time(&gba->timing);
            CHECK(!gba->cpuInstructionPending && fixture.issues == 1);
            CHECK(gba->video.frameCounter == (wrap ? 0 : 2));
            for (unsigned frameLimit = 1; frameLimit <= 3; ++frameLimit)
                for (int clockMargin = -1; clockMargin <= 1; ++clockMargin) {
                    prepare_dma(gba, &fixture, epochs[e], wrap);
                    fixture.run.frameLimit = frameLimit;
                    fixture.run.clockLimit = completedClock + clockMargin;
                    CHECK(cupid_gba_run_steps(gba, 1, cupid_test_probe_boundary, &fixture.run) ==
                          1);
                    CHECK(fixture.issues == 1 && fixture.run.terminalIssued &&
                          fixture.run.steps == 1);
                    CHECK(fixture.run.frames == 2 && fixture.run.lastFrame == (wrap ? 0 : 2));
                    CHECK(fixture.run.terminalCompleted == (frameLimit == 3 && clockMargin == 1));
                    CHECK(cupid_timing_global_time(&gba->timing) == completedClock);
                    CHECK(!gba->cpuInstructionPending && !gba->memory.dmaRunning);
                    CHECK(gba->cpu.prefetch[0] == 0xEAFFFFFE);
                    /* Inspect bytes without issuing another emulated bus access. */
                    for (unsigned word = 0; word < GBA_SIZE_EWRAM / 4; ++word) {
                        uint32_t value;
                        memcpy(&value, gba->memory.ewram + 4 * word, sizeof(value));
                        CHECK(value == UINT32_C(0x19283746));
                    }
                    ++scenes;
                }
        }
    CHECK(scenes == 72);
    destroy(gba);
    printf("PASS %u DMA-held terminal/deadline scenes with two presentations per return\n", scenes);
}

static void halt_request(struct CupidGBA *gba) {
    /* Supply a BIOS-only HALTCNT strobe in this owned bus fixture. */
    gba->memory.activeRegion = GBA_REGION_BIOS;
    gba->cpu.memory.timed = true;
    cupid_io_write8(gba, REG_HALTCNT, 0);
    gba->cpu.memory.timed = false;
}

static void halt(void) {
    struct CupidGBA *gba = machine();
    struct Fixture fixture;
    for (unsigned e = 0; e < 4; ++e)
        for (unsigned a1 = 0; a1 < 4; a1 += 2) {
            reset(gba, &fixture, epochs[e], a1, MODE_SYS, 0, 3);
            halt_request(gba);
            CHECK(cupid_gba_run_steps(gba, 1, cupid_test_probe_boundary, &fixture.run) == 1);
            CHECK(gba->cpuInstructionPending && gba->cpu.halted && fixture.issues == 1);
            CHECK(fixture.run.terminalIssued && !fixture.run.terminalCompleted);
            CHECK(cupid_test_probe_boundary(gba, 0, &fixture.run));
            CHECK(fixture.run.steps == 1 && !fixture.run.terminalCompleted);
            cupid_io_write(gba, REG_IE, IRQ_KEYPAD);
            cupid_raise_irq(gba, IRQ_KEYPAD, 0);
            cupid_gba_run_steps(gba, 100, cupid_test_probe_boundary, &fixture.run);
            CHECK(!gba->cpuInstructionPending && !gba->cpu.halted && fixture.issues == 1);
            CHECK(fixture.run.terminalCompleted && fixture.run.steps > 1);
            reset(gba, &fixture, epochs[e], a1, MODE_SYS, 0, 3);
            fixture.run.frameLimit = 1;
            halt_request(gba);
            cupid_gba_run_steps(gba, 50000, cupid_test_probe_boundary, &fixture.run);
            CHECK(fixture.issues == 1 && fixture.run.terminalIssued &&
                  !fixture.run.terminalCompleted);
            CHECK(fixture.run.frames == 1 && gba->cpuInstructionPending && gba->cpu.halted);
        }
    destroy(gba);
    puts("PASS eight pending/wake pairs and eight unwoken terminal deadline controls");
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "predicates"))
        predicates();
    else if (!strcmp(argv[1], "execution"))
        execution();
    else if (!strcmp(argv[1], "dma"))
        dma();
    else if (!strcmp(argv[1], "halt"))
        halt();
    else
        return 2;
    return 0;
}
