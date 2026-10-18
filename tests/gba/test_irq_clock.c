/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

struct Sample { uint64_t clock; uint16_t flags; bool available, line, dma; };
struct History {
    struct CupidGBA* gba;
    struct CupidTimingEvent event;
    struct Sample samples[192];
    uint64_t origin;
    unsigned count, limit;
};
struct Input {
    struct CupidGBA* gba;
    struct CupidTimingEvent event;
    unsigned count, limit;
};
static unsigned waveformClocks;
static void record(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct History* history = context;
    CHECK(history->count < history->limit && history->limit <= 192);
    struct CupidGBA* gba = history->gba;
    struct Sample* sample = &history->samples[history->count++];
    ++waveformClocks;
    *sample = (struct Sample){cupid_timing_global_time(timing), gba->irqf,
        gba->irq.available, gba->irq.line, gba->memory.dmaRunning};
    CHECK(sample->clock == history->origin + history->count);
    if (history->count < history->limit) cupid_timing_schedule(timing, &history->event, 1);
}
static void input(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct Input* change = context;
    if (change->count & 1) cupid_irq_write(change->gba, REG_IF, IRQ_TIMER0);
    else cupid_raise_irq_at(change->gba, IRQ_TIMER0, cupid_timing_global_time(timing));
    if (++change->count < change->limit) cupid_timing_schedule(timing, &change->event, 1);
}
static void acknowledge(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing; (void)late;
    cupid_irq_write(context, REG_IF, IRQ_TIMER0);
}
static void history_start(struct History* history, struct CupidGBA* gba, unsigned limit) {
    *history = (struct History){.gba = gba, .origin = cupid_timing_global_time(&gba->timing), .limit = limit};
    history->event = (struct CupidTimingEvent){.context = history, .callback = record,
        .priority = 8, .name = "CPU IRQ waveform"};
    cupid_timing_schedule(&gba->timing, &history->event, 1);
}
static void input_start(struct Input* change, struct CupidGBA* gba, unsigned first, unsigned limit) {
    *change = (struct Input){.gba = gba, .limit = limit};
    change->event = (struct CupidTimingEvent){.context = change, .callback = input,
        .priority = 0, .name = "IRQ waveform input"};
    cupid_timing_schedule(&gba->timing, &change->event, (int32_t)first);
}
static void program(struct CupidGBA* gba, uint32_t opcode) {
    for (unsigned i = 0; i < 1024; i += 4) {
        const uint32_t nop = 0xE1A00000;
        memcpy(gba->memory.iwram + 0x1000 + i, &nop, 4);
    }
    memcpy(gba->memory.iwram + 0x1000, &opcode, 4);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
    CupidARMWritePC(&gba->cpu);
}
static struct CupidGBA* machine(uint64_t origin, uint16_t waitcnt) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba); cupid_gba_reset(gba);
    gba->memory.rom = calloc(1, 0x40000); CHECK(gba->memory.rom);
    gba->memory.romSize = 0x40000; gba->memory.romMask = 0x3FFFF;
    cupid_memory_update_waitstates(gba, waitcnt);
    cupid_timing_clear(&gba->timing); cupid_irq_init(gba);
    gba->timing.globalCycles = origin; gba->timing.masterCycles = (uint32_t)origin;
    gba->cpu.cpsr.i = 1; gba->cpu.irqDisableLatch = true;
    /* Establish stable enabled registers before the observed input changes. */
    gba->ie = gba->irq.pendingIE = IRQ_TIMER0;
    gba->ime = gba->irq.pendingIME = 1;
    gba->memory.io[REG_IE >> 1] = IRQ_TIMER0; gba->memory.io[REG_IME >> 1] = 1;
    program(gba, 0xE1A00000);
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }
static void finish_history(struct History* history) {
    for (unsigned step = 0; step < 192 && history->count < history->limit; ++step)
        cupid_gba_step(history->gba);
    CHECK(history->count == history->limit);
}
static void fetch(struct CupidGBA* gba, uint32_t address, unsigned width) {
    cupid_memory_set_active_region(&gba->cpu, address);
    cupid_bus_fetch(&gba->cpu, address, width, false);
    cupid_bus_finish(&gba->cpu);
    cupid_memory_set_active_region(&gba->cpu, GBA_BASE_IWRAM + 0x1000);
}
static const uint64_t origins[] = {0, INT32_MAX - UINT64_C(4), UINT32_MAX - UINT64_C(4), UINT64_C(0x300000000)};

static void ready_edges_and_visible_registers(void) {
    const struct { uint32_t address; unsigned width, clocks; uint16_t waitcnt; } cases[] = {
        {GBA_BASE_IWRAM,2,1,0}, {GBA_BASE_IWRAM,4,1,0},
        {GBA_BASE_EWRAM,2,3,0}, {GBA_BASE_EWRAM,4,6,0},
        {GBA_BASE_ROM0,2,5,0}, {GBA_BASE_ROM0,4,8,0},
        {GBA_BASE_ROM1,2,5,0}, {GBA_BASE_ROM1,4,10,0},
        {GBA_BASE_ROM2,2,5,0}, {GBA_BASE_ROM2,4,14,0},
        {GBA_BASE_ROM0,2,9,0xC}, {GBA_BASE_ROM0,4,12,0xC}
    };
    for (unsigned epoch = 0; epoch < 4; ++epoch) for (unsigned item = 0; item < sizeof(cases)/sizeof(cases[0]); ++item) {
        struct CupidGBA* gba = machine(origins[epoch], cases[item].waitcnt);
        struct History history; history_start(&history, gba, cases[item].clocks + 3);
        struct Input change; input_start(&change, gba, 1, 1);
        fetch(gba, cases[item].address + 0x100, cases[item].width);
        if (cupid_timing_global_time(&gba->timing) != origins[epoch] + cases[item].clocks)
            fprintf(stderr, "fetch case %u width=%u address=%08x actual=%" PRIu64 " expected=%u\n", item,
                cases[item].width, cases[item].address, cupid_timing_global_time(&gba->timing)-origins[epoch], cases[item].clocks);
        CHECK(cupid_timing_global_time(&gba->timing) == origins[epoch] + cases[item].clocks);
        finish_history(&history);
        const unsigned firstLine = cases[item].clocks > 3 ? cases[item].clocks + 1 : 4;
        for (unsigned i = 0; i < history.count; ++i) {
            const unsigned clock = i + 1;
            CHECK(history.samples[i].flags == (clock >= 2 ? IRQ_TIMER0 : 0));
            CHECK(history.samples[i].available == (clock >= 3));
            CHECK(history.samples[i].line == (clock >= firstLine));
        }
        CHECK(change.count == 1);
        destroy(gba);
    }
    /* Complete CPU loads and stores must retain their data while short input
     * pulses are invisible to a CPU held in the external data bus cycle. */
    for (unsigned store = 0; store < 2; ++store) {
        struct CupidGBA* gba = machine(0, 0);
        const uint32_t payload = 0xC01DF00D;
        memcpy(gba->memory.ewram + 0x100, &payload, 4);
        program(gba, store ? 0xE5810000 : 0xE5910000);
        gba->cpu.gprs[1] = GBA_BASE_EWRAM + 0x100;
        gba->cpu.gprs[0] = store ? 0x12345678 : 0;
        struct History history; history_start(&history, gba, 12);
        struct Input change; input_start(&change, gba, 1, 1);
        struct CupidTimingEvent ack = {.context = gba, .callback = acknowledge, .priority = 0};
        cupid_timing_schedule(&gba->timing, &ack, 3);
        cupid_gba_step(gba); finish_history(&history);
        uint32_t actual; memcpy(&actual, gba->memory.ewram + 0x100, 4);
        CHECK(actual == (store ? 0x12345678u : payload));
        CHECK((uint32_t)gba->cpu.gprs[0] == (store ? 0x12345678u : payload));
        for (unsigned i = 0; i < history.count; ++i) CHECK(!history.samples[i].line);
        destroy(gba);
    }
}

static void unclocked_pulses_and_latched_stage_ownership(void) {
    for (unsigned epoch = 0; epoch < 4; ++epoch) {
        struct CupidGBA* gba = machine(origins[epoch], 0xC);
        struct History history; history_start(&history, gba, 15);
        struct Input change; input_start(&change, gba, 1, 1);
        struct CupidTimingEvent ack = {.context = gba, .callback = acknowledge, .priority = 0};
        cupid_timing_schedule(&gba->timing, &ack, 3);
        fetch(gba, GBA_BASE_ROM0 + 0x100, 4); finish_history(&history);
        for (unsigned i = 0; i < history.count; ++i) {
            unsigned clock = i + 1;
            CHECK(history.samples[i].flags == (clock >= 2 && clock < 4 ? IRQ_TIMER0 : 0));
            CHECK(history.samples[i].available == (clock >= 3 && clock < 5));
            CHECK(!history.samples[i].line);
        }
        destroy(gba);
        gba = machine(origins[epoch], 0xC);
        history_start(&history, gba, 17);
        cupid_raise_irq_at(gba, IRQ_TIMER0, origins[epoch]);
        ack = (struct CupidTimingEvent){.context = gba, .callback = acknowledge, .priority = 0};
        cupid_timing_schedule(&gba->timing, &ack, 5);
        cupid_timing_tick(&gba->timing, 2);
        CHECK(!gba->irq.line);
        fetch(gba, GBA_BASE_ROM0 + 0x100, 4); finish_history(&history);
        for (unsigned i = 0; i < history.count; ++i) {
            unsigned clock = i + 1;
            CHECK(history.samples[i].flags == (clock < 6 ? IRQ_TIMER0 : 0));
            CHECK(history.samples[i].available == (clock >= 2 && clock < 7));
            CHECK(history.samples[i].line == (clock == 14));
        }
        destroy(gba);
        gba = machine(origins[epoch], 0);
        cupid_memory_update_ewram_waitstates(gba, 0); /* A 32-clock word access. */
        history_start(&history, gba, 35); input_start(&change, gba, 1, 20);
        fetch(gba, GBA_BASE_EWRAM + 0x100, 4); finish_history(&history);
        CHECK(change.count == 20);
        for (unsigned i = 0; i < history.count; ++i) {
            unsigned clock = i + 1;
            CHECK(history.samples[i].flags == (clock >= 2 && clock < 22 && !(clock & 1) ? IRQ_TIMER0 : 0));
            CHECK(history.samples[i].available == (clock >= 3 && clock < 23 && (clock & 1)));
            CHECK(!history.samples[i].line);
        }
        destroy(gba);
    }
}

static void dma_start(struct CupidGBA* gba) {
    for (unsigned i = 0; i < 64; ++i) gba->memory.iwram[0x2000+i] = (uint8_t)(i ^ 0xA5);
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x2000);
    cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 0x2000);
    cupid_dma_write_cnt_lo(gba, 3, 32); cupid_dma_write_cnt(gba, 3, 0x8000);
}
static void dma_waits_freeze_sampling_but_internal_cycles_run(void) {
    for (unsigned epoch = 0; epoch < 4; ++epoch) {
        struct CupidGBA* gba = machine(origins[epoch], 0);
        struct History history; history_start(&history, gba, 192);
        struct Input change; input_start(&change, gba, 4, 20);
        dma_start(gba); cupid_timing_tick(&gba->timing, 2);
        CHECK(gba->memory.dmaRunning);
        fetch(gba, GBA_BASE_IWRAM + 0x1000, 4);
        CHECK(cupid_timing_global_time(&gba->timing) > origins[epoch] + 128);
        CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable);
        CHECK(!memcmp(gba->memory.iwram + 0x2000, gba->memory.ewram + 0x2000, 64));
        finish_history(&history); CHECK(change.count == 20);
        for (unsigned i = 0; i < history.count; ++i) CHECK(!history.samples[i].line);
        destroy(gba);
        gba = machine(origins[epoch], 0);
        program(gba, 0xE0000291); /* MUL r0,r1,r2: four internal cycles. */
        gba->cpu.gprs[1] = 0x11111111; gba->cpu.gprs[2] = 0x33333333;
        history_start(&history, gba, 192); input_start(&change, gba, 2, 1);
        struct CupidTimingEvent ack = {.context = gba, .callback = acknowledge, .priority = 0};
        cupid_timing_schedule(&gba->timing, &ack, 4); dma_start(gba);
        cupid_gba_step(gba);
        CHECK(cupid_timing_global_time(&gba->timing) == origins[epoch] + 5);
        CHECK(gba->memory.dmaRunning && gba->irq.line);
        CHECK((uint32_t)gba->cpu.gprs[0] == UINT32_C(0x11111111) * UINT32_C(0x33333333));
        CHECK(history.count == 5 && history.samples[4].line);
        for (unsigned i = 0; i < 4; ++i) CHECK(!history.samples[i].line);
        cupid_gba_step(gba); CHECK(gba->irq.line && !gba->memory.dmaRunning);
        cupid_gba_step(gba); CHECK(!gba->irq.line);
        CHECK(!memcmp(gba->memory.iwram + 0x2000, gba->memory.ewram + 0x2000, 64));
        destroy(gba);
    }
}
static void reset_discards_a_latched_pending_cpu_signal(void) {
    struct CupidGBA* fresh = calloc(1, sizeof(*fresh)); CHECK(fresh);
    cupid_gba_create(fresh);
    cupid_memory_update_ewram_waitstates(fresh, 0x0D00);
    cupid_memory_set_active_region(&fresh->cpu, GBA_BASE_EWRAM);
    cupid_bus_fetch(&fresh->cpu, GBA_BASE_EWRAM + 0x100, 4, false);
    cupid_bus_finish(&fresh->cpu);
    CHECK(cupid_timing_global_time(&fresh->timing) == 6 && !fresh->irq.line);
    cupid_gba_reset(fresh); destroy(fresh);
    struct CupidGBA* gba = machine(0, 0xC);
    struct Input change; input_start(&change, gba, 1, 1);
    fetch(gba, GBA_BASE_ROM0 + 0x100, 4);
    CHECK(!gba->irq.line && gba->irqf == IRQ_TIMER0);
    cupid_gba_reset(gba);
    CHECK(!gba->irq.line && !gba->irq.available && !gba->irqf && !gba->ie && !gba->ime);
    cupid_timing_clear(&gba->timing); program(gba, 0xE1A00000);
    for (unsigned step = 0; step < 8; ++step) { cupid_gba_step(gba); CHECK(!gba->irq.line); }
    destroy(gba);
}
struct PreparedIRQ {
    struct CupidGBA* gba;
    struct CupidTimingEvent event;
    uint64_t timestamp;
};
static void prepare_irq(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing; (void)late;
    struct PreparedIRQ* input = context;
    cupid_raise_irq_at(input->gba, IRQ_TIMER0, input->timestamp);
}
static void same_ready_edge_retains_input_order(void) {
    const struct { uint32_t address; unsigned width, clocks; } cases[] = {
        {GBA_BASE_IWRAM,2,1}, {GBA_BASE_IWRAM,4,1},
        {GBA_BASE_EWRAM,2,3}, {GBA_BASE_EWRAM,4,6},
        {GBA_BASE_ROM0,2,5}, {GBA_BASE_ROM0,4,8}
    };
    for (unsigned epoch = 0; epoch < 4; ++epoch) for (unsigned i = 0; i < 6; ++i)
    for (unsigned deferred = 0; deferred < 2; ++deferred) {
        struct CupidGBA* gba = machine(origins[epoch], 0);
        struct History history; history_start(&history,gba,cases[i].clocks + 4);
        struct PreparedIRQ input = {.gba=gba,.timestamp=origins[epoch]+cases[i].clocks-1};
        if (deferred) {
            input.event = (struct CupidTimingEvent){.context=&input,.callback=prepare_irq,
                .priority=0,.name="Deferred IRQ preparation"};
            cupid_timing_schedule(&gba->timing,&input.event,1);
        } else cupid_raise_irq_at(gba,IRQ_TIMER0,input.timestamp);
        /* Both preparations accept IF on the final ready edge. Queue creation
         * time cannot move that new level into the already completing sample. */
        fetch(gba,cases[i].address,cases[i].width); finish_history(&history);
        for (unsigned sample = 0; sample < history.count; ++sample) {
            unsigned clock = sample + 1;
            CHECK(history.samples[sample].flags == (clock >= cases[i].clocks ? IRQ_TIMER0 : 0));
            CHECK(history.samples[sample].available == (clock >= cases[i].clocks + 1));
            CHECK(history.samples[sample].line == (clock >= cases[i].clocks + 2));
        }
        destroy(gba);
    }
}

struct QuietClock {
    unsigned count;
    uint64_t clock;
    void (*forward)(void*, uint64_t);
    void* context;
};
struct QuietCPU {
    struct CupidARM* cpu;
    void (*forward)(struct CupidARM*);
    unsigned count;
    bool paused;
};
static struct QuietCPU* quietCPU;
static void quiet_cpu_events(struct CupidARM* cpu) {
    CHECK(quietCPU && quietCPU->cpu==cpu);
    ++quietCPU->count;
    quietCPU->paused=((struct CupidGBA*)cpu->master)->irq.cpuClockPaused;
    quietCPU->forward(cpu);
}
static void quiet_clock(void* context, uint64_t clock) {
    struct QuietClock* observer = context;
    ++observer->count; observer->clock = clock;
    if (observer->forward) observer->forward(observer->context, clock);
}
struct QuietInput {
    struct CupidGBA* gba;
    struct CupidTimingEvent event;
    void (*release)(struct CupidTiming*, void*, uint32_t);
    unsigned count;
    bool paused, inCallback;
};
static void quiet_input(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct QuietInput* input = context;
    ++input->count; input->paused = input->gba->irq.cpuClockPaused;
    cupid_raise_irq_at(input->gba, IRQ_TIMER0, cupid_timing_global_time(timing));
}
static void quiet_release(struct CupidTiming* timing, void* context, uint32_t late) {
    struct QuietInput* input = context;
    ++input->count; input->paused = input->gba->irq.cpuClockPaused;
    input->inCallback = timing->inCallback;
    input->release(timing, input->gba, late);
}
static void same_quiet_state(const struct CupidGBA* a, const struct CupidGBA* b) {
    CHECK(a->timing.globalCycles == b->timing.globalCycles && a->timing.masterCycles == b->timing.masterCycles);
    CHECK(a->cpu.cycles == b->cpu.cycles && a->cpu.nextEvent == b->cpu.nextEvent && a->bus == b->bus);
    CHECK(a->memory.busBusy == b->memory.busBusy && a->memory.busLocked == b->memory.busLocked);
    CHECK(a->irq.line == b->irq.line && a->irq.logicalLine == b->irq.logicalLine);
    CHECK(a->irq.available == b->irq.available && a->irq.logicalAvailable == b->irq.logicalAvailable);
    CHECK(a->irq.cpuClockPaused == b->irq.cpuClockPaused && a->irq.cpuReady == b->irq.cpuReady);
    CHECK(a->irq.scheduledLineMask == b->irq.scheduledLineMask && a->irq.pausedLineMask == b->irq.pausedLineMask);
    CHECK(a->ie == b->ie && a->irqf == b->irqf && a->ime == b->ime);
    CHECK(a->irq.pendingIE == b->irq.pendingIE && a->irq.pendingIF == b->irq.pendingIF && a->irq.pendingIME == b->irq.pendingIME);
    CHECK(a->irq.cpuClockResumeEvent.when == b->irq.cpuClockResumeEvent.when);
    CHECK(!a->irq.cpuClockResumeEvent.next && !b->irq.cpuClockResumeEvent.next);
    CHECK(!cupid_timing_is_scheduled(&a->timing,&a->irq.cpuClockResumeEvent));
    CHECK(!cupid_timing_is_scheduled(&b->timing,&b->irq.cpuClockResumeEvent));
    CHECK(a->timing.inCallback == b->timing.inCallback && a->timing.callbackCycles == b->timing.callbackCycles);
    CHECK((a->timing.root != NULL) == (b->timing.root != NULL));
    CHECK((a->timing.reroot != NULL) == (b->timing.reroot != NULL));
    CHECK(!a->timing.root || a->timing.root->when == b->timing.root->when);
    for (unsigned i=0;i<4;++i) {
        const struct CupidIRQSignalChange* x=&a->irq.lineChanges[i], *y=&b->irq.lineChanges[i];
        CHECK(x->value == y->value && x->remaining == y->remaining && x->pausedPending == y->pausedPending);
        CHECK(x->event.when == y->event.when);
        CHECK(cupid_timing_is_scheduled(&a->timing,&x->event) == cupid_timing_is_scheduled(&b->timing,&y->event));
        x=&a->irq.availableChanges[i]; y=&b->irq.availableChanges[i];
        CHECK(x->value == y->value && x->remaining == y->remaining && x->pausedPending == y->pausedPending);
        CHECK(x->event.when == y->event.when);
        CHECK(cupid_timing_is_scheduled(&a->timing,&x->event) == cupid_timing_is_scheduled(&b->timing,&y->event));
        const struct CupidIRQRegisterWrite* wa=&a->irq.writes[i], *wb=&b->irq.writes[i];
        CHECK(wa->ie == wb->ie && wa->ime == wb->ime && wa->ifSet == wb->ifSet && wa->ifClear == wb->ifClear);
        CHECK(wa->event.when == wb->event.when && wa->event.priority == wb->event.priority);
    }
    const struct CupidTimingEvent* qa[]={a->timing.root,a->timing.reroot};
    const struct CupidTimingEvent* qb[]={b->timing.root,b->timing.reroot};
    for (unsigned i=0;i<2;++i) {
        const struct CupidTimingEvent* x=qa[i], *y=qb[i];
        while (x && y) {
            CHECK(x->when == y->when && x->priority == y->priority && x->callback == y->callback);
            CHECK((x->name != NULL) == (y->name != NULL));
            CHECK(!x->name || !strcmp(x->name,y->name));
            x=x->next; y=y->next;
        }
        CHECK(!x && !y);
    }
    /* Renderer structures contain values and inline fetch buffers. Compare
     * every byte, including latches and the complete output, after both the
     * atomic ready edge and the following peripheral clocks. */
    CHECK(!memcmp(&a->video.pipeline,&b->video.pipeline,sizeof(a->video.pipeline)));
    CHECK(!memcmp(a->video.outputBuffer,b->video.outputBuffer,sizeof(a->video.outputBuffer)));
    CHECK(!memcmp(a->video.scanline,b->video.scanline,sizeof(a->video.scanline)));
    CHECK(!memcmp(a->video.bg,b->video.bg,sizeof(a->video.bg)));
    CHECK(!memcmp(&a->video.blend,&b->video.blend,sizeof(a->video.blend)));
    CHECK(!memcmp(&a->video.win,&b->video.win,sizeof(a->video.win)));
    CHECK(!memcmp(a->video.palette,b->video.palette,sizeof(a->video.palette)));
    CHECK(!memcmp(&a->video.oam,&b->video.oam,sizeof(a->video.oam)));
    CHECK(!memcmp(a->video.vram,b->video.vram,GBA_SIZE_VRAM));
    CHECK(a->video.dispcnt == b->video.dispcnt && a->video.dispstat == b->video.dispstat);
    CHECK(a->video.vcount == b->video.vcount && a->video.mosaic == b->video.mosaic);
    CHECK(a->video.frameCounter == b->video.frameCounter && a->video.displayDmaLatched == b->video.displayDmaLatched);
}
static void quiet_atomic_transfers_retain_ready_edge_state(void) {
    const uint64_t epochs[]={0,INT32_MAX-UINT64_C(4),UINT32_MAX-UINT64_C(4),UINT64_MAX-UINT64_C(4),UINT64_C(0x300000000)};
    const unsigned waits[]={2,3,6,8,12,18}, relatives[]={0,1,3};
    unsigned scenes=0;
    for (unsigned referenceKind=0;referenceKind<2;++referenceKind)
    for (unsigned observerKind=0;observerKind<2;++observerKind)
    for (unsigned e=0;e<5;++e) for (unsigned w=0;w<6;++w)
    for (unsigned r=0;r<3;++r) for (unsigned scenario=0;scenario<8;++scenario) {
        struct CupidGBA* pair[]={machine(epochs[e],0),machine(epochs[e],0)};
        struct QuietInput inputs[2]={0};
        const unsigned clocks=waits[w];
        for (unsigned i=0;i<2;++i) {
            struct CupidGBA* g=pair[i];
            cupid_gba_process_events(g);
            cupid_video_reset(&g->video);
            for (unsigned y=0;y<6;++y) for (unsigned x=0;x<240;++x)
                g->video.vram[y*240+x]=x%7==2 ? 0x7FFF : 0;
            cupid_io_write(g,REG_BG2CNT,0x40);
            cupid_io_write(g,REG_MOSAIC,0x0F0F);
            cupid_io_write(g,REG_DISPCNT,scenario&1 ? 0x80 : 0x403);
            cupid_timing_tick(&g->timing,4*1232+46);
            CHECK(g->video.vcount==4);
            if (!observerKind) { g->timing.syncClock=NULL; g->timing.clockContext=NULL; }
            if (scenario==4) { cupid_raise_irq(g,IRQ_TIMER0,0); cupid_timing_tick(&g->timing,1); }
            g->cpu.cycles=(int32_t)relatives[r];
            /* A real request consumes existing breakpoints at bus entry.
             * Do that before placing an atomic hold across pending stages. */
            cupid_bus_wait(&g->cpu);
            inputs[i].gba=g;
            if ((scenario>=1 && scenario<=3) || scenario==5) {
                inputs[i].event=(struct CupidTimingEvent){.context=&inputs[i],.callback=quiet_input,.priority=8};
                cupid_timing_schedule(&g->timing,&inputs[i].event,
                    (int32_t)(scenario==2 ? clocks : scenario==3 ? clocks-1 : clocks+1));
                if (scenario==5) cupid_timing_interrupt(&g->timing);
            }
            if (scenario==6) g->irq.cpuClockResumeEvent.priority=9;
            if (scenario==7) {
                inputs[i].release=g->irq.cpuClockResumeEvent.callback;
                g->irq.cpuClockResumeEvent.callback=quiet_release;
                g->irq.cpuClockResumeEvent.context=&inputs[i];
            }
        }
        struct QuietClock observer={.forward=pair[0]->timing.syncClock,.context=pair[0]->timing.clockContext};
        struct QuietCPU cpuObserver={.cpu=&pair[0]->cpu,.forward=pair[0]->cpu.irqh.processEvents};
        if (!referenceKind) { pair[0]->timing.clockContext=&observer; pair[0]->timing.syncClock=quiet_clock; }
        else { quietCPU=&cpuObserver; pair[0]->cpu.irqh.processEvents=quiet_cpu_events; }
        for (unsigned i=0;i<2;++i) cupid_bus_advance(&pair[i]->cpu,clocks,true);
        if (!referenceKind) CHECK(observer.count==1 && observer.clock==pair[0]->timing.globalCycles);
        else CHECK(cpuObserver.count==1 && cpuObserver.paused);
        pair[0]->timing.syncClock=observer.forward; pair[0]->timing.clockContext=observer.context;
        same_quiet_state(pair[0],pair[1]);
        CHECK(inputs[0].count==inputs[1].count && inputs[0].paused==inputs[1].paused);
        if (scenario==2) CHECK(inputs[0].count==1 && !inputs[0].paused);
        if (scenario==3 || scenario==7) CHECK(inputs[0].count==1 && inputs[0].paused);
        if (scenario==7) CHECK(inputs[0].inCallback && inputs[1].inCallback);
        for (unsigned i=0;i<2;++i) cupid_timing_tick(&pair[i]->timing,4);
        same_quiet_state(pair[0],pair[1]);
        CHECK(inputs[0].count==inputs[1].count);
        /* Empty bus-entry, completion and partial-advance calls still publish
         * ARM/Thumb open bus and clear early exit. A foreign CPU handler must
         * receive all three calls; none of them advances a board clock. */
        const uint64_t finalClock=pair[0]->timing.globalCycles;
        const unsigned calls=cpuObserver.count;
        for (unsigned i=0;i<2;++i) {
            struct CupidGBA* g=pair[i];
            CHECK(g->cpu.cycles<g->cpu.nextEvent);
            _CupidSetMode(&g->cpu,e&1 ? MODE_THUMB : MODE_ARM);
            g->cpu.prefetch[1]=e&1 ? 0x1234 : 0xA5B61234;
            g->bus=0;g->earlyExit=true;
            cupid_bus_wait(&g->cpu);
            CHECK(g->bus==(e&1 ? 0x12341234u : 0xA5B61234u) && !g->earlyExit);
            g->bus=0;g->earlyExit=true;
            cupid_bus_finish(&g->cpu);
            CHECK(g->bus==(e&1 ? 0x12341234u : 0xA5B61234u) && !g->earlyExit);
            g->bus=0;g->earlyExit=true;
            cupid_bus_advance_part(&g->cpu,0);
            CHECK(g->bus==(e&1 ? 0x12341234u : 0xA5B61234u) && !g->earlyExit);
            CHECK(g->timing.globalCycles==finalClock);
        }
        if (referenceKind) CHECK(cpuObserver.count==calls+3 && !cpuObserver.paused);
        same_quiet_state(pair[0],pair[1]);
        pair[0]->cpu.irqh.processEvents=cpuObserver.forward;
        quietCPU=NULL;
        ++scenes; destroy(pair[0]); destroy(pair[1]);
    }
    CHECK(scenes==2880);
    printf("Verified %u atomic wait scenes with complete renderer, queue, ready-state, input, observer and clock-wrap equality\n",scenes);
}

int cupid_test_irq_source_probe(int argc, char** argv);
void cupid_test_irq_source_observer(void);
int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "probe")) return cupid_test_irq_source_probe(argc, argv);
    CHECK(argc == 2);
    if (!strcmp(argv[1], "waits")) RUN(ready_edges_and_visible_registers);
    else if (!strcmp(argv[1], "pulses")) RUN(unclocked_pulses_and_latched_stage_ownership);
    else if (!strcmp(argv[1], "dma")) RUN(dma_waits_freeze_sampling_but_internal_cycles_run);
    else if (!strcmp(argv[1], "reset")) RUN(reset_discards_a_latched_pending_cpu_signal);
    else if (!strcmp(argv[1], "ordering")) RUN(same_ready_edge_retains_input_order);
    else if (!strcmp(argv[1], "observer")) cupid_test_irq_source_observer();
    else if (!strcmp(argv[1], "quiet")) RUN(quiet_atomic_transfers_retain_ready_edge_state);
    else CHECK(false);
    printf("Verified %u complete waveform clocks\n", waveformClocks);
    return 0;
}
