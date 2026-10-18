/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

/* Sample every system clock, including unavailable display clocks. The CPU
 * completes one bus clock only on the final free bridge beat. Input changes
 * at that edge follow the ready sample; a captured stage must survive a hold. */
struct History {
    struct CupidGBA* gba;
    struct CupidTimingEvent event;
    unsigned count;
    bool line[32];
};
struct Outcome { uint32_t value, payload; unsigned clocks; int cycles; };
static const uint32_t regions[] = {GBA_BASE_PALETTE,GBA_BASE_VRAM,GBA_BASE_OAM};
static const uint32_t initial = 0x11223344;
static uint64_t signature = UINT64_C(14695981039346656037);
static void hash(uint64_t value) { signature = (signature ^ value) * UINT64_C(1099511628211); }
static void sample(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct History* h = context;
    if (h->count == 32) abort();
    h->line[h->count++] = h->gba->irq.line;
    if (h->count < 32) cupid_timing_schedule(timing,&h->event,1);
}
static uint8_t* payload(struct CupidGBA* g, unsigned region) {
    return (uint8_t*)(region == 0 ? g->video.palette : region == 1 ? g->video.vram : g->video.oam.raw) + 0x100;
}
static struct CupidGBA* machine(uint64_t origin, unsigned phase, unsigned region, bool seed) {
    struct CupidGBA* g = calloc(1,sizeof(*g));
    if (!g) abort();
    cupid_gba_create(g); cupid_gba_reset(g); cupid_timing_clear(&g->timing); cupid_irq_init(g);
    g->timing.globalCycles = origin; g->timing.masterCycles = (uint32_t)origin;
    g->cpu.cpsr.i = 1; g->cpu.irqDisableLatch = true;
    g->ie = g->irq.pendingIE = IRQ_TIMER0; g->ime = g->irq.pendingIME = 1;
    cupid_video_reset(&g->video); cupid_io_write(g,REG_DISPCNT,0x1404);
    memcpy(payload(g,region),&initial,4);
    cupid_timing_tick(&g->timing,3*1232+40+(int)phase);
    if (seed) cupid_raise_irq_at(g,IRQ_TIMER0,cupid_timing_global_time(&g->timing));
    cupid_timing_tick(&g->timing,1);
    return g;
}
static void destroy(struct CupidGBA* g) { cupid_gba_destroy(g); free(g); }
static struct Outcome request(struct CupidGBA* g, unsigned region, unsigned op) {
    struct Outcome result = {0};
    uint64_t before = cupid_timing_global_time(&g->timing);
    uint32_t address = regions[region]+0x100;
    unsigned width = (op & 1) ? 4 : 2;
    g->cpu.memory.timed = true;
    if (op < 2) {
        result.value = width == 4 ? cupid_memory_load32(&g->cpu,address,&result.cycles)
            : (uint16_t)cupid_memory_load16(&g->cpu,address,&result.cycles);
    } else if (op < 4) {
        if (width == 4) cupid_memory_store32(&g->cpu,address,(int32_t)UINT32_C(0xA1B2C3D4),&result.cycles);
        else cupid_memory_store16(&g->cpu,address,(int16_t)0xC3D4,&result.cycles);
    } else {
        _CupidSetMode(&g->cpu,width == 4 ? MODE_ARM : MODE_THUMB);
        cupid_memory_set_active_region(&g->cpu,address);
        cupid_bus_fetch(&g->cpu,address,width,false); cupid_bus_finish(&g->cpu);
    }
    g->cpu.memory.timed = false;
    result.clocks = (unsigned)(cupid_timing_global_time(&g->timing)-before);
    memcpy(&result.payload,payload(g,region),4);
    return result;
}

static void same_display_state(const struct CupidGBA* a, const struct CupidGBA* b) {
    if (cupid_timing_global_time(&a->timing) != cupid_timing_global_time(&b->timing)
        || a->timing.masterCycles != b->timing.masterCycles
        || a->cpu.cycles != b->cpu.cycles || a->cpu.nextEvent != b->cpu.nextEvent
        || a->irq.line != b->irq.line || a->irq.available != b->irq.available
        || a->irq.cpuClockPaused != b->irq.cpuClockPaused
        || (a->irq.cpuReady != NULL) != (b->irq.cpuReady != NULL)
        || a->irq.cpuClockResumeEvent.when != b->irq.cpuClockResumeEvent.when
        || a->ie != b->ie || a->irqf != b->irqf || a->ime != b->ime
        || memcmp(&a->video.pipeline, &b->video.pipeline, sizeof(a->video.pipeline))
        || memcmp(a->video.bg, b->video.bg, sizeof(a->video.bg))
        || memcmp(a->video.scanline, b->video.scanline, sizeof(a->video.scanline))
        || memcmp(a->video.outputBuffer, b->video.outputBuffer,
                  GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT * sizeof(uint32_t))
        || memcmp(a->video.vram, b->video.vram, GBA_SIZE_VRAM)
        || memcmp(a->video.palette, b->video.palette, GBA_SIZE_PALETTE)
        || memcmp(a->video.oam.raw, b->video.oam.raw, GBA_SIZE_OAM)) abort();
}

static void watch_start(struct History* h, struct CupidGBA* g) {
    *h = (struct History){.gba=g};
    h->event = (struct CupidTimingEvent){.context=h,.callback=sample,
        .priority=9,.name="Observed display request"};
    cupid_timing_schedule(&g->timing,&h->event,1);
}

static int quiet_requests(void) {
    const uint64_t origins[] = {0,INT32_MAX-UINT64_C(4),UINT32_MAX-UINT64_C(4),UINT64_C(0x300000000)};
    const unsigned phases[] = {0,1,2,3,6,7,14,15};
    unsigned checks = 0;
    for (unsigned blank = 0; blank < 2; ++blank)
    for (unsigned origin = 0; origin < 4; ++origin)
    for (unsigned phase = 0; phase < 8; ++phase)
    for (unsigned region = 0; region < 3; ++region) {
        for (unsigned op = 0; op < 6; ++op) {
            struct CupidGBA* control = machine(origins[origin],phases[phase],region,false);
            struct CupidGBA* quiet = machine(origins[origin],phases[phase],region,false);
            if (blank) {
                cupid_io_write(control,REG_DISPCNT,0x80);
                cupid_io_write(quiet,REG_DISPCNT,0x80);
            }
            /* A passive clock observer forces the ordinary event path. The
             * other request has no callback in its first two clocks. Compare
             * complete retained state as well as each original bus result. */
            struct History history;
            watch_start(&history,control);
            struct Outcome a = request(control,region,op), b = request(quiet,region,op);
            cupid_timing_deschedule(&control->timing,&history.event);
            if (a.value != b.value || a.payload != b.payload || a.clocks != b.clocks
                || a.cycles != b.cycles || history.count != a.clocks
                || quiet->irq.cpuClockPaused || quiet->irq.cpuReady
                || cupid_timing_is_scheduled(&quiet->timing,&quiet->irq.cpuClockResumeEvent)) abort();
            same_display_state(control,quiet);
            hash(a.value); hash(a.payload); hash(a.clocks); hash((unsigned)a.cycles);
            ++checks;
            destroy(control); destroy(quiet);
        }
        if (region == 2) continue; /* OAM words use one physical beat. */
        struct CupidGBA* control = machine(origins[origin],phases[phase],region,false);
        struct CupidGBA* quiet = machine(origins[origin],phases[phase],region,false);
        if (blank) {
            cupid_io_write(control,REG_DISPCNT,0x80);
            cupid_io_write(quiet,REG_DISPCNT,0x80);
        }
        struct History history;
        watch_start(&history,control);
        struct CupidGBA* pair[] = {control,quiet};
        for (unsigned half = 0; half < 2; ++half) {
            for (unsigned i = 0; i < 2; ++i) {
                struct CupidGBA* g = pair[i];
                uint32_t address = regions[region]+0x100+half*2;
                cupid_bus_display_beat(&g->cpu,address,half != 0);
                cupid_memory_store16(&g->cpu,address,(int16_t)(half ? 0xA1B2 : 0xC3D4),NULL);
                uint32_t value;
                memcpy(&value,payload(g,region),4);
                if (value != (half ? UINT32_C(0xA1B2C3D4) : UINT32_C(0x1122C3D4))) abort();
                if (!half && (!g->irq.cpuClockPaused || !g->irq.cpuReady
                    || !cupid_timing_is_scheduled(&g->timing,&g->irq.cpuClockResumeEvent)
                    || g->irq.cpuClockResumeEvent.when != (uint32_t)cupid_timing_global_time(&g->timing)+1)) abort();
            }
            /* Drop only the passive observer so the underlying next-event
             * deadlines can also be compared at the visible low halfword. */
            cupid_timing_deschedule(&control->timing,&history.event);
            same_display_state(control,quiet);
            if (!half) {
                history.count = 0;
                cupid_timing_schedule(&control->timing,&history.event,1);
            }
        }
        if (quiet->irq.cpuClockPaused || quiet->irq.cpuReady
            || cupid_timing_is_scheduled(&quiet->timing,&quiet->irq.cpuClockResumeEvent)) abort();
        hash(cupid_timing_global_time(&quiet->timing));
        ++checks;
        destroy(control); destroy(quiet);
    }
    if (checks != 1280) abort();
    printf("checks=%u quiet-state=%016" PRIx64 "\n",checks,signature);
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1],"quiet")) return quiet_requests();
    unsigned input = !strcmp(argv[1],"pulse") ? 0 : !strcmp(argv[1],"level") ? 1
        : !strcmp(argv[1],"captured") ? 2 : !strcmp(argv[1],"edge") ? 3 : 4;
    if (input == 4) return 2;
    const uint64_t origins[] = {0,INT32_MAX-UINT64_C(4),UINT32_MAX-UINT64_C(4),UINT64_C(0x300000000)};
    unsigned checks = 0, mismatches = 0, stretched = 0, captured = 0;
    for (unsigned origin = 0; origin < 4; ++origin) for (unsigned phase = 0; phase < 16; ++phase)
    for (unsigned region = 0; region < 3; ++region) for (unsigned op = 0; op < 6; ++op) {
        unsigned expectedClocks = 0;
        if (input == 3) {
            /* Measure the identical request without an IRQ. Queue the real
             * assertion before the CPU request, accepted on that final edge. */
            struct CupidGBA* control = machine(origins[origin],phase,region,false);
            expectedClocks = request(control,region,op).clocks;
            destroy(control);
            if (!expectedClocks || expectedClocks > 16) return 2;
        }
        struct CupidGBA* g = machine(origins[origin],phase,region,input != 3);
        if (input == 2) { cupid_timing_tick(&g->timing,1); ++captured; }
        if (input == 0 || input == 2) cupid_irq_write(g,REG_IF,IRQ_TIMER0);
        if (input == 3) cupid_raise_irq_at(g,IRQ_TIMER0,cupid_timing_global_time(&g->timing)+expectedClocks-1);
        struct History history = {.gba=g};
        history.event = (struct CupidTimingEvent){.context=&history,.callback=sample,.priority=9,.name="Display ready waveform"};
        cupid_timing_schedule(&g->timing,&history.event,1);
        uint64_t before = cupid_timing_global_time(&g->timing);
        struct Outcome result = request(g,region,op);
        unsigned clocks = result.clocks;
        if (!clocks || clocks > 16 || history.count != clocks) return 2;
        bool valid = input != 3 || clocks == expectedClocks;
        for (unsigned i = 0; i < clocks; ++i) valid &= history.line[i] == (input == 2 && i+1 == clocks);
        if (clocks > 1) ++stretched;
        valid &= result.payload == (op == 2 ? 0x1122C3D4 : op == 3 ? 0xA1B2C3D4 : initial);
        if (op < 2) valid &= result.value == ((op & 1) ? initial : 0x3344);
        valid &= result.cycles == (op < 2 ? 1 : 0);
        for (unsigned i = 0; i < 2; ++i) cupid_bus_advance(&g->cpu,1,false);
        valid &= history.count == clocks+2;
        bool first = input != 3 && (input == 1 || clocks == 1);
        bool second = input == 1 || input == 3;
        valid &= history.line[clocks] == first && history.line[clocks+1] == second;
        if (!valid && mismatches++ < 12) printf("mismatch origin=%u phase=%u region=%u op=%u input=%u clocks=%u end=%u post=%u,%u payload=%08x value=%08x internal=%d\n",
            origin,phase,region,op,input,clocks,history.line[clocks-1],history.line[clocks],history.line[clocks+1],result.payload,result.value,result.cycles);
        hash(before); hash(clocks); hash(result.payload); hash(result.value); hash((unsigned)result.cycles); ++checks;
        destroy(g);
    }
    printf("checks=%u stretched=%u captured=%u mismatches=%u physical-signature=%016" PRIx64 "\n",checks,stretched,captured,mismatches,signature);
    return mismatches ? 1 : 0;
}
