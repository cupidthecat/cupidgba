/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/**
 * @file gba.h
 * @brief Top-level GBA system struct and main emulation loop.
 *
 */

#ifndef CUPID_GBA_H
#define CUPID_GBA_H

#include "cupid/types.h"
#include "cupid/arm.h"
#include "cupid/memory.h"
#include "cupid/video.h"
#include "cupid/audio.h"
#include "cupid/timer.h"
#include "cupid/timing.h"
#include "cupid/io.h"
#include "cupid/bios.h"
#include "cupid/render.h"

/** @name GBA Clock */
#define GBA_ARM7TDMI_FREQUENCY 0x1000000U  /**< 16,777,216 Hz */

/**
 * Cycles between IE&IF becoming non-zero and the CPU actually vectoring.
 */
#define GBA_IRQ_DELAY 7

/**
 * @name GBA IRQ Bit Positions
 *
 * Use as: (1 << GBA_IRQ_VBLANK), etc. Our CupidIRQ enum in io.h
 */
enum GBAIRQ {
    GBA_IRQ_VBLANK   = 0x0,
    GBA_IRQ_HBLANK   = 0x1,
    GBA_IRQ_VCOUNTER = 0x2,
    GBA_IRQ_TIMER0   = 0x3,
    GBA_IRQ_TIMER1   = 0x4,
    GBA_IRQ_TIMER2   = 0x5,
    GBA_IRQ_TIMER3   = 0x6,
    GBA_IRQ_SIO      = 0x7,
    GBA_IRQ_DMA0     = 0x8,
    GBA_IRQ_DMA1     = 0x9,
    GBA_IRQ_DMA2     = 0xA,
    GBA_IRQ_DMA3     = 0xB,
    GBA_IRQ_KEYPAD   = 0xC,
    GBA_IRQ_GAMEPAK  = 0xD
};

/** @name Initial Stack-Pointer Bases */
enum {
    GBA_SP_BASE_SYSTEM     = 0x03007F00,
    GBA_SP_BASE_IRQ        = 0x03007FA0,
    GBA_SP_BASE_SUPERVISOR = 0x03007FE0
};

/** @name Idle-Loop Optimization Levels */
enum CupidIdleLoopOpt {
    IDLE_LOOP_IGNORE = -1,
    IDLE_LOOP_REMOVE = 0,
    IDLE_LOOP_DETECT
};

/** @name GBA System */
struct CupidGBA {
    struct CupidARM   cpu;
    struct CupidMemory memory;
    struct CupidVideo  video;
    struct CupidAudio  audio;
    struct CupidTimer  timers[4];

    /** Cycle-accurate event scheduler */
    struct CupidTiming timing;

    /** Open bus value (updated each instruction fetch) */
    uint32_t bus;

    /** DMA state (>= 0 while a DMA transfer is in progress) */
    int performingDMA;

    /** IRQ event (scheduled with GBA_IRQ_DELAY after IE & IF become non-zero) */
    struct CupidTimingEvent irqEvent;

    /** Interrupt state (stored in IO but mirrored here for fast access) */
    uint16_t ime;        /**< Master interrupt enable */
    uint16_t ie;         /**< Interrupt enable mask */
    uint16_t irqf;       /**< Interrupt request flags (IF) */

    /** Key input */
    uint16_t keysActive;     /**< Current key state (host → emu) */
    uint16_t keysLast;       /**< Previous frame's key state */

    /** Frame complete flag (set when VBlank starts) */
    bool frameReady;

    /** Last branch target (idle-loop detection) */
    uint32_t lastJump;

    /** CPU flow control */
    bool cpuBlocked;     /**< True while DMA owns the bus */
    bool earlyExit;      /**< Break out of the run loop (for sync) */
    uint32_t biosStall;  /**< Extra stall cycles added by BIOS reads */
    uint32_t dmaPC;      /**< PC snapshot when DMA started */

    /** ROM/BIOS identification */
    uint32_t biosChecksum;
    bool     hasBios;        /**< True when a real BIOS image is loaded */
    uint32_t romCrc32;

    /** Cycle tracking */
    int32_t totalCycles;

    /** BIOS call interception (when no BIOS ROM loaded) */
    bool biosHLE;

    /** Idle-loop detection state */
    enum CupidIdleLoopOpt idleOptimization;
    uint32_t idleLoop;
    int idleDetectionStep;
    int idleDetectionFailures;
    int32_t cachedRegisters[16];
    bool taintedRegisters[16];
};

/** @name API */
void cupid_gba_create(struct CupidGBA* gba);
void cupid_gba_reset(struct CupidGBA* gba);
void cupid_gba_destroy(struct CupidGBA* gba);
void cupid_gba_skip_bios(struct CupidGBA* gba);
void cupid_gba_halt(struct CupidGBA* gba);

/** Test and fire keypad IRQ based on KEYCNT (call after key state changes) */
void cupid_gba_test_keypad_irq(struct CupidGBA* gba);

/** Run one frame's worth of cycles (~280896 cycles) */
void cupid_gba_run_frame(struct CupidGBA* gba);

/** Run a single CPU step + process events */
void cupid_gba_step(struct CupidGBA* gba);

/** Process events (video, audio, timers, DMA, IRQ) after CPU steps */
void cupid_gba_process_events(struct CupidGBA* gba);

#endif /* CUPID_GBA_H */
