/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/timing.h"
#include "../test.h"

struct Fixture {
    struct CupidTiming timing;
    int32_t cycles;
    int32_t next;
    unsigned count;
    unsigned order[16];
    uint32_t late[16];
    uint64_t time[16];
    unsigned syncCount, syncEvents;
    uint64_t syncTime;
};

struct Context {
    struct Fixture* fixture;
    unsigned id;
    struct CupidTimingEvent* reschedule;
    bool interrupt;
};

static void callback(struct CupidTiming* timing, void* opaque, uint32_t late) {
    struct Context* context = opaque;
    struct Fixture* fixture = context->fixture;
    CHECK(fixture->count < 16);
    fixture->order[fixture->count] = context->id;
    fixture->late[fixture->count] = late;
    fixture->time[fixture->count++] = cupid_timing_global_time(timing);
    CHECK((uint32_t)cupid_timing_current_time(timing) == timing->masterCycles - late);
    if (context->interrupt) {
        cupid_timing_interrupt(timing);
    }
    if (context->reschedule) {
        cupid_timing_schedule(timing, context->reschedule, 2);
    }
}

static void init(struct Fixture* fixture) {
    memset(fixture, 0, sizeof(*fixture));
    cupid_timing_init(&fixture->timing, &fixture->cycles, &fixture->next);
}

static struct CupidTimingEvent event(struct Context* context, unsigned priority) {
    struct CupidTimingEvent result = {0};
    result.context = context;
    result.callback = callback;
    result.priority = priority;
    return result;
}

static void priority_and_lateness(void) {
    struct Fixture f;
    init(&f);
    struct Context contexts[] = {{&f, 0, NULL, false}, {&f, 1, NULL, false}, {&f, 2, NULL, false}};
    struct CupidTimingEvent events[] = {event(&contexts[0], 8), event(&contexts[1], 1), event(&contexts[2], 1)};
    for (unsigned i = 0; i < 3; i++) cupid_timing_schedule(&f.timing, &events[i], 10);
    CHECK(cupid_timing_tick(&f.timing, 13) == INT32_MAX);
    CHECK(f.count == 3);
    CHECK(f.order[0] == 1 && f.order[1] == 2 && f.order[2] == 0);
    CHECK(f.late[0] == 3 && f.late[1] == 3 && f.late[2] == 3);
}

static void reschedule_and_cancel(void) {
    struct Fixture f;
    init(&f);
    struct Context a = {&f, 0, NULL, false}, b = {&f, 1, NULL, false};
    struct CupidTimingEvent first = event(&a, 0), second = event(&b, 0);
    cupid_timing_schedule(&f.timing, &first, 10);
    cupid_timing_schedule(&f.timing, &second, 30);
    cupid_timing_schedule(&f.timing, &first, 20);
    CHECK(f.timing.root == &first && first.next == &second && second.next == NULL);
    CHECK(f.next == 20);
    cupid_timing_deschedule(&f.timing, &first);
    CHECK(!cupid_timing_is_scheduled(&f.timing, &first));
    CHECK(f.next == 30 && first.next == NULL);
    cupid_timing_deschedule(&f.timing, &second);
    CHECK(f.next == INT32_MAX);
}

static void callback_queue_changes(void) {
    struct Fixture f;
    init(&f);
    struct Context a = {&f, 0, NULL, true}, b = {&f, 1, NULL, false}, c = {&f, 2, NULL, false};
    struct CupidTimingEvent first = event(&a, 0), second = event(&b, 0), inserted = event(&c, 0);
    a.reschedule = &inserted;
    cupid_timing_schedule(&f.timing, &first, 10);
    cupid_timing_schedule(&f.timing, &second, 11);
    CHECK(cupid_timing_tick(&f.timing, 14) == INT32_MAX);
    CHECK(f.count == 3);
    CHECK(f.order[0] == 0 && f.order[1] == 1 && f.order[2] == 2);
    /* Relative scheduling inside a late callback uses its original deadline. */
    CHECK(f.time[0] == 10 && f.time[1] == 11 && f.time[2] == 12);
    CHECK(f.late[0] == 4 && f.late[1] == 3 && f.late[2] == 2);
    CHECK(!f.timing.inCallback && cupid_timing_global_time(&f.timing) == 14);
}

static void relative_and_absolute_time(void) {
    struct Fixture f;
    init(&f);
    struct Context a = {&f, 0, NULL, false};
    struct CupidTimingEvent e = event(&a, 0);
    f.cycles = 7;
    cupid_timing_schedule(&f.timing, &e, 5);
    CHECK(e.when == 12 && f.next == 12);
    CHECK(cupid_timing_until(&f.timing, &e) == 5);
    CHECK(cupid_timing_next_event(&f.timing) == 5);
    f.cycles = 0;
    CHECK(cupid_timing_tick(&f.timing, 12) == INT32_MAX);
    CHECK(cupid_timing_global_time(&f.timing) == 12);
}

static void clock_wrap(void) {
    struct Fixture f;
    init(&f);
    f.timing.masterCycles = UINT32_MAX - 5;
    f.timing.globalCycles = UINT32_MAX - 5;
    struct Context a = {&f, 0, NULL, false}, b = {&f, 1, NULL, false};
    struct CupidTimingEvent first = event(&a, 0), second = event(&b, 0);
    cupid_timing_schedule(&f.timing, &second, 10);
    cupid_timing_schedule(&f.timing, &first, 3);
    CHECK(cupid_timing_tick(&f.timing, 4) == 6);
    CHECK(f.count == 1 && f.late[0] == 1);
    CHECK(cupid_timing_tick(&f.timing, 6) == INT32_MAX);
    CHECK(f.count == 2 && f.late[1] == 0);
    CHECK(cupid_timing_current_time(&f.timing) == 4);
    CHECK(cupid_timing_global_time(&f.timing) == (uint64_t)UINT32_MAX + 5);

    f.timing.masterCycles = (uint32_t)INT32_MAX - 1;
    f.cycles = 4;
    CHECK((uint32_t)cupid_timing_current_time(&f.timing) == (uint32_t)INT32_MAX + 3);
    cupid_timing_schedule_absolute(&f.timing, &first, (int32_t)((uint32_t)INT32_MAX + 8));
    CHECK(cupid_timing_until(&f.timing, &first) == 5);
}

static void clear_resets_deadlines(void) {
    struct Fixture f;
    init(&f);
    struct Context a = {&f, 0, NULL, false};
    struct CupidTimingEvent e = event(&a, 0);
    cupid_timing_schedule(&f.timing, &e, 10);
    f.cycles = 6;
    cupid_timing_clear(&f.timing);
    CHECK(f.cycles == 0 && f.next == INT32_MAX && f.timing.root == NULL);
    CHECK(cupid_timing_global_time(&f.timing) == 0);
}

static void observe_clock(void* context, uint64_t time) {
    struct Fixture* f = context;
    f->syncCount++;
    f->syncTime = time;
    f->syncEvents = f->count;
}

static void clock_observer_runs_after_due_callbacks_without_a_deadline(void) {
    struct Fixture f;
    init(&f);
    f.timing.clockContext = &f;
    f.timing.syncClock = observe_clock;
    struct Context context = {&f, 0, NULL, false};
    struct CupidTimingEvent e = event(&context, 0);
    cupid_timing_schedule(&f.timing, &e, 10);
    CHECK(cupid_timing_tick(&f.timing, 14) == INT32_MAX);
    CHECK(f.time[0] == 10 && f.syncTime == 14 && f.syncCount == 1 && f.syncEvents == 1);
    CHECK(cupid_timing_tick(&f.timing, 2) == INT32_MAX);
    CHECK(f.syncTime == 16 && f.syncCount == 2);
    cupid_timing_clear(&f.timing);
    CHECK(cupid_timing_tick(&f.timing, 3) == INT32_MAX);
    CHECK(f.syncTime == 3 && f.syncCount == 3);
}

struct QueuedContext {
    struct Fixture* fixture;
    struct CupidTimingEvent* event;
    bool requeued;
};

static void queued_callback(struct CupidTiming* timing, void* opaque, uint32_t late) {
    struct QueuedContext* context = opaque;
    struct Fixture* fixture = context->fixture;
    CHECK(fixture->count < 16);
    fixture->order[fixture->count] = 0;
    fixture->late[fixture->count] = late;
    fixture->time[fixture->count++] = cupid_timing_global_time(timing);
    CHECK(!cupid_timing_is_scheduled(timing, context->event));
    if (!context->requeued) {
        context->requeued = true;
        /* The dispatched event owns its next insertion even when another
         * callback queue is deferred and both deadlines are already late. */
        cupid_timing_interrupt(timing);
        cupid_timing_queue_absolute(timing, context->event,
            (int32_t)(uint32_t)(cupid_timing_global_time(timing) + 2));
    }
}

static void queue_absolute_ownership(void) {
    const uint64_t epochs[] = {0, (uint32_t)INT32_MAX - 1,
                               (uint64_t)UINT32_MAX - 1, UINT64_MAX - 1};
    for (unsigned i = 0; i < sizeof(epochs) / sizeof(epochs[0]); ++i) {
        struct Fixture f;
        init(&f);
        f.timing.masterCycles = (uint32_t)epochs[i];
        f.timing.globalCycles = epochs[i];
        struct QueuedContext queued = {&f, NULL, false};
        struct CupidTimingEvent first = {.context = &queued, .callback = queued_callback};
        queued.event = &first;
        struct Context ordinary = {&f, 1, NULL, false};
        struct CupidTimingEvent second = event(&ordinary, 1);
        cupid_timing_schedule(&f.timing, &second, 2);
        cupid_timing_queue_absolute(&f.timing, &first, (int32_t)(uint32_t)(epochs[i] + 2));
        CHECK(f.next == 2);
        CHECK(cupid_timing_tick(&f.timing, 5) == INT32_MAX);
        CHECK(f.count == 3 && f.order[0] == 0 && f.order[1] == 1 && f.order[2] == 0);
        CHECK(f.time[0] == epochs[i] + 2 && f.time[1] == epochs[i] + 2 && f.time[2] == epochs[i] + 4);
        CHECK(f.late[0] == 3 && f.late[1] == 3 && f.late[2] == 1);
        CHECK(f.timing.root == NULL && f.timing.reroot == NULL && !f.timing.inCallback);
        CHECK(cupid_timing_global_time(&f.timing) == epochs[i] + 5);
    }
}

struct ObserverQueue {
    struct Fixture* fixture;
    struct CupidTimingEvent* first;
    struct CupidTimingEvent* second;
    unsigned calls;
};

static void observer_changes_queue(void* opaque, uint64_t time) {
    struct ObserverQueue* observer = opaque;
    struct Fixture* f = observer->fixture;
    CHECK(time == f->timing.globalCycles && !f->timing.inCallback);
    CHECK(f->timing.reroot == NULL);
    switch (observer->calls++) {
    case 0:
        CHECK(f->count == 0 && f->timing.root == observer->first);
        cupid_timing_deschedule(&f->timing, observer->first);
        cupid_timing_schedule(&f->timing, observer->second, 4);
        cupid_timing_interrupt(&f->timing);
        break;
    case 1:
        CHECK(f->count == 0 && f->timing.root == observer->second);
        cupid_timing_deschedule(&f->timing, observer->second);
        cupid_timing_schedule(&f->timing, observer->first, 0);
        cupid_timing_interrupt(&f->timing);
        break;
    case 2:
        CHECK(f->count == 1 && f->timing.root == NULL);
        cupid_timing_schedule(&f->timing, observer->second, 2);
        break;
    default:
        CHECK(false);
    }
}

static void clock_observer_changes_update_next_deadline(void) {
    const uint64_t epochs[] = {0, (uint32_t)INT32_MAX - 1,
                               (uint64_t)UINT32_MAX - 1, UINT64_MAX - 1};
    for (unsigned i = 0; i < sizeof(epochs) / sizeof(epochs[0]); ++i) {
        struct Fixture f;
        init(&f);
        f.timing.masterCycles = (uint32_t)epochs[i];
        f.timing.globalCycles = epochs[i];
        struct Context contexts[] = {{&f, 0, NULL, false}, {&f, 1, NULL, false}};
        struct CupidTimingEvent first = event(&contexts[0], 0), second = event(&contexts[1], 0);
        cupid_timing_schedule(&f.timing, &first, 10);
        cupid_timing_interrupt(&f.timing);
        CHECK(cupid_timing_tick(&f.timing, 2) == 8);
        CHECK(f.count == 0 && f.timing.root == &first && f.timing.reroot == NULL);
        struct ObserverQueue observer = {&f, &first, &second, 0};
        f.timing.clockContext = &observer;
        f.timing.syncClock = observer_changes_queue;
        CHECK(cupid_timing_tick(&f.timing, 1) == 4 && f.next == 4);
        CHECK(f.count == 0 && f.timing.root == NULL && f.timing.reroot == &second);
        CHECK(cupid_timing_tick(&f.timing, 1) == 0 && f.next == 0);
        CHECK(f.count == 0 && f.timing.root == NULL && f.timing.reroot == &first);
        CHECK(cupid_timing_tick(&f.timing, 0) == 2 && f.next == 2);
        CHECK(f.count == 1 && f.time[0] == epochs[i] + 4 && f.late[0] == 0);
        f.timing.syncClock = NULL;
        CHECK(cupid_timing_tick(&f.timing, 2) == INT32_MAX && f.next == INT32_MAX);
        CHECK(f.count == 2 && f.order[1] == 1 && f.time[1] == epochs[i] + 6 && f.late[1] == 0);
        CHECK(observer.calls == 3 && f.timing.root == NULL && f.timing.reroot == NULL);
    }
}

int main(void) {
    RUN(priority_and_lateness);
    RUN(reschedule_and_cancel);
    RUN(callback_queue_changes);
    RUN(relative_and_absolute_time);
    RUN(clock_wrap);
    RUN(clear_resets_deadlines);
    RUN(clock_observer_runs_after_due_callbacks_without_a_deadline);
    RUN(queue_absolute_ownership);
    RUN(clock_observer_changes_update_next_deadline);
    return 0;
}
