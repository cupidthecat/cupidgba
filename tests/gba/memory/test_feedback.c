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
                    CHECK(g->memory.inputBusValid && g->memory.openBus == expected);
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
    CHECK(!second->memory.inputBusValid);
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
    CHECK(!first->memory.inputBusValid);
    cupid_bus_finish(&first->cpu);
    cupid_gba_reset(first);
    CHECK(!first->memory.inputBusValid && first->memory.openBus == 0 &&
          first->memory.iwramBus == 0);
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
                CHECK(g->memory.inputBusValid && cupid_memory_open_bus(&g->cpu) == expected);
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

static void fetch(unsigned region) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    const uint32_t bases[] = {0x00000100, 0x03000100, 0x07000100, 0x02000100, 0x08000040};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned mode = 0; mode < 2; mode++) {
            for (unsigned alignment = 0; alignment < 2; alignment++)
                for (unsigned timed = 0; timed < 2; timed++)
                    for (unsigned layout = 0; layout < 4; layout++) {
                        struct CupidGBA *g = machine(clocks[epoch]);
                        const unsigned sizes[] = {GBA_SIZE_BIOS, GBA_SIZE_IWRAM, GBA_SIZE_OAM,
                                                  GBA_SIZE_EWRAM, 256};
                        const unsigned size = sizes[region];
                        uint32_t address = bases[region];
                        if (layout == 3) {
                            address = (bases[region] & 0xFF000000) + size -
                                      (region == 0 || region == 4 ? 32 : 4);
                        } else if (region == 0)
                            address += layout * 0x100;
                        else if (region == 4)
                            address += layout * 0x02000000;
                        else
                            address += layout * size;
                        address += mode ? alignment * 2 : alignment * 4;
                        const uint16_t thumb[] = {0x6808, 0x46C0, 0x2211, 0x6655};
                        const uint32_t arm[] = {0xE5910000, 0xE1A00000, 0xE3A05055, 0xE1A00000};
                        uint8_t *memory = region == 0   ? (uint8_t *)g->memory.bios
                                          : region == 1 ? g->memory.iwram
                                          : region == 2 ? (uint8_t *)g->video.oam.raw
                                          : region == 3 ? g->memory.ewram
                                                        : g->memory.rom;
                        const uint8_t *program =
                            mode ? (const uint8_t *)thumb : (const uint8_t *)arm;
                        const unsigned bytes = mode ? sizeof(thumb) : sizeof(arm);
                        for (unsigned byte = 0; byte < bytes; byte++)
                            memory[(address + byte) & (size - 1)] = program[byte];
                        _CupidSetMode(&g->cpu, mode ? MODE_THUMB : MODE_ARM);
                        g->cpu.gprs[ARM_PC] = (int32_t)address;
                        g->cpu.gprs[1] = 0x01000000;
                        g->cpu.memory.timed = timed != 0;
                        if (mode)
                            CupidThumbWritePC(&g->cpu);
                        else
                            CupidARMWritePC(&g->cpu);
                        cupid_gba_step(g);
                        uint32_t expected = arm[2];
                        if (mode) {
                            if (region == 1)
                                expected = alignment ? 0x221146C0 : 0x46C02211;
                            else if (region == 0 || region == 2)
                                expected = alignment ? 0x221146C0 : 0x66552211;
                            else
                                expected = 0x22112211;
                        }
                        CHECK((uint32_t)g->cpu.gprs[0] == expected);
                        destroy(g);
                        scenes++;
                    }
        }
    CHECK(scenes == 128);
    printf("opcode feedback region=%u scenes=%u\n", region, scenes);
}

static void fetch_history(void) {
    const uint16_t values[] = {0, 0xFFFF, 0x8001, 0x1357};
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned alignment = 0; alignment < 2; alignment++) {
            for (unsigned wide = 0; wide < 2; wide++)
                for (unsigned sample = 0; sample < 4; sample++) {
                    struct CupidGBA *g = machine(clocks[epoch]);
                    const uint16_t code[] = {(uint16_t)(wide ? 0x681A : 0x881A), 0x6808, 0x46C0,
                                             0x46C0, 0x46C0};
                    const uint32_t word = alignment ? (uint32_t)values[sample] << 16 | 0x56EF
                                                    : UINT32_C(0x89AB0000) | values[sample];
                    memcpy(g->memory.iwram + alignment * 2, code, sizeof(code));
                    memcpy(g->memory.iwram + 0x100, &word, sizeof(word));
                    _CupidSetMode(&g->cpu, MODE_THUMB);
                    g->cpu.gprs[ARM_PC] = 0x03000000 + alignment * 2;
                    g->cpu.gprs[1] = 0x01000000;
                    g->cpu.gprs[3] = 0x03000100 + alignment * 2;
                    g->cpu.memory.timed = true;
                    CupidThumbWritePC(&g->cpu);
                    cupid_gba_step(g);
                    uint32_t loaded =
                        wide ? (alignment ? word >> 16 | word << 16 : word) : values[sample];
                    CHECK((uint32_t)g->cpu.gprs[2] == loaded);
                    cupid_gba_step(g);
                    const uint32_t expected = alignment ? (uint32_t)values[sample] << 16 | 0x46C0
                                                        : UINT32_C(0x46C00000) | values[sample];
                    CHECK((uint32_t)g->cpu.gprs[0] == expected);
                    destroy(g);
                    scenes++;
                }
        }
    CHECK(scenes == 64);
    printf("retained data across opcode fetch scenes=%u\n", scenes);
}

static void fetch_dma(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned alignment = 0; alignment < 2; alignment++) {
            for (unsigned channel = 0; channel < 4; channel++) {
                struct CupidGBA *g = machine(clocks[epoch]);
                const uint16_t code[] = {0x6808, 0x46C0, 0x2211, 0x46C0};
                const uint32_t payload = UINT32_C(0x8BADFACE) ^ (channel * UINT32_C(0x11221122));
                memcpy(g->memory.iwram + alignment * 2, code, sizeof(code));
                seed(g, 0x02000200, payload);
                _CupidSetMode(&g->cpu, MODE_THUMB);
                g->cpu.gprs[ARM_PC] = 0x03000000 + alignment * 2;
                g->cpu.gprs[1] = 0x01000000;
                g->cpu.memory.timed = true;
                CupidThumbWritePC(&g->cpu);
                start(g, channel, 0x02000200, 0x03000200);
                cupid_bus_advance(&g->cpu, 2, false);
                CHECK(g->memory.dmaRunning);
                cupid_gba_step(g);
                uint32_t expected = alignment ? (payload & 0xFFFF) | UINT32_C(0x22110000)
                                              : (payload & UINT32_C(0xFFFF0000)) | 0x2211;
                CHECK((uint32_t)g->cpu.gprs[0] == expected);
                CHECK(cupid_memory_load32(&g->cpu, 0x03000200, NULL) == payload);
                CHECK(g->memory.dma[channel].nextSource == 0x02000204);
                CHECK(g->memory.dma[channel].nextDest == 0x03000204);
                CHECK(!g->memory.dmaRunning && !g->cpuBlocked);
                destroy(g);
                scenes++;
            }
        }
    CHECK(scenes == 32);
    printf("retained DMA data across opcode fetch scenes=%u\n", scenes);
}

static struct {
    struct CupidARM *owner;
    uint32_t address[3], value[3];
    unsigned width[3], count;
} opcodeLog;

static void record_opcode(struct CupidARM *cpu, uint32_t address, unsigned width, uint32_t value) {
    CHECK(cpu == opcodeLog.owner && opcodeLog.count < 3);
    unsigned index = opcodeLog.count++;
    opcodeLog.address[index] = address;
    opcodeLog.width[index] = width;
    opcodeLog.value[index] = value;
    cupid_memory_capture_opcode(cpu, address, width, value);
}

static void fetch_irq(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    const uint32_t bases[] = {0x00000100, 0x03000100, 0x07000100, 0x02000100, 0x08000040};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; epoch++)
        for (unsigned region = 0; region < 5; region++) {
            for (unsigned mode = 0; mode < 2; mode++)
                for (unsigned alignment = 0; alignment < 2; alignment++) {
                    struct CupidGBA *g = machine(clocks[epoch]);
                    const uint32_t address = bases[region] + (mode ? alignment * 2 : alignment * 4);
                    const uint16_t thumb[] = {0x6808, 0x46C0, 0x2211, 0x6655};
                    const uint32_t arm[] = {0xE5910000, 0xE1A00000, 0xE3A05055, 0xE1A00000};
                    const uint32_t vector[] = {0xE3A03063, 0xE1A00000};
                    uint8_t *memory = region == 0   ? (uint8_t *)g->memory.bios
                                      : region == 1 ? g->memory.iwram
                                      : region == 2 ? (uint8_t *)g->video.oam.raw
                                      : region == 3 ? g->memory.ewram
                                                    : g->memory.rom;
                    memcpy(memory + (address & 1023),
                           mode ? (const void *)thumb : (const void *)arm,
                           mode ? sizeof(thumb) : sizeof(arm));
                    memcpy((uint8_t *)g->memory.bios + BASE_IRQ, vector, sizeof(vector));
                    _CupidSetMode(&g->cpu, mode ? MODE_THUMB : MODE_ARM);
                    g->cpu.gprs[ARM_PC] = (int32_t)address;
                    g->cpu.memory.timed = true;
                    if (mode)
                        CupidThumbWritePC(&g->cpu);
                    else
                        CupidARMWritePC(&g->cpu);
                    const uint32_t status = g->cpu.cpsr.packed;
                    const unsigned width = mode ? 2 : 4;
                    const uint32_t discarded = (uint32_t)g->cpu.gprs[ARM_PC] + width;
                    const uint64_t before = cupid_timing_global_time(&g->timing);
                    const unsigned duration = 3 + (mode ? g->cpu.memory.activeSeqCycles16
                                                        : g->cpu.memory.activeSeqCycles32);
                    memset(&opcodeLog, 0, sizeof(opcodeLog));
                    opcodeLog.owner = &g->cpu;
                    g->cpu.memory.captureOpcode = record_opcode;
                    cupid_arm_take_irq(&g->cpu);
                    CHECK(opcodeLog.count == 3);
                    CHECK(opcodeLog.address[0] == discarded && opcodeLog.width[0] == width);
                    CHECK(opcodeLog.value[0] == (mode ? thumb[2] : arm[2]));
                    for (unsigned slot = 0; slot < 2; slot++) {
                        CHECK(opcodeLog.address[slot + 1] == BASE_IRQ + slot * 4);
                        CHECK(opcodeLog.width[slot + 1] == 4 &&
                              opcodeLog.value[slot + 1] == vector[slot]);
                        CHECK(g->cpu.prefetch[slot] == vector[slot]);
                    }
                    CHECK(g->cpu.privilegeMode == MODE_IRQ && g->cpu.executionMode == MODE_ARM);
                    CHECK((uint32_t)g->cpu.spsr.packed == status &&
                          (uint32_t)g->cpu.gprs[ARM_LR] == address + 4);
                    CHECK(cupid_timing_global_time(&g->timing) == before + duration);
                    destroy(g);
                    scenes++;
                }
        }
    CHECK(scenes == 80);
    printf("discarded IRQ opcode samples=%u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "handover"))
        RUN(handover);
    else if (!strcmp(argv[1], "ownership"))
        RUN(ownership);
    else if (!strcmp(argv[1], "bios"))
        RUN(bios);
    else if (!strncmp(argv[1], "fetch-", 6) && argv[1][6] >= '0' && argv[1][6] <= '4' &&
             !argv[1][7])
        fetch((unsigned)(argv[1][6] - '0'));
    else if (!strcmp(argv[1], "fetch-history"))
        RUN(fetch_history);
    else if (!strcmp(argv[1], "fetch-dma"))
        RUN(fetch_dma);
    else if (!strcmp(argv[1], "fetch-irq"))
        RUN(fetch_irq);
    else
        CHECK(false);
    return 0;
}
