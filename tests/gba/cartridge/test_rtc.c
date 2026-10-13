/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/rtc.h"
#include "../../test.h"

static void formats_use_the_hour_flag_and_independent_weekday(void) {
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, 0);
    for (unsigned hour = 0; hour < 24; hour++) {
        for (unsigned mode = 0; mode < 2; mode++) {
            struct CupidRTCDateTime date = {26, 10, 13, 5, (uint8_t)hour, 59, 37};
            CHECK(cupid_rtc_set_datetime(&rtc, &date));
            cupid_rtc_write_control(&rtc, mode ? 0x40 : 0);
            uint8_t data[7];
            cupid_rtc_read_datetime(&rtc, data);
            CHECK(data[0] == 0x26 && data[1] == 0x10 && data[2] == 0x13 && data[3] == 5);
            unsigned value = mode ? hour : hour % 12;
            CHECK(data[4] == ((value / 10) * 16 + value % 10 + (hour >= 12 ? 0x80 : 0)));
            CHECK(data[5] == 0x59 && data[6] == 0x37);
            cupid_rtc_write_datetime(&rtc, data);
            CHECK(!memcmp(&rtc.date, &date, sizeof(date)));
        }
    }
}

static void midnight_month_leap_and_year_boundaries(void) {
    const uint8_t dates[][6] = {
        {0, 2, 28, 0, 2, 29}, {0, 2, 29, 0, 3, 1},
        {1, 2, 28, 1, 3, 1}, {4, 2, 28, 4, 2, 29},
        {96, 2, 29, 96, 3, 1}, {99, 12, 31, 0, 1, 1},
        {26, 4, 30, 26, 5, 1}, {26, 1, 31, 26, 2, 1}
    };
    for (unsigned i = 0; i < sizeof(dates) / sizeof(dates[0]); i++) {
        struct CupidRTC rtc;
        cupid_rtc_init(&rtc, 0);
        const struct CupidRTCDateTime date = {dates[i][0], dates[i][1], dates[i][2], 6, 23, 59, 59};
        CHECK(cupid_rtc_set_datetime(&rtc, &date));
        cupid_rtc_sync(&rtc, CUPID_RTC_CLOCK_HZ - 1);
        CHECK(!memcmp(&rtc.date, &date, sizeof(date)));
        cupid_rtc_sync(&rtc, CUPID_RTC_CLOCK_HZ);
        CHECK(rtc.date.year == dates[i][3] && rtc.date.month == dates[i][4] && rtc.date.day == dates[i][5]);
        CHECK(rtc.date.weekday == 0 && rtc.date.hour == 0 && rtc.date.minute == 0 && rtc.date.second == 0);
    }
}

static void fractional_clocks_reset_and_large_advance(void) {
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, UINT64_C(0xFFFFFFF0));
    cupid_rtc_sync(&rtc, UINT64_C(0xFFFFFFF0) + CUPID_RTC_CLOCK_HZ / 2);
    CHECK(rtc.date.second == 0 && rtc.fraction == CUPID_RTC_CLOCK_HZ / 2);
    cupid_rtc_rebase(&rtc, 0);
    cupid_rtc_sync(&rtc, CUPID_RTC_CLOCK_HZ / 2);
    CHECK(rtc.date.second == 1 && rtc.fraction == 0);
    const struct CupidRTCDateTime date = {0, 1, 1, 0, 0, 0, 0};
    CHECK(cupid_rtc_set_datetime(&rtc, &date));
    cupid_rtc_rebase(&rtc, 0);
    cupid_rtc_sync(&rtc, UINT64_C(36525) * 86400 * CUPID_RTC_CLOCK_HZ);
    CHECK(rtc.date.year == 0 && rtc.date.month == 1 && rtc.date.day == 1 && rtc.date.weekday == 6);
    CHECK(rtc.date.hour == 0 && rtc.date.minute == 0 && rtc.date.second == 0);
    cupid_rtc_rebase(&rtc, 0);
    cupid_rtc_sync(&rtc, UINT64_MAX);
    CHECK(rtc.date.month >= 1 && rtc.date.month <= 12 && rtc.date.day >= 1 && rtc.date.day <= 31);
    CHECK(rtc.date.hour <= 23 && rtc.date.minute <= 59 && rtc.date.second <= 59);
}

static void field_correction_and_delayed_second_carry(void) {
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, 0);
    const uint8_t invalid[] = {0xFA, 0, 0x32, 7, 0x2A, 0x6F, 0x6A};
    cupid_rtc_write_datetime(&rtc, invalid);
    uint8_t read[7];
    cupid_rtc_read_datetime(&rtc, read);
    const uint8_t corrected[] = {0, 1, 1, 0, 0, 0, 0x6A};
    CHECK(!memcmp(read, corrected, sizeof(read)));
    cupid_rtc_sync(&rtc, CUPID_RTC_CLOCK_HZ - 1);
    CHECK(rtc.invalidSecond);
    cupid_rtc_sync(&rtc, CUPID_RTC_CLOCK_HZ);
    CHECK(!rtc.invalidSecond && rtc.date.second == 0 && rtc.date.minute == 1);
    const uint8_t february[] = {1, 2, 0x30, 3, 0x12, 0x34, 0x56};
    cupid_rtc_write_datetime(&rtc, february);
    CHECK(rtc.date.year == 1 && rtc.date.month == 3 && rtc.date.day == 1 && rtc.date.weekday == 3);
    const struct CupidRTCDateTime before = rtc.date;
    struct CupidRTCDateTime bad = before;
    bad.month = 0;
    CHECK(!cupid_rtc_set_datetime(&rtc, &bad));
    CHECK(!memcmp(&rtc.date, &before, sizeof(before)));
}

static void control_masks_readonly_power_and_force_reset(void) {
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, 0);
    cupid_rtc_sample_power(&rtc, false);
    CHECK(!rtc.powerSampling);
    cupid_rtc_sample_power(&rtc, true);
    CHECK(rtc.control == 0xC0);
    cupid_rtc_write_control(&rtc, 0);
    CHECK(rtc.control == 0x80);
    CHECK(cupid_rtc_read_control(&rtc) == 0x80);
    CHECK(rtc.powerSampling);
    CHECK(cupid_rtc_read_control(&rtc) == 0x80);
    cupid_rtc_sample_power(&rtc, true);
    CHECK(cupid_rtc_read_control(&rtc) == 0);
    cupid_rtc_write_control(&rtc, 0xFF);
    CHECK(cupid_rtc_read_control(&rtc) == 0x6A);
    cupid_rtc_sample_power(&rtc, false);
    CHECK(!rtc.powerSampling);
    cupid_rtc_reset_registers(&rtc);
    const struct CupidRTCDateTime reset = {0, 1, 1, 0, 0, 0, 0};
    CHECK(!memcmp(&rtc.date, &reset, sizeof(reset)));
    CHECK(rtc.control == 0 && rtc.fraction == 0 && !rtc.invalidSecond && rtc.powerSampling);
}

static unsigned drive(struct CupidRTC* rtc, uint8_t pins, uint64_t* clock) {
    *clock += 128;
    return cupid_rtc_write_pins(rtc, pins, *clock);
}

static void select_chip(struct CupidRTC* rtc, uint64_t* clock) {
    CHECK(!drive(rtc, 1, clock));
    CHECK(!drive(rtc, 5, clock));
}

static unsigned send_byte(struct CupidRTC* rtc, uint8_t data, bool msb, uint64_t* clock) {
    unsigned requests = 0;
    for (unsigned i = 0; i < 8; i++) {
        const uint8_t bit = (data >> (msb ? 7 - i : i)) & 1;
        const uint8_t pins = (uint8_t)(4 | bit << 1);
        requests += drive(rtc, pins | 1, clock);
        requests += drive(rtc, pins, clock);
        requests += drive(rtc, pins, clock); /* A repeated level is not another clock. */
        requests += drive(rtc, pins | 1, clock);
    }
    return requests;
}

static uint8_t read_byte(struct CupidRTC* rtc, uint64_t* clock) {
    uint8_t result = 0;
    for (unsigned i = 0; i < 8; i++) {
        CHECK(!drive(rtc, 4, clock));
        const uint8_t bit = (cupid_rtc_output_pins(rtc) >> 1) & 1;
        CHECK(!drive(rtc, 4, clock));
        CHECK(((cupid_rtc_output_pins(rtc) >> 1) & 1) == bit);
        CHECK(!drive(rtc, 5, clock));
        CHECK(((cupid_rtc_output_pins(rtc) >> 1) & 1) == bit);
        result |= (uint8_t)(bit << i);
    }
    return result;
}

static void wire_commands_payloads_and_both_command_representations(void) {
    for (unsigned msb = 0; msb < 2; msb++) {
        struct CupidRTC rtc;
        cupid_rtc_init(&rtc, 0);
        uint64_t clock = 0;
        select_chip(&rtc, &clock);
        CHECK(!send_byte(&rtc, msb ? 0x62 : 0x46, msb, &clock));
        CHECK(!send_byte(&rtc, 0x40, false, &clock));
        select_chip(&rtc, &clock);
        CHECK(!send_byte(&rtc, msb ? 0x63 : 0xC6, msb, &clock));
        CHECK(read_byte(&rtc, &clock) == 0x40);
        const uint8_t date[] = {0x26, 0x10, 0x13, 5, 0x93, 0x34, 0x56};
        select_chip(&rtc, &clock);
        CHECK(!send_byte(&rtc, msb ? 0x64 : 0x26, msb, &clock));
        for (unsigned i = 0; i < 7; i++) CHECK(!send_byte(&rtc, date[i], false, &clock));
        select_chip(&rtc, &clock);
        CHECK(!send_byte(&rtc, msb ? 0x65 : 0xA6, msb, &clock));
        for (unsigned i = 0; i < 7; i++) CHECK(read_byte(&rtc, &clock) == date[i]);
        const uint8_t time[] = {0xA3, 0x12, 0x34};
        select_chip(&rtc, &clock);
        CHECK(!send_byte(&rtc, 0x66, msb, &clock));
        for (unsigned i = 0; i < 3; i++) CHECK(!send_byte(&rtc, time[i], false, &clock));
        select_chip(&rtc, &clock);
        CHECK(!send_byte(&rtc, msb ? 0x67 : 0xE6, msb, &clock));
        for (unsigned i = 0; i < 3; i++) CHECK(read_byte(&rtc, &clock) == time[i]);
        CHECK(!drive(&rtc, 1, &clock));
        CHECK(cupid_rtc_output_pins(&rtc) == 2);
    }
}

static void wire_read_snapshot_survives_a_calendar_carry(void) {
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, 0);
    const struct CupidRTCDateTime date = {99, 12, 31, 0, 23, 59, 59};
    CHECK(cupid_rtc_set_datetime(&rtc, &date));
    uint64_t clock = CUPID_RTC_CLOCK_HZ - 6000;
    select_chip(&rtc, &clock);
    CHECK(!send_byte(&rtc, 0xA6, false, &clock));
    CHECK(clock < CUPID_RTC_CLOCK_HZ);
    const uint8_t old[] = {0x99, 0x12, 0x31, 0, 0xA3, 0x59, 0x59};
    for (unsigned i = 0; i < 7; i++) CHECK(read_byte(&rtc, &clock) == old[i]);
    CHECK(rtc.date.year == 0 && rtc.date.month == 1 && rtc.date.day == 1 && rtc.date.weekday == 1);
    select_chip(&rtc, &clock);
    CHECK(!send_byte(&rtc, 0xA6, false, &clock));
    const uint8_t next[] = {0, 1, 1, 1, 0, 0, 0};
    for (unsigned i = 0; i < 7; i++) CHECK(read_byte(&rtc, &clock) == next[i]);
}

static void wire_abort_reset_strobes_and_forced_requests(void) {
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, 0);
    uint64_t clock = 0;
    select_chip(&rtc, &clock);
    CHECK(!send_byte(&rtc, 0x26, false, &clock));
    CHECK(!send_byte(&rtc, 0x99, false, &clock));
    const struct CupidRTCDateTime before = rtc.date;
    CHECK(!drive(&rtc, 1, &clock));
    CHECK(!memcmp(&rtc.date, &before, sizeof(before)));
    for (unsigned read = 0; read < 2; read++) {
        select_chip(&rtc, &clock);
        CHECK(send_byte(&rtc, read ? 0xB6 : 0x36, false, &clock) == 1);
        CHECK(!drive(&rtc, 5, &clock));
        CHECK(!drive(&rtc, 5, &clock));
        select_chip(&rtc, &clock);
        CHECK(!send_byte(&rtc, read ? 0x86 : 0x06, false, &clock));
        CHECK(rtc.control == 0 && rtc.date.year == 0 && rtc.date.month == 1 && rtc.date.day == 1);
        CHECK(rtc.date.weekday == 0 && rtc.date.hour == 0 && rtc.date.minute == 0 && rtc.date.second == 0);
    }
    select_chip(&rtc, &clock);
    CHECK(!send_byte(&rtc, 0x12, false, &clock));
    CHECK(!send_byte(&rtc, 0xFF, false, &clock));
    CHECK(rtc.control == 0);
    for (unsigned command = 1; command < 8; command += 2) {
        if (command == 3) continue;
        select_chip(&rtc, &clock);
        CHECK(!send_byte(&rtc, (uint8_t)(0x86 | command << 4), false, &clock));
        CHECK(read_byte(&rtc, &clock) == 0xFF);
    }
}

static void explicit_clock_input_and_strict_calendar_seed(void) {
    struct CupidRTCDateTime date = {0};
    CHECK(cupid_rtc_parse_datetime("2026-10-13T12:34:56", &date));
    CHECK(date.year == 26 && date.month == 10 && date.day == 13 && date.weekday == 2);
    CHECK(date.hour == 12 && date.minute == 34 && date.second == 56);
    const struct CupidRTCDateTime before = date;
    const char* invalid[] = {"2025-02-29T00:00:00", "2100-01-01T00:00:00", "2000-01-01T24:00:00",
        "2000-01-01 00:00:00", "2000-01-01T00:00:00Z", "2000-01-01T00:00:60", "2000-00-01T00:00:00",
        "2000-01-32T00:00:00", "2000-0x-01T00:00:00", "2000-1-1T0:0:0", ""};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        CHECK(!cupid_rtc_parse_datetime(invalid[i], &date));
        CHECK(!memcmp(&date, &before, sizeof(date)));
    }
    CHECK(cupid_rtc_parse_datetime("2000-02-29T23:59:59", &date) && date.weekday == 2);
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, 123);
    CHECK(cupid_rtc_set_datetime(&rtc, &date));
    cupid_rtc_sync(&rtc, 123 + CUPID_RTC_CLOCK_HZ / 2);
    cupid_rtc_advance_seconds(&rtc, 1);
    CHECK(rtc.date.month == 3 && rtc.date.day == 1 && rtc.date.weekday == 3);
    CHECK(rtc.date.hour == 0 && rtc.date.minute == 0 && rtc.date.second == 0);
    CHECK(rtc.fraction == CUPID_RTC_CLOCK_HZ / 2);
    const uint64_t period = UINT64_C(36525) * 86400 * 7;
    struct CupidRTC bounded = rtc;
    cupid_rtc_advance_seconds(&rtc, UINT64_MAX);
    cupid_rtc_advance_seconds(&bounded, UINT64_MAX % period);
    CHECK(!memcmp(&rtc.date, &bounded.date, sizeof(rtc.date)));
    CHECK(rtc.fraction == bounded.fraction);
}

static void minute_output_latches_and_steady_release(void) {
    for (unsigned mode = 0; mode < 4; mode++) {
        struct CupidRTC rtc;
        cupid_rtc_init(&rtc, 0);
        const struct CupidRTCDateTime date = {26, 10, 13, 2, 12, 34, 59};
        CHECK(cupid_rtc_set_datetime(&rtc, &date));
        cupid_rtc_write_control(&rtc, (uint8_t)(0x40 | (mode & 1 ? 8 : 0) | (mode & 2 ? 2 : 0)));
        CHECK(cupid_rtc_next_interrupt(&rtc) == (mode & 1 ? CUPID_RTC_CLOCK_HZ : 0));
        cupid_rtc_sync(&rtc, CUPID_RTC_CLOCK_HZ - 1);
        CHECK(!rtc.minuteLow);
        cupid_rtc_sync(&rtc, CUPID_RTC_CLOCK_HZ);
        CHECK(rtc.minuteLow == ((mode & 1) != 0));
        CHECK(rtc.date.minute == 35 && rtc.date.second == 0);
        CHECK(cupid_rtc_next_interrupt(&rtc) == (mode == 3 ? 30 * CUPID_RTC_CLOCK_HZ : 0));
        cupid_rtc_sync(&rtc, 31 * CUPID_RTC_CLOCK_HZ - 1);
        CHECK(rtc.minuteLow == ((mode & 1) != 0));
        cupid_rtc_sync(&rtc, 31 * CUPID_RTC_CLOCK_HZ);
        CHECK(rtc.minuteLow == (mode == 1));
        if (mode == 3) CHECK(cupid_rtc_next_interrupt(&rtc) == 30 * CUPID_RTC_CLOCK_HZ);
        cupid_rtc_write_control(&rtc, rtc.control);
        CHECK(!rtc.minuteLow);
        if (mode & 1) CHECK(cupid_rtc_next_interrupt(&rtc) == 30 * CUPID_RTC_CLOCK_HZ);
        cupid_rtc_sync(&rtc, 61 * CUPID_RTC_CLOCK_HZ);
        CHECK(rtc.minuteLow == ((mode & 1) != 0));
        cupid_rtc_reset_registers(&rtc);
        CHECK(!rtc.minuteLow && !cupid_rtc_next_interrupt(&rtc));
    }
}

static void minute_output_bulk_clock_matches_each_second(void) {
    for (unsigned second = 0; second < 60; second++) {
        for (unsigned steady = 0; steady < 2; steady++) for (unsigned held = 0; held < 2; held++) {
            struct CupidRTC bulk;
            cupid_rtc_init(&bulk, UINT64_C(0xFFFFFFF0));
            const struct CupidRTCDateTime date = {99, 12, 31, 6, 23, 59, (uint8_t)second};
            CHECK(cupid_rtc_set_datetime(&bulk, &date));
            cupid_rtc_write_control(&bulk, steady ? 0x4A : 0x48);
            bulk.minuteLow = held != 0;
            bulk.fraction = CUPID_RTC_CLOCK_HZ / 3;
            struct CupidRTC each = bulk;
            const uint64_t end = bulk.lastClock + 183 * CUPID_RTC_CLOCK_HZ;
            cupid_rtc_sync(&bulk, end);
            for (unsigned i = 1; i <= 183; i++)
                cupid_rtc_sync(&each, UINT64_C(0xFFFFFFF0) + i * CUPID_RTC_CLOCK_HZ);
            CHECK(!memcmp(&bulk.date, &each.date, sizeof(bulk.date)));
            CHECK(bulk.minuteLow == each.minuteLow && bulk.fraction == each.fraction);
            CHECK(cupid_rtc_next_interrupt(&bulk) == cupid_rtc_next_interrupt(&each));
        }
    }
}

static void invalid_seconds_offline_clock_and_rebase_keep_the_minute_latch(void) {
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, 0);
    cupid_rtc_write_control(&rtc, 0x48);
    const uint8_t invalid[] = {0x23, 0x59, 0x6A};
    cupid_rtc_write_time(&rtc, invalid);
    CHECK(cupid_rtc_next_interrupt(&rtc) == CUPID_RTC_CLOCK_HZ);
    cupid_rtc_sync(&rtc, CUPID_RTC_CLOCK_HZ - 1);
    CHECK(!rtc.minuteLow);
    cupid_rtc_sync(&rtc, CUPID_RTC_CLOCK_HZ);
    CHECK(rtc.minuteLow && !cupid_rtc_next_interrupt(&rtc));
    cupid_rtc_rebase(&rtc, 0);
    cupid_rtc_sync(&rtc, 17);
    CHECK(rtc.minuteLow && rtc.fraction == 17);
    cupid_rtc_write_control(&rtc, 0x48);
    cupid_rtc_advance_seconds(&rtc, UINT64_MAX);
    CHECK(rtc.minuteLow && rtc.fraction == 17);
}

int main(void) {
    RUN(minute_output_latches_and_steady_release);
    RUN(minute_output_bulk_clock_matches_each_second);
    RUN(invalid_seconds_offline_clock_and_rebase_keep_the_minute_latch);
    RUN(explicit_clock_input_and_strict_calendar_seed);
    RUN(formats_use_the_hour_flag_and_independent_weekday);
    RUN(midnight_month_leap_and_year_boundaries);
    RUN(fractional_clocks_reset_and_large_advance);
    RUN(field_correction_and_delayed_second_carry);
    RUN(control_masks_readonly_power_and_force_reset);
    RUN(wire_commands_payloads_and_both_command_representations);
    RUN(wire_read_snapshot_survives_a_calendar_carry);
    RUN(wire_abort_reset_strobes_and_forced_requests);
    return 0;
}
