#include "cupid/gba.h"

static void schedule_at(struct CupidGBA* gba, struct CupidTimingEvent* event, uint64_t when) {
    cupid_timing_schedule_absolute(&gba->timing, event, (int32_t)(uint32_t)when);
}

static uint64_t current_time(struct CupidGBA* gba, uint32_t late) {
    return (gba->timing.inCallback ? gba->timing.globalCycles
                                  : cupid_timing_global_time(&gba->timing)) - late;
}

static void line_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    struct CupidIRQLineChange* change = context;
    change->gba->irq.line = change->value;
}

static void available_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    struct CupidGBA* gba = context;
    gba->irq.available = gba->irq.pendingAvailable;
}

static void update_lines(struct CupidGBA* gba, uint64_t when) {
    struct CupidIRQState* irq = &gba->irq;
    bool available = (gba->ie & gba->irqf) != 0;
    if (available != irq->logicalAvailable) {
        irq->logicalAvailable = available;
        irq->pendingAvailable = available;
        schedule_at(gba, &irq->availableEvent, when + 1);
    }
    bool line = gba->ime && available;
    if (line != irq->logicalLine) {
        irq->logicalLine = line;
        struct CupidIRQLineChange* change = &irq->lineChanges[irq->nextLineChange];
        irq->nextLineChange = (irq->nextLineChange + 1) % 4;
        change->value = line;
        schedule_at(gba, &change->event, when + 2);
    }
}

static void write_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    struct CupidIRQRegisterWrite* write = context;
    struct CupidGBA* gba = write->gba;
    gba->ie = write->ie;
    gba->irqf = (gba->irqf & ~write->ifClear) | write->ifSet;
    gba->ime = write->ime;
    gba->memory.io[REG_IE >> 1] = gba->ie;
    gba->memory.io[REG_IF >> 1] = gba->irqf;
    gba->memory.io[REG_IME >> 1] = gba->ime;
    update_lines(gba, timing->globalCycles - late);
}

static void queue_write(struct CupidGBA* gba, uint64_t when, unsigned priority,
                        uint16_t ifSet, uint16_t ifClear) {
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
    }
    /* Coalesce changes with the same acceptance deadline. Later deadlines
     * must not publish a newly asserted source through an earlier write. */
    write->ifSet = (write->ifSet & ~ifClear) | ifSet;
    write->ifClear |= ifClear;
    write->ie = gba->irq.pendingIE;
    write->ime = gba->irq.pendingIME;
    write->event.priority = priority;
    schedule_at(gba, &write->event, when);
}

void cupid_irq_init(struct CupidGBA* gba) {
    memset(&gba->irq, 0, sizeof(gba->irq));
    gba->cpu.irqDisableLatch = gba->cpu.cpsr.i;
    gba->irq.availableEvent = (struct CupidTimingEvent){.context = gba,
        .callback = available_callback, .name = "Interrupt available", .priority = 0};
    for (unsigned i = 0; i < 4; i++) {
        struct CupidIRQRegisterWrite* write = &gba->irq.writes[i];
        write->gba = gba;
        write->event = (struct CupidTimingEvent){.context = write,
            .callback = write_callback, .name = "Interrupt register write", .priority = 1};
        struct CupidIRQLineChange* change = &gba->irq.lineChanges[i];
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
    queue_write(gba, cupid_timing_global_time(&gba->timing) + 1, 1,
                0, address == REG_IF ? value : 0);
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
    gba->irq.pendingIF |= source;
    queue_write(gba, timestamp + 1, 0, source, 0);
}

void cupid_raise_irq(struct CupidGBA* gba, enum CupidIRQ source, uint32_t late) {
    cupid_raise_irq_at(gba, source, current_time(gba, late));
}

void cupid_test_irq(struct CupidGBA* gba, uint32_t late) {
    update_lines(gba, current_time(gba, late));
}
