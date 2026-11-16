/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_opcode_ownership.c
 * @author    cupidthecat
 * @brief     Validate mapped opcode samples across device, IRQ and DMA ownership.
 * @date      2026-11-16
 *
 * - Keep live cartridge-port samples ahead of an out-of-view ROM fallback.
 * - Retain discarded IRQ fetches and CPU input through DMA bus handover.
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

static const uint64_t origins[] = {0, UINT32_MAX - UINT64_C(17)};

static struct CupidGBA *machine(uint64_t origin) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = origin;
    gba->timing.masterCycles = (uint32_t)origin;
    gba->audio.lastSample = gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + 512;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

struct PortInput {
    uint8_t pins;
    unsigned reads;
};

static uint8_t input_pins(void *context) {
    struct PortInput *port = context;
    port->reads++;
    return port->pins;
}

static void mapped_fallback_keeps_live_device_priority(void) {
    unsigned scenes = 0;
    for (unsigned width = 2; width <= 4; width += 2)
        for (unsigned fromBios = 0; fromBios < 2; fromBios++)
            for (unsigned epoch = 0; epoch < 2; epoch++) {
                struct CupidGBA *gba = machine(origins[epoch]);
                struct CupidARM *cpu = &gba->cpu;
                /* The mapped image stops at C4; the readable GPIO board
                 * remains visible beyond that image's direct opcode span. */
                gba->memory.rom = calloc(1, 0x100);
                CHECK(gba->memory.rom);
                gba->memory.romSize = 0xC4;
                gba->memory.romMask = 0xFF;
                struct PortInput port = {.pins = 1};
                cupid_cartridge_set_input(gba, input_pins, &port);
                CHECK(cupid_cartridge_gpio_write(gba, GBA_BASE_ROM0 + 0xC6, 4));
                CHECK(cupid_cartridge_gpio_write(gba, GBA_BASE_ROM0 + 0xC4, 4));
                CHECK(cupid_cartridge_gpio_write(gba, GBA_BASE_ROM0 + 0xC8, 1));
                CHECK(gba->cartridge.readable && cpu->memory.readOpcode);

                _CupidSetMode(cpu, width == 2 ? MODE_THUMB : MODE_ARM);
                uint32_t source = fromBios ? 0x100 : GBA_BASE_IWRAM + 0x100;
                cpu->gprs[ARM_PC] = (int32_t)source;
                cupid_memory_set_active_region(cpu, source);
                cpu->prefetch[1] = UINT32_C(0xA1B2C3D4);
                gba->memory.biosPrefetch = UINT32_C(0x10293847);
                gba->memory.lastPrefetchedPc = GBA_BASE_ROM0 + 0x300;
                uint32_t lastJump = gba->lastJump;

                port.reads = 0;
                uint32_t expected = width == 2 ? 5 : UINT32_C(0x00040005);
                CHECK(CupidReadOpcode(cpu, GBA_BASE_ROM0 + 0xC4, width) == expected);
                CHECK(port.reads == 1);
                CHECK(gba->memory.activeRegion == GBA_REGION_ROM0);
                CHECK(cpu->memory.activeRegion == (const uint32_t *)gba->memory.rom);
                CHECK(gba->lastJump == lastJump &&
                      gba->memory.lastPrefetchedPc == GBA_BASE_ROM0 + 0x300);
                CHECK(gba->memory.biosPrefetch ==
                      (fromBios ? UINT32_C(0xA1B2C3D4) : UINT32_C(0x10293847)));

                port.pins = 9;
                expected = width == 2 ? 13 : UINT32_C(0x0004000D);
                CHECK(CupidReadOpcode(cpu, GBA_BASE_ROM0 + 0xC4, width) == expected);
                CHECK(port.reads == 2);
                CHECK(!gba->cartridge.prefetchMask && !gba->cartridge.opcodeValid);

                CHECK(cupid_cartridge_gpio_write(gba, GBA_BASE_ROM0 + 0xC8, 0));
                CHECK(!cpu->memory.readOpcode);
                const uint32_t undriven = width == 2 ? UINT32_C(0x0062) : UINT32_C(0x00630062);
                CHECK(CupidReadOpcode(cpu, GBA_BASE_ROM0 + 0xC4, width) == undriven);
                CHECK(port.reads == 2);
                CHECK(gba->lastJump == lastJump);
                destroy(gba);
                scenes++;
            }
    CHECK(scenes == 8);
    printf("PASS %u mapped ROM/device ownership scenes\n", scenes);
}

struct IRQSamples {
    struct CupidARM *owner;
    uint32_t address[3], value[3];
    unsigned width[3], count;
    uint32_t firstIWRAM, firstInput;
    bool firstValid;
};

static struct IRQSamples irqSamples;

static void capture_irq_opcode(struct CupidARM *cpu, uint32_t address, unsigned width,
                               uint32_t value) {
    CHECK(cpu == irqSamples.owner && irqSamples.count < 3);
    const unsigned index = irqSamples.count++;
    irqSamples.address[index] = address;
    irqSamples.width[index] = width;
    irqSamples.value[index] = value;
    cupid_memory_capture_opcode(cpu, address, width, value);
    if (!index) {
        irqSamples.firstIWRAM = cpu->master->memory.iwramBus;
        irqSamples.firstInput = cpu->master->memory.openBus;
        irqSamples.firstValid = cpu->master->memory.inputBusValid;
    }
}

static void irq_discarded_target_is_sampled_before_vector(void) {
    unsigned scenes = 0;
    for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned epoch = 0; epoch < 2; epoch++) {
            struct CupidGBA *gba = machine(origins[epoch]);
            struct CupidARM *cpu = &gba->cpu;
            const unsigned width = thumb ? 2 : 4;
            const uint32_t source = GBA_BASE_IWRAM - 2 * width;
            const uint16_t sourceThumb[] = {0x46C0, 0x46C0};
            const uint32_t sourceARM[] = {0xE1A00000, 0xE1A00000};
            const uint32_t target = thumb ? 0x2022 : UINT32_C(0xE3A00022);
            const uint32_t wrapped = thumb ? 0x2033 : UINT32_C(0xE3A00033);
            const uint32_t vector[] = {UINT32_C(0xE3A03063), UINT32_C(0xE1A00000)};
            const uint32_t retained = UINT32_C(0x789ABCDE);
            memcpy(gba->memory.ewram + GBA_SIZE_EWRAM - 2 * width,
                   thumb ? (const void *)sourceThumb : (const void *)sourceARM, 2 * width);
            memcpy(gba->memory.ewram, &wrapped, width);
            memcpy(gba->memory.iwram, &target, width);
            memcpy(gba->memory.bios + BASE_IRQ, vector, sizeof(vector));
            cupid_memory_store32(cpu, GBA_BASE_IWRAM + 0x100, (int32_t)retained, NULL);
            cpu->memory.timed = true;
            int cycles = 0;
            CHECK(cupid_memory_load32(cpu, GBA_BASE_IWRAM + 0x100, &cycles) == retained);
            CHECK(gba->memory.iwramBus == retained);
            _CupidSetMode(cpu, thumb ? MODE_THUMB : MODE_ARM);
            cpu->gprs[ARM_PC] = (int32_t)source;
            cpu->gprs[0] = 0x66;
            if (thumb)
                CupidThumbWritePC(cpu);
            else
                CupidARMWritePC(cpu);
            CHECK(cpu->prefetch[0] == (thumb ? sourceThumb[0] : sourceARM[0]) &&
                  cpu->prefetch[1] == (thumb ? sourceThumb[1] : sourceARM[1]));
            CHECK(gba->memory.activeRegion == GBA_REGION_EWRAM);
            const uint32_t status = cpu->cpsr.packed;

            irqSamples = (struct IRQSamples){.owner = cpu};
            cpu->memory.captureOpcode = capture_irq_opcode;
            cupid_arm_take_irq(cpu);
            cpu->memory.captureOpcode = cupid_memory_capture_opcode;
            CHECK(irqSamples.count == 3);
            CHECK(irqSamples.address[0] == GBA_BASE_IWRAM && irqSamples.width[0] == width);
            CHECK(irqSamples.value[0] == target);
            for (unsigned i = 0; i < 2; i++) {
                CHECK(irqSamples.address[i + 1] == BASE_IRQ + i * 4);
                CHECK(irqSamples.width[i + 1] == 4 && irqSamples.value[i + 1] == vector[i]);
                CHECK(cpu->prefetch[i] == vector[i]);
            }
            const uint32_t expectedInput =
                thumb ? (retained & UINT32_C(0xFFFF0000)) | target : target;
            CHECK(irqSamples.firstValid && irqSamples.firstInput == expectedInput &&
                  irqSamples.firstIWRAM == expectedInput);
            CHECK(gba->memory.iwramBus == expectedInput);
            CHECK(cpu->privilegeMode == MODE_IRQ && cpu->executionMode == MODE_ARM);
            CHECK((uint32_t)cpu->gprs[ARM_LR] == source + 4 &&
                  (uint32_t)cpu->spsr.packed == status);
            CHECK(cpu->gprs[0] == 0x66 && gba->memory.activeRegion == GBA_REGION_BIOS);
            cupid_arm_step(cpu);
            CHECK(cpu->gprs[3] == 0x63 && cpu->gprs[0] == 0x66);
            destroy(gba);
            scenes++;
        }
    CHECK(scenes == 4);
    printf("PASS %u discarded mapped-IRQ fetch scenes\n", scenes);
}

static void dma_observes_first_mapped_target_and_retained_lane(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 2; epoch++)
        for (unsigned channel = 0; channel < 4; channel += 3) {
            struct CupidGBA *gba = machine(origins[epoch]);
            struct CupidARM *cpu = &gba->cpu;
            const uint16_t source[] = {0x46C0, 0x46C0};
            const uint16_t target[] = {0x2022, 0x46C0};
            const uint16_t wrapped = 0x2033;
            const uint32_t retained = UINT32_C(0xA5B6C7D8);
            const uint32_t expected = (retained & UINT32_C(0xFFFF0000)) | target[0];
            memcpy(gba->memory.ewram + GBA_SIZE_EWRAM - sizeof(source), source, sizeof(source));
            memcpy(gba->memory.ewram, &wrapped, sizeof(wrapped));
            memcpy(gba->memory.iwram, target, sizeof(target));
            cupid_memory_store32(cpu, GBA_BASE_IWRAM + 0x100, (int32_t)retained, NULL);
            cpu->memory.timed = true;
            int cycles = 0;
            CHECK(cupid_memory_load32(cpu, GBA_BASE_IWRAM + 0x100, &cycles) == retained);
            CHECK(gba->memory.iwramBus == retained);
            _CupidSetMode(cpu, MODE_THUMB);
            cpu->gprs[ARM_PC] = (int32_t)(GBA_BASE_IWRAM - sizeof(source));
            CupidThumbWritePC(cpu);
            CHECK(cpu->prefetch[0] == source[0] && cpu->prefetch[1] == source[1]);
            const uint32_t branch = gba->lastJump;
            const uint64_t before = cupid_timing_global_time(&gba->timing);
            const uint32_t destination = GBA_BASE_EWRAM + 0x100;
            cupid_dma_write_sad(gba, (int)channel, GBA_BASE_IO + 0x1000);
            cupid_dma_write_dad(gba, (int)channel, destination);
            cupid_dma_write_cnt_lo(gba, (int)channel, 1);
            cupid_dma_write_cnt(gba, (int)channel, 0x8400);
            CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->memory.dma[channel].requestEvent));
            cupid_bus_advance(cpu, 1, false);
            cupid_bus_finish(cpu);
            CHECK(!gba->memory.dmaRunning);
            cupid_thumb_step(cpu);
            CHECK(cpu->prefetch[0] == source[1] && cpu->prefetch[1] == target[0]);
            CHECK((uint32_t)cpu->gprs[ARM_PC] == GBA_BASE_IWRAM);
            CHECK(cpu->gprs[0] == 0 && gba->lastJump == branch);
            CHECK(gba->memory.activeRegion == GBA_REGION_IWRAM && gba->memory.iwramBus == expected);
            cupid_bus_wait(cpu);
            cupid_bus_finish(cpu);
            CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked);
            CHECK(gba->memory.dma[channel].latch == expected);
            CHECK((uint32_t)cupid_memory_load32(cpu, destination, NULL) == expected);
            CHECK(cupid_timing_global_time(&gba->timing) > before);
            cupid_thumb_step(cpu);
            cupid_thumb_step(cpu);
            CHECK(cpu->gprs[0] == 0x22);
            destroy(gba);
            scenes++;
        }
    CHECK(scenes == 4);
    printf("PASS %u mapped-opcode CPU-to-DMA handovers\n", scenes);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "ports"))
        mapped_fallback_keeps_live_device_priority();
    else if (!strcmp(argv[1], "irq"))
        irq_discarded_target_is_sampled_before_vector();
    else if (!strcmp(argv[1], "dma"))
        dma_observes_first_mapped_target_and_retained_lane();
    else
        CHECK(false);
    return 0;
}
