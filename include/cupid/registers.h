/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_REGISTERS_H
#define CUPID_REGISTERS_H

#include "cupid/arm.h"

/* Only registers with distinct current and user storage have two drivers.
 * Bank switching itself copies architectural state, not operand-port values. */
static inline int32_t* CupidUserRegister(struct CupidARM* cpu, unsigned reg) {
    if (cpu->privilegeMode == MODE_USR || cpu->privilegeMode == MODE_SYS) return NULL;
    if (reg >= 13 && reg <= 14) return &cpu->bankedRegisters[BANK_NONE][reg - 13];
    if (reg >= 8 && reg <= 12 && cpu->privilegeMode == MODE_FIQ)
        return &cpu->bankedRegisters[BANK_NONE][reg - 6];
    return NULL;
}

static inline int32_t CupidReadRegister(struct CupidARM* cpu, unsigned reg) {
    int32_t value = cpu->gprs[reg];
    if (cpu->userBankActive) {
        int32_t* user = CupidUserRegister(cpu, reg);
        if (user) value |= *user;
    }
    return value;
}

static inline void CupidWriteRegister(struct CupidARM* cpu, unsigned reg, int32_t value) {
    /* The read-port conflict does not select the user bank for writeback. */
    cpu->gprs[reg] = value;
}

/* A data or internal phase releases the extra register-bank selection. Wait
 * clocks on the instruction bus do not issue another CPU operand phase. */
static inline void CupidRegisterDataPhase(struct CupidARM* cpu) {
    cpu->userBankActive = false;
}

#endif
