/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/platform/rtc.h"
#include "../test.h"
#include <errno.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

static char path[1100];

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_cartridge_enable_rtc(gba, true);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void first_launch_and_explicit_seed_are_deterministic(void) {
    struct CupidGBA* gba = machine();
    struct CupidPlatformRTC config = {.path = path};
    CHECK(cupid_platform_rtc_load(gba, &config, NULL, 987654321));
    CHECK(gba->cartridge.rtc.date.year == 0 && gba->cartridge.rtc.date.weekday == 6 && config.hostAnchor == 0);
    const struct CupidRTCDateTime seed = {26, 10, 13, 5, 23, 59, 59};
    config.seed = &seed;
    CHECK(cupid_platform_rtc_load(gba, &config, NULL, 987654321));
    CHECK(!memcmp(&seed, &gba->cartridge.rtc.date, sizeof(seed)));
    cupid_timing_tick(&gba->timing, CUPID_RTC_CLOCK_HZ / 2);
    CHECK(cupid_platform_rtc_save(gba, &config, 987654321));
    destroy(gba); gba = machine();
    config.seed = NULL;
    CHECK(cupid_platform_rtc_load(gba, &config, NULL, UINT64_MAX));
    CHECK(!memcmp(&seed, &gba->cartridge.rtc.date, sizeof(seed)));
    CHECK(gba->cartridge.rtc.fraction == CUPID_RTC_CLOCK_HZ / 2 && config.hostAnchor == 0);
    destroy(gba);
    CHECK(remove(path) == 0);
}

static void offline_elapsed_time_keeps_game_clock_offset_and_weekday(void) {
    struct CupidGBA* gba = machine();
    const struct CupidRTCDateTime seed = {26, 10, 13, 5, 23, 59, 59};
    struct CupidPlatformRTC config = {.path = path, .offlineWall = true};
    CHECK(cupid_platform_rtc_load(gba, &config, &seed, 1000));
    CHECK(config.hostAnchor == 1000);
    cupid_timing_tick(&gba->timing, CUPID_RTC_CLOCK_HZ / 2);
    CHECK(cupid_platform_rtc_save(gba, &config, 1000));
    destroy(gba); gba = machine();
    CHECK(cupid_platform_rtc_load(gba, &config, NULL, 1002));
    CHECK(gba->cartridge.rtc.date.day == 14 && gba->cartridge.rtc.date.weekday == 6);
    CHECK(gba->cartridge.rtc.date.hour == 0 && gba->cartridge.rtc.date.second == 1);
    CHECK(gba->cartridge.rtc.fraction == CUPID_RTC_CLOCK_HZ / 2);
    CHECK(cupid_platform_rtc_save(gba, &config, 1002));
    destroy(gba); gba = machine();
    CHECK(cupid_platform_rtc_load(gba, &config, NULL, 900));
    CHECK(gba->cartridge.rtc.date.second == 1 && config.hostAnchor == 1002);
    CHECK(cupid_platform_rtc_save(gba, &config, 901));
    uint64_t stamp = 0;
    CHECK(cupid_cartridge_load_rtc(gba, path, &stamp) && stamp == 1002);
    config.seed = &seed;
    CHECK(cupid_platform_rtc_load(gba, &config, NULL, 5000));
    CHECK(!memcmp(&seed, &gba->cartridge.rtc.date, sizeof(seed)) && gba->cartridge.rtc.fraction == 0);
    config.seed = NULL; config.offlineWall = false;
    CHECK(cupid_platform_rtc_save(gba, &config, 5000));
    config.offlineWall = true;
    CHECK(cupid_platform_rtc_load(gba, &config, NULL, UINT64_MAX));
    CHECK(!memcmp(&seed, &gba->cartridge.rtc.date, sizeof(seed)));
    destroy(gba);
    CHECK(remove(path) == 0);
}

static void invalid_input_preserves_state_and_disabled_hardware_has_no_file_io(void) {
    struct CupidGBA* gba = machine();
    const struct CupidRTCDateTime seed = {26, 2, 30, 5, 0, 0, 0};
    struct CupidPlatformRTC config = {.path = path, .seed = &seed};
    const struct CupidCartridge before = gba->cartridge;
    CHECK(!cupid_platform_rtc_load(gba, &config, NULL, 0) && errno == EINVAL);
    CHECK(!memcmp(&before, &gba->cartridge, sizeof(before)));
    config.seed = NULL;
    FILE* file = fopen(path, "wb");
    CHECK(file && fputs("invalid clock", file) >= 0 && fclose(file) == 0);
    CHECK(!cupid_platform_rtc_load(gba, &config, NULL, 0) && errno == EINVAL);
    CHECK(!memcmp(&before, &gba->cartridge, sizeof(before)));
    cupid_cartridge_enable_rtc(gba, false);
    CHECK(cupid_platform_rtc_load(gba, &config, NULL, 0));
    CHECK(cupid_platform_rtc_save(gba, &config, 0));
    char actual[14] = {0}; file = fopen(path, "rb");
    CHECK(file && fread(actual, 1, 13, file) == 13 && fclose(file) == 0);
    CHECK(!strcmp(actual, "invalid clock"));
    destroy(gba); CHECK(remove(path) == 0);
}

static void offline_output_transitions_do_not_replay_console_interrupts(void) {
    struct CupidGBA* gba = machine();
    const struct CupidRTCDateTime seed = {26, 10, 13, 2, 0, 0, 29};
    struct CupidPlatformRTC config = {.path = path, .offlineWall = true};
    CHECK(cupid_platform_rtc_load(gba, &config, &seed, 1000));
    cupid_rtc_write_control(&gba->cartridge.rtc, 0x4A);
    gba->cartridge.rtc.minuteLow = true;
    gba->cartridge.rtc.fraction = CUPID_RTC_CLOCK_HZ / 2;
    CHECK(cupid_platform_rtc_save(gba, &config, 1000));
    destroy(gba);
    gba = machine();
    CHECK(cupid_platform_rtc_load(gba, &config, NULL, 1002));
    CHECK(gba->cartridge.rtc.date.second == 31 && !gba->cartridge.rtc.minuteLow);
    CHECK(!gba->irqf && !gba->irq.pendingIF);
    CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) ==
          29 * CUPID_RTC_CLOCK_HZ - CUPID_RTC_CLOCK_HZ / 2);
    destroy(gba);
    CHECK(remove(path) == 0);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
#ifdef _WIN32
    unsigned long process = GetCurrentProcessId();
#else
    unsigned long process = (unsigned long)getpid();
#endif
    int size = snprintf(path, sizeof(path), "%s/rtc-config-%lu.rtc", argv[1], process);
    CHECK(size > 0 && (size_t)size < sizeof(path));
    FILE* previous = fopen(path, "rb");
    CHECK(!previous && errno == ENOENT);
    RUN(first_launch_and_explicit_seed_are_deterministic);
    RUN(offline_elapsed_time_keeps_game_clock_offset_and_weekday);
    RUN(invalid_input_preserves_state_and_disabled_hardware_has_no_file_io);
    RUN(offline_output_transitions_do_not_replay_console_interrupts);
    return 0;
}
