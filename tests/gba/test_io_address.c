/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    gba->memory.io[REG_DISPCNT >> 1] = 0x2468;
    gba->memory.io[REG_GREENSWAP >> 1] = 1;
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void bank_zero_readback(void) {
    struct CupidGBA* gba = machine();
    CHECK(cupid_memory_load8(&gba->cpu, GBA_BASE_IO, NULL) == 0x68);
    CHECK(cupid_memory_load8(&gba->cpu, GBA_BASE_IO + 1, NULL) == 0x24);
    CHECK(cupid_memory_load8(&gba->cpu, GBA_BASE_IO + 2, NULL) == 1);
    CHECK(cupid_memory_load8(&gba->cpu, GBA_BASE_IO + 3, NULL) == 0);
    destroy(gba);
}

static void upper_banks_keep_their_address(void) {
    struct CupidGBA* gba = machine();
    unsigned scenes = 0;
    for (unsigned dma = 0; dma < 2; dma++) {
        for (unsigned bank = 1; bank < 256; bank++) {
            for (unsigned offset = 0; offset < 0x400; offset++) {
                uint32_t address = GBA_BASE_IO + (bank << 16) + offset;
                gba->cpu.prefetch[1] = 0x89ABCDEF;
                gba->memory.lastWasDMA = dma != 0;
                gba->memory.dmaBus = 0x13579BDF;
                uint32_t expected = dma ? 0x13579BDF : 0x89ABCDEF;
                uint32_t byte = expected >> ((address & 3) * 8) & 255;
                CHECK(cupid_memory_load8(&gba->cpu, address, NULL) == byte);
                CHECK(cupid_memory_load8(&gba->cpu, address, NULL) ==
                    (cupid_memory_load16(&gba->cpu, address & ~1u, NULL) >> ((address & 1) * 8) & 255));
                CHECK(cupid_memory_load8(&gba->cpu, address, NULL) ==
                    (cupid_memory_load32(&gba->cpu, address & ~3u, NULL) >> ((address & 3) * 8) & 255));
                scenes++;
            }
        }
    }
    CHECK(scenes == 522240);
    printf("upper-bank byte scenes=%u\n", scenes);
    destroy(gba);
}

static void cpu_byte_loads_keep_their_address(void) {
    struct CupidGBA* gba = machine();
    static const unsigned offsets[] = {0, 1, 2, 3, REG_BG0CNT, REG_KEYINPUT, REG_WAITCNT, REG_POSTFLG};
    unsigned scenes = 0;
    for (unsigned timed = 0; timed < 2; timed++) {
        for (unsigned thumb = 0; thumb < 2; thumb++) {
            for (unsigned bank = 1; bank < 256; bank++) {
                for (unsigned i = 0; i < sizeof(offsets) / sizeof(*offsets); i++) {
                    uint32_t address = GBA_BASE_IO + (bank << 16) + offsets[i];
                    const uint32_t data = 0xA55AA55A;
                    for (unsigned w = 0; w < 4; w++) memcpy(gba->memory.iwram + w * 4, &data, 4);
                    const uint32_t opcode = thumb ? 0x7801 : 0xE5D01000; /* LDRB r1, [r0] */
                    memcpy(gba->memory.iwram, &opcode, thumb ? 2 : 4);
                    gba->cpu.memory.timed = timed != 0;
                    gba->memory.lastWasDMA = false;
                    gba->cpu.gprs[0] = (int32_t)address;
                    gba->cpu.gprs[1] = -1;
                    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
                    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
                    if (thumb) {
                        CupidThumbWritePC(&gba->cpu);
                        cupid_thumb_step(&gba->cpu);
                    } else {
                        CupidARMWritePC(&gba->cpu);
                        cupid_arm_step(&gba->cpu);
                    }
                    CHECK((uint32_t)gba->cpu.gprs[1] == (data >> ((address & 3) * 8) & 255));
                    scenes++;
                }
            }
        }
    }
    CHECK(scenes == 8160);
    printf("ARM/Thumb upper-bank byte loads=%u\n", scenes);
    destroy(gba);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "base")) RUN(bank_zero_readback);
    else if (!strcmp(argv[1], "banks")) RUN(upper_banks_keep_their_address);
    else if (!strcmp(argv[1], "cpu")) RUN(cpu_byte_loads_keep_their_address);
    else CHECK(false);
    return 0;
}
