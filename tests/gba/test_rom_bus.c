/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include <string.h>

static struct CupidGBA* machine(bool attached) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    if (attached) {
        gba->memory.rom = malloc(0x4000);
        CHECK(gba->memory.rom);
        gba->memory.romSize = 0x4000;
        gba->memory.romMask = 0x3FFF;
        for (unsigned i = 0; i < 0x4000; i++) gba->memory.rom[i] = (uint8_t)(i ^ 0xA5);
    }
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void reads_cover_every_address_lane(void) {
    for (unsigned attached = 0; attached < 2; attached++) {
        struct CupidGBA* gba = machine(attached != 0);
        for (unsigned region = 8; region <= 13; region++) {
            /* One complete halfword-address period outside the attached image. */
            uint32_t base = (region << 24) + 0x80000;
            for (unsigned i = 0; i < 0x20000; i += 2) {
                uint32_t address = base + i;
                uint16_t expected = (uint16_t)(i / 2);
                CHECK((uint16_t)cupid_memory_load16(&gba->cpu, address, NULL) == expected);
                CHECK(cupid_memory_load8(&gba->cpu, address, NULL) == (uint8_t)expected);
                CHECK(cupid_memory_load8(&gba->cpu, address + 1, NULL) == (uint8_t)(expected >> 8));
                if (!(i & 3)) {
                    uint32_t word = ((uint32_t)(uint16_t)(expected + 1) << 16) | expected;
                    CHECK((uint32_t)cupid_memory_load32(&gba->cpu, address, NULL) == word);
                }
            }
        }
        if (attached) {
            for (unsigned window = 0; window < 3; window++) {
                uint32_t base = GBA_BASE_ROM0 + window * GBA_SIZE_ROM;
                CHECK((uint16_t)cupid_memory_load16(&gba->cpu, base + 0x3FFE, NULL) == 0x5A5B);
                CHECK((uint16_t)cupid_memory_load16(&gba->cpu, base + 0x4000, NULL) == 0x2000);
                CHECK((uint32_t)cupid_memory_load32(&gba->cpu, base + 0x3FFC, NULL) == 0x5A5B5859);
                CHECK((uint32_t)cupid_memory_load32(&gba->cpu, base + 0x4000, NULL) == 0x20012000);
            }
        }
        destroy(gba);
    }
}

static void execute(struct CupidGBA* gba, uint32_t opcode, bool thumb) {
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    memcpy(gba->memory.iwram, &opcode, sizeof(opcode));
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb) {
        CupidThumbWritePC(&gba->cpu);
        cupid_thumb_step(&gba->cpu);
    } else {
        CupidARMWritePC(&gba->cpu);
        cupid_arm_step(&gba->cpu);
    }
}

static void cpu_loads_sample_the_rom_bus(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        for (unsigned region = 8; region <= 13; region++) {
            struct CupidGBA* gba = machine(true);
            uint32_t address = (region << 24) + 0x82468;
            gba->cpu.gprs[1] = (int32_t)address;
            execute(gba, thumb ? 0x8808 : 0xE1D100B0, thumb != 0); /* LDRH r0, [r1] */
            CHECK(gba->cpu.gprs[0] == 0x1234);
            execute(gba, thumb ? 0x7808 : 0xE5D10000, thumb != 0); /* LDRB r0, [r1] */
            CHECK(gba->cpu.gprs[0] == 0x34);
            gba->cpu.gprs[1]++;
            execute(gba, thumb ? 0x7808 : 0xE5D10000, thumb != 0);
            CHECK(gba->cpu.gprs[0] == 0x12);
            gba->cpu.gprs[1]--;
            execute(gba, thumb ? 0x6808 : 0xE5910000, thumb != 0); /* LDR r0, [r1] */
            CHECK((uint32_t)gba->cpu.gprs[0] == 0x12351234);
            destroy(gba);
        }
    }
}

static void dma_reads_publish_halfwords_and_words(void) {
    for (unsigned word = 0; word < 2; word++) {
        for (unsigned region = 8; region <= 13; region++) {
            struct CupidGBA* gba = machine(true);
            uint32_t source = (region << 24) + 0x9FFF8;
            cupid_dma_write_sad(gba, 3, source);
            cupid_dma_write_dad(gba, 3, GBA_BASE_IWRAM + 0x100);
            cupid_dma_write_cnt_lo(gba, 3, 8);
            cupid_dma_write_cnt(gba, 3, word ? 0x8400 : 0x8000);
            cupid_timing_tick(&gba->timing, 512);
            CHECK(!(gba->memory.dma[3].reg & 0x8000));
            for (unsigned i = 0; i < (word ? 16u : 8u); i++) {
                uint16_t expected = (uint16_t)(0xFFFCu + i);
                CHECK((uint16_t)cupid_memory_load16(&gba->cpu, GBA_BASE_IWRAM + 0x100 + i * 2, NULL) == expected);
            }
            destroy(gba);
        }
    }
}

static void only_selected_eeprom_returns_ready(void) {
    const enum CupidSaveType types[] = {SAVE_AUTODETECT, SAVE_SRAM, SAVE_SRAM512,
        SAVE_FLASH512, SAVE_FLASH1M, SAVE_EEPROM512, SAVE_EEPROM};
    for (unsigned i = 0; i < sizeof(types) / sizeof(*types); i++) {
        struct CupidGBA* gba = machine(true);
        cupid_savedata_force_type(&gba->memory.savedata, types[i]);
        bool eeprom = types[i] == SAVE_EEPROM512 || types[i] == SAVE_EEPROM;
        CHECK((uint16_t)cupid_memory_load16(&gba->cpu, 0x0D082468, NULL) == (eeprom ? 1 : 0x1234));
        /* Save selection does not redirect the other five ROM regions. */
        for (unsigned region = 8; region < 13; region++) {
            CHECK((uint16_t)cupid_memory_load16(&gba->cpu, (region << 24) + 0x82468, NULL) == 0x1234);
        }
        destroy(gba);
    }
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "reads")) RUN(reads_cover_every_address_lane);
    else if (!strcmp(argv[1], "cpu")) RUN(cpu_loads_sample_the_rom_bus);
    else if (!strcmp(argv[1], "dma")) RUN(dma_reads_publish_halfwords_and_words);
    else if (!strcmp(argv[1], "eeprom")) RUN(only_selected_eeprom_returns_ready);
    else CHECK(false);
    return 0;
}
