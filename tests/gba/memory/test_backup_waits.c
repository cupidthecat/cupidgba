/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_backup_waits.c
 * @author    cupidthecat
 * @brief     Backup-bus wait selection through reset, CPU accesses and DMA completion.
 * @date      2026-11-16
 *
 * - Check both SRAM windows before the first WAITCNT write.
 * - Exercise byte-bus timing with ARM, Thumb and direct data accesses.
 * - Observe DMA destination strobes, completion and interrupt publication.
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
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include <string.h>

static const unsigned waits[] = {4, 3, 2, 8};
static const uint32_t windows[] = {GBA_BASE_SRAM, GBA_BASE_SRAM_MIRROR};
static const char *externalBios;
static unsigned observations;

/* Boot 0 uses the built-in RAM entry, 1 uses its cartridge entry, and 2
 * starts at the firmware vector. An optional BIOS replaces the owned stub. */
static struct CupidGBA *machine(unsigned boot) {
    struct CupidGBA *g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    if (boot == 1) {
        g->memory.rom = calloc(1, 0x100);
        CHECK(g->memory.rom);
        g->memory.romSize = g->memory.romImageSize = 0x100;
        g->memory.romMask = 0xFF;
    } else if (boot == 2) {
        if (externalBios)
            CHECK(cupid_load_bios(g, externalBios));
        else {
            const uint32_t code[] = {0xE1A00000, 0xEAFFFFFE};
            memcpy(g->memory.bios, code, sizeof(code));
            g->memory.fullBios = true;
        }
    }
    CHECK(cupid_savedata_init_sram(&g->memory.savedata));
    return g;
}

static void reset_at(struct CupidGBA *g, uint64_t clock) {
    cupid_gba_reset(g);
    cupid_timing_clear(&g->timing);
    g->timing.globalCycles = clock;
    g->timing.masterCycles = (uint32_t)clock;
    g->audio.lastSample = g->audio.sampleOrigin = clock;
    g->audio.nextSample = clock + 512;
    g->cpu.cpsr.i = 1;
    g->cpu.irqDisableLatch = true;
    CHECK(cupid_io_read(g, REG_WAITCNT) == 0);
}

static void destroy(struct CupidGBA *g) {
    cupid_gba_destroy(g);
    free(g);
}

static uint32_t read_width(struct CupidGBA *g, uint32_t address, unsigned width, int *cycles) {
    if (width == 1)
        return cupid_memory_load8(&g->cpu, address, cycles);
    if (width == 2)
        return cupid_memory_load16(&g->cpu, address, cycles);
    return cupid_memory_load32(&g->cpu, address, cycles);
}

static void write_width(struct CupidGBA *g, uint32_t address, unsigned width, uint32_t value,
                        int *cycles) {
    if (width == 1)
        cupid_memory_store8(&g->cpu, address, (int8_t)value, cycles);
    else if (width == 2)
        cupid_memory_store16(&g->cpu, address, (int16_t)value, cycles);
    else
        cupid_memory_store32(&g->cpu, address, (int32_t)value, cycles);
}

static uint32_t repeated_byte(unsigned width) {
    return width == 1 ? 0xA5 : width == 2 ? 0xA5A5 : UINT32_C(0xA5A5A5A5);
}

static void check_elapsed(struct CupidGBA *g, uint64_t before, unsigned expected, uint32_t address,
                          unsigned width, bool write) {
    uint64_t elapsed = cupid_timing_global_time(&g->timing) - before;
    if (elapsed != expected)
        fprintf(stderr,
                "backup %s address=%08" PRIX32 " width=%u elapsed=%" PRIu64 " expected=%u\n",
                write ? "write" : "read", address, width, elapsed, expected);
    CHECK(elapsed == expected);
}

/* All widths select one byte on the backup bus. The sequential hint cannot
 * shorten that strobe; a load reports its separate final internal cycle. */
static void data_accesses(struct CupidGBA *g, unsigned wait) {
    g->cpu.memory.timed = true;
    for (unsigned window = 0; window < 2; ++window)
        for (unsigned sequential = 0; sequential < 2; ++sequential)
            for (unsigned width = 1; width <= 4; width *= 2)
                for (unsigned write = 0; write < 2; ++write) {
                    const uint32_t address = windows[window] + 0x20;
                    g->cpu.memory.dataSequential = sequential != 0;
                    g->memory.savedata.data[0x20] = 0xA5;
                    int cycles = 0;
                    uint64_t before = cupid_timing_global_time(&g->timing);
                    if (write)
                        write_width(g, address, width, 0x1234565A, &cycles);
                    else
                        CHECK(read_width(g, address, width, &cycles) == repeated_byte(width));
                    check_elapsed(g, before, wait + 1, address, width, write != 0);
                    CHECK(cycles == (write ? 0 : 1));
                    CHECK(g->memory.savedata.data[0x20] == (write ? 0x5A : 0xA5));
                    ++observations;
                }
}

static void reset(void) {
    for (unsigned boot = 0; boot < 3; ++boot) {
        struct CupidGBA *g = machine(boot);
        for (unsigned repeated = 0; repeated < 2; ++repeated) {
            if (repeated)
                cupid_io_write(g, REG_WAITCNT, 3);
            reset_at(g, 0);
            CHECK(g->biosHLE == (boot != 2));
            CHECK(cupid_arm_instruction_address(&g->cpu) == (boot == 2   ? GBA_BASE_BIOS
                                                             : boot == 1 ? GBA_BASE_ROM0
                                                                         : GBA_BASE_EWRAM));
            data_accesses(g, 4);
            /* Rewriting the reset value must preserve the already-observed
             * duration in both windows, including after a previous setting. */
            cupid_io_write(g, REG_WAITCNT, 0);
            data_accesses(g, 4);
        }
        destroy(g);
    }
}

static void instructions(struct CupidGBA *g, int setting) {
    /* Each operation uses r1 as the backup address and r0 as the data.
     * Local instruction fetch costs one clock; a load adds one internal clock. */
    static const uint32_t arm[] = {0xE5D10000, 0xE1D100B0, 0xE5910000,
                                   0xE5C10000, 0xE1C100B0, 0xE5810000};
    static const uint16_t thumb[] = {0x7808, 0x8808, 0x6808, 0x7008, 0x8008, 0x6008};
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned window = 0; window < 2; ++window)
            for (unsigned operation = 0; operation < 6; ++operation) {
                reset_at(g, 0);
                if (setting >= 0)
                    cupid_io_write(g, REG_WAITCNT, (uint16_t)setting);
                const unsigned width = 1u << (operation % 3);
                const bool write = operation >= 3;
                const uint32_t address = windows[window] + 0x20;
                g->memory.savedata.data[0x20] = 0xA5;
                g->cpu.gprs[0] = 0x1234565A;
                g->cpu.gprs[1] = (int32_t)address;
                _CupidSetMode(&g->cpu, mode ? MODE_THUMB : MODE_ARM);
                g->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
                if (mode) {
                    const uint16_t code[] = {thumb[operation], 0x46C0, 0x46C0};
                    memcpy(g->memory.iwram, code, sizeof(code));
                    CupidThumbWritePC(&g->cpu);
                } else {
                    const uint32_t code[] = {arm[operation], 0xE1A00000, 0xE1A00000};
                    memcpy(g->memory.iwram, code, sizeof(code));
                    CupidARMWritePC(&g->cpu);
                }
                uint64_t before = cupid_timing_global_time(&g->timing);
                cupid_gba_step(g);
                const unsigned wait = setting < 0 ? 4 : waits[setting];
                check_elapsed(g, before, wait + (write ? 2 : 3), address, width, write);
                CHECK(g->memory.savedata.data[0x20] == (write ? 0x5A : 0xA5));
                CHECK((uint32_t)g->cpu.gprs[0] == (write ? 0x1234565Au : repeated_byte(width)));
                CHECK(cupid_arm_instruction_address(&g->cpu) == GBA_BASE_IWRAM + (mode ? 2 : 4));
                ++observations;
            }
}

static void cpu(void) {
    struct CupidGBA *g = machine(externalBios ? 2 : 0);
    reset_at(g, 0);
    for (unsigned setting = 0; setting < 4; ++setting) {
        cupid_io_write(g, REG_WAITCNT, (uint16_t)setting);
        data_accesses(g, waits[setting]);
    }
    for (int setting = -1; setting < 4; ++setting)
        instructions(g, setting);
    destroy(g);
}

static void dma_scene(struct CupidGBA *g, uint64_t origin, int setting, unsigned window,
                      unsigned width, bool write) {
    reset_at(g, origin);
    if (setting >= 0)
        cupid_io_write(g, REG_WAITCNT, (uint16_t)setting);
    const unsigned wait = setting < 0 ? 4 : waits[setting];
    const uint32_t backup = windows[window] + 0x20, internal = GBA_BASE_IWRAM + 0x100;
    g->memory.savedata.data[0x20] = 0xA5;
    write_width(g, internal, width, write ? 0x123456C4 : 0, NULL);
    write_width(g, internal + width, width, write ? 0x234567D5 : 0, NULL);
    cupid_io_write(g, REG_IE, IRQ_DMA3);
    cupid_io_write(g, REG_IME, 1);
    cupid_dma_write_sad(g, 3, write ? internal : backup);
    cupid_dma_write_dad(g, 3, write ? backup : internal);
    cupid_dma_write_cnt_lo(g, 3, 2);
    cupid_dma_write_cnt(g, 3,
                        (uint16_t)(0xC000 | (width == 4 ? 0x400 : 0) | (write ? 0x40 : 0x100)));
    /* Two request clocks, one head clock, then a backup byte strobe and an
     * internal strobe per unit. The second unit exercises sequential timing. */
    const unsigned first = 3 + wait + 2, final = first + wait + 2;
    for (unsigned clock = 1; clock <= final + 4; ++clock) {
        cupid_timing_tick(&g->timing, 1);
        if (write) {
            uint8_t expected = clock >= final ? 0xD5 : clock >= first ? 0xC4 : 0xA5;
            CHECK(g->memory.savedata.data[0x20] == expected);
        } else {
            uint32_t value = repeated_byte(width);
            CHECK(read_width(g, internal, width, NULL) == (clock >= first ? value : 0));
            CHECK(read_width(g, internal + width, width, NULL) == (clock >= final ? value : 0));
        }
        CHECK(g->memory.dmaRunning == (clock >= 2 && clock < final + 1));
        CHECK(g->memory.dma[3].enable == (clock < final));
        CHECK(g->irq.pendingIF == (clock >= final + 1 ? IRQ_DMA3 : 0));
        CHECK(g->irqf == (clock >= final + 2 ? IRQ_DMA3 : 0));
        CHECK(g->irq.available == (clock >= final + 3));
        CHECK(g->irq.line == (clock >= final + 4));
        if (clock >= final) {
            CHECK(g->memory.dma[3].busyUntil == origin + final);
            CHECK(g->memory.dma[3].nextCount == 0);
        }
    }
    CHECK(cupid_timing_global_time(&g->timing) == origin + final + 4);
    ++observations;
}

static void dma(void) {
    static const uint64_t origins[] = {0, INT32_MAX - 6u, UINT32_MAX - 6u};
    struct CupidGBA *g = machine(externalBios ? 2 : 0);
    for (unsigned epoch = 0; epoch < 3; ++epoch)
        for (int setting = -1; setting < 4; ++setting)
            for (unsigned window = 0; window < 2; ++window)
                for (unsigned width = 2; width <= 4; width *= 2)
                    for (unsigned write = 0; write < 2; ++write)
                        dma_scene(g, origins[epoch], setting, window, width, write != 0);
    destroy(g);
}

int main(int argc, char **argv) {
    CHECK(argc == 2 || argc == 3);
    if (argc == 3)
        externalBios = argv[2];
    if (!strcmp(argv[1], "reset"))
        RUN(reset);
    else if (!strcmp(argv[1], "cpu"))
        RUN(cpu);
    else if (!strcmp(argv[1], "dma"))
        RUN(dma);
    else
        return 2;
    printf("backup wait observations=%u\n", observations);
    return 0;
}
