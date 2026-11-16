/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      caller.h
 * @author    cupidthecat
 * @brief     BIOS caller setup and checked service-return helpers.
 * @date      2026-11-16
 *
 * - Provide cupid_test_bios_caller, cupid_test_bios_return_boundary,
 *   cupid_test_bios_run_to_return.
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
#ifndef CUPID_TEST_BIOS_CALLER_H
#define CUPID_TEST_BIOS_CALLER_H

#include "cupid/gba.h"

/* Service fixtures inject application code without executing firmware boot.
 * Supply its caller stacks and mode without changing clocks or peripherals. */
static inline void cupid_test_bios_caller(struct CupidGBA *gba) {
    cupid_arm_set_priv_mode(&gba->cpu, MODE_IRQ);
    gba->cpu.gprs[ARM_SP] = GBA_SP_BASE_IRQ;
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SVC);
    gba->cpu.gprs[ARM_SP] = GBA_SP_BASE_SUPERVISOR;
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SYS);
    gba->cpu.gprs[ARM_SP] = GBA_SP_BASE_SYSTEM;
    gba->cpu.cpsr.packed = MODE_SYS;
    gba->cpu.irqDisableLatch = false;
}

struct CupidTestBiosReturn {
    uint32_t pc;
    bool thumb, reached;
};

/* The runner visits the initial state and each ordinary step boundary. Ignore
 * the initial visit: the previous fixture loops always executed a step first. */
static inline bool cupid_test_bios_return_boundary(struct CupidGBA *gba, uint64_t steps,
                                                   void *context) {
    struct CupidTestBiosReturn *boundary = context;
    boundary->reached = steps &&
                        gba->cpu.executionMode == (boundary->thumb ? MODE_THUMB : MODE_ARM) &&
                        (uint32_t)gba->cpu.gprs[ARM_PC] == boundary->pc;
    return !boundary->reached;
}

/* Keep the same guest-step budget and post-step return test without switching
 * to the host stack after every instruction. A return on the last step counts. */
static inline bool cupid_test_bios_run_to_return(struct CupidGBA *gba, bool thumb, uint32_t pc,
                                                 uint64_t limit) {
    struct CupidTestBiosReturn boundary = {.pc = pc, .thumb = thumb};
    cupid_gba_run_steps(gba, limit, cupid_test_bios_return_boundary, &boundary);
    return boundary.reached;
}

#endif
