/** @file arm.c
 * @brief ARM7TDMI CPU core: init, reset, step, run loop, exceptions.
 *
 *   - 16-entry condition code LUT indexed by NZCV flags
 *   - Fast-path for condition 0xE (always execute)
 *   - ARMStep / ThumbStep are static inline, called from ARMRun / ARMRunLoop
 *   - Fetch/data callbacks advance bus phases during CPU execution;
 *     instruction handlers retain internal clocks
 *   - ARMWritePC / ThumbWritePC refill the pipeline on branches & exceptions
 *   - Mode switch sets nextEvent = cycles → forces loop exit
 */

#include "cupid/isa-inlines.h"
#include "cupid/gba.h"

/** Condition code lookup table.
 * Each entry is a 16-bit bitmask. Bit index = {N,Z,C,V}.
 * conditionLut[cond] & (1 << flags) → whether condition passes.
 */
static const uint16_t conditionLut[16] = {
	0xF0F0, // EQ  Z set
	0x0F0F, // NE  Z clear
	0xCCCC, // CS  C set
	0x3333, // CC  C clear
	0xFF00, // MI  N set
	0x00FF, // PL  N clear
	0xAAAA, // VS  V set
	0x5555, // VC  V clear
	0x0C0C, // HI  C set and Z clear
	0xF3F3, // LS  C clear or Z set
	0xAA55, // GE  N == V
	0x55AA, // LT  N != V
	0x0A05, // GT  Z clear && N == V
	0xF5FA, // LE  Z set || N != V
	0xFFFF, // AL  always
	0x0000  // NV  never
};

/** Prefetch-cycle cost for a no-op (failed condition or idle fetch) */
#define ARM_PREFETCH_CYCLES  (cpu->memory.timed ? 0 : 1 + (int)cpu->memory.activeSeqCycles32)

/** @name Mode Switching
 *
 * Banked register layout:
 *   bankedRegisters[bank][0] = SP (R13)
 *   bankedRegisters[bank][1] = LR (R14)
 *   bankedRegisters[bank][2] = R8   (only for BANK_NONE / BANK_FIQ)
 *   bankedRegisters[bank][3] = R9
 *   bankedRegisters[bank][4] = R10
 *   bankedRegisters[bank][5] = R11
 *   bankedRegisters[bank][6] = R12
 */

static inline enum CupidRegisterBank _selectBank(enum CupidPrivMode mode) {
	switch (mode) {
	case MODE_USR: case MODE_SYS: return BANK_NONE;
	case MODE_FIQ: return BANK_FIQ;
	case MODE_IRQ: return BANK_IRQ;
	case MODE_SVC: return BANK_SUPERVISOR;
	case MODE_ABT: return BANK_ABORT;
	case MODE_UND: return BANK_UNDEFINED;
	default:       return BANK_NONE;
	}
}

void cupid_arm_set_priv_mode(struct CupidARM* cpu, enum CupidPrivMode mode) {
	if (mode == cpu->privilegeMode) {
		return;
	}

	enum CupidRegisterBank newBank = _selectBank(mode);
	enum CupidRegisterBank oldBank = _selectBank(cpu->privilegeMode);

	if (newBank != oldBank) {
		/** If switching to/from FIQ, also bank R8–R12 */
		if (mode == MODE_FIQ || cpu->privilegeMode == MODE_FIQ) {
			int oldFIQBank = (oldBank == BANK_FIQ);
			int newFIQBank = (newBank == BANK_FIQ);
			cpu->bankedRegisters[oldFIQBank][2] = cpu->gprs[8];
			cpu->bankedRegisters[oldFIQBank][3] = cpu->gprs[9];
			cpu->bankedRegisters[oldFIQBank][4] = cpu->gprs[10];
			cpu->bankedRegisters[oldFIQBank][5] = cpu->gprs[11];
			cpu->bankedRegisters[oldFIQBank][6] = cpu->gprs[12];
			cpu->gprs[8]  = cpu->bankedRegisters[newFIQBank][2];
			cpu->gprs[9]  = cpu->bankedRegisters[newFIQBank][3];
			cpu->gprs[10] = cpu->bankedRegisters[newFIQBank][4];
			cpu->gprs[11] = cpu->bankedRegisters[newFIQBank][5];
			cpu->gprs[12] = cpu->bankedRegisters[newFIQBank][6];
		}
		cpu->bankedRegisters[oldBank][0] = cpu->gprs[ARM_SP];
		cpu->bankedRegisters[oldBank][1] = cpu->gprs[ARM_LR];
		cpu->gprs[ARM_SP] = cpu->bankedRegisters[newBank][0];
		cpu->gprs[ARM_LR] = cpu->bankedRegisters[newBank][1];

		cpu->bankedSPSRs[oldBank] = cpu->spsr.packed;
		cpu->spsr.packed = cpu->bankedSPSRs[newBank];
	}
	cpu->privilegeMode = mode;
}

/** @name Init / Reset */

void cupid_arm_init(struct CupidARM* cpu) {
	memset(cpu->gprs, 0, sizeof(cpu->gprs));
	memset(cpu->bankedRegisters, 0, sizeof(cpu->bankedRegisters));
	memset(cpu->bankedSPSRs, 0, sizeof(cpu->bankedSPSRs));
	cpu->cpsr.packed = 0;
	cpu->irqDisableLatch = false;
	cpu->spsr.packed = 0;
	cpu->cycles = 0;
	cpu->nextEvent = 0;
	cpu->halted = 0;
	cpu->executionMode = MODE_ARM;
	cpu->privilegeMode = MODE_SYS;
	cpu->shifterOperand = 0;
	cpu->shifterCarryOut = 0;
	cpu->prefetch[0] = 0;
	cpu->prefetch[1] = 0;
}

void cupid_arm_reset(struct CupidARM* cpu) {
	int i;
	for (i = 0; i < 16; ++i) {
		cpu->gprs[i] = 0;
	}
	for (i = 0; i < 6; ++i) {
		cpu->bankedRegisters[i][0] = 0;
		cpu->bankedRegisters[i][1] = 0;
		cpu->bankedRegisters[i][2] = 0;
		cpu->bankedRegisters[i][3] = 0;
		cpu->bankedRegisters[i][4] = 0;
		cpu->bankedRegisters[i][5] = 0;
		cpu->bankedRegisters[i][6] = 0;
		cpu->bankedSPSRs[i] = 0;
	}

	/* Reset enters ARM Supervisor mode with both interrupt classes masked. */
	cpu->privilegeMode = MODE_SVC;
	cpu->cpsr.packed = MODE_SVC | 0xC0;
	cpu->irqDisableLatch = true;
	cpu->spsr.packed = 0;

	cpu->shifterOperand = 0;
	cpu->shifterCarryOut = 0;

	/** Start in Thumb then switch to ARM to properly configure activeMask */
	cpu->executionMode = MODE_THUMB;
	_CupidSetMode(cpu, MODE_ARM);

	/** Fill the initial prefetch pipeline from address 0 (reset vector).
	 * Updates the program counter before the reset callback.
	 */
	CupidARMWritePC(cpu);

	cpu->cycles = 0;
	cpu->nextEvent = 0;
	cpu->halted = 0;


	/** Platform-specific reset callback (GBA sets SPs, inits subsystems). **/
	if (cpu->irqh.reset) {
		cpu->irqh.reset(cpu);
	}
}

/** @name Exception Entry
 *
 *   1. Save CPSR
 *   2. Switch privilege mode (banks SP/LR/SPSR)
 *   3. Set cpsr.priv to new mode
 *   4. Calculate LR (return address)
 *   5. Set PC to exception vector
 *   6. Switch to ARM mode
 *   7. cycles += ARMWritePC (refills pipeline)
 *   8. Restore saved CPSR into SPSR
 *   9. Disable IRQ (cpsr.i = 1)
 */

void cupid_arm_raise_irq(struct CupidARM* cpu) {
	if (cpu->cpsr.i) {
		return;
	}
	cupid_arm_take_irq(cpu);
}

void cupid_arm_take_irq(struct CupidARM* cpu) {
	/* IRQ entry discards one instruction fetch before refilling the vector. */
	if (cpu->memory.timed) {
		unsigned width = cpu->executionMode == MODE_ARM ? WORD_SIZE_ARM : WORD_SIZE_THUMB;
		cpu->memory.fetch(cpu, (uint32_t)cpu->gprs[ARM_PC] + width, width, true);
		cpu->memory.finish(cpu);
	} else {
		cpu->cycles += 1 + (cpu->executionMode == MODE_ARM
			? cpu->memory.activeSeqCycles32 : cpu->memory.activeSeqCycles16);
	}
	union CupidPSR cpsr = cpu->cpsr;
	int instructionWidth;
	if (cpu->executionMode == MODE_THUMB) {
		instructionWidth = WORD_SIZE_THUMB;
	} else {
		instructionWidth = WORD_SIZE_ARM;
	}
	cupid_arm_set_priv_mode(cpu, MODE_IRQ);
	cpu->cpsr.priv = MODE_IRQ;
	cpu->gprs[ARM_LR] = cpu->gprs[ARM_PC] - instructionWidth + WORD_SIZE_ARM;
	cpu->gprs[ARM_PC] = BASE_IRQ;
	_CupidSetMode(cpu, MODE_ARM);
	cpu->spsr = cpsr;
	cpu->cpsr.i = 1;
	cpu->cycles += CupidARMWritePC(cpu);
	cpu->halted = 0;
}

void cupid_arm_raise_swi(struct CupidARM* cpu) {
	union CupidPSR cpsr = cpu->cpsr;
	int instructionWidth;
	if (cpu->executionMode == MODE_THUMB) {
		instructionWidth = WORD_SIZE_THUMB;
	} else {
		instructionWidth = WORD_SIZE_ARM;
	}
	cupid_arm_set_priv_mode(cpu, MODE_SVC);
	cpu->cpsr.priv = MODE_SVC;
	cpu->gprs[ARM_LR] = cpu->gprs[ARM_PC] - instructionWidth;
	cpu->gprs[ARM_PC] = BASE_SWI;
	_CupidSetMode(cpu, MODE_ARM);
	cpu->spsr = cpsr;
	cpu->cpsr.i = 1;
	cpu->cycles += CupidARMWritePC(cpu);
}

void cupid_arm_raise_undefined(struct CupidARM* cpu) {
	/* The trap's internal interval precedes the vector's N/S fetches. */
	cpu->cycles += cpu->memory.stall ? cpu->memory.stall(cpu, 1) : 1;
	if (cpu->memory.timed && cpu->memory.finish) cpu->memory.finish(cpu);
	union CupidPSR cpsr = cpu->cpsr;
	int instructionWidth;
	if (cpu->executionMode == MODE_THUMB) {
		instructionWidth = WORD_SIZE_THUMB;
	} else {
		instructionWidth = WORD_SIZE_ARM;
	}
	cupid_arm_set_priv_mode(cpu, MODE_UND);
	cpu->cpsr.priv = MODE_UND;
	cpu->gprs[ARM_LR] = cpu->gprs[ARM_PC] - instructionWidth;
	cpu->gprs[ARM_PC] = BASE_UNDEF;
	_CupidSetMode(cpu, MODE_ARM);
	cpu->spsr = cpsr;
	cpu->cpsr.i = 1;
	cpu->cycles += CupidARMWritePC(cpu);
}

/** @name ARMStep
 *
 * ARMStep — fetch → decode → execute (ARM mode).
 *
 * A timed fetch runs peripheral events before reading the next opcode.
 * Handlers account for internal cycles and invoke data-bus callbacks. Without
 * timing callbacks, handlers and failed conditions use the static fetch cost.
 *
 * Fast-path: condition 0xE (AL) skips the LUT check entirely.
 */

static inline void cupid_arm_step_inline(struct CupidARM* cpu) {
	uint32_t opcode = cpu->prefetch[0];
	cpu->prefetch[0] = cpu->prefetch[1];
	cpu->gprs[ARM_PC] += WORD_SIZE_ARM;
	if (cpu->memory.timed) cpu->memory.fetch(cpu, (uint32_t)cpu->gprs[ARM_PC], WORD_SIZE_ARM, true);
	cpu->prefetch[1] = CupidReadOpcode(cpu, (uint32_t)cpu->gprs[ARM_PC], WORD_SIZE_ARM);
	if (cpu->memory.timed) cpu->memory.finish(cpu);
	cpu->memory.dataSequential = false;

	unsigned condition = opcode >> 28;
	if (condition != 0xE) {
		/** Check condition code via LUT:
		 * flags byte = bits 24-31 of cpsr.packed
		 * flags >> 4 = {N,Z,C,V} as 4-bit index
		 */
		unsigned flags = cpu->cpsr.flags >> 4;
		bool conditionMet = conditionLut[condition] & (1 << flags);
		if (!conditionMet) {
			cpu->cycles += ARM_PREFETCH_CYCLES;
			return;
		}
	}
	CupidARMInstruction instruction = cupid_arm_table[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0x00F)];
	instruction(cpu, opcode);
}

/** @name ThumbStep
 *
 * ThumbStep — fetch → decode → execute (Thumb mode).
 *
 * Thumb instructions are unconditional (no condition field).
 * Bus callbacks handle fetch/data clocks; the handler adds internal cycles.
 */

static inline void cupid_thumb_step_inline(struct CupidARM* cpu) {
	uint32_t opcode = cpu->prefetch[0];
	cpu->prefetch[0] = cpu->prefetch[1];
	cpu->gprs[ARM_PC] += WORD_SIZE_THUMB;
	if (cpu->memory.timed) cpu->memory.fetch(cpu, (uint32_t)cpu->gprs[ARM_PC], WORD_SIZE_THUMB, true);
	cpu->prefetch[1] = CupidReadOpcode(cpu, (uint32_t)cpu->gprs[ARM_PC], WORD_SIZE_THUMB);
	if (cpu->memory.timed) cpu->memory.finish(cpu);
	cpu->memory.dataSequential = false;
	CupidThumbInstruction instruction = cupid_thumb_table[opcode >> 6];
	instruction(cpu, opcode);
}

/** @name Public Step Functions */

void cupid_arm_step(struct CupidARM* cpu) {
	bool previous = cpu->memory.timed;
	cpu->memory.timed = cpu->memory.fetch != NULL;
	cupid_arm_step_inline(cpu);
	if (cpu->memory.timed) cpu->memory.finish(cpu);
	cpu->memory.timed = previous;
}

void cupid_thumb_step(struct CupidARM* cpu) {
	bool previous = cpu->memory.timed;
	cpu->memory.timed = cpu->memory.fetch != NULL;
	cupid_thumb_step_inline(cpu);
	if (cpu->memory.timed) cpu->memory.finish(cpu);
	cpu->memory.timed = previous;
}

/** @name ARMRun
 *
 * ARMRun — execute a single step with event processing.
 *
 *   1. Process any pending events
 *   2. Execute one instruction
 *   3. Process any new events triggered by that instruction
 */

void cupid_arm_run(struct CupidARM* cpu) {
	while (cpu->cycles >= cpu->nextEvent) {
		cpu->irqh.processEvents(cpu);
	}
	if (cpu->executionMode == MODE_THUMB) {
		cupid_thumb_step(cpu);
	} else {
		cupid_arm_step(cpu);
	}
	while (cpu->cycles >= cpu->nextEvent) {
		cpu->irqh.processEvents(cpu);
	}
}

/** @name ARMRunLoop
 *
 * ARMRunLoop — execute until cycles >= nextEvent, then process events.
 *
 * The execution mode is checked once at the top. If a mode switch
 * occurs mid-loop (e.g., BX instruction), _CupidSetMode sets
 * nextEvent = cycles, which naturally breaks the loop. The next
 * call to ARMRunLoop (or processEvents) will recheck the mode.
 */

void cupid_arm_run_loop(struct CupidARM* cpu) {
	if (cpu->executionMode == MODE_THUMB) {
		while (cpu->cycles < cpu->nextEvent) {
			cupid_thumb_step(cpu);
		}
	} else {
		while (cpu->cycles < cpu->nextEvent) {
			cupid_arm_step(cpu);
		}
	}
	cpu->irqh.processEvents(cpu);
}

/** @name ARMRunFake
 *
 * ARMRunFake — inject an opcode into the pipeline (debug/test).
 */

void cupid_arm_run_fake(struct CupidARM* cpu, uint32_t opcode) {
	if (cpu->executionMode == MODE_ARM) {
		cpu->gprs[ARM_PC] -= WORD_SIZE_ARM;
	} else {
		cpu->gprs[ARM_PC] -= WORD_SIZE_THUMB;
	}
	cpu->prefetch[1] = cpu->prefetch[0];
	cpu->prefetch[0] = opcode;
}
