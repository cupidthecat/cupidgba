/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"

static unsigned axis(const struct CupidTilt* tilt, bool y) {
    uint8_t low, high;
    CHECK(cupid_tilt_read(tilt, y ? 0x8400 : 0x8200, &low));
    CHECK(cupid_tilt_read(tilt, y ? 0x8500 : 0x8300, &high));
    CHECK((high & 0xF0) == (y ? 0 : 0x80));
    return low | (unsigned)(high & 15) << 8;
}

static void complete_axis_range_and_retained_snapshots(void) {
    struct CupidTilt tilt;
    cupid_tilt_init(&tilt);
    CHECK(axis(&tilt, false) == 4095 && axis(&tilt, true) == 4095);
    CHECK(tilt.inputX == CUPID_TILT_NEUTRAL && tilt.inputY == CUPID_TILT_NEUTRAL);
    for (unsigned value = 0; value < 4096; value++) {
        const unsigned oldX = axis(&tilt, false), oldY = axis(&tilt, true);
        CHECK(cupid_tilt_set_sample(&tilt, (uint16_t)value, (uint16_t)(4095 - value)));
        CHECK(axis(&tilt, false) == oldX && axis(&tilt, true) == oldY);
        CHECK(cupid_tilt_write(&tilt, 0x8000, 0x55));
        CHECK(axis(&tilt, false) == oldX && axis(&tilt, true) == oldY);
        CHECK(cupid_tilt_write(&tilt, 0x8100, 0xAA));
        CHECK(axis(&tilt, false) == value && axis(&tilt, true) == 4095 - value);
    }
    CHECK(!cupid_tilt_set_sample(&tilt, 4096, 1));
    CHECK(tilt.inputX == 4095 && tilt.inputY == 0);
    CHECK(!cupid_tilt_set_sample(&tilt, 1, UINT16_MAX));
    CHECK(tilt.inputX == 4095 && tilt.inputY == 0);
    CHECK(axis(&tilt, false) == 4095 && axis(&tilt, true) == 0);
}

static void command_pair_and_reset_preserve_input_ownership(void) {
    struct CupidTilt tilt;
    cupid_tilt_init(&tilt);
    CHECK(cupid_tilt_set_sample(&tilt, 0x123, 0xABC));
    CHECK(cupid_tilt_write(&tilt, 0x8100, 0xAA));
    CHECK(axis(&tilt, false) == 4095 && !tilt.armed);
    CHECK(cupid_tilt_write(&tilt, 0x8000, 0));
    CHECK(!tilt.armed);
    CHECK(cupid_tilt_write(&tilt, 0x8000, 0x55) && tilt.armed);
    CHECK(cupid_tilt_write(&tilt, 0x8100, 0) && tilt.armed);
    uint8_t unchanged = 0x5A;
    CHECK(!cupid_tilt_read(&tilt, 0x8201, &unchanged) && unchanged == 0x5A);
    CHECK(!cupid_tilt_write(&tilt, 0x8001, 0xAA) && tilt.armed);
    CHECK(cupid_tilt_set_sample(&tilt, 0x456, 0x789));
    CHECK(cupid_tilt_write(&tilt, 0x8100, 0xAA) && !tilt.armed);
    CHECK(axis(&tilt, false) == 0x456 && axis(&tilt, true) == 0x789);
    CHECK(cupid_tilt_set_sample(&tilt, 0, 0));
    CHECK(cupid_tilt_write(&tilt, 0x8100, 0xAA));
    CHECK(axis(&tilt, false) == 0x456 && axis(&tilt, true) == 0x789);
    cupid_tilt_reset(&tilt);
    CHECK(!tilt.armed && !tilt.inputX && !tilt.inputY);
    CHECK(axis(&tilt, false) == 4095 && axis(&tilt, true) == 4095);
}

static struct CupidGBA* machine(const char* board) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 4096);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 4096;
    gba->memory.romMask = 4095;
    memcpy(gba->memory.rom + 0xAC, board, 4);
    cupid_cartridge_attach(gba, gba->memory.rom, 4096);
    cupid_gba_reset(gba);
    cupid_memory_update_waitstates(gba, 0x0803);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static uint32_t access(struct CupidGBA* gba, bool thumb, unsigned width,
                       uint32_t address, uint32_t value, bool load) {
    const uint16_t thumbLoad[] = {0x7801, 0x8801, 0x6801};
    const uint16_t thumbStore[] = {0x7001, 0x8001, 0x6001};
    const uint32_t armLoad[] = {0xE5D01000, 0xE1D010B0, 0xE5901000};
    const uint32_t armStore[] = {0xE5C01000, 0xE1C010B0, 0xE5801000};
    const unsigned slot = width == 4 ? 2 : width - 1;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    if (thumb) {
        const uint16_t code[] = {load ? thumbLoad[slot] : thumbStore[slot], 0x46C0, 0x46C0};
        memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        CupidThumbWritePC(&gba->cpu);
    } else {
        const uint32_t code[] = {load ? armLoad[slot] : armStore[slot], 0xE1A00000, 0xE1A00000};
        memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        CupidARMWritePC(&gba->cpu);
    }
    gba->cpu.gprs[0] = (int32_t)address;
    gba->cpu.gprs[1] = (int32_t)value;
    cupid_gba_step(gba);
    return (uint32_t)gba->cpu.gprs[1];
}

static void cpu_widths_and_save_windows_keep_the_complete_bytes(void) {
    const char* boards[] = {"KHPJ", "KYGJ", "KYGE", "KYGP"};
    const unsigned values[] = {0, 1, 0x100, 0x3A0, 0x5A5, 0xA5A, 4095};
    for (unsigned b = 0; b < 4; b++) for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned mirror = 0; mirror < 2; mirror++) {
            struct CupidGBA* gba = machine(boards[b]);
            CHECK(gba->cartridge.tiltPresent && !gba->cartridge.rtcPresent &&
                !gba->cartridge.solarPresent && !gba->cartridge.rumblePresent && !gba->cartridge.gyroPresent);
            const uint32_t base = 0x0E008000 + mirror * 0x01000000;
            for (unsigned width = 1; width <= 4; width *= 2) for (unsigned v = 0; v < 7; v++) {
                const unsigned x = values[v], y = 4095 - values[v];
                CHECK(cupid_cartridge_set_tilt_sample(gba, (uint16_t)x, (uint16_t)y));
                access(gba, thumb != 0, width, base, 0xA1B2C355, false);
                access(gba, thumb != 0, width, base + 0x100, 0xD4E5F6AA, false);
                const uint8_t bytes[] = {(uint8_t)x, (uint8_t)(0x80 | (x >> 8)),
                    (uint8_t)y, (uint8_t)(y >> 8)};
                for (unsigned r = 0; r < 4; r++) {
                    const uint32_t repeated = width == 1 ? bytes[r] : width == 2
                        ? bytes[r] * UINT32_C(0x0101) : bytes[r] * UINT32_C(0x01010101);
                    CHECK(access(gba, thumb != 0, width, base + 0x200 + r * 0x100, 0, true) == repeated);
                }
                CHECK(gba->memory.savedata.type == SAVE_AUTODETECT && !gba->memory.savedata.dirty);
            }
            destroy(gba);
        }
}

static void storage_gpio_and_lifecycle_stay_independent(void) {
    struct CupidGBA* gba = machine("KYGE");
    cupid_savedata_init_sram(&gba->memory.savedata);
    for (unsigned i = 0; i < GBA_SIZE_SRAM; i++) gba->memory.savedata.data[i] = (uint8_t)(i ^ (i >> 8));
    uint8_t* before = malloc(GBA_SIZE_SRAM);
    CHECK(before);
    memcpy(before, gba->memory.savedata.data, GBA_SIZE_SRAM);
    CHECK(cupid_cartridge_set_tilt_sample(gba, 0x123, 0xABC));
    cupid_memory_store8(&gba->cpu, 0x0E008000, 0x55, NULL);
    cupid_memory_store8(&gba->cpu, 0x0E008100, 0xAA, NULL);
    CHECK(cupid_memory_load8(&gba->cpu, 0x0E008200, NULL) == 0x23);
    CHECK(!memcmp(before, gba->memory.savedata.data, GBA_SIZE_SRAM) && !gba->memory.savedata.dirty);
    cupid_memory_store8(&gba->cpu, 0x0E000123, 0x5A, NULL);
    before[0x123] = 0x5A;
    CHECK(!memcmp(before, gba->memory.savedata.data, GBA_SIZE_SRAM) && gba->memory.savedata.dirty);
    free(before);
    cupid_cartridge_enable_rumble(gba, true);
    cupid_cartridge_gpio_write(gba, 0x080000C6, 8);
    cupid_cartridge_gpio_write(gba, 0x080000C4, 8);
    CHECK(gba->cartridge.rumbleActive);
    cupid_cartridge_enable_tilt(gba, false);
    CHECK(gba->cartridge.rumbleActive && gba->cartridge.direction == 8);
    CHECK(gba->cartridge.tilt.inputX == 0x123 && gba->cartridge.tilt.inputY == 0xABC);
    cupid_cartridge_enable_tilt(gba, true);
    CHECK(gba->cartridge.rumbleActive && !gba->cartridge.tilt.armed);
    cupid_gba_reset(gba);
    CHECK(gba->cartridge.tiltPresent && gba->cartridge.tilt.inputX == 0x123 &&
        gba->cartridge.tilt.inputY == 0xABC && gba->cartridge.tilt.x == 4095);
    memcpy(gba->memory.rom + 0xAC, "V49E", 4);
    cupid_cartridge_attach(gba, gba->memory.rom, 4096);
    CHECK(!gba->cartridge.tiltPresent && gba->cartridge.rumblePresent);
    CHECK(gba->cartridge.tilt.inputX == 0x123 && gba->cartridge.tilt.inputY == 0xABC);
    CHECK(!gba->cartridge.tilt.armed && gba->cartridge.tilt.x == 4095);
    destroy(gba);
}

static void board_wiring_wins_over_unused_library_signatures(void) {
    const char* boards[] = {"KYGE", "RZWE", "V49E", "U3IE", "XXXX"};
    for (unsigned board = 0; board < 5; board++) {
        struct CupidGBA* gba = machine(boards[board]);
        memcpy(gba->memory.rom + 0x200, "SIIRTC_V001", 11);
        cupid_cartridge_attach(gba, gba->memory.rom, 4096);
        CHECK(gba->cartridge.rtcPresent == (board >= 3));
        CHECK(gba->cartridge.tiltPresent == (board == 0));
        CHECK(gba->cartridge.gyroPresent == (board == 1));
        CHECK(gba->cartridge.rumblePresent == (board == 1 || board == 2));
        CHECK(gba->cartridge.solarPresent == (board == 3));
        cupid_cartridge_enable_rtc(gba, true);
        CHECK(gba->cartridge.rtcPresent); /* Explicit device selection remains available. */
        destroy(gba);
    }
}

static void eeprom_dma(struct CupidGBA* gba, uint16_t* bits, unsigned count, bool read) {
    if (!read) memcpy(gba->memory.iwram + 0x2000, bits, count * sizeof(*bits));
    cupid_dma_write_sad(gba, 3, read ? 0x0D000000 : GBA_BASE_IWRAM + 0x2000);
    cupid_dma_write_dad(gba, 3, read ? GBA_BASE_IWRAM + 0x2000 : 0x0D000000);
    cupid_dma_write_cnt_lo(gba, 3, (uint16_t)count);
    cupid_dma_write_cnt(gba, 3, 0x8000);
    cupid_timing_tick(&gba->timing, 8192);
    CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable);
    if (read) memcpy(bits, gba->memory.iwram + 0x2000, count * sizeof(*bits));
}

static void eeprom_driver_and_sensor_commands_preserve_each_others_state(void) {
    struct CupidGBA* gba = machine("KYGE");
    cupid_savedata_force_type(&gba->memory.savedata, SAVE_EEPROM512);
    CHECK(gba->memory.savedata.type == SAVE_EEPROM512 && gba->memory.savedata.data);
    const uint8_t payload[] = {0x80, 0x01, 0xA5, 0x5A, 0x12, 0x34, 0xCD, 0xEF};
    uint16_t bits[73] = {1, 0};
    unsigned count = 2;
    for (int bit = 5; bit >= 0; bit--) bits[count++] = (7u >> bit) & 1;
    for (unsigned byte = 0; byte < 8; byte++) for (int bit = 7; bit >= 0; bit--)
        bits[count++] = (payload[byte] >> bit) & 1;
    bits[count++] = 0;
    CHECK(count == 73);
    eeprom_dma(gba, bits, count, false);
    CHECK(cupid_cartridge_set_tilt_sample(gba, 0x123, 0xABC));
    cupid_memory_store8(&gba->cpu, 0x0E008000, 0x55, NULL);
    cupid_memory_store8(&gba->cpu, 0x0E008100, 0xAA, NULL);
    CHECK(gba->memory.savedata.type == SAVE_EEPROM512);
    unsigned polls = 0;
    do {
        eeprom_dma(gba, bits, 1, true);
        CHECK(++polls <= 2048); /* At most one second of modeled cartridge clocks. */
    } while (!(bits[0] & 1));
    bits[0] = bits[1] = 1;
    count = 2;
    for (int bit = 5; bit >= 0; bit--) bits[count++] = (7u >> bit) & 1;
    bits[count++] = 0;
    eeprom_dma(gba, bits, count, false);
    eeprom_dma(gba, bits, 68, true);
    for (unsigned dummy = 0; dummy < 4; dummy++) CHECK(!(bits[dummy] & 1));
    for (unsigned byte = 0; byte < 8; byte++) {
        unsigned value = 0;
        for (unsigned bit = 0; bit < 8; bit++) value = value * 2 + (bits[4 + byte * 8 + bit] & 1);
        CHECK(value == payload[byte]);
    }
    for (unsigned offset = 0; offset < 512; offset++)
        CHECK(gba->memory.savedata.data[offset] == (offset >= 56 && offset < 64 ? payload[offset - 56] : 0xFF));
    CHECK(gba->memory.savedata.dirty && gba->cartridge.tilt.x == 0x123 && gba->cartridge.tilt.y == 0xABC);
    destroy(gba);
}

struct InputChange { struct CupidGBA* gba; unsigned calls; uint64_t clock; };
static void change_input(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct InputChange* change = context;
    change->calls++;
    change->clock = cupid_timing_global_time(timing);
    CHECK(cupid_cartridge_set_tilt_sample(change->gba, 0xABC, 0xDEF));
}

static void command_capture_uses_its_cpu_strobe_across_late_dispatch_and_wrap(void) {
    const uint64_t epochs[] = {0, INT32_MAX - 6u, UINT32_MAX - 6u, UINT64_C(0x200000100)};
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned offset = 9; offset <= 11; offset++) {
            struct CupidGBA* gba = machine("KYGE");
            CHECK(cupid_cartridge_set_tilt_sample(gba, 0x123, 0x456));
            cupid_memory_store8(&gba->cpu, 0x0E008000, 0x55, NULL);
            cupid_timing_clear(&gba->timing);
            gba->timing.globalCycles = epochs[epoch];
            gba->timing.masterCycles = (uint32_t)epochs[epoch];
            gba->cpu.cycles = 0;
            struct InputChange change = {.gba = gba};
            struct CupidTimingEvent event = {.context = &change, .callback = change_input, .priority = 0x60};
            cupid_timing_schedule(&gba->timing, &event, (int32_t)offset);
            access(gba, thumb != 0, 1, 0x0E008100, 0xAA, false);
            CHECK(cupid_timing_global_time(&gba->timing) == epochs[epoch] + 10);
            CHECK(gba->cartridge.tilt.x == (offset <= 10 ? 0xABC : 0x123));
            CHECK(gba->cartridge.tilt.y == (offset <= 10 ? 0xDEF : 0x456));
            cupid_timing_tick(&gba->timing, 16);
            CHECK(change.calls == 1 && change.clock == epochs[epoch] + offset);
            CHECK(gba->cartridge.tilt.inputX == 0xABC && gba->cartridge.tilt.inputY == 0xDEF);
            CHECK(gba->cartridge.tilt.x == (offset <= 10 ? 0xABC : 0x123));
            destroy(gba);
        }
}

int main(void) {
    RUN(complete_axis_range_and_retained_snapshots);
    RUN(command_pair_and_reset_preserve_input_ownership);
    RUN(cpu_widths_and_save_windows_keep_the_complete_bytes);
    RUN(storage_gpio_and_lifecycle_stay_independent);
    RUN(board_wiring_wins_over_unused_library_signatures);
    RUN(eeprom_driver_and_sensor_commands_preserve_each_others_state);
    RUN(command_capture_uses_its_cpu_strobe_across_late_dispatch_and_wrap);
    return 0;
}
