/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/**
 * @file memory.c
 * @brief GBA memory bus: init, load, store, region switching, ROM/BIOS loading.
 */

#include "cupid/gba.h"
#include <errno.h>

/**
 * @brief HLE BIOS stub installed by default and overwritten when a real BIOS is loaded.
 */
extern const uint8_t cupid_hle_bios[];

/**
 * @brief Default wait-state tables per region, indexed by address >> 24.
 */
static const char GBA_BASE_WAITSTATES[16]       = { 0, 0, 2, 0, 0, 0, 0, 0, 4, 4, 4, 4, 4, 4, 4, 0 };
static const char GBA_BASE_WAITSTATES_32[16]    = { 0, 0, 5, 0, 0, 1, 1, 0, 7, 7, 9, 9, 13, 13, 9, 0 };
static const char GBA_BASE_WAITSTATES_SEQ[16]   = { 0, 0, 2, 0, 0, 0, 0, 0, 2, 2, 4, 4, 8, 8, 4, 0 };
static const char GBA_BASE_WAITSTATES_SEQ_32[16]= { 0, 0, 5, 0, 0, 1, 1, 0, 5, 5, 9, 9, 17, 17, 9, 0 };

/**
 * @brief ROM wait-state lookup from WAITCNT register fields.
 */
const char GBA_ROM_WAITSTATES[4]     = { 4, 3, 2, 8 };
const char GBA_ROM_WAITSTATES_SEQ[6] = { 2, 1, 4, 1, 8, 1 };

/**
 * @brief Initialize memory subsystem buffers and CPU memory bus callbacks.
 */

void cupid_memory_init(struct CupidGBA* gba) {
    struct CupidMemory* mem = &gba->memory;

    if (!mem->ewram) mem->ewram = calloc(1, GBA_SIZE_EWRAM);
    if (!mem->iwram) mem->iwram = calloc(1, GBA_SIZE_IWRAM);
    memset(mem->io, 0, sizeof(mem->io));

    /**
     * Always allocate the BIOS buffer and install the HLE BIOS stub.
     * memory->bios starts with the HLE stub and fullBios = 0. If
     * cupid_load_bios() is called later, real BIOS data overwrites this
     * buffer and sets fullBios = 1.
     */
    if (!mem->bios) {
        mem->bios = calloc(1, GBA_SIZE_BIOS);
        memcpy(mem->bios, cupid_hle_bios, GBA_SIZE_BIOS);
    }
    mem->fullBios = 0;

    /** Wire up CPU memory bus callbacks. */
    struct CupidARM* cpu = &gba->cpu;
    cpu->memory.load32  = cupid_memory_load32;
    cpu->memory.load16  = cupid_memory_load16;
    cpu->memory.loadU16 = cupid_memory_loadU16;
    cpu->memory.load8   = cupid_memory_load8;
    cpu->memory.store32 = cupid_memory_store32;
    cpu->memory.store16 = cupid_memory_store16;
    cpu->memory.store8  = cupid_memory_store8;
    cpu->memory.loadMultiple  = cupid_memory_load_multiple;
    cpu->memory.storeMultiple = cupid_memory_store_multiple;
    cpu->memory.setActiveRegion = cupid_memory_set_active_region;
    cpu->memory.stall = cupid_memory_stall;
    cpu->memory.accessSource = CUPID_ACCESS_UNKNOWN;

    mem->activeDMA = -1;
    mem->dmaRunning = false;
    mem->activeRegion = -1;  /** Force full setActiveRegion on first call. */

    cupid_dma_init(gba);
    cupid_savedata_init(&mem->savedata);
}

void cupid_memory_reset(struct CupidGBA* gba) {
    struct CupidMemory* mem = &gba->memory;
    memset(mem->ewram, 0, GBA_SIZE_EWRAM);
    memset(mem->iwram, 0, GBA_SIZE_IWRAM);

    /** Reinstall HLE BIOS stub if no real BIOS is loaded. */
    if (!mem->fullBios && mem->bios) {
        memcpy(mem->bios, cupid_hle_bios, GBA_SIZE_BIOS);
    }
    memset(mem->io, 0, sizeof(mem->io));

    /** Initialize default wait states. */
    int i;
    for (i = 0; i < 16; ++i) {
        mem->waitstatesNonseq16[i] = GBA_BASE_WAITSTATES[i];
        mem->waitstatesSeq16[i]    = GBA_BASE_WAITSTATES_SEQ[i];
        mem->waitstatesNonseq32[i] = GBA_BASE_WAITSTATES_32[i];
        mem->waitstatesSeq32[i]    = GBA_BASE_WAITSTATES_SEQ_32[i];
    }
    for (; i < 256; ++i) {
        mem->waitstatesNonseq16[i] = 0;
        mem->waitstatesSeq16[i]    = 0;
        mem->waitstatesNonseq32[i] = 0;
        mem->waitstatesSeq32[i]    = 0;
    }

    mem->activeRegion = -1;
    mem->biosPrefetch = 0;
    mem->prefetch = false;
    mem->lastPrefetchedPc = 0;
    mem->openBus = 0;
}

void cupid_memory_deinit(struct CupidGBA* gba) {
    struct CupidMemory* mem = &gba->memory;
    free(mem->ewram);  mem->ewram = NULL;
    free(mem->iwram);  mem->iwram = NULL;
    free(mem->rom);    mem->rom = NULL;
    free(mem->bios);   mem->bios = NULL;
    if (mem->savedata.data) {
        free(mem->savedata.data);
        mem->savedata.data = NULL;
    }
}

/**
 * @brief Update wait-state tables from WAITCNT register fields.
 */
void cupid_memory_update_waitstates(struct CupidGBA* gba, uint16_t parameters) {
    struct CupidMemory* memory = &gba->memory;
    struct CupidARM* cpu = &gba->cpu;
    int sram   = parameters & 0x0003;
    int ws0    = (parameters & 0x000C) >> 2;
    int ws0seq = (parameters & 0x0010) >> 4;
    int ws1    = (parameters & 0x0060) >> 5;
    int ws1seq = (parameters & 0x0080) >> 7;
    int ws2    = (parameters & 0x0300) >> 8;
    int ws2seq = (parameters & 0x0400) >> 10;
    int prefetch = parameters & 0x4000;

    memory->waitstatesNonseq16[GBA_REGION_SRAM] = memory->waitstatesNonseq16[GBA_REGION_SRAM_MIRROR] = GBA_ROM_WAITSTATES[sram];
    memory->waitstatesSeq16[GBA_REGION_SRAM]    = memory->waitstatesSeq16[GBA_REGION_SRAM_MIRROR]    = GBA_ROM_WAITSTATES[sram];
    memory->waitstatesNonseq32[GBA_REGION_SRAM] = memory->waitstatesNonseq32[GBA_REGION_SRAM_MIRROR] = 2 * GBA_ROM_WAITSTATES[sram] + 1;
    memory->waitstatesSeq32[GBA_REGION_SRAM]    = memory->waitstatesSeq32[GBA_REGION_SRAM_MIRROR]    = 2 * GBA_ROM_WAITSTATES[sram] + 1;

    memory->waitstatesNonseq16[GBA_REGION_ROM0] = memory->waitstatesNonseq16[GBA_REGION_ROM0_EX] = GBA_ROM_WAITSTATES[ws0];
    memory->waitstatesNonseq16[GBA_REGION_ROM1] = memory->waitstatesNonseq16[GBA_REGION_ROM1_EX] = GBA_ROM_WAITSTATES[ws1];
    memory->waitstatesNonseq16[GBA_REGION_ROM2] = memory->waitstatesNonseq16[GBA_REGION_ROM2_EX] = GBA_ROM_WAITSTATES[ws2];

    memory->waitstatesSeq16[GBA_REGION_ROM0] = memory->waitstatesSeq16[GBA_REGION_ROM0_EX] = GBA_ROM_WAITSTATES_SEQ[ws0seq];
    memory->waitstatesSeq16[GBA_REGION_ROM1] = memory->waitstatesSeq16[GBA_REGION_ROM1_EX] = GBA_ROM_WAITSTATES_SEQ[ws1seq + 2];
    memory->waitstatesSeq16[GBA_REGION_ROM2] = memory->waitstatesSeq16[GBA_REGION_ROM2_EX] = GBA_ROM_WAITSTATES_SEQ[ws2seq + 4];

    memory->waitstatesNonseq32[GBA_REGION_ROM0] = memory->waitstatesNonseq32[GBA_REGION_ROM0_EX] = memory->waitstatesNonseq16[GBA_REGION_ROM0] + 1 + memory->waitstatesSeq16[GBA_REGION_ROM0];
    memory->waitstatesNonseq32[GBA_REGION_ROM1] = memory->waitstatesNonseq32[GBA_REGION_ROM1_EX] = memory->waitstatesNonseq16[GBA_REGION_ROM1] + 1 + memory->waitstatesSeq16[GBA_REGION_ROM1];
    memory->waitstatesNonseq32[GBA_REGION_ROM2] = memory->waitstatesNonseq32[GBA_REGION_ROM2_EX] = memory->waitstatesNonseq16[GBA_REGION_ROM2] + 1 + memory->waitstatesSeq16[GBA_REGION_ROM2];

    memory->waitstatesSeq32[GBA_REGION_ROM0] = memory->waitstatesSeq32[GBA_REGION_ROM0_EX] = 2 * memory->waitstatesSeq16[GBA_REGION_ROM0] + 1;
    memory->waitstatesSeq32[GBA_REGION_ROM1] = memory->waitstatesSeq32[GBA_REGION_ROM1_EX] = 2 * memory->waitstatesSeq16[GBA_REGION_ROM1] + 1;
    memory->waitstatesSeq32[GBA_REGION_ROM2] = memory->waitstatesSeq32[GBA_REGION_ROM2_EX] = 2 * memory->waitstatesSeq16[GBA_REGION_ROM2] + 1;

    memory->prefetch = prefetch;

    cpu->memory.activeSeqCycles32    = memory->waitstatesSeq32[memory->activeRegion];
    cpu->memory.activeSeqCycles16    = memory->waitstatesSeq16[memory->activeRegion];
    cpu->memory.activeNonseqCycles32 = memory->waitstatesNonseq32[memory->activeRegion];
    cpu->memory.activeNonseqCycles16 = memory->waitstatesNonseq16[memory->activeRegion];
}

/**
 * @brief Update EWRAM wait-state timing from control parameters.
 */
void cupid_memory_update_ewram_waitstates(struct CupidGBA* gba, uint16_t parameters) {
    struct CupidMemory* memory = &gba->memory;
    struct CupidARM* cpu = &gba->cpu;

    int wait = 15 - ((parameters >> 8) & 0xF);
    if (wait) {
        memory->waitstatesNonseq16[GBA_REGION_EWRAM] = wait;
        memory->waitstatesSeq16[GBA_REGION_EWRAM]    = wait;
        memory->waitstatesNonseq32[GBA_REGION_EWRAM] = 2 * wait + 1;
        memory->waitstatesSeq32[GBA_REGION_EWRAM]    = 2 * wait + 1;

        cpu->memory.activeSeqCycles32    = memory->waitstatesSeq32[memory->activeRegion];
        cpu->memory.activeSeqCycles16    = memory->waitstatesSeq16[memory->activeRegion];
        cpu->memory.activeNonseqCycles32 = memory->waitstatesNonseq32[memory->activeRegion];
        cpu->memory.activeNonseqCycles16 = memory->waitstatesNonseq16[memory->activeRegion];
    }
}

/**
 * @brief Illegal instruction bytes used as dead region for invalid jumps.
 */
static uint8_t _deadbeef[4] = { 0x10, 0xB7, 0x10, 0xE7 };

/**
 * @brief Return open-bus data for invalid or protected reads.
 *
 * Returns the prefetch pipeline value, swizzled for Thumb mode.
 * During DMA (or the instruction right after DMA starts), returns gba->bus,
 * which holds the last value driven by DMA or the pipeline.
 */
uint32_t cupid_memory_open_bus(struct CupidARM* cpu) {
    struct CupidGBA* gba = cpu->master;
    uint32_t value = 0;
    if (gba->performingDMA ||
        cpu->gprs[ARM_PC] - gba->dmaPC ==
            (uint32_t)(cpu->executionMode == MODE_THUMB ? WORD_SIZE_THUMB : WORD_SIZE_ARM)) {
        return gba->bus;
    }
    value = cpu->prefetch[1];
    if (cpu->executionMode == MODE_THUMB) {
        switch (cpu->gprs[ARM_PC] >> GBA_BASE_OFFSET) {
        case GBA_REGION_BIOS:
        case GBA_REGION_OAM:
            value <<= 16;
            value |= cpu->prefetch[0];
            break;
        case GBA_REGION_IWRAM:
            if (cpu->gprs[ARM_PC] & 2) {
                value <<= 16;
                value |= cpu->prefetch[0];
            } else {
                value |= cpu->prefetch[0] << 16;
            }
            break;
        default:
            value |= value << 16;
            break;
        }
    }
    return value;
}

/**
 * @brief Set the active execution region after a branch.
 *
 * Records lastJump, resets prefetch state, handles fast-path updates when the
 * region is unchanged, saves BIOS prefetch when leaving BIOS, updates
 * accessSource and region pointers, and updates cycle cost tables.
 */
void cupid_memory_set_active_region(struct CupidARM* cpu, uint32_t address) {
    struct CupidGBA* gba = cpu->master;
    struct CupidMemory* memory = &gba->memory;
    int newRegion = address >> GBA_BASE_OFFSET;

    gba->lastJump = address;
    memory->lastPrefetchedPc = 0;

    /** Fast path: same region, only adjust mask alignment. */
    if (newRegion == memory->activeRegion) {
        if (cpu->cpsr.t) {
            cpu->memory.activeMask |= WORD_SIZE_THUMB;
        } else {
            cpu->memory.activeMask &= -WORD_SIZE_ARM;
        }
        if (newRegion < GBA_REGION_ROM0 || (address & (GBA_SIZE_ROM - 1)) < memory->romSize) {
            return;
        }
    }

    /** Save BIOS prefetch when leaving the BIOS region. */
    if (memory->activeRegion == GBA_REGION_BIOS) {
        memory->biosPrefetch = cpu->prefetch[1];
    }

    memory->activeRegion = newRegion;
    switch (newRegion) {
    case GBA_REGION_BIOS:
        cpu->memory.accessSource = CUPID_ACCESS_SYSTEM;
        cpu->memory.activeRegion = (const uint32_t*)memory->bios;
        cpu->memory.activeMask = GBA_SIZE_BIOS - 1;
        break;
    case GBA_REGION_EWRAM:
        cpu->memory.accessSource = CUPID_ACCESS_PROGRAM;
        cpu->memory.activeRegion = (const uint32_t*)memory->ewram;
        cpu->memory.activeMask = GBA_SIZE_EWRAM - 1;
        break;
    case GBA_REGION_IWRAM:
        cpu->memory.accessSource = CUPID_ACCESS_PROGRAM;
        cpu->memory.activeRegion = (const uint32_t*)memory->iwram;
        cpu->memory.activeMask = GBA_SIZE_IWRAM - 1;
        break;
    case GBA_REGION_PALETTE:
        cpu->memory.accessSource = CUPID_ACCESS_PROGRAM;
        cpu->memory.activeRegion = (const uint32_t*)gba->video.palette;
        cpu->memory.activeMask = GBA_SIZE_PALETTE - 1;
        break;
    case GBA_REGION_VRAM:
        cpu->memory.accessSource = CUPID_ACCESS_PROGRAM;
        if (address & 0x10000) {
            cpu->memory.activeRegion = (const uint32_t*)((uint8_t*)gba->video.vram + 0x10000);
            cpu->memory.activeMask = 0x00007FFF;
        } else {
            cpu->memory.activeRegion = (const uint32_t*)gba->video.vram;
            cpu->memory.activeMask = 0x0000FFFF;
        }
        break;
    case GBA_REGION_OAM:
        cpu->memory.accessSource = CUPID_ACCESS_PROGRAM;
        cpu->memory.activeRegion = (const uint32_t*)gba->video.oam.raw;
        cpu->memory.activeMask = GBA_SIZE_OAM - 1;
        break;
    case GBA_REGION_ROM0:
    case GBA_REGION_ROM0_EX:
    case GBA_REGION_ROM1:
    case GBA_REGION_ROM1_EX:
    case GBA_REGION_ROM2:
    case GBA_REGION_ROM2_EX:
        cpu->memory.accessSource = CUPID_ACCESS_PROGRAM;
        cpu->memory.activeRegion = (const uint32_t*)memory->rom;
        cpu->memory.activeMask = memory->romMask;
        if ((address & (GBA_SIZE_ROM - 1)) < memory->romSize) {
            break;
        }
        /* fall through */
    default:
        cpu->memory.accessSource = CUPID_ACCESS_UNKNOWN;
        memory->activeRegion = -1;
        cpu->memory.activeRegion = (const uint32_t*)_deadbeef;
        cpu->memory.activeMask = 0;
        fprintf(stderr, "CUPID: Jumped to invalid address: 0x%08X\n", address);
        return;
    }

    cpu->memory.activeSeqCycles32    = memory->waitstatesSeq32[memory->activeRegion];
    cpu->memory.activeSeqCycles16    = memory->waitstatesSeq16[memory->activeRegion];
    cpu->memory.activeNonseqCycles32 = memory->waitstatesNonseq32[memory->activeRegion];
    cpu->memory.activeNonseqCycles16 = memory->waitstatesNonseq16[memory->activeRegion];
    cpu->memory.activeMask &= -(cpu->cpsr.t ? WORD_SIZE_THUMB : WORD_SIZE_ARM);
}

/**
 * @brief Core load and store handlers for 8/16/32-bit accesses.
 *
 * Handles per-region dispatch, alignment behavior, wait-state penalties,
 * BIOS protection, VRAM mirroring, SRAM 8-bit bus behavior, and open bus.
 */

/**
 * @brief Load 32-bit value from memory bus.
 */
uint32_t cupid_memory_load32(struct CupidARM* cpu, uint32_t address, int* cycleCounter) {
    struct CupidGBA* gba = cpu->master;
    struct CupidMemory* memory = &gba->memory;
    uint32_t value = 0;
    int wait = 0;
    char* waitstatesRegion = memory->waitstatesNonseq32;

    switch (address >> GBA_BASE_OFFSET) {
    case GBA_REGION_BIOS:
        if (address < GBA_SIZE_BIOS) {
            if (memory->activeRegion == GBA_REGION_BIOS) {
                LOAD_32(value, address & -4, memory->bios);
            } else {
                value = memory->biosPrefetch;
            }
        } else {
            value = cupid_memory_open_bus(cpu);
        }
        break;
    case GBA_REGION_EWRAM:
        LOAD_32(value, address & (GBA_SIZE_EWRAM - 4), memory->ewram);
        wait += waitstatesRegion[GBA_REGION_EWRAM];
        break;
    case GBA_REGION_IWRAM:
        LOAD_32(value, address & (GBA_SIZE_IWRAM - 4), memory->iwram);
        break;
    case GBA_REGION_IO:
        value = cupid_io_read(gba, address & GBA_OFFSET_MASK & ~3)
              | ((uint32_t)cupid_io_read(gba, (address & GBA_OFFSET_MASK & ~1) | 2) << 16);
        break;
    case GBA_REGION_PALETTE:
        LOAD_32(value, address & (GBA_SIZE_PALETTE - 4), gba->video.palette);
        wait += waitstatesRegion[GBA_REGION_PALETTE];
        break;
    case GBA_REGION_VRAM:
        if ((address & 0x0001FFFF) >= GBA_SIZE_VRAM) {
            LOAD_32(value, address & 0x00017FFC, gba->video.vram);
        } else {
            LOAD_32(value, address & 0x0001FFFC, gba->video.vram);
        }
        ++wait;
        break;
    case GBA_REGION_OAM:
        LOAD_32(value, address & (GBA_SIZE_OAM - 4), gba->video.oam.raw);
        break;
    case GBA_REGION_ROM0:
    case GBA_REGION_ROM0_EX:
    case GBA_REGION_ROM1:
    case GBA_REGION_ROM1_EX:
    case GBA_REGION_ROM2:
        wait += waitstatesRegion[address >> GBA_BASE_OFFSET];
        if ((address & (GBA_SIZE_ROM - 4)) < memory->romSize) {
            LOAD_32(value, address & (GBA_SIZE_ROM - 4), memory->rom);
        } else {
            value = ((address & ~3) >> 1) & 0xFFFF;
            value |= (((address & ~3) + 2) >> 1) << 16;
        }
        break;
    case GBA_REGION_ROM2_EX:
        wait += waitstatesRegion[address >> GBA_BASE_OFFSET];
        if (memory->savedata.type == SAVE_EEPROM || memory->savedata.type == SAVE_EEPROM512) {
            value = cupid_savedata_read_eeprom(&memory->savedata);
        } else if ((address & (GBA_SIZE_ROM - 4)) < memory->romSize) {
            LOAD_32(value, address & (GBA_SIZE_ROM - 4), memory->rom);
        } else {
            value = ((address & ~3) >> 1) & 0xFFFF;
            value |= (((address & ~3) + 2) >> 1) << 16;
        }
        break;
    case GBA_REGION_SRAM:
    case GBA_REGION_SRAM_MIRROR:
        wait = memory->waitstatesNonseq16[address >> GBA_BASE_OFFSET];
        value = cupid_memory_load8(cpu, address, 0);
        value |= value << 8;
        value |= value << 16;
        break;
    default:
        value = cupid_memory_open_bus(cpu);
        break;
    }

    if (cycleCounter) {
        wait += 2;
        *cycleCounter += wait;
    }
    /** Unaligned 32-bit loads are rotated to match hardware behavior. */
    int rotate = (address & 3) << 3;
    return ROR(value, rotate);
}

/**
 * @brief Load 16-bit unsigned value from memory bus.
 *
 * Sign extension for LDRSH is handled by the instruction decoder.
 */
uint32_t cupid_memory_load16(struct CupidARM* cpu, uint32_t address, int* cycleCounter) {
    struct CupidGBA* gba = cpu->master;
    struct CupidMemory* memory = &gba->memory;
    uint32_t value = 0;
    int wait = 0;

    switch (address >> GBA_BASE_OFFSET) {
    case GBA_REGION_BIOS:
        if (address < GBA_SIZE_BIOS) {
            if (memory->activeRegion == GBA_REGION_BIOS) {
                LOAD_16(value, address & -2, memory->bios);
            } else {
                value = (memory->biosPrefetch >> ((address & 2) * 8)) & 0xFFFF;
            }
        } else {
            value = (cupid_memory_open_bus(cpu) >> ((address & 2) * 8)) & 0xFFFF;
        }
        break;
    case GBA_REGION_EWRAM:
        LOAD_16(value, address & (GBA_SIZE_EWRAM - 2), memory->ewram);
        wait = memory->waitstatesNonseq16[GBA_REGION_EWRAM];
        break;
    case GBA_REGION_IWRAM:
        LOAD_16(value, address & (GBA_SIZE_IWRAM - 2), memory->iwram);
        break;
    case GBA_REGION_IO:
        value = cupid_io_read(gba, address & (GBA_OFFSET_MASK - 1));
        break;
    case GBA_REGION_PALETTE:
        LOAD_16(value, address & (GBA_SIZE_PALETTE - 2), gba->video.palette);
        break;
    case GBA_REGION_VRAM:
        if ((address & 0x0001FFFF) >= GBA_SIZE_VRAM) {
            LOAD_16(value, address & 0x00017FFE, gba->video.vram);
        } else {
            LOAD_16(value, address & 0x0001FFFE, gba->video.vram);
        }
        break;
    case GBA_REGION_OAM:
        LOAD_16(value, address & (GBA_SIZE_OAM - 2), gba->video.oam.raw);
        break;
    case GBA_REGION_ROM0:
    case GBA_REGION_ROM0_EX:
    case GBA_REGION_ROM1:
    case GBA_REGION_ROM1_EX:
    case GBA_REGION_ROM2:
        wait = memory->waitstatesNonseq16[address >> GBA_BASE_OFFSET];
        if ((address & (GBA_SIZE_ROM - 2)) < memory->romSize) {
            LOAD_16(value, address & (GBA_SIZE_ROM - 2), memory->rom);
        } else {
            value = (address >> 1) & 0xFFFF;
        }
        break;
    case GBA_REGION_ROM2_EX:
        wait = memory->waitstatesNonseq16[address >> GBA_BASE_OFFSET];
        if (memory->savedata.type == SAVE_EEPROM || memory->savedata.type == SAVE_EEPROM512) {
            value = cupid_savedata_read_eeprom(&memory->savedata);
        } else if ((address & (GBA_SIZE_ROM - 2)) < memory->romSize) {
            LOAD_16(value, address & (GBA_SIZE_ROM - 2), memory->rom);
        } else {
            /** ROM address past real data returns open-bus-compatible value. */
            value = 1;  /** EEPROM ready bit. */
        }
        break;
    case GBA_REGION_SRAM:
    case GBA_REGION_SRAM_MIRROR:
        wait = memory->waitstatesNonseq16[address >> GBA_BASE_OFFSET];
        value = cupid_memory_load8(cpu, address, 0);
        value |= value << 8;
        break;
    default:
        value = (cupid_memory_open_bus(cpu) >> ((address & 2) * 8)) & 0xFFFF;
        break;
    }

    if (cycleCounter) {
        wait += 2;
        *cycleCounter += wait;
    }
    /** Unaligned 16-bit loads are rotated on real hardware. */
    int rotate = (address & 1) << 3;
    return ROR(value, rotate);
}

/**
 * @brief Legacy wrapper for unsigned 16-bit load.
 */
uint32_t cupid_memory_loadU16(struct CupidARM* cpu, uint32_t address, int* cycleCounter) {
    return cupid_memory_load16(cpu, address, cycleCounter);
}

/**
 * @brief Load 8-bit value from memory bus.
 */
uint32_t cupid_memory_load8(struct CupidARM* cpu, uint32_t address, int* cycleCounter) {
    struct CupidGBA* gba = cpu->master;
    struct CupidMemory* memory = &gba->memory;
    uint32_t value = 0;
    int wait = 0;

    switch (address >> GBA_BASE_OFFSET) {
    case GBA_REGION_BIOS:
        if (address < GBA_SIZE_BIOS) {
            if (memory->activeRegion == GBA_REGION_BIOS) {
                value = ((uint8_t*)memory->bios)[address];
            } else {
                value = (memory->biosPrefetch >> ((address & 3) * 8)) & 0xFF;
            }
        } else {
            value = (cupid_memory_open_bus(cpu) >> ((address & 3) * 8)) & 0xFF;
        }
        break;
    case GBA_REGION_EWRAM:
        value = ((uint8_t*)memory->ewram)[address & (GBA_SIZE_EWRAM - 1)];
        wait = memory->waitstatesNonseq16[GBA_REGION_EWRAM];
        break;
    case GBA_REGION_IWRAM:
        value = ((uint8_t*)memory->iwram)[address & (GBA_SIZE_IWRAM - 1)];
        break;
    case GBA_REGION_IO:
        value = (cupid_io_read(gba, address & 0xFFFE) >> ((address & 1) << 3)) & 0xFF;
        break;
    case GBA_REGION_PALETTE:
        value = ((uint8_t*)gba->video.palette)[address & (GBA_SIZE_PALETTE - 1)];
        break;
    case GBA_REGION_VRAM:
        if ((address & 0x0001FFFF) >= GBA_SIZE_VRAM) {
            value = ((uint8_t*)gba->video.vram)[address & 0x00017FFF];
        } else {
            value = ((uint8_t*)gba->video.vram)[address & 0x0001FFFF];
        }
        break;
    case GBA_REGION_OAM:
        value = ((uint8_t*)gba->video.oam.raw)[address & (GBA_SIZE_OAM - 1)];
        break;
    case GBA_REGION_ROM0:
    case GBA_REGION_ROM0_EX:
    case GBA_REGION_ROM1:
    case GBA_REGION_ROM1_EX:
    case GBA_REGION_ROM2:
    case GBA_REGION_ROM2_EX:
        wait = memory->waitstatesNonseq16[address >> GBA_BASE_OFFSET];
        if ((address & (GBA_SIZE_ROM - 1)) < memory->romSize) {
            value = ((uint8_t*)memory->rom)[address & (GBA_SIZE_ROM - 1)];
        } else {
            value = ((address >> 1) >> ((address & 1) * 8)) & 0xFF;
        }
        break;
    case GBA_REGION_SRAM:
    case GBA_REGION_SRAM_MIRROR:
        wait = memory->waitstatesNonseq16[address >> GBA_BASE_OFFSET];
        if (memory->savedata.type == SAVE_AUTODETECT) {
            cupid_savedata_init_sram(&memory->savedata);
        }
        if (memory->savedata.type == SAVE_FLASH512 || memory->savedata.type == SAVE_FLASH1M) {
            value = cupid_savedata_read_flash(&memory->savedata, address & 0xFFFF);
        } else if (memory->savedata.type == SAVE_SRAM) {
            value = memory->savedata.data[address & (GBA_SIZE_SRAM - 1)];
        } else if (memory->savedata.type == SAVE_SRAM512) {
            value = memory->savedata.data[address & (GBA_SIZE_FLASH512 - 1)];
        } else {
            value = 0xFF;
        }
        value &= 0xFF;
        break;
    default:
        value = (cupid_memory_open_bus(cpu) >> ((address & 3) * 8)) & 0xFF;
        break;
    }

    if (cycleCounter) {
        wait += 2;
        *cycleCounter += wait;
    }
    return value;
}

/**
 * @brief Store 32-bit value to memory bus.
 */
void cupid_memory_store32(struct CupidARM* cpu, uint32_t address, int32_t value, int* cycleCounter) {
    struct CupidGBA* gba = cpu->master;
    struct CupidMemory* memory = &gba->memory;
    int wait = 0;
    char* waitstatesRegion = memory->waitstatesNonseq32;

    switch (address >> GBA_BASE_OFFSET) {
    case GBA_REGION_EWRAM:
        STORE_32(value, address & (GBA_SIZE_EWRAM - 4), memory->ewram);
        wait += waitstatesRegion[GBA_REGION_EWRAM];
        break;
    case GBA_REGION_IWRAM:
        STORE_32(value, address & (GBA_SIZE_IWRAM - 4), memory->iwram);
        break;
    case GBA_REGION_IO:
        cupid_io_write(gba, address & (GBA_OFFSET_MASK - 3), value & 0xFFFF);
        cupid_io_write(gba, (address & (GBA_OFFSET_MASK - 3)) + 2, value >> 16);
        break;
    case GBA_REGION_PALETTE:
        STORE_32(value, address & (GBA_SIZE_PALETTE - 4), gba->video.palette);
        wait += waitstatesRegion[GBA_REGION_PALETTE];
        break;
    case GBA_REGION_VRAM:
        if ((address & 0x0001FFFF) >= GBA_SIZE_VRAM) {
            STORE_32(value, address & 0x00017FFC, gba->video.vram);
        } else {
            STORE_32(value, address & 0x0001FFFC, gba->video.vram);
        }
        ++wait;
        break;
    case GBA_REGION_OAM:
        STORE_32(value, address & (GBA_SIZE_OAM - 4), gba->video.oam.raw);
        break;
    case GBA_REGION_ROM0:
    case GBA_REGION_ROM0_EX:
    case GBA_REGION_ROM1:
    case GBA_REGION_ROM1_EX:
    case GBA_REGION_ROM2:
    case GBA_REGION_ROM2_EX:
        wait += waitstatesRegion[address >> GBA_BASE_OFFSET];
        break;
    case GBA_REGION_SRAM:
    case GBA_REGION_SRAM_MIRROR:
        cupid_memory_store8(cpu, address, value >> (8 * (address & 3)), cycleCounter);
        return;  /** Store8 handles the cycle counter. */
    default:
        break;
    }

    if (cycleCounter) {
        ++wait;
        *cycleCounter += wait;
    }
}

/**
 * @brief Store 16-bit value to memory bus.
 */
void cupid_memory_store16(struct CupidARM* cpu, uint32_t address, int16_t value, int* cycleCounter) {
    struct CupidGBA* gba = cpu->master;
    struct CupidMemory* memory = &gba->memory;
    int wait = 0;

    switch (address >> GBA_BASE_OFFSET) {
    case GBA_REGION_EWRAM:
        STORE_16(value, address & (GBA_SIZE_EWRAM - 2), memory->ewram);
        wait = memory->waitstatesNonseq16[GBA_REGION_EWRAM];
        break;
    case GBA_REGION_IWRAM:
        STORE_16(value, address & (GBA_SIZE_IWRAM - 2), memory->iwram);
        break;
    case GBA_REGION_IO:
        cupid_io_write(gba, address & (GBA_OFFSET_MASK - 1), value);
        break;
    case GBA_REGION_PALETTE:
        STORE_16(value, address & (GBA_SIZE_PALETTE - 2), gba->video.palette);
        break;
    case GBA_REGION_VRAM:
        if ((address & 0x0001FFFF) >= GBA_SIZE_VRAM) {
            STORE_16(value, address & 0x00017FFE, gba->video.vram);
        } else {
            STORE_16(value, address & 0x0001FFFE, gba->video.vram);
        }
        break;
    case GBA_REGION_OAM:
        STORE_16(value, address & (GBA_SIZE_OAM - 2), gba->video.oam.raw);
        break;
    case GBA_REGION_ROM2_EX:
        /** EEPROM writes. */
        if (memory->savedata.type == SAVE_AUTODETECT) {
            memory->savedata.type = SAVE_EEPROM512;
            cupid_savedata_init_eeprom(&memory->savedata);
        }
        if (memory->savedata.type == SAVE_EEPROM || memory->savedata.type == SAVE_EEPROM512) {
            cupid_savedata_write_eeprom(&memory->savedata, value & 1, 1);
        }
        break;
    case GBA_REGION_SRAM:
    case GBA_REGION_SRAM_MIRROR:
        /** SRAM is an 8-bit bus; unaligned 16-bit store shifts down. */
        if (address & 1) {
            value >>= 8;
        }
        cupid_memory_store8(cpu, address, value, cycleCounter);
        return;  /** Store8 handles the cycle counter. */
    default:
        break;
    }

    if (cycleCounter) {
        ++wait;
        *cycleCounter += wait;
    }
}

/**
 * @brief Store 8-bit value to memory bus.
 */
void cupid_memory_store8(struct CupidARM* cpu, uint32_t address, int8_t value, int* cycleCounter) {
    struct CupidGBA* gba = cpu->master;
    struct CupidMemory* memory = &gba->memory;
    int wait = 0;

    switch (address >> GBA_BASE_OFFSET) {
    case GBA_REGION_EWRAM:
        ((int8_t*)memory->ewram)[address & (GBA_SIZE_EWRAM - 1)] = value;
        wait = memory->waitstatesNonseq16[GBA_REGION_EWRAM];
        break;
    case GBA_REGION_IWRAM:
        ((int8_t*)memory->iwram)[address & (GBA_SIZE_IWRAM - 1)] = value;
        break;
    case GBA_REGION_IO:
        cupid_io_write8(gba, address & GBA_OFFSET_MASK, (uint8_t)value);
        break;
    case GBA_REGION_PALETTE:
        /** 8-bit writes duplicate byte to both halves of the halfword. */
        cupid_memory_store16(cpu, address & ~1, ((uint8_t)value) | ((uint8_t)value << 8), cycleCounter);
        return;  /** Store16 handles the cycle counter. */
    case GBA_REGION_VRAM:
    {
        /**
         * 8-bit writes affect BG region only, not OBJ VRAM.
         * In modes 3-5 the boundary is 0x14000, otherwise 0x10000.
         */
        uint32_t vaddr = address & 0x0001FFFF;
        uint32_t limit = (gba->video.dispcnt & 7) >= 3 ? 0x00014000 : 0x00010000;
        if (vaddr >= limit) {
            break;  /** Cannot Store8 to OBJ VRAM. */
        }
        /** Write byte to both halves of the halfword. */
        uint16_t hw = ((uint8_t)value) | ((uint8_t)value << 8);
        STORE_16(hw, (address & 0x1FFFE), gba->video.vram);
        break;
    }
    case GBA_REGION_OAM:
        /** Cannot Store8 to OAM. */
        break;
    case GBA_REGION_ROM0:
        /** Stub: some carts use 8-bit GPIO writes. */
        break;
    case GBA_REGION_SRAM:
    case GBA_REGION_SRAM_MIRROR:
        if (memory->savedata.type == SAVE_AUTODETECT) {
            if ((address & 0xFFFF) == FLASH_BASE_HI && (uint8_t)value == FLASH_COMMAND_START) {
                memory->savedata.type = SAVE_FLASH512;
                cupid_savedata_init_flash(&memory->savedata);
            } else {
                cupid_savedata_init_sram(&memory->savedata);
            }
        }
        if (memory->savedata.type == SAVE_FLASH512 || memory->savedata.type == SAVE_FLASH1M) {
            cupid_savedata_write_flash(&memory->savedata, address & 0xFFFF, (uint8_t)value);
        } else if (memory->savedata.type == SAVE_SRAM) {
            if (memory->savedata.data) {
                memory->savedata.data[address & (GBA_SIZE_SRAM - 1)] = value;
                memory->savedata.dirty = true;
            }
        } else if (memory->savedata.type == SAVE_SRAM512) {
            if (memory->savedata.data) {
                memory->savedata.data[address & (GBA_SIZE_FLASH512 - 1)] = value;
                memory->savedata.dirty = true;
            }
        }
        wait = memory->waitstatesNonseq16[GBA_REGION_SRAM];
        break;
    default:
        break;
    }

    if (cycleCounter) {
        ++wait;
        *cycleCounter += wait;
    }
}

/**
 * @brief Helpers.
 */

/**
 * @brief Round up to next power of two.
 */
static uint32_t cupid_to_pow2(uint32_t v) {
    if (!v) return 0;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

/**
 * @brief Load a ROM image into memory.
 */
bool cupid_load_rom(struct CupidGBA* gba, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "CUPID: Failed to open ROM: %s: %s\n", path, strerror(errno));
        return false;
    }

    /** Free any previously loaded ROM. */
    if (gba->memory.rom) {
        free(gba->memory.rom);
        gba->memory.rom = NULL;
        gba->memory.romSize = 0;
        gba->memory.romMask = 0;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fileSize <= 0 || (size_t)fileSize > GBA_SIZE_ROM) {
        fprintf(stderr, "CUPID: Invalid ROM size: %ld\n", fileSize);
        fclose(f);
        return false;
    }

    size_t pristineSize = (size_t)fileSize;

    /** Cap at 32 MiB (one ROM mirror region). */
    if (pristineSize > GBA_SIZE_ROM) {
        pristineSize = GBA_SIZE_ROM;
    }

    /** 1 MiB ROMs (Classic NES series) appear as 4x mirrored. */
    if (pristineSize == 0x00100000) {
        gba->memory.romSize = 0x00400000;
        gba->memory.rom = calloc(1, GBA_SIZE_ROM);
        if (!gba->memory.rom) { fclose(f); return false; }

        size_t rd = fread(gba->memory.rom, 1, pristineSize, f);
        fclose(f);
        if (rd != pristineSize) {
            fprintf(stderr, "CUPID: Short ROM read: %zu / %zu\n", rd, pristineSize);
            free(gba->memory.rom); gba->memory.rom = NULL;
            return false;
        }
        /** Mirror 4x: copy the 1 MiB image three more times. */
        memcpy(gba->memory.rom + 0x100000, gba->memory.rom, 0x100000);
        memcpy(gba->memory.rom + 0x200000, gba->memory.rom, 0x100000);
        memcpy(gba->memory.rom + 0x300000, gba->memory.rom, 0x100000);
    } else {
        /** Normal ROM: allocate rounded up to a power of two size. */
        gba->memory.romSize = pristineSize;
        gba->memory.rom = calloc(1, GBA_SIZE_ROM);
        if (!gba->memory.rom) { fclose(f); return false; }

        size_t rd = fread(gba->memory.rom, 1, pristineSize, f);
        fclose(f);
        if (rd != pristineSize) {
            fprintf(stderr, "CUPID: Short ROM read: %zu / %zu\n", rd, pristineSize);
            free(gba->memory.rom); gba->memory.rom = NULL;
            return false;
        }
    }

    /** Compute romMask as next power-of-two minus one. */
    gba->memory.romMask = cupid_to_pow2((uint32_t)gba->memory.romSize) - 1;

    /**
     * If ROM size is not a power of 2, treat as full 32 MiB to match
     * flash-cart-like behavior.
     */
    if (__builtin_popcount((unsigned)gba->memory.romSize) != 1) {
        gba->memory.romSize = GBA_SIZE_ROM;
        gba->memory.romMask = GBA_SIZE_ROM - 1;
    }

    /**
     * Extract ROM header info for identification.
     * GBA header: title at 0xA0 (12 bytes), game code at 0xAC (4 bytes).
     */
    char title[13] = {0};
    char gameCode[5] = {0};
    if (pristineSize >= 0xB0) {
        memcpy(title, gba->memory.rom + 0xA0, 12);
        memcpy(gameCode, gba->memory.rom + 0xAC, 4);
        /** Store 2-byte ROM ID from game code (bytes 0xAC-0xAD). */
        gba->memory.romID = (uint16_t)(gba->memory.rom[0xAC] | (gba->memory.rom[0xAD] << 8));
    }

    printf("CUPID: Loaded ROM: %s (%zu bytes, mask=0x%08X)\n",
           path, pristineSize, gba->memory.romMask);
    printf("CUPID: Title: \"%.12s\"  Code: \"%.4s\"\n", title, gameCode);

    /** If CPU is already initialized and PC is in ROM, re-arm active region. */
    if (gba->memory.activeRegion >= GBA_REGION_ROM0) {
        gba->cpu.memory.setActiveRegion(&gba->cpu, gba->cpu.gprs[15]);
    }

    /** Detect save type from ROM strings. */
    cupid_savedata_detect_type(&gba->memory.savedata, gba->memory.rom, pristineSize);

    return true;
}

/**
 * @brief Load a BIOS image and enable full BIOS mode.
 */
bool cupid_load_bios(struct CupidGBA* gba, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "CUPID: Failed to open BIOS: %s: %s\n", path, strerror(errno));
        return false;
    }

    /**
     * The BIOS buffer is already allocated by cupid_memory_init with HLE
     * stub data. Overwrite it with the real BIOS image.
     */
    if (!gba->memory.bios) {
        gba->memory.bios = calloc(1, GBA_SIZE_BIOS);
    }
    memset(gba->memory.bios, 0, GBA_SIZE_BIOS);
    size_t nread = fread(gba->memory.bios, 1, GBA_SIZE_BIOS, f);
    fclose(f);

    if (nread != GBA_SIZE_BIOS) {
        fprintf(stderr, "CUPID: BIOS file too small: %zu bytes (expected %d)\n", nread, GBA_SIZE_BIOS);
        /** Still usable, but may be incomplete. */
    }

    gba->memory.fullBios = 1;
    gba->memory.biosLoaded = true;
    gba->hasBios = true;
    gba->biosHLE = false;
    printf("CUPID: Loaded real BIOS: %s (%zu bytes)\n", path, nread);
    return true;
}

/**
 * @brief Block transfer helpers for LDM and STM.
 *
 * Uses existing load32/store32 paths per register for correctness.
 */

static inline int popcount16(int mask) {
    int c = 0;
    for (int i = 0; i < 16; i++) {
        if (mask & (1 << i)) c++;
    }
    return c;
}

uint32_t cupid_memory_load_multiple(struct CupidARM* cpu, uint32_t address,
                                    int mask, enum CupidLSMDirection direction,
                                    int* cycleCounter)
{
    unsigned count = mask ? (unsigned)popcount16(mask) : 16;
    uint32_t writeback = direction & LSM_D ? address - count * 4 : address + count * 4;
    if (direction & LSM_D) address = writeback;
    if ((direction & LSM_B) != ((direction & LSM_D) >> 1)) address += 4;
    address &= 0xFFFFFFFC;

    int region = address >> GBA_BASE_OFFSET;
    int wait = 0;

    /* An empty list transfers PC, with writeback for sixteen registers. */
    if (!mask) mask = 1 << ARM_PC;

    for (int i = 0; i < 16; i++) {
        if (!(mask & (1 << i))) continue;
        int w = 0;
        uint32_t val = cupid_memory_load32(cpu, address, &w);
        wait += w;
        cpu->gprs[i] = (int32_t)val;
        address += 4;
    }

    if (cycleCounter) {
        ++wait; /** +1 internal cycle for LDM. */
        struct CupidGBA* gba = cpu->master;
        if (region < GBA_REGION_ROM0) {
            wait = cupid_memory_stall(cpu, wait);
        }
        (void)gba;
        *cycleCounter += wait;
    }

    return writeback;
}

uint32_t cupid_memory_store_multiple(struct CupidARM* cpu, uint32_t address,
                                     int mask, enum CupidLSMDirection direction,
                                     int* cycleCounter)
{
    unsigned count = mask ? (unsigned)popcount16(mask) : 16;
    uint32_t writeback = direction & LSM_D ? address - count * 4 : address + count * 4;
    if (direction & LSM_D) address = writeback;
    if ((direction & LSM_B) != ((direction & LSM_D) >> 1)) address += 4;
    address &= 0xFFFFFFFC;

    int region = address >> GBA_BASE_OFFSET;
    int wait = 0;

    if (!mask) mask = 1 << ARM_PC;

    for (int i = 0; i < 16; i++) {
        if (!(mask & (1 << i))) continue;
        int32_t val = cpu->gprs[i];
        /** PC is stored as PC+12 in ARM mode (current + 2 instructions). */
        if (i == ARM_PC) {
            val = (uint32_t)val + (cpu->executionMode == MODE_ARM ? WORD_SIZE_ARM : WORD_SIZE_THUMB);
        }
        int w = 0;
        cupid_memory_store32(cpu, address, val, &w);
        wait += w;
        address += 4;
    }

    if (cycleCounter) {
        if (region < GBA_REGION_ROM0) {
            wait = cupid_memory_stall(cpu, wait);
        }
        *cycleCounter += wait;
    }

    return writeback;
}

/**
 * @brief Apply memory stall adjustment for wait cycles.
 *
 * Currently pass-through; prefetch-buffer effects are not yet modeled here.
 */
int32_t cupid_memory_stall(struct CupidARM* cpu, int32_t wait) {
    (void)cpu;
    return wait;
}
