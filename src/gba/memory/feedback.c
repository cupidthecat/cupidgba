/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file feedback.c
 * @brief CPU input retention across data reads, opcode fetches and DMA handover.
 *
 * - Capture aligned port data before CPU rotation or sign extension.
 * - Keep IWRAM halfword banks separate from the shared input word.
 * - Preserve BIOS protection and full-word OAM sampling on narrow reads.
 * - Publish opcode samples after the fetch and before CPU bus release.
 */
#include "cupid/gba.h"

/* Byte selection happens after these 32-bit ports supply input. Reuse their
 * halfword sampling rules, which retain the full BIOS/OAM word and apply
 * BIOS protection. Do not infer byte-lane behavior for the other devices. */
void cupid_memory_capture_byte(struct CupidGBA *gba, uint32_t address) {
    const unsigned region = address >> GBA_BASE_OFFSET;
    if (region == GBA_REGION_BIOS || region == GBA_REGION_OAM)
        cupid_memory_capture_halfword(gba, address, 0);
}

/* A word read from a 32-bit device drives every input lane. The caller
 * supplies the decoded address and publishes before CPU alignment rotation
 * and bus release. Narrow external ports keep their existing feedback path. */
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
            return;
    }
    memory->openBus = value;
    memory->inputBusValid = true;
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
        const unsigned shift = (address & 2) * 8;
        input = (memory->iwramBus & ~(UINT32_C(0xFFFF) << shift)) | (uint32_t)value << shift;
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
        /* Unmapped reads do not drive new input. Register-specific I/O
         * feedback is not yet qualified, so retain its existing read path. */
        return;
    }
    memory->openBus = input;
    memory->inputBusValid = true;
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
    memory->openBus = input;
    memory->inputBusValid = true;
}
