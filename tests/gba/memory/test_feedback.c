/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

static struct CupidGBA *machine(uint64_t clock) {
    struct CupidGBA *g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    cupid_gba_reset(g);
    cupid_timing_clear(&g->timing);
    g->timing.globalCycles = clock;
    g->timing.masterCycles = (uint32_t)clock;
    g->cpu.cpsr.i = 1;
    g->cpu.irqDisableLatch = true;
    g->video.dispcnt = 0x80;
    g->cpu.prefetch[0] = 0x11112222;
    g->cpu.prefetch[1] = 0x33334444;
    g->memory.rom = calloc(1, 256);
    CHECK(g->memory.rom);
    g->memory.romSize = g->memory.romImageSize = 256;
    g->memory.romMask = 255;
    return g;
}

static void destroy(struct CupidGBA *g) {
    cupid_gba_destroy(g);
    free(g);
}

static void start(struct CupidGBA *g, unsigned ch, uint32_t source, uint32_t dest) {
    cupid_dma_write_sad(g, ch, source);
    cupid_dma_write_dad(g, ch, dest);
    cupid_dma_write_cnt_lo(g, ch, 1);
    cupid_dma_write_cnt(g, ch, 0x8400);
}

static void seed(struct CupidGBA *g, uint32_t address, uint32_t word) {
    if (address >> 24 >= 8) {
        memcpy(g->memory.rom + (address & 255), &word, sizeof(word));
    } else {
        cupid_memory_store32(&g->cpu, address, (int32_t)word, NULL);
    }
}

/* The CPU owns its read until the ready edge. DMA activation during that
 * interval must wait, then read the captured input through an unmapped port. */
static void handover(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    const uint32_t addresses[] = {0x02000020, 0x03000020, 0x05000020, 0x06000020,
                                  0x07000020, 0x08000020, 0x0A000020, 0x0C000020};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned region = 0; region < 8; region++) {
            for (unsigned lane = 0; lane < 4; lane++)
                for (unsigned channel = 0; channel < 4; channel++) {
                    struct CupidGBA *g = machine(clocks[epoch]);
                    const uint32_t word = UINT32_C(0x8B72E195);
                    const uint32_t retained = UINT32_C(0xA7C45B36);
                    const uint32_t address = addresses[region] + lane;
                    seed(g, addresses[region], word);
                    /* Prime IWRAM through a real DMA write, not an observer access. */
                    seed(g, 0x02000200, retained);
                    start(g, 3, 0x02000200, 0x03000200);
                    cupid_timing_tick(&g->timing, 11);
                    CHECK(!g->memory.dmaRunning);
                    start(g, channel, 0x04001000, 0x02000100);
                    cupid_timing_tick(&g->timing, 1);
                    CHECK(!g->memory.dmaRunning);
                    g->cpu.memory.timed = true;
                    int cycles = 0;
                    uint32_t half = (word >> ((lane & 2) * 8)) & 0xFFFF;
                    uint32_t expected = half | half << 16;
                    if (region == 1) {
                        unsigned shift = (lane & 2) * 8;
                        expected = (retained & ~(UINT32_C(0xFFFF) << shift)) | half << shift;
                    } else if (region == 4)
                        expected = word;
                    uint32_t result = cupid_memory_load16(&g->cpu, address, &cycles);
                    CHECK(result == (lane & 1 ? (half >> 8) | half << 24 : half));
                    CHECK(cycles == 1);
                    CHECK(g->memory.dataBusValid && g->memory.openBus == expected);
                    CHECK(g->memory.dmaRunning);
                    cupid_bus_wait(&g->cpu);
                    CHECK(!g->memory.dmaRunning && !g->cpuBlocked);
                    CHECK(g->memory.dma[channel].latch == expected);
                    CHECK(cupid_memory_load32(&g->cpu, 0x02000100, NULL) == expected);
                    CHECK(g->memory.lastWasDMA && cupid_memory_open_bus(&g->cpu) == expected);
                    destroy(g);
                    scenes++;
                }
        }
    CHECK(scenes == 512);
    printf("halfword CPU/DMA handovers=%u\n", scenes);
}

static void ownership(void) {
    struct CupidGBA *first = machine(0);
    struct CupidGBA *second = machine(0);
    seed(first, 0x07000020, 0x89ABCDEF);
    first->cpu.memory.timed = true;
    int cycles = 0;
    CHECK(cupid_memory_load16(&first->cpu, 0x07000020, &cycles) == 0xCDEF);
    CHECK(cupid_memory_open_bus(&first->cpu) == UINT32_C(0x89ABCDEF));
    CHECK(!second->memory.dataBusValid);
    /* Untimed observers neither replace CPU input nor touch IWRAM banks. */
    uint32_t banks = first->memory.iwramBus;
    cupid_memory_load16(&first->cpu, 0x03000000, NULL);
    cupid_memory_load32(&first->cpu, 0x03000000, NULL);
    cupid_memory_store32(&first->cpu, 0x03000000, 0x12345678, NULL);
    CHECK(first->memory.iwramBus == banks);
    CHECK(cupid_memory_open_bus(&first->cpu) == UINT32_C(0x89ABCDEF));
    cycles = 0;
    cupid_memory_load16(&first->cpu, 0x01000000, &cycles);
    CHECK(cupid_memory_open_bus(&first->cpu) == UINT32_C(0x89ABCDEF));
    cupid_memory_set_active_region(&first->cpu, 0x02000000);
    cupid_bus_fetch(&first->cpu, 0x02000000, 4, false);
    CHECK(!first->memory.dataBusValid);
    cupid_bus_finish(&first->cpu);
    cupid_gba_reset(first);
    CHECK(!first->memory.dataBusValid && first->memory.openBus == 0 && first->memory.iwramBus == 0);
    destroy(first);
    destroy(second);
}

static void bios(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned active = 0; active < 2; active++) {
            for (unsigned lane = 0; lane < 4; lane++) {
                struct CupidGBA *g = machine(clocks[epoch]);
                const uint32_t raw = UINT32_C(0x8172E3A4);
                const uint32_t protected = UINT32_C(0x59BC26D8);
                memcpy((uint8_t *)g->memory.bios + 0x100, &raw, sizeof(raw));
                g->memory.activeRegion = active ? GBA_REGION_BIOS : GBA_REGION_ROM0;
                g->memory.biosPrefetch = protected;
                g->cpu.memory.timed = true;
                int cycles = 0;
                uint32_t expected = active ? raw : protected;
                uint32_t half = (expected >> ((lane & 2) * 8)) & 0xFFFF;
                uint32_t value = cupid_memory_load16(&g->cpu, 0x100 + lane, &cycles);
                CHECK(value == (lane & 1 ? (half >> 8) | half << 24 : half));
                CHECK(g->memory.dataBusValid && cupid_memory_open_bus(&g->cpu) == expected);
                cycles = 0;
                cupid_memory_load16(&g->cpu, 0x4000 + lane, &cycles);
                CHECK(cupid_memory_open_bus(&g->cpu) == expected);
                CHECK(g->memory.biosPrefetch == protected);
                destroy(g);
                scenes++;
            }
        }
    CHECK(scenes == 32);
    printf("BIOS halfword protection scenes=%u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "handover"))
        RUN(handover);
    else if (!strcmp(argv[1], "ownership"))
        RUN(ownership);
    else if (!strcmp(argv[1], "bios"))
        RUN(bios);
    else
        CHECK(false);
    return 0;
}
