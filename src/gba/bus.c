/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"

void cupid_bus_wait(struct CupidARM* cpu) {
    struct CupidGBA* gba = cpu->master;
    cpu->irqh.processEvents(cpu);
    cupid_dma_kick(gba, cupid_timing_global_time(&gba->timing));
    while (gba->memory.dmaRunning && !gba->memory.busLocked) {
        cpu->cycles = cpu->nextEvent;
        cpu->irqh.processEvents(cpu);
    }
}

void cupid_bus_advance(struct CupidARM* cpu, unsigned cycles, bool occupied) {
    struct CupidGBA* gba = cpu->master;
    gba->memory.busBusy = occupied;
    cpu->cycles += (int32_t)cycles;
    cpu->irqh.processEvents(cpu);
}

void cupid_bus_release(struct CupidARM* cpu) {
    struct CupidGBA* gba = cpu->master;
    gba->memory.busBusy = false;
    cupid_dma_kick(gba, cupid_timing_global_time(&gba->timing));
}

void cupid_bus_fetch(struct CupidARM* cpu, uint32_t address, unsigned width, bool sequential) {
    struct CupidGBA* gba = cpu->master;
    cupid_bus_wait(cpu);
    sequential = sequential && !gba->memory.fetchNonseq;
    unsigned region = address >> GBA_BASE_OFFSET;
    if (region >= GBA_REGION_ROM0 && region <= GBA_REGION_ROM2_EX &&
        !(address & 0x1FFFF)) sequential = false;
    /* ROM mirrors have independently configured waits. The actual fetch
     * address selects the bank even when execution crosses a cached region. */
    unsigned wait;
    if (region >= GBA_REGION_ROM0 && region <= GBA_REGION_ROM2_EX) {
        wait = width == 4
            ? (sequential ? gba->memory.waitstatesSeq32[region] : gba->memory.waitstatesNonseq32[region])
            : (sequential ? gba->memory.waitstatesSeq16[region] : gba->memory.waitstatesNonseq16[region]);
    } else {
        wait = width == 4
            ? (sequential ? cpu->memory.activeSeqCycles32 : cpu->memory.activeNonseqCycles32)
            : (sequential ? cpu->memory.activeSeqCycles16 : cpu->memory.activeNonseqCycles16);
    }
    gba->memory.fetchNonseq = false;
    unsigned cycles = cupid_prefetch_fetch(gba, address, width, wait + 1);
    if (region >= GBA_REGION_PALETTE && region <= GBA_REGION_OAM) {
        unsigned beats = width == 4 && region != GBA_REGION_OAM ? 2 : 1;
        for (unsigned i = 0; i < beats; i++) {
            cupid_bus_advance(cpu, 1, true);
            while (cupid_video_memory_busy(&gba->video, address)) cupid_bus_advance(cpu, 1, true);
        }
    } else cupid_bus_advance(cpu, cycles, true);
    cupid_prefetch_finish_fetch(gba);
    if (region == GBA_REGION_VRAM) cupid_memory_refresh_vram_fetch(cpu, address);
    /* The caller reads the opcode before releasing ownership of the bus. */
    gba->memory.lastWasDMA = false;
}

void cupid_bus_finish(struct CupidARM* cpu) {
    struct CupidGBA* gba = cpu->master;
    cupid_bus_release(cpu);
    cpu->irqh.processEvents(cpu);
    cupid_prefetch_update(gba, cupid_timing_global_time(&gba->timing));
    cupid_dma_kick(gba, cupid_timing_global_time(&gba->timing));
}

void cupid_bus_lock(struct CupidARM* cpu, bool locked) {
    struct CupidGBA* gba = cpu->master;
    if (locked) cupid_bus_wait(cpu);
    gba->memory.busLocked = locked;
    if (!locked) cupid_bus_release(cpu);
}
