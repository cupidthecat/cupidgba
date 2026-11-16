/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_display_control.c
 * @author    cupidthecat
 * @brief     GBA display-control fields across CPU, DMA and timed display accesses.
 * @date      2026-11-16
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
#include "../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

/* Each writable field is exercised beside the protected legacy-mode bit.
 * BIOS and DMA cases qualify the selected GBA-only policy, not legacy activation. */
static const struct {
    uint16_t raw, fields;
} patterns[] = {{0x0000, 0x0000}, {0x0008, 0x0000}, {0x0007, 0x0007}, {0x000F, 0x0007},
                {0x0088, 0x0080}, {0x0018, 0x0010}, {0x0028, 0x0020}, {0x0048, 0x0040},
                {0x0108, 0x0100}, {0x0208, 0x0200}, {0x0408, 0x0400}, {0x0808, 0x0800},
                {0x1008, 0x1000}, {0x2008, 0x2000}, {0x4008, 0x4000}, {0x8008, 0x8000},
                {0xFFFF, 0xFFF7}, {0xA55A, 0xA552}, {0x1248, 0x1240}, {0x8088, 0x8080}};

static const uint64_t epochs[] = {0, UINT32_MAX - UINT64_C(4)};

static struct CupidGBA *machine(uint64_t epoch) {
    struct CupidGBA *g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    cupid_gba_reset(g);
    cupid_timing_clear(&g->timing);
    g->timing.globalCycles = epoch;
    g->timing.masterCycles = (uint32_t)epoch;
    cupid_video_reset(&g->video);
    g->cpu.cpsr.i = 1;
    g->cpu.irqDisableLatch = true;
    return g;
}

static void destroy(struct CupidGBA *g) {
    cupid_gba_destroy(g);
    free(g);
}

static void store(struct CupidGBA *g, unsigned width, uint32_t address, uint32_t value,
                  int *cycles) {
    if (width == 1)
        cupid_memory_store8(&g->cpu, address, (int8_t)value, cycles);
    else if (width == 2)
        cupid_memory_store16(&g->cpu, address, (int16_t)value, cycles);
    else
        cupid_memory_store32(&g->cpu, address, (int32_t)value, cycles);
}

static void check_register(struct CupidGBA *g, uint16_t fields, unsigned green) {
    CHECK(cupid_io_read(g, REG_DISPCNT) == fields);
    CHECK(g->video.dispcnt == fields && g->memory.io[REG_DISPCNT >> 1] == fields);
    CHECK(cupid_memory_load8(&g->cpu, GBA_BASE_IO, NULL) == (fields & 255));
    CHECK(cupid_memory_load8(&g->cpu, GBA_BASE_IO + 1, NULL) == (fields >> 8));
    CHECK(cupid_io_read(g, REG_GREENSWAP) == green);
    CHECK((uint32_t)cupid_memory_load32(&g->cpu, GBA_BASE_IO, NULL) ==
          (fields | (uint32_t)green << 16));
}

static void registers(void) {
    const uint16_t initial[] = {0x0080, 0xA550};
    struct CupidGBA *g = machine(0);
    unsigned scenes = 0;
    for (unsigned old = 0; old < 2; ++old)
        for (unsigned method = 0; method < 5; ++method)
            for (unsigned p = 0; p < sizeof(patterns) / sizeof(*patterns); ++p) {
                cupid_io_write(g, REG_DISPCNT, initial[old]);
                cupid_io_write(g, REG_GREENSWAP, 1);
                uint16_t raw = patterns[p].raw, fields = patterns[p].fields;
                if (method == 0)
                    cupid_io_write(g, REG_DISPCNT, raw);
                else if (method <= 2) {
                    unsigned first = method == 2;
                    store(g, 1, GBA_BASE_IO + first, raw >> (8 * first), NULL);
                    uint16_t partial = first ? (initial[old] & 255) | (raw & 0xFF00)
                                             : (initial[old] & 0xFF00) | (fields & 255);
                    check_register(g, partial, 1);
                    store(g, 1, GBA_BASE_IO + (first ^ 1), raw >> (8 * (first ^ 1)), NULL);
                } else
                    store(g, method == 3 ? 2 : 4, GBA_BASE_IO,
                          raw | ((UINT32_C(0xA7D2) | (p & 1)) << 16), NULL);
                check_register(g, fields, method == 4 ? p & 1 : 1);
                CHECK(cupid_timing_global_time(&g->timing) == 0);
                ++scenes;
            }
    CHECK(scenes == 200);
    printf("register and byte-lane scenes=%u\n", scenes);
    destroy(g);
}

/* The following word load makes both DISPCNT and GREENSWAP visible to guest code. */
static void program(struct CupidGBA *g, uint32_t origin, bool thumb, unsigned width) {
    const unsigned index = width == 1 ? 0 : width == 2 ? 1 : 2;
    const uint32_t arm[] = {0xE5C10000, 0xE1C100B0, 0xE5810000};
    const uint16_t narrow[] = {0x7008, 0x8008, 0x6008};
    uint8_t *data;
    if (origin < GBA_SIZE_BIOS)
        data = (uint8_t *)g->memory.bios + origin;
    else if (origin >> 24 == GBA_REGION_EWRAM)
        data = g->memory.ewram + 0x100;
    else if (origin >> 24 == GBA_REGION_IWRAM)
        data = g->memory.iwram + 0x100;
    else {
        g->memory.rom = calloc(1, 0x400);
        CHECK(g->memory.rom);
        g->memory.romSize = g->memory.romImageSize = 0x400;
        g->memory.romMask = 0x3FF;
        data = g->memory.rom + 0x100;
    }
    if (thumb) {
        const uint16_t code[] = {narrow[index], 0x681A, 0x46C0, 0x46C0};
        memcpy(data, code, sizeof(code));
        _CupidSetMode(&g->cpu, MODE_THUMB);
        g->cpu.gprs[ARM_PC] = (int32_t)origin;
        CupidThumbWritePC(&g->cpu);
    } else {
        const uint32_t code[] = {arm[index], 0xE5932000, 0xE1A00000, 0xE1A00000};
        memcpy(data, code, sizeof(code));
        g->cpu.gprs[ARM_PC] = (int32_t)origin;
        CupidARMWritePC(&g->cpu);
    }
}

static void guest(void) {
    const uint32_t origins[] = {0x02000100, 0x03000100, 0x08000100, 0x100};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 2; ++epoch)
        for (unsigned origin = 0; origin < 4; ++origin)
            for (unsigned thumb = 0; thumb < 2; ++thumb)
                for (unsigned width = 1; width <= 4; width *= 2)
                    for (unsigned lane = 0; lane < (width == 1 ? 2 : width); ++lane)
                        for (unsigned p = 0; p < sizeof(patterns) / sizeof(*patterns); ++p) {
                            struct CupidGBA *g = machine(epochs[epoch]);
                            program(g, origins[origin], thumb != 0, width);
                            cupid_io_write(g, REG_DISPCNT, 0x0080);
                            cupid_io_write(g, REG_GREENSWAP, 0);
                            g->cpu.gprs[0] = (int32_t)(UINT32_C(0xA7D30000) | patterns[p].raw);
                            g->cpu.gprs[1] = GBA_BASE_IO + lane;
                            g->cpu.gprs[3] = GBA_BASE_IO;
                            uint16_t fields = patterns[p].fields;
                            if (width == 1)
                                fields =
                                    lane ? (uint16_t)(0x80 | patterns[p].raw << 8) : fields & 255;
                            unsigned green = width == 4;
                            cupid_gba_step(g);
                            check_register(g, fields, green);
                            cupid_gba_step(g);
                            CHECK((uint32_t)g->cpu.gprs[2] == (fields | (uint32_t)green << 16));
                            CHECK(!g->cpu.halted && !g->stopped);
                            destroy(g);
                            ++scenes;
                        }
    CHECK(scenes == 2560);
    printf("ARM/Thumb stores and guest readbacks=%u\n", scenes);
}

static void start_dma(struct CupidGBA *g, unsigned channel, uint32_t source, uint32_t dest,
                      unsigned width, bool interrupt) {
    cupid_dma_write_sad(g, (int)channel, source);
    cupid_dma_write_dad(g, (int)channel, dest);
    cupid_dma_write_cnt_lo(g, (int)channel, 1);
    cupid_dma_write_cnt(g, (int)channel,
                        (uint16_t)(0x8000 | (width == 4 ? 0x400 : 0) | (interrupt ? 0x4000 : 0)));
}

static void dma(void) {
    const unsigned cases[] = {1, 16, 18, 0};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 2; ++epoch)
        for (unsigned channel = 0; channel < 4; ++channel)
            for (unsigned width = 2; width <= 4; width *= 2)
                for (unsigned origin = 0; origin < 2; ++origin)
                    for (unsigned p = 0; p < 4; ++p) {
                        struct CupidGBA *g = machine(epochs[epoch]);
                        program(g, origin ? 0x100 : 0x03000100, false, 2);
                        uint16_t raw = patterns[cases[p]].raw, fields = patterns[cases[p]].fields;
                        uint32_t payload = UINT32_C(0xA7D30000) | raw;
                        cupid_memory_store32(&g->cpu, 0x03000400, (int32_t)payload, NULL);
                        start_dma(g, channel, 0x03000400, GBA_BASE_IO, width, true);
                        cupid_timing_tick(&g->timing, 8);
                        struct CupidDMA *d = &g->memory.dma[channel];
                        unsigned green = width == 4;
                        check_register(g, fields, green);
                        CHECK(!d->enable && !g->memory.dmaRunning && !g->cpuBlocked);
                        CHECK(d->nextSource == 0x03000400 + width &&
                              d->nextDest == GBA_BASE_IO + width);
                        CHECK(d->busyUntil == epochs[epoch] + 5);
                        CHECK((g->irqf & (IRQ_DMA0 << channel)) != 0);
                        uint32_t expected = width == 4 ? payload : (uint32_t)raw * 0x10001;
                        CHECK(d->latch == expected && g->memory.dmaBus == expected);
                        start_dma(g, channel, GBA_BASE_IO, 0x02000100, width, false);
                        /* The word readback needs two request clocks, a head,
                         * one I/O read, six EWRAM write clocks and a tail. */
                        cupid_timing_tick(&g->timing, 11);
                        expected = fields | (uint32_t)green << 16;
                        CHECK(!d->enable && !g->memory.dmaRunning && !g->cpuBlocked);
                        CHECK(d->busyUntil == epochs[epoch] + (width == 4 ? 18 : 15));
                        CHECK((uint32_t)cupid_memory_load32(&g->cpu, 0x02000100, NULL) == expected);
                        CHECK(cupid_timing_global_time(&g->timing) == epochs[epoch] + 19);
                        destroy(g);
                        ++scenes;
                    }
    CHECK(scenes == 128);
    printf("DMA writes, raw latches and register readback=%u\n", scenes);
}

static void input(void) {
    const unsigned cases[] = {1, 16, 18, 0};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 2; ++epoch)
        for (unsigned channel = 0; channel < 4; ++channel)
            for (unsigned width = 1; width <= 4; width *= 2)
                for (unsigned lane = 0; lane < 4; ++lane)
                    for (unsigned green = 0; green < 2; ++green)
                        for (unsigned p = 0; p < 4; ++p) {
                            struct CupidGBA *g = machine(epochs[epoch]);
                            cupid_io_write(g, REG_DISPCNT, patterns[cases[p]].raw);
                            cupid_io_write(g, REG_GREENSWAP, (uint16_t)green);
                            g->memory.iwramBus = UINT32_C(0x315AE297);
                            g->memory.openBus = UINT32_C(0xB7A5D369);
                            g->memory.inputBusValid = true;
                            start_dma(g, channel, 0x04001000, 0x02000100, 4, false);
                            cupid_timing_tick(&g->timing, 1);
                            g->cpu.memory.timed = true;
                            uint32_t word = patterns[cases[p]].fields | (uint32_t)green << 16;
                            uint32_t expected, sampled, value;
                            int cycles = 0;
                            if (width == 1) {
                                value = cupid_memory_load8(&g->cpu, GBA_BASE_IO + lane, &cycles);
                                expected = word >> (8 * lane) & 255;
                                sampled = expected * UINT32_C(0x01010101);
                            } else if (width == 2) {
                                value = cupid_memory_load16(&g->cpu, GBA_BASE_IO + lane, &cycles);
                                uint32_t half = word >> (8 * (lane & 2)) & 0xFFFF;
                                expected = ROR(half, (lane & 1) * 8);
                                sampled = half | half << 16;
                            } else {
                                value = cupid_memory_load32(&g->cpu, GBA_BASE_IO + lane, &cycles);
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
    CHECK(scenes == 768);
    printf("CPU input publication before DMA=%u\n", scenes);
}

static void advance(struct CupidGBA *g, unsigned clocks, bool punctual) {
    if (punctual)
        for (unsigned i = 0; i < clocks; ++i)
            cupid_timing_tick(&g->timing, 1);
    else
        cupid_timing_tick(&g->timing, (int32_t)clocks);
}

static void chronology(void) {
    const unsigned phases[] = {45, 46, 1005, 1006};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 2; ++epoch)
        for (unsigned phase = 0; phase < 4; ++phase)
            for (unsigned width = 1; width <= 4; width *= 2)
                for (unsigned punctual = 0; punctual < 2; ++punctual) {
                    struct CupidGBA *g[2];
                    for (unsigned side = 0; side < 2; ++side) {
                        g[side] = machine(epochs[epoch]);
                        for (unsigned i = 0; i < GBA_SIZE_VRAM / 2; ++i)
                            g[side]->video.vram[i] = (uint16_t)((i * 109) & 0x7FFF);
                        for (unsigned i = 0; i < 128; ++i)
                            g[side]->video.oam.raw[i * 4] = 0x0200;
                        cupid_io_write(g[side], REG_DISPCNT, 0x0403);
                        advance(g[side], phases[phase], punctual != 0);
                        g[side]->cpu.memory.timed = true;
                        int cycles = 0;
                        store(g[side], width, GBA_BASE_IO, 0x0403 | (side ? 8 : 0), &cycles);
                        /* Timed stores consume their bus clock immediately;
                         * unlike loads they leave no internal cycle pending. */
                        CHECK(cycles == 0);
                        CHECK(cupid_timing_global_time(&g[side]->timing) ==
                              epochs[epoch] + phases[phase] + 1);
                        advance(g[side], 1232 - phases[phase] - 1, punctual != 0);
                        cupid_video_sync(&g[side]->video);
                        check_register(g[side], 0x0403, 0);
                    }
                    CHECK(!memcmp(g[0]->video.outputBuffer, g[1]->video.outputBuffer,
                                  sizeof(g[0]->video.outputBuffer)));
                    CHECK(!memcmp(&g[0]->video.pipeline, &g[1]->video.pipeline,
                                  sizeof(g[0]->video.pipeline)));
                    CHECK(g[0]->video.vcount == g[1]->video.vcount);
                    CHECK(g[0]->video.dispstat == g[1]->video.dispstat);
                    CHECK(cupid_timing_global_time(&g[0]->timing) == epochs[epoch] + 1232);
                    CHECK(cupid_timing_global_time(&g[1]->timing) == epochs[epoch] + 1232);
                    destroy(g[0]);
                    destroy(g[1]);
                    ++scenes;
                }
    CHECK(scenes == 48);
    printf("paired display chronology scenes=%u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "registers"))
        RUN(registers);
    else if (!strcmp(argv[1], "guest"))
        RUN(guest);
    else if (!strcmp(argv[1], "dma"))
        RUN(dma);
    else if (!strcmp(argv[1], "input"))
        RUN(input);
    else {
        CHECK(!strcmp(argv[1], "chronology"));
        RUN(chronology);
    }
    return 0;
}
