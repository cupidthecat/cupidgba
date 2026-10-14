/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/rtc.h"
#include "../../test.h"

/* The first whole 16,777,216 Hz clock after the nominal 10 ms carry window. */
enum { CARRY_END = 167773 };

static void minute_carry(struct CupidRTC* rtc, bool steady) {
    cupid_rtc_init(rtc, 123);
    const struct CupidRTCDateTime date = {26, 10, 13, 2, 12, 34, 59};
    CHECK(cupid_rtc_set_datetime(rtc, &date));
    rtc->fraction = CUPID_RTC_CLOCK_HZ - 1;
    cupid_rtc_write_control(rtc, steady ? 0x4A : 0x48);
    cupid_rtc_sync(rtc, 124);
    CHECK(rtc->interruptLow && rtc->date.minute == 35 && !rtc->date.second && !rtc->fraction);
}

static void acknowledgement_retains_the_carry_signal_on_both_sides_of_10ms(void) {
    for (unsigned steady = 0; steady < 2; steady++) {
        struct CupidRTC rtc;
        minute_carry(&rtc, steady != 0);
        cupid_rtc_write_control(&rtc, rtc.control);
        CHECK(rtc.interruptLow);
        cupid_rtc_advance_clocks(&rtc, CARRY_END - 1);
        cupid_rtc_write_control(&rtc, rtc.control);
        CHECK(rtc.interruptLow);
        cupid_rtc_advance_clocks(&rtc, 1);
        CHECK(rtc.interruptLow); /* Expiration alone does not acknowledge output. */
        cupid_rtc_write_control(&rtc, rtc.control);
        CHECK(!rtc.interruptLow);
        CHECK(cupid_rtc_next_interrupt(&rtc) == 60 * CUPID_RTC_CLOCK_HZ - CARRY_END);
        cupid_rtc_advance_clocks(&rtc, 60 * CUPID_RTC_CLOCK_HZ - CARRY_END);
        CHECK(rtc.interruptLow && rtc.date.minute == 36 && !rtc.date.second);
        if (steady) {
            cupid_rtc_advance_clocks(&rtc, 30 * CUPID_RTC_CLOCK_HZ);
            CHECK(!rtc.interruptLow);
        }
    }
}

static void disable_and_enable_use_the_original_carry_deadline(void) {
    const uint32_t elapsed[] = {0, 1, CARRY_END - 1, CARRY_END, CARRY_END + 1};
    for (unsigned steady = 0; steady < 2; steady++)
        for (unsigned i = 0; i < sizeof(elapsed) / sizeof(*elapsed); i++) {
            struct CupidRTC rtc;
            minute_carry(&rtc, steady != 0);
            cupid_rtc_write_control(&rtc, 0x40);
            CHECK(!rtc.interruptLow);
            cupid_rtc_advance_clocks(&rtc, elapsed[i]);
            cupid_rtc_write_control(&rtc, steady ? 0x4A : 0x48);
            CHECK(rtc.interruptLow == (elapsed[i] < CARRY_END));
        }
    struct CupidRTC rtc;
    minute_carry(&rtc, true);
    cupid_rtc_advance_clocks(&rtc, 10 * CUPID_RTC_CLOCK_HZ);
    CHECK(rtc.interruptLow);
    cupid_rtc_write_control(&rtc, 0x40);
    cupid_rtc_advance_clocks(&rtc, 1);
    cupid_rtc_write_control(&rtc, 0x4A);
    CHECK(!rtc.interruptLow); /* A short disable does not restart an expired carry. */
}

static void fresh_zero_seconds_seed_reset_and_time_writes_do_not_fabricate_a_carry(void) {
    for (unsigned steady = 0; steady < 2; steady++) {
        struct CupidRTC rtc;
        cupid_rtc_init(&rtc, 0);
        const uint8_t control = steady ? 0x4A : 0x48;
        cupid_rtc_write_control(&rtc, control);
        CHECK(!rtc.interruptLow);
        const struct CupidRTCDateTime date = {26, 10, 13, 2, 12, 35, 0};
        CHECK(cupid_rtc_set_datetime(&rtc, &date));
        cupid_rtc_write_control(&rtc, control);
        CHECK(!rtc.interruptLow);
        minute_carry(&rtc, steady != 0);
        const uint8_t time[] = {0x92, 0x35, 0};
        cupid_rtc_write_time(&rtc, time);
        cupid_rtc_write_control(&rtc, control);
        CHECK(!rtc.interruptLow);
        minute_carry(&rtc, steady != 0);
        cupid_rtc_reset_registers(&rtc);
        cupid_rtc_write_control(&rtc, control);
        CHECK(!rtc.interruptLow);
    }
}

static void mode_priority_and_power_flags_preserve_a_real_carry(void) {
    for (unsigned value = 0; value < 256; value++) {
        struct CupidRTC rtc;
        minute_carry(&rtc, false);
        cupid_rtc_sample_power(&rtc, false);
        cupid_rtc_write_interrupt_parameters(&rtc, 0xAAAA);
        cupid_rtc_write_control(&rtc, (uint8_t)(value | 8));
        CHECK(rtc.interruptLow && (rtc.control & 0x80));
        CHECK(cupid_rtc_read_control(&rtc) == (uint8_t)(0x80 | ((value | 8) & 0x6A)));
    }
}

static void bulk_and_chunked_clock_inputs_retain_the_same_window_and_latch(void) {
    const uint64_t intervals[] = {1, CARRY_END - 1, CARRY_END, CARRY_END + 1,
        CUPID_RTC_CLOCK_HZ, 60 * CUPID_RTC_CLOCK_HZ + 1,
        60 * CUPID_RTC_CLOCK_HZ + CARRY_END};
    for (unsigned steady = 0; steady < 2; steady++)
        for (unsigned i = 0; i < sizeof(intervals) / sizeof(*intervals); i++) {
            struct CupidRTC bulk, pieces;
            minute_carry(&bulk, steady != 0);
            pieces = bulk;
            cupid_rtc_advance_clocks(&bulk, intervals[i]);
            for (uint64_t elapsed = 0; elapsed < intervals[i];) {
                uint64_t amount = intervals[i] - elapsed;
                if (amount > 65537) amount = 65537;
                cupid_rtc_advance_clocks(&pieces, amount);
                elapsed += amount;
            }
            CHECK(!memcmp(&bulk.date, &pieces.date, sizeof(bulk.date)));
            CHECK(bulk.fraction == pieces.fraction && bulk.interruptLow == pieces.interruptLow);
            cupid_rtc_write_control(&bulk, bulk.control);
            cupid_rtc_write_control(&pieces, pieces.control);
            CHECK(bulk.interruptLow == pieces.interruptLow);
            CHECK(bulk.interruptLow == (bulk.date.second == 0 && bulk.fraction < CARRY_END));
        }
}

static void offline_seconds_invalid_seconds_and_console_rebase_keep_real_carries(void) {
    struct CupidRTC rtc;
    minute_carry(&rtc, false);
    cupid_rtc_advance_seconds(&rtc, 60);
    CHECK(rtc.date.minute == 36 && !rtc.date.second);
    cupid_rtc_write_control(&rtc, rtc.control);
    CHECK(rtc.interruptLow);
    cupid_rtc_rebase(&rtc, UINT64_MAX - 5);
    cupid_rtc_sync(&rtc, UINT64_MAX);
    cupid_rtc_write_control(&rtc, rtc.control);
    CHECK(rtc.interruptLow && rtc.fraction == 5);
    cupid_rtc_sync(&rtc, 0);
    cupid_rtc_sync(&rtc, CARRY_END - 5);
    cupid_rtc_write_control(&rtc, rtc.control);
    CHECK(!rtc.interruptLow);
    const uint8_t invalid[] = {0x92, 0x35, 0x6A};
    cupid_rtc_write_time(&rtc, invalid);
    cupid_rtc_advance_clocks(&rtc, CUPID_RTC_CLOCK_HZ);
    CHECK(!rtc.invalidSecond && rtc.date.minute == 36 && !rtc.date.second);
    cupid_rtc_write_control(&rtc, rtc.control);
    CHECK(rtc.interruptLow);
}

int main(void) {
    RUN(acknowledgement_retains_the_carry_signal_on_both_sides_of_10ms);
    RUN(disable_and_enable_use_the_original_carry_deadline);
    RUN(fresh_zero_seconds_seed_reset_and_time_writes_do_not_fabricate_a_carry);
    RUN(mode_priority_and_power_flags_preserve_a_real_carry);
    RUN(bulk_and_chunked_clock_inputs_retain_the_same_window_and_latch);
    RUN(offline_seconds_invalid_seconds_and_console_rebase_keep_real_carries);
    return 0;
}
