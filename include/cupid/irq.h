/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_IRQ_H
#define CUPID_IRQ_H
#include "cupid/timing.h"
struct CupidGBA;

struct CupidIRQLineChange {
    struct CupidGBA* gba;
    bool value;
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
    bool available, logicalAvailable, pendingAvailable;
    bool line, logicalLine;
    unsigned nextLineChange;
    struct CupidTimingEvent availableEvent;
    struct CupidIRQRegisterWrite writes[4];
    struct CupidIRQLineChange lineChanges[4];
};

void cupid_irq_init(struct CupidGBA* gba);
void cupid_irq_write(struct CupidGBA* gba, uint32_t address, uint16_t value);
void cupid_irq_write8(struct CupidGBA* gba, uint32_t address, uint8_t value);
#endif
