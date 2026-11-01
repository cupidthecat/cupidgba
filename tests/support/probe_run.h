/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_TEST_PROBE_RUN_H
#define CUPID_TEST_PROBE_RUN_H

#include "cupid/gba.h"

struct CupidTestProbeRun {
    uint64_t frames, steps, frameLimit, clockLimit;
    uint32_t lastFrame, terminalAddress;
    bool terminalIssued, terminalCompleted, rejected;
};

static inline struct CupidTestProbeRun cupid_test_probe_begin(const struct CupidGBA *gba,
                                                              uint32_t terminal, uint64_t frames,
                                                              uint64_t clockLimit) {
    return (struct CupidTestProbeRun){.frameLimit = frames,
                                      .clockLimit = clockLimit,
                                      .lastFrame = gba->video.frameCounter,
                                      .terminalAddress = terminal};
}

/* A normal CPU return can span several presentations during DMA. Count every
 * completion, including counter wrap, and reject elapsed limits before a zero
 * result can pass. The caller keeps its original ordinary-step budget. */
static inline bool cupid_test_probe_boundary(struct CupidGBA *gba, uint64_t steps, void *context) {
    struct CupidTestProbeRun *run = context;
    if (steps)
        ++run->steps;
    run->frames += (uint32_t)(gba->video.frameCounter - run->lastFrame);
    run->lastFrame = gba->video.frameCounter;
    gba->frameReady = false;
    if (run->rejected || run->frames >= run->frameLimit ||
        cupid_timing_global_time(&gba->timing) >= run->clockLimit)
        return false;
    if (run->terminalIssued && !gba->cpuInstructionPending) {
        /* Issue alone does not complete a HALT-held branch refill. Wait for
         * its ordinary return and require the original self-branch pipeline. */
        run->terminalCompleted = gba->cpu.executionMode == MODE_ARM &&
                                 cupid_arm_instruction_address(&gba->cpu) == run->terminalAddress &&
                                 gba->cpu.prefetch[0] == UINT32_C(0xEAFFFFFE);
        return false;
    }
    return true;
}

#endif
