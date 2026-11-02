/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_paged_rom.c
 * @brief Paged cartridge registers, backing ranges and visible windows.
 *
 * - Check mapping preserves complete window and backing.
 * - Check register widths masks and aliases.
 */
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

static const uint32_t port = 0x08800100;
static const uint64_t epochs[] = {0, INT32_MAX - 16u, UINT32_MAX - 16u, UINT64_C(0x300000000) - 16};
static const char *directory;

static uint8_t pattern(size_t i) {
    return (uint8_t)(i ^ (i >> 9) ^ (i >> 17) ^ (i >> 25));
}

static struct CupidGBA *machine(bool attached) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    if (attached) {
        gba->memory.rom = calloc(1, GBA_SIZE_ROM);
        gba->memory.pagedROM.image = malloc(GBA_SIZE_PAGED_ROM);
        CHECK(gba->memory.rom && gba->memory.pagedROM.image);
        gba->memory.pagedROM.imageSize = gba->memory.romImageSize = GBA_SIZE_PAGED_ROM;
        gba->memory.romSize = GBA_SIZE_PAGED_VIEW;
        gba->memory.romMask = GBA_SIZE_PAGED_VIEW - 1;
        for (size_t i = 0; i < GBA_SIZE_PAGED_ROM; ++i)
            gba->memory.pagedROM.image[i] = pattern(i);
        cupid_paged_rom_reset(&gba->memory.pagedROM, gba->memory.rom);
        CHECK(!memcmp(gba->memory.rom, gba->memory.pagedROM.image, 0x1000));
        CHECK(!memcmp(gba->memory.rom + 0x1000, gba->memory.pagedROM.image + 0x200, 0x1000));
    }
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void word(struct CupidGBA *gba, uint32_t address, uint32_t value) {
    cupid_memory_store32(&gba->cpu, address, (int32_t)value, NULL);
}

static void map(struct CupidGBA *gba, uint32_t address, uint32_t source, uint32_t dest,
                uint32_t count, uint32_t command) {
    word(gba, address + 4, source);
    word(gba, address + 8, dest);
    word(gba, address + 12, count);
    word(gba, address, command);
}

static void mapping_preserves_complete_window_and_backing(void) {
    struct CupidGBA *gba = machine(true);
    uint8_t expected[GBA_SIZE_PAGED_WINDOW];
    memcpy(expected, gba->memory.rom, sizeof(expected));
    unsigned scenes = 0;
    for (unsigned mirror = 0; mirror < 3; ++mirror)
        for (unsigned page = 0; page < 16; ++page)
            for (unsigned count = 1; count <= 15 && count <= 16 - page; ++count)
                for (unsigned high = 0; high < 2; ++high)
                    for (unsigned command = 1; command <= 0x11; command += 0x10) {
                        const uint32_t source =
                            high * GBA_SIZE_ROM + 0x1234 + (command == 0x11 ? 0x8000 : 0);
                        map(gba, port + mirror * GBA_SIZE_ROM, source, page * 512, count, command);
                        memcpy(expected + page * 512, gba->memory.pagedROM.image + source,
                               count * 512);
                        CHECK(!memcmp(gba->memory.rom, expected, sizeof(expected)));
                        CHECK(gba->memory.pagedROM.command == command);
                        CHECK(gba->memory.pagedROM.source == source);
                        CHECK(gba->memory.pagedROM.destination == page * 512);
                        CHECK(gba->memory.pagedROM.length == count * 512);
                        ++scenes;
                    }
    CHECK(scenes == 1620);
    for (size_t i = GBA_SIZE_PAGED_WINDOW; i < GBA_SIZE_PAGED_VIEW; ++i)
        CHECK(gba->memory.rom[i] == 0);
    for (size_t i = 0; i < GBA_SIZE_PAGED_ROM; ++i)
        CHECK(gba->memory.pagedROM.image[i] == pattern(i));
    /* Data loads use the remapped bytes through every normal ROM mirror. */
    for (unsigned mirror = 0; mirror < 3; ++mirror) {
        for (unsigned i = 0; i < sizeof(expected); i += 4) {
            uint32_t value;
            memcpy(&value, expected + i, 4);
            uint32_t address = GBA_BASE_ROM0 + mirror * GBA_SIZE_ROM + i;
            CHECK(cupid_memory_load32(&gba->cpu, address, NULL) == value);
            CHECK(cupid_memory_loadU16(&gba->cpu, address, NULL) == (value & 0xFFFF));
            CHECK(cupid_memory_load8(&gba->cpu, address + 3, NULL) == (value >> 24));
        }
    }
    destroy(gba);
}

static void register_widths_masks_and_aliases(void) {
    struct CupidGBA *gba = machine(true);
    for (unsigned alias = 0; alias < 4; ++alias) {
        uint32_t address = port + alias * 64;
        word(gba, address + 4, 0xFFFF1234);
        CHECK(gba->memory.pagedROM.source == 0x03FF1234);
        cupid_memory_store16(&gba->cpu, address + 6, 0x5678, NULL);
        CHECK(gba->memory.pagedROM.source == 0x03FF5678);
        word(gba, address + 8, 0xFFFFFF00);
        CHECK(gba->memory.pagedROM.destination == 0x007FFF00);
        cupid_memory_store16(&gba->cpu, address + 8, 0x0200, NULL);
        CHECK(gba->memory.pagedROM.destination == 0x007F0200);
        word(gba, address + 12, 3);
        CHECK(gba->memory.pagedROM.length == 1536);
        word(gba, address + 12, 0);
        CHECK(gba->memory.pagedROM.length == 1536);
        const struct CupidPagedROM before = gba->memory.pagedROM;
        for (unsigned offset = 0; offset < 256; ++offset)
            cupid_memory_store8(&gba->cpu, port + offset, 0x11, NULL);
        for (unsigned mirror = 1; mirror < 3; ++mirror)
            cupid_memory_store16(&gba->cpu, address + 4 + mirror * GBA_SIZE_ROM, 0, NULL);
        word(gba, address + 16, 0xFFFFFFFF);
        word(gba, address + 0x100, 0xFFFFFFFF);
        CHECK(!memcmp(&before, &gba->memory.pagedROM, sizeof(before)));
    }
    destroy(gba);
}

static void invalid_windows_and_short_backing_reads(void) {
    struct CupidGBA *gba = machine(true);
    uint8_t before[GBA_SIZE_PAGED_WINDOW];
    memcpy(before, gba->memory.rom, sizeof(before));
    const uint32_t invalid[][3] = {{0, 16, 1},     {1, 1, 1}, {0x2000, 1, 1},
                                   {0x1E00, 2, 1}, {0, 1, 2}, {0x7FFFFF, 15, 0x11}};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        map(gba, port, 0x02002000, invalid[i][0], invalid[i][1], invalid[i][2]);
        CHECK(!memcmp(before, gba->memory.rom, sizeof(before)));
    }
    map(gba, port, GBA_SIZE_PAGED_ROM - 7, 0x1000, 1, 1);
    memcpy(before + 0x1000, gba->memory.pagedROM.image + GBA_SIZE_PAGED_ROM - 7, 7);
    CHECK(!memcmp(before, gba->memory.rom, sizeof(before)));
    gba->memory.pagedROM.imageSize = GBA_SIZE_ROM + 1;
    map(gba, port, GBA_SIZE_ROM + 1, 0, 1, 1);
    CHECK(!memcmp(before, gba->memory.rom, sizeof(before)));
    destroy(gba);
}

static void execute(struct CupidGBA *gba, uint32_t opcode, bool thumb) {
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    memcpy(gba->memory.iwram, &opcode, sizeof(opcode));
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb)
        CupidThumbWritePC(&gba->cpu);
    else
        CupidARMWritePC(&gba->cpu);
    cupid_gba_step(gba);
}

static void cpu_stores_and_cached_instruction_fetches(void) {
    struct CupidGBA *gba = machine(true);
    const uint32_t instruction = 0xE3A0005A; /* MOV r0, #5Ah */
    memcpy(gba->memory.pagedROM.image + 0x02004000, &instruction, 4);
    for (unsigned thumb = 0; thumb < 2; ++thumb) {
        for (unsigned half = 0; half < 2; ++half) {
            word(gba, port + 4, 0x02000000);
            const uint32_t values[] = {0x02004000, 0x400, 1, 1};
            const uint32_t offsets[] = {4, 8, 12, 0};
            for (unsigned i = 0; i < 4; ++i) {
                gba->cpu.gprs[0] = (int32_t)values[i];
                gba->cpu.gprs[1] = (int32_t)(port + offsets[i]);
                execute(gba, thumb ? (half ? 0x8008 : 0x6008) : (half ? 0xE1C100B0 : 0xE5810000),
                        thumb != 0);
            }
            CHECK(!memcmp(gba->memory.rom + 0x400, gba->memory.pagedROM.image + 0x02004000, 512));
            _CupidSetMode(&gba->cpu, MODE_ARM);
            gba->cpu.gprs[0] = 0;
            gba->cpu.gprs[ARM_PC] = GBA_BASE_ROM0 + 0x400;
            CupidARMWritePC(&gba->cpu);
            cupid_gba_step(gba);
            CHECK(gba->cpu.gprs[0] == 0x5A);
        }
    }
    const uint32_t oldInstruction = 0xE3A00011; /* MOV r0, #11h */
    memcpy(gba->memory.pagedROM.image + 0x02004200, &oldInstruction, 4);
    map(gba, port, 0x02004200, 0x400, 1, 1);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_ROM0 + 0x400;
    CupidARMWritePC(&gba->cpu);
    map(gba, port, 0x02004000, 0x400, 1, 1);
    /* Changing the backing window cannot rewrite an already fetched opcode. */
    CHECK(gba->cpu.prefetch[0] == oldInstruction);
    cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[0] == 0x11);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_ROM0 + 0x400;
    CupidARMWritePC(&gba->cpu);
    cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[0] == 0x5A);
    destroy(gba);
}

static void dma_words_reach_the_controller_at_transfer_completion(void) {
    struct CupidGBA *gba = machine(true);
    for (unsigned epoch = 0; epoch < 4; ++epoch) {
        for (unsigned mirror = 0; mirror < 3; ++mirror) {
            cupid_gba_reset(gba);
            cupid_timing_clear(&gba->timing);
            gba->timing.globalCycles = epochs[epoch];
            gba->timing.masterCycles = (uint32_t)epochs[epoch];
            cupid_paged_rom_reset(&gba->memory.pagedROM, gba->memory.rom);
            const uint32_t values[] = {0x02005678, 0x1200, 2, 0x11};
            memcpy(gba->memory.iwram + 0x100, values, sizeof(values));
            for (unsigned command = 0; command < 2; ++command) {
                cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x100 + (command ? 12 : 0));
                cupid_dma_write_dad(gba, 3, port + mirror * GBA_SIZE_ROM + (command ? 0 : 4));
                cupid_dma_write_cnt_lo(gba, 3, command ? 1 : 3);
                uint8_t before[GBA_SIZE_PAGED_WINDOW];
                memcpy(before, gba->memory.rom, sizeof(before));
                const uint64_t start = cupid_timing_global_time(&gba->timing);
                const unsigned last = 5 + gba->memory.waitstatesNonseq32[8 + mirror * 2];
                cupid_dma_write_cnt(gba, 3, command ? 0xC400 : 0x8400);
                if (command) {
                    for (unsigned clock = 1; clock <= last + 2; ++clock) {
                        cupid_timing_tick(&gba->timing, 1);
                        if (clock < last)
                            CHECK(!memcmp(before, gba->memory.rom, sizeof(before)));
                        else {
                            CHECK(!memcmp(gba->memory.rom + 0x1200,
                                          gba->memory.pagedROM.image + 0x02005678, 1024));
                            CHECK(gba->memory.dma[3].busyUntil == start + last);
                        }
                        CHECK((gba->irqf & IRQ_DMA3) == (clock >= last + 2 ? IRQ_DMA3 : 0));
                        CHECK(gba->memory.dmaRunning == (clock >= 2 && clock <= last));
                    }
                } else {
                    cupid_timing_tick(&gba->timing, 128);
                    CHECK(!memcmp(before, gba->memory.rom, sizeof(before)));
                }
                CHECK(!gba->memory.dma[3].enable && !gba->memory.dmaRunning);
                CHECK(!gba->cpuBlocked);
            }
            CHECK(!memcmp(gba->memory.rom + 0x1200, gba->memory.pagedROM.image + 0x02005678, 1024));
            CHECK(gba->memory.pagedROM.command == 0x11);
        }
    }
    destroy(gba);
}

static void write_image(const char *path, size_t size, bool paged) {
    uint8_t header[512] = {0};
    const uint32_t branch = 0xEAFFFFFE;
    memcpy(header, &branch, 4);
    header[0xAC] = paged ? 'M' : 'A';
    memcpy(header + 256, "SRAM_V", 6);
    FILE *file = fopen(path, "wb");
    CHECK(file && fwrite(header, 1, sizeof(header), file) == sizeof(header));
    if (size > sizeof(header)) {
        /* Distinct source pages make the two reset mappings observable. */
        memset(header, 0x3C, sizeof(header));
        CHECK(fwrite(header, 1, sizeof(header), file) == sizeof(header));
        CHECK(fseek(file, (long)size - 1, SEEK_SET) == 0);
        CHECK(fputc(0xA5, file) != EOF);
    }
    CHECK(fclose(file) == 0);
}

static void image_loading_reset_and_replacement(void) {
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/paged-cartridge-owned.gba", directory);
    CHECK(n > 0 && (size_t)n < sizeof(path));
    struct CupidGBA *gba = machine(false);
    write_image(path, GBA_SIZE_PAGED_ROM, true);
    CHECK(cupid_load_rom(gba, path));
    CHECK(gba->memory.romImageSize == GBA_SIZE_PAGED_ROM);
    CHECK(gba->memory.romSize == GBA_SIZE_PAGED_VIEW);
    CHECK(gba->memory.romMask == GBA_SIZE_PAGED_VIEW - 1);
    CHECK(gba->memory.pagedROM.imageSize == GBA_SIZE_PAGED_ROM);
    CHECK(!memcmp(gba->memory.rom, gba->memory.pagedROM.image, 0x1000));
    CHECK(!memcmp(gba->memory.rom + 0x1000, gba->memory.pagedROM.image + 0x200, 0x1000));
    map(gba, port, GBA_SIZE_PAGED_ROM - 1, 0, 1, 1);
    CHECK(gba->memory.rom[0] == 0xA5);
    gba->memory.savedata.data[3] = 0x37;
    cupid_gba_reset(gba);
    CHECK(!memcmp(gba->memory.rom, gba->memory.pagedROM.image, 0x1000));
    CHECK(gba->memory.pagedROM.command == 0 && gba->memory.savedata.data[3] == 0x37);
    cupid_gba_step(gba);
    /* The core retains the next prefetched address between ARM steps. */
    CHECK(gba->cpu.gprs[ARM_PC] == GBA_BASE_ROM0 + 4);
    CHECK(gba->cpu.prefetch[0] == 0xEAFFFFFE);
    uint8_t *original = gba->memory.pagedROM.image;
    write_image(path, GBA_SIZE_PAGED_ROM + 1u, true);
    CHECK(!cupid_load_rom(gba, path) && gba->memory.pagedROM.image == original);
    write_image(path, GBA_SIZE_ROM + 1u, false);
    CHECK(!cupid_load_rom(gba, path) && gba->memory.pagedROM.image == original);
    write_image(path, GBA_SIZE_ROM + 1u, true);
    CHECK(cupid_load_rom(gba, path));
    CHECK(gba->memory.pagedROM.image != original);
    CHECK(gba->memory.pagedROM.imageSize == GBA_SIZE_ROM + 1u);
    CHECK(gba->memory.savedata.data[3] == 0xFF);
    write_image(path, 512, false);
    CHECK(cupid_load_rom(gba, path));
    CHECK(!gba->memory.pagedROM.image && !gba->memory.pagedROM.imageSize);
    CHECK(!cupid_paged_rom_write(gba, port, 1, 4));
    destroy(gba);
    CHECK(remove(path) == 0);
}

int main(int argc, char **argv) {
    CHECK(argc == 3);
    directory = argv[2];
    if (!strcmp(argv[1], "mapping"))
        RUN(mapping_preserves_complete_window_and_backing);
    else if (!strcmp(argv[1], "registers"))
        RUN(register_widths_masks_and_aliases);
    else if (!strcmp(argv[1], "invalid"))
        RUN(invalid_windows_and_short_backing_reads);
    else if (!strcmp(argv[1], "cpu"))
        RUN(cpu_stores_and_cached_instruction_fetches);
    else if (!strcmp(argv[1], "dma"))
        RUN(dma_words_reach_the_controller_at_transfer_completion);
    else if (!strcmp(argv[1], "loader"))
        RUN(image_loading_reset_and_replacement);
    else
        CHECK(false);
    return 0;
}
