/**
 * @file arm.h
 * @brief ARM7TDMI CPU core definitions for CupidGBA emulator.
 * 
 * This header defines the complete ARM7TDMI processor core interface, including:
 * - Register file and status register (PSR) layout
 * - Execution modes (ARM/Thumb) and privilege modes (USR, FIQ, IRQ, SVC, ABT, UND, SYS)
 * - Exception vectors and interrupt handling
 * - Memory bus abstraction layer with timing support
 * - CPU state structure and lifecycle management
 * 
 * Supports 2-stage instruction prefetch pipeline, cycle-accurate timing, and banked
 * register management across all ARM privilege modes.
 * 
 */
#ifndef CUPID_ARM_H
#define CUPID_ARM_H

#include "cupid/types.h"

/** @name Forward Declarations */
struct CupidARM;
struct CupidGBA;

/** @name Register Indices */
enum {
    ARM_SP = 13,
    ARM_LR = 14,
    ARM_PC = 15
};

/** @name Execution Modes */
enum CupidExecMode {
    MODE_ARM   = 0,
    MODE_THUMB = 1
};

/** @name Privilege Modes */
enum CupidPrivMode {
    MODE_USR = 0x10,
    MODE_FIQ = 0x11,
    MODE_IRQ = 0x12,
    MODE_SVC = 0x13,
    MODE_ABT = 0x17,
    MODE_UND = 0x1B,
    MODE_SYS = 0x1F
};

/** @name Word Sizes */
enum CupidWordSize {
    WORD_SIZE_ARM   = 4,
    WORD_SIZE_THUMB = 2
};

/** @name Exception Vectors */
enum CupidExecVector {
    BASE_RESET = 0x00000000,
    BASE_UNDEF = 0x00000004,
    BASE_SWI   = 0x00000008,
    BASE_PABT  = 0x0000000C,
    BASE_DABT  = 0x00000010,
    BASE_IRQ   = 0x00000018,
    BASE_FIQ   = 0x0000001C
};

/** @name Register Bank Indices */
enum CupidRegisterBank {
    BANK_NONE       = 0,
    BANK_FIQ        = 1,
    BANK_IRQ        = 2,
    BANK_SUPERVISOR = 3,
    BANK_ABORT      = 4,
    BANK_UNDEFINED  = 5
};

/** @name LDM/STM Directions */
enum CupidLSMDirection {
    LSM_B  = 1,
    LSM_D  = 2,
    LSM_IA = 0,
    LSM_IB = 1,
    LSM_DA = 2,
    LSM_DB = 3
};

/** @name PSR (Program Status Register) */
union CupidPSR {
    struct {
        unsigned priv  : 5;   /**< Privilege mode */
        unsigned t     : 1;   /**< Thumb state */
        unsigned f     : 1;   /**< FIQ disable */
        unsigned i     : 1;   /**< IRQ disable */
        unsigned _res  : 20;
        unsigned v     : 1;   /**< Overflow flag */
        unsigned c     : 1;   /**< Carry flag */
        unsigned z     : 1;   /**< Zero flag */
        unsigned n     : 1;   /**< Negative flag */
    };

    /** Byte-level access */
    struct {
        uint8_t control;    /**< Bits 0-7: priv[4:0], t, f, i */
        uint8_t extension;  /**< Bits 8-15 */
        uint8_t status;     /**< Bits 16-23 */
        uint8_t flags;      /**< Bits 24-31: v, c, z, n */
    };

    int32_t packed;
};

/** @name Memory Access Source */
enum CupidAccessSource {
    CUPID_ACCESS_UNKNOWN  = 0,
    CUPID_ACCESS_PROGRAM  = 1,
    CUPID_ACCESS_DMA      = 2,
    CUPID_ACCESS_SYSTEM   = 3,
    CUPID_ACCESS_DATA     = 4,
    CUPID_ACCESS_PREFETCH = 5
};

/** @name Memory Bus Interface */
struct CupidARMMemory {
    uint32_t (*load32)(struct CupidARM* cpu, uint32_t addr, int* cycleCounter);
    uint32_t (*load16)(struct CupidARM* cpu, uint32_t addr, int* cycleCounter);
    uint32_t (*loadU16)(struct CupidARM* cpu, uint32_t addr, int* cycleCounter);
    uint32_t (*load8)(struct CupidARM* cpu, uint32_t addr, int* cycleCounter);
    void     (*store32)(struct CupidARM* cpu, uint32_t addr, int32_t val, int* cycleCounter);
    void     (*store16)(struct CupidARM* cpu, uint32_t addr, int16_t val, int* cycleCounter);
    void     (*store8)(struct CupidARM* cpu, uint32_t addr, int8_t val, int* cycleCounter);

    /** Block transfer helpers (LDM/STM) */
    uint32_t (*loadMultiple)(struct CupidARM* cpu, uint32_t baseAddr, int mask,
                             enum CupidLSMDirection dir, int* cycleCounter);
    uint32_t (*storeMultiple)(struct CupidARM* cpu, uint32_t baseAddr, int mask,
                              enum CupidLSMDirection dir, int* cycleCounter);

    /** Active memory region for fast prefetch */
    const uint32_t* activeRegion;
    uint32_t activeMask;
    uint32_t activeSeqCycles32;
    uint32_t activeSeqCycles16;
    uint32_t activeNonseqCycles32;
    uint32_t activeNonseqCycles16;

    /** Stall callback (returns adjusted wait cycles) */
    int32_t (*stall)(struct CupidARM* cpu, int32_t wait);

    /** Set the active memory region for prefetch (called on branch) */
    void (*setActiveRegion)(struct CupidARM* cpu, uint32_t addr);
    void (*fetch)(struct CupidARM* cpu, uint32_t address, unsigned width, bool sequential);
    void (*finish)(struct CupidARM* cpu);
    void (*lock)(struct CupidARM* cpu, bool locked);
    bool timed;             /**< CPU execution advances individual bus phases */
    bool dataSequential;

    /** Current access source for timing/debug */
    enum CupidAccessSource accessSource;
};

/** @name Interrupt / Event Handler Callbacks */
struct CupidARMInterruptHandler {
    void (*reset)(struct CupidARM* cpu);
    void (*processEvents)(struct CupidARM* cpu);
    void (*swi16)(struct CupidARM* cpu, int immediate);
    void (*swi32)(struct CupidARM* cpu, int immediate);
    void (*hitIllegal)(struct CupidARM* cpu, uint32_t opcode);
    void (*bkpt16)(struct CupidARM* cpu, int immediate);
    void (*bkpt32)(struct CupidARM* cpu, int immediate);
    void (*readCPSR)(struct CupidARM* cpu);

    void (*hitStub)(struct CupidARM* cpu, uint32_t opcode);
};

/** @name ARM Register File */
#define CUPID_ARM_REGISTER_FILE struct { \
    int32_t gprs[16]; \
    union CupidPSR cpsr; \
    union CupidPSR spsr; \
}

struct CupidARMRegisterFile {
    CUPID_ARM_REGISTER_FILE;
};

/** @name ARM7TDMI CPU Core */
struct CupidARM {
    /** Register file — accessible as cpu->gprs[i] or cpu->regs.gprs[i] */
    union {
        struct CupidARMRegisterFile regs;
        CUPID_ARM_REGISTER_FILE;
    };

    /** Cycle accounting */
    int32_t cycles;
    int32_t nextEvent;

    /** Halt state */
    int halted;
    bool irqDisableLatch; /**< IRQ mask sampled by the instruction pipeline */
    /* User/current register ports overlap in the operand phase after LDM^. */
    bool userBankPending, userBankActive;

    /**
     * Banked registers (one set per mode: USR/SYS, FIQ, IRQ, SVC, ABT, UND).
     * Per bank: [0] = SP, [1] = LR. Slots [2..6] save R8-R12 when
     * switching between the shared register set and FIQ.
     */
    int32_t bankedRegisters[6][7];
    int32_t bankedSPSRs[6];

    /** Barrel shifter output (for ARM data processing) */
    int32_t shifterOperand;
    int32_t shifterCarryOut;

    /** 2-stage prefetch pipeline */
    uint32_t prefetch[2];
    enum CupidExecMode executionMode;
    enum CupidPrivMode privilegeMode;

    /** Memory bus */
    struct CupidARMMemory memory;

    /** Interrupt / event handler */
    struct CupidARMInterruptHandler irqh;

    /** Back-pointer to owning GBA */
    struct CupidGBA* master;

    /* Read-only host observation immediately before an ARM/Thumb instruction
     * is issued, after the owner's IRQ/HALT arbitration. Failed ARM predicates
     * are still issued instructions. The observer must not modify the core or
     * advance emulated time. Host ownership survives reset. */
    void (*instructionObserver)(const struct CupidARM* cpu, void* context);
    void* instructionObserverContext;
};
#undef CUPID_ARM_REGISTER_FILE

/** @name Forward Declarations for Inline Helpers */
void cupid_arm_set_priv_mode(struct CupidARM* cpu, enum CupidPrivMode mode);

/** @name Inline Helpers */
static inline void _cupidSetMode(struct CupidARM* cpu, int thumb) {
    cpu->executionMode = thumb ? MODE_THUMB : MODE_ARM;
}

static inline void _cupidReadCPSR(struct CupidARM* cpu) {
    _cupidSetMode(cpu, cpu->cpsr.t);
    cupid_arm_set_priv_mode(cpu, cpu->cpsr.priv);
    cpu->irqh.readCPSR(cpu);
}

/** @name Public API */

void cupid_arm_init(struct CupidARM* cpu);
void cupid_arm_reset(struct CupidARM* cpu);
void cupid_arm_run(struct CupidARM* cpu);
void cupid_arm_run_loop(struct CupidARM* cpu);
void cupid_arm_run_fake(struct CupidARM* cpu, uint32_t opcode);
void cupid_arm_step(struct CupidARM* cpu);
void cupid_thumb_step(struct CupidARM* cpu);

/** @name Exception Entry */
void cupid_arm_raise_irq(struct CupidARM* cpu);
void cupid_arm_take_irq(struct CupidARM* cpu);
void cupid_arm_raise_swi(struct CupidARM* cpu);
void cupid_arm_raise_undefined(struct CupidARM* cpu);

/** @name Instruction Execution */
typedef void (*CupidARMInstruction)(struct CupidARM*, uint32_t opcode);
typedef void (*CupidThumbInstruction)(struct CupidARM*, uint16_t opcode);

/** Instruction tables (compile-time generated in isa-arm.c / isa-thumb.c) */
extern const CupidARMInstruction   cupid_arm_table[4096];
extern const CupidThumbInstruction cupid_thumb_table[1024];

#endif /* CUPID_ARM_H */
