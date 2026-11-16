/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      probe_run.h
 * @author    cupidthecat
 * @brief     Headless probe lifecycle and terminal callback interface.
 * @date      2026-11-16
 *
 * - Provide cupid_test_probe_begin, cupid_test_probe_boundary.
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
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
