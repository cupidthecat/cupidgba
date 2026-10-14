/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "cupid/gba.h"
#include "cupid/file.h"
#include "../../test.h"
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

static char directory[1024], statePath[1100], savePath[1100];
static uint8_t valid[44];

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    const uint8_t image[] = "test cartridge SIIRTC_V001";
    cupid_cartridge_attach(gba, image, sizeof(image));
    CHECK(gba->cartridge.rtcPresent);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void write_bytes(const void* data, size_t size) {
    FILE* file = fopen(statePath, "wb");
    CHECK(file && fwrite(data, 1, size, file) == size && fclose(file) == 0);
}

static void repair_checksum(uint8_t data[44]) {
    uint32_t sum = 2166136261u;
    for (unsigned i = 0; i < 40; i++) sum = (sum ^ data[i]) * 16777619u;
    for (unsigned i = 0; i < 4; i++) data[40 + i] = (uint8_t)(sum >> (8 * i));
}

static void reject_without_changing_battery(const uint8_t* data, size_t size) {
    write_bytes(data, size);
    struct CupidGBA* gba = machine();
    const struct CupidCartridge before = gba->cartridge;
    uint64_t stamp = 7;
    CHECK(!cupid_cartridge_load_rtc(gba, statePath, &stamp) && errno == EINVAL);
    CHECK(stamp == 7 && !memcmp(&before, &gba->cartridge, sizeof(before)));
    destroy(gba);
}

static void roundtrip_uses_a_separate_bound_battery_and_clock_anchor(void) {
    const uint8_t rawSave[] = {0x12, 0x34, 0xA5, 0x5A, 0xFF};
    CHECK(cupid_file_replace(savePath, rawSave, sizeof(rawSave)));
    struct CupidGBA* gba = machine();
    const struct CupidRTCDateTime date = {26, 10, 13, 5, 23, 59, 59};
    CHECK(cupid_rtc_set_datetime(&gba->cartridge.rtc, &date));
    cupid_rtc_write_control(&gba->cartridge.rtc, 0x4A);
    cupid_rtc_sample_power(&gba->cartridge.rtc, false);
    cupid_timing_tick(&gba->timing, CUPID_RTC_CLOCK_HZ / 2);
    CHECK(cupid_cartridge_save_rtc(gba, statePath, UINT64_C(0x1122334455667788)));
    CHECK(cupid_file_read_exact(statePath, valid, sizeof(valid)));
    CHECK(!memcmp(valid, "CPDRTC01", 8) && valid[16] == 26 && valid[19] == 5 && valid[23] == 0xCA);
    destroy(gba);
    gba = machine();
    cupid_timing_tick(&gba->timing, 1234);
    uint64_t stamp = 0;
    CHECK(cupid_cartridge_load_rtc(gba, statePath, &stamp));
    CHECK(stamp == UINT64_C(0x1122334455667788) && gba->cartridge.rtc.lastClock == 1234);
    CHECK(!memcmp(&gba->cartridge.rtc.date, &date, sizeof(date)));
    CHECK(gba->cartridge.rtc.control == 0xCA && !gba->cartridge.rtc.powerSampling);
    CHECK(gba->cartridge.rtc.fraction == CUPID_RTC_CLOCK_HZ / 2);
    cupid_timing_tick(&gba->timing, CUPID_RTC_CLOCK_HZ / 2);
    cupid_rtc_sync(&gba->cartridge.rtc, cupid_timing_global_time(&gba->timing));
    CHECK(gba->cartridge.rtc.date.day == 14 && gba->cartridge.rtc.date.weekday == 6);
    CHECK(gba->cartridge.rtc.date.hour == 0 && gba->cartridge.rtc.date.second == 0);
    uint8_t readSave[sizeof(rawSave)];
    CHECK(cupid_file_read_exact(savePath, readSave, sizeof(readSave)) && !memcmp(rawSave, readSave, sizeof(rawSave)));
    gba->cartridge.identity ^= 1;
    const struct CupidCartridge before = gba->cartridge;
    CHECK(!cupid_cartridge_load_rtc(gba, statePath, &stamp) && errno == EINVAL);
    CHECK(!memcmp(&before, &gba->cartridge, sizeof(before)));
    destroy(gba);
}

static void corrupt_or_impossible_state_never_changes_the_running_clock(void) {
    for (unsigned size = 0; size < sizeof(valid); size++) reject_without_changing_battery(valid, size);
    uint8_t longer[45];
    memcpy(longer, valid, sizeof(valid)); longer[44] = 0;
    reject_without_changing_battery(longer, sizeof(longer));
    for (unsigned i = 0; i < sizeof(valid); i++) {
        uint8_t corrupt[44];
        memcpy(corrupt, valid, sizeof(corrupt)); corrupt[i] ^= 1;
        reject_without_changing_battery(corrupt, sizeof(corrupt));
    }
    const struct {unsigned at; uint8_t value;} impossible[] = {
        {16,100}, {17,0}, {17,13}, {18,32}, {19,7}, {20,24}, {21,60}, {22,60},
        {23,0xCB}, {27,1}, {28,8}, {29,1}, {30,1}, {31,1}
    };
    for (unsigned i = 0; i < sizeof(impossible) / sizeof(*impossible); i++) {
        uint8_t data[44]; memcpy(data, valid, sizeof(data));
        data[impossible[i].at] = impossible[i].value;
        repair_checksum(data);
        reject_without_changing_battery(data, sizeof(data));
    }
    uint8_t data[44]; memcpy(data, valid, sizeof(data));
    data[17] = 2; data[18] = 30; repair_checksum(data);
    reject_without_changing_battery(data, sizeof(data));
    memcpy(data, valid, sizeof(data)); data[28] |= 1; data[22] = 0x59; repair_checksum(data);
    reject_without_changing_battery(data, sizeof(data));
    memcpy(data, valid, sizeof(data)); data[28] |= 4; data[23] &= (uint8_t)~8; repair_checksum(data);
    reject_without_changing_battery(data, sizeof(data));
}

static void minute_output_latch_roundtrips_without_an_extra_irq(void) {
    for (unsigned steady = 0; steady < 2; steady++) {
        struct CupidGBA* gba = machine();
        cupid_rtc_write_control(&gba->cartridge.rtc, steady ? 0x4A : 0x48);
        const struct CupidRTCDateTime date = {26, 10, 13, 2, 12, 34, 29};
        CHECK(cupid_rtc_set_datetime(&gba->cartridge.rtc, &date));
        gba->cartridge.rtc.interruptLow = true;
        gba->cartridge.rtc.fraction = CUPID_RTC_CLOCK_HZ - 12;
        CHECK(cupid_cartridge_save_rtc(gba, statePath, 0));
        uint8_t data[44];
        CHECK(cupid_file_read_exact(statePath, data, sizeof(data)) && (data[28] & 4));
        destroy(gba);
        gba = machine();
        CHECK(cupid_cartridge_load_rtc(gba, statePath, NULL));
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent) == (steady != 0));
        cupid_timing_tick(&gba->timing, 11);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->cartridge.rtc.interruptLow == (steady == 0));
        CHECK(gba->irq.pendingIF == (steady ? IRQ_GAMEPAK : 0));
        destroy(gba);
    }
}

static void extended_interrupt_state_roundtrips_and_validates_both_versions(void) {
    for (unsigned mode = 0; mode < 3; mode++) {
        struct CupidGBA* gba = machine();
        const struct CupidRTCDateTime date = {26, 10, 13, 2, 12, 34, 0};
        CHECK(cupid_rtc_set_datetime(&gba->cartridge.rtc, &date));
        const uint16_t parameters = mode == 1 ? 0x8000 : 0x3492;
        cupid_rtc_write_interrupt_parameters(&gba->cartridge.rtc, parameters);
        cupid_rtc_write_control(&gba->cartridge.rtc, mode == 1 ? 0x42 : mode == 2 ? 0x60 : 0x40);
        if (mode == 1) gba->cartridge.rtc.fraction = CUPID_RTC_CLOCK_HZ / 2 - 12;
        CHECK(cupid_cartridge_save_rtc(gba, statePath, 1234));
        uint8_t data[44];
        CHECK(cupid_file_read_exact(statePath, data, sizeof(data)));
        CHECK(!memcmp(data, "CPDRTC02", 8) && data[29] == (uint8_t)parameters && data[30] == parameters >> 8);
        CHECK(((data[28] & 4) != 0) == (mode != 0) && !data[31]);
        destroy(gba);
        gba = machine();
        uint64_t stamp = 0;
        CHECK(cupid_cartridge_load_rtc(gba, statePath, &stamp));
        CHECK(stamp == 1234 && gba->cartridge.rtc.interruptParameters == parameters);
        CHECK(gba->cartridge.rtc.interruptLow == (mode != 0) && !gba->irq.pendingIF && !gba->irqf);
        CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent) == (mode == 1));
        if (mode == 1) {
            CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) == 12);
            cupid_timing_tick(&gba->timing, 11);
            CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
            cupid_timing_tick(&gba->timing, 1);
            CHECK(!gba->cartridge.rtc.interruptLow && gba->irq.pendingIF == IRQ_GAMEPAK);
        }
        destroy(gba);
        const unsigned invalidAt[] = {7, 28, 31};
        const uint8_t invalidValue[] = {'3', 8, 1};
        for (unsigned i = 0; i < 3; i++) {
            uint8_t bad[44]; memcpy(bad, data, sizeof(bad));
            bad[invalidAt[i]] = invalidValue[i]; repair_checksum(bad);
            reject_without_changing_battery(bad, sizeof(bad));
        }
        memcpy(data, "CPDRTC01", 8); repair_checksum(data);
        reject_without_changing_battery(data, sizeof(data)); /* v1 reserves parameters. */
    }
    uint8_t data[44]; memcpy(data, valid, sizeof(data));
    memcpy(data, "CPDRTC02", 8); data[23] = 0x40; data[28] |= 4; repair_checksum(data);
    reject_without_changing_battery(data, sizeof(data));
}

static void invalid_second_and_power_latch_survive_until_the_next_carry(void) {
    struct CupidGBA* gba = machine();
    const uint8_t time[] = {0x23, 0x59, 0x6A};
    cupid_rtc_write_time(&gba->cartridge.rtc, time);
    cupid_rtc_sample_power(&gba->cartridge.rtc, false);
    CHECK(cupid_cartridge_save_rtc(gba, statePath, 0));
    destroy(gba); gba = machine();
    CHECK(cupid_cartridge_load_rtc(gba, statePath, NULL));
    CHECK(gba->cartridge.rtc.invalidSecond && gba->cartridge.rtc.date.second == 0x6A);
    CHECK((gba->cartridge.rtc.control & 0x80) && !gba->cartridge.rtc.powerSampling);
    cupid_rtc_advance_seconds(&gba->cartridge.rtc, 1);
    CHECK(!gba->cartridge.rtc.invalidSecond && gba->cartridge.rtc.date.day == 2);
    CHECK(gba->cartridge.rtc.date.hour == 0 && gba->cartridge.rtc.date.second == 0);
    destroy(gba);
}

static void failed_save_preserves_the_existing_file(void) {
    write_bytes(valid, sizeof(valid));
#ifdef _WIN32
    CHECK(SetFileAttributesA(statePath, FILE_ATTRIBUTE_READONLY));
#else
    CHECK(chmod(statePath, 0400) == 0);
#endif
    struct CupidGBA* gba = machine();
    CHECK(!cupid_cartridge_save_rtc(gba, statePath, 0));
    uint8_t actual[44];
    CHECK(cupid_file_read_exact(statePath, actual, sizeof(actual)) && !memcmp(actual, valid, sizeof(valid)));
#ifdef _WIN32
    CHECK(SetFileAttributesA(statePath, FILE_ATTRIBUTE_NORMAL));
#else
    CHECK(chmod(statePath, 0600) == 0);
#endif
    CHECK(!cupid_cartridge_load_rtc(gba, directory, NULL));
    CHECK(!cupid_cartridge_save_rtc(gba, directory, 0));
    cupid_cartridge_enable_rtc(gba, false);
    CHECK(cupid_cartridge_load_rtc(gba, directory, NULL));
    CHECK(cupid_cartridge_save_rtc(gba, directory, 0));
    destroy(gba);
}

static void live_stop_clock_roundtrips_without_replaying_its_pending_request(void) {
    struct CupidGBA* gba = machine();
    cupid_gba_stop(gba);
    CHECK(gba->stopped);
    const struct CupidRTCDateTime date = {26, 10, 13, 2, 23, 59, 59};
    CHECK(cupid_rtc_set_datetime(&gba->cartridge.rtc, &date));
    cupid_rtc_write_interrupt_parameters(&gba->cartridge.rtc, 1);
    cupid_rtc_write_control(&gba->cartridge.rtc, 0x42);
    cupid_cartridge_rtc_reschedule(gba);
    const uint64_t interval = 2 * CUPID_RTC_CLOCK_HZ + 123;
    CHECK(cupid_gba_advance_stopped_time(gba, interval) == interval);
    CHECK(gba->stopped && gba->irq.pendingIF == IRQ_GAMEPAK && !gba->irqf);
    CHECK(gba->cartridge.rtc.date.day == 14 && gba->cartridge.rtc.date.second == 1);
    CHECK(gba->cartridge.rtc.fraction == 123);
    CHECK(cupid_cartridge_save_rtc(gba, statePath, 456));
    destroy(gba);
    gba = machine();
    uint64_t stamp = 0;
    CHECK(cupid_cartridge_load_rtc(gba, statePath, &stamp));
    CHECK(stamp == 456 && gba->cartridge.rtc.date.day == 14 && gba->cartridge.rtc.date.second == 1);
    CHECK(gba->cartridge.rtc.fraction == 123 && gba->cartridge.rtc.interruptParameters == 1);
    CHECK(!gba->stopped && !gba->irq.pendingIF && !gba->irqf);
    CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) == 133);
    cupid_timing_tick(&gba->timing, 132);
    CHECK(!gba->irq.pendingIF);
    cupid_timing_tick(&gba->timing, 1);
    CHECK(gba->irq.pendingIF == IRQ_GAMEPAK);
    destroy(gba);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
#ifdef _WIN32
    unsigned long process = GetCurrentProcessId();
#else
    unsigned long process = (unsigned long)getpid();
#endif
    unsigned attempt = 0;
    for (; attempt < 100; attempt++) {
        int size = snprintf(directory, sizeof(directory), "%s/rtc-battery-%lu-%u", argv[1], process, attempt);
        CHECK(size > 0 && (size_t)size < sizeof(directory));
#ifdef _WIN32
        if (_mkdir(directory) == 0) break;
#else
        if (mkdir(directory, 0700) == 0) break;
#endif
        CHECK(errno == EEXIST);
    }
    CHECK(attempt < 100);
    CHECK(snprintf(statePath, sizeof(statePath), "%s/input.rtc", directory) > 0);
    CHECK(snprintf(savePath, sizeof(savePath), "%s/input.sav", directory) > 0);
    RUN(roundtrip_uses_a_separate_bound_battery_and_clock_anchor);
    RUN(corrupt_or_impossible_state_never_changes_the_running_clock);
    RUN(invalid_second_and_power_latch_survive_until_the_next_carry);
    RUN(failed_save_preserves_the_existing_file);
    RUN(minute_output_latch_roundtrips_without_an_extra_irq);
    RUN(extended_interrupt_state_roundtrips_and_validates_both_versions);
    RUN(live_stop_clock_roundtrips_without_replaying_its_pending_request);
    CHECK(remove(statePath) == 0 && remove(savePath) == 0);
#ifdef _WIN32
    CHECK(_rmdir(directory) == 0);
#else
    CHECK(rmdir(directory) == 0);
#endif
    return 0;
}
