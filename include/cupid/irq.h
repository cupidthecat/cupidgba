/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_IRQ_H
#define CUPID_IRQ_H
#include "cupid/timing.h"
struct CupidGBA;

struct CupidIRQSignalChange {
    struct CupidGBA* gba;
    bool value;
    /* CPU-line changes retain one or two CPU clocks while the bus is held.
     * Availability changes use only the system-clock event. */
    bool pausedPending;
    uint32_t remaining;
    struct CupidTimingEvent event;
};

struct CupidIRQRegisterWrite {
    struct CupidGBA* gba;
    uint16_t ie;
    uint16_t ifSet, ifClear;
    bool ime;
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
    struct CupidTimingEvent cpuClockResumeEvent;
    unsigned nextLineChange, nextAvailableChange;
    struct CupidIRQRegisterWrite writes[4];
    struct CupidIRQSignalChange lineChanges[4];
    struct CupidIRQSignalChange availableChanges[4];
};

void cupid_irq_init(struct CupidGBA* gba);
void cupid_irq_write(struct CupidGBA* gba, uint32_t address, uint16_t value);
void cupid_irq_write8(struct CupidGBA* gba, uint32_t address, uint8_t value);

/* CPU bus ownership: a stretched transfer completes one CPU clock at its
 * deadline. DMA waits resume without completing a CPU clock themselves. */
void cupid_irq_hold_cpu(struct CupidGBA* gba, unsigned cycles);
void cupid_irq_pause_cpu(struct CupidGBA* gba);
void cupid_irq_resume_cpu(struct CupidGBA* gba);
#endif
