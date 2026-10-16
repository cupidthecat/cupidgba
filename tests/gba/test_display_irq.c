/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
/* Sample every system clock, including unavailable display clocks. The CPU
 * completes one bus clock only on the final free bridge beat. Input changes
 * at that edge follow the ready sample; a captured stage must survive a hold. */
struct History { struct CupidGBA* gba; struct CupidTimingEvent event; unsigned count; bool line[32]; };
static void sample(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late; struct History* h = context;
    if (h->count == 32) abort();
    h->line[h->count++] = h->gba->irq.line;
    if (h->count < 32) cupid_timing_schedule(timing, &h->event, 1);
}
static uint64_t signature = UINT64_C(14695981039346656037);
static void hash(uint64_t value) { signature = (signature ^ value) * UINT64_C(1099511628211); }
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    unsigned input = !strcmp(argv[1], "pulse") ? 0 : !strcmp(argv[1], "level") ? 1 : !strcmp(argv[1], "captured") ? 2 : 3;
    if (input == 3) return 2;
    const uint64_t origins[] = {0, INT32_MAX - UINT64_C(4), UINT32_MAX - UINT64_C(4), UINT64_C(0x300000000)};
    const uint32_t regions[] = {GBA_BASE_PALETTE, GBA_BASE_VRAM, GBA_BASE_OAM};
    unsigned checks = 0, mismatches = 0, stretched = 0, captured = 0;
    for (unsigned origin = 0; origin < 4; ++origin) for (unsigned phase = 0; phase < 16; ++phase)
    for (unsigned region = 0; region < 3; ++region) for (unsigned op = 0; op < 6; ++op)
    {
        struct CupidGBA* g = calloc(1, sizeof(*g)); if (!g) return 2;
        cupid_gba_create(g); cupid_gba_reset(g); cupid_timing_clear(&g->timing); cupid_irq_init(g);
        g->timing.globalCycles = origins[origin]; g->timing.masterCycles = (uint32_t)origins[origin];
        g->cpu.cpsr.i = 1; g->cpu.irqDisableLatch = true;
        g->ie = g->irq.pendingIE = IRQ_TIMER0; g->ime = g->irq.pendingIME = 1;
        cupid_video_reset(&g->video); cupid_io_write(g, REG_DISPCNT, 0x1404);
        uint8_t* bytes = (uint8_t*)(region == 0 ? g->video.palette : region == 1 ? g->video.vram : g->video.oam.raw) + 0x100;
        const uint32_t initial = 0x11223344; memcpy(bytes, &initial, 4);
        cupid_timing_tick(&g->timing, 3 * 1232 + 40 + (int)phase);
        cupid_raise_irq_at(g, IRQ_TIMER0, cupid_timing_global_time(&g->timing)); cupid_timing_tick(&g->timing, 1);
        if (input == 2) { cupid_timing_tick(&g->timing, 1); ++captured; }
        if (input != 1) cupid_irq_write(g, REG_IF, IRQ_TIMER0);
        struct History history = {.gba = g}; history.event = (struct CupidTimingEvent){.context=&history,.callback=sample,.priority=9,.name="Display ready waveform"};
        cupid_timing_schedule(&g->timing, &history.event, 1);
        uint64_t before = cupid_timing_global_time(&g->timing);
        uint32_t address = regions[region] + 0x100, value = 0; unsigned width = (op & 1) ? 4 : 2;
        g->cpu.memory.timed = true; int cycles = 0;
        if (op < 2) value = width == 4 ? cupid_memory_load32(&g->cpu,address,&cycles) : (uint16_t)cupid_memory_load16(&g->cpu,address,&cycles);
        else if (op < 4) {
            if (width == 4) cupid_memory_store32(&g->cpu,address,0xA1B2C3D4,&cycles);
            else cupid_memory_store16(&g->cpu,address,(int16_t)0xC3D4,&cycles);
        } else {
            _CupidSetMode(&g->cpu,width == 4 ? MODE_ARM : MODE_THUMB); cupid_memory_set_active_region(&g->cpu,address);
            cupid_bus_fetch(&g->cpu,address,width,false); cupid_bus_finish(&g->cpu);
        }
        g->cpu.memory.timed = false;
        unsigned clocks = (unsigned)(cupid_timing_global_time(&g->timing) - before);
        if (!clocks || clocks > 16 || history.count != clocks) return 2;
        bool valid = true;
        for (unsigned i = 0; i < clocks; ++i) valid &= history.line[i] == (input == 2 && i + 1 == clocks);
        if (clocks > 1) ++stretched;
        uint32_t payload; memcpy(&payload,bytes,4);
        valid &= payload == (op == 2 ? 0x1122C3D4 : op == 3 ? 0xA1B2C3D4 : initial);
        if (op < 2) valid &= value == (width == 4 ? initial : 0x3344);
        valid &= cycles == (op < 2 ? 1 : 0);
        for (unsigned i = 0; i < 2; ++i) cupid_bus_advance(&g->cpu,1,false);
        valid &= history.count == clocks + 2;
        bool first = input == 1 || clocks == 1;
        valid &= history.line[clocks] == first && history.line[clocks+1] == (input == 1);
        if (!valid && mismatches++ < 12) printf("mismatch origin=%u phase=%u region=%u op=%u input=%u clocks=%u end=%u post=%u,%u payload=%08x value=%08x internal=%d\n",origin,phase,region,op,input,clocks,history.line[clocks-1],history.line[clocks],history.line[clocks+1],payload,value,cycles);
        hash(before); hash(clocks); hash(payload); hash(value); hash((unsigned)cycles); ++checks;
        cupid_gba_destroy(g); free(g);
    }
    printf("checks=%u stretched=%u captured=%u mismatches=%u physical-signature=%016" PRIx64 "\n",checks,stretched,captured,mismatches,signature);
    return mismatches ? 1 : 0;
}
