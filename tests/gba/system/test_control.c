/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_control.c
 * @brief System-control access widths, sleep strobes and driven interrupt input.
 *
 * - Exhaust every wide power-port byte pair with both boot-flag states.
 * - Execute ARM/Thumb stores across BIOS, RAM and cartridge origins.
 * - Retain HALT acceptance, interrupt wake and STOP wake across store widths.
 * - Observe zero IME upper input through CPU reads and waiting DMA channels.
 */
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
    return g;
}

static void destroy(struct CupidGBA *g) {
    cupid_gba_destroy(g);
    free(g);
}

static void store(struct CupidGBA *g, unsigned width, uint32_t address, uint32_t value) {
    if (width == 1)
        cupid_memory_store8(&g->cpu, address, (int8_t)value, NULL);
    else if (width == 2)
        cupid_memory_store16(&g->cpu, address, (int16_t)value, NULL);
    else
        cupid_memory_store32(&g->cpu, address, (int32_t)value, NULL);
}

static void wide(unsigned width) {
    const unsigned origins[] = {GBA_REGION_BIOS, GBA_REGION_IWRAM, GBA_REGION_ROM0};
    unsigned scenes = 0;
    for (unsigned origin = 0; origin < 3; ++origin) {
        struct CupidGBA *g = machine(0);
        g->memory.activeRegion = (int)origins[origin];
        for (unsigned old = 0; old < 2; ++old)
            for (uint32_t value = 0; value <= UINT16_MAX; ++value) {
                g->memory.io[REG_POSTFLG >> 1] = (uint16_t)old;
                g->cpu.halted = 0;
                g->stopped = false;
                store(g, width, GBA_BASE_IO + REG_POSTFLG + (value & (width - 1)),
                      value | UINT32_C(0xD6A50000));
                CHECK(cupid_io_read(g, REG_POSTFLG) == (old | (origin == 0 ? value & 1 : 0)));
                CHECK(g->cpu.halted == (origin == 0));
                CHECK(g->stopped == (origin == 0 && (value & 0x8000) != 0));
                CHECK(cupid_io_read(g, REG_POSTFLG + 2) == 0);
                CHECK(cupid_timing_global_time(&g->timing) == 0);
                ++scenes;
            }
        destroy(g);
    }
    CHECK(scenes == 393216);
    printf("power widths=%u scenes=%u\n", width, scenes);
}

/* The following ADD makes delayed HALT acceptance visible to the guest. */
static void program(struct CupidGBA *g, uint32_t address, unsigned thumb, unsigned width) {
    const unsigned index = width == 1 ? 0 : width == 2 ? 1 : 2;
    const uint32_t arm[] = {0xE5C10000, 0xE1C100B0, 0xE5810000};
    const uint16_t narrow[] = {0x7008, 0x8008, 0x6008};
    uint8_t *data;
    if (address < GBA_SIZE_BIOS)
        data = (uint8_t *)g->memory.bios + address;
    else if (address >> 24 == GBA_REGION_EWRAM)
        data = g->memory.ewram + (address & (GBA_SIZE_EWRAM - 1));
    else if (address >> 24 == GBA_REGION_IWRAM)
        data = g->memory.iwram + (address & (GBA_SIZE_IWRAM - 1));
    else {
        g->memory.rom = calloc(1, 0x400);
        CHECK(g->memory.rom);
        g->memory.romSize = 0x400;
        g->memory.romMask = 0x3FF;
        data = g->memory.rom + 0x100;
    }
    if (thumb) {
        const uint16_t code[] = {narrow[index], 0x3501, 0x46C0, 0x46C0};
        memcpy(data, code, sizeof(code));
        _CupidSetMode(&g->cpu, MODE_THUMB);
        g->cpu.gprs[ARM_PC] = address;
        CupidThumbWritePC(&g->cpu);
    } else {
        const uint32_t code[] = {arm[index], 0xE2855001, 0xE1A00000, 0xE1A00000};
        memcpy(data, code, sizeof(code));
        g->cpu.gprs[ARM_PC] = address;
        CupidARMWritePC(&g->cpu);
    }
    g->cpu.gprs[5] = 0;
}

static void guest(void) {
    const uint32_t origins[] = {0x100, 0x02000100, 0x03000100, 0x08000100};
    const uint32_t values[] = {0, 1, 0xFE, 0xFF, 0x7FFF, 0x8000, 0x8001, 0xFFFF};
    unsigned scenes = 0;
    for (unsigned origin = 0; origin < 4; ++origin)
        for (unsigned thumb = 0; thumb < 2; ++thumb)
            for (unsigned width = 1; width <= 4; width *= 2)
                for (unsigned lane = 0; lane < (width == 1 ? 2 : width); ++lane)
                    for (unsigned old = 0; old < 2; ++old)
                        for (unsigned value = 0; value < 8; ++value) {
                            struct CupidGBA *g = machine(UINT32_MAX - UINT64_C(2));
                            program(g, origins[origin], thumb, width);
                            g->memory.io[REG_POSTFLG >> 1] = (uint16_t)old;
                            g->cpu.gprs[0] = (int32_t)(UINT32_C(0xD6A50000) | values[value]);
                            g->cpu.gprs[1] = GBA_BASE_IO + REG_POSTFLG + lane;
                            cupid_gba_step(g);
                            const bool boot = origin == 0 && (width != 1 || lane == 0);
                            const bool power = origin == 0 && (width != 1 || lane == 1);
                            const bool stop =
                                power && (values[value] & (width == 1 ? 0x80 : 0x8000));
                            CHECK(cupid_io_read(g, REG_POSTFLG) ==
                                  (old | (boot ? values[value] & 1 : 0)));
                            CHECK(g->cpu.halted == stop && g->stopped == stop);
                            CHECK(cupid_timing_is_scheduled(&g->timing, &g->haltEvent) ==
                                  (power && !stop));
                            if (!stop) {
                                cupid_gba_step(g);
                                CHECK(g->cpu.gprs[5] == 1);
                                CHECK(g->cpu.halted == power);
                            }
                            destroy(g);
                            ++scenes;
                        }
    CHECK(scenes == 1024);
    printf("guest stores scenes=%u\n", scenes);
}

static void halt(void) {
    const uint64_t clocks[] = {10, UINT32_MAX - UINT64_C(2)};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 2; ++epoch)
        for (unsigned thumb = 0; thumb < 2; ++thumb)
            for (unsigned width = 1; width <= 4; width *= 2)
                for (unsigned old = 0; old < 2; ++old)
                    for (unsigned asserted = 0; asserted < 2; ++asserted) {
                        struct CupidGBA *g = machine(clocks[epoch] - 1);
                        g->cpu.cpsr.packed = MODE_SYS;
                        const uint32_t handler = 0xE3A03063;
                        memcpy(g->memory.bios + BASE_IRQ, &handler, sizeof(handler));
                        program(g, 0x100, thumb, width);
                        g->memory.io[REG_POSTFLG >> 1] = (uint16_t)old;
                        g->cpu.gprs[0] = width == 1 ? 0 : 0x007F;
                        g->cpu.gprs[1] = GBA_BASE_IO + (width == 1 ? REG_HALTCNT : REG_POSTFLG);
                        cupid_io_write(g, REG_IE, IRQ_TIMER0);
                        cupid_io_write(g, REG_IME, 1);
                        cupid_timing_tick(&g->timing, 1);
                        cupid_raise_irq_at(g, IRQ_TIMER0, clocks[epoch] - asserted);
                        cupid_gba_step(g);
                        CHECK(cupid_timing_global_time(&g->timing) == clocks[epoch] + 2);
                        CHECK(!g->cpu.halted && !g->cpuInstructionPending);
                        CHECK(g->irq.available && g->irq.line == (asserted != 0));
                        CHECK(cupid_timing_is_scheduled(&g->timing, &g->haltEvent));
                        cupid_gba_step(g);
                        CHECK(cupid_timing_global_time(&g->timing) == clocks[epoch] + 3);
                        CHECK(g->cpu.halted && g->irq.available && g->irq.line);
                        CHECK(g->cpu.gprs[5] == (asserted ? 0 : 1));
                        CHECK(g->cpuInstructionPending == (asserted != 0));
                        CHECK(g->cpu.gprs[ARM_PC] ==
                              (asserted ? BASE_IRQ : 0x100 + (thumb ? 6 : 12)));
                        cupid_gba_step(g);
                        CHECK(!g->cpu.halted && !g->cpuInstructionPending);
                        CHECK(cupid_timing_global_time(&g->timing) ==
                              clocks[epoch] + (asserted ? 8 : 9));
                        CHECK(g->cpu.privilegeMode == MODE_IRQ && g->cpu.gprs[3] == 99);
                        CHECK(g->cpu.gprs[ARM_LR] == 0x104 + (thumb ? 2 : 4) * (asserted ? 1 : 2));
                        CHECK(g->cpu.spsr.t == (thumb != 0));
                        CHECK(cupid_io_read(g, REG_POSTFLG) == (width == 1 ? old : 1));
                        destroy(g);
                        ++scenes;
                    }
    CHECK(scenes == 48);
    printf("halt acceptance and IRQ wake scenes=%u\n", scenes);
}

static void stop(void) {
    unsigned scenes = 0;
    for (unsigned thumb = 0; thumb < 2; ++thumb)
        for (unsigned width = 1; width <= 4; width *= 2)
            for (unsigned old = 0; old < 2; ++old) {
                struct CupidGBA *g = machine(0);
                program(g, 0x100, thumb, width);
                g->memory.io[REG_POSTFLG >> 1] = (uint16_t)old;
                g->cpu.gprs[0] = width == 1 ? 0xFF : 0xFFFF;
                g->cpu.gprs[1] = GBA_BASE_IO + (width == 1 ? REG_HALTCNT : REG_POSTFLG);
                cupid_io_write(g, REG_IE, IRQ_KEYPAD);
                cupid_io_write(g, REG_KEYCNT, 0x4001);
                cupid_gba_step(g);
                CHECK(g->stopped && g->cpu.halted);
                const uint64_t clock = cupid_timing_global_time(&g->timing);
                cupid_gba_advance_stopped_time(g, 1000);
                CHECK(g->stopped && cupid_timing_global_time(&g->timing) == clock);
                g->keysActive = 1;
                cupid_gba_test_keypad_irq(g);
                CHECK(!g->stopped);
                cupid_timing_tick(&g->timing, 2);
                cupid_gba_step(g);
                CHECK(!g->cpu.halted && g->cpu.gprs[5] == 1);
                CHECK(cupid_io_read(g, REG_POSTFLG) == (width == 1 ? old : 1));
                destroy(g);
                ++scenes;
            }
    CHECK(scenes == 12);
    printf("stop and keypad wake scenes=%u\n", scenes);
}

static void input(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x300000000)};
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned ime = 0; ime < 2; ++ime)
            for (unsigned width = 1; width <= 4; width *= 2)
                for (unsigned lane = 0; lane < 4; ++lane)
                    for (unsigned channel = 0; channel < 4; ++channel) {
                        struct CupidGBA *g = machine(clocks[epoch]);
                        g->ime = (int)ime;
                        g->cpu.prefetch[1] = UINT32_C(0xB7A5D369);
                        g->memory.openBus = UINT32_C(0x315AE297);
                        g->memory.inputBusValid = true;
                        g->memory.iwramBus = UINT32_C(0xA7C45B36);
                        cupid_dma_write_sad(g, channel, 0x04001000);
                        cupid_dma_write_dad(g, channel, 0x02000100);
                        cupid_dma_write_cnt_lo(g, channel, 1);
                        cupid_dma_write_cnt(g, channel, 0x8400);
                        cupid_timing_tick(&g->timing, 1);
                        g->cpu.memory.timed = true;
                        int cycles = 0;
                        const uint32_t address = GBA_BASE_IO + REG_IME + lane;
                        uint32_t value, expected, sampled;
                        if (width == 1) {
                            value = cupid_memory_load8(&g->cpu, address, &cycles);
                            expected = lane == 0 ? ime : 0;
                            sampled = expected * UINT32_C(0x01010101);
                        } else if (width == 2) {
                            value = cupid_memory_load16(&g->cpu, address, &cycles);
                            expected = lane < 2 ? ROR(ime, (lane & 1) * 8) : 0;
                            sampled = lane < 2 ? ime | ime << 16 : 0;
                        } else {
                            value = cupid_memory_load32(&g->cpu, address, &cycles);
                            expected = ROR(ime, lane * 8);
                            sampled = ime;
                        }
                        CHECK(value == expected && cycles == 1);
                        CHECK(g->memory.inputBusValid && g->memory.openBus == sampled);
                        CHECK(g->memory.iwramBus == UINT32_C(0xA7C45B36));
                        CHECK(g->memory.dmaRunning);
                        cupid_bus_wait(&g->cpu);
                        CHECK(!g->memory.dmaRunning && !g->cpuBlocked);
                        CHECK((uint32_t)cupid_memory_load32(&g->cpu, 0x02000100, NULL) == sampled);
                        CHECK(g->memory.dma[channel].latch == sampled);
                        destroy(g);
                        ++scenes;
                    }
    CHECK(scenes == 384);
    printf("IME zero input and DMA handover scenes=%u\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "halfword"))
        wide(2);
    else if (!strcmp(argv[1], "word"))
        wide(4);
    else if (!strcmp(argv[1], "guest"))
        guest();
    else if (!strcmp(argv[1], "halt"))
        halt();
    else if (!strcmp(argv[1], "stop"))
        stop();
    else {
        CHECK(!strcmp(argv[1], "input"));
        input();
    }
    return 0;
}
