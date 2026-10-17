/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"
#include <string.h>

static const char* directory;

static struct CupidGBA* machine(size_t size, enum CupidSaveType type) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    if (size) {
        gba->memory.rom = calloc(1, size);
        CHECK(gba->memory.rom);
        gba->memory.romSize = size;
        gba->memory.romMask = (uint32_t)size - 1;
    }
    cupid_savedata_force_type(&gba->memory.savedata, type);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void execute(struct CupidGBA* gba, uint32_t opcode, bool thumb) {
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    memcpy(gba->memory.iwram, &opcode, sizeof(opcode));
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb) { CupidThumbWritePC(&gba->cpu); cupid_thumb_step(&gba->cpu); }
    else { CupidARMWritePC(&gba->cpu); cupid_arm_step(&gba->cpu); }
}

static void large_images_keep_rom_reads_and_ignore_unselected_writes(void) {
    const enum CupidSaveType types[] = {SAVE_EEPROM512, SAVE_EEPROM, SAVE_AUTODETECT};
    for (unsigned t = 0; t < sizeof(types) / sizeof(*types); t++) {
        struct CupidGBA* gba = machine(GBA_SIZE_ROM, types[t]);
        const uint32_t sample = 0xA55A1234;
        memcpy(gba->memory.rom + 0x1001000, &sample, 4);
        CHECK(cupid_memory_load16(&gba->cpu, 0x0D001000, NULL) == 0x1234);
        CHECK(cupid_memory_load32(&gba->cpu, 0x0D001000, NULL) == sample);
        for (unsigned thumb = 0; thumb < 2; thumb++) {
            gba->cpu.gprs[1] = 0x0D001000;
            execute(gba, thumb ? 0x8808 : 0xE1D100B0, thumb != 0);
            CHECK(gba->cpu.gprs[0] == 0x1234);
        }
        enum CupidSavedataCommand command = gba->memory.savedata.command;
        for (unsigned page = 0; page < 0xFFFF; page++) {
            cupid_memory_store16(&gba->cpu, 0x0D000000 + page * 256, 1, NULL);
            CHECK(gba->memory.savedata.command == command);
            CHECK(gba->memory.savedata.type == types[t]);
        }
        for (unsigned offset = 0; offset < 256; offset += 2) {
            CHECK(cupid_memory_load16(&gba->cpu, 0x0DFFFF00 + offset, NULL) == (types[t] == SAVE_AUTODETECT ? 0u : 1u));
        }
        cupid_memory_store16(&gba->cpu, 0x0DFFFF00, 1, NULL);
        CHECK(gba->memory.savedata.type == (types[t] == SAVE_AUTODETECT ? SAVE_EEPROM512 : types[t]));
        CHECK(gba->memory.savedata.command == EEPROM_COMMAND_PENDING);
        destroy(gba);
    }
}

static void dma(struct CupidGBA* gba, uint32_t source, uint32_t dest, unsigned count) {
    cupid_dma_write_sad(gba, 3, source);
    cupid_dma_write_dad(gba, 3, dest);
    cupid_dma_write_cnt_lo(gba, 3, (uint16_t)count);
    cupid_dma_write_cnt(gba, 3, 0x8000);
    cupid_timing_tick(&gba->timing, 4096);
    CHECK(!(gba->memory.dma[3].reg & 0x8000));
}

static unsigned request(uint16_t* wire, unsigned bits, unsigned address, bool write) {
    unsigned n = 0;
    wire[n++] = 1;
    wire[n++] = write ? 0 : 1;
    for (unsigned i = bits; i; i--) wire[n++] = (uint16_t)((address >> (i - 1)) & 1);
    return n;
}

static void dma_commands_only_reach_the_decoded_port(void) {
    const uint8_t payload[8] = {0x12, 0xA5, 0, 0xFF, 0x81, 0x3C, 0x6E, 0xD7};
    for (unsigned large = 0; large < 2; large++) {
        for (unsigned eightK = 0; eightK < 2; eightK++) {
            struct CupidGBA* gba = machine(large ? GBA_SIZE_ROM : 0x4000,
                eightK ? SAVE_EEPROM : SAVE_EEPROM512);
            unsigned bits = eightK ? 14 : 6;
            unsigned address = eightK ? 0x3FF : 0x3F;
            uint16_t* wire = (uint16_t*)(gba->memory.ewram + 0x100);
            unsigned n = request(wire, bits, address, true);
            for (unsigned i = 0; i < 64; i++) wire[n++] = (payload[i / 8] >> (7 - i % 8)) & 1;
            wire[n++] = 0;
            if (large) {
                dma(gba, GBA_BASE_EWRAM + 0x100, 0x0D001000, n);
                CHECK(!gba->memory.savedata.dirty && gba->memory.savedata.command == EEPROM_COMMAND_NULL);
            }
            uint32_t port = large ? 0x0DFFFF00 : 0x0D001000;
            dma(gba, GBA_BASE_EWRAM + 0x100, port, n);
            /* A complete write is followed by the chip's self-timed interval. */
            cupid_timing_tick(&gba->timing, (int32_t)gba->memory.savedata.eepromProgramClocks);
            CHECK(gba->memory.savedata.dirty);
            CHECK(!memcmp(gba->memory.savedata.data + address * 8, payload, 8));
            n = request(wire, bits, address, false);
            wire[n++] = 0;
            dma(gba, GBA_BASE_EWRAM + 0x100, port, n);
            CHECK(gba->memory.savedata.readBitsRemaining == 68);
            if (large) {
                CHECK(cupid_memory_load16(&gba->cpu, 0x0D001000, NULL) == 0);
                CHECK(gba->memory.savedata.readBitsRemaining == 68);
            }
            dma(gba, port, GBA_BASE_EWRAM + 0x400, 68);
            uint16_t* readback = (uint16_t*)(gba->memory.ewram + 0x400);
            for (unsigned i = 0; i < 4; i++) CHECK(readback[i] == 0);
            for (unsigned i = 0; i < 64; i++) CHECK(readback[i + 4] == ((payload[i / 8] >> (7 - i % 8)) & 1));
            CHECK(cupid_memory_load16(&gba->cpu, port, NULL) == 1);
            destroy(gba);
        }
    }
}

static void path(char* result, size_t capacity, const char* name) {
    int length = snprintf(result, capacity, "%s/cartridge-image-%s.bin", directory, name);
    CHECK(length > 0 && (size_t)length < capacity);
}

static void image(const char* name, size_t length) {
    FILE* file = fopen(name, "wb");
    CHECK(file);
    if (length) {
        CHECK(fseek(file, (long)length - 1, SEEK_SET) == 0);
        CHECK(fputc(0x5A, file) != EOF);
    }
    CHECK(fclose(file) == 0);
}

static void decoder_uses_file_size_before_mapping_padding(void) {
    char file[1024];
    path(file, sizeof(file), "sizes");
    const size_t sizes[] = {0x2001, 0x1000000, 0x1000001, GBA_SIZE_ROM};
    struct CupidGBA* gba = machine(0, SAVE_AUTODETECT);
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(*sizes); i++) {
        image(file, sizes[i]);
        CHECK(cupid_load_rom(gba, file));
        cupid_savedata_force_type(&gba->memory.savedata, SAVE_EEPROM512);
        unsigned expected = sizes[i] <= 0x1000000 ? 1 : sizes[i] == 0x1000001 ? 0x5A : 0;
        CHECK(cupid_memory_load16(&gba->cpu, 0x0D000000, NULL) == expected);
        CHECK(cupid_memory_load16(&gba->cpu, 0x0DFFFF00, NULL) == 1);
    }
    destroy(gba);
    CHECK(remove(file) == 0);
}

static void failed_replacement_preserves_image_save_and_cpu_mapping(void) {
    char valid[1024], invalid[1024];
    path(valid, sizeof(valid), "valid");
    path(invalid, sizeof(invalid), "invalid");
    image(valid, 0x4000);
    struct CupidGBA* gba = machine(0, SAVE_AUTODETECT);
    CHECK(cupid_load_rom(gba, valid));
    cupid_savedata_force_type(&gba->memory.savedata, SAVE_SRAM);
    gba->memory.savedata.data[7] = 0xAB;
    gba->memory.savedata.dirty = true;
    gba->cpu.gprs[ARM_PC] = GBA_BASE_ROM0 + 0x100;
    cupid_memory_set_active_region(&gba->cpu, GBA_BASE_ROM0 + 0x100);
    struct CupidMemory before = gba->memory;
    const size_t invalidSizes[] = {0, GBA_SIZE_ROM + 1};
    for (unsigned i = 0; i < sizeof(invalidSizes) / sizeof(*invalidSizes); i++) {
        image(invalid, invalidSizes[i]);
        CHECK(!cupid_load_rom(gba, invalid));
        CHECK(gba->memory.rom == before.rom && gba->memory.romSize == before.romSize);
        CHECK(gba->memory.romMask == before.romMask && gba->memory.activeRegion == before.activeRegion);
        CHECK((const void*)gba->cpu.memory.activeRegion == before.rom);
        CHECK(gba->memory.savedata.data == before.savedata.data);
        CHECK(gba->memory.savedata.data[7] == 0xAB && gba->memory.savedata.dirty);
    }
    image(valid, 0x8000);
    CHECK(cupid_load_rom(gba, valid));
    CHECK(gba->memory.rom != before.rom);
    CHECK((const void*)gba->cpu.memory.activeRegion == gba->memory.rom && gba->cpu.memory.activeMask == 0x7FFC);
    /* A headerless replacement must not retain an old cartridge identifier. */
    gba->memory.romID = 0xA55A;
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    cupid_memory_set_active_region(&gba->cpu, GBA_BASE_IWRAM);
    image(valid, 1);
    CHECK(cupid_load_rom(gba, valid) && gba->memory.romID == 0);
    destroy(gba);
    CHECK(remove(valid) == 0 && remove(invalid) == 0);
}

int main(int argc, char** argv) {
    CHECK(argc == 3);
    directory = argv[2];
    if (!strcmp(argv[1], "decoder")) RUN(large_images_keep_rom_reads_and_ignore_unselected_writes);
    else if (!strcmp(argv[1], "dma")) RUN(dma_commands_only_reach_the_decoded_port);
    else if (!strcmp(argv[1], "sizes")) RUN(decoder_uses_file_size_before_mapping_padding);
    else if (!strcmp(argv[1], "replacement")) RUN(failed_replacement_preserves_image_save_and_cpu_mapping);
    else CHECK(false);
    return 0;
}
