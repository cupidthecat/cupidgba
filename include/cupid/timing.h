/**
 * @file timing.h
 * @brief Sorted linked-list priority queue for cycle-accurate event scheduling.
 *
 * Events are inserted in order of (when, priority). The run loop calls
 * cupid_timing_tick() each time the CPU's cycle counter crosses nextEvent;
 * that function fires all due callbacks in order and returns the next
 * deadline.
 */

#ifndef CUPID_TIMING_H
#define CUPID_TIMING_H

#include "cupid/types.h"
#include <limits.h>

/** @name Forward Declaration */
struct CupidTiming;

/** @name Timing Event */
struct CupidTimingEvent {
    void* context;
    void (*callback)(struct CupidTiming* timing, void* context, uint32_t cyclesLate);
    const char* name;
    uint32_t when;            /**< Absolute master-cycle timestamp */
    unsigned priority;        /**< Lower = fires first when tied */
    struct CupidTimingEvent* next;
};

/** @name Timing Scheduler */
struct CupidTiming {
    struct CupidTimingEvent* root;       /**< Head of sorted singly-linked list */
    struct CupidTimingEvent* reroot;     /**< Deferred re-insertions (reentrancy) */

    uint64_t globalCycles;               /**< Total cycles since power-on */
    uint32_t masterCycles;               /**< Current absolute master counter */
    bool inCallback;                     /**< Time queries use the active event's deadline */
    uint64_t callbackCycles;             /**< Timestamp of the event being dispatched */
    int32_t* relativeCycles;             /**< → cpu->cycles (elapsed since last tick) */
    int32_t* nextEvent;                  /**< → cpu->nextEvent (next deadline) */

    /* Optional host clock coordinator. It consumes the requested elapsed
     * clocks and dispatches events before returning the next local deadline.
     * Install only while the CPU is idle; clear before destroying its host. */
    int32_t (*synchronize)(void* context, struct CupidTiming* timing, int32_t cycles);
    void* synchronizeContext;
    bool waitingForEvent;                /**< HALT or DMA wait may end at a newly arrived event */
};

/** @name API */

/** Initialise: wires the scheduler to the CPU's cycle counters. */
void cupid_timing_init(struct CupidTiming* timing,
                       int32_t* relativeCycles,
                       int32_t* nextEvent);

/** No-op deinit (no dynamic allocations). */
void cupid_timing_deinit(struct CupidTiming* timing);

/** Remove all scheduled events and reset cycle counters. */
void cupid_timing_clear(struct CupidTiming* timing);

/** Defer the pending queue during a callback. Tick still dispatches due events. */
void cupid_timing_interrupt(struct CupidTiming* timing);

/**
 * Schedule an event `when` cycles from the current CPU time or callback deadline.
 * If the event is already scheduled it is silently moved.
 */
void cupid_timing_schedule(struct CupidTiming* timing,
                           struct CupidTimingEvent* event,
                           int32_t when);

/** Schedule at an absolute master-cycle timestamp. */
void cupid_timing_schedule_absolute(struct CupidTiming* timing,
                                    struct CupidTimingEvent* event,
                                    int32_t when);

/** Queue an owned event known to be absent from both scheduler lists.
 * Use schedule_absolute to move an event that may already be queued.
 * Dispatch removes an event before its callback, permitting owned requeue. */
void cupid_timing_queue_absolute(struct CupidTiming* timing,
                                 struct CupidTimingEvent* event,
                                 int32_t when);

/** Remove an event from the queue (safe if not scheduled). */
void cupid_timing_deschedule(struct CupidTiming* timing,
                             struct CupidTimingEvent* event);

/** Check whether an event is currently in the queue. */
bool cupid_timing_is_scheduled(const struct CupidTiming* timing,
                               const struct CupidTimingEvent* event);

/**
 * Advance the master clock by `cycles` and fire all due events.
 * Advances both masterCycles and globalCycles. The caller consumes and clears
 * relativeCycles before calling this function. Callbacks may add CPU cycles.
 * Returns the next CPU breakpoint relative to masterCycles (or INT_MAX).
 */
int32_t cupid_timing_tick(struct CupidTiming* timing, int32_t cycles);

/* Coordinator primitives: advance a clock without callbacks, then dispatch
 * one due event. Keeping these separate permits a host to order equal-time
 * events across several cores before releasing any CPU bus phase. */
void cupid_timing_advance_clock(struct CupidTiming* timing, uint32_t cycles);
bool cupid_timing_dispatch_next(struct CupidTiming* timing);
const struct CupidTimingEvent* cupid_timing_first_event(const struct CupidTiming* timing);

/** Master time including CPU elapsed cycles, or the active callback deadline. */
int32_t cupid_timing_current_time(const struct CupidTiming* timing);

/** Global time since power-on, or the active callback deadline (64-bit). */
uint64_t cupid_timing_global_time(const struct CupidTiming* timing);

/** Cycles until the earliest pending event. */
int32_t cupid_timing_next_event(struct CupidTiming* timing);

/** Cycles until a specific event fires. */
int32_t cupid_timing_until(const struct CupidTiming* timing,
                           const struct CupidTimingEvent* event);

#endif /* CUPID_TIMING_H */
