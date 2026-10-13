/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"

void cupid_prefetch_update(struct CupidGBA* gba, uint64_t timestamp) {
    struct CupidMemory* memory = &gba->memory;
    if (timestamp < memory->prefetchUnit.epoch) return;
    uint64_t elapsed = timestamp - memory->prefetchUnit.epoch;
    memory->prefetchUnit.epoch = timestamp;
    if (!memory->prefetchUnit.active || memory->prefetchUnit.busy ||
        !memory->prefetchUnit.remaining) return;
    unsigned capacity = 16 / memory->prefetchUnit.width;
    while (elapsed >= memory->prefetchUnit.remaining) {
        elapsed -= memory->prefetchUnit.remaining;
        memory->prefetchUnit.count++;
        memory->prefetchUnit.tail += memory->prefetchUnit.width;
        if (!memory->prefetch || memory->prefetchUnit.count >= capacity) {
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
        if (remaining == 1 || (memory->prefetchUnit.width == 4 &&
            remaining == (memory->prefetchUnit.duty / 2) + 1)) penalty = 1;
    }
    memory->prefetchUnit.active = false;
    memory->prefetchUnit.count = 0;
    memory->prefetchUnit.remaining = 0;
    return penalty;
}

unsigned cupid_prefetch_fetch(struct CupidGBA* gba, uint32_t address, unsigned width,
                              unsigned cycles) {
    struct CupidMemory* memory = &gba->memory;
    cupid_prefetch_update(gba, cupid_timing_global_time(&gba->timing));
    memory->prefetchUnit.fetchKind = CUPID_PREFETCH_NONE;
    memory->prefetchUnit.address = address;
    unsigned region = address >> GBA_BASE_OFFSET;
    if (region < GBA_REGION_ROM0 || region > GBA_REGION_ROM2_EX) return cycles;
    if (memory->prefetchUnit.active && memory->prefetchUnit.width == width) {
        if (memory->prefetchUnit.count && address == memory->prefetchUnit.head) {
            memory->prefetchUnit.count--;
            memory->prefetchUnit.head += width;
            if (!memory->prefetchUnit.remaining && memory->prefetch) {
                memory->prefetchUnit.remaining = memory->prefetchUnit.duty;
            }
            memory->prefetchUnit.fetchKind = CUPID_PREFETCH_CACHED;
            return 1;
        }
        if (memory->prefetchUnit.remaining && address == memory->prefetchUnit.tail) {
            memory->prefetchUnit.fetchKind = CUPID_PREFETCH_PENDING;
            memory->prefetchUnit.busy = true;
            return memory->prefetchUnit.remaining;
        }
    }
    cycles += stop(gba);
    if (memory->prefetchUnit.disabled) {
        cycles = 1 + (width == 4 ? memory->waitstatesNonseq32[region]
                                  : memory->waitstatesNonseq16[region]);
        memory->prefetchUnit.disabled = false;
    }
    memory->prefetchUnit.width = width;
    memory->prefetchUnit.fetchKind = CUPID_PREFETCH_START;
    memory->prefetchUnit.busy = true;
    return cycles;
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
        memory->prefetchUnit.count = 0;
        memory->prefetchUnit.head = memory->prefetchUnit.address + width;
        memory->prefetchUnit.tail = memory->prefetchUnit.head;
        memory->prefetchUnit.duty = 1 + (width == 4 ? memory->waitstatesSeq32[region]
                                                   : memory->waitstatesSeq16[region]);
        memory->prefetchUnit.remaining = memory->prefetchUnit.duty;
    }
    memory->prefetchUnit.busy = false;
    memory->prefetchUnit.fetchKind = CUPID_PREFETCH_NONE;
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
