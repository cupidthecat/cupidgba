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

int main(void) {
    RUN(priority_and_lateness);
    RUN(reschedule_and_cancel);
    RUN(callback_queue_changes);
    RUN(relative_and_absolute_time);
    RUN(clock_wrap);
    RUN(clear_resets_deadlines);
    return 0;
}
