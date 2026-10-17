/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"

struct Sample { uint64_t clock; uint16_t flags; bool available, line; };
struct History {
    struct CupidGBA* gba;
    struct CupidTimingEvent event;
    struct Sample samples[32];
    uint64_t origin;
    unsigned count, limit;
};
static unsigned waveformClocks;

static uint8_t input_pins(void* context) { (void)context; return 0; }

static void request_irq(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    cupid_raise_irq_at(context, IRQ_TIMER0, cupid_timing_global_time(timing));
}

static void record(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct History* history = context;
    CHECK(history->count < history->limit && history->limit <= 32);
    struct CupidGBA* gba = history->gba;
    struct Sample* sample = &history->samples[history->count++];
    *sample = (struct Sample){cupid_timing_global_time(timing), gba->irqf,
        gba->irq.available, gba->irq.line};
    CHECK(sample->clock == history->origin + history->count);
    ++waveformClocks;
    if (history->count < history->limit)
        cupid_timing_schedule(timing, &history->event, 1);
}

static void capture(struct History* history, bool gpio, unsigned operation,
                    unsigned bank, uint16_t waitcnt, bool sequential,
                    uint64_t epoch, unsigned clocks, unsigned arrival) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 4096);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 4096;
    gba->memory.romMask = 4095;
    cupid_gba_reset(gba);
    /* A constant input port supplies no device-generated interrupt. The only
     * signal in either waveform is the same independent timer request. */
    cupid_cartridge_set_input(gba, input_pins, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 15, NULL);
    cupid_memory_update_waitstates(gba, waitcnt);
    cupid_timing_clear(&gba->timing);
    gba->cpu.cycles = 0;
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    cupid_irq_init(gba);
    cupid_irq_write(gba, REG_IE, IRQ_TIMER0);
    cupid_irq_write(gba, REG_IME, 1);
    cupid_timing_tick(&gba->timing, 3);
    CHECK(!gba->irqf && !gba->irq.available && !gba->irq.line);

    *history = (struct History){.gba = gba,
        .origin = cupid_timing_global_time(&gba->timing), .limit = clocks + 6};
    history->event = (struct CupidTimingEvent){.context = history,
        .callback = record, .priority = 8, .name = "Word request IRQ history"};
    struct CupidTimingEvent request = {.context = gba, .callback = request_irq,
        .priority = 0, .name = "Word request timer input"};
    cupid_timing_schedule(&gba->timing, &request, (int32_t)arrival);
    cupid_timing_schedule(&gba->timing, &history->event, 1);
    const uint32_t address = 0x08000000 + bank * 0x02000000 + (gpio ? 0xC4 : 0x100);
    gba->cpu.memory.timed = true;
    gba->cpu.memory.dataSequential = sequential;
    gba->memory.fetchNonseq = false;
    int cycles = 0;
    if (operation == 0) {
        cupid_bus_fetch(&gba->cpu, address, 4, sequential);
        cupid_bus_finish(&gba->cpu);
    } else if (operation == 1) {
        (void)cupid_memory_load32(&gba->cpu, address, &cycles);
        CHECK(cycles == 1);
    } else {
        cupid_memory_store32(&gba->cpu, address, 0x000F0005, &cycles);
        CHECK(cycles == 0);
    }
    CHECK(cupid_timing_global_time(&gba->timing) == history->origin + clocks);
    CHECK(!gba->memory.busBusy);
    cupid_timing_tick(&gba->timing, 6);
    CHECK(history->count == history->limit);
    const struct Sample* last = &history->samples[history->count - 1];
    CHECK(last->flags == IRQ_TIMER0 && last->available && last->line);
    cupid_gba_destroy(gba);
    free(gba);
    history->gba = NULL;
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    unsigned operation;
    if (!strcmp(argv[1], "fetch")) operation = 0;
    else if (!strcmp(argv[1], "load")) operation = 1;
    else { CHECK(!strcmp(argv[1], "store")); operation = 2; }
    static const unsigned nonseq[] = {5, 4, 3, 9};
    static const unsigned slowSeq[] = {3, 5, 9};
    static const unsigned nshift[] = {2, 5, 8}, sshift[] = {4, 7, 10};
    static const uint64_t epochs[] = {0, INT32_MAX - 4u, UINT32_MAX - 4u, UINT64_C(0x300000000)};
    unsigned cases = 0;
    for (unsigned bank = 0; bank < 3; ++bank) for (unsigned n = 0; n < 4; ++n)
        for (unsigned s = 0; s < 2; ++s) for (unsigned seq = 0; seq < 2; ++seq)
            for (unsigned prefetch = 0; prefetch < 2; ++prefetch)
                for (unsigned epoch = 0; epoch < 4; ++epoch) {
                    const unsigned high = s ? 2 : slowSeq[bank];
                    const unsigned clocks = (seq ? high : nonseq[n]) + high;
                    const uint16_t waitcnt = (uint16_t)((n << nshift[bank]) |
                        (s << sshift[bank]) | (prefetch << 14));
                    for (unsigned arrival = 1; arrival <= clocks + 2; ++arrival) {
                        struct History ordinary, port;
                        capture(&ordinary, false, operation, bank, waitcnt, seq != 0,
                            epochs[epoch], clocks, arrival);
                        capture(&port, true, operation, bank, waitcnt, seq != 0,
                            epochs[epoch], clocks, arrival);
                        CHECK(ordinary.count == port.count);
                        for (unsigned i = 0; i < ordinary.count; ++i) {
                            const struct Sample* a = &ordinary.samples[i];
                            const struct Sample* b = &port.samples[i];
                            CHECK(a->clock == b->clock && a->flags == b->flags);
                            CHECK(a->available == b->available && a->line == b->line);
                        }
                        ++cases;
                    }
                }
    CHECK(cases == 3984);
    CHECK(waveformClocks == 129344);
    printf("%s: %u complete paired word waveforms; %u board-clock observations\n",
        argv[1], cases, waveformClocks);
    return 0;
}
