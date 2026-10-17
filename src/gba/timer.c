#include "cupid/gba.h"

static const unsigned PRESCALE_BITS[4] = {0, 6, 8, 10};

static void schedule_at(struct CupidTimer* timer, struct CupidTimingEvent* event,
                        uint64_t when) {
    cupid_timing_schedule_absolute(&timer->gba->timing, event, (int32_t)(uint32_t)when);
}

static uint16_t counter_at(const struct CupidTimer* timer, uint64_t when) {
    /* Enabling an independent timer delays its first counting edge. */
    uint64_t delta = timer->running && when > timer->epoch
                   ? (when - timer->epoch) >> timer->prescaleBits : 0;
    return (uint16_t)(timer->counter + delta);
}

static void start(struct CupidTimer* timer, uint64_t epoch) {
    timer->running = true;
    timer->epoch = epoch;
    uint64_t period = UINT64_C(1) << timer->prescaleBits;
    schedule_at(timer, &timer->event, epoch + (0x10000u - timer->counter) * period);
}

static void overflow(struct CupidTimer* timer, uint64_t when) {
    struct CupidGBA* gba = timer->gba;
    timer->counter = timer->reload;
    if (timer->control & 0x40) cupid_raise_irq_at(gba, IRQ_TIMER0 << timer->id, when);
    if (timer->id < 2) cupid_audio_timer_overflow(&gba->audio, (int)timer->id, when);
    if (timer->id < 3) {
        struct CupidTimer* next = &gba->timers[timer->id + 1];
        if ((next->control & 0x84) == 0x84 && ++next->counter == 0) {
            overflow(next, when);
        }
    }
}

static void overflow_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    struct CupidTimer* timer = context;
    uint64_t when = timing->globalCycles - late;
    overflow(timer, when);
    /* Reschedule from this overflow. Dispatch every overdue overflow, including
     * its IRQ, FIFO and cascade effects, in chronological order. */
    start(timer, when);
}

static void reload_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    struct CupidTimer* timer = context;
    timer->reload = timer->pendingReload;
}

static void control_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    struct CupidTimer* timer = context;
    uint64_t when = timing->globalCycles - late;
    bool wasEnabled = (timer->control & 0x80) != 0;
    timer->counter = counter_at(timer, when);
    timer->running = false;
    cupid_timing_deschedule(timing, &timer->event);
    timer->control = timer->pendingControl;
    timer->gba->memory.io[(REG_TMCNT_LO(timer->id) + 2) >> 1] = timer->control;
    timer->prescaleBits = PRESCALE_BITS[timer->control & 3];
    if (!(timer->control & 0x80)) return;
    if (timer->control & 4) {
        if (!wasEnabled) timer->counter = timer->reload;
        return;
    }

    uint64_t mask = (UINT64_C(1) << timer->prescaleBits) - 1;
    uint64_t epoch = when & ~mask;
    if (!wasEnabled) {
        /* At a shared prescaler boundary, a stopped FFFF reaches the next
         * divided tick before loading the reload. Other enables load it now
         * and postpone the counting epoch by one clock. */
        if (timer->counter != 0xFFFF || (when & mask)) {
            timer->counter = timer->reload;
            epoch++;
        }
    }
    start(timer, epoch);
}

void cupid_timer_init(struct CupidGBA* gba) {
    memset(gba->timers, 0, sizeof(gba->timers));
    for (unsigned i = 0; i < 4; i++) {
        struct CupidTimer* timer = &gba->timers[i];
        timer->gba = gba;
        timer->id = i;
        timer->event = (struct CupidTimingEvent){.context = timer,
            .callback = overflow_callback, .name = "Timer overflow", .priority = 0};
        timer->reloadEvent = (struct CupidTimingEvent){.context = timer,
            .callback = reload_callback, .name = "Timer reload write", .priority = 1};
        timer->controlEvent = (struct CupidTimingEvent){.context = timer,
            .callback = control_callback, .name = "Timer control write", .priority = 2};
    }
}

void cupid_timer_write_reload(struct CupidGBA* gba, int ch, uint16_t value) {
    struct CupidTimer* timer = &gba->timers[ch];
    timer->pendingReload = value;
    cupid_timing_schedule(&gba->timing, &timer->reloadEvent, 1);
}

void cupid_timer_write_control(struct CupidGBA* gba, int ch, uint16_t value) {
    struct CupidTimer* timer = &gba->timers[ch];
    timer->pendingControl = value & (ch ? 0xC7 : 0xC3);
    cupid_timing_schedule(&gba->timing, &timer->controlEvent, 1);
}

void cupid_timer_write_byte(struct CupidGBA* gba, unsigned ch, unsigned byte, uint8_t value) {
    uint16_t reload = gba->timers[ch].pendingReload;
    switch (byte) {
    case 0: cupid_timer_write_reload(gba, (int)ch, (reload & 0xFF00) | value); break;
    case 1: cupid_timer_write_reload(gba, (int)ch, (reload & 0xFF) | ((uint16_t)value << 8)); break;
    case 2: cupid_timer_write_control(gba, (int)ch, value); break;
    default: break; /* The reserved byte does not change control or its timing. */
    }
}

uint16_t cupid_timer_read_counter(struct CupidGBA* gba, int ch) {
    return counter_at(&gba->timers[ch], cupid_timing_global_time(&gba->timing));
}
