/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"

void cupid_prefetch_update(struct CupidGBA* gba, uint64_t timestamp) {
    struct CupidMemory* memory = &gba->memory;
    if (timestamp < memory->prefetchUnit.epoch) return;
    uint64_t elapsed = timestamp - memory->prefetchUnit.epoch;
    memory->prefetchUnit.epoch = timestamp;
    if (!memory->prefetchUnit.active || memory->prefetchUnit.busy ||
        !memory->prefetchUnit.remaining) return;
    if (elapsed) memory->prefetchUnit.engaged = true;
    unsigned capacity = 8;
    while (elapsed >= memory->prefetchUnit.remaining) {
        elapsed -= memory->prefetchUnit.remaining;
        memory->prefetchUnit.count++;
        memory->prefetchUnit.tail += 2;
        if (!memory->prefetch || memory->prefetchUnit.count >= capacity) {
            memory->prefetchUnit.paused = memory->prefetch;
            memory->prefetchUnit.remaining = 0;
            return;
        }
        memory->prefetchUnit.remaining = memory->prefetchUnit.duty;
    }
    memory->prefetchUnit.remaining -= (unsigned)elapsed;
}

static unsigned stop(struct CupidGBA* gba) {
    struct CupidMemory* memory = &gba->memory;
    unsigned penalty = 0;
    unsigned region = (uint32_t)gba->cpu.gprs[ARM_PC] >> GBA_BASE_OFFSET;
    if (memory->prefetchUnit.active && region >= GBA_REGION_ROM0 && region <= GBA_REGION_ROM2_EX) {
        unsigned remaining = memory->prefetchUnit.remaining;
        if (remaining == 1) penalty = 1;
    }
    memory->prefetchUnit.active = false;
    memory->prefetchUnit.count = 0;
    memory->prefetchUnit.remaining = 0;
    memory->prefetchUnit.paused = false;
    memory->prefetchUnit.engaged = false;
    cupid_cartridge_prefetch_cancel(gba);
    return penalty;
}

unsigned cupid_prefetch_fetch(struct CupidGBA* gba, uint32_t address, unsigned width,
                              unsigned cycles, bool refill) {
    struct CupidMemory* memory = &gba->memory;
    cupid_prefetch_update(gba, cupid_timing_global_time(&gba->timing));
    memory->prefetchUnit.fetchKind = CUPID_PREFETCH_NONE;
    memory->prefetchUnit.address = address;
    unsigned region = address >> GBA_BASE_OFFSET;
    if (region < GBA_REGION_ROM0 || region > GBA_REGION_ROM2_EX) return cycles;
    memory->prefetchUnit.width = width;
    /* Align the CPU's word query, preserving the halfword FIFO cursor. A
     * Thumb-to-ARM transition can leave A1 set in the logical fetch stream. */
    uint32_t lookup = address & ~(uint32_t)(width - 1);
    /* A physical 128 KiB page edge restarts the cartridge transfer. The bus
     * has selected N timing; do not bypass it with a cached or pending hit.
     * Keep the ordinary stop path's in-flight halfword cancellation below. */
    if (memory->prefetchUnit.active && (lookup & 0x1FFFF)) {
        unsigned halfwords = width / 2;
        if (memory->prefetchUnit.count >= halfwords && lookup == memory->prefetchUnit.head) {
            memory->prefetchUnit.count -= halfwords;
            memory->prefetchUnit.head += width;
            if (!memory->prefetchUnit.remaining && memory->prefetch && !memory->prefetchUnit.paused) {
                memory->prefetchUnit.remaining = memory->prefetchUnit.duty;
            }
            memory->prefetchUnit.fetchKind = CUPID_PREFETCH_CACHED;
            cupid_cartridge_prefetch_schedule(gba);
            return 1;
        }
        /* A branch can join a started background read only at the FIFO
         * head. Sequential CPU demand can keep an unstarted burst alive. */
        if (memory->prefetchUnit.remaining &&
            (!refill || (memory->prefetchUnit.engaged && lookup == memory->prefetchUnit.head)) &&
            (lookup == memory->prefetchUnit.tail ||
             (lookup == memory->prefetchUnit.head && memory->prefetchUnit.count < halfwords))) {
            /* A word may already have its first halfword buffered. Only the
             * outstanding halfwords need cartridge clocks; changing CPU
             * instruction width does not invalidate the FIFO's addresses. */
            unsigned cached = lookup == memory->prefetchUnit.head ? memory->prefetchUnit.count : 0;
            unsigned needed = halfwords - cached;
            memory->prefetchUnit.fetchKind = CUPID_PREFETCH_PENDING;
            memory->prefetchUnit.busy = true;
            return memory->prefetchUnit.remaining + (needed - 1) * memory->prefetchUnit.duty;
        }
    }
    bool restart = memory->prefetchUnit.paused;
    unsigned penalty = stop(gba);
    if (memory->prefetchUnit.disabled || restart) {
        cycles = 1 + (width == 4 ? memory->waitstatesNonseq32[region]
                                  : memory->waitstatesNonseq16[region]);
        memory->prefetchUnit.disabled = false;
    }
    memory->prefetchUnit.fetchKind = CUPID_PREFETCH_START;
    memory->prefetchUnit.busy = true;
    /* A forced N transfer still owes the stream-stop arbitration clock. */
    return cycles + penalty;
}

void cupid_prefetch_finish_fetch(struct CupidGBA* gba) {
    struct CupidMemory* memory = &gba->memory;
    uint64_t now = cupid_timing_global_time(&gba->timing);
    cupid_prefetch_update(gba, now);
    if (memory->prefetchUnit.fetchKind == CUPID_PREFETCH_PENDING ||
        memory->prefetchUnit.fetchKind == CUPID_PREFETCH_START) {
        unsigned region = memory->prefetchUnit.address >> GBA_BASE_OFFSET;
        unsigned width = memory->prefetchUnit.width;
        memory->prefetchUnit.active = memory->prefetch;
        if (memory->prefetchUnit.fetchKind == CUPID_PREFETCH_START || !memory->prefetch)
            memory->prefetchUnit.engaged = false;
        memory->prefetchUnit.count = 0;
        memory->prefetchUnit.head = memory->prefetchUnit.address + width;
        memory->prefetchUnit.tail = memory->prefetchUnit.head;
        memory->prefetchUnit.duty = 1 + memory->waitstatesSeq16[region];
        memory->prefetchUnit.remaining = memory->prefetchUnit.duty;
        memory->prefetchUnit.paused = false;
    }
    memory->prefetchUnit.busy = false;
    memory->prefetchUnit.fetchKind = CUPID_PREFETCH_NONE;
    cupid_cartridge_prefetch_schedule(gba);
}

unsigned cupid_prefetch_before_data(struct CupidGBA* gba, uint32_t address) {
    struct CupidMemory* memory = &gba->memory;
    cupid_prefetch_update(gba, cupid_timing_global_time(&gba->timing));
    unsigned region = address >> GBA_BASE_OFFSET;
    bool gamepak = region >= GBA_REGION_ROM0 && region <= GBA_REGION_SRAM_MIRROR;
    unsigned penalty = gamepak ? stop(gba) : 0;
    memory->prefetchUnit.busy = gamepak;
    return penalty;
}

void cupid_prefetch_after_data(struct CupidGBA* gba) {
    cupid_prefetch_update(gba, cupid_timing_global_time(&gba->timing));
    gba->memory.prefetchUnit.busy = false;
}
