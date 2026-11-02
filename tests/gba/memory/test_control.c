/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_control.c
 * @brief Internal-memory remapping, protection, timing and pipeline views.
 */
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

static const unsigned controls[] = {0x20, 0, 0x21, 1};
/* Independent device table: BIOS, unmapped, external RAM, internal RAM. */
static const unsigned devices[4][4] = {{0, 1, 2, 3}, {0, 1, 3, 3}, {2, 3, 0, 1}, {3, 3, 0, 1}};

static struct CupidGBA *machine(void) {
    struct CupidGBA *g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    cupid_gba_reset(g);
    cupid_timing_clear(&g->timing);
    g->cpu.cpsr.i = 1;
    g->cpu.irqDisableLatch = true;
    g->memory.activeRegion = -1;
    return g;
}

static void destroy(struct CupidGBA *g) {
    cupid_gba_destroy(g);
    free(g);
}

static void select_control(struct CupidGBA *g, unsigned value) {
    cupid_memory_store16(&g->cpu, 0x04000800, (int16_t)value, NULL);
}

static void registers(void) {
    struct CupidGBA *fresh = calloc(1, sizeof(*fresh));
    CHECK(fresh);
    cupid_gba_create(fresh);
    CHECK(cupid_memory_load16(&fresh->cpu, 0x04000800, NULL) == 0x20);
    cupid_memory_update_ewram_waitstates(fresh, 0x0D00);
    cupid_memory_set_active_region(&fresh->cpu, GBA_BASE_EWRAM);
    CHECK(fresh->cpu.memory.activeRegion == (const uint32_t *)fresh->memory.ewram);
    destroy(fresh);
    struct CupidGBA *g = machine();
    CHECK(cupid_memory_load32(&g->cpu, 0x04000800, NULL) == 0x0D000020);
    for (unsigned bank = 0; bank < 256; ++bank) {
        const uint32_t base = 0x04000800 + (bank << 16);
        for (unsigned value = 0; value < 256; ++value) {
            for (unsigned width = 1; width <= 4; width *= 2) {
                if (width == 1)
                    cupid_memory_store8(&g->cpu, base, (int8_t)value, NULL);
                else if (width == 2)
                    cupid_memory_store16(&g->cpu, base, (int16_t)(value | 0xFF00), NULL);
                else
                    cupid_memory_store32(&g->cpu, base, (int32_t)(0x0D00FF00 | value), NULL);
                CHECK(cupid_memory_load32(&g->cpu, base, NULL) == (0x0D000000 | (value & 0x2F)));
                CHECK(cupid_memory_load16(&g->cpu, 0x04000800, NULL) == (value & 0x2F));
                cupid_memory_store8(&g->cpu, base + 1, -1, NULL);
                CHECK(cupid_memory_load8(&g->cpu, base + 1, NULL) == 0);
                CHECK(cupid_memory_load8(&g->cpu, base, NULL) == (value & 0x2F));
            }
        }
    }
    select_control(g, 0x2F);
    cupid_gba_reset(g);
    CHECK(cupid_memory_load32(&g->cpu, 0x04FF0800, NULL) == 0x0D000020);
    destroy(g);
}

static void storage(void) {
    static const uint32_t offsets[] = {0, 0x3FFC, 0x7FFC, 0x8000, 0x3FFFC, 0xFFFFFC};
    struct CupidGBA *g = machine();
    uint8_t *expectedE = malloc(GBA_SIZE_EWRAM), *expectedI = malloc(GBA_SIZE_IWRAM);
    CHECK(expectedE && expectedI);
    for (unsigned scene = 0; scene < 4; ++scene) {
        select_control(g, controls[scene]);
        for (unsigned region = 0; region < 4; ++region) {
            const unsigned device = devices[scene][region];
            if (device < 2)
                continue;
            for (unsigned sample = 0; sample < sizeof(offsets) / sizeof(*offsets); ++sample) {
                for (unsigned width = 1; width <= 4; width *= 2) {
                    memset(g->memory.ewram, 0xE2, GBA_SIZE_EWRAM);
                    memset(g->memory.iwram, 0x13, GBA_SIZE_IWRAM);
                    memcpy(expectedE, g->memory.ewram, GBA_SIZE_EWRAM);
                    memcpy(expectedI, g->memory.iwram, GBA_SIZE_IWRAM);
                    const uint32_t address = (region << 24) | offsets[sample];
                    const uint32_t value = 0x76543210;
                    uint8_t *expected = device == 2 ? expectedE : expectedI;
                    const unsigned size = device == 2 ? GBA_SIZE_EWRAM : GBA_SIZE_IWRAM;
                    memcpy(expected + (address & (size - 1)), &value, width);
                    if (width == 1)
                        cupid_memory_store8(&g->cpu, address, (int8_t)value, NULL);
                    else if (width == 2)
                        cupid_memory_store16(&g->cpu, address, (int16_t)value, NULL);
                    else
                        cupid_memory_store32(&g->cpu, address, (int32_t)value, NULL);
                    CHECK(!memcmp(expectedE, g->memory.ewram, GBA_SIZE_EWRAM));
                    CHECK(!memcmp(expectedI, g->memory.iwram, GBA_SIZE_IWRAM));
                    for (unsigned alias = 0; alias < 4; ++alias) {
                        if (devices[scene][alias] != device)
                            continue;
                        const uint32_t mirror = (alias << 24) | offsets[sample];
                        uint32_t word;
                        memcpy(&word, expected + (mirror & (size - 4)), 4);
                        CHECK(cupid_memory_load32(&g->cpu, mirror, NULL) == word);
                        for (unsigned lane = 0; lane < 4; ++lane)
                            CHECK(cupid_memory_load8(&g->cpu, mirror + lane, NULL) ==
                                  (word >> (lane * 8) & 255));
                        CHECK(cupid_memory_load16(&g->cpu, mirror, NULL) == (word & 65535));
                        CHECK(cupid_memory_load16(&g->cpu, mirror + 2, NULL) == word >> 16);
                    }
                }
            }
        }
    }
    free(expectedE);
    free(expectedI);
    destroy(g);
}

static void protection(void) {
    struct CupidGBA *g = machine();
    const uint32_t bios = 0x12345678, retained = 0xA1B2C3D4;
    memcpy(g->memory.bios + 0x100, &bios, 4);
    for (unsigned scene = 0; scene < 4; ++scene) {
        g->memory.activeRegion = -1;
        select_control(g, controls[scene]);
        const uint32_t base = controls[scene] & 1 ? GBA_BASE_EWRAM : 0;
        g->cpu.gprs[ARM_PC] = (int32_t)base;
        cupid_memory_set_active_region(&g->cpu, base);
        CHECK(cupid_memory_load32(&g->cpu, base + 0x100, NULL) == bios);
        cupid_memory_store32(&g->cpu, base + 0x100, 0, NULL);
        CHECK(!memcmp(g->memory.bios + 0x100, &bios, 4));
        g->memory.activeRegion = GBA_REGION_IWRAM;
        g->memory.biosPrefetch = retained;
        CHECK(cupid_memory_load32(&g->cpu, base + 0x100, NULL) == retained);
        CHECK(cupid_memory_load16(&g->cpu, base + 0x102, NULL) == retained >> 16);
        CHECK(cupid_memory_load8(&g->cpu, base + 0x101, NULL) == (retained >> 8 & 255));
        g->memory.openBus = 0x55667788;
        g->memory.inputBusValid = true;
        CHECK(cupid_memory_load32(&g->cpu, base + GBA_SIZE_BIOS, NULL) == 0x55667788);
        CHECK(cupid_memory_load32(&g->cpu, base + 0x01000000, NULL) == 0x55667788);
    }
    destroy(g);
}

static void timing(void) {
    const uint64_t epochs[] = {0, INT32_MAX - 8u, UINT32_MAX - 8u, UINT64_C(0x300000000)};
    for (unsigned epoch = 0; epoch < 4; ++epoch) {
        struct CupidGBA *g = machine();
        g->timing.globalCycles = epochs[epoch];
        g->timing.masterCycles = (uint32_t)epochs[epoch];
        for (unsigned scene = 0; scene < 4; ++scene) {
            g->memory.activeRegion = -1;
            select_control(g, controls[scene]);
            for (unsigned region = 0; region < 4; ++region) {
                if (devices[scene][region] < 2)
                    continue;
                for (unsigned width = 1; width <= 4; width *= 2) {
                    const uint32_t address = (region << 24) | 0x100;
                    const unsigned expected =
                        devices[scene][region] == 2 ? (width == 4 ? 6 : 3) : 1;
                    g->cpu.memory.timed = true;
                    int cycles = 0;
                    uint64_t before = cupid_timing_global_time(&g->timing);
                    if (width == 1)
                        cupid_memory_store8(&g->cpu, address, 0x35, &cycles);
                    else if (width == 2)
                        cupid_memory_store16(&g->cpu, address, 0x3579, &cycles);
                    else
                        cupid_memory_store32(&g->cpu, address, 0x35792468, &cycles);
                    CHECK(cupid_timing_global_time(&g->timing) - before == expected);
                    cycles = 0;
                    before = cupid_timing_global_time(&g->timing);
                    if (width == 1)
                        CHECK(cupid_memory_load8(&g->cpu, address, &cycles) == 0x35);
                    else if (width == 2)
                        CHECK(cupid_memory_load16(&g->cpu, address, &cycles) == 0x3579);
                    else
                        CHECK(cupid_memory_load32(&g->cpu, address, &cycles) == 0x35792468);
                    CHECK(cupid_timing_global_time(&g->timing) - before == expected);
                    g->cpu.gprs[ARM_PC] = (int32_t)address;
                    cupid_memory_set_active_region(&g->cpu, address);
                    for (unsigned fetchWidth = 2; fetchWidth <= 4; fetchWidth += 2) {
                        before = cupid_timing_global_time(&g->timing);
                        cupid_bus_fetch(&g->cpu, address, fetchWidth, false);
                        cupid_bus_finish(&g->cpu);
                        CHECK(cupid_timing_global_time(&g->timing) - before ==
                              (devices[scene][region] == 2 ? fetchWidth / 2 * 3 : 1));
                    }
                    g->cpu.memory.timed = false;
                }
            }
        }
        destroy(g);
    }
}

/* Block transfers cross the physical 32 KiB boundary while keeping virtual
 * writeback addresses. Check every direction with sparse register lists. */
static void blocks(void) {
    const uint32_t values[] = {0x12345678, 0xA5B6C7D8, 0x87654321, 0x10203040};
    const int starts[] = {0, -4, 12, 16};
    for (unsigned scene = 0; scene < 4; ++scene) {
        for (unsigned region = 0; region < 4; ++region) {
            const unsigned device = devices[scene][region];
            if (device < 2)
                continue;
            for (unsigned direction = 0; direction < 4; ++direction) {
                for (unsigned timed = 0; timed < 2; ++timed) {
                    struct CupidGBA *g = machine();
                    select_control(g, controls[scene]);
                    g->cpu.memory.timed = timed;
                    uint8_t *buffer = device == 2 ? g->memory.ewram : g->memory.iwram;
                    const unsigned size = device == 2 ? GBA_SIZE_EWRAM : GBA_SIZE_IWRAM;
                    const uint32_t first = (region << 24) | 0x7FFC;
                    const uint32_t address = first + starts[direction];
                    const uint32_t writeback = direction < 2 ? address + 16 : address - 16;
                    const unsigned clocks = device == 2 ? 24 : 4;
                    for (unsigned i = 0; i < 4; ++i)
                        g->cpu.gprs[i * 2] = (int32_t)values[i];
                    int cycles = 0;
                    uint64_t before = cupid_timing_global_time(&g->timing);
                    CHECK(cupid_memory_store_multiple(&g->cpu, address, 0x55,
                                                      (enum CupidLSMDirection)direction,
                                                      &cycles) == writeback);
                    CHECK(!g->cpu.memory.dataSequential);
                    CHECK(timed ? cupid_timing_global_time(&g->timing) - before == clocks
                                : cycles == (int)clocks);
                    for (unsigned i = 0; i < 4; ++i) {
                        uint32_t actual;
                        memcpy(&actual, buffer + ((first + i * 4) & (size - 1)), 4);
                        CHECK(actual == values[i]);
                        g->cpu.gprs[i * 2] = 0;
                        g->cpu.gprs[i * 2 + 1] = -1;
                    }
                    cycles = 0;
                    before = cupid_timing_global_time(&g->timing);
                    CHECK(cupid_memory_load_multiple(&g->cpu, address, 0x55,
                                                     (enum CupidLSMDirection)direction,
                                                     &cycles) == writeback);
                    CHECK(!g->cpu.memory.dataSequential);
                    CHECK(timed ? cupid_timing_global_time(&g->timing) - before == clocks
                                : cycles == (int)clocks + 1);
                    for (unsigned i = 0; i < 4; ++i) {
                        CHECK((uint32_t)g->cpu.gprs[i * 2] == values[i]);
                        CHECK(g->cpu.gprs[i * 2 + 1] == -1);
                    }
                    destroy(g);
                }
            }
        }
    }
}

static void pipeline(void) {
    const uint32_t armStores[] = {0xE5C10000, 0xE1C100B0, 0xE5810000};
    const uint16_t thumbStores[] = {0x7008, 0x8008, 0x6008};
    const uint64_t epochs[] = {0, INT32_MAX - 8u, UINT32_MAX - 8u, UINT64_C(0x300000000)};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch) {
        for (unsigned from = 0; from < 4; ++from) {
            for (unsigned to = 0; to < 4; ++to) {
                for (unsigned region = 0; region < 4; ++region) {
                    const unsigned oldDevice = devices[from][region],
                                   newDevice = devices[to][region];
                    if (oldDevice == 1 || newDevice == 1 || oldDevice == newDevice)
                        continue;
                    for (unsigned thumb = 0; thumb < 2; ++thumb) {
                        for (unsigned width = 0; width < 3; ++width) {
                            struct CupidGBA *g = machine();
                            g->timing.globalCycles = epochs[epoch];
                            g->timing.masterCycles = (uint32_t)epochs[epoch];
                            select_control(g, controls[from]);
                            uint8_t *buffers[] = {g->memory.bios, NULL, g->memory.ewram,
                                                  g->memory.iwram};
                            uint32_t oldArm[] = {armStores[width], 0xE3A02011, 0xE3A03022,
                                                 0xE3A04033};
                            const uint32_t newArm[] = {0xE1A00000, 0xE3A02044, 0xE3A03055,
                                                       0xE3A04066};
                            uint16_t oldThumb[] = {thumbStores[width], 0x2211, 0x2322, 0x2433};
                            const uint16_t newThumb[] = {0x46C0, 0x2244, 0x2355, 0x2466};
                            memcpy(buffers[oldDevice],
                                   thumb ? (const void *)oldThumb : (const void *)oldArm,
                                   thumb ? sizeof(oldThumb) : sizeof(oldArm));
                            memcpy(buffers[newDevice],
                                   thumb ? (const void *)newThumb : (const void *)newArm,
                                   thumb ? sizeof(newThumb) : sizeof(newArm));
                            _CupidSetMode(&g->cpu, thumb ? MODE_THUMB : MODE_ARM);
                            g->cpu.gprs[0] =
                                (int32_t)(controls[to] | (width == 2 ? 0x0D000000 : 0));
                            g->cpu.gprs[1] = 0x04FF0800;
                            g->cpu.gprs[ARM_PC] = (int32_t)(region << 24);
                            g->cpu.memory.timed = true;
                            if (thumb)
                                CupidThumbWritePC(&g->cpu);
                            else
                                CupidARMWritePC(&g->cpu);
                            const uint32_t lastJump = g->lastJump;
                            cupid_gba_step(g);
                            CHECK(g->lastJump == lastJump);
                            CHECK(g->cpu.memory.activeRegion ==
                                  (const uint32_t *)buffers[newDevice]);
                            CHECK(g->cpu.prefetch[0] == (thumb ? oldThumb[1] : oldArm[1]));
                            CHECK(g->cpu.prefetch[1] == (thumb ? oldThumb[2] : oldArm[2]));
                            cupid_gba_step(g);
                            CHECK(g->cpu.gprs[2] == 0x11);
                            cupid_gba_step(g);
                            CHECK(g->cpu.gprs[3] == 0x22);
                            cupid_gba_step(g);
                            CHECK(g->cpu.gprs[4] == 0x66);
                            destroy(g);
                            ++scenes;
                        }
                    }
                }
            }
        }
    }
    CHECK(scenes == 480);
    printf("ARM/Thumb decoder transitions=%u\n", scenes);
}

static void dma(void) {
    const uint32_t words[] = {0x12345678, 0x9ABCDEF0, 0x13579BDF, 0x2468ACE0};
    for (unsigned scene = 0; scene < 4; ++scene) {
        for (unsigned source = 0; source < 4; ++source) {
            for (unsigned target = 0; target < 4; ++target) {
                if (devices[scene][target] < 2)
                    continue;
                for (unsigned word = 0; word < 2; ++word) {
                    struct CupidGBA *g = machine();
                    select_control(g, controls[scene]);
                    uint8_t *src = devices[scene][source] == 2 ? g->memory.ewram : g->memory.iwram;
                    uint8_t *dst = devices[scene][target] == 2 ? g->memory.ewram : g->memory.iwram;
                    memcpy(src + 0x100, words, sizeof(words));
                    memset(dst + 0x200, 0xCC, sizeof(words));
                    g->memory.dma[3].latch = 0x56785678;
                    cupid_dma_write_sad(g, 3, (source << 24) | 0x100);
                    cupid_dma_write_dad(g, 3, (target << 24) | 0x200);
                    cupid_dma_write_cnt_lo(g, 3, 4);
                    cupid_dma_write_cnt(g, 3, word ? 0x8400 : 0x8000);
                    const unsigned sourceClocks = devices[scene][source] == 2 ? (word ? 6 : 3) : 1;
                    const unsigned targetClocks = devices[scene][target] == 2 ? (word ? 6 : 3) : 1;
                    const unsigned duration = 4 * (sourceClocks + targetClocks) + 4;
                    cupid_timing_tick(&g->timing, (int32_t)duration);
                    CHECK(!g->memory.dmaRunning && !g->memory.dma[3].enable);
                    CHECK(g->memory.dma[3].busyUntil == duration - 1);
                    const size_t length = word ? 16 : 8;
                    if (devices[scene][source] >= 2) {
                        CHECK(!memcmp(dst + 0x200, words, length));
                    } else {
                        for (size_t i = 0; i < length; ++i)
                            CHECK(dst[0x200 + i] == (i & 1 ? 0x56 : 0x78));
                    }
                    for (size_t i = length; i < sizeof(words); ++i)
                        CHECK(dst[0x200 + i] == 0xCC);
                    destroy(g);
                }
            }
        }
    }
}

static void feedback(void) {
    const uint32_t contents = 0x12345678;
    for (unsigned scene = 0; scene < 4; ++scene) {
        for (unsigned region = 0; region < 4; ++region) {
            if (devices[scene][region] != 3)
                continue;
            struct CupidGBA *g = machine();
            select_control(g, controls[scene]);
            memcpy(g->memory.iwram + 0x100, &contents, 4);
            const uint32_t address = (region << 24) | 0x100;
            for (unsigned lane = 0; lane < 2; ++lane) {
                g->memory.iwramBus = 0xA1B2C3D4;
                g->cpu.memory.timed = true;
                int cycles = 0;
                uint16_t value = (uint16_t)(contents >> (lane * 16));
                CHECK(cupid_memory_load16(&g->cpu, address + lane * 2, &cycles) == value);
                uint32_t expected = lane ? 0x1234C3D4 : 0xA1B25678;
                CHECK(g->memory.iwramBus == expected);
                CHECK(cupid_memory_open_bus(&g->cpu) == expected);
                g->memory.iwramBus = 0xA1B2C3D4;
                cupid_memory_capture_opcode(&g->cpu, address + lane * 2, 2, value);
                CHECK(g->memory.iwramBus == expected);
                CHECK(cupid_memory_open_bus(&g->cpu) == expected);
            }
            destroy(g);
        }
    }
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "registers"))
        RUN(registers);
    else if (!strcmp(argv[1], "storage"))
        RUN(storage);
    else if (!strcmp(argv[1], "protection"))
        RUN(protection);
    else if (!strcmp(argv[1], "timing")) {
        RUN(timing);
        RUN(blocks);
    } else if (!strcmp(argv[1], "pipeline"))
        RUN(pipeline);
    else if (!strcmp(argv[1], "dma"))
        RUN(dma);
    else if (!strcmp(argv[1], "feedback"))
        RUN(feedback);
    else
        CHECK(false);
    return 0;
}
