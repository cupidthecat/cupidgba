/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/**
 * @file timer.h
 * @brief GBA hardware timers (4 channels), event-driven via CupidTiming.
 */

#ifndef CUPID_TIMER_H
#define CUPID_TIMER_H

#include "cupid/types.h"
#include "cupid/timing.h"

struct CupidGBA;

/** Compute the IO-register offset for timer X's counter (REG_TMxCNT_L) */
#define REG_TMCNT_LO(X) (0x100 + ((X) << 2))

/** @name Timer Channel */
struct CupidTimer {
    uint16_t reload;                   /**< TIMERxCNT_L latch (written by game) */
    int32_t lastEvent;                 /**< masterCycles of last counter sync */
    struct CupidTimingEvent event;     /**< Overflow callback in timing queue */

    /** Decoded fields from TIMERxCNT_H */
    int prescaleBits;                  /**< 0, 6, 8, or 10 */
    bool countUp;                      /**< Cascade from previous timer */
    bool doIrq;                        /**< Raise IRQ on overflow */
    bool enable;
};

/** @name API */
void cupid_timer_init(struct CupidGBA* gba);

/** Sync the running counter into memory.io[] and reschedule the event */
void cupid_timer_update_register(struct CupidGBA* gba, int timer,
                                 int32_t cyclesLate);

/** Called when TIMERxCNT_L is written (updates reload latch only) */
void cupid_timer_write_reload(struct CupidGBA* gba, int ch, uint16_t val);

/** Called when TIMERxCNT_H is written */
void cupid_timer_write_control(struct CupidGBA* gba, int ch, uint16_t val);

/** Read the current counter value (syncs before returning) */
uint16_t cupid_timer_read_counter(struct CupidGBA* gba, int ch);

#endif /* CUPID_TIMER_H */
