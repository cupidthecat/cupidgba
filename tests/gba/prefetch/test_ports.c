/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_ports.c
 * @author    cupidthecat
 * @brief     Physical ARM word streams and sampled cartridge opcode ports.
 * @date      2026-11-17
 *
 * - Check logical A1 interworking against aligned Game Pak word transfers.
 * - Preserve GPIO and card-reader samples across pending and cached refills.
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

static const uint64_t epochs[] = {0, UINT32_MAX - UINT64_C(40), UINT64_C(0x300000000)};
static const uint32_t banks[] = {GBA_BASE_ROM0, GBA_BASE_ROM1, GBA_BASE_ROM2};
static const unsigned sequentialClocks[3][2] = {{3, 2}, {5, 2}, {9, 2}};

static struct CupidGBA *machine(unsigned epoch, unsigned bank, unsigned sequential) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epochs[epoch];
    gba->timing.masterCycles = (uint32_t)epochs[epoch];
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    gba->memory.rom = calloc(1, 0x4000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 0x4000;
    gba->memory.romImageSize = 0x4000;
    gba->memory.romMask = 0x3FFF;
    cupid_memory_update_waitstates(gba, (uint16_t)(0x4000 | sequential << (4 + bank * 3)));
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void arm_stream(struct CupidGBA *gba, uint32_t address) {
    _CupidSetMode(&gba->cpu, MODE_ARM);
    gba->cpu.memory.timed = true;
    gba->cpu.gprs[ARM_PC] = (int32_t)address;
    cupid_memory_set_active_region(&gba->cpu, address);
}

static uint32_t fetch_word(struct CupidGBA *gba, uint32_t address, bool sequential,
                           uint64_t *clocks) {
    uint64_t before = cupid_timing_global_time(&gba->timing);
    cupid_bus_fetch(&gba->cpu, address, 4, sequential);
    uint32_t opcode = CupidReadOpcode(&gba->cpu, address, 4);
    cupid_bus_finish(&gba->cpu);
    *clocks = cupid_timing_global_time(&gba->timing) - before;
    return opcode;
}

static void idle(struct CupidGBA *gba, unsigned cycles) {
    cupid_bus_advance(&gba->cpu, cycles, false);
    cupid_bus_finish(&gba->cpu);
}

static uint8_t input_pins(void *context) {
    return *(uint8_t *)context;
}

static void change_input(struct CupidTiming *timing, void *context, uint32_t late) {
    (void)timing;
    (void)late;
    *(uint8_t *)context = 9;
}

static void interrupt_stream_with_rom_data(struct CupidGBA *gba) {
    int cycles = 0;
    (void)cupid_memory_loadU16(&gba->cpu, GBA_BASE_ROM0 + 0x100, &cycles);
    gba->cpu.cycles += cycles;
    cupid_bus_finish(&gba->cpu);
    CHECK(!gba->memory.prefetchUnit.active);
}

static void gpio_port(struct CupidGBA *gba, uint8_t *input) {
    const uint8_t rom[] = {0x11, 0x22, 0x33, 0x44};
    memcpy(gba->memory.rom + 0xC0, rom, sizeof(rom));
    gba->memory.rom[0xCA] = 0xAA;
    gba->memory.rom[0xCB] = 0xBB;
    cupid_cartridge_set_input(gba, input_pins, input);
    CHECK(cupid_cartridge_gpio_write(gba, GBA_BASE_ROM0 + 0xC6, 4));
    CHECK(cupid_cartridge_gpio_write(gba, GBA_BASE_ROM0 + 0xC4, 4));
    CHECK(cupid_cartridge_gpio_write(gba, GBA_BASE_ROM0 + 0xC8, 1));
}

static void gpio_a1_streams_preserve_port_samples(void) {
    unsigned cases = 0;
    for (unsigned epoch = 0; epoch < 3; ++epoch)
        for (unsigned bank = 0; bank < 3; ++bank)
            for (unsigned sequential = 0; sequential < 2; ++sequential)
                for (unsigned phase = 0; phase < 3; ++phase) {
                    struct CupidGBA *gba = machine(epoch, bank, sequential);
                    uint8_t input = 1;
                    gpio_port(gba, &input);
                    const uint32_t base = banks[bank];
                    const unsigned half = sequentialClocks[bank][sequential];
                    const uint32_t first = phase == 2 ? base + 0xC6 : base + 0xC2;
                    arm_stream(gba, first);
                    uint64_t clocks;
                    if (phase == 2) {
                        /* The low GPIO halfword is sampled before the new pin
                         * level, even when A1 is retained by the ARM address. */
                        struct CupidTimingEvent change = {.context = &input,
                                                          .callback = change_input,
                                                          .name = "GPIO word sample",
                                                          .priority = 0x60};
                        cupid_timing_schedule(&gba->timing, &change, 6);
                        CHECK(fetch_word(gba, first, false, &clocks) == UINT32_C(0x00040005));
                        CHECK(input == 9 && clocks == 5 + half);
                        idle(gba, 8 * half);
                        CHECK(fetch_word(gba, base + 0xCA, false, &clocks) == UINT32_C(0xBBAA0001));
                        CHECK(clocks == 1);
                    } else {
                        CHECK(fetch_word(gba, first, false, &clocks) == UINT32_C(0x44332211));
                        CHECK(clocks == 5 + half);
                        if (phase == 0) {
                            /* Two physical halfwords for C4/C6 are queued
                             * before the input and direction registers change. */
                            idle(gba, 8 * half);
                            input = 9;
                            CHECK(cupid_cartridge_gpio_write(gba, GBA_BASE_ROM0 + 0xC6, 2));
                            CHECK(fetch_word(gba, base + 0xC6, false, &clocks) ==
                                  UINT32_C(0x00040005));
                            CHECK(clocks == 1);
                            CHECK(fetch_word(gba, base + 0xCA, true, &clocks) ==
                                  UINT32_C(0xBBAA0001));
                            CHECK(clocks == 1);
                        } else {
                            /* Only the first port halfword is ready. The
                             * remaining halfword must retain its bus phase. */
                            idle(gba, half + 1);
                            input = 9;
                            CHECK(cupid_cartridge_gpio_write(gba, GBA_BASE_ROM0 + 0xC6, 2));
                            CHECK(fetch_word(gba, base + 0xC6, false, &clocks) ==
                                  UINT32_C(0x00020005));
                            CHECK(clocks == half - 1);
                        }
                    }
                    uint16_t live;
                    CHECK(cupid_cartridge_gpio_read(gba, base + 0xC4, &live) &&
                          live == (phase == 2 ? 13 : 9));
                    /* An ordinary Game Pak data access cancels the stream;
                     * a later physical fetch observes the new pin value. */
                    interrupt_stream_with_rom_data(gba);
                    CHECK(fetch_word(gba, base + 0xC6, false, &clocks) ==
                          (phase == 2 ? UINT32_C(0x0004000D) : UINT32_C(0x00020009)));
                    CHECK(clocks == 5 + half);
                    destroy(gba);
                    ++cases;
                }
    CHECK(cases == 54);
    printf("PASS %u aligned GPIO stream/sampling/cancellation scenes\n", cases);
}

static void card_a1_streams_preserve_sensor_samples(void) {
    unsigned cases = 0;
    for (unsigned epoch = 0; epoch < 3; ++epoch)
        for (unsigned sequential = 0; sequential < 2; ++sequential)
            for (unsigned pending = 0; pending < 2; ++pending) {
                struct CupidGBA *gba = machine(epoch, 2, sequential);
                cupid_cartridge_enable_card_reader(gba, true);
                struct CupidCardReader *reader = &gba->cartridge.cardReader;
                reader->sensor[0] = 0x1122;
                reader->sensor[1] = 0x3344;
                reader->sensor[2] = 0x5566;
                reader->sensor[3] = 0x7788;
                const uint32_t address = UINT32_C(0x0DFC0002);
                const unsigned half = sequentialClocks[2][sequential];
                arm_stream(gba, address);
                uint64_t clocks;
                CHECK(fetch_word(gba, address, false, &clocks) == UINT32_C(0x33441122));
                CHECK(clocks == 5 + half);
                idle(gba, pending ? half + 1 : half * 8);
                reader->sensor[2] = 0xAABB;
                reader->sensor[3] = 0xCCDD;
                CHECK(fetch_word(gba, address + 4, false, &clocks) ==
                      (pending ? UINT32_C(0xCCDD5566) : UINT32_C(0x77885566)));
                CHECK(clocks == (pending ? half - 1 : 1));
                interrupt_stream_with_rom_data(gba);
                CHECK(fetch_word(gba, address + 4, false, &clocks) == UINT32_C(0xCCDDAABB));
                CHECK(clocks == 5 + half);
                destroy(gba);
                ++cases;
            }
    CHECK(cases == 12);
    printf("PASS %u aligned card-reader sampling/cancellation scenes\n", cases);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "gpio"))
        RUN(gpio_a1_streams_preserve_port_samples);
    else if (!strcmp(argv[1], "card"))
        RUN(card_a1_streams_preserve_sensor_samples);
    else
        CHECK(false);
    return 0;
}
