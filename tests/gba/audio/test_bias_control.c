/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_bias_control.c
 * @brief SOUNDBIAS writable fields, bus input and scheduled digital output.
 *
 * - Exhaust all register stimuli through direct, byte, halfword and word writes.
 * - Exercise ARM/Thumb stores, DMA publication and CPU-to-DMA input retention.
 * - Compare ignored-bit stimuli with canonical bias/divider fields in PCM.
 * - Retain capture deadlines across byte lanes, power states and clock wrap.
 */
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

static const uint64_t epochs[] = {0, INT32_MAX - 4u, UINT32_MAX - 4u, UINT64_C(0x300000000) - 4};
static const uint16_t patterns[] = {0xFFFF, 0x0001, 0x3C00, 0xC000, 0x00FF, 0xFF00,
                                    0x0201, 0x0101, 0xC3FF, 0x8001, 0x4001, 0x3C01};

struct Capture {
    unsigned count;
    uint64_t clock[64];
    int16_t left[64], right[64];
};

static void capture(void *context, uint64_t clock, int16_t left, int16_t right) {
    struct Capture *c = context;
    CHECK(c->count < 64);
    c->clock[c->count] = clock;
    c->left[c->count] = left;
    c->right[c->count++] = right;
}

/* Decode the two documented fields rather than retaining unused input bits. */
static uint16_t fields(uint16_t raw) {
    return (uint16_t)((((raw >> 1) & 511) << 1) | (((raw >> 14) & 3) << 14));
}

static struct CupidGBA *machine(uint64_t origin, bool power) {
    struct CupidGBA *g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    cupid_gba_reset(g);
    cupid_timing_clear(&g->timing);
    g->timing.globalCycles = origin;
    g->timing.masterCycles = (uint32_t)origin;
    g->audio.lastSample = g->audio.sampleOrigin = origin;
    g->audio.nextSample = origin + 512;
    cupid_timing_schedule(&g->timing, &g->audio.sampleEvent, 512);
    g->cpu.cpsr.i = 1;
    g->cpu.irqDisableLatch = true;
    cupid_io_write(g, REG_SOUNDCNT_X, power ? 0x80 : 0);
    return g;
}

static void destroy(struct CupidGBA *g) {
    cupid_gba_destroy(g);
    free(g);
}

static void write(struct CupidGBA *g, unsigned method, uint16_t raw, unsigned gap) {
    uint32_t address = GBA_BASE_IO + REG_SOUNDBIAS;
    if (method == 0)
        cupid_io_write(g, REG_SOUNDBIAS, raw);
    else if (method == 1 || method == 2) {
        unsigned first = method == 2;
        cupid_memory_store8(&g->cpu, address + first, (int8_t)(raw >> (8 * first)), NULL);
        if (gap)
            cupid_timing_tick(&g->timing, (int)gap);
        cupid_memory_store8(&g->cpu, address + (first ^ 1), (int8_t)(raw >> (8 * (first ^ 1))),
                            NULL);
    } else if (method == 3)
        cupid_memory_store16(&g->cpu, address, (int16_t)raw, NULL);
    else {
        CHECK(method == 4);
        cupid_memory_store32(&g->cpu, address, (int32_t)(UINT32_C(0xA7D30000) | raw), NULL);
    }
}

static void check_register(struct CupidGBA *g, uint16_t expected) {
    uint32_t address = GBA_BASE_IO + REG_SOUNDBIAS;
    CHECK(cupid_io_read(g, REG_SOUNDBIAS) == expected);
    CHECK(g->audio.soundbias == expected && g->memory.io[REG_SOUNDBIAS >> 1] == expected);
    CHECK(cupid_memory_load8(&g->cpu, address, NULL) == (expected & 255));
    CHECK(cupid_memory_load8(&g->cpu, address + 1, NULL) == (expected >> 8));
    CHECK((uint32_t)cupid_memory_load32(&g->cpu, address, NULL) == expected);
    CHECK(g->audio.sampleInterval == (512 >> (expected >> 14)));
}

static void registers(void) {
    unsigned scenes = 0;
    for (unsigned power = 0; power < 2; ++power) {
        struct CupidGBA *g = machine(0, power != 0);
        for (unsigned method = 0; method < 5; ++method)
            for (uint32_t raw = 0; raw <= UINT16_MAX; ++raw) {
                write(g, method, (uint16_t)raw, 0);
                check_register(g, fields((uint16_t)raw));
                CHECK(cupid_timing_global_time(&g->timing) == 0);
                CHECK(g->audio.nextSample == 512);
                ++scenes;
            }
        destroy(g);
    }
    CHECK(scenes == 655360);
    printf("register access scenes=%u\n", scenes);
}

static void program(struct CupidGBA *g, uint32_t address, bool thumb, unsigned width) {
    const uint32_t arm[] = {0xE5C10000, 0xE1C100B0, 0xE5810000};
    const uint16_t narrow[] = {0x7008, 0x8008, 0x6008};
    unsigned index = width == 1 ? 0 : width == 2 ? 1 : 2;
    uint8_t *target;
    if (address < GBA_SIZE_BIOS)
        target = (uint8_t *)g->memory.bios + address;
    else if (address >> 24 == GBA_REGION_IWRAM)
        target = g->memory.iwram + 0x100;
    else {
        g->memory.rom = calloc(1, 0x400);
        CHECK(g->memory.rom);
        g->memory.romSize = 0x400;
        g->memory.romMask = 0x3FF;
        target = g->memory.rom + 0x100;
    }
    if (thumb) {
        const uint16_t code[] = {narrow[index], 0x681A, 0x46C0, 0x46C0};
        memcpy(target, code, sizeof(code));
        _CupidSetMode(&g->cpu, MODE_THUMB);
        g->cpu.gprs[ARM_PC] = address;
        CupidThumbWritePC(&g->cpu);
    } else {
        const uint32_t code[] = {arm[index], 0xE5932000, 0xE1A00000, 0xE1A00000};
        memcpy(target, code, sizeof(code));
        g->cpu.gprs[ARM_PC] = address;
        CupidARMWritePC(&g->cpu);
    }
}

static void guest(void) {
    const uint32_t origins[] = {0x100, 0x03000100, 0x08000100};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned origin = 0; origin < 3; ++origin)
            for (unsigned thumb = 0; thumb < 2; ++thumb)
                for (unsigned width = 1; width <= 4; width *= 2)
                    for (unsigned lane = 0; lane < (width == 1 ? 2 : width); ++lane)
                        for (unsigned p = 0; p < 12; ++p) {
                            struct CupidGBA *g = machine(epochs[epoch], true);
                            program(g, origins[origin], thumb != 0, width);
                            g->cpu.gprs[0] = (int32_t)(UINT32_C(0xA7D30000) | patterns[p]);
                            g->cpu.gprs[1] = GBA_BASE_IO + REG_SOUNDBIAS + lane;
                            g->cpu.gprs[3] = GBA_BASE_IO + REG_SOUNDBIAS;
                            uint16_t raw = patterns[p];
                            if (width == 1)
                                raw = lane ? (uint16_t)(raw << 8) : (uint16_t)(0x200 | (raw & 255));
                            cupid_gba_step(g);
                            check_register(g, fields(raw));
                            cupid_gba_step(g);
                            CHECK((uint32_t)g->cpu.gprs[2] == fields(raw));
                            CHECK(!g->cpu.halted && !g->stopped);
                            destroy(g);
                            ++scenes;
                        }
    CHECK(scenes == 2304);
    printf("ARM/Thumb guest scenes=%u\n", scenes);
}

static void start(struct CupidGBA *g, unsigned channel, uint32_t source, uint32_t dest,
                  unsigned width, bool interrupt) {
    cupid_dma_write_sad(g, (int)channel, source);
    cupid_dma_write_dad(g, (int)channel, dest);
    cupid_dma_write_cnt_lo(g, (int)channel, 1);
    cupid_dma_write_cnt(g, (int)channel,
                        (uint16_t)(0x8000 | (width == 4 ? 0x400 : 0) | (interrupt ? 0x4000 : 0)));
}

static void dma(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned channel = 0; channel < 4; ++channel)
            for (unsigned width = 2; width <= 4; width *= 2)
                for (unsigned power = 0; power < 2; ++power)
                    for (unsigned p = 0; p < 12; ++p) {
                        struct CupidGBA *g = machine(epochs[epoch], power != 0);
                        uint32_t payload = UINT32_C(0xA7D30000) | patterns[p];
                        cupid_memory_store32(&g->cpu, 0x03000400, (int32_t)payload, NULL);
                        start(g, channel, 0x03000400, GBA_BASE_IO + REG_SOUNDBIAS, width, true);
                        cupid_timing_tick(&g->timing, 8);
                        struct CupidDMA *d = &g->memory.dma[channel];
                        check_register(g, fields(patterns[p]));
                        CHECK(!d->enable && !g->memory.dmaRunning && !g->cpuBlocked);
                        CHECK(d->nextSource == 0x03000400 + width &&
                              d->nextDest == GBA_BASE_IO + REG_SOUNDBIAS + width);
                        CHECK(d->busyUntil == epochs[epoch] + 5);
                        CHECK((g->irqf & (IRQ_DMA0 << channel)) != 0);
                        uint32_t expected = width == 4 ? payload : (uint32_t)patterns[p] * 0x10001;
                        CHECK(d->latch == expected && g->memory.dmaBus == expected);
                        CHECK(cupid_timing_global_time(&g->timing) == epochs[epoch] + 8);
                        destroy(g);
                        ++scenes;
                    }
    CHECK(scenes == 768);
    printf("DMA register publication and raw latch scenes=%u\n", scenes);
}

static void input(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned channel = 0; channel < 4; ++channel)
            for (unsigned width = 1; width <= 4; width *= 2)
                for (unsigned lane = 0; lane < 4; ++lane)
                    for (unsigned power = 0; power < 2; ++power)
                        for (unsigned p = 0; p < 12; ++p) {
                            struct CupidGBA *g = machine(epochs[epoch], power != 0);
                            write(g, 0, patterns[p], 0);
                            g->memory.iwramBus = UINT32_C(0x315AE297);
                            g->memory.openBus = UINT32_C(0xB7A5D369);
                            g->memory.inputBusValid = true;
                            start(g, channel, 0x04001000, 0x02000100, 4, false);
                            cupid_timing_tick(&g->timing, 1);
                            g->cpu.memory.timed = true;
                            uint32_t word = fields(patterns[p]), expected, sampled, value;
                            uint32_t address = GBA_BASE_IO + REG_SOUNDBIAS + lane;
                            int cycles = 0;
                            if (width == 1) {
                                value = cupid_memory_load8(&g->cpu, address, &cycles);
                                expected = word >> (8 * lane) & 255;
                                sampled = expected * UINT32_C(0x01010101);
                            } else if (width == 2) {
                                value = cupid_memory_load16(&g->cpu, address, &cycles);
                                uint32_t half = word >> (8 * (lane & 2));
                                expected = ROR(half, (lane & 1) * 8);
                                sampled = half | half << 16;
                            } else {
                                value = cupid_memory_load32(&g->cpu, address, &cycles);
                                expected = ROR(word, 8 * lane);
                                sampled = word;
                            }
                            CHECK(value == expected && cycles == 1);
                            CHECK(g->memory.openBus == sampled && g->memory.inputBusValid);
                            CHECK(g->memory.iwramBus == UINT32_C(0x315AE297));
                            CHECK(g->memory.dmaRunning);
                            cupid_bus_wait(&g->cpu);
                            CHECK(!g->memory.dmaRunning && !g->cpuBlocked);
                            CHECK((uint32_t)cupid_memory_load32(&g->cpu, 0x02000100, NULL) ==
                                  sampled);
                            CHECK(g->memory.dma[channel].latch == sampled);
                            destroy(g);
                            ++scenes;
                        }
    CHECK(scenes == 4608);
    printf("masked input and DMA handover scenes=%u\n", scenes);
}

static void pcm(void) {
    const int samples[] = {-128, -1, 0, 1, 127};
    unsigned scenes = 0;
    for (unsigned power = 0; power < 2; ++power) {
        struct CupidGBA *g[2] = {machine(0, power != 0), machine(0, power != 0)};
        struct Capture c[2] = {{0}, {0}};
        for (unsigned side = 0; side < 2; ++side) {
            g[side]->audio.sampleSink = capture;
            g[side]->audio.sampleSinkContext = &c[side];
            g[side]->audio.chALeft = g[side]->audio.chBRight = true;
            g[side]->audio.volumeChA = g[side]->audio.volumeChB = true;
        }
        for (uint32_t raw = 0; raw <= UINT16_MAX; ++raw) {
            for (unsigned side = 0; side < 2; ++side) {
                c[side].count = 0;
                g[side]->audio.chA.latchedSample = (int8_t)samples[raw % 5];
                g[side]->audio.chB.latchedSample = (int8_t)samples[(raw / 5) % 5];
                write(g[side], side ? 1 + (raw % 4) : 3,
                      side ? (uint16_t)raw : fields((uint16_t)raw), 0);
                uint64_t next = g[side]->audio.nextSample;
                cupid_timing_tick(&g[side]->timing,
                                  (int)(next - cupid_timing_global_time(&g[side]->timing)));
                CHECK(c[side].count == 1 && c[side].clock[0] == next);
            }
            CHECK(c[0].clock[0] == c[1].clock[0]);
            CHECK(c[0].left[0] == c[1].left[0] && c[0].right[0] == c[1].right[0]);
            if (!power)
                CHECK(c[0].left[0] == 0 && c[0].right[0] == 0);
            ++scenes;
        }
        destroy(g[0]);
        destroy(g[1]);
    }
    CHECK(scenes == 131072);
    printf("canonical/ignored-bit PCM pairs=%u\n", scenes);
}

static void chronology(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned mode = 0; mode < 4; ++mode)
            for (unsigned phase = 511; phase <= 513; ++phase)
                for (unsigned method = 1; method < 5; ++method)
                    for (unsigned power = 0; power < 2; ++power)
                        for (unsigned punctual = 0; punctual < 2; ++punctual) {
                            struct Capture c[2] = {{0}, {0}};
                            struct CupidGBA *g[2];
                            for (unsigned side = 0; side < 2; ++side) {
                                g[side] = machine(epochs[epoch], power != 0);
                                g[side]->audio.sampleSink = capture;
                                g[side]->audio.sampleSinkContext = &c[side];
                                write(g[side], 3, 0, 0);
                                if (punctual)
                                    for (unsigned i = 0; i < phase; ++i)
                                        cupid_timing_tick(&g[side]->timing, 1);
                                else
                                    cupid_timing_tick(&g[side]->timing, (int)phase);
                                uint16_t raw =
                                    (uint16_t)((mode << 14) | 0x3FE | (side ? 0x3C01 : 0));
                                write(g[side], method, raw, 1);
                                unsigned remainder = 2048 - phase - (method <= 2 ? 1 : 0);
                                if (punctual)
                                    for (unsigned i = 0; i < remainder; ++i)
                                        cupid_timing_tick(&g[side]->timing, 1);
                                else
                                    cupid_timing_tick(&g[side]->timing, (int)remainder);
                            }
                            CHECK(c[0].count == c[1].count && c[0].count > 0);
                            CHECK(!memcmp(c[0].clock, c[1].clock,
                                          c[0].count * sizeof(c[0].clock[0])));
                            CHECK(!memcmp(c[0].left, c[1].left, c[0].count * sizeof(c[0].left[0])));
                            CHECK(!memcmp(c[0].right, c[1].right,
                                          c[0].count * sizeof(c[0].right[0])));
                            CHECK(g[0]->audio.nextSample == g[1]->audio.nextSample);
                            CHECK(cupid_timing_global_time(&g[0]->timing) == epochs[epoch] + 2048);
                            CHECK(cupid_timing_global_time(&g[1]->timing) == epochs[epoch] + 2048);
                            check_register(g[1], (uint16_t)(mode << 14 | 0x3FE));
                            destroy(g[0]);
                            destroy(g[1]);
                            ++scenes;
                        }
    CHECK(scenes == 768);
    printf("capture chronology pairs=%u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "registers"))
        registers();
    else if (!strcmp(argv[1], "guest"))
        guest();
    else if (!strcmp(argv[1], "dma"))
        dma();
    else if (!strcmp(argv[1], "input"))
        input();
    else if (!strcmp(argv[1], "pcm"))
        pcm();
    else {
        CHECK(!strcmp(argv[1], "chronology"));
        chronology();
    }
    return 0;
}
