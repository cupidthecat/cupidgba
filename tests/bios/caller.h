/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_TEST_BIOS_CALLER_H
#define CUPID_TEST_BIOS_CALLER_H

#include "cupid/gba.h"

/* Service fixtures inject application code without executing firmware boot.
 * Supply its caller stacks and mode without changing clocks or peripherals. */
static inline void cupid_test_bios_caller(struct CupidGBA* gba) {
    cupid_arm_set_priv_mode(&gba->cpu, MODE_IRQ);
    gba->cpu.gprs[ARM_SP] = GBA_SP_BASE_IRQ;
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SVC);
    gba->cpu.gprs[ARM_SP] = GBA_SP_BASE_SUPERVISOR;
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SYS);
    gba->cpu.gprs[ARM_SP] = GBA_SP_BASE_SYSTEM;
    gba->cpu.cpsr.packed = MODE_SYS;
    gba->cpu.irqDisableLatch = false;
}

#endif
