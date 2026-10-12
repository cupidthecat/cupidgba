#include "cupid/timing.h"

/* Timestamps wrap modulo 2^32. Deadlines must be within INT32_MAX cycles. */
static int32_t distance(uint32_t to, uint32_t from) {
    return (int32_t)(to - from);
}

static struct CupidTimingEvent* first_event(const struct CupidTiming* timing) {
    return timing->root ? timing->root : timing->reroot;
}

static int32_t deadline(const struct CupidTiming* timing) {
    const struct CupidTimingEvent* first = first_event(timing);
    return first ? distance(first->when, timing->masterCycles) : INT32_MAX;
}

static void refresh_deadline(struct CupidTiming* timing) {
    /* A CPU mode change can request a breakpoint at the current cycle. */
    if (*timing->nextEvent > *timing->relativeCycles) {
        *timing->nextEvent = deadline(timing);
    }
}

void cupid_timing_init(struct CupidTiming* timing, int32_t* relativeCycles,
                       int32_t* nextEvent) {
    timing->relativeCycles = relativeCycles;
    timing->nextEvent = nextEvent;
    timing->clockContext = NULL;
    timing->syncClock = NULL;
    cupid_timing_clear(timing);
}

void cupid_timing_deinit(struct CupidTiming* timing) {
    (void)timing;
}

void cupid_timing_clear(struct CupidTiming* timing) {
    timing->root = NULL;
    timing->reroot = NULL;
    timing->globalCycles = 0;
    timing->masterCycles = 0;
    timing->inCallback = false;
    timing->callbackCycles = 0;
    *timing->relativeCycles = 0;
    *timing->nextEvent = INT32_MAX;
}

void cupid_timing_interrupt(struct CupidTiming* timing) {
    if (timing->root) {
        timing->reroot = timing->root;
        timing->root = NULL;
    }
}

void cupid_timing_deschedule(struct CupidTiming* timing, struct CupidTimingEvent* event) {
    struct CupidTimingEvent** lists[] = {&timing->root, &timing->reroot};
    for (unsigned i = 0; i < 2; i++) {
        struct CupidTimingEvent** link = lists[i];
        while (*link) {
            if (*link == event) {
                *link = event->next;
                event->next = NULL;
                refresh_deadline(timing);
                return;
            }
            link = &(*link)->next;
        }
    }
}

void cupid_timing_schedule(struct CupidTiming* timing, struct CupidTimingEvent* event,
                           int32_t when) {
    cupid_timing_deschedule(timing, event);
    event->when = (uint32_t)cupid_timing_current_time(timing) + (uint32_t)when;
    struct CupidTimingEvent** link = timing->reroot ? &timing->reroot : &timing->root;
    while (*link) {
        int32_t delta = distance((*link)->when, event->when);
        if (delta > 0 || (delta == 0 && (*link)->priority > event->priority)) break;
        link = &(*link)->next;
    }
    event->next = *link;
    *link = event;
    refresh_deadline(timing);
}

void cupid_timing_schedule_absolute(struct CupidTiming* timing, struct CupidTimingEvent* event,
                                    int32_t when) {
    uint32_t now = (uint32_t)cupid_timing_current_time(timing);
    cupid_timing_schedule(timing, event, distance((uint32_t)when, now));
}

bool cupid_timing_is_scheduled(const struct CupidTiming* timing,
                               const struct CupidTimingEvent* event) {
    const struct CupidTimingEvent* lists[] = {timing->root, timing->reroot};
    for (unsigned i = 0; i < 2; i++) {
        for (const struct CupidTimingEvent* next = lists[i]; next; next = next->next) {
            if (next == event) return true;
        }
    }
    return false;
}

int32_t cupid_timing_tick(struct CupidTiming* timing, int32_t cycles) {
    timing->masterCycles += (uint32_t)cycles;
    timing->globalCycles += (uint32_t)cycles;
    for (;;) {
        if (!timing->root) {
            timing->root = timing->reroot;
            timing->reroot = NULL;
        }
        struct CupidTimingEvent* next = timing->root;
        if (!next || distance(next->when, timing->masterCycles) > 0) break;
        timing->root = next->next;
        next->next = NULL;
        uint32_t late = timing->masterCycles - next->when;
        bool wasInCallback = timing->inCallback;
        uint64_t previousCallback = timing->callbackCycles;
        timing->inCallback = true;
        timing->callbackCycles = timing->globalCycles - late;
        next->callback(timing, next->context, late);
        timing->inCallback = wasInCallback;
        timing->callbackCycles = previousCallback;
    }
    if (timing->syncClock) timing->syncClock(timing->clockContext, timing->globalCycles);
    /* The CPU compares its relative accumulator against this breakpoint. */
    *timing->nextEvent = deadline(timing);
    return *timing->nextEvent;
}

int32_t cupid_timing_current_time(const struct CupidTiming* timing) {
    if (timing->inCallback) return (int32_t)(uint32_t)timing->callbackCycles;
    return (int32_t)(timing->masterCycles + (uint32_t)*timing->relativeCycles);
}

uint64_t cupid_timing_global_time(const struct CupidTiming* timing) {
    if (timing->inCallback) return timing->callbackCycles;
    return timing->globalCycles + (uint32_t)*timing->relativeCycles;
}

int32_t cupid_timing_next_event(struct CupidTiming* timing) {
    const struct CupidTimingEvent* first = first_event(timing);
    return first ? cupid_timing_until(timing, first) : INT32_MAX;
}

int32_t cupid_timing_until(const struct CupidTiming* timing, const struct CupidTimingEvent* event) {
    uint32_t now = timing->masterCycles + (uint32_t)*timing->relativeCycles;
    return distance(event->when, now);
}
