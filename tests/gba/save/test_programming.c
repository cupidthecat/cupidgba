/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
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

static const uint64_t anchors[] = {0, INT32_MAX - 9u, UINT32_MAX - 9u, UINT64_C(0x300000000)};
static const uint8_t payload[8] = {0x12, 0xA5, 0, 0xFF, 0x81, 0x3C, 0x6E, 0xD7};

static struct CupidGBA* machine(uint64_t clock, enum CupidSaveType type) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    cupid_savedata_force_type(&gba->memory.savedata, type);
    CHECK(gba->memory.savedata.timing == &gba->timing);
    CHECK(cupid_savedata_set_programming_clocks(&gba->memory.savedata, 23, 11, 31, 67));
    return gba;
}

static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

static void flash(struct CupidSavedata* save, unsigned command) {
    cupid_savedata_write_flash(save, 0x5555, 0xAA);
    cupid_savedata_write_flash(save, 0x2AAA, 0x55);
    cupid_savedata_write_flash(save, 0x5555, (uint8_t)command);
}

static void eeprom(struct CupidSavedata* save, unsigned block, bool stop) {
    unsigned bits = save->type == SAVE_EEPROM ? 14 : 6;
    uint16_t wire[81]; unsigned n = 0;
    wire[n++] = 1; wire[n++] = 0;
    for (unsigned i = bits; i; i--) wire[n++] = (block >> (i - 1)) & 1;
    for (unsigned i = 0; i < 64; i++) wire[n++] = (payload[i / 8] >> (7 - i % 8)) & 1;
    wire[n++] = 0;
    for (unsigned i = 0; i < n - (stop ? 0u : 1u); i++)
        cupid_savedata_write_eeprom(save, wire[i], n - i);
}

static void eeprom_acceptance_and_ready_have_owned_deadlines(void) {
    for (unsigned c = 0; c < 4; c++) for (unsigned large = 0; large < 2; large++) {
        struct CupidGBA* gba = machine(anchors[c], large ? SAVE_EEPROM : SAVE_EEPROM512);
        struct CupidSavedata* save = &gba->memory.savedata;
        unsigned block = large ? 0x3FF : 0x3F;
        unsigned offset = block * 8;
        eeprom(save, block, false);
        CHECK(!save->programming && !save->dirty);
        for (unsigned i = 0; i < 8; i++) CHECK(save->data[offset + i] == 0xFF);
        cupid_timing_tick(&gba->timing, 40);
        CHECK(!save->programming && !save->dirty);
        cupid_savedata_write_eeprom(save, 0, 1);
        CHECK(save->programming && save->readyClock == anchors[c] + 63);
        CHECK(cupid_savedata_read_eeprom(save) == 0);
        CHECK(!cupid_savedata_set_programming_clocks(save, 1, 1, 1, 1));
        for (unsigned i = 0; i < 100; i++) {
            CHECK(cupid_savedata_read_eeprom(save) == 0);
            cupid_savedata_write_eeprom(save, i & 1, 81 - i % 81);
        }
        CHECK(save->readyClock == anchors[c] + 63 && !save->dirty);
        cupid_timing_tick(&gba->timing, 22);
        CHECK(cupid_savedata_read_eeprom(save) == 0 && !save->dirty);
        for (unsigned i = 0; i < 8; i++) CHECK(save->data[offset + i] == 0xFF);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(!save->programming && save->dirty);
        CHECK(cupid_savedata_read_eeprom(save) == 1);
        CHECK(!memcmp(save->data + offset, payload, 8));
        CHECK(save->command == EEPROM_COMMAND_NULL && save->readBitsRemaining == 0);
        destroy(gba);
    }
    puts("PASS EEPROM stop acceptance, busy suppression and clock boundaries");
}

static void flash_polling_and_data_follow_operation_scope(void) {
    for (unsigned c = 0; c < 4; c++) for (unsigned large = 0; large < 2; large++) {
      for (unsigned operation = 0; operation < 3; operation++) {
        struct CupidGBA* gba = machine(anchors[c], large ? SAVE_FLASH1M : SAVE_FLASH512);
        struct CupidSavedata* save = &gba->memory.savedata;
        memset(save->data, 0x5A, save->size);
        if (large) { flash(save, 0xB0); cupid_savedata_write_flash(save, 0, 1); }
        uint32_t bank = large ? 0x10000 : 0;
        uint32_t offset = operation == 0 ? bank + 0x2345 : operation == 1 ? bank + 0x2000 : 0;
        uint32_t length = operation == 0 ? 1 : operation == 1 ? 0x1000 : (uint32_t)save->size;
        unsigned clocks = operation == 0 ? 11 : operation == 1 ? 31 : 67;
        if (!operation) { flash(save, 0xA0); cupid_savedata_write_flash(save, 0x2345, 0x25); }
        else {
            flash(save, 0x80);
            if (operation == 1) {
                cupid_savedata_write_flash(save, 0x5555, 0xAA);
                cupid_savedata_write_flash(save, 0x2AAA, 0x55);
                cupid_savedata_write_flash(save, 0x2000, 0x30);
            } else flash(save, 0x10);
        }
        CHECK(save->programming && !save->dirty && save->readyClock == anchors[c] + clocks);
        CHECK(save->programOffset == offset && save->programLength == length);
        uint16_t poll = operation == 0 ? 0x2345 : operation == 1 ? 0x2ABC : 0x3456;
        uint8_t first = cupid_savedata_read_flash(save, poll), second = cupid_savedata_read_flash(save, poll);
        CHECK((first ^ second) == 0x40);
        CHECK(!(first & 0x20) && (first & 0x80) == (operation ? 0 : 0x80));
        if (operation != 2) CHECK(cupid_savedata_read_flash(save, 0x1234) == 0x5A);
        flash(save, 0xB0); cupid_savedata_write_flash(save, 0, 0);
        flash(save, 0xA0); cupid_savedata_write_flash(save, 0x4444, 0xFF);
        CHECK(save->flashBank == (int)large && save->programOffset == offset);
        cupid_timing_tick(&gba->timing, (int32_t)clocks - 1);
        CHECK(save->programming && !save->dirty);
        for (size_t i = 0; i < save->size; i++) CHECK(save->data[i] == 0x5A);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(!save->programming && save->dirty);
        for (size_t i = 0; i < save->size; i++) {
            uint8_t expected = i >= offset && i - offset < length ? (operation ? 0xFF : 0x25) : 0x5A;
            CHECK(save->data[i] == expected);
        }
        CHECK(cupid_savedata_read_flash(save, poll) == (operation ? 0xFF : 0x25));
        destroy(gba);
      }
    }
    puts("PASS Flash byte, sector and whole-chip scope with polling and both banks");
}

struct Observation { struct CupidSavedata* save; unsigned count; bool before, after; };
static void observe(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct Observation* result = context;
    result->count++;
    if (timing->callbackCycles == result->save->readyClock - 1)
        result->before = result->save->programming && !result->save->dirty;
    else result->after = !result->save->programming && result->save->dirty;
}

static void late_dispatch_and_dma_see_the_original_ready_edge(void) {
    for (unsigned c = 0; c < 4; c++) for (unsigned late = 0; late < 2; late++) {
        struct CupidGBA* gba = machine(anchors[c], SAVE_EEPROM512);
        struct CupidSavedata* save = &gba->memory.savedata;
        eeprom(save, 0, true);
        struct Observation seen = {.save = save};
        struct CupidTimingEvent before = {.context = &seen, .callback = observe, .priority = 0};
        struct CupidTimingEvent after = {.context = &seen, .callback = observe, .priority = 1};
        cupid_timing_schedule(&gba->timing, &before, 22);
        cupid_timing_schedule(&gba->timing, &after, 23);
        cupid_timer_write_reload(gba, 0, 0xFFF0);
        cupid_timer_write_control(gba, 0, 0xC0);
        /* Starting DMA at clock 20 makes its first read coincide with ready. */
        cupid_timing_tick(&gba->timing, 20);
        cupid_dma_write_sad(gba, 3, 0x0D001000);
        cupid_dma_write_dad(gba, 3, GBA_BASE_IWRAM);
        cupid_dma_write_cnt_lo(gba, 3, 1);
        cupid_dma_write_cnt(gba, 3, 0xC000);
        if (late) cupid_timing_tick(&gba->timing, 80);
        else for (unsigned i = 0; i < 80; i++) cupid_timing_tick(&gba->timing, 1);
        CHECK(seen.count == 2 && seen.before && seen.after);
        CHECK(gba->irqf & IRQ_TIMER0);
        CHECK(gba->irqf & IRQ_DMA3);
        CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IWRAM, NULL) == 1);
        CHECK(!save->programming && !memcmp(save->data, payload, 8));
        destroy(gba);
    }
    puts("PASS timer/DMA/IRQ chronology through late dispatch and clock wrap");
}

static void stop_and_console_reset_keep_the_pending_operation(void) {
    for (unsigned c = 0; c < 4; c++) {
        struct CupidGBA* gba = machine(anchors[c], SAVE_FLASH1M);
        struct CupidSavedata* save = &gba->memory.savedata;
        flash(save, 0xB0); cupid_savedata_write_flash(save, 0, 1);
        flash(save, 0xA0); cupid_savedata_write_flash(save, 0x1234, 0x25);
        cupid_timing_tick(&gba->timing, 4);
        cupid_gba_reset(gba);
        CHECK(save->programming && save->readyClock == 7 && save->flashBank == 1);
        CHECK(save->data[0x11234] == 0xFF && !save->dirty);
        /* STOP advances only explicit cartridge time, without fabricated frames. */
        gba->stopped = true;
        uint64_t clock = cupid_timing_global_time(&gba->timing);
        uint16_t timer = cupid_timer_read_counter(gba, 0);
        uint16_t line = gba->memory.io[REG_VCOUNT >> 1];
        cupid_gba_run_interval(gba, 6);
        CHECK(save->programming && save->readyClock == 1);
        CHECK(cupid_timing_global_time(&gba->timing) == clock);
        CHECK(cupid_timer_read_counter(gba, 0) == timer && gba->memory.io[REG_VCOUNT >> 1] == line);
        CHECK(!gba->frameReady && !save->dirty);
        cupid_gba_run_interval(gba, 1);
        CHECK(!save->programming && save->data[0x11234] == 0x25 && save->dirty);
        CHECK(cupid_timing_global_time(&gba->timing) == clock && gba->stopped);
        gba->stopped = false;
        CHECK(!cupid_gba_advance_stopped_time(gba, 20));
        destroy(gba);
    }
    puts("PASS console reset rebasing and self-timed programming during STOP");
}

static void cpu_store_acceptance_and_polling_use_bus_clocks(void) {
    for (unsigned c = 0; c < 4; c++) for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(anchors[c], SAVE_FLASH512);
        struct CupidSavedata* save = &gba->memory.savedata;
        flash(save, 0xA0);
        uint32_t store = thumb ? 0x7008 : 0xE5C10000; /* STRB r0, [r1] */
        memcpy(gba->memory.iwram, &store, 4);
        gba->cpu.gprs[0] = 0x25; gba->cpu.gprs[1] = 0x0E001234;
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
        _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
        if (thumb) { CupidThumbWritePC(&gba->cpu); cupid_thumb_step(&gba->cpu); }
        else { CupidARMWritePC(&gba->cpu); cupid_arm_step(&gba->cpu); }
        cupid_gba_process_events(gba);
        uint64_t clock = cupid_timing_global_time(&gba->timing);
        CHECK(save->programming && save->readyClock == clock + 11);
        CHECK(cupid_memory_load8(&gba->cpu, 0x0E001234, NULL) == 0x80);
        cupid_timing_tick(&gba->timing, 10);
        CHECK(save->programming && save->data[0x1234] == 0xFF);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(!save->programming && cupid_memory_load8(&gba->cpu, 0x0E001234, NULL) == 0x25);
        destroy(gba);
    }
    puts("PASS ARM/Thumb write strobes and Flash ready polling");
}

static void configuration_and_replacement_preserve_clock_ownership(void) {
    struct CupidGBA* gba = machine(0, SAVE_EEPROM512);
    struct CupidSavedata* save = &gba->memory.savedata;
    CHECK(!cupid_savedata_set_programming_clocks(save, 0, 1, 1, 1));
    CHECK(!cupid_savedata_set_programming_clocks(save, 1, 1, 1, UINT32_MAX));
    CHECK(save->eepromProgramClocks == 23 && save->flashProgramClocks == 11);
    eeprom(save, 0, true);
    cupid_timing_tick(&gba->timing, 5);
    cupid_savedata_bind_timing(save, NULL);
    CHECK(save->programming && save->readyClock == 18);
    cupid_savedata_advance_clock(save, 17);
    CHECK(save->programming && !save->dirty);
    cupid_savedata_bind_timing(save, &gba->timing);
    CHECK(save->readyClock == 6);
    cupid_timing_tick(&gba->timing, 1);
    CHECK(!save->programming && !memcmp(save->data, payload, 8));
    save->dirty = false;
    eeprom(save, 1, true);
    cupid_savedata_force_type(save, SAVE_SRAM);
    CHECK(!save->programming && save->timing == &gba->timing);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &save->readyEvent));
    cupid_timing_tick(&gba->timing, 100);
    for (size_t i = 0; i < save->size; i++) CHECK(save->data[i] == 0xFF);
    CHECK(!save->dirty);
    destroy(gba);
    puts("PASS profile validation, clock rebinding and device replacement");
}

static void pending_programming_and_file_failures_retain_complete_images(const char* directory) {
    char folder[1024], path[1100], absent[1100];
#ifdef _WIN32
    unsigned long process = (unsigned long)_getpid();
#else
    unsigned long process = (unsigned long)getpid();
#endif
    unsigned attempt;
    for (attempt = 0; attempt < 16; attempt++) {
        int n = snprintf(folder, sizeof(folder), "%s/save-programming.%lu.%u", directory, process, attempt);
        CHECK(n > 0 && (size_t)n < sizeof(folder));
#ifdef _WIN32
        if (_mkdir(folder) == 0) break;
#else
        if (mkdir(folder, 0700) == 0) break;
#endif
        CHECK(errno == EEXIST);
    }
    CHECK(attempt < 16);
    CHECK(snprintf(path, sizeof(path), "%s/data.sav", folder) > 0);
    CHECK(snprintf(absent, sizeof(absent), "%s/absent.sav", folder) > 0);
    struct CupidGBA* gba = machine(0, SAVE_EEPROM512);
    struct CupidSavedata* save = &gba->memory.savedata;
    memset(save->data, 0x5A, save->size); save->dirty = true;
    CHECK(cupid_savedata_save_file(save, path) && !save->dirty);
    eeprom(save, 0, true);
    CHECK(save->programming && !save->dirty);
    CHECK(cupid_savedata_save_file(save, path));
    CHECK(!cupid_savedata_load_file(save, absent) && save->programming);
    uint64_t deadline = save->readyClock;
    CHECK(!cupid_savedata_load_file(save, folder) && save->readyClock == deadline);
    struct CupidSavedata loaded;
    cupid_savedata_init(&loaded);
    CHECK(cupid_savedata_load_file(&loaded, path));
    for (size_t i = 0; i < loaded.size; i++) CHECK(loaded.data[i] == 0x5A);
    cupid_timing_tick(&gba->timing, 23);
    CHECK(!save->programming && save->dirty && !memcmp(save->data, payload, 8));
    CHECK(!cupid_savedata_save_file(save, folder) && save->dirty);
    CHECK(cupid_savedata_save_file(save, path) && !save->dirty);
    CHECK(cupid_savedata_load_file(&loaded, path));
    CHECK(!memcmp(loaded.data, payload, 8));
    for (size_t i = 8; i < loaded.size; i++) CHECK(loaded.data[i] == 0x5A);
    eeprom(save, 1, true);
    CHECK(cupid_savedata_load_file(save, path) && !save->programming);
    CHECK(save->command == EEPROM_COMMAND_NULL && save->readBitsRemaining == 0);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &save->readyEvent));
    cupid_timing_tick(&gba->timing, 100);
    CHECK(!save->dirty && !memcmp(save->data, loaded.data, loaded.size));
    cupid_savedata_deinit(&loaded);
    destroy(gba);
    CHECK(!remove(path));
#ifdef _WIN32
    CHECK(!_rmdir(folder));
#else
    CHECK(!rmdir(folder));
#endif
    puts("PASS pending/completed persistence and transactional load failures");
}

static uint32_t branch(unsigned condition, uint32_t from, uint32_t to) {
    return condition << 28 | 0x0A000000 | ((to - from - 8) >> 2 & 0xFFFFFF);
}

static void guest_polling_uses_the_default_byte_program_profile(void) {
    /* Owned ARM instructions use the same bus and polling path as a cartridge
     * driver. The comparison and bounded loop run inside the emulated CPU. */
    const uint32_t code[] = {
        0xE3A000AA, 0xE5C10000, 0xE3A00055, 0xE5C20000,
        0xE3A000A0, 0xE5C10000, 0xE3A00025, 0xE5C30000,
        0xE5D34000, 0xE1540000, 0, 0xE2566001, 0, 0,
        0xE3A0C000, 0, 0xE3A0C001, 0
    };
    for (unsigned c = 0; c < 4; c++) for (unsigned region = 0; region < 2; region++) {
        struct CupidGBA* gba = machine(anchors[c], SAVE_FLASH1M);
        struct CupidSavedata* save = &gba->memory.savedata;
        CHECK(cupid_savedata_set_programming_clocks(save, 108368, 650, 30000, 30000));
        uint32_t program[sizeof(code) / sizeof(*code)];
        memcpy(program, code, sizeof(code));
        uint32_t base = region ? GBA_BASE_ROM0 + 0x100 : GBA_BASE_IWRAM;
        program[10] = branch(0, base + 40, base + 56); /* BEQ pass */
        program[12] = branch(0, base + 48, base + 64); /* BEQ fail */
        program[13] = branch(14, base + 52, base + 32);
        program[15] = 0xEAFFFFFE;
        program[17] = 0xEAFFFFFE;
        if (region) {
            gba->memory.rom = calloc(1, 0x1000); CHECK(gba->memory.rom);
            gba->memory.romSize = 0x1000; gba->memory.romMask = 0xFFF;
            memcpy(gba->memory.rom + 0x100, program, sizeof(program));
        } else memcpy(gba->memory.iwram, program, sizeof(program));
        gba->cpu.gprs[1] = 0x0E005555; gba->cpu.gprs[2] = 0x0E002AAA;
        gba->cpu.gprs[3] = 0x0E000040; gba->cpu.gprs[6] = 20000;
        gba->cpu.gprs[ARM_PC] = (int32_t)base;
        _CupidSetMode(&gba->cpu, MODE_ARM); CupidARMWritePC(&gba->cpu);
        uint64_t accepted = 0, readyClock = 0;
        bool busyObserved = false;
        unsigned steps;
        for (steps = 0; steps < 100000; steps++) {
            if ((uint32_t)gba->cpu.gprs[ARM_PC] == base + 64) break;
            cupid_gba_step(gba);
            if (save->programming && !busyObserved) {
                busyObserved = true; accepted = cupid_timing_global_time(&gba->timing);
                readyClock = save->readyClock;
                CHECK(save->data[0x40] == 0xFF && !save->dirty);
            }
        }
        CHECK(steps < 100000 && busyObserved && gba->cpu.gprs[12] == 0);
        CHECK(readyClock - accepted == 650);
        CHECK(cupid_timing_global_time(&gba->timing) >= readyClock);
        CHECK(!save->programming && save->dirty && save->data[0x40] == 0x25);
        destroy(gba);
    }
    puts("PASS owned IWRAM/ROM ARM driver polling with default programming duration");
}

int main(int argc, char** argv) {
    CHECK(argc == 2 || argc == 3);
    if (!strcmp(argv[1], "eeprom")) eeprom_acceptance_and_ready_have_owned_deadlines();
    else if (!strcmp(argv[1], "flash")) flash_polling_and_data_follow_operation_scope();
    else if (!strcmp(argv[1], "chronology")) late_dispatch_and_dma_see_the_original_ready_edge();
    else if (!strcmp(argv[1], "power")) stop_and_console_reset_keep_the_pending_operation();
    else if (!strcmp(argv[1], "cpu")) cpu_store_acceptance_and_polling_use_bus_clocks();
    else if (!strcmp(argv[1], "configuration")) configuration_and_replacement_preserve_clock_ownership();
    else if (!strcmp(argv[1], "guest")) guest_polling_uses_the_default_byte_program_profile();
    else if (!strcmp(argv[1], "persistence") && argc == 3)
        pending_programming_and_file_failures_retain_complete_images(argv[2]);
    else return 2;
    return 0;
}
