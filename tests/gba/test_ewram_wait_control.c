/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    cupid_gba_reset(g);
    cupid_timing_clear(&g->timing);
    g->timing.globalCycles = clock;
    g->timing.masterCycles = (uint32_t)clock;
    g->cpu.cpsr.i = 1;
    g->cpu.irqDisableLatch = true;
    return g;
}

static void destroy(struct CupidGBA* g) { cupid_gba_destroy(g); free(g); }

static void write_control(struct CupidGBA* g, uint32_t address, unsigned width, unsigned field) {
    if (width == 1) cupid_memory_store8(&g->cpu, address + 3, (int8_t)field, NULL);
    else if (width == 2) cupid_memory_store16(&g->cpu, address + 2, (int16_t)(field << 8), NULL);
    else cupid_memory_store32(&g->cpu, address, (int32_t)((field << 24) | 0x20), NULL);
}

static void tables(struct CupidGBA* g, unsigned field) {
    unsigned clocks = 16 - field;
    CHECK(g->memory.waitstatesNonseq16[GBA_REGION_EWRAM] + 1u == clocks);
    CHECK(g->memory.waitstatesSeq16[GBA_REGION_EWRAM] + 1u == clocks);
    CHECK(g->memory.waitstatesNonseq32[GBA_REGION_EWRAM] + 1u == 2 * clocks);
    CHECK(g->memory.waitstatesSeq32[GBA_REGION_EWRAM] + 1u == 2 * clocks);
}

static void registers(void) {
    struct CupidGBA* g = machine(0);
    unsigned scenes = 0;
    for (unsigned bank = 0; bank < 256; bank++) {
        uint32_t base = GBA_BASE_IO + (bank << 16) + 0x800;
        CHECK(cupid_memory_load16(&g->cpu, base + 2, NULL) == 0x0D00);
        CHECK(cupid_memory_load8(&g->cpu, base + 2, NULL) == 0);
        CHECK(cupid_memory_load8(&g->cpu, base + 3, NULL) == 13);
    }
    for (unsigned bank = 0; bank < 256; bank++) for (unsigned field = 0; field < 15; field++) {
        for (unsigned width = 1; width <= 4; width *= 2) {
            uint32_t base = GBA_BASE_IO + (bank << 16) + 0x800;
            write_control(g, base, width, field);
            tables(g, field);
            CHECK(cupid_memory_load16(&g->cpu, GBA_BASE_IO + 0x802, NULL) == field << 8);
            CHECK(cupid_memory_load16(&g->cpu, base + 2, NULL) == field << 8);
            /* The unused low byte of this high halfword cannot change waits. */
            cupid_memory_store8(&g->cpu, base + 2, -1, NULL);
            tables(g, field);
            CHECK(cupid_memory_load16(&g->cpu, base + 2, NULL) == field << 8);
            scenes++;
        }
    }
    CHECK(scenes == 11520);
    printf("wait-control register scenes=%u\n", scenes);
    destroy(g);
}

static void guest(void) {
    const uint32_t arm[] = {0xE5C10000, 0xE1C100B0, 0xE5810000};
    const uint16_t thumb[] = {0x7008, 0x8008, 0x6008};
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned mode = 0; mode < 2; mode++) {
        for (unsigned field = 0; field < 15; field++) for (unsigned size = 0; size < 3; size++) {
            struct CupidGBA* g = machine(clocks[epoch]);
            unsigned width = 1u << size;
            uint32_t opcode = mode ? thumb[size] : arm[size];
            memcpy(g->memory.iwram, &opcode, mode ? 2 : 4);
            g->cpu.gprs[0] = (int32_t)(width == 4 ? (field << 24) | 0x20 : width == 2 ? field << 8 : field);
            g->cpu.gprs[1] = (int32_t)(0x04FF0800 + (width == 4 ? 0 : width == 2 ? 2 : 3));
            _CupidSetMode(&g->cpu, mode ? MODE_THUMB : MODE_ARM);
            g->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
            g->cpu.memory.timed = true;
            if (mode) CupidThumbWritePC(&g->cpu); else CupidARMWritePC(&g->cpu);
            cupid_gba_step(g);
            tables(g, field);
            CHECK(cupid_memory_load16(&g->cpu, GBA_BASE_IO + 0x802, NULL) == field << 8);
            destroy(g);
            scenes++;
        }
    }
    CHECK(scenes == 360);
    printf("guest ARM/Thumb store scenes=%u\n", scenes);
}

static void fetch(void) {
    struct CupidGBA* g = machine(0);
    cupid_memory_set_active_region(&g->cpu, GBA_BASE_EWRAM);
    for (unsigned field = 0; field < 15; field++) {
        write_control(g, GBA_BASE_IO + 0x800, 2, field);
        CHECK(g->cpu.memory.activeNonseqCycles16 + 1u == 16 - field);
        CHECK(g->cpu.memory.activeSeqCycles32 + 1u == 2 * (16 - field));
        for (unsigned width = 2; width <= 4; width += 2) for (unsigned sequential = 0; sequential < 2; sequential++) {
            uint64_t before = cupid_timing_global_time(&g->timing);
            cupid_bus_fetch(&g->cpu, GBA_BASE_EWRAM + 0x100, width, sequential != 0);
            cupid_bus_finish(&g->cpu);
            CHECK(cupid_timing_global_time(&g->timing) - before == (width / 2) * (16 - field));
        }
    }
    destroy(g);
}

static void data(void) {
    struct CupidGBA* g = machine(0);
    const uint32_t word = 0x12345678;
    memcpy(g->memory.ewram + 0x100, &word, 4);
    for (unsigned field = 0; field < 15; field++) {
        write_control(g, GBA_BASE_IO + 0x800, 1, field);
        g->cpu.memory.timed = true;
        for (unsigned width = 1; width <= 4; width *= 2) {
            uint64_t before = cupid_timing_global_time(&g->timing);
            int cycles = 0;
            uint32_t value;
            if (width == 1) value = cupid_memory_load8(&g->cpu, GBA_BASE_EWRAM + 0x100, &cycles);
            else if (width == 2) value = cupid_memory_load16(&g->cpu, GBA_BASE_EWRAM + 0x100, &cycles);
            else value = cupid_memory_load32(&g->cpu, GBA_BASE_EWRAM + 0x100, &cycles);
            CHECK(value == (width == 4 ? word : width == 2 ? word & 65535 : word & 255));
            CHECK(cupid_timing_global_time(&g->timing) - before == (width == 4 ? 2 : 1) * (16 - field));
        }
        g->cpu.memory.timed = false;
    }
    destroy(g);
}

static void dma(void) {
    const uint32_t words[] = {0x12345678, 0x9ABCDEF0, 0x13579BDF, 0x2468ACE0};
    for (unsigned field = 0; field < 15; field++) {
        struct CupidGBA* g = machine(0);
        write_control(g, GBA_BASE_IO + 0x800, 4, field);
        memcpy(g->memory.ewram + 0x100, words, sizeof(words));
        cupid_dma_write_sad(g, 3, GBA_BASE_EWRAM + 0x100);
        cupid_dma_write_dad(g, 3, GBA_BASE_IWRAM + 0x100);
        cupid_dma_write_cnt_lo(g, 3, 4);
        cupid_dma_write_cnt(g, 3, 0x8400);
        unsigned duration = 8 * (16 - field) + 8;
        cupid_timing_tick(&g->timing, (int32_t)duration);
        CHECK(!g->memory.dmaRunning && !g->memory.dma[3].enable);
        CHECK(g->memory.dma[3].busyUntil == duration - 1);
        CHECK(!memcmp(g->memory.iwram + 0x100, words, sizeof(words)));
        destroy(g);
    }
}

static void owners(void) {
    struct CupidGBA* a = machine(0);
    struct CupidGBA* b = machine(0);
    write_control(a, GBA_BASE_IO + 0x800, 1, 14);
    tables(a, 14); tables(b, 13);
    cupid_gba_reset(a);
    tables(a, 13);
    CHECK(cupid_memory_load16(&a->cpu, GBA_BASE_IO + 0x802, NULL) == 0x0D00);
    cupid_memory_update_ewram_waitstates(b, 0x0B00);
    tables(b, 11);
    CHECK(cupid_memory_load16(&b->cpu, GBA_BASE_IO + 0x802, NULL) == 0x0B00);
    destroy(a); destroy(b);
}


static void readback(void) {
    struct CupidGBA* g = machine(0);
    for (unsigned bank = 0; bank < 256; bank++) {
        uint32_t base = GBA_BASE_IO + (bank << 16) + 0x800;
        CHECK(cupid_memory_load32(&g->cpu, base, NULL) == 0x0D000020);
        const unsigned bytes[] = {0x20, 0, 0, 13};
        for (unsigned lane = 0; lane < 4; lane++)
            CHECK(cupid_memory_load8(&g->cpu, base + lane, NULL) == bytes[lane]);
    }
    unsigned scenes = 0;
    for (unsigned bank = 0; bank < 256; bank++) for (unsigned upper = 0; upper < 16; upper++) {
        for (unsigned field = 0; field < 16; field++) for (unsigned width = 1; width <= 4; width *= 2) {
            uint32_t base = GBA_BASE_IO + (bank << 16) + 0x800;
            unsigned high = (upper << 12) | (field << 8);
            if (width == 1) cupid_memory_store8(&g->cpu, base + 3, (int8_t)(high >> 8), NULL);
            else if (width == 2) cupid_memory_store16(&g->cpu, base + 2, (int16_t)(high | 255), NULL);
            else cupid_memory_store32(&g->cpu, base, (int32_t)((high << 16) | 0x20), NULL);
            CHECK(cupid_memory_load16(&g->cpu, base, NULL) == 0x20);
            CHECK(cupid_memory_load16(&g->cpu, base + 2, NULL) == high);
            CHECK(cupid_memory_load8(&g->cpu, base + 3, NULL) == high >> 8);
            CHECK(cupid_memory_load8(&g->cpu, base + 2, NULL) == 0);
            uint32_t word = (high << 16) | 0x20;
            for (unsigned lane = 0; lane < 4; lane++) {
                unsigned rotate = lane * 8;
                uint32_t expected = rotate ? (word >> rotate) | (word << (32 - rotate)) : word;
                CHECK(cupid_memory_load32(&g->cpu, base + lane, NULL) == expected);
            }
            /* Value 15 checks the existing software policy only. This does
             * not qualify the physical RAM corruption/lockup behavior. */
            tables(g, field == 15 ? 14 : field);
            scenes++;
        }
    }
    CHECK(scenes == 196608);
    printf("full control-lane readback scenes=%u\n", scenes);
    destroy(g);
}

static void decode(void) {
    struct CupidGBA* g = machine(0);
    g->cpu.prefetch[0] = g->cpu.prefetch[1] = 0xA55AA55A;
    write_control(g, GBA_BASE_IO + 0x800, 2, 14);
    const unsigned offsets[] = {0x7F8, 0x7FC, 0x804, 0x808, 0x1800, 0x1804};
    for (unsigned bank = 0; bank < 256; bank++) for (unsigned i = 0; i < sizeof(offsets) / sizeof(*offsets); i++) {
        uint32_t base = GBA_BASE_IO + (bank << 16) + offsets[i];
        cupid_memory_store32(&g->cpu, base, 0, NULL);
        cupid_memory_store16(&g->cpu, base + 2, 0, NULL);
        cupid_memory_store8(&g->cpu, base + 3, 0, NULL);
        CHECK(cupid_memory_load32(&g->cpu, base, NULL) == 0xA55AA55A);
        CHECK(cupid_memory_load16(&g->cpu, GBA_BASE_IO + 0x802, NULL) == 0x0E00);
        tables(g, 14);
    }
    /* The field has no alias in the ordinary 1 KiB register array or beyond
     * the 24-bit I/O offset accepted by these internal dispatch functions. */
    CHECK(cupid_memory_load32(&g->cpu, GBA_BASE_IO + 0x210, NULL) == 0xA55AA55A);
    cupid_io_write(g, 0x01000802, 0);
    cupid_io_write8(g, 0x01000803, 0);
    cupid_io_write32(g, 0x01000800, 0);
    CHECK(cupid_io_read(g, 0x01000802) == 0xA55A);
    tables(g, 14);
    destroy(g);
}

static void pipeline(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned mode = 0; mode < 2; mode++) {
        for (unsigned field = 0; field < 15; field++) {
            struct CupidGBA* g = machine(clocks[epoch]);
            const uint32_t arm[] = {0xE5810000, 0xE3A0205A, 0xE3A03033, 0xE3A04055};
            const uint16_t thumb[] = {0x6008, 0x225A, 0x2333, 0x2455};
            memcpy(g->memory.ewram, mode ? (const void*)thumb : (const void*)arm, mode ? sizeof(thumb) : sizeof(arm));
            g->cpu.gprs[0] = (int32_t)((field << 24) | 0x20);
            g->cpu.gprs[1] = 0x04FF0800;
            _CupidSetMode(&g->cpu, mode ? MODE_THUMB : MODE_ARM);
            g->cpu.gprs[ARM_PC] = GBA_BASE_EWRAM;
            g->cpu.memory.timed = true;
            if (mode) CupidThumbWritePC(&g->cpu); else CupidARMWritePC(&g->cpu);
            cupid_gba_step(g);
            tables(g, field);
            CHECK(g->cpu.prefetch[0] == (mode ? thumb[1] : arm[1]));
            uint64_t before = cupid_timing_global_time(&g->timing);
            cupid_gba_step(g);
            CHECK(g->cpu.gprs[2] == 0x5A);
            CHECK(cupid_timing_global_time(&g->timing) - before == (mode ? 1 : 2) * (16 - field));
            destroy(g);
            scenes++;
        }
    }
    CHECK(scenes == 120);
    printf("live EWRAM pipeline scenes=%u\n", scenes);
}

static void writes(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    const uint32_t initial = 0x12345678, value = 0xA1B2C3D4;
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned field = 0; field < 15; field++) {
        struct CupidGBA* g = machine(clocks[epoch]);
        write_control(g, GBA_BASE_IO + 0x800, 4, field);
        g->cpu.memory.timed = true;
        for (unsigned width = 1; width <= 4; width *= 2) {
            memcpy(g->memory.ewram + 0x100, &initial, 4);
            int cycles = 0;
            uint64_t before = cupid_timing_global_time(&g->timing);
            uint32_t expected;
            if (width == 1) {
                cupid_memory_store8(&g->cpu, GBA_BASE_EWRAM + 0x101, (int8_t)value, &cycles);
                expected = (initial & 0xFFFF00FF) | ((value & 255) << 8);
            } else if (width == 2) {
                cupid_memory_store16(&g->cpu, GBA_BASE_EWRAM + 0x102, (int16_t)value, &cycles);
                expected = (initial & 65535) | (value << 16);
            } else {
                cupid_memory_store32(&g->cpu, GBA_BASE_EWRAM + 0x100, (int32_t)value, &cycles);
                expected = value;
            }
            CHECK(cupid_timing_global_time(&g->timing) - before == (width == 4 ? 2 : 1) * (16 - field));
            CHECK(cupid_memory_load32(&g->cpu, GBA_BASE_EWRAM + 0x100, NULL) == expected);
            scenes++;
        }
        g->cpu.memory.timed = false;
        const uint32_t words[] = {0x12345678, 0x9ABCDEF0, 0x13579BDF, 0x2468ACE0};
        memcpy(g->memory.iwram + 0x100, words, sizeof(words));
        cupid_gba_process_events(g);
        uint64_t before = cupid_timing_global_time(&g->timing);
        cupid_dma_write_sad(g, 3, GBA_BASE_IWRAM + 0x100);
        cupid_dma_write_dad(g, 3, GBA_BASE_EWRAM + 0x100);
        cupid_dma_write_cnt_lo(g, 3, 4);
        cupid_dma_write_cnt(g, 3, 0x8400);
        unsigned duration = 8 * (16 - field) + 8;
        cupid_timing_tick(&g->timing, (int32_t)duration);
        CHECK(!g->memory.dmaRunning && !g->memory.dma[3].enable);
        CHECK(g->memory.dma[3].busyUntil == before + duration - 1);
        CHECK(!memcmp(g->memory.ewram + 0x100, words, sizeof(words)));
        destroy(g);
    }
    CHECK(scenes == 180);
    printf("timed CPU writes=%u DMA destination scenes=60\n", scenes);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "registers")) RUN(registers);
    else if (!strcmp(argv[1], "guest")) RUN(guest);
    else if (!strcmp(argv[1], "fetch")) RUN(fetch);
    else if (!strcmp(argv[1], "data")) RUN(data);
    else if (!strcmp(argv[1], "dma")) RUN(dma);
    else if (!strcmp(argv[1], "owners")) RUN(owners);
    else if (!strcmp(argv[1], "readback")) RUN(readback);
    else if (!strcmp(argv[1], "decode")) RUN(decode);
    else if (!strcmp(argv[1], "pipeline")) RUN(pipeline);
    else if (!strcmp(argv[1], "writes")) RUN(writes);
    else CHECK(false);
    return 0;
}
