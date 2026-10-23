/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static uint16_t rom_halfword(uint32_t offset) {
    return (uint16_t)((offset / 2) * 73 ^ (offset >> 17) * 0x7321 ^ 0x5A5A);
}

static void reset(struct CupidGBA* gba, uint64_t origin) {
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = origin;
    gba->timing.masterCycles = (uint32_t)origin;
    gba->audio.lastSample = origin;
    gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + 512;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
}

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba);
    /* Both halves of all three ROM mirrors need mapped, owned opcode data. */
    gba->memory.rom = calloc(1, GBA_SIZE_ROM); CHECK(gba->memory.rom);
    gba->memory.romSize = GBA_SIZE_ROM;
    gba->memory.romMask = GBA_SIZE_ROM - 1;
    for (unsigned offset = 0; offset < GBA_SIZE_ROM; offset += 2) {
        uint16_t value = rom_halfword(offset);
        STORE_16(value, offset, gba->memory.rom);
    }
    return gba;
}

static uint64_t fetch(struct CupidGBA* gba, uint32_t address, unsigned width, bool sequential) {
    uint32_t offset = (address & (GBA_SIZE_ROM - 1)) & ~(uint32_t)(width - 1);
    uint32_t expected = rom_halfword(offset);
    if (width == 4) expected |= (uint32_t)rom_halfword(offset + 2) << 16;
    uint32_t physical = address & ~(uint32_t)(width - 1);
    uint32_t actual = width == 4 ? (uint32_t)cupid_memory_load32(&gba->cpu, physical, NULL)
                               : cupid_memory_loadU16(&gba->cpu, physical, NULL);
    CHECK(actual == expected);
    uint32_t pc = address + 2 * width;
    gba->cpu.gprs[ARM_PC] = (int32_t)pc;
    uint64_t before = cupid_timing_global_time(&gba->timing);
    cupid_bus_fetch(&gba->cpu, address, width, sequential);
    cupid_bus_finish(&gba->cpu);
    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] == pc);
    actual = width == 4 ? (uint32_t)cupid_memory_load32(&gba->cpu, physical, NULL)
                        : cupid_memory_loadU16(&gba->cpu, physical, NULL);
    CHECK(actual == expected);
    return cupid_timing_global_time(&gba->timing) - before;
}

static void prepare(struct CupidGBA* gba, uint32_t head, unsigned width,
                    uint16_t waitcnt, uint64_t origin, unsigned idle) {
    reset(gba, origin);
    gba->cpu.cpsr.t = width == 2;
    gba->cpu.executionMode = width == 2 ? MODE_THUMB : MODE_ARM;
    cupid_memory_set_active_region(&gba->cpu, head - width);
    cupid_io_write(gba, REG_WAITCNT, waitcnt);
    fetch(gba, head - width, width, false);
    cupid_bus_advance(&gba->cpu, idle, false);
    cupid_bus_finish(&gba->cpu);
    /* A live register write arms the restart without replacing FIFO state. */
    cupid_io_write(gba, REG_WAITCNT, waitcnt & ~0x4000u);
    CHECK(!gba->memory.prefetch && gba->memory.prefetchUnit.disabled);
    CHECK(gba->memory.io[REG_WAITCNT >> 1] == (waitcnt & ~0x4000u));
}

static void disabled_misses_and_retained_hits(unsigned group) {
    unsigned width = group == 0 ? 2 : 4;
    unsigned logicalA1 = group == 2 ? 2 : 0;
    const unsigned nwaits[] = {4, 3, 2, 8};
    const unsigned swaits[3][2] = {{2, 1}, {4, 1}, {8, 1}};
    const unsigned nshift[] = {2, 5, 8}, sshift[] = {4, 7, 10};
    const uint64_t anchors[] = {0x1000, (UINT64_C(1) << 31) - 2,
        (UINT64_C(1) << 32) - 2, (UINT64_C(3) << 32) - 2};
    struct CupidGBA* machines[] = {machine(), machine(), machine()};
    unsigned scenes = 0, completing = 0, hits = 0;
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
                        for (unsigned refill = 0; refill < 2; ++refill) {
                            uint64_t origin = anchors[epoch] - nonseq - idle;
                            for (unsigned i = 0; i < 3; ++i) {
                                prepare(machines[i], boundary + (i == 2 ? 0x100 : 0), width,
                                        waitcnt, origin, idle);
                                const struct CupidPrefetch* p = &machines[i]->memory.prefetchUnit;
                                CHECK(cupid_timing_global_time(&machines[i]->timing) == anchors[epoch]);
                                CHECK(p->active && p->count == count && p->remaining == remaining);
                                CHECK(p->paused == (filled >= 8));
                            }
                            uint32_t address = boundary + logicalA1;
                            /* A disabled restart replaces the transfer's S/N base,
                             * but still owes the ordinary stream-stop clock. */
                            unsigned expected = nonseq + (remaining == 1 ? 1u : 0u);
                            CHECK(fetch(machines[0], address, width, !refill) == expected);
                            CHECK(fetch(machines[1], address + 0x100, width, !refill) == expected);
                            for (unsigned i = 0; i < 2; ++i) {
                                const struct CupidPrefetch* p = &machines[i]->memory.prefetchUnit;
                                CHECK(!p->active && !p->busy && !p->paused && !p->engaged);
                                CHECK(!p->disabled && p->count == 0 && p->remaining == duty);
                                CHECK(p->head == address + i * 0x100 + width && p->tail == p->head);
                                CHECK(p->fetchKind == CUPID_PREFETCH_NONE);
                                CHECK(machines[i]->timing.masterCycles == (uint32_t)(anchors[epoch] + expected));
                                if (epoch == 1) CHECK(((uint32_t)anchors[epoch] ^ machines[i]->timing.masterCycles) & 0x80000000u);
                                if (epoch >= 2) CHECK(machines[i]->timing.masterCycles < (uint32_t)anchors[epoch]);
                            }
                            /* Disabling must keep existing FIFO hits. A cached
                             * or pending hit retains the restart for a later miss. */
                            unsigned halfwords = width / 2;
                            bool cached = count >= halfwords;
                            bool pending = !cached && remaining && (!refill || idle);
                            unsigned normal = cached ? 1 : pending
                                ? remaining + (halfwords - count - 1) * duty : expected;
                            CHECK(fetch(machines[2], address + 0x100, width, !refill) == normal);
                            CHECK(machines[2]->memory.prefetchUnit.disabled == (cached || pending));
                            completing += remaining == 1;
                            hits += cached || pending;
                            ++scenes;
                        }
                    }
                }
            }
        }
    }
    for (unsigned i = 0; i < 3; ++i) { cupid_gba_destroy(machines[i]); free(machines[i]); }
    CHECK(completing && hits);
    printf("PASS %u disabled boundary/miss/hit scene triples; %u completing phases; %u retained hits\n",
           scenes, completing, hits);
}

struct GuestTrace {
    bool branch, target;
    uint64_t before, after;
};

static void observe_guest(const struct CupidARM* cpu, void* context) {
    struct GuestTrace* trace = context;
    const struct CupidGBA* gba = cpu->master;
    uint32_t address = cupid_arm_instruction_address(cpu);
    if (address == GBA_BASE_ROM0 + 0x1FFF8) {
        CHECK(!trace->branch && cpu->executionMode == MODE_ARM && cpu->prefetch[0] == 0xEA000100);
        CHECK(!gba->memory.prefetch && gba->memory.prefetchUnit.disabled);
        CHECK(gba->memory.prefetchUnit.active && gba->memory.prefetchUnit.remaining == 1);
        trace->branch = true;
        trace->before = cupid_timing_global_time(&gba->timing);
    } else if (address == GBA_BASE_ROM0 + 0x20400) {
        CHECK(trace->branch && !trace->target && cpu->executionMode == MODE_ARM && cpu->prefetch[0] == 0xE3A0305A);
        trace->target = true;
        trace->after = cupid_timing_global_time(&gba->timing);
    }
}

static void guest_disable_then_page_crossing(void) {
    const uint64_t origins[] = {0x1000, (UINT64_C(1) << 31) - 40,
        (UINT64_C(1) << 32) - 40, (UINT64_C(3) << 32) - 40};
    struct CupidGBA* gba = machine();
    const uint32_t program[] = {
        0xE1C100B0, /* STRH r0,[r1]: enable prefetch. */
        0xE5929000, 0xE5929000, 0xE5929000, /* Three EWRAM LDRs fill the FIFO. */
        0xE3A00000, /* MOV r0,#0 */
        0xE1C100B0, /* STRH r0,[r1]: disable through the timed I/O bus. */
        0xEA000100  /* B 0x08020400: first fetch crosses the 128 KiB page. */
    };
    for (unsigned i = 0; i < sizeof(program) / sizeof(*program); ++i)
        STORE_32(program[i], 0x1FFE0 + i * 4, gba->memory.rom);
    const uint32_t target[] = {0xE3A0305A, 0xEAFFFFFE}; /* MOV r3,#0x5A; B . */
    STORE_32(target[0], 0x20400, gba->memory.rom);
    STORE_32(target[1], 0x20404, gba->memory.rom);
    for (unsigned epoch = 0; epoch < 4; ++epoch) {
        reset(gba, origins[epoch]);
        gba->cpu.gprs[0] = 0x4000;
        gba->cpu.gprs[1] = GBA_BASE_IO + REG_WAITCNT;
        gba->cpu.gprs[2] = GBA_BASE_EWRAM;
        gba->cpu.gprs[ARM_PC] = GBA_BASE_ROM0 + 0x1FFE0;
        CupidARMWritePC(&gba->cpu);
        struct GuestTrace trace = {0};
        gba->cpu.instructionObserver = observe_guest;
        gba->cpu.instructionObserverContext = &trace;
        unsigned steps = 0;
        while (!trace.target && steps < 16) { cupid_gba_step(gba); ++steps; }
        CHECK(trace.target && steps == 8 && gba->cpu.gprs[3] == 0x5A);
        /* The boundary demand costs N8 plus its completing-halfword clock.
         * The two branch-target refill fetches retain N8 and S6. */
        CHECK(trace.after - trace.before == 9 + 8 + 6);
        CHECK(trace.after - origins[epoch] == 64);
        CHECK(!gba->memory.prefetchUnit.disabled && !gba->memory.prefetchUnit.active);
        CHECK(!gba->memory.io[REG_WAITCNT >> 1]);
        CHECK(gba->timing.masterCycles == (uint32_t)cupid_timing_global_time(&gba->timing));
        if (epoch == 1) CHECK(((uint32_t)origins[epoch] ^ gba->timing.masterCycles) & 0x80000000u);
        if (epoch >= 2) CHECK(gba->timing.masterCycles < (uint32_t)origins[epoch]);
    }
    cupid_gba_destroy(gba); free(gba);
    puts("PASS guest WAITCNT stores, ROM page crossing and branch refill at four clock epochs");
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    char* end;
    unsigned long group = strtoul(argv[1], &end, 10);
    CHECK(end != argv[1] && *end == 0 && group < 4);
    if (group < 3) disabled_misses_and_retained_hits((unsigned)group);
    else guest_disable_then_page_crossing();
    return 0;
}
