/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file control.c
 * @brief Internal-memory address decoding and live opcode-view rebinding.
 *
 * - Swap low memory windows and select the physical internal RAM alias.
 * - Retain supported control bits without copying or clearing RAM.
 * - Rebind future fetches while preserving pipeline samples and jump history.
 */
#include "cupid/gba.h"

/* Decode the low 64 MiB before selecting a memory device. Bit zero swaps
 * the two 32 MiB halves; disabling external RAM then selects internal RAM
 * throughout the external RAM window. Higher regions are unaffected. */
uint32_t cupid_memory_decode_address(const struct CupidMemory *memory, uint32_t address) {
    if (address >= GBA_BASE_IO)
        return address;
    if (memory->internalMemoryControl & 1)
        address ^= GBA_BASE_EWRAM;
    if (!(memory->internalMemoryControl & 0x20) && (address >> GBA_BASE_OFFSET) == GBA_REGION_EWRAM)
        address = (address & GBA_OFFSET_MASK) | GBA_BASE_IWRAM;
    return address;
}

/* Changing the decoder affects future requests, not instructions already in
 * the pipeline. Rebind the direct-fetch view without executing a branch or
 * discarding the previously sampled opcode and input latches. */
void cupid_memory_update_internal_control(struct CupidGBA *gba, uint16_t value) {
    struct CupidMemory *memory = &gba->memory;
    const uint16_t previous = memory->internalMemoryControl;
    memory->internalMemoryControl = value & 0x2F;
    if (!((previous ^ value) & 0x21) || memory->activeRegion < 0)
        return;
    const uint32_t lastJump = gba->lastJump;
    const uint32_t lastPrefetchedPc = memory->lastPrefetchedPc;
    const uint32_t address = (uint32_t)gba->cpu.gprs[ARM_PC];
    if (memory->activeRegion == GBA_REGION_BIOS &&
        (cupid_memory_decode_address(memory, address) >> GBA_BASE_OFFSET) != GBA_REGION_BIOS)
        memory->biosPrefetch = gba->cpu.prefetch[1];
    memory->activeRegion = -1;
    cupid_memory_set_active_region(&gba->cpu, address);
    gba->lastJump = lastJump;
    memory->lastPrefetchedPc = lastPrefetchedPc;
}
