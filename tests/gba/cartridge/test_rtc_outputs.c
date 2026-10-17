/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/rtc.h"
#include "../../test.h"
#include <limits.h>

static uint8_t bcd(unsigned value) {
    return (uint8_t)(value / 10 * 16 + value % 10);
}

static void every_frequency_has_two_edges_per_period(void) {
    for (unsigned bit = 0; bit < 16; bit++) {
        struct CupidRTC rtc;
        cupid_rtc_init(&rtc, UINT64_C(0xFFFFFFF0));
        cupid_rtc_write_interrupt_parameters(&rtc, (uint16_t)(1u << bit));
        cupid_rtc_write_control(&rtc, 0x42);
        const uint32_t half = 256u << bit;
        CHECK(rtc.interruptLow && cupid_rtc_next_interrupt(&rtc) == half);
        for (unsigned edge = 1; edge <= 6; edge++) {
            const uint64_t when = UINT64_C(0xFFFFFFF0) + (uint64_t)half * edge;
            cupid_rtc_sync(&rtc, when - 1);
            CHECK(rtc.interruptLow == ((edge & 1) != 0));
            CHECK(cupid_rtc_next_interrupt(&rtc) == 1);
            cupid_rtc_sync(&rtc, when);
            CHECK(rtc.interruptLow == ((edge & 1) == 0));
            CHECK(cupid_rtc_next_interrupt(&rtc) == half);
        }
    }
}

static bool selected_clocks_high(unsigned mask, unsigned phase) {
    bool high = true;
    for (unsigned bit = 0; bit < 16; bit++)
        if (mask & (1u << bit)) high = high && ((phase >> bit) & 1);
    return high;
}

static void combined_frequency_deadlines_match_the_complete_waveform(void) {
    /* Enumerate the waveform independently of the next-edge arithmetic. */
    const uint16_t masks[] = {0x000A, 0x8001, 0x5555, 0xAAAA, 0xFFFF, 0xA132};
    uint32_t* distances = calloc(65536, sizeof(*distances));
    CHECK(distances);
    for (unsigned m = 0; m < sizeof(masks) / sizeof(*masks); m++) {
        unsigned distance = 0;
        for (int phase = 131071; phase >= 0; phase--) {
            if (selected_clocks_high(masks[m], (unsigned)phase & 65535) !=
                selected_clocks_high(masks[m], (unsigned)(phase + 1) & 65535)) distance = 1;
            else ++distance;
            if (phase < 65536) distances[phase] = distance;
        }
        struct CupidRTC rtc;
        cupid_rtc_init(&rtc, 0);
        cupid_rtc_write_interrupt_parameters(&rtc, masks[m]);
        cupid_rtc_write_control(&rtc, 0x42);
        for (unsigned phase = 0; phase < 65536; phase++) {
            const unsigned offsets[] = {0, 17, 255};
            for (unsigned at = 0; at < 3; at++) {
                rtc.fraction = phase * 256 + offsets[at];
                cupid_rtc_sync(&rtc, 0);
                CHECK(rtc.interruptLow == !selected_clocks_high(masks[m], phase));
                CHECK(cupid_rtc_next_interrupt(&rtc) == distances[phase] * 256 - offsets[at]);
            }
        }
    }
    free(distances);
}

static void mode_priority_and_divider_writes_preserve_clock_phase(void) {
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, 0);
    for (unsigned flags = 0; flags < 8; flags++) {
        cupid_rtc_write_control(&rtc, 0x40);
        cupid_rtc_write_interrupt_parameters(&rtc, 0x0010);
        const uint8_t value = (uint8_t)(0x40 | (flags & 1 ? 2 : 0) |
                                      (flags & 2 ? 8 : 0) | (flags & 4 ? 0x20 : 0));
        cupid_rtc_write_control(&rtc, value);
        CHECK(rtc.interruptLow == ((flags & 1) && !(flags & 2)));
        CHECK(cupid_rtc_next_interrupt(&rtc) == (flags & 2 ? 60 * CUPID_RTC_CLOCK_HZ :
                                               flags & 1 ? 4096 : flags & 4 ? INT32_MAX : 0));
    }
    cupid_rtc_write_interrupt_parameters(&rtc, 0x000A);
    cupid_rtc_write_control(&rtc, 0x42);
    cupid_rtc_sync(&rtc, 3 * 256 + 17);
    CHECK(rtc.interruptLow && cupid_rtc_next_interrupt(&rtc) == 7 * 256 - 17);
    cupid_rtc_write_interrupt_parameters(&rtc, 2);
    CHECK(!rtc.interruptLow && rtc.fraction == 3 * 256 + 17);
    CHECK(cupid_rtc_next_interrupt(&rtc) == 256 - 17);
    cupid_rtc_write_control(&rtc, 0x42);
    CHECK(!rtc.interruptLow && rtc.fraction == 3 * 256 + 17);
    const uint8_t time[] = {0x12, 0x34, 0x56};
    cupid_rtc_write_time(&rtc, time);
    CHECK(rtc.interruptLow && rtc.fraction == 0 && cupid_rtc_next_interrupt(&rtc) == 512);
    cupid_rtc_advance_seconds(&rtc, UINT64_MAX);
    CHECK(rtc.interruptLow && rtc.fraction == 0);
    cupid_rtc_write_interrupt_parameters(&rtc, 0);
    CHECK(!rtc.interruptLow && !cupid_rtc_next_interrupt(&rtc));
    cupid_rtc_reset_registers(&rtc);
    CHECK(!rtc.interruptParameters && !rtc.interruptLow && !cupid_rtc_next_interrupt(&rtc));
}

static void alarms_compare_every_hour_in_both_formats_and_hold_until_disabled(void) {
    for (unsigned format = 0; format < 2; format++) for (unsigned hour = 0; hour < 24; hour++) {
        struct CupidRTC rtc;
        cupid_rtc_init(&rtc, 0);
        const struct CupidRTCDateTime date = {26, 10, 13, 2, (uint8_t)hour, 17, 59};
        CHECK(cupid_rtc_set_datetime(&rtc, &date));
        const uint8_t control = (uint8_t)(format ? 0x60 : 0x20);
        const uint8_t hourByte = (uint8_t)(bcd(format ? hour : hour % 12) | (hour >= 12 ? 0x80 : 0));
        cupid_rtc_write_interrupt_parameters(&rtc, (uint16_t)(0x1800 | hourByte));
        cupid_rtc_write_control(&rtc, control);
        CHECK(!rtc.interruptLow && cupid_rtc_next_interrupt(&rtc) == CUPID_RTC_CLOCK_HZ);
        cupid_rtc_sync(&rtc, CUPID_RTC_CLOCK_HZ - 1);
        CHECK(!rtc.interruptLow && cupid_rtc_next_interrupt(&rtc) == 1);
        cupid_rtc_sync(&rtc, CUPID_RTC_CLOCK_HZ);
        CHECK(rtc.interruptLow && !cupid_rtc_next_interrupt(&rtc));
        cupid_rtc_write_control(&rtc, control);
        CHECK(rtc.interruptLow);
        cupid_rtc_write_control(&rtc, control & (uint8_t)~0x20);
        CHECK(!rtc.interruptLow);
        cupid_rtc_write_control(&rtc, control);
        CHECK(rtc.interruptLow); /* The coincidence remains true for the minute. */
        cupid_rtc_advance_seconds(&rtc, 120);
        CHECK(rtc.interruptLow && !cupid_rtc_next_interrupt(&rtc));
        cupid_rtc_write_control(&rtc, control & (uint8_t)~0x20);
        cupid_rtc_write_control(&rtc, control);
        CHECK(!rtc.interruptLow && cupid_rtc_next_interrupt(&rtc) == INT32_MAX);
    }
}

static void alarm_fields_and_offline_intervals_do_not_invent_matches(void) {
    const uint16_t rejected[] = {0x183A, 0x6012, 0x1881, 0x1823, 0x1812};
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(*rejected); i++) {
        struct CupidRTC rtc;
        cupid_rtc_init(&rtc, 0);
        cupid_rtc_write_interrupt_parameters(&rtc, rejected[i]);
        cupid_rtc_write_control(&rtc, 0x60);
        CHECK(!rtc.interruptLow && !cupid_rtc_next_interrupt(&rtc));
        cupid_rtc_advance_seconds(&rtc, UINT64_MAX);
        CHECK(!rtc.interruptLow);
    }
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, 0);
    const struct CupidRTCDateTime date = {99, 12, 31, 6, 23, 59, 59};
    CHECK(cupid_rtc_set_datetime(&rtc, &date));
    /* Reserved bits do not participate in the hour/minute comparison. */
    cupid_rtc_write_interrupt_parameters(&rtc, 0x8040);
    cupid_rtc_write_control(&rtc, 0x60);
    rtc.fraction = CUPID_RTC_CLOCK_HZ - 11;
    CHECK(cupid_rtc_next_interrupt(&rtc) == 11);
    cupid_rtc_sync(&rtc, 10);
    CHECK(!rtc.interruptLow);
    cupid_rtc_sync(&rtc, 11);
    CHECK(rtc.interruptLow && rtc.date.year == 0 && rtc.date.weekday == 0);
    cupid_rtc_write_control(&rtc, 0x40);
    cupid_rtc_write_interrupt_parameters(&rtc, 0x0100);
    cupid_rtc_write_control(&rtc, 0x60);
    cupid_rtc_advance_seconds(&rtc, 86401);
    CHECK(rtc.interruptLow && rtc.date.minute == 0 && rtc.date.second == 1);
    cupid_rtc_write_control(&rtc, 0x40);
    const uint8_t invalid[] = {0x23, 0x59, 0x6A};
    cupid_rtc_write_time(&rtc, invalid);
    cupid_rtc_write_interrupt_parameters(&rtc, 0);
    cupid_rtc_write_control(&rtc, 0x60);
    CHECK(cupid_rtc_next_interrupt(&rtc) == CUPID_RTC_CLOCK_HZ);
    cupid_rtc_advance_seconds(&rtc, 1);
    CHECK(rtc.interruptLow && !rtc.invalidSecond && rtc.date.hour == 0 && rtc.date.minute == 0);
}

static void alarm_long_wait_checkpoints_and_bulk_clock_keep_the_exact_carry(void) {
    for (unsigned bulk = 0; bulk < 2; bulk++) {
        struct CupidRTC rtc;
        cupid_rtc_init(&rtc, UINT64_C(0xFFFFFFF0));
        const struct CupidRTCDateTime date = {0, 2, 28, 1, 23, 58, 37};
        CHECK(cupid_rtc_set_datetime(&rtc, &date));
        cupid_rtc_write_interrupt_parameters(&rtc, 0x0192); /* 12:01 PM next day. */
        cupid_rtc_write_control(&rtc, 0x60);
        rtc.fraction = 193;
        const uint64_t end = rtc.lastClock + UINT64_C(43343) * CUPID_RTC_CLOCK_HZ - 193;
        CHECK(cupid_rtc_next_interrupt(&rtc) == INT32_MAX);
        if (bulk) cupid_rtc_sync(&rtc, end - 1);
        else while (end - rtc.lastClock > cupid_rtc_next_interrupt(&rtc)) {
            cupid_rtc_sync(&rtc, rtc.lastClock + cupid_rtc_next_interrupt(&rtc));
            CHECK(!rtc.interruptLow);
        }
        cupid_rtc_sync(&rtc, end - 1);
        CHECK(!rtc.interruptLow && cupid_rtc_next_interrupt(&rtc) == 1);
        cupid_rtc_sync(&rtc, end);
        CHECK(rtc.interruptLow && !cupid_rtc_next_interrupt(&rtc));
        CHECK(rtc.date.month == 2 && rtc.date.day == 29 && rtc.date.hour == 12);
        CHECK(rtc.date.minute == 1 && rtc.date.second == 0 && rtc.fraction == 0);
    }
}

int main(void) {
    RUN(every_frequency_has_two_edges_per_period);
    RUN(combined_frequency_deadlines_match_the_complete_waveform);
    RUN(mode_priority_and_divider_writes_preserve_clock_phase);
    RUN(alarms_compare_every_hour_in_both_formats_and_hold_until_disabled);
    RUN(alarm_fields_and_offline_intervals_do_not_invent_matches);
    RUN(alarm_long_wait_checkpoints_and_bulk_clock_keep_the_exact_carry);
    return 0;
}
