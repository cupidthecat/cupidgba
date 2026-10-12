/* Copyright (c) 2013-2018 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/**
 * @file timer.c
 * @brief Event-driven GBA hardware timers (4 channels).
 *
 * Each non-cascade timer owns a CupidTimingEvent that fires on overflow.
 * The running counter is stored in @c memory.io[REG_TMxCNT_L >> 1] so that
 * I/O reads can return the up-to-date value after a sync. Cascade timers are
 * incremented directly when the preceding timer overflows.
 */

#include "cupid/gba.h"

/**
 * @brief Prescaler control bits 0-1 select shift amount (F/1, F/64, F/256, F/1024).
 */
static const unsigned PRESCALE_BITS[4] = { 0, 6, 8, 10 };

/**
 * @brief Per-timer overflow handler common logic.
 */
static void _timerUpdate(struct CupidGBA* gba, int timerId,
                         uint32_t cyclesLate)
{
    struct CupidTimer* timer = &gba->timers[timerId];

    /**
     * Reload counter.
     * Cascade timers get the reload value directly.
     * Non-cascade timers sync the running counter first (writes into
     * memory.io), then reload.
     */
    if (timer->countUp) {
        gba->memory.io[REG_TMCNT_LO(timerId) >> 1] = timer->reload;
    } else {
        cupid_timer_update_register(gba, timerId, cyclesLate);
    }

    /** IRQ. */
    if (timer->doIrq) {
        cupid_raise_irq(gba, IRQ_TIMER0 << timerId, cyclesLate);
    }

    /** Audio FIFO for timers 0 and 1. */
    if (timerId < 2) {
        cupid_audio_timer_overflow(&gba->audio, timerId);
    }

    /** Cascade to the next timer. */
    if (timerId < 3) {
        struct CupidTimer* next = &gba->timers[timerId + 1];
        if (next->countUp && next->enable) {
            ++gba->memory.io[REG_TMCNT_LO(timerId + 1) >> 1];
            if (!gba->memory.io[REG_TMCNT_LO(timerId + 1) >> 1]) {
                /** Wrapped to 0, overflow on the next timer. */
                _timerUpdate(gba, timerId + 1, cyclesLate);
            }
        }
    }
}

/**
 * @brief Per-channel timing callbacks.
 */
static void _timerUpdate0(struct CupidTiming* timing, void* context,
                          uint32_t cyclesLate)
{
    (void)timing;
    _timerUpdate(context, 0, cyclesLate);
}
static void _timerUpdate1(struct CupidTiming* timing, void* context,
                          uint32_t cyclesLate)
{
    (void)timing;
    _timerUpdate(context, 1, cyclesLate);
}
static void _timerUpdate2(struct CupidTiming* timing, void* context,
                          uint32_t cyclesLate)
{
    (void)timing;
    _timerUpdate(context, 2, cyclesLate);
}
static void _timerUpdate3(struct CupidTiming* timing, void* context,
                          uint32_t cyclesLate)
{
    (void)timing;
    _timerUpdate(context, 3, cyclesLate);
}

/**
 * @brief Callback table indexed by timer id.
 */
static void (*const TIMER_CALLBACKS[4])(struct CupidTiming*, void*, uint32_t) = {
    _timerUpdate0, _timerUpdate1, _timerUpdate2, _timerUpdate3
};

/**
 * @brief Initialize timer events.
 */
void cupid_timer_init(struct CupidGBA* gba) {
    static const char* NAMES[4] = {
        "CupidTimer 0", "CupidTimer 1", "CupidTimer 2", "CupidTimer 3"
    };

    memset(gba->timers, 0, sizeof(gba->timers));

    for (int i = 0; i < 4; i++) {
        gba->timers[i].event.name     = NAMES[i];
        gba->timers[i].event.callback = TIMER_CALLBACKS[i];
        gba->timers[i].event.context  = gba;
        gba->timers[i].event.priority = 0x20 + i;
    }
}

/**
 * @brief Sync timer counter into IO register and reschedule overflow event.
 */
void cupid_timer_update_register(struct CupidGBA* gba, int timer,
                                 int32_t cyclesLate)
{
    struct CupidTimer* t = &gba->timers[timer];
    if (!t->enable || t->countUp) {
        return;
    }

    int prescaleBits = t->prescaleBits;

    /** Align to prescaler boundary. */
    int32_t currentTime = cupid_timing_current_time(&gba->timing) - cyclesLate;
    int32_t tickMask    = (1 << prescaleBits) - 1;
    currentTime &= ~tickMask;

    /** Compute elapsed ticks since last sync. */
    int32_t tickIncrement = currentTime - t->lastEvent;
    t->lastEvent = currentTime;
    tickIncrement >>= prescaleBits;
    tickIncrement += gba->memory.io[REG_TMCNT_LO(timer) >> 1];

    /** Absorb any overflows (event normally fires first). */
    while (tickIncrement >= 0x10000) {
        tickIncrement -= 0x10000 - t->reload;
    }
    gba->memory.io[REG_TMCNT_LO(timer) >> 1] = (uint16_t)tickIncrement;

    /** Schedule next overflow. */
    tickIncrement = (0x10000 - tickIncrement) << prescaleBits;
    currentTime += tickIncrement;
    currentTime &= ~tickMask;

    cupid_timing_deschedule(&gba->timing, &t->event);
    cupid_timing_schedule_absolute(&gba->timing, &t->event, currentTime);
}

/**
 * @brief Write TMxCNT_L reload latch.
 */
void cupid_timer_write_reload(struct CupidGBA* gba, int ch, uint16_t val) {
    gba->timers[ch].reload = val;
}

/**
 * @brief Write TMxCNT_H control register.
 */
void cupid_timer_write_control(struct CupidGBA* gba, int ch, uint16_t value)
{
    struct CupidTimer* t = &gba->timers[ch];

    /** Sync counter before changing any settings. */
    cupid_timer_update_register(gba, ch, 0);

    unsigned prescaleBits = PRESCALE_BITS[value & 3];

    /** Snapshot previous state. */
    bool wasEnabled = t->enable;
    bool wasCountUp = t->countUp;
    int  oldPrescale = t->prescaleBits;

    /** Decode new settings. */
    t->prescaleBits = (int)prescaleBits;
    t->countUp      = (ch > 0) && (value & 0x0004);
    t->doIrq        = (value & 0x0040) != 0;
    t->enable       = (value & 0x0080) != 0;

    /** Determine whether to reschedule. */
    bool reschedule = false;
    if (wasEnabled != t->enable) {
        reschedule = true;
        if (t->enable) {
            /** 0 to 1 transition: reload counter. */
            gba->memory.io[REG_TMCNT_LO(ch) >> 1] = t->reload;
        }
    } else if (wasCountUp != t->countUp) {
        reschedule = true;
    } else if (oldPrescale != t->prescaleBits) {
        reschedule = true;
    }

    if (reschedule) {
        cupid_timing_deschedule(&gba->timing, &t->event);
        if (t->enable && !t->countUp) {
            int32_t tickMask = (1 << prescaleBits) - 1;
            t->lastEvent = cupid_timing_current_time(&gba->timing) & ~tickMask;
            cupid_timer_update_register(gba, ch, 0);
        }
    }
}

/**
 * @brief Read TMxCNT_L current counter value.
 */
uint16_t cupid_timer_read_counter(struct CupidGBA* gba, int ch) {
    cupid_timer_update_register(gba, ch, 0);
    return gba->memory.io[REG_TMCNT_LO(ch) >> 1];
}
