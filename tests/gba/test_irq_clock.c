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
int cupid_test_irq_source_probe(int argc, char** argv);
void cupid_test_irq_source_observer(void);
int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "probe")) return cupid_test_irq_source_probe(argc, argv);
    CHECK(argc == 2);
    if (!strcmp(argv[1], "waits")) RUN(ready_edges_and_visible_registers);
    else if (!strcmp(argv[1], "pulses")) RUN(unclocked_pulses_and_latched_stage_ownership);
    else if (!strcmp(argv[1], "dma")) RUN(dma_waits_freeze_sampling_but_internal_cycles_run);
    else if (!strcmp(argv[1], "reset")) RUN(reset_discards_a_latched_pending_cpu_signal);
    else if (!strcmp(argv[1], "observer")) cupid_test_irq_source_observer();
    else CHECK(false);
    printf("Verified %u complete waveform clocks\n", waveformClocks);
    return 0;
}
