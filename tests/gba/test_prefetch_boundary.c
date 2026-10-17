/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"

static struct CupidGBA* machines[3];

static uint16_t rom_halfword(uint32_t offset) {
    return (uint16_t)((offset / 2) * 73 ^ (offset >> 17) * 0x7321 ^ 0x5A5A);
}

static uint32_t opcode(struct CupidGBA* gba, uint32_t address, unsigned width) {
    address &= ~(uint32_t)(width - 1);
    return width == 4 ? (uint32_t)cupid_memory_load32(&gba->cpu, address, NULL)
                      : cupid_memory_loadU16(&gba->cpu, address, NULL);
}

static uint64_t fetch(struct CupidGBA* gba, uint32_t address, unsigned width, bool sequential) {
    uint32_t offset = (address & (GBA_SIZE_ROM - 1)) & ~(uint32_t)(width - 1);
    uint32_t expected = rom_halfword(offset);
    if (width == 4) expected |= (uint32_t)rom_halfword(offset + 2) << 16;
    CHECK(opcode(gba, address, width) == expected);
    uint32_t pc = address + 2 * width;
    gba->cpu.gprs[ARM_PC] = (int32_t)pc;
    uint64_t start = cupid_timing_global_time(&gba->timing);
    cupid_bus_fetch(&gba->cpu, address, width, sequential);
    CHECK(opcode(gba, address, width) == expected);
    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == pc);
    cupid_bus_finish(&gba->cpu);
    return cupid_timing_global_time(&gba->timing) - start;
}

static void prepare(struct CupidGBA* gba, uint32_t head, unsigned width,
                    uint16_t waitcnt, uint64_t origin, unsigned idle) {
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = origin;
    gba->timing.masterCycles = (uint32_t)origin;
    gba->audio.lastSample = origin;
    gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + 512;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    gba->cpu.cpsr.t = width == 2;
    gba->cpu.executionMode = width == 2 ? MODE_THUMB : MODE_ARM;
    cupid_memory_set_active_region(&gba->cpu, head - width);
    cupid_memory_update_waitstates(gba, waitcnt);
    fetch(gba, head - width, width, false);
    cupid_bus_advance(&gba->cpu, idle, false);
    cupid_bus_finish(&gba->cpu);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    char* end;
    unsigned long group = strtoul(argv[1], &end, 10);
    CHECK(end != argv[1] && *end == 0 && group < 4);
    unsigned width = group < 2 ? 2 : 4;
    unsigned logicalA1 = group == 3 ? 2 : 0;
    const unsigned nwaits[] = {4, 3, 2, 8};
    const unsigned swaits[3][2] = {{2, 1}, {4, 1}, {8, 1}};
    const unsigned nshift[] = {2, 5, 8}, sshift[] = {4, 7, 10};
    const uint64_t anchors[] = {0x1000, (UINT64_C(1) << 31) - 2,
        (UINT64_C(1) << 32) - 2, (UINT64_C(3) << 32) - 2};
    for (unsigned i = 0; i < 3; ++i) {
        struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
        cupid_gba_create(gba);
        /* The odd ROM windows address the upper 16 MiB. Back those pages
         * with owned data too, so every timing scene uses mapped opcodes. */
        gba->memory.rom = calloc(1, GBA_SIZE_ROM); CHECK(gba->memory.rom);
        gba->memory.romSize = GBA_SIZE_ROM;
        gba->memory.romMask = GBA_SIZE_ROM - 1;
        uint8_t* data = (uint8_t*)gba->memory.rom;
        for (unsigned offset = 0; offset < GBA_SIZE_ROM; offset += 2) {
            uint16_t value = rom_halfword(offset);
            data[offset] = (uint8_t)value;
            data[offset + 1] = (uint8_t)(value >> 8);
        }
        machines[i] = gba;
    }
    unsigned scenes = 0;
    for (unsigned region = 8; region < 14; ++region) {
        unsigned bank = (region - 8) / 2;
        uint32_t boundary = (region << 24) | 0x20000;
        for (unsigned n = 0; n < 4; ++n) for (unsigned s = 0; s < 2; ++s) {
            uint16_t waitcnt = (uint16_t)(0x4000 | (n << nshift[bank]) | (s << sshift[bank]));
            unsigned duty = swaits[bank][s] + 1;
            unsigned nonseq = nwaits[n] + 1 + (width == 4 ? duty : 0);
            for (unsigned filled = 0; filled <= 9; ++filled) {
                for (unsigned phase = 0; phase < (filled < 8 ? duty : 1); ++phase) {
                    unsigned idle = filled < 8 ? filled * duty + phase
                                  : filled == 8 ? 8 * duty : 11 * duty + 1;
                    unsigned count = filled < 8 ? filled : 8;
                    unsigned remaining = filled < 8 ? duty - phase : 0;
                    for (unsigned epoch = 0; epoch < 4; ++epoch) {
                        for (unsigned refill = 0; refill < (group < 2 ? 1u : 2u); ++refill) {
                            bool sequential = group < 2 ? group == 0 : refill == 0;
                            uint64_t origin = anchors[epoch] - nonseq - idle;
                            for (unsigned i = 0; i < 3; ++i) {
                                prepare(machines[i], boundary + (i == 1 ? 0x100 : 0), width, waitcnt, origin, idle);
                                const struct CupidPrefetch* p = &machines[i]->memory.prefetchUnit;
                                CHECK(cupid_timing_global_time(&machines[i]->timing) == anchors[epoch]);
                                CHECK(p->count == count && p->remaining == remaining);
                                CHECK(p->paused == (filled >= 8));
                            }
                            uint32_t address = boundary + logicalA1;
                            uint64_t boundaryCycles = fetch(machines[0], address, width, sequential);
                            /* An unrelated in-page miss already has the correct N
                             * transfer and in-flight cancellation behavior. */
                            uint64_t missCycles = fetch(machines[2], address + 0x100, width, false);
                            CHECK(boundaryCycles == missCycles);
                            CHECK(boundaryCycles == nonseq + (remaining == 1 ? 1u : 0u));
                            /* A populated ordinary page must keep its fast path. */
                            unsigned halfwords = width / 2;
                            unsigned normal = count >= halfwords ? 1
                                            : remaining + (halfwords - count - 1) * duty;
                            CHECK(fetch(machines[1], address + 0x100, width, true) == normal);
                            const struct CupidPrefetch* p = &machines[0]->memory.prefetchUnit;
                            CHECK(p->active && !p->busy && !p->paused && p->count == 0);
                            CHECK(p->head == address + width && p->tail == p->head);
                            CHECK(p->remaining == duty && p->fetchKind == CUPID_PREFETCH_NONE);
                            CHECK(machines[0]->timing.masterCycles == (uint32_t)(anchors[epoch] + boundaryCycles));
                            if (epoch == 1) CHECK(((uint32_t)anchors[epoch] ^ machines[0]->timing.masterCycles) & 0x80000000u);
                            if (epoch >= 2) CHECK(machines[0]->timing.masterCycles < (uint32_t)anchors[epoch]);
                            ++scenes;
                        }
                    }
                }
            }
        }
    }
    for (unsigned i = 0; i < 3; ++i) { cupid_gba_destroy(machines[i]); free(machines[i]); }
    printf("PASS %u boundary/miss/cache scene triples across six ROM windows, all waits and four epochs\n", scenes);
    return 0;
}
