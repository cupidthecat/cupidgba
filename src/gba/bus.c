/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"

void cupid_bus_wait(struct CupidARM* cpu) {
    struct CupidGBA* gba = cpu->master;
    cpu->irqh.processEvents(cpu);
    if (cpu->halted && gba->cpuInstructionPending) cupid_gba_wait_halt(gba);
    cupid_dma_poll(gba);
    bool waiting = gba->memory.dmaRunning && !gba->memory.busLocked;
    /* The CPU waits for bus ownership; DMA's peripheral clocks still run. */
    if (waiting) cupid_irq_pause_cpu(gba);
    while (gba->memory.dmaRunning && !gba->memory.busLocked) {
        gba->timing.waitingForEvent = true;
        cpu->cycles = cpu->nextEvent;
        cpu->irqh.processEvents(cpu);
        gba->timing.waitingForEvent = false;
    }
    if (waiting) cupid_irq_resume_cpu(gba);
}

void cupid_bus_begin_transfer(struct CupidARM* cpu, unsigned cycles, bool occupied) {
    struct CupidGBA* gba = cpu->master;
    gba->memory.busBusy = occupied;
    /* Wait states stretch one CPU bus cycle. Only its final ready edge
     * advances the IRQ synchronizer; peripheral clocks continue throughout. */
    if (occupied && cycles > 1) cupid_irq_hold_cpu(gba, cycles);
}

void cupid_bus_advance_part(struct CupidARM* cpu, unsigned cycles) {
    cpu->cycles += (int32_t)cycles;
    cpu->irqh.processEvents(cpu);
}

void cupid_bus_advance(struct CupidARM* cpu, unsigned cycles, bool occupied) {
    /* This call consumes the whole interval. Split display/GPIO requests
     * retain begin-transfer/advance-part ownership across their strobes. */
    if (occupied && cycles > 1) {
        struct CupidGBA* gba = cpu->master;
        if (cpu == &gba->cpu && cupid_irq_prepare_quiet_transfer(gba, cycles)) {
            gba->memory.busBusy = true;
            cupid_bus_advance_part(cpu, cycles);
            return;
        }
    }
    cupid_bus_begin_transfer(cpu, cycles, occupied);
    cupid_bus_advance_part(cpu, cycles);
}

/* A PPU bridge beat can finish while the CPU word request remains held.
 * Only the final free beat clocks the CPU IRQ stages. The ready event runs
 * before same-edge MMIO acceptance, matching ordinary stretched transfers. */
static bool display_ready(struct CupidGBA* gba) {
    if (cupid_video_memory_busy(&gba->video, gba->memory.displayAddress)) return false;
    gba->memory.displayBeatReady = true;
    return gba->memory.displayFinal;
}

static bool isolated_first_beat(struct CupidGBA* gba) {
    const struct CupidTiming* timing = &gba->timing;
    const struct CupidARM* cpu = &gba->cpu;
    /* Keep callbacks, clock coordinators, deferred queues and pending CPU
     * line stages on the ordinary event path. Neither peripheral callbacks
     * nor a host clock observer may inspect this first beat. */
    if (gba->irq.cpuClockPaused || gba->irq.scheduledLineMask || gba->irq.pausedLineMask
        || timing->inCallback || timing->reroot || timing->synchronize || timing->syncClock
        || cpu->cycles < 0 || cpu->cycles > INT32_MAX - 2 || cpu->cycles >= cpu->nextEvent)
        return false;
    return cupid_timing_next_event(&gba->timing) > 2;
}

void cupid_bus_display_beat(struct CupidARM* cpu, uint32_t address, bool final) {
    struct CupidGBA* gba = cpu->master;
    gba->memory.displayAddress = address;
    gba->memory.displayBeatReady = false;
    gba->memory.displayFinal = final;
    if (!final && isolated_first_beat(gba)) {
        /* A partial beat cannot release the CPU clock. With both following
         * clocks free of peripheral events, retain its ready event at clock
         * two and test the first beat directly after the ordinary clock-one
         * tick. The renderer still visits that beat and a low halfword can
         * publish before the high half. An unavailable port keeps waiting
         * through the same owned event and the same per-clock predicates. */
        gba->irq.cpuReady = display_ready;
        cupid_irq_hold_cpu(gba, 2);
        cpu->nextEvent = cpu->cycles + 1;
        cupid_bus_advance(cpu, 1, true);
        display_ready(gba);
        if (gba->memory.displayBeatReady) return;
    } else if (!gba->irq.cpuClockPaused) cupid_irq_hold_cpu_until(gba, display_ready);
    else if (gba->irq.cpuReady != display_ready) abort();
    do {
        cupid_bus_advance(cpu, 1, true);
    } while (!gba->memory.displayBeatReady);
}

void cupid_bus_release(struct CupidARM* cpu) {
    struct CupidGBA* gba = cpu->master;
    gba->memory.busBusy = false;
    cupid_dma_poll(gba);
}

void cupid_bus_fetch(struct CupidARM* cpu, uint32_t address, unsigned width, bool sequential) {
    struct CupidGBA* gba = cpu->master;
    cupid_bus_wait(cpu);
    /* A pipeline refill differs from a sequential CPU request whose bus
     * access was made nonsequential by an internal cycle or a page edge. */
    bool refill = !sequential;
    sequential = sequential && !gba->memory.fetchNonseq;
    unsigned region = address >> GBA_BASE_OFFSET;
    /* The page decoder uses the physical opcode address. Retain the logical
     * stream address, including ARM A1, for the FIFO lookup below. */
    if (region >= GBA_REGION_ROM0 && region <= GBA_REGION_ROM2_EX &&
        !((address & ~(uint32_t)(width - 1)) & 0x1FFFF)) sequential = false;
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
    unsigned cycles = cupid_prefetch_fetch(gba, address, width, wait + 1, refill);
    if (region >= GBA_REGION_PALETTE && region <= GBA_REGION_OAM) {
        unsigned beats = width == 4 && region != GBA_REGION_OAM ? 2 : 1;
        for (unsigned i = 0; i < beats; i++) {
            cupid_bus_display_beat(cpu, address + i * 2, i + 1 == beats);
        }
    } else if (!gba->cartridge.readable || !cupid_cartridge_fetch_bus(gba, address, width, cycles)) {
        cupid_bus_advance(cpu, cycles, true);
    }
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
    cupid_dma_poll(gba);
}

void cupid_bus_lock(struct CupidARM* cpu, bool locked) {
    struct CupidGBA* gba = cpu->master;
    if (locked) cupid_bus_wait(cpu);
    gba->memory.busLocked = locked;
    if (!locked) cupid_bus_release(cpu);
}
