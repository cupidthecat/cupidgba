/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#endif

static const uint8_t payload[8] = {0x12, 0xA5, 0, 0xFF, 0x81, 0x3C, 0x6E, 0xD7};

static unsigned request(uint16_t wire[81], unsigned bits, unsigned block, bool write) {
    unsigned n = 0;
    wire[n++] = 1;
    wire[n++] = write ? 0 : 1;
    for (unsigned i = bits; i; i--) wire[n++] = (block >> (i - 1)) & 1;
    if (write) {
        for (unsigned i = 0; i < 64; i++) wire[n++] = (payload[i / 8] >> (7 - i % 8)) & 1;
    }
    wire[n++] = 0;
    return n;
}

static void send(struct CupidSavedata* save, unsigned bits, unsigned block, bool write) {
    uint16_t wire[81];
    unsigned n = request(wire, bits, block, write);
    for (unsigned i = 0; i < n; i++) cupid_savedata_write_eeprom(save, wire[i], n - i);
}

static void readback(struct CupidSavedata* save, const uint8_t expected[8]) {
    for (unsigned i = 0; i < 4; i++) CHECK(cupid_savedata_read_eeprom(save) == 0);
    for (unsigned i = 0; i < 64; i++) {
        CHECK(cupid_savedata_read_eeprom(save) == ((expected[i / 8] >> (7 - i % 8)) & 1));
    }
    CHECK(save->readBitsRemaining == 0);
    CHECK(save->command == EEPROM_COMMAND_NULL);
    CHECK(cupid_savedata_read_eeprom(save) == 1);
}

static void every_address_selects_a_complete_block(void) {
    const enum CupidSaveType types[] = {SAVE_EEPROM512, SAVE_EEPROM};
    for (unsigned t = 0; t < 2; t++) {
        struct CupidSavedata save;
        cupid_savedata_init(&save);
        cupid_savedata_force_type(&save, types[t]);
        unsigned bits = t ? 14 : 6;
        size_t size = t ? 8192 : 512;
        for (unsigned block = 0; block < (1u << bits); block++) {
            memset(save.data, 0x5A, size);
            save.dirty = false;
            send(&save, bits, block, true);
            size_t offset = (block & (t ? 0x3FF : 0x3F)) * 8;
            CHECK(save.type == types[t] && save.size == size);
            CHECK(save.command == EEPROM_COMMAND_NULL && save.dirty);
            CHECK(!memcmp(save.data + offset, payload, 8));
            for (size_t i = 0; i < size; i++) {
                if (i < offset || i >= offset + 8) CHECK(save.data[i] == 0x5A);
            }
            send(&save, bits, block, false);
            readback(&save, payload);
            /* A canonical request must reach the same block and end cleanly. */
            send(&save, bits, block & (t ? 0x3FF : 0x3F), false);
            readback(&save, payload);
        }
        /* A cancelled transfer can leave all data bits accepted without a
         * stop bit. Resuming it must not access beyond the selected image. */
        uint16_t wire[81];
        unsigned n = request(wire, bits, t ? 0x3FF : 0x3F, true);
        for (unsigned i = 0; i + 1 < n; i++) cupid_savedata_write_eeprom(&save, wire[i], n - i);
        CHECK(save.command == EEPROM_COMMAND_WRITE);
        CHECK(save.writeAddress == size * 8);
        cupid_savedata_write_eeprom(&save, 1, 2);
        cupid_savedata_write_eeprom(&save, 0, 1);
        CHECK(save.type == types[t] && save.size == size);
        send(&save, bits, t ? 0x3FF : 0x3F, false);
        readback(&save, payload);
        cupid_savedata_deinit(&save);
    }
    puts("PASS 16448 complete write/read address cases");
}

static void transfer_width_selects_capacity_before_the_address(void) {
    for (unsigned write = 0; write < 2; write++) {
        for (unsigned block = 0; block < 1024; block++) {
            struct CupidSavedata save;
            cupid_savedata_init(&save);
            save.type = SAVE_EEPROM512;
            cupid_savedata_init_eeprom(&save);
            memcpy(save.data, payload, 8);
            uint16_t wire[81];
            unsigned n = request(wire, 14, block, write != 0);
            cupid_savedata_write_eeprom(&save, wire[0], n);
            cupid_savedata_write_eeprom(&save, wire[1], n - 1);
            CHECK(save.type == SAVE_EEPROM && save.size == 8192 && !save.typeForced);
            CHECK(!memcmp(save.data, payload, 8) && !save.dirty);
            for (unsigned i = 8; i < 512; i++) CHECK(save.data[i] == 0xFF);
            for (unsigned i = 512; i < 8192; i++) CHECK(save.data[i] == 0xFF);
            for (unsigned i = 2; i < n; i++) cupid_savedata_write_eeprom(&save, wire[i], n - i);
            if (write) {
                CHECK(!memcmp(save.data + block * 8, payload, 8));
                CHECK(save.dirty && save.command == EEPROM_COMMAND_NULL);
                send(&save, 14, block, false);
                readback(&save, payload);
            } else {
                const uint8_t erased[8] = {255, 255, 255, 255, 255, 255, 255, 255};
                readback(&save, block ? erased : payload);
                CHECK(!save.dirty);
            }
            cupid_savedata_deinit(&save);
        }
    }
    for (unsigned write = 0; write < 2; write++) {
        for (unsigned block = 0; block < 64; block++) {
            struct CupidSavedata save;
            cupid_savedata_init(&save);
            save.type = SAVE_EEPROM512;
            cupid_savedata_init_eeprom(&save);
            uint16_t wire[81];
            unsigned n = request(wire, 6, block, write != 0);
            cupid_savedata_write_eeprom(&save, wire[0], n);
            cupid_savedata_write_eeprom(&save, wire[1], n - 1);
            CHECK(save.type == SAVE_EEPROM512 && save.size == 512);
            cupid_savedata_deinit(&save);
        }
    }
    puts("PASS 2176 read/write capacity selections");
}

static void every_read_address_returns_to_idle_without_writes(void) {
    struct CupidSavedata save;
    cupid_savedata_init(&save);
    cupid_savedata_force_type(&save, SAVE_EEPROM);
    for (size_t i = 0; i < save.size; i++) save.data[i] = (uint8_t)(i * 13 + (i >> 8));
    for (unsigned block = 0; block < 16384; block++) {
        send(&save, 14, block, false);
        readback(&save, save.data + (block & 0x3FF) * 8);
        CHECK(save.type == SAVE_EEPROM && save.size == 8192 && !save.dirty);
    }
    for (size_t i = 0; i < save.size; i++) CHECK(save.data[i] == (uint8_t)(i * 13 + (i >> 8)));
    cupid_savedata_deinit(&save);
    puts("PASS 16384 complete read-only address cases");
}

static struct CupidGBA* machine(bool large, uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    gba->memory.romSize = large ? GBA_SIZE_ROM : 0x4000;
    gba->memory.romMask = GBA_SIZE_ROM - 1;
    return gba;
}

static void dma(struct CupidGBA* gba, uint32_t source, uint32_t dest, unsigned count) {
    cupid_dma_write_sad(gba, 3, source);
    cupid_dma_write_dad(gba, 3, dest);
    cupid_dma_write_cnt_lo(gba, 3, (uint16_t)count);
    cupid_dma_write_cnt(gba, 3, 0x8000);
    cupid_timing_tick(&gba->timing, 4096);
    CHECK(!(gba->memory.dma[3].reg & 0x8000));
}

static void dma_retains_framing_data_and_clock_wrap(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 19u, UINT32_MAX - UINT64_C(19)};
    const unsigned blocks[] = {0, 0x3F, 0x3FF, 0x400, 0x1ABC, 0x3FFF};
    for (unsigned large = 0; large < 2; large++) {
        for (unsigned c = 0; c < 3; c++) {
            for (unsigned a = 0; a < sizeof(blocks) / sizeof(*blocks); a++) {
                struct CupidGBA* gba = machine(large != 0, clocks[c]);
                uint16_t* wire = (uint16_t*)(gba->memory.ewram + 0x100);
                unsigned n = request(wire, 14, blocks[a], true);
                uint32_t port = large ? 0x0DFFFF00 : 0x0D001000;
                dma(gba, GBA_BASE_EWRAM + 0x100, port, n);
                CHECK(gba->memory.savedata.type == SAVE_EEPROM);
                CHECK(gba->memory.savedata.size == 8192);
                CHECK(gba->memory.savedata.dirty);
                CHECK(!memcmp(gba->memory.savedata.data + (blocks[a] & 0x3FF) * 8, payload, 8));
                n = request(wire, 14, blocks[a], false);
                dma(gba, GBA_BASE_EWRAM + 0x100, port, n);
                CHECK(gba->memory.savedata.readBitsRemaining == 68);
                dma(gba, port, GBA_BASE_EWRAM + 0x400, 68);
                uint16_t* result = (uint16_t*)(gba->memory.ewram + 0x400);
                for (unsigned i = 0; i < 4; i++) CHECK(result[i] == 0);
                for (unsigned i = 0; i < 64; i++) {
                    CHECK(result[i + 4] == ((payload[i / 8] >> (7 - i % 8)) & 1));
                }
                CHECK(gba->memory.savedata.command == EEPROM_COMMAND_NULL);
                CHECK(cupid_memory_load16(&gba->cpu, port, NULL) == 1);
                CHECK(cupid_timing_global_time(&gba->timing) == clocks[c] + 12288);
                cupid_gba_destroy(gba);
                free(gba);
            }
        }
    }
    puts("PASS 36 complete DMA write/read cases");
}

static void reset_and_files_retain_the_selected_capacity(const char* directory) {
    char folder[1024], path[1100];
#ifdef _WIN32
    unsigned long process = (unsigned long)_getpid();
#else
    unsigned long process = (unsigned long)getpid();
#endif
    unsigned attempt;
    for (attempt = 0; attempt < 16; attempt++) {
        int length = snprintf(folder, sizeof(folder), "%s/eeprom-test.%lu.%u", directory, process, attempt);
        CHECK(length > 0 && (size_t)length < sizeof(folder));
#ifdef _WIN32
        if (_mkdir(folder) == 0) break;
#else
        if (mkdir(folder, 0700) == 0) break;
#endif
        CHECK(errno == EEXIST);
    }
    CHECK(attempt < 16);
    CHECK(snprintf(path, sizeof(path), "%s/data.sav", folder) > 0);
    for (unsigned t = 0; t < 3; t++) {
        struct CupidSavedata save, loaded;
        cupid_savedata_init(&save);
        save.type = t == 2 ? SAVE_EEPROM : SAVE_EEPROM512;
        save.typeForced = t == 2;
        cupid_savedata_init_eeprom(&save);
        unsigned bits = t ? 14 : 6;
        send(&save, bits, 0, true);
        CHECK(save.size == (t ? 8192u : 512u));
        for (size_t i = 0; i < save.size; i++) save.data[i] = (uint8_t)(i * 13 + (i >> 8));
        unsigned block = t == 2 ? 0x3FFF : 0;
        send(&save, bits, block, true);
        uint8_t snapshot[8192];
        memcpy(snapshot, save.data, save.size);
        CHECK(cupid_savedata_save_file(&save, path) && !save.dirty);
        FILE* file = fopen(path, "rb");
        CHECK(file && !fseek(file, 0, SEEK_END));
        CHECK(ftell(file) == (long)save.size && !fclose(file));
        send(&save, bits, block, false);
        CHECK(cupid_savedata_read_eeprom(&save) == 0 && save.readBitsRemaining == 67);
        cupid_savedata_reset(&save);
        CHECK(save.command == EEPROM_COMMAND_NULL && save.readBitsRemaining == 0);
        CHECK(save.size == (t ? 8192u : 512u) && save.typeForced == (t == 2));
        CHECK(!memcmp(save.data, snapshot, save.size) && !save.dirty);
        send(&save, bits, block, false);
        readback(&save, payload);

        cupid_savedata_init(&loaded);
        CHECK(cupid_savedata_load_file(&loaded, path));
        CHECK(loaded.type == save.type && loaded.size == save.size && !loaded.dirty);
        CHECK(!memcmp(loaded.data, snapshot, save.size));
        send(&loaded, bits, block, false);
        readback(&loaded, payload);
        CHECK(loaded.size == save.size && !loaded.dirty);
        cupid_savedata_deinit(&loaded);
        cupid_savedata_deinit(&save);
        CHECK(!remove(path));
    }
#ifdef _WIN32
    CHECK(!_rmdir(folder));
#else
    CHECK(!rmdir(folder));
#endif
    puts("PASS 3 complete save/reset/load cases");
}

int main(int argc, char** argv) {
    CHECK(argc == 2 || argc == 3);
    if (!strcmp(argv[1], "addresses")) every_address_selects_a_complete_block();
    else if (!strcmp(argv[1], "capacity")) transfer_width_selects_capacity_before_the_address();
    else if (!strcmp(argv[1], "reads")) every_read_address_returns_to_idle_without_writes();
    else if (!strcmp(argv[1], "dma")) dma_retains_framing_data_and_clock_wrap();
    else if (!strcmp(argv[1], "persistence") && argc == 3) reset_and_files_retain_the_selected_capacity(argv[2]);
    else return 2;
    return 0;
}
