/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_joybus_readback.c
 * @author    cupidthecat
 * @brief     Validate readable JOY transmit data through CPU and DMA bus ownership.
 * @date      2026-11-16
 *
 * - Read the staged JOY transmit word through every aligned byte and halfword lane.
 * - Execute real ARM and Thumb loads and preserve send data across JOY commands.
 * - Check timed CPU input publication and scheduled DMA reads independently.
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

static const uint64_t origins[] = {0, INT32_MAX - UINT64_C(9), UINT32_MAX - UINT64_C(9),
                                   UINT64_C(0x300000000)};

/* Start each scene with a fresh JOY-mode machine and an independent clock epoch. */
static struct CupidGBA *machine(uint64_t clock) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    gba->audio.lastSample = gba->audio.sampleOrigin = clock;
    gba->audio.nextSample = clock + 512;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    cupid_io_write(gba, REG_RCNT, 0xC000);
    CHECK(cupid_serial_mode(&gba->serial) == SERIAL_JOY);
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

/* Use the CPU data path for stores, including byte-lane merges and full words. */
static void write_port(struct CupidGBA *gba, uint32_t address, unsigned width, uint32_t value) {
    if (width == 1)
        cupid_memory_store8(&gba->cpu, address, (int8_t)value, NULL);
    else if (width == 2)
        cupid_memory_store16(&gba->cpu, address, (int16_t)value, NULL);
    else {
        CHECK(width == 4);
        cupid_memory_store32(&gba->cpu, address, (int32_t)value, NULL);
    }
}

static uint32_t read_port(struct CupidGBA *gba, uint32_t address, unsigned width, int *cycles) {
    if (width == 1)
        return cupid_memory_load8(&gba->cpu, address, cycles);
    if (width == 2)
        return cupid_memory_load16(&gba->cpu, address, cycles);
    CHECK(width == 4);
    return cupid_memory_load32(&gba->cpu, address, cycles);
}

/* JOY_TRANS is two little-endian halfwords; these aligned width reads select
 * the same lanes as a general CPU data read, independent of the I/O handler. */
static uint32_t selected_value(uint32_t word, unsigned lane, unsigned width) {
    if (width == 1)
        return (word >> (lane * 8)) & 0xFFu;
    if (width == 2)
        return (word >> ((lane & 2u) * 8)) & 0xFFFFu;
    CHECK(width == 4 && !lane);
    return word;
}

/* An aligned CPU store replaces only the addressed lane(s) in its shared
 * register word. Keep the expected value independent of the I/O store path. */
static uint32_t merged_word(uint32_t oldWord, uint32_t value, unsigned lane, unsigned width) {
    const uint32_t mask = width == 4 ? UINT32_MAX : (UINT32_C(1) << (width * 8)) - 1u;
    const unsigned shift = lane * 8;
    return (oldWord & ~(mask << shift)) | ((value & mask) << shift);
}

/* The existing CPU-input model repeats narrow I/O samples; the guest register
 * result remains the selected lane, without altering the local IWRAM banks. */
static uint32_t sampled_input(uint32_t word, unsigned lane, unsigned width) {
    uint32_t selected = selected_value(word, lane, width);
    if (width == 1)
        return selected * UINT32_C(0x01010101);
    if (width == 2)
        return selected | selected << 16;
    return selected;
}

/* Verify every stored byte remains available through both 16-bit registers,
 * all four byte lanes and the complete aligned 32-bit register pair. */
static void check_word_reads(struct CupidGBA *gba, uint32_t value) {
    const uint32_t base = GBA_BASE_IO + REG_JOY_TRANS_LO;
    CHECK(cupid_io_read(gba, REG_JOY_TRANS_LO) == (value & 0xFFFFu));
    CHECK(cupid_io_read(gba, REG_JOY_TRANS_HI) == (value >> 16));
    for (unsigned lane = 0; lane < 4; ++lane)
        CHECK(read_port(gba, base + lane, 1, NULL) == selected_value(value, lane, 1));
    for (unsigned lane = 0; lane < 4; lane += 2)
        CHECK(read_port(gba, base + lane, 2, NULL) == selected_value(value, lane, 2));
    CHECK(read_port(gba, base, 4, NULL) == value);
}

/* A read must use the addressed stored byte even after a partial write changes
 * only one lane. Peer-ready status is independent of CPU readback. */
static void registers(void) {
    const uint32_t base = GBA_BASE_IO + REG_JOY_TRANS_LO;
    const uint32_t initial[] = {UINT32_C(0x10293847), UINT32_C(0xA5C396F0)};
    unsigned scenes = 0;
    for (unsigned seed = 0; seed < 2; ++seed)
        for (unsigned width = 1; width <= 4; width *= 2)
            for (unsigned lane = 0; lane < 4; lane += width)
                for (unsigned epoch = 0; epoch < 4; ++epoch) {
                    struct CupidGBA *gba = machine(origins[epoch]);
                    write_port(gba, base, 4, initial[seed]);
                    CHECK(cupid_io_read(gba, REG_JOYSTAT) == 8);
                    const uint32_t expected =
                        merged_word(initial[seed], UINT32_C(0x89ABCDEF), lane, width);
                    write_port(gba, base + lane, width, UINT32_C(0x89ABCDEF));
                    CHECK(gba->memory.io[REG_JOY_TRANS_LO >> 1] == (uint16_t)expected);
                    CHECK(gba->memory.io[REG_JOY_TRANS_HI >> 1] == (uint16_t)(expected >> 16));
                    check_word_reads(gba, expected);
                    CHECK(cupid_io_read(gba, REG_JOYSTAT) == 8);
                    destroy(gba);
                    ++scenes;
                }
    CHECK(scenes == 56);
    printf("PASS %u JOY transmit partial-store and read-lane scenes\n", scenes);
}

/* Run actual ARM/Thumb memory instructions rather than calling the I/O handler
 * alone. The first path loads different bytes; the second stores and reloads. */
static void guest(void) {
    static const uint32_t armStore[] = {0xE5C10000, 0xE1C100B0, 0xE5810000};
    static const uint32_t armLoad[] = {0xE5D10000, 0xE1D100B0, 0xE5910000};
    static const uint16_t thumbStore[] = {0x7008, 0x8008, 0x6008};
    static const uint16_t thumbLoad[] = {0x7808, 0x8808, 0x6808};
    const uint32_t base = GBA_BASE_IO + REG_JOY_TRANS_LO;
    unsigned scenes = 0;
    for (unsigned thumb = 0; thumb < 2; ++thumb)
        for (unsigned widthIndex = 0; widthIndex < 3; ++widthIndex) {
            const unsigned width = 1u << widthIndex;
            for (unsigned lane = 0; lane < 4; lane += width)
                for (unsigned writeFirst = 0; writeFirst < 2; ++writeFirst)
                    for (unsigned epoch = 0; epoch < 2; ++epoch) {
                        struct CupidGBA *gba = machine(origins[epoch * 2]);
                        const uint32_t firstWord =
                            writeFirst ? UINT32_C(0x10293847) : UINT32_C(0x89ABCDEF);
                        write_port(gba, base, 4, firstWord);
                        const unsigned instructionWidth = thumb ? 2 : 4;
                        if (thumb) {
                            const uint16_t program[] = {writeFirst ? thumbStore[widthIndex]
                                                                   : thumbLoad[widthIndex],
                                                        thumbLoad[widthIndex]};
                            memcpy(gba->memory.iwram, program, sizeof(program));
                        } else {
                            const uint32_t program[] = {writeFirst ? armStore[widthIndex]
                                                                   : armLoad[widthIndex],
                                                        armLoad[widthIndex]};
                            memcpy(gba->memory.iwram, program, sizeof(program));
                        }
                        _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
                        gba->cpu.gprs[0] =
                            (int32_t)(writeFirst ? UINT32_C(0x89ABCDEF) : UINT32_C(0x315AE297));
                        gba->cpu.gprs[1] = (int32_t)(base + lane);
                        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
                        if (thumb)
                            CupidThumbWritePC(&gba->cpu);
                        else
                            CupidARMWritePC(&gba->cpu);
                        cupid_gba_step(gba);
                        if (writeFirst)
                            cupid_gba_step(gba);
                        const uint32_t stored =
                            writeFirst ? merged_word(firstWord, UINT32_C(0x89ABCDEF), lane, width)
                                       : firstWord;
                        const uint32_t expected = selected_value(stored, lane, width);
                        CHECK((uint32_t)gba->cpu.gprs[0] == expected);
                        check_word_reads(gba, stored);
                        CHECK((cupid_io_read(gba, REG_JOYSTAT) & 8) != 0);
                        CHECK((uint32_t)gba->cpu.gprs[ARM_PC] ==
                              GBA_BASE_IWRAM + instructionWidth * (writeFirst ? 3u : 2u));
                        destroy(gba);
                        ++scenes;
                    }
        }
    CHECK(scenes == 56);
    printf("PASS %u actual ARM/Thumb JOY transmit load and store/load scenes\n", scenes);
}

/* Peer acknowledgement clears ready without clearing the staged data. Joy mode
 * owns the send port: other serial modes retain their existing zero readback. */
static void lifecycle(void) {
    const uint32_t base = GBA_BASE_IO + REG_JOY_TRANS_LO;
    const uint32_t words[] = {UINT32_C(0x89ABCDEF), UINT32_C(0x01234567), UINT32_C(0xF0C39A5E)};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned pattern = 0; pattern < 3; ++pattern) {
            struct CupidGBA *gba = machine(origins[epoch]);
            cupid_io_write(gba, REG_JOYSTAT, 0x30);
            write_port(gba, base, 4, words[pattern]);
            CHECK(cupid_io_read(gba, REG_JOYSTAT) == 0x38);
            uint8_t code = SERIAL_JOY_STATUS, reply[5] = {0};
            CHECK(cupid_serial_joybus_command(&gba->serial, &code, 1, reply, sizeof(reply)) == 3);
            CHECK(reply[0] == 0 && reply[1] == 4 && reply[2] == 0x38);

            /* Receive a different word before reading transmit data. A JOY
             * transmit read must never acknowledge the independent receive
             * flag or borrow the incoming buffer in place of its own data. */
            const uint8_t incoming[] = {SERIAL_JOY_WRITE, 0x12, 0x34, 0x56, 0x78};
            CHECK(cupid_serial_joybus_command(&gba->serial, incoming, sizeof(incoming), reply,
                                              sizeof(reply)) == 1);
            CHECK(reply[0] == 0x3A && cupid_io_read(gba, REG_JOYSTAT) == 0x3A);
            CHECK(cupid_io_read(gba, REG_JOYCNT) == 2);
            CHECK(gba->memory.io[REG_JOY_RECV_LO >> 1] == 0x3412);
            CHECK(gba->memory.io[REG_JOY_RECV_HI >> 1] == 0x7856);
            check_word_reads(gba, words[pattern]);
            CHECK(cupid_io_read(gba, REG_JOYSTAT) == 0x3A && cupid_io_read(gba, REG_JOYCNT) == 2);
            code = SERIAL_JOY_READ;
            CHECK(cupid_serial_joybus_command(&gba->serial, &code, 1, reply, sizeof(reply)) == 5);
            for (unsigned lane = 0; lane < 4; ++lane)
                CHECK(reply[lane] == selected_value(words[pattern], lane, 1));
            CHECK(reply[4] == 0x3A && cupid_io_read(gba, REG_JOYSTAT) == 0x32);
            CHECK(cupid_io_read(gba, REG_JOYCNT) == 6);
            check_word_reads(gba, words[pattern]);
            CHECK(cupid_io_read(gba, REG_JOYSTAT) == 0x32);
            CHECK(cupid_io_read(gba, REG_JOYCNT) == 6);

            /* A peer reset request marks its own event while retaining both
             * data buffers. A later serial-console reset clears them. */
            code = SERIAL_JOY_RESET;
            CHECK(cupid_serial_joybus_command(&gba->serial, &code, 1, reply, sizeof(reply)) == 3);
            CHECK(reply[0] == 0 && reply[1] == 4 && reply[2] == 0x32);
            CHECK(cupid_io_read(gba, REG_JOYSTAT) == 0x32 && cupid_io_read(gba, REG_JOYCNT) == 7);
            check_word_reads(gba, words[pattern]);
            CHECK(gba->memory.io[REG_JOY_RECV_LO >> 1] == 0x3412);
            CHECK(gba->memory.io[REG_JOY_RECV_HI >> 1] == 0x7856);
            code = SERIAL_JOY_READ;
            CHECK(cupid_serial_joybus_command(&gba->serial, &code, 1, reply, sizeof(reply)) == 5);
            CHECK(reply[4] == 0x32 && cupid_io_read(gba, REG_JOYSTAT) == 0x32);
            for (unsigned lane = 0; lane < 4; ++lane)
                CHECK(reply[lane] == selected_value(words[pattern], lane, 1));

            /* Only these non-JOY modes are within the established read policy. */
            static const uint16_t control[] = {0, 0x1000, 0x2000, 0x3000, 0};
            for (unsigned mode = 0; mode < 5; ++mode) {
                cupid_io_write(gba, REG_RCNT, mode == 4 ? 0x8000 : 0);
                cupid_io_write(gba, REG_SIOCNT, control[mode]);
                CHECK(cupid_serial_mode(&gba->serial) != SERIAL_JOY);
                CHECK(read_port(gba, base, 4, NULL) == 0);
                CHECK(read_port(gba, base, 2, NULL) == 0);
                CHECK(read_port(gba, base + 2, 2, NULL) == 0);
                cupid_io_write(gba, REG_RCNT, 0xC000);
                CHECK(cupid_serial_mode(&gba->serial) == SERIAL_JOY);
                check_word_reads(gba, words[pattern]);
                CHECK(cupid_io_read(gba, REG_JOYSTAT) == 0x32 &&
                      cupid_io_read(gba, REG_JOYCNT) == 7);
            }
            CHECK(cupid_io_read(gba, REG_JOY_RECV_LO) == 0x3412);
            CHECK(cupid_io_read(gba, REG_JOY_RECV_HI) == 0x7856);
            CHECK(cupid_io_read(gba, REG_JOYSTAT) == 0x30);
            cupid_serial_reset(&gba->serial);
            cupid_io_write(gba, REG_RCNT, 0xC000);
            CHECK(cupid_serial_mode(&gba->serial) == SERIAL_JOY);
            check_word_reads(gba, 0);
            CHECK(!cupid_io_read(gba, REG_JOYSTAT) && !cupid_io_read(gba, REG_JOYCNT));
            destroy(gba);
            ++scenes;
        }
    CHECK(scenes == 12);
    printf("PASS %u JOY peer-read, mode-isolation and reset scenes\n", scenes);
}

/* An observer uses the readable guest value without publishing CPU input. A
 * timed or legacy counted CPU read publishes its selected port lanes once. */
static void input(void) {
    const uint32_t base = GBA_BASE_IO + REG_JOY_TRANS_LO;
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned width = 1; width <= 4; width *= 2)
            for (unsigned lane = 0; lane < 4; lane += width)
                for (unsigned timed = 0; timed < 2; ++timed) {
                    struct CupidGBA *gba = machine(origins[epoch]);
                    write_port(gba, base, 4, UINT32_C(0x89ABCDEF));
                    gba->memory.openBus = UINT32_C(0x315AE297);
                    gba->memory.inputBusValid = true;
                    gba->memory.lastWasDMA = true;
                    gba->memory.dmaBus = UINT32_C(0xA7C45B36);
                    gba->memory.iwramBus = UINT32_C(0x12345678);
                    gba->cpu.memory.timed = timed != 0;
                    const uint32_t expected = selected_value(UINT32_C(0x89ABCDEF), lane, width);
                    CHECK(read_port(gba, base + lane, width, NULL) == expected);
                    CHECK(gba->memory.inputBusValid && gba->memory.lastWasDMA);
                    CHECK(gba->memory.openBus == UINT32_C(0x315AE297));
                    CHECK(gba->memory.iwramBus == UINT32_C(0x12345678));
                    int cycles = 0;
                    CHECK(read_port(gba, base + lane, width, &cycles) == expected);
                    CHECK(cycles == 1);
                    CHECK(gba->memory.inputBusValid && !gba->memory.lastWasDMA);
                    CHECK(gba->memory.openBus == sampled_input(UINT32_C(0x89ABCDEF), lane, width));
                    CHECK(gba->memory.iwramBus == UINT32_C(0x12345678));
                    CHECK(cupid_memory_open_bus(&gba->cpu) == gba->memory.openBus);
                    CHECK(cupid_io_read(gba, REG_JOYSTAT) == 8);
                    destroy(gba);
                    ++scenes;
                }
    CHECK(scenes == 56);
    printf("PASS %u JOY input-publication and untimed-observer scenes\n", scenes);
}

/* Schedule a single ordinary immediate transfer and let its existing DMA
 * events finish. The guard detects a stalled fixture without prescribing a
 * hardware clock count or altering the DMA phase schedule. */
static void start_dma(struct CupidGBA *gba, unsigned channel, uint32_t source, uint32_t destination,
                      unsigned width) {
    cupid_dma_write_sad(gba, (int)channel, source);
    cupid_dma_write_dad(gba, (int)channel, destination);
    cupid_dma_write_cnt_lo(gba, (int)channel, 1);
    cupid_dma_write_cnt(gba, (int)channel, (uint16_t)(0x8000 | (width == 4 ? 0x0400 : 0)));
    CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->memory.dma[channel].requestEvent));
}

static void complete_dma(struct CupidGBA *gba, unsigned channel) {
    unsigned clocks = 0;
    while (gba->memory.dma[channel].enable || gba->memory.dmaRunning ||
           cupid_timing_is_scheduled(&gba->timing, &gba->memory.dma[channel].requestEvent)) {
        CHECK(clocks++ < 128);
        cupid_timing_tick(&gba->timing, 1);
    }
    CHECK(!gba->cpuBlocked && gba->memory.activeDMA == -1);
}

/* DMA reads a staged JOY word/halfword through the same read path, while DMA
 * writes reach the same staged word and send-ready flag as ordinary CPU stores.
 * A waiting DMA must inherit the CPU's newer readable I/O sample. */
static void dma(void) {
    const uint32_t base = GBA_BASE_IO + REG_JOY_TRANS_LO;
    unsigned sourceScenes = 0, destinationScenes = 0, handoverScenes = 0;
    for (unsigned epoch = 0; epoch < 2; ++epoch)
        for (unsigned channel = 0; channel < 4; ++channel)
            for (unsigned width = 2; width <= 4; width += 2)
                for (unsigned lane = 0; lane <= (width == 2 ? 2u : 0u); lane += width) {
                    struct CupidGBA *gba = machine(origins[epoch * 2]);
                    write_port(gba, base, 4, UINT32_C(0x89ABCDEF));
                    gba->memory.openBus = UINT32_C(0x315AE297);
                    gba->memory.inputBusValid = true;
                    const uint32_t destination = GBA_BASE_EWRAM + 0x100;
                    start_dma(gba, channel, base + lane, destination, width);
                    complete_dma(gba, channel);
                    const uint32_t expected = selected_value(UINT32_C(0x89ABCDEF), lane, width);
                    const uint32_t sampled = width == 2 ? expected | expected << 16 : expected;
                    CHECK(gba->memory.dma[channel].latch == sampled);
                    CHECK(gba->memory.dmaBus == sampled && gba->memory.lastWasDMA);
                    CHECK(gba->memory.openBus == UINT32_C(0x315AE297) && gba->memory.inputBusValid);
                    CHECK(read_port(gba, destination, width, NULL) == expected);
                    CHECK(cupid_io_read(gba, REG_JOYSTAT) == 8);
                    check_word_reads(gba, UINT32_C(0x89ABCDEF));
                    destroy(gba);
                    ++sourceScenes;
                }

    for (unsigned epoch = 0; epoch < 2; ++epoch)
        for (unsigned channel = 0; channel < 4; ++channel) {
            struct CupidGBA *gba = machine(origins[epoch * 2]);
            write_port(gba, GBA_BASE_EWRAM + 0x200, 4, UINT32_C(0x89ABCDEF));
            start_dma(gba, channel, GBA_BASE_EWRAM + 0x200, base, 4);
            complete_dma(gba, channel);
            CHECK(gba->memory.dma[channel].latch == UINT32_C(0x89ABCDEF));
            CHECK(cupid_io_read(gba, REG_JOYSTAT) == 8);
            check_word_reads(gba, UINT32_C(0x89ABCDEF));
            const uint8_t request = SERIAL_JOY_READ;
            uint8_t reply[5];
            CHECK(cupid_serial_joybus_command(&gba->serial, &request, 1, reply, sizeof(reply)) ==
                  5);
            CHECK(reply[0] == 0xEF && reply[1] == 0xCD && reply[2] == 0xAB && reply[3] == 0x89 &&
                  reply[4] == 8);
            CHECK(!cupid_io_read(gba, REG_JOYSTAT));
            check_word_reads(gba, UINT32_C(0x89ABCDEF));
            destroy(gba);
            ++destinationScenes;
        }

    for (unsigned epoch = 0; epoch < 2; ++epoch)
        for (unsigned channel = 0; channel < 4; ++channel)
            for (unsigned width = 1; width <= 4; width *= 2)
                for (unsigned lane = 0; lane < 4; lane += width) {
                    struct CupidGBA *gba = machine(origins[epoch * 2]);
                    write_port(gba, base, 4, UINT32_C(0x89ABCDEF));
                    /* The preceding real DMA leaves an old input on the bus. */
                    write_port(gba, GBA_BASE_EWRAM + 0x200, 4, UINT32_C(0xA7C45B36));
                    start_dma(gba, 3, GBA_BASE_EWRAM + 0x200, GBA_BASE_IWRAM + 0x200, 4);
                    complete_dma(gba, 3);
                    CHECK(gba->memory.lastWasDMA && gba->memory.dmaBus == UINT32_C(0xA7C45B36));
                    const uint32_t destination = GBA_BASE_EWRAM + 0x100;
                    start_dma(gba, channel, GBA_BASE_IO + 0x1000, destination, 4);
                    cupid_timing_tick(&gba->timing, 1);
                    CHECK(!gba->memory.dmaRunning);
                    gba->cpu.memory.timed = true;
                    int cycles = 0;
                    const uint32_t expected = selected_value(UINT32_C(0x89ABCDEF), lane, width);
                    CHECK(read_port(gba, base + lane, width, &cycles) == expected);
                    CHECK(cycles == 1);
                    const uint32_t driven = sampled_input(UINT32_C(0x89ABCDEF), lane, width);
                    CHECK(gba->memory.inputBusValid && gba->memory.openBus == driven &&
                          !gba->memory.lastWasDMA);
                    cupid_bus_wait(&gba->cpu);
                    CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked);
                    CHECK(gba->memory.dma[channel].latch == driven);
                    CHECK(read_port(gba, destination, 4, NULL) == driven);
                    CHECK(cupid_io_read(gba, REG_JOYSTAT) == 8);
                    destroy(gba);
                    ++handoverScenes;
                }
    CHECK(sourceScenes == 24 && destinationScenes == 8 && handoverScenes == 56);
    printf("PASS %u JOY-source DMA, %u JOY-destination DMA and %u CPU handover scenes\n",
           sourceScenes, destinationScenes, handoverScenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "registers"))
        registers();
    else if (!strcmp(argv[1], "guest"))
        guest();
    else if (!strcmp(argv[1], "lifecycle"))
        lifecycle();
    else if (!strcmp(argv[1], "input"))
        input();
    else if (!strcmp(argv[1], "dma"))
        dma();
    else
        CHECK(false);
    return 0;
}
