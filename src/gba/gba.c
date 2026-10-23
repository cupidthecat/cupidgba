/**
 * @file gba.c
 * @brief Top-level GBA system: create, reset, destroy, main frame loop.
 */

#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "serial/uart.h"
#include "../platform/context.h"

enum ExecutionRequest { EXECUTE_STEP, EXECUTE_FRAME, EXECUTE_INTERVAL, EXECUTE_STEPS };
struct CupidExecution {
    struct CupidContext* host;
    struct CupidContext* context;
    enum ExecutionRequest request;
    uint64_t start, clocks, stoppedClocks;
    uint64_t steps, stepLimit;
    bool (*boundary)(struct CupidGBA*, uint64_t, void*);
    void* boundaryContext;
    bool inside;
};

static void step_cpu(struct CupidGBA* gba);

static uint64_t execution_progress(const struct CupidGBA* gba) {
    const struct CupidExecution* execution = gba->execution;
    return cupid_timing_global_time(&gba->timing) - execution->start + execution->stoppedClocks;
}

static bool execution_ready(struct CupidGBA* gba) {
    struct CupidExecution* execution = gba->execution;
    if (execution->request == EXECUTE_STEP) return true;
    if (execution->request == EXECUTE_FRAME) return gba->frameReady || gba->stopped;
    if (execution->request == EXECUTE_STEPS) {
        /* Each visit is the return boundary of one ordinary step, including
         * a HALT acceptance or peripheral wait inside an unfinished fetch. */
        ++execution->steps;
        bool proceed = !execution->boundary || execution->boundary(gba, execution->steps, execution->boundaryContext);
        return !proceed || gba->stopped || execution->steps == execution->stepLimit;
    }
    return execution_progress(gba) >= execution->clocks;
}

static void yield_execution(struct CupidGBA* gba) {
    struct CupidExecution* execution = gba->execution;
    if (execution && execution->inside && execution_ready(gba))
        cupid_context_yield(execution->context);
}

static void advance_stopped_execution(struct CupidGBA* gba) {
    struct CupidExecution* execution = gba->execution;
    const uint64_t remaining = execution->clocks - execution_progress(gba);
    execution->stoppedClocks += cupid_gba_advance_stopped_time(gba, remaining);
}

static void execute_cpu(void* argument) {
    struct CupidGBA* gba = argument;
    for (;;) {
        if (gba->stopped) {
            if (gba->execution->request == EXECUTE_INTERVAL) advance_stopped_execution(gba);
        } else {
            gba->cpuInstructionPending = true;
            step_cpu(gba);
            gba->cpuInstructionPending = false;
        }
        yield_execution(gba);
    }
}

static void release_execution(struct CupidGBA* gba) {
    struct CupidExecution* execution = gba->execution;
    if (!execution) {
        gba->cpuInstructionPending = false;
        return;
    }
    /* Cancellation retires the stack at its yield. No unfinished fetch, MMIO
     * side effect or peripheral callback runs against reset/freed memory. */
    cupid_context_destroy(execution->context);
    cupid_context_destroy(execution->host);
    free(execution);
    gba->execution = NULL;
    gba->cpuInstructionPending = false;
}

static struct CupidExecution* prepare_execution(struct CupidGBA* gba) {
    if (!gba->execution) {
        struct CupidExecution* execution = calloc(1, sizeof(*execution));
        if (!execution) abort();
        execution->host = cupid_context_host_create();
        if (!execution->host) abort();
        execution->context = cupid_context_create(execution->host, execute_cpu, gba);
        if (!execution->context) abort();
        gba->execution = execution;
    }
    struct CupidExecution* execution = gba->execution;
    if (execution->inside) abort();
    return execution;
}

static void run_execution(struct CupidGBA* gba, enum ExecutionRequest request, uint64_t clocks) {
    struct CupidExecution* execution = prepare_execution(gba);
    execution->request = request;
    execution->start = cupid_timing_global_time(&gba->timing);
    execution->clocks = clocks;
    execution->stoppedClocks = 0;
    execution->inside = true;
    cupid_context_resume(execution->context);
    execution->inside = false;
}

/**
 * @brief Forward declarations for irqh callbacks.
 */
static void _cupidGBAReset(struct CupidARM* cpu);
static void _cupidSwi16(struct CupidARM* cpu, int immediate);
static void _cupidSwi32(struct CupidARM* cpu, int immediate);
static void _cupidHitIllegal(struct CupidARM* cpu, uint32_t opcode);
static void _cupidHitStub(struct CupidARM* cpu, uint32_t opcode);
static void _cupidBreakpoint(struct CupidARM* cpu, int immediate);
static void _cupidCPSRChanged(struct CupidARM* cpu);

/**
 * @brief Wire interrupt handler callbacks.
 */
static void _cupidInterruptHandlerInit(struct CupidARMInterruptHandler* irqh) {
    irqh->reset = _cupidGBAReset;
    irqh->processEvents = cupid_gba_process_cpu_events;
    irqh->swi16 = _cupidSwi16;
    irqh->swi32 = _cupidSwi32;
    irqh->hitIllegal = _cupidHitIllegal;
    irqh->readCPSR = _cupidCPSRChanged;
    irqh->hitStub = _cupidHitStub;
    irqh->bkpt16 = _cupidBreakpoint;
    irqh->bkpt32 = _cupidBreakpoint;
}

/**
 * @brief Create a GBA instance.
 *
 * Allocates memory, wires CPU bus callbacks to GBA load/store functions,
 * wires the interrupt handler, initialises all subsystems, and sets up the
 * timing scheduler. After this, call cupid_gba_reset() to power on.
 */
void cupid_gba_create(struct CupidGBA* gba) {
    memset(gba, 0, sizeof(*gba));
    gba->cpu.master = gba;

    /** Wire interrupt handler callbacks (must happen before first reset). */
    _cupidInterruptHandlerInit(&gba->cpu.irqh);

    /** Wire CPU memory bus to GBA load/store and active-region functions. */
    cupid_memory_init(gba);

    /** Init video, audio, IO, and timers. */
    cupid_video_init(&gba->video, gba);
    cupid_audio_init(&gba->audio, gba);
    cupid_io_init(gba);
    cupid_timer_init(gba);

    /** Key input defaults: bit=1 is pressed, 0 is released. */
    gba->keysActive = 0;       /** all keys released */
    gba->keysLast   = 0x0400;  /** matches BIOS boot state */
    gba->biosHLE = true;       /** default to HLE BIOS until real BIOS loaded */

    /** DMA and flow-control. */
    gba->performingDMA = 0;
    gba->cpuBlocked = false;
    gba->earlyExit = false;
    gba->biosStall = 0;
    gba->dmaPC = 0;

    /** Identification. */
    gba->biosChecksum = 0;
    gba->hasBios = false;
    gba->romCrc32 = 0;

    /** Idle-loop detection. */
    gba->idleOptimization = IDLE_LOOP_REMOVE;
    gba->idleLoop = 0;
    gba->idleDetectionStep = 0;
    gba->idleDetectionFailures = 0;

    /** Init timing scheduler and wire CPU cycle and next-event counters. */
    cupid_timing_init(&gba->timing, &gba->cpu.cycles, &gba->cpu.nextEvent);
    /* Bus helpers are available before power-on reset, so their IRQ clock
     * owner must already have valid event callbacks. */
    cupid_irq_init(gba);
    cupid_savedata_bind_timing(&gba->memory.savedata, &gba->timing);
    cupid_cartridge_init(gba);
    cupid_serial_init(&gba->serial, gba);

}

/**
 * @brief Reset the full system (public entry point).
 *
 * Clears timing, then calls cupid_arm_reset which invokes _cupidGBAReset
 * (the irqh.reset callback) to reinitialize subsystems. The selected boot path
 * supplies either reset-vector control or the built-in post-boot state.
 */
void cupid_gba_reset(struct CupidGBA* gba) {
    release_execution(gba);
    /** Ensure master pointer is valid. */
    gba->cpu.master = gba;

    /* Accepted cartridge operations retain their remaining duration while
     * the system scheduler is rebased by console reset. */
    cupid_savedata_bind_timing(&gba->memory.savedata, NULL);
    /** Clear timing event queue first. */
    cupid_rtc_sync(&gba->cartridge.rtc, cupid_timing_global_time(&gba->timing));
    cupid_timing_clear(&gba->timing);
    cupid_rtc_rebase(&gba->cartridge.rtc, 0);
    cupid_cartridge_reset_bus(gba);

    /**
     * Reset the CPU into masked ARM Supervisor mode and refill the reset vector.
     * The platform callback resets subsystems without applying fast-boot state.
     */
    cupid_arm_reset(&gba->cpu);

    /**
     * Sync the biosHLE flag from the fullBios state.
     * fullBios = 1 means a real BIOS is loaded; fullBios = 0 means HLE.
     */
    gba->biosHLE = !gba->memory.fullBios;

    /** If no real BIOS is loaded, skip straight to ROM entry. */
    if (!gba->memory.fullBios) {
        cupid_gba_skip_bios(gba);
    }
    cupid_savedata_bind_timing(&gba->memory.savedata, &gba->timing);
}

/**
 * @brief GBA-specific reset (irqh.reset callback).
 *
 * Called from cupid_arm_reset after zeroing registers and setting mode.
 */
static void _cupidGBAReset(struct CupidARM* cpu) {
    struct CupidGBA* gba = cpu->master;

    /** Reset subsystems (memory, video, audio, IO, timers). */
    cupid_memory_reset(gba);
    cupid_dma_reset(gba);
    cupid_video_reset(&gba->video);
    cupid_audio_reset(&gba->audio);
    cupid_io_init(gba);    /** IO init runs during reset. */
    cupid_timer_init(gba); /** Timer init runs during reset. */
    cupid_serial_reset(&gba->serial);

    /** Clear GBA-level state. */
    gba->ime = 0;
    gba->ie = 0;
    gba->irqf = 0;
    cupid_irq_init(gba);
    gba->frameReady = false;
    gba->bus = 0;
    gba->totalCycles = 0;
    gba->lastJump = 0;
    gba->cpuBlocked = false;
    gba->earlyExit = false;
    gba->biosStall = 0;
    gba->dmaPC = 0;
    gba->performingDMA = 0;
    gba->idleDetectionStep = 0;
    gba->idleDetectionFailures = 0;
    cpu->halted = 0;
    gba->stopped = false;
    gba->haltIRQSample = false;
    gba->haltIRQSampleValid = false;
    gba->haltTimed = false;
    gba->haltEvent = (struct CupidTimingEvent){0};
    gba->intervalOvershoot = 0;
    gba->keysLast = 0x0400;

    /**
     * PC and pipeline are already set by cupid_arm_reset (CupidARMWritePC)
     * before this callback is invoked. PC = BASE_RESET + WORD_SIZE_ARM.
     */
}

/**
 * @brief Apply post-boot state at the reset vector.
 *
 * Sets System mode and stack banks, vcount = 0x7E, POSTFLG = 1 and entry PC.
 * Only acts if PC is still at the reset vector (start of BIOS).
 */
void cupid_gba_skip_bios(struct CupidGBA* gba) {
    struct CupidARM* cpu = &gba->cpu;

    /** Guard: only skip if we have not left the reset vector yet. */
    if (cpu->gprs[ARM_PC] != BASE_RESET + WORD_SIZE_ARM) {
        return;
    }

    /* Only the deliberate fast entry installs post-boot control and stacks. */
    cupid_arm_set_priv_mode(cpu, MODE_IRQ);
    cpu->gprs[ARM_SP] = GBA_SP_BASE_IRQ;
    cupid_arm_set_priv_mode(cpu, MODE_SVC);
    cpu->gprs[ARM_SP] = GBA_SP_BASE_SUPERVISOR;
    cupid_arm_set_priv_mode(cpu, MODE_SYS);
    cpu->gprs[ARM_SP] = GBA_SP_BASE_SYSTEM;
    cpu->cpsr.packed = MODE_SYS;
    cpu->irqDisableLatch = false;

    if (gba->memory.rom) {
        cpu->gprs[ARM_PC] = GBA_BASE_ROM0;
    } else if (gba->memory.ewram && gba->memory.ewram[0x30]) {
        cpu->gprs[ARM_PC] = GBA_BASE_EWRAM + 0xC0;
    } else {
        cpu->gprs[ARM_PC] = GBA_BASE_EWRAM;
    }

    /** Simulate the BIOS having run to completion. */
    gba->memory.io[0x006 >> 1] = 0x7E;  /** REG_VCOUNT */
    gba->memory.io[0x300 >> 1] = 1;     /** POSTFLG = 1 */

    /** Reschedule video event to match post-boot timing. */
    cupid_video_skip_bios(&gba->video);

    gba->biosHLE = true;

    /** Flush the pipeline so the CPU fetches from the new PC. */
    CupidARMWritePC(cpu);
}

/**
 * @brief Halt the CPU until an enabled interrupt request can wake it.
 */
void cupid_gba_halt(struct CupidGBA* gba) {
    gba->haltIRQSampleValid = false;
    gba->cpu.nextEvent = gba->cpu.cycles;
    gba->cpu.halted = 1;
}

static void accept_halt(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    struct CupidGBA* gba = context;
    cupid_gba_halt(gba);
    gba->haltTimed = true;
}

void cupid_gba_request_halt(struct CupidGBA* gba) {
    gba->haltEvent.callback = accept_halt;
    gba->haltEvent.context = gba;
    gba->haltEvent.name = "HALTCNT acceptance";
    gba->haltEvent.priority = 3;
    cupid_timing_schedule(&gba->timing, &gba->haltEvent, 1);
}

void cupid_gba_wait_halt(struct CupidGBA* gba) {
    struct CupidARM* cpu = &gba->cpu;
    /* Retain this bus request while peripheral events run. Standalone calls
     * can return a frame/interval without completing or replaying the fetch.
     * A cable already owns a suspended CPU stack and yields in its clock
     * coordinator, so it does not enter a second execution context. */
    for (;;) {
        yield_execution(gba);
        if (!cpu->halted || gba->irq.available) break;
        if (gba->stopped && gba->execution && gba->execution->inside) {
            advance_stopped_execution(gba);
            continue;
        }
        gba->timing.waitingForEvent = true;
        cpu->cycles = cpu->nextEvent;
        cpu->irqh.processEvents(cpu);
    }
    if (cpu->halted) {
        cpu->halted = 0;
        cpu->cycles += gba->haltTimed ? 2 : 1;
        gba->haltTimed = false;
        gba->haltIRQSampleValid = false;
        if (cpu->cycles >= cpu->nextEvent) cpu->irqh.processEvents(cpu);
    }
    gba->timing.waitingForEvent = false;
}

static unsigned stop_sources(struct CupidGBA* gba) {
    unsigned sources = IRQ_KEYPAD | IRQ_GAMEPAK;
    if (cupid_serial_mode(&gba->serial) == SERIAL_GPIO) sources |= IRQ_SIO;
    return sources & gba->ie;
}

void cupid_gba_stop(struct CupidGBA* gba) {
    /* Finish the bus clocks preceding acceptance of STOP, then leave all
     * peripheral deadlines in place while the system clock is frozen. */
    gba->cpu.nextEvent = gba->cpu.cycles;
    gba->cpu.irqh.processEvents(&gba->cpu);
    cupid_cartridge_rtc_sync(gba);
    cupid_gba_halt(gba);
    gba->stopped = (stop_sources(gba) & gba->irqf) == 0;
}

void cupid_gba_wake_stop(struct CupidGBA* gba, enum CupidIRQ sources) {
    if (gba->stopped && (stop_sources(gba) & sources)) {
        gba->stopped = false;
        cupid_serial_uart_resume(&gba->serial);
    }
}

uint64_t cupid_gba_advance_stopped_time(struct CupidGBA* gba, uint64_t clocks) {
    /* RTC output can wake STOP before the supplied interval ends. Self-timed
     * save operations consume only that same accepted cartridge interval. */
    const uint64_t elapsed = cupid_cartridge_advance_stopped_time(gba, clocks);
    cupid_savedata_advance_clock(&gba->memory.savedata, elapsed);
    return elapsed;
}

/**
 * @brief Destroy a GBA instance.
 */
void cupid_gba_destroy(struct CupidGBA* gba) {
    release_execution(gba);
    cupid_cartridge_deinit(gba);
    cupid_video_deinit(&gba->video);
    cupid_memory_deinit(gba);
    cupid_timing_deinit(&gba->timing);
}

/**
 * @brief Flush accumulated CPU clocks and dispatch due scheduler events.
 */
void cupid_gba_process_events(struct CupidGBA* gba) {
    gba->cpu.nextEvent = gba->cpu.cycles;
    gba->cpu.irqh.processEvents(&gba->cpu);
}

/**
 * @brief Test keypad IRQ after key changes or KEYCNT writes.
 *
 * KEYCNT bit 14 is IRQ enable, bit 15 is AND condition.
 * AND mode suppresses the same matching held-key state until it changes or
 * a newly selected button invalidates that history. OR mode requests on
 * every check with at least one selected button held.
 */
void cupid_gba_test_keypad_irq(struct CupidGBA* gba) {
    uint16_t keysLast   = gba->keysLast;
    uint16_t keysActive = gba->keysActive;

    uint16_t keycnt = gba->memory.io[REG_KEYCNT >> 1];
    if (!(keycnt & 0x4000)) {
        /** IRQ not enabled. */
        return;
    }
    gba->keysLast = keysActive;
    int isAnd = keycnt & 0x8000;

    keycnt &= 0x3FF;
    if (isAnd && keycnt == (keysActive & keycnt)) {
        /** AND mode: all selected keys are pressed, fire only on edge. */
        if (keysLast == keysActive) {
            return;
        }
        cupid_raise_irq(gba, IRQ_KEYPAD, 0);
    } else if (!isAnd && (keysActive & keycnt)) {
        /** OR mode: at least one selected key is pressed. */
        cupid_raise_irq(gba, IRQ_KEYPAD, 0);
    } else {
        gba->keysLast = 0x400;
    }
}

/**
 * @brief Run a single CPU step.
 */
static void step_cpu(struct CupidGBA* gba) {
    struct CupidARM* cpu = &gba->cpu;

    if (gba->stopped) return;

    /* Sample before HALT's wake clock; a line arriving during that clock is
     * accepted after the resumed instruction, at its next boundary. */
    const bool irqLine = gba->haltIRQSampleValid ? gba->haltIRQSample : gba->irq.line;
    gba->haltIRQSampleValid = false;
    if (cpu->halted && gba->irq.available) {
        cpu->halted = 0;
        cpu->cycles += gba->haltTimed ? 2 : 1;
        gba->haltTimed = false;
        if (cpu->cycles >= cpu->nextEvent) cpu->irqh.processEvents(cpu);
    }
    if (cpu->halted) {
        gba->timing.waitingForEvent = true;
        cpu->cycles = cpu->nextEvent;
    } else {
        if (irqLine && !cpu->irqDisableLatch) {
            /* Static CPU clients use the exception's existing cycle path. */
            cpu->memory.timed = cpu->memory.fetch != NULL;
            cupid_arm_take_irq(cpu);
            cpu->memory.timed = false;
        }
        cpu->irqDisableLatch = cpu->cpsr.i;
        if (cpu->executionMode == MODE_THUMB) cupid_thumb_step(cpu);
        else cupid_arm_step(cpu);
    }

    if (cpu->cycles >= cpu->nextEvent || gba->timing.synchronize) {
        cpu->irqh.processEvents(cpu);
    }
    gba->timing.waitingForEvent = false;
}

void cupid_gba_step(struct CupidGBA* gba) {
    if (gba->stopped) return;
    if (gba->externalExecution) {
        gba->cpuInstructionPending = true;
        step_cpu(gba);
        gba->cpuInstructionPending = false;
    } else {
        run_execution(gba, EXECUTE_STEP, 0);
    }
}

uint64_t cupid_gba_run_steps(struct CupidGBA* gba, uint64_t limit,
    bool (*boundary)(struct CupidGBA*, uint64_t, void*), void* context) {
    if (!limit || (boundary && !boundary(gba, 0, context)) || gba->stopped) return 0;
    if (gba->externalExecution) {
        uint64_t steps = 0;
        do {
            cupid_gba_step(gba);
            ++steps;
            bool proceed = !boundary || boundary(gba, steps, context);
            if (!proceed || gba->stopped || steps == limit) break;
        } while (true);
        return steps;
    }
    struct CupidExecution* execution = prepare_execution(gba);
    execution->steps = 0;
    execution->stepLimit = limit;
    execution->boundary = boundary;
    execution->boundaryContext = context;
    run_execution(gba, EXECUTE_STEPS, 0);
    /* The callback can belong to a caller's stack. Retain only CPU work after
     * returning, never the caller's observation/input context. */
    execution->boundary = NULL;
    execution->boundaryContext = NULL;
    return execution->steps;
}

/**
 * @brief Run the emulator until a frame is ready.
 */
void cupid_gba_run_frame(struct CupidGBA* gba) {
    gba->frameReady = false;
    if (!gba->externalExecution) {
        if (!gba->stopped) run_execution(gba, EXECUTE_FRAME, 0);
        return;
    }
    while (!gba->frameReady && !gba->stopped) {
        cupid_gba_step(gba);
    }
}

void cupid_gba_run_interval(struct CupidGBA* gba, uint32_t clocks) {
    gba->frameReady = false;
    uint64_t remaining = clocks;
    if (gba->intervalOvershoot >= remaining) {
        gba->intervalOvershoot -= remaining;
        return;
    }
    remaining -= gba->intervalOvershoot;
    gba->intervalOvershoot = 0;
    if (!gba->externalExecution) {
        run_execution(gba, EXECUTE_INTERVAL, remaining);
        gba->intervalOvershoot = execution_progress(gba) - remaining;
        return;
    }
    while (remaining) {
        uint64_t elapsed;
        if (gba->stopped) {
            elapsed = cupid_gba_advance_stopped_time(gba, remaining);
        } else {
            const uint64_t before = cupid_timing_global_time(&gba->timing);
            cupid_gba_step(gba);
            elapsed = cupid_timing_global_time(&gba->timing) - before;
        }
        if (elapsed >= remaining) {
            gba->intervalOvershoot = elapsed - remaining;
            break;
        }
        remaining -= elapsed;
    }
}

/**
 * @brief IRQ handler callbacks wired into cpu->irqh.
 */

/**
 * @brief Process timing events when cpu->cycles reaches cpu->nextEvent.
 */
void cupid_gba_process_cpu_events(struct CupidARM* cpu) {
    struct CupidGBA* gba = cpu->master;

    /** Update open bus value from prefetch pipeline. */
    gba->bus = cpu->prefetch[1];
    if (cpu->executionMode == MODE_THUMB) {
        gba->bus |= cpu->prefetch[1] << 16;
    }

    /** Tick the timing scheduler and fire due callbacks (e.g. IRQ). */
    int32_t nextEvent = cpu->nextEvent;
    bool synchronize = gba->timing.synchronize != NULL;
    while (cpu->cycles >= nextEvent || synchronize) {
        synchronize = false;
        cpu->nextEvent = INT32_MAX;
        nextEvent = 0;
        int32_t cycles = cpu->cycles;
        cpu->cycles = 0;
        nextEvent = cupid_timing_tick(&gba->timing, cycles);

        cpu->nextEvent = nextEvent;
    }
    gba->earlyExit = false;

}

/**
 * @brief SWI in Thumb mode (immediate is the 8-bit comment field).
 */
static void _cupidSwi16(struct CupidARM* cpu, int immediate) {
    struct CupidGBA* gba = cpu->master;
    if (gba->biosHLE) {
        cupid_bios_swi(gba, (uint8_t)immediate);
    } else {
        cupid_arm_raise_swi(cpu);
    }
}

/**
 * @brief SWI in ARM mode (top 8 bits are the call number).
 */
static void _cupidSwi32(struct CupidARM* cpu, int immediate) {
    _cupidSwi16(cpu, immediate >> 16);
}

/**
 * @brief Illegal or undefined opcode handler.
 */
static void _cupidHitIllegal(struct CupidARM* cpu, uint32_t opcode) {
    (void)opcode;
    cupid_arm_raise_undefined(cpu);
}

/**
 * @brief Stub instruction handler (unimplemented coprocessor ops, etc.).
 */
static void _cupidHitStub(struct CupidARM* cpu, uint32_t opcode) {
    (void)cpu;
    fprintf(stderr, "CUPID: Stub opcode: 0x%08X at PC=0x%08X\n",
            opcode, cpu->gprs[ARM_PC] - (cpu->executionMode == MODE_THUMB ? 4 : 8));
}

/**
 * @brief Handle an unsupported breakpoint encoding on the GBA CPU.
 */
static void _cupidBreakpoint(struct CupidARM* cpu, int immediate) {
    (void)immediate;
    /* ARM7TDMI has no BKPT instruction. An embedding debugger can replace
     * this callback; ordinary execution takes the Undefined vector. */
    cupid_arm_raise_undefined(cpu);
}

/**
 * @brief CPSR changes take effect through the CPU's instruction-boundary latch.
 */
static void _cupidCPSRChanged(struct CupidARM* cpu) {
    (void)cpu; /* IRQ entry is handled by cupid_gba_step. */
}
