/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file feedback.c
 * @brief CPU input retention across data reads, opcode fetches and DMA handover.
 *
 * - Capture aligned port data before CPU rotation or sign extension.
 * - Keep IWRAM halfword banks separate from the shared input word.
 * - Retain addressed IWRAM write lanes without publishing CPU store output.
 * - Preserve BIOS protection and full-word OAM sampling on narrow reads.
 * - Publish readable I/O data once, using the register's drive decision.
 * - Publish opcode samples after the fetch and before CPU bus release.
 */
#include "cupid/gba.h"
#include <assert.h>

static uint32_t merge_iwram_lanes(uint32_t retained, uint32_t address, uint32_t value,
                                  unsigned width) {
    assert(width == 1 || width == 2 || width == 4);
    if (width == 4)
        return value;
    const unsigned shift = (address & (4 - width)) * 8;
    const uint32_t mask = (width == 1 ? UINT32_C(0xFF) : UINT32_C(0xFFFF)) << shift;
    return (retained & ~mask) | ((value << shift) & mask);
}

void cupid_memory_retain_iwram_lanes(struct CupidGBA *gba, uint32_t address, uint32_t value,
                                     unsigned width) {
    gba->memory.iwramBus = merge_iwram_lanes(gba->memory.iwramBus, address, value, width);
}

static bool external_data_port(unsigned region) {
    return region == GBA_REGION_EWRAM || region == GBA_REGION_PALETTE ||
           region == GBA_REGION_VRAM ||
           (region >= GBA_REGION_ROM0 && region <= GBA_REGION_SRAM_MIRROR);
}

static void publish_input(struct CupidMemory *memory, uint32_t value) {
    memory->openBus = value;
    memory->inputBusValid = true;
    memory->lastWasDMA = false;
}

/* I/O supplies the value sampled by the existing register dispatch. Publish
 * only a driven CPU read; observer and DMA reads do not enter this path. */
void cupid_memory_capture_io(struct CupidGBA *gba, uint32_t value, unsigned width) {
    assert(width == 1 || width == 2 || width == 4);
    if (width == 1)
        value = (value & UINT32_C(0xFF)) * UINT32_C(0x01010101);
    else if (width == 2) {
        value &= UINT32_C(0xFFFF);
        value |= value << 16;
    }
    publish_input(&gba->memory, value);
}

/* Byte selection happens after these 32-bit ports supply input. Reuse their
 * halfword sampling rules, which retain the full BIOS/OAM word and apply
 * BIOS protection. IWRAM updates its addressed byte in the retained banks;
 * other mapped data ports repeat the sampled byte across the input word. */
void cupid_memory_capture_byte(struct CupidGBA *gba, uint32_t address, uint8_t value) {
    const unsigned region = address >> GBA_BASE_OFFSET;
    if (region == GBA_REGION_IWRAM) {
        struct CupidMemory *memory = &gba->memory;
        memory->iwramBus = merge_iwram_lanes(memory->iwramBus, address, value, 1);
        publish_input(memory, memory->iwramBus);
    } else if (region == GBA_REGION_BIOS || region == GBA_REGION_OAM)
        cupid_memory_capture_halfword(gba, address, 0);
    else if (external_data_port(region)) {
        publish_input(&gba->memory, (uint32_t)value * UINT32_C(0x01010101));
    }
}

/* Publish the assembled data word before CPU rotation and bus release.
 * The value includes any separate bus samples and eight-bit expansion. */
void cupid_memory_capture_word(struct CupidGBA *gba, uint32_t address, uint32_t value) {
    struct CupidMemory *memory = &gba->memory;
    switch (address >> GBA_BASE_OFFSET) {
        case GBA_REGION_BIOS:
            if (address >= GBA_SIZE_BIOS)
                return;
            break;
        case GBA_REGION_IWRAM:
            memory->iwramBus = value;
            break;
        case GBA_REGION_OAM:
            break;
        default:
            if (!external_data_port(address >> GBA_BASE_OFFSET))
                return;
            break;
    }
    publish_input(memory, value);
}

/* Capture input pins before CPU alignment rotation or sign extension. The
 * returned halfword alone loses OAM's other lane and the retained IWRAM bank.
 * Publish before releasing the CPU bus so a waiting DMA sees this read. */
void cupid_memory_capture_halfword(struct CupidGBA *gba, uint32_t address, uint16_t value) {
    struct CupidMemory *memory = &gba->memory;
    const unsigned region = address >> GBA_BASE_OFFSET;
    uint32_t input = (uint32_t)value | (uint32_t)value << 16;
    if (region == GBA_REGION_IWRAM) {
        /* Only the selected sixteen-bit bank changes on a halfword read.
         * A preceding word transfer can leave different data in each bank. */
        input = merge_iwram_lanes(memory->iwramBus, address, value, 2);
        memory->iwramBus = input;
    } else if (region == GBA_REGION_OAM) {
        /* OAM presents its aligned word, even for a narrow CPU request. */
        LOAD_32(input, address & (GBA_SIZE_OAM - 4), gba->video.oam.raw);
    } else if (region == GBA_REGION_BIOS) {
        if (address >= GBA_SIZE_BIOS)
            return;
        if (memory->activeRegion == GBA_REGION_BIOS) {
            LOAD_32(input, address & ~3u, memory->bios);
        } else {
            input = memory->biosPrefetch;
        }
    } else if (region == 1 || region >= 16 || region == GBA_REGION_IO) {
        /* Unmapped reads do not drive new input. I/O publishes through the
         * register dispatch's separate drive decision. */
        return;
    }
    publish_input(memory, input);
}

/* Fast RAM fetches update one sixteen-bit bank in Thumb state. Its other
 * bank can still contain an earlier data access, not the preceding opcode.
 * BIOS and OAM instead present their complete aligned word on either fetch.
 * Other regions keep the existing pipeline reconstruction until qualified. */
void cupid_memory_capture_opcode(struct CupidARM *cpu, uint32_t address, unsigned width,
                                 uint32_t value) {
    struct CupidGBA *gba = cpu->master;
    struct CupidMemory *memory = &gba->memory;
    address = cupid_memory_decode_address(memory, address);
    const unsigned region = address >> GBA_BASE_OFFSET;
    uint32_t input;
    memory->inputBusValid = false;
    if (region == GBA_REGION_IWRAM) {
        if (width == 4) {
            input = value;
        } else {
            const uint32_t half = value;
            const unsigned shift = (address & 2) * 8;
            input = (memory->iwramBus & ~(UINT32_C(0xFFFF) << shift)) | half << shift;
        }
        memory->iwramBus = input;
    } else if (region == GBA_REGION_OAM) {
        LOAD_32(input, address & (GBA_SIZE_OAM - 4), gba->video.oam.raw);
    } else if (region == GBA_REGION_BIOS && address < GBA_SIZE_BIOS) {
        LOAD_32(input, address & ~3u, memory->bios);
    } else {
        return;
    }
    publish_input(memory, input);
}
