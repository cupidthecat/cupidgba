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
    struct CupidGBA* gba;
    unsigned id;
    uint16_t reload;                   /**< Accepted reload latch, separate from counter */
    uint16_t pendingReload;
    uint16_t control;
    uint16_t pendingControl;
    uint16_t counter;                  /**< Counter at epoch, or frozen counter */
    uint64_t epoch;                    /**< Prescaler origin since power-on */
    unsigned prescaleBits;
    bool running;                     /**< Independent clock is active */
    struct CupidTimingEvent event;
    struct CupidTimingEvent reloadEvent;
    struct CupidTimingEvent controlEvent;
};

/** @name API */
void cupid_timer_init(struct CupidGBA* gba);

/** Bus writes are accepted one cycle later. */
void cupid_timer_write_reload(struct CupidGBA* gba, int ch, uint16_t val);
void cupid_timer_write_byte(struct CupidGBA* gba, unsigned ch, unsigned byte, uint8_t val);

/** Called when TIMERxCNT_H is written */
void cupid_timer_write_control(struct CupidGBA* gba, int ch, uint16_t val);

/** Read the counter at the current emulated time without changing its phase. */
uint16_t cupid_timer_read_counter(struct CupidGBA* gba, int ch);

#endif /* CUPID_TIMER_H */
