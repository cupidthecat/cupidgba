/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      irq.h
 * @author    cupidthecat
 * @brief     Interrupt register, availability and CPU-line pipeline state.
 * @date      2026-11-16
 *
 * - Keep queued register writes and signal transitions separate.
 * - Declare timed requests, acknowledgements and line updates.
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
#ifndef CUPID_IRQ_H
#define CUPID_IRQ_H
#include "cupid/timing.h"
struct CupidGBA;

struct CupidIRQSignalChange {
    struct CupidGBA *gba;
    bool value;
    /* CPU-line changes retain one or two CPU clocks while the bus is held.
     * Availability changes use only the system-clock event. */
    bool pausedPending;
    uint32_t remaining;
    struct CupidTimingEvent event;
};

struct CupidIRQRegisterWrite {
    struct CupidGBA *gba;
    uint16_t ie;
    uint16_t ifSet, ifClear;
    bool ime;
    /* A future peripheral assertion must not restore an older CPU mask. */
    bool writeIE, writeIME;
    struct CupidTimingEvent event;
};

struct CupidIRQState {
    uint16_t pendingIE, pendingIF;
    bool pendingIME;
    bool available, logicalAvailable;
    bool line, logicalLine;
    /* Peripheral registers and HALT availability keep the system clock. */
    bool cpuClockPaused;
    /* Owned CPU-line slots; register and HALT domains remain independent. */
    uint8_t scheduledLineMask, pausedLineMask;
    /* Optional bus-owner predicate, polled before same-edge input updates. */
    bool (*cpuReady)(struct CupidGBA *gba);
    struct CupidTimingEvent cpuClockResumeEvent;
    unsigned nextLineChange, nextAvailableChange;
    struct CupidIRQRegisterWrite writes[4];
    struct CupidIRQSignalChange lineChanges[4];
    struct CupidIRQSignalChange availableChanges[4];
};

void cupid_irq_init(struct CupidGBA *gba);
void cupid_irq_write(struct CupidGBA *gba, uint32_t address, uint16_t value);
void cupid_irq_write8(struct CupidGBA *gba, uint32_t address, uint8_t value);

/* CPU bus ownership: a stretched transfer completes one CPU clock at its
 * deadline. DMA waits resume without completing a CPU clock themselves. */
void cupid_irq_hold_cpu(struct CupidGBA *gba, unsigned cycles);
/* Atomic occupied transfers only: the caller must immediately advance all
 * supplied clocks without an intermediate strobe. A quiet default release
 * needs only its final breakpoint and retained event bookkeeping. The owned
 * renderer still observes that tick; foreign observers use the event path. */
bool cupid_irq_prepare_quiet_transfer(struct CupidGBA *gba, unsigned cycles);
void cupid_irq_pause_cpu(struct CupidGBA *gba);
/* A dynamic bus owner decides readiness before same-edge register writes. */
void cupid_irq_hold_cpu_until(struct CupidGBA *gba, bool (*ready)(struct CupidGBA *));
void cupid_irq_resume_cpu(struct CupidGBA *gba);
#endif
