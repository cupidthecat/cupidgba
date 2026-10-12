/**
 * @file isa-inlines.h
 * @brief Shared inline helpers and macros for ARM/Thumb ISA implementations.
 */

#ifndef CUPID_ISA_INLINES_H
#define CUPID_ISA_INLINES_H

#include "cupid/arm.h"

/** @name Compiler / Portability Helpers */

#ifndef UNLIKELY
#define UNLIKELY(X) __builtin_expect(!!(X), 0)
#endif

#ifndef ATTRIBUTE_UNUSED
#define ATTRIBUTE_UNUSED __attribute__((unused))
#endif

#ifndef ATTRIBUTE_NOINLINE
#define ATTRIBUTE_NOINLINE __attribute__((noinline))
#endif

#ifndef UNUSED
#define UNUSED(V) (void)(V)
#endif

/** @name PSR Constants */

#define PSR_USER_MASK   0xF0000000
#define PSR_PRIV_MASK   0x000000CF
#define PSR_STATE_MASK  0x00000020

/** @name Condition Code Macros */

#define ARM_COND_EQ (cpu->cpsr.z)
#define ARM_COND_NE (!cpu->cpsr.z)
#define ARM_COND_CS (cpu->cpsr.c)
#define ARM_COND_CC (!cpu->cpsr.c)
#define ARM_COND_MI (cpu->cpsr.n)
#define ARM_COND_PL (!cpu->cpsr.n)
#define ARM_COND_VS (cpu->cpsr.v)
#define ARM_COND_VC (!cpu->cpsr.v)
#define ARM_COND_HI (cpu->cpsr.c && !cpu->cpsr.z)
#define ARM_COND_LS (!cpu->cpsr.c || cpu->cpsr.z)
#define ARM_COND_GE (!cpu->cpsr.n == !cpu->cpsr.v)
#define ARM_COND_LT (!cpu->cpsr.n != !cpu->cpsr.v)
#define ARM_COND_GT (!cpu->cpsr.z && !cpu->cpsr.n == !cpu->cpsr.v)
#define ARM_COND_LE (cpu->cpsr.z || !cpu->cpsr.n != !cpu->cpsr.v)
#define ARM_COND_AL 1

/** @name Bit / Sign Helpers */

#define ARM_SIGN(I) ((uint32_t)(I) >> 31)
#define ARM_SXT_8(I)  ((int32_t)(int8_t)(I))
#define ARM_SXT_16(I) ((int32_t)(int16_t)(I))
#define ARM_UXT_64(I) (uint64_t)(uint32_t)(I)

/** @name Flag Computation Helpers */

#define ARM_CARRY_FROM(M, N, D) \
	(((uint32_t)(M) >> 31) + ((uint32_t)(N) >> 31) > ((uint32_t)(D) >> 31))
#define ARM_BORROW_FROM(M, N, D) \
	(((uint32_t)(M)) >= ((uint32_t)(N)))
#define ARM_BORROW_FROM_CARRY(M, N, D, C) \
	(ARM_UXT_64(M) >= ARM_UXT_64(N) + (uint64_t)(C))
#define ARM_V_ADDITION(M, N, D) \
	(!(ARM_SIGN((M) ^ (N))) && (ARM_SIGN((M) ^ (D))))
#define ARM_V_SUBTRACTION(M, N, D) \
	((ARM_SIGN((M) ^ (N))) && (ARM_SIGN((M) ^ (D))))

/** @name Exception Macros */

#define ARM_ILL cpu->irqh.hitIllegal(cpu, opcode)
#define ARM_STUB cpu->irqh.hitStub(cpu, opcode)

/** @name PC Write Helpers */

static inline int32_t CupidARMWritePC(struct CupidARM* cpu) {
	uint32_t pc = cpu->gprs[ARM_PC] & ~(uint32_t)(WORD_SIZE_ARM - 1);
	cpu->memory.setActiveRegion(cpu, pc);
	if (cpu->memory.timed) cpu->memory.fetch(cpu, WORD_SIZE_ARM, false);
	LOAD_32(cpu->prefetch[0], pc & cpu->memory.activeMask, cpu->memory.activeRegion);
	if (cpu->memory.timed) cpu->memory.finish(cpu);
	pc += WORD_SIZE_ARM;
	if (cpu->memory.timed) cpu->memory.fetch(cpu, WORD_SIZE_ARM, true);
	LOAD_32(cpu->prefetch[1], pc & cpu->memory.activeMask, cpu->memory.activeRegion);
	if (cpu->memory.timed) cpu->memory.finish(cpu);
	cpu->gprs[ARM_PC] = pc;
	cpu->irqDisableLatch = cpu->cpsr.i;
	return cpu->memory.timed ? 0 : 2 + (int)cpu->memory.activeNonseqCycles32 + (int)cpu->memory.activeSeqCycles32;
}

static inline int32_t CupidThumbWritePC(struct CupidARM* cpu) {
	uint32_t pc = cpu->gprs[ARM_PC] & ~(uint32_t)(WORD_SIZE_THUMB - 1);
	cpu->memory.setActiveRegion(cpu, pc);
	if (cpu->memory.timed) cpu->memory.fetch(cpu, WORD_SIZE_THUMB, false);
	LOAD_16(cpu->prefetch[0], pc & cpu->memory.activeMask, cpu->memory.activeRegion);
	if (cpu->memory.timed) cpu->memory.finish(cpu);
	pc += WORD_SIZE_THUMB;
	if (cpu->memory.timed) cpu->memory.fetch(cpu, WORD_SIZE_THUMB, true);
	LOAD_16(cpu->prefetch[1], pc & cpu->memory.activeMask, cpu->memory.activeRegion);
	if (cpu->memory.timed) cpu->memory.finish(cpu);
	cpu->gprs[ARM_PC] = pc;
	cpu->irqDisableLatch = cpu->cpsr.i;
	return cpu->memory.timed ? 0 : 2 + (int)cpu->memory.activeNonseqCycles16 + (int)cpu->memory.activeSeqCycles16;
}

/* Complete a load's internal cycle before refilling a branch target. */
static inline void CupidFlushCycles(struct CupidARM* cpu, int* cycles) {
	if (cpu->memory.timed) {
		cpu->cycles += *cycles;
		*cycles = 0;
		cpu->memory.finish(cpu);
	}
}

/** @name Mode Helpers */

static inline int _CupidModeHasSPSR(unsigned mode) {
	return mode != MODE_SYS && mode != MODE_USR;
}

static inline void _CupidSetMode(struct CupidARM* cpu, int executionMode) {
	if (executionMode == (int)cpu->executionMode) return;
	cpu->executionMode = executionMode;
	if (executionMode == MODE_ARM) {
		cpu->cpsr.t = 0;
		cpu->memory.activeMask &= ~2u;
	} else {
		cpu->cpsr.t = 1;
		cpu->memory.activeMask |= 2u;
	}
	cpu->nextEvent = cpu->cycles;
}

static inline void _CupidReadCPSR(struct CupidARM* cpu) {
	_CupidSetMode(cpu, cpu->cpsr.t);
	cupid_arm_set_priv_mode(cpu, cpu->cpsr.priv);
	cpu->irqh.readCPSR(cpu);
}

/** @name Multiply Wait Macros */

#define ARM_WAIT_SMUL(R, WAIT) \
	{ \
		int32_t wait = WAIT; \
		if (((R) & 0xFFFFFF00) == 0xFFFFFF00 || !((R) & 0xFFFFFF00)) { wait += 1; } \
		else if (((R) & 0xFFFF0000) == 0xFFFF0000 || !((R) & 0xFFFF0000)) { wait += 2; } \
		else if (((R) & 0xFF000000) == 0xFF000000 || !((R) & 0xFF000000)) { wait += 3; } \
		else { wait += 4; } \
		if (cpu->memory.stall) { currentCycles += cpu->memory.stall(cpu, wait); } \
		else { currentCycles += wait; } \
	}

#define ARM_WAIT_UMUL(R, WAIT) \
	{ \
		int32_t wait = WAIT; \
		if (!((R) & 0xFFFFFF00)) { wait += 1; } \
		else if (!((R) & 0xFFFF0000)) { wait += 2; } \
		else if (!((R) & 0xFF000000)) { wait += 3; } \
		else { wait += 4; } \
		if (cpu->memory.stall) { currentCycles += cpu->memory.stall(cpu, wait); } \
		else { currentCycles += wait; } \
	}

#endif /* CUPID_ISA_INLINES_H */
