/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_rom_fetch.c
 * @author    cupidthecat
 * @brief     ROM opcode bounds, branch execution, mirrors and cartridge overlays.
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
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include <string.h>

static const uint64_t epochs[] = {0, UINT32_MAX - UINT64_C(17), UINT64_C(0x300000000)};
static const uint16_t waits[] = {0, 0x4317};
static const unsigned nonseq[2][3] = {{5, 5, 5}, {4, 5, 9}};
static const unsigned seq[2][3] = {{3, 5, 9}, {2, 5, 9}};
static const char *directory;
static unsigned scenes;

static struct CupidGBA *machine(bool attached, uint64_t epoch, unsigned wait) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (attached) {
        gba->memory.rom = calloc(1, 0x4000);
        CHECK(gba->memory.rom);
        gba->memory.romSize = gba->memory.romImageSize = 0x4000;
        gba->memory.romMask = 0x3FFF;
        const uint32_t nop = 0xE1A00000;
        for (unsigned i = 0; i < 0x4000; i += 4)
            memcpy(gba->memory.rom + i, &nop, sizeof(nop));
    }
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    cupid_memory_update_waitstates(gba, waits[wait]);
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void prime(struct CupidGBA *gba, uint32_t address, bool thumb, bool timed) {
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.memory.timed = timed;
    gba->cpu.gprs[ARM_PC] = (int32_t)address;
    if (thumb)
        CupidThumbWritePC(&gba->cpu);
    else
        CupidARMWritePC(&gba->cpu);
    gba->cpu.memory.timed = false;
}

static void pipeline(struct CupidGBA *gba, uint32_t first, uint32_t second) {
    if (gba->cpu.prefetch[0] != first || gba->cpu.prefetch[1] != second)
        fprintf(stderr,
                "scene=%u pc=%08" PRIX32 " fetched=%08" PRIX32 "/%08" PRIX32 " expected=%08" PRIX32
                "/%08" PRIX32 "\n",
                scenes, (uint32_t)gba->cpu.gprs[ARM_PC], gba->cpu.prefetch[0], gba->cpu.prefetch[1],
                first, second);
    CHECK(gba->cpu.prefetch[0] == first && gba->cpu.prefetch[1] == second);
    const uint32_t input = gba->cpu.executionMode == MODE_THUMB ? second | second << 16 : second;
    CHECK(cupid_memory_open_bus(&gba->cpu) == input);
    CHECK(!gba->memory.busBusy && !gba->memory.dmaRunning && !gba->irq.cpuClockPaused);
}

static void missing_rom_refills_use_address_inputs(void) {
    for (unsigned attached = 0; attached < 2; ++attached)
        for (unsigned region = 8; region < 14; ++region)
            for (unsigned thumb = 0; thumb < 2; ++thumb)
                for (unsigned wait = 0; wait < 2; ++wait)
                    for (unsigned epoch = 0; epoch < 3; ++epoch)
                        for (unsigned low = 0; low < 4; ++low)
                            for (unsigned timed = 0; timed < 2; ++timed) {
                                struct CupidGBA *gba = machine(attached != 0, epochs[epoch], wait);
                                const uint32_t target = (region << 24) + 0x82468 + low;
                                const uint64_t before = cupid_timing_global_time(&gba->timing);
                                const unsigned bank = (region - 8) / 2;
                                prime(gba, target, thumb != 0, timed != 0);
                                if (thumb)
                                    pipeline(gba, 0x1234 + (low >> 1), 0x1235 + (low >> 1));
                                else
                                    pipeline(gba, 0x12351234, 0x12371236);
                                CHECK((uint32_t)gba->cpu.gprs[ARM_PC] ==
                                      (target & ~1u) + (thumb ? 2u : 4u));
                                CHECK(cupid_arm_instruction_address(&gba->cpu) ==
                                      (target & ~(thumb ? 1u : 3u)));
                                CHECK(cupid_timing_global_time(&gba->timing) - before ==
                                      (timed ? nonseq[wait][bank] +
                                                   (thumb ? 1u : 3u) * seq[wait][bank]
                                             : 0));
                                destroy(gba);
                                ++scenes;
                            }
}

struct Issued {
    uint32_t address[3], opcode[3];
    unsigned count;
};

static void observe(const struct CupidARM *cpu, void *context) {
    struct Issued *issued = context;
    CHECK(issued->count < 3);
    issued->address[issued->count] = cupid_arm_instruction_address(cpu);
    issued->opcode[issued->count++] = cpu->prefetch[0];
}

static void branches_execute_the_sampled_instructions(void) {
    for (unsigned region = 8; region < 14; ++region)
        for (unsigned thumb = 0; thumb < 2; ++thumb)
            for (unsigned wait = 0; wait < 2; ++wait)
                for (unsigned epoch = 0; epoch < 3; ++epoch)
                    for (unsigned a1 = 0; a1 < (thumb ? 1u : 2u); ++a1) {
                        struct CupidGBA *gba = machine(true, epochs[epoch], wait);
                        const uint32_t source[] = {0xE12FFF14, 0xE1A00000, 0xE1A00000};
                        memcpy(gba->memory.iwram, source, sizeof(source));
                        const uint32_t target =
                            (region << 24) + (thumb ? 0x84024 : 0x9C500) + a1 * 2;
                        gba->cpu.gprs[0] = (int32_t)0xAABBCCDD;
                        gba->cpu.gprs[1] = 0x12345678;
                        gba->cpu.gprs[4] = (int32_t)(target | thumb);
                        prime(gba, GBA_BASE_IWRAM, false, true);
                        struct Issued issued = {0};
                        gba->cpu.instructionObserver = observe;
                        gba->cpu.instructionObserverContext = &issued;
                        const uint64_t before = cupid_timing_global_time(&gba->timing);
                        cupid_gba_step(gba);
                        pipeline(gba, thumb ? 0x2012 : 0xE281E280, thumb ? 0x2013 : 0xE283E282);
                        const unsigned bank = (region - 8) / 2;
                        CHECK(cupid_timing_global_time(&gba->timing) - before ==
                              1u + nonseq[wait][bank] + (thumb ? 1u : 3u) * seq[wait][bank]);
                        CHECK(issued.count == 1 && issued.address[0] == GBA_BASE_IWRAM &&
                              issued.opcode[0] == source[0]);
                        CHECK(gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM));
                        const uint32_t control = (uint32_t)gba->cpu.cpsr.packed;
                        cupid_gba_step(gba);
                        CHECK(issued.count == 2 && issued.address[1] == (target & ~3u));
                        CHECK(issued.opcode[1] == (thumb ? 0x2012u : 0xE281E280u));
                        if (thumb) {
                            CHECK(gba->cpu.gprs[0] == 0x12);
                            cupid_gba_step(gba);
                            CHECK(gba->cpu.gprs[0] == 0x13 && issued.count == 3);
                            CHECK(issued.address[2] == target + 2 && issued.opcode[2] == 0x2013);
                        } else {
                            CHECK(gba->cpu.gprs[ARM_LR] == 0x12345680);
                            CHECK((uint32_t)gba->cpu.cpsr.packed == control);
                        }
                        destroy(gba);
                        ++scenes;
                    }
}

static void sequential_fetches_cross_the_image_end(void) {
    for (unsigned region = 8; region < 14; region += 2)
        for (unsigned thumb = 0; thumb < 2; ++thumb)
            for (unsigned wait = 0; wait < 2; ++wait)
                for (unsigned epoch = 0; epoch < 3; ++epoch)
                    for (unsigned a1 = 0; a1 < (thumb ? 1u : 2u); ++a1)
                        for (unsigned entry = 1; entry <= 2; ++entry) {
                            struct CupidGBA *gba = machine(true, epochs[epoch], wait);
                            const unsigned width = thumb ? 2 : 4;
                            if (thumb) {
                                const uint16_t nops[] = {0x46C0, 0x46C0};
                                memcpy(gba->memory.rom + 0x3FFC, nops, sizeof(nops));
                            }
                            const uint32_t target =
                                (region << 24) + 0x4000 - entry * width + a1 * 2;
                            prime(gba, target, thumb != 0, true);
                            const uint32_t nop = thumb ? 0x46C0 : 0xE1A00000;
                            pipeline(gba, nop, entry == 2 ? nop : thumb ? 0x2000 : 0x20012000);
                            cupid_gba_step(gba);
                            if (entry == 2) {
                                pipeline(gba, nop, thumb ? 0x2000 : 0x20012000);
                                cupid_gba_step(gba);
                            }
                            pipeline(gba, thumb ? 0x2000 : 0x20012000, thumb ? 0x2001 : 0x20032002);
                            /* A branch back into the same window must restore image bytes. */
                            prime(gba, (region << 24) + 0x100, false, true);
                            pipeline(gba, 0xE1A00000, 0xE1A00000);
                            destroy(gba);
                            ++scenes;
                        }
}

static void window_crossings_select_new_bytes_and_waits(void) {
    const uint32_t first[] = {0xE3A0502A, 0xE3A0603B};
    for (unsigned boundary = 0; boundary < 2; ++boundary)
        for (unsigned thumb = 0; thumb < 2; ++thumb)
            for (unsigned wait = 0; wait < 2; ++wait)
                for (unsigned epoch = 0; epoch < 3; ++epoch)
                    for (unsigned a1 = 0; a1 < (thumb ? 1u : 2u); ++a1) {
                        struct CupidGBA *gba = machine(true, epochs[epoch], wait);
                        memcpy(gba->memory.rom, first, sizeof(first));
                        const uint32_t next = 0x0A000000 + boundary * GBA_SIZE_ROM;
                        const uint32_t target = next - (thumb ? 2u : 4u) + a1 * 2;
                        const uint64_t before = cupid_timing_global_time(&gba->timing);
                        prime(gba, target, thumb != 0, true);
                        pipeline(gba, thumb ? 0xFFFF : 0xFFFFFFFE, thumb ? 0x502A : first[0]);
                        const unsigned expected =
                            nonseq[wait][boundary] + nonseq[wait][boundary + 1] +
                            (thumb ? 0 : seq[wait][boundary] + seq[wait][boundary + 1]);
                        CHECK(cupid_timing_global_time(&gba->timing) - before == expected);
                        if (!thumb) {
                            /* The absent word has condition NV. The next two real words issue. */
                            cupid_gba_step(gba);
                            pipeline(gba, first[0], first[1]);
                            cupid_gba_step(gba);
                            CHECK(gba->cpu.gprs[5] == 0x2A);
                            cupid_gba_step(gba);
                            CHECK(gba->cpu.gprs[6] == 0x3B);
                        }
                        destroy(gba);
                        ++scenes;
                    }
}

static void complete_images_wrap_from_the_odd_window(void) {
    const uint32_t first[] = {0xE3A0502A, 0xE3A0603B};
    for (unsigned boundary = 0; boundary < 2; ++boundary)
        for (unsigned thumb = 0; thumb < 2; ++thumb) {
            struct CupidGBA *gba = machine(false, epochs[1], 1);
            gba->memory.rom = calloc(1, GBA_SIZE_ROM);
            CHECK(gba->memory.rom);
            gba->memory.romSize = gba->memory.romImageSize = GBA_SIZE_ROM;
            gba->memory.romMask = GBA_SIZE_ROM - 1;
            gba->memory.activeRegion = -1;
            const uint32_t tail = thumb ? 0x46C046C0 : 0xE1A00000;
            memcpy(gba->memory.rom, first, sizeof(first));
            memcpy(gba->memory.rom + GBA_SIZE_ROM - 4, &tail, sizeof(tail));
            const uint32_t next = 0x0A000000 + boundary * GBA_SIZE_ROM;
            prime(gba, next - (thumb ? 2u : 4u), thumb != 0, true);
            pipeline(gba, thumb ? 0x46C0 : tail, thumb ? 0x502A : first[0]);
            if (!thumb) {
                cupid_gba_step(gba);
                pipeline(gba, first[0], first[1]);
                cupid_gba_step(gba);
                CHECK(gba->cpu.gprs[5] == 0x2A);
            }
            destroy(gba);
            ++scenes;
        }
}

static void write_image(const char *path, const void *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    CHECK(file);
    if (size)
        CHECK(fwrite(bytes, 1, size, file) == size);
    CHECK(fclose(file) == 0);
}

static void loaded_views_keep_padding_and_mirrors(void) {
    const unsigned sizes[] = {1, 2, 3, 4, 8, 0x100000};
    const uint8_t prefix[] = {0x2A, 0x50, 0xA0, 0xE3, 0x3B, 0x60, 0xA0, 0xE3};
    const uint32_t armFirst[] = {0x2A, 0x502A, 0x00A0502A, 0xE3A0502A, 0xE3A0502A, 0xE3A0502A};
    const uint32_t armSecond[] = {0x00030002, 0x00030002, 0, 0x00030002, 0xE3A0603B, 0xE3A0603B};
    const uint32_t thumbFirst[] = {0x2A, 0x502A, 0x502A, 0x502A, 0x502A, 0x502A};
    const uint32_t thumbSecond[] = {1, 1, 0xA0, 0xE3A0, 0xE3A0, 0xE3A0};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) {
        char path[1024];
        const int length =
            snprintf(path, sizeof(path), "%s/rom-fetch-image-%u.gba", directory, sizes[i]);
        CHECK(length > 0 && (unsigned)length < sizeof(path));
        uint8_t *image = calloc(1, sizes[i]);
        CHECK(image);
        memcpy(image, prefix, sizes[i] < sizeof(prefix) ? sizes[i] : sizeof(prefix));
        FILE *file = fopen(path, "wb");
        CHECK(file && fwrite(image, 1, sizes[i], file) == sizes[i] && fclose(file) == 0);
        free(image);
        struct CupidGBA *gba = machine(false, 0, 0);
        CHECK(cupid_load_rom(gba, path));
        CHECK(remove(path) == 0);
        CHECK(gba->memory.romImageSize == sizes[i]);
        CHECK(gba->memory.romSize == (sizes[i] == 3          ? GBA_SIZE_ROM
                                      : sizes[i] == 0x100000 ? 0x400000u
                                                             : sizes[i]));
        for (unsigned mirror = 0; mirror < 3; ++mirror) {
            const uint32_t address = GBA_BASE_ROM0 + mirror * GBA_SIZE_ROM;
            prime(gba, address, false, true);
            pipeline(gba, armFirst[i], armSecond[i]);
            prime(gba, address, true, true);
            pipeline(gba, thumbFirst[i], thumbSecond[i]);
            if (sizes[i] == 0x100000)
                for (unsigned copy = 1; copy < 4; ++copy) {
                    prime(gba, address + copy * 0x100000, false, true);
                    pipeline(gba, armFirst[i], armSecond[i]);
                }
            ++scenes;
        }
        destroy(gba);
    }
}

static void replacement_rebinds_the_live_opcode_view(void) {
    const uint32_t oldWords[] = {0xE3A0502A, 0xE3A0603B, 0xE1A00000, 0xE1A00000};
    const uint32_t smallWord = 0xE3A0505A;
    const uint32_t largeWords[] = {0xE3A05061, 0xE3A06072, 0xE3A07083, 0xE3A08094};
    for (unsigned mirror = 0; mirror < 3; ++mirror)
        for (unsigned thumb = 0; thumb < 2; ++thumb)
            for (unsigned absent = 0; absent < 2; ++absent) {
                char path[1024];
                const int length =
                    snprintf(path, sizeof(path), "%s/rom-fetch-replacement-%u-%u-%u.gba", directory,
                             mirror, thumb, absent);
                CHECK(length > 0 && (unsigned)length < sizeof(path));
                struct CupidGBA *gba = machine(false, epochs[1], 1);
                write_image(path, oldWords, sizeof(oldWords));
                CHECK(cupid_load_rom(gba, path));
                const uint32_t base = GBA_BASE_ROM0 + mirror * GBA_SIZE_ROM;
                prime(gba, base + (absent ? 0x1000 : 0), thumb != 0, true);
                const int32_t pc = gba->cpu.gprs[ARM_PC];
                const uint32_t first = gba->cpu.prefetch[0], second = gba->cpu.prefetch[1];
                write_image(path, &smallWord, sizeof(smallWord));
                CHECK(cupid_load_rom(gba, path));
                /* Read immediately after installation: a branch would hide a
                 * stale cached span by setting up the execution region again. */
                CHECK(CupidReadOpcode(&gba->cpu, base, 4) == smallWord);
                CHECK(CupidReadOpcode(&gba->cpu, base + 4, 4) == 0x00030002);
                CHECK(CupidReadOpcode(&gba->cpu, base + 4, 2) == 2);
                CHECK(CupidReadOpcode(&gba->cpu, base + 0x1000, 4) == 0x08010800);
                CHECK(gba->cpu.gprs[ARM_PC] == pc && gba->cpu.prefetch[0] == first &&
                      gba->cpu.prefetch[1] == second);
                write_image(path, largeWords, sizeof(largeWords));
                CHECK(cupid_load_rom(gba, path));
                CHECK(CupidReadOpcode(&gba->cpu, base + 4, 4) == largeWords[1]);
                CHECK(CupidReadOpcode(&gba->cpu, base + 12, 4) == largeWords[3]);
                CHECK(CupidReadOpcode(&gba->cpu, base + 12, 2) == 0x8094);
                CHECK(CupidReadOpcode(&gba->cpu, base + 16, 4) == 0x00090008);
                CHECK(gba->cpu.gprs[ARM_PC] == pc && gba->cpu.prefetch[0] == first &&
                      gba->cpu.prefetch[1] == second);
                const uint8_t *allocation = gba->memory.rom;
                write_image(path, NULL, 0);
                CHECK(!cupid_load_rom(gba, path));
                CHECK(remove(path) == 0);
                CHECK(gba->memory.rom == allocation && gba->memory.romSize == sizeof(largeWords) &&
                      gba->memory.romMask == sizeof(largeWords) - 1);
                CHECK(CupidReadOpcode(&gba->cpu, base, 4) == largeWords[0]);
                CHECK(CupidReadOpcode(&gba->cpu, base + 12, 4) == largeWords[3]);
                CHECK(CupidReadOpcode(&gba->cpu, base + 16, 4) == 0x00090008);
                CHECK(gba->cpu.gprs[ARM_PC] == pc && gba->cpu.prefetch[0] == first &&
                      gba->cpu.prefetch[1] == second);
                destroy(gba);
                ++scenes;
            }
}

static uint8_t input_pins(void *context) {
    return *(const uint8_t *)context;
}

static void overlays_and_lifecycle_keep_fetch_ownership(void) {
    for (unsigned epoch = 0; epoch < 3; ++epoch) {
        struct CupidGBA *gba = machine(true, epochs[epoch], 1);
        /* Keep the board attached while its readable ports lie beyond this image. */
        gba->memory.romSize = gba->memory.romImageSize = 0x80;
        gba->memory.romMask = 0x7F;
        gba->memory.activeRegion = -1;
        uint8_t pins = 6;
        cupid_cartridge_set_input(gba, input_pins, &pins);
        CHECK(cupid_cartridge_gpio_write(gba, 0x080000C8, 1));
        prime(gba, 0x080000C6, false, true);
        pipeline(gba, 0x00000006, 0x00650001);
        CHECK(cupid_cartridge_gpio_write(gba, 0x080000C8, 0));
        CHECK(!gba->cpu.memory.readOpcode);
        prime(gba, 0x080000C6, false, true);
        pipeline(gba, 0x00630062, 0x00650064);
        cupid_cartridge_enable_card_reader(gba, true);
        prime(gba, 0x0D082468, true, true);
        pipeline(gba, 0x1234, 0x1235);
        cupid_cartridge_enable_card_reader(gba, false);
        CHECK(!gba->cpu.memory.readOpcode);
        prime(gba, 0x0D082468, true, true);
        pipeline(gba, 0x1234, 0x1235);
        cupid_cartridge_reset_bus(gba);
        prime(gba, 0x08000000, false, true);
        pipeline(gba, 0xE1A00000, 0xE1A00000);
        prime(gba, 0x08082468, false, true);
        pipeline(gba, 0x12351234, 0x12371236);
        cupid_gba_reset(gba);
        prime(gba, 0x08082468, false, true);
        pipeline(gba, 0x12351234, 0x12371236);
        destroy(gba);
        ++scenes;
    }
}

int main(int argc, char **argv) {
    CHECK(argc >= 2);
    if (!strcmp(argv[1], "images")) {
        CHECK(argc == 3);
        directory = argv[2];
        RUN(loaded_views_keep_padding_and_mirrors);
        RUN(replacement_rebinds_the_live_opcode_view);
    } else {
        CHECK(argc == 2);
        if (!strcmp(argv[1], "refill"))
            RUN(missing_rom_refills_use_address_inputs);
        else if (!strcmp(argv[1], "branches"))
            RUN(branches_execute_the_sampled_instructions);
        else if (!strcmp(argv[1], "boundaries"))
            RUN(sequential_fetches_cross_the_image_end);
        else if (!strcmp(argv[1], "windows")) {
            RUN(window_crossings_select_new_bytes_and_waits);
            RUN(complete_images_wrap_from_the_odd_window);
        } else if (!strcmp(argv[1], "ownership"))
            RUN(overlays_and_lifecycle_keep_fetch_ownership);
        else
            CHECK(false);
    }
    printf("PASS ROM opcode %s scenes=%u\n", argv[1], scenes);
    return 0;
}
