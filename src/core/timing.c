/**
 * @file timing.c
 * @brief Sorted linked-list priority queue for cycle-accurate event scheduling.
 *
 * Design:
 *   - Events are stored in a singly-linked list sorted by @c (when, priority).
 *   - @c root is the active head. @c reroot holds deferred insertions made
 *     while iterating through the list (reentrancy safety).
 *   - @c masterCycles is the absolute cycle counter. @c *relativeCycles points
 *     to @c cpu->cycles (elapsed since the last event-processing pass).
 *   - @c *nextEvent points to @c cpu->nextEvent and is the run-loop breakpoint.
 */

#include "cupid/timing.h"
#include <limits.h>

#ifndef UNLIKELY
#define UNLIKELY(X) __builtin_expect(!!(X), 0)
#endif

/**
 * @brief Initialise a timing context.
 *
 * @param timing         Timing context to initialise.
 * @param relativeCycles Pointer to the CPU's elapsed-cycle accumulator.
 * @param nextEvent      Pointer to the CPU's next-event breakpoint.
 */
void cupid_timing_init(struct CupidTiming* timing,
                       int32_t* relativeCycles,
                       int32_t* nextEvent)
{
    timing->root           = NULL;
    timing->reroot         = NULL;
    timing->globalCycles   = 0;
    timing->masterCycles   = 0;
    timing->relativeCycles = relativeCycles;
    timing->nextEvent      = nextEvent;
    *nextEvent             = INT32_MAX;
}

/**
 * @brief Release any resources held by a timing context.
 *
 * Currently a no-op; provided for symmetry with cupid_timing_init().
 *
 * @param timing Timing context to deinitialise.
 */
void cupid_timing_deinit(struct CupidTiming* timing) {
    (void)timing;
}

/**
 * @brief Clear all scheduled events and reset cycle counters to zero.
 *
 * @param timing Timing context to clear.
 */
void cupid_timing_clear(struct CupidTiming* timing) {
    timing->root         = NULL;
    timing->reroot       = NULL;
    timing->globalCycles = 0;
    timing->masterCycles = 0;
}

/**
 * @brief Signal a pending interrupt to the timing queue.
 *
 * Moves the active event list to @c reroot so that any events fired during
 * the current tick do not alter the list being iterated.
 *
 * @param timing Timing context.
 */
void cupid_timing_interrupt(struct CupidTiming* timing) {
    if (!timing->root) {
        return;
    }
    timing->reroot = timing->root;
    timing->root   = NULL;
}

/**
 * @brief Schedule an event to fire @p when cycles from now.
 *
 * The event is inserted into the sorted event queue. If @p when is earlier
 * than the current @c nextEvent deadline, that deadline is updated so the
 * CPU run-loop wakes up in time.
 *
 * @param timing Timing context.
 * @param event  Event to schedule. Must not already be in the queue.
 * @param when   Cycles from the current time until the event fires.
 */
void cupid_timing_schedule(struct CupidTiming* timing,
                           struct CupidTimingEvent* event,
                           int32_t when)
{
    /* Compute absolute timestamp from relative offset */
    int32_t nextEvent = when + *timing->relativeCycles;
    event->when = (uint32_t)(nextEvent + (int32_t)timing->masterCycles);

    /* Possibly pull in the CPU's next-event deadline */
    if (nextEvent < *timing->nextEvent) {
        *timing->nextEvent = nextEvent;
    }

    /* Find insertion point (sorted by when, then priority) */
    struct CupidTimingEvent** previous;
    struct CupidTimingEvent*  next;
    if (timing->reroot) {
        previous = &timing->reroot;
        next     = timing->reroot;
    } else {
        previous = &timing->root;
        next     = timing->root;
    }

    unsigned priority = event->priority;
    while (next) {
        int32_t nextWhen = (int32_t)(next->when - timing->masterCycles);
        if (nextWhen > nextEvent ||
            (nextWhen == nextEvent && next->priority > priority)) {
            break;
        }
        previous = &next->next;
        next     = next->next;
    }
    event->next = next;
    *previous   = event;
}

/**
 * @brief Schedule an event at an absolute cycle timestamp.
 *
 * Convenience wrapper around cupid_timing_schedule() that converts
 * @p when from an absolute master-cycle value to a relative offset.
 *
 * @param timing Timing context.
 * @param event  Event to schedule.
 * @param when   Absolute master-cycle timestamp at which the event fires.
 */
void cupid_timing_schedule_absolute(struct CupidTiming* timing,
                                    struct CupidTimingEvent* event,
                                    int32_t when)
{
    cupid_timing_schedule(timing, event,
                          when - cupid_timing_current_time(timing));
}

/**
 * @brief Remove a previously scheduled event from the queue.
 *
 * Safe to call even if @p event is not currently scheduled (no-op in that
 * case). Searches both @c root and @c reroot lists.
 *
 * @param timing Timing context.
 * @param event  Event to remove.
 */
void cupid_timing_deschedule(struct CupidTiming* timing,
                             struct CupidTimingEvent* event)
{
    struct CupidTimingEvent** previous;
    struct CupidTimingEvent*  next;
    if (timing->reroot) {
        previous = &timing->reroot;
        next     = timing->reroot;
    } else {
        previous = &timing->root;
        next     = timing->root;
    }

    while (next) {
        if (next == event) {
            *previous = next->next;
            return;
        }
        previous = &next->next;
        next     = next->next;
    }
}

/**
 * @brief Test whether an event is currently in the queue.
 *
 * @param timing Timing context.
 * @param event  Event to look up.
 * @return @c true if @p event is scheduled, @c false otherwise.
 */
bool cupid_timing_is_scheduled(const struct CupidTiming* timing,
                               const struct CupidTimingEvent* event)
{
    const struct CupidTimingEvent* next = timing->root;
    if (!next) {
        next = timing->reroot;
    }
    while (next) {
        if (next == event) {
            return true;
        }
        next = next->next;
    }
    return false;
}

/**
 * @brief Advance the master clock by @p cycles and fire any due events.
 *
 * Events whose deadline has elapsed are dequeued and their callbacks
 * invoked in order. After all due events have fired, any items inserted
 * into @c reroot during callbacks are merged back into @c root and the
 * next-event deadline is recalculated.
 *
 * @param timing Timing context.
 * @param cycles Number of cycles elapsed since the last call.
 * @return       Cycles until the next scheduled event (the new breakpoint).
 */
int32_t cupid_timing_tick(struct CupidTiming* timing, int32_t cycles) {
    timing->masterCycles += (uint32_t)cycles;
    uint32_t masterCycles = timing->masterCycles;

    while (timing->root) {
        struct CupidTimingEvent* next = timing->root;
        int32_t nextWhen = (int32_t)(next->when - masterCycles);
        if (nextWhen > 0) {
            return nextWhen;
        }
        timing->root = next->next;
        next->callback(timing, next->context, (uint32_t)(-nextWhen));
    }

    /* If callbacks moved items to reroot via cupid_timing_interrupt(),
     * merge them back and recalculate the next-event deadline. */
    if (UNLIKELY(timing->reroot)) {
        timing->root   = timing->reroot;
        timing->reroot = NULL;
        *timing->nextEvent = cupid_timing_next_event(timing);
    }
    return *timing->nextEvent;
}

/**
 * @brief Return the current logical time (master cycles + relative offset).
 *
 * @param timing Timing context.
 * @return Current cycle count as seen by the event scheduler.
 */
int32_t cupid_timing_current_time(const struct CupidTiming* timing) {
    return (int32_t)timing->masterCycles + *timing->relativeCycles;
}

/**
 * @brief Return the 64-bit global cycle counter.
 *
 * Monotonically increasing; never wraps for any practical emulation session.
 *
 * @param timing Timing context.
 * @return Absolute cycle count since emulation began.
 */
uint64_t cupid_timing_global_time(const struct CupidTiming* timing) {
    return timing->globalCycles + (uint64_t)(uint32_t)*timing->relativeCycles;
}

/**
 * @brief Calculate cycles until the next scheduled event.
 *
 * @param timing Timing context.
 * @return Relative cycle count until the nearest event, or @c INT_MAX if
 *         the queue is empty.
 */
int32_t cupid_timing_next_event(struct CupidTiming* timing) {
    struct CupidTimingEvent* next = timing->root;
    if (!next) {
        return INT_MAX;
    }
    return (int32_t)(next->when - timing->masterCycles)
         - *timing->relativeCycles;
}

/**
 * @brief Calculate the relative cycle offset until a specific event fires.
 *
 * @param timing Timing context.
 * @param event  Event whose deadline to query. Must be scheduled.
 * @return Cycles from the current time until @p event fires.
 */
int32_t cupid_timing_until(const struct CupidTiming* timing,
                           const struct CupidTimingEvent* event)
{
    return (int32_t)(event->when - timing->masterCycles)
         - *timing->relativeCycles;
}
