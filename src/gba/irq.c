#include "cupid/gba.h"

/* A completing CPU edge samples the previously accepted input. Peripheral
 * assertions then precede CPU register writes on that same system clock. */
enum { IRQ_SAMPLE_PHASE = 0, IRQ_ASSERT_PHASE = 1, IRQ_WRITE_PHASE = 2 };

static void schedule_at(struct CupidGBA* gba, struct CupidTimingEvent* event, uint64_t when) {
    cupid_timing_schedule_absolute(&gba->timing, event, (int32_t)(uint32_t)when);
}

static unsigned line_mask(const struct CupidIRQSignalChange* change) {
    return 1u << (unsigned)(change - change->gba->irq.lineChanges);
}

static void schedule_line(struct CupidGBA* gba, struct CupidIRQSignalChange* change,
                          uint64_t when) {
    schedule_at(gba, &change->event, when);
    gba->irq.scheduledLineMask |= line_mask(change);
}

static uint64_t current_time(struct CupidGBA* gba, uint32_t late) {
    return (gba->timing.inCallback ? gba->timing.globalCycles
                                  : cupid_timing_global_time(&gba->timing)) - late;
}

static void line_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    struct CupidIRQSignalChange* change = context;
    change->gba->irq.scheduledLineMask &= ~line_mask(change);
    change->gba->irq.pausedLineMask &= ~line_mask(change);
    change->pausedPending = false;
    change->gba->irq.line = change->value;
}

static void available_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    struct CupidIRQSignalChange* change = context;
    change->gba->irq.available = change->value;
}

static void update_lines(struct CupidGBA* gba, uint64_t when) {
    struct CupidIRQState* irq = &gba->irq;
    bool available = (gba->ie & gba->irqf) != 0;
    if (available != irq->logicalAvailable) {
        irq->logicalAvailable = available;
        /* Keep this accepted value even if another register update runs at
         * its publication deadline and queues the opposite value. */
        struct CupidIRQSignalChange* change = &irq->availableChanges[irq->nextAvailableChange];
        irq->nextAvailableChange = (irq->nextAvailableChange + 1) % 4;
        change->value = available;
        schedule_at(gba, &change->event, when + 1);
    }
    bool line = gba->ime && available;
    if (line != irq->logicalLine) {
        irq->logicalLine = line;
        /* A level not yet sampled into the first CPU stage may change
         * while the clock is held. Preserve an already latched first stage. */
        struct CupidIRQSignalChange* change = NULL;
        if (irq->cpuClockPaused) {
            for (unsigned i = 0; i < 4; ++i) {
                if (irq->lineChanges[i].pausedPending && irq->lineChanges[i].remaining == 2) {
                    change = &irq->lineChanges[i];
                    break;
                }
            }
        }
        if (!change) {
            change = &irq->lineChanges[irq->nextLineChange];
            irq->nextLineChange = (irq->nextLineChange + 1) % 4;
            if (change->pausedPending) abort();
        }
        change->value = line;
        if (irq->cpuClockPaused) {
            change->remaining = 2;
            change->pausedPending = true;
            irq->pausedLineMask |= line_mask(change);
        } else {
            schedule_line(gba, change, when + 2);
        }
    }
}

static void write_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    struct CupidIRQRegisterWrite* write = context;
    struct CupidGBA* gba = write->gba;
    if (write->writeIE) gba->ie = write->ie;
    gba->irqf = (gba->irqf & ~write->ifClear) | write->ifSet;
    if (write->writeIME) gba->ime = write->ime;
    gba->memory.io[REG_IE >> 1] = gba->ie;
    gba->memory.io[REG_IF >> 1] = gba->irqf;
    gba->memory.io[REG_IME >> 1] = gba->ime;
    update_lines(gba, timing->globalCycles - late);
}

static void queue_write(struct CupidGBA* gba, uint64_t when, unsigned priority,
                        uint32_t address, uint16_t ifSet, uint16_t ifClear) {
    struct CupidIRQRegisterWrite* write = NULL;
    for (unsigned i = 0; i < 4; i++) {
        struct CupidIRQRegisterWrite* candidate = &gba->irq.writes[i];
        if (!cupid_timing_is_scheduled(&gba->timing, &candidate->event)) {
            if (!write) write = candidate;
        } else if (candidate->event.when == (uint32_t)when) {
            write = candidate;
            if (candidate->event.priority < priority) priority = candidate->event.priority;
            break;
        }
    }
    /* Register acceptance has a one-cycle pipeline. CPU bus synchronization
     * and ordered peripheral dispatch limit it to two pending timestamps. */
    if (!write) abort();
    if (!cupid_timing_is_scheduled(&gba->timing, &write->event)) {
        write->ifSet = 0;
        write->ifClear = 0;
        write->writeIE = false;
        write->writeIME = false;
    }
    /* Coalesce changes with the same acceptance deadline. Later deadlines
     * must not publish a newly asserted source through an earlier write. */
    write->ifSet = (write->ifSet & ~ifClear) | ifSet;
    write->ifClear |= ifClear;
    /* Retain only the registers this deadline actually writes. A display
     * source phase can be queued ahead of an intervening CPU mask write. */
    if (address == REG_IE) {
        write->ie = gba->irq.pendingIE;
        write->writeIE = true;
    } else if (address == REG_IME) {
        write->ime = gba->irq.pendingIME;
        write->writeIME = true;
    }
    write->event.priority = priority;
    schedule_at(gba, &write->event, when);
}

static void resume_cpu_clock(struct CupidTiming* timing, void* context, uint32_t late);

void cupid_irq_init(struct CupidGBA* gba) {
    memset(&gba->irq, 0, sizeof(gba->irq));
    gba->cpu.irqDisableLatch = gba->cpu.cpsr.i;
    gba->irq.cpuClockResumeEvent = (struct CupidTimingEvent){.context = gba,
        .callback = resume_cpu_clock, .name = "CPU IRQ clock release", .priority = IRQ_SAMPLE_PHASE};
    for (unsigned i = 0; i < 4; i++) {
        struct CupidIRQRegisterWrite* write = &gba->irq.writes[i];
        write->gba = gba;
        write->event = (struct CupidTimingEvent){.context = write,
            .callback = write_callback, .name = "Interrupt register write", .priority = IRQ_WRITE_PHASE};
        struct CupidIRQSignalChange* available = &gba->irq.availableChanges[i];
        available->gba = gba;
        available->event = (struct CupidTimingEvent){.context = available,
            .callback = available_callback, .name = "Interrupt available", .priority = 0};
        struct CupidIRQSignalChange* change = &gba->irq.lineChanges[i];
        change->gba = gba;
        change->event = (struct CupidTimingEvent){.context = change,
            .callback = line_callback, .name = "Interrupt line change", .priority = 0};
    }
}

void cupid_irq_write(struct CupidGBA* gba, uint32_t address, uint16_t value) {
    switch (address) {
    case REG_IE: gba->irq.pendingIE = value & 0x3FFF; break;
    case REG_IF: gba->irq.pendingIF &= ~value; break;
    case REG_IME: gba->irq.pendingIME = value & 1; break;
    default: return;
    }
    queue_write(gba, cupid_timing_global_time(&gba->timing) + 1, IRQ_WRITE_PHASE,
                address, 0, address == REG_IF ? value : 0);
}

void cupid_irq_write8(struct CupidGBA* gba, uint32_t address, uint8_t value) {
    if ((address & ~1u) == REG_IE) {
        unsigned shift = (address & 1) * 8;
        value &= address & 1 ? 0x3F : 0xFF;
        uint16_t merged = (gba->irq.pendingIE & ~(0xFFu << shift)) | ((uint16_t)value << shift);
        cupid_irq_write(gba, REG_IE, merged);
    } else if ((address & ~1u) == REG_IF) {
        cupid_irq_write(gba, REG_IF, (uint16_t)value << ((address & 1) * 8));
    } else if (address == REG_IME) {
        cupid_irq_write(gba, REG_IME, value);
    }
}

void cupid_raise_irq_at(struct CupidGBA* gba, enum CupidIRQ source, uint64_t timestamp) {
    cupid_gba_wake_stop(gba, source);
    gba->irq.pendingIF |= source;
    queue_write(gba, timestamp + 1, IRQ_ASSERT_PHASE, 0, source, 0);
}

void cupid_raise_irq(struct CupidGBA* gba, enum CupidIRQ source, uint32_t late) {
    cupid_raise_irq_at(gba, source, current_time(gba, late));
}

void cupid_test_irq(struct CupidGBA* gba, uint32_t late) {
    update_lines(gba, current_time(gba, late));
}

static void pause_line_events(struct CupidGBA* gba) {
    uint32_t now = (uint32_t)cupid_timing_global_time(&gba->timing);
    /* Walk each scheduler list once instead of searching it separately for
     * all four possible line events on every stretched memory transfer.
     * Read next before descheduling, which clears the removed event's link. */
    struct CupidTimingEvent* lists[] = {gba->timing.root, gba->timing.reroot};
    for (unsigned i = 0; i < 2; ++i) {
        struct CupidTimingEvent* event = lists[i];
        while (event) {
            struct CupidTimingEvent* next = event->next;
            if (event->callback == line_callback) {
                struct CupidIRQSignalChange* change = event->context;
                change->remaining = event->when - now;
                if (!change->remaining || change->remaining > 2) abort();
                change->pausedPending = true;
                gba->irq.scheduledLineMask &= ~line_mask(change);
                gba->irq.pausedLineMask |= line_mask(change);
                cupid_timing_deschedule(&gba->timing, event);
            }
            event = next;
        }
    }
}

void cupid_irq_pause_cpu(struct CupidGBA* gba) {
    if (gba->irq.cpuClockPaused) abort();
    gba->irq.cpuClockPaused = true;
    /* Keep the held clock even when no line event needs moving. Isolate the
     * queue walk and its scratch storage from ordinary empty-domain waits. */
    if (gba->irq.scheduledLineMask) pause_line_events(gba);
}

static void resume_cpu(struct CupidGBA* gba, uint64_t now, unsigned completedClocks) {
    if (!gba->irq.cpuClockPaused) abort();
    gba->irq.cpuClockPaused = false;
    if (!gba->irq.pausedLineMask) return;
    for (unsigned i = 0; i < 4; ++i) {
        struct CupidIRQSignalChange* change = &gba->irq.lineChanges[i];
        if (!change->pausedPending) continue;
        change->pausedPending = false;
        gba->irq.pausedLineMask &= ~line_mask(change);
        schedule_line(gba, change, now + change->remaining - completedClocks);
    }
}

void cupid_irq_resume_cpu(struct CupidGBA* gba) {
    /* Releasing DMA ownership allows a new CPU request; that request has not
     * completed its ready edge yet. */
    resume_cpu(gba, cupid_timing_global_time(&gba->timing), 0);
}

static void resume_cpu_clock(struct CupidTiming* timing, void* context, uint32_t late) {
    struct CupidGBA* gba = context;
    /* Priority zero samples an already accepted input before a register
     * update on this same edge. A captured final stage publishes now. */
    if (gba->irq.cpuReady && !gba->irq.cpuReady(gba)) {
        /* Dispatch dequeued this owned event before testing bus readiness. */
        cupid_timing_queue_absolute(&gba->timing, &gba->irq.cpuClockResumeEvent,
            (int32_t)(uint32_t)(timing->globalCycles - late + 1));
        return;
    }
    gba->irq.cpuReady = NULL;
    resume_cpu(gba, timing->globalCycles - late, 1);
}

bool cupid_irq_prepare_quiet_transfer(struct CupidGBA* gba, unsigned cycles) {
    struct CupidARM* cpu = &gba->cpu;
    struct CupidTiming* timing = &gba->timing;
    struct CupidIRQState* irq = &gba->irq;
    struct CupidTimingEvent* ready = &irq->cpuClockResumeEvent;
    if (cycles < 2 || cycles > INT32_MAX || cpu->cycles < 0
        || (uint32_t)cpu->cycles > (uint32_t)INT32_MAX - cycles
        || cpu->cycles >= cpu->nextEvent || cpu->irqh.processEvents != cupid_gba_process_cpu_events
        || irq->cpuClockPaused || irq->cpuReady
        || irq->scheduledLineMask || irq->pausedLineMask
        || ready->callback != resume_cpu_clock || ready->context != gba
        || ready->priority != IRQ_SAMPLE_PHASE
        || timing->inCallback || timing->reroot || timing->synchronize
        || (timing->syncClock && (timing->syncClock != cupid_video_clock
            || timing->clockContext != &gba->video))
        || timing->relativeCycles != &cpu->cycles || timing->nextEvent != &cpu->nextEvent
        || (uint32_t)timing->globalCycles != timing->masterCycles) return false;
    uint32_t now = timing->masterCycles + (uint32_t)cpu->cycles;
    if (timing->root && (int32_t)(timing->root->when - now) <= (int32_t)cycles) return false;
    /* No line stage, peripheral callback or intermediate data strobe can see
     * this held interval. The owned renderer observes its ordinary completed
     * tick after release; foreign observers retain the general path. The
     * default release only clears an empty pause and leaves its dequeued
     * event here. Preserve that state and the same completed ready clock. */
    ready->when = now + cycles;
    ready->next = NULL;
    cpu->nextEvent = cpu->cycles + (int32_t)cycles;
    return true;
}

void cupid_irq_hold_cpu(struct CupidGBA* gba, unsigned cycles) {
    if (cycles < 2) abort();
    cupid_irq_pause_cpu(gba);
    /* Use a scheduled ready edge rather than splitting the board tick.
     * Observer, timer, DMA and audio dispatch partitions stay unchanged. */
    /* The completed hold dequeued this owned event before clearing the pause.
     * Its next request needs no cancellation search through peripheral events. */
    cupid_timing_queue_absolute(&gba->timing, &gba->irq.cpuClockResumeEvent,
        (int32_t)(uint32_t)(cupid_timing_global_time(&gba->timing) + cycles));
}

void cupid_irq_hold_cpu_until(struct CupidGBA* gba, bool (*ready)(struct CupidGBA*)) {
    cupid_irq_pause_cpu(gba);
    gba->irq.cpuReady = ready;
    /* The bridge retains the wait across unavailable clocks and partial word
     * beats. Its predicate completes exactly one CPU-ready edge. */
    cupid_timing_queue_absolute(&gba->timing, &gba->irq.cpuClockResumeEvent,
        (int32_t)(uint32_t)(cupid_timing_global_time(&gba->timing) + 1));
}
