/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/rtc.h"
#include "../../test.h"

static const struct CupidRTCDateTime dates[] = {
    {24, 6, 15, 6, 8, 35, 10},
    {26, 10, 16, 5, 21, 40, 25},
    {28, 2, 29, 2, 17, 12, 34}
};
static const uint64_t epochs[] = {0, UINT32_MAX - 8u, UINT64_C(0x300000000)};
static uint64_t clock;
static unsigned scenarios;
static void drive(struct CupidRTC* rtc, uint8_t pins) {
    clock += 128;
    CHECK(!cupid_rtc_write_pins(rtc, pins, clock));
}
static void select_chip(struct CupidRTC* rtc) {
    drive(rtc, 1); drive(rtc, 5);
}
static uint8_t bcd(unsigned value) {
    return (uint8_t)((value / 10) * 16 + value % 10);
}
static void payload(unsigned index, bool wholeDate, bool hour24, uint8_t data[7]) {
    const struct CupidRTCDateTime* date = &dates[index];
    const unsigned offset = wholeDate ? 4 : 0;
    if (wholeDate) {
        data[0] = bcd(date->year); data[1] = bcd(date->month);
        data[2] = bcd(date->day); data[3] = date->weekday;
    }
    data[offset] = bcd(hour24 ? date->hour : date->hour % 12) | (date->hour >= 12 ? 0x80 : 0);
    data[offset + 1] = bcd(date->minute); data[offset + 2] = bcd(date->second);
}
static void bit(struct CupidRTC* rtc, bool value) {
    const uint8_t pins = (uint8_t)(4 | (value ? 2 : 0));
    /* Repeated driven levels advance battery time without another sample. */
    drive(rtc, pins | 1); drive(rtc, pins | 1);
    drive(rtc, pins); drive(rtc, pins);
    drive(rtc, pins | 1); drive(rtc, pins | 1);
}
static void bits(struct CupidRTC* rtc, const uint8_t* data, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bit(rtc, (data[i / 8] >> (i % 8)) & 1);
}
static void begin(struct CupidRTC* rtc, bool wholeDate, bool hour24, uint64_t epoch) {
    clock = epoch; cupid_rtc_init(rtc, clock);
    cupid_rtc_write_control(rtc, hour24 ? 0x40 : 0);
    CHECK(cupid_rtc_set_datetime(rtc, &dates[0]));
    select_chip(rtc);
    const uint8_t command = wholeDate ? 0x26 : 0x66;
    bits(rtc, &command, 8);
}
static void expect(const struct CupidRTC* rtc, unsigned index, bool wholeDate) {
    struct CupidRTCDateTime expected = dates[index];
    if (!wholeDate) {
        expected.year = dates[0].year; expected.month = dates[0].month;
        expected.day = dates[0].day; expected.weekday = dates[0].weekday;
    }
    CHECK(!memcmp(&rtc->date, &expected, sizeof(expected)));
}
static void partial_and_abort(bool wholeDate, bool hour24, uint64_t epoch) {
    const unsigned length = wholeDate ? 56 : 24;
    uint8_t first[7], second[7], third[7];
    payload(0, wholeDate, hour24, first); payload(1, wholeDate, hour24, second);
    payload(2, wholeDate, hour24, third);
    for (unsigned count = 0; count <= length; ++count) {
        struct CupidRTC rtc; begin(&rtc, wholeDate, hour24, epoch);
        bits(&rtc, first, length); expect(&rtc, 0, wholeDate);
        /* The final repeated high level is one battery tick after commit. */
        CHECK(rtc.fraction == 128);
        const uint64_t anchor = clock - 128;
        bits(&rtc, second, count);
        expect(&rtc, count == length ? 1 : 0, wholeDate);
        CHECK(rtc.fraction == (count == length ? 128 : (uint32_t)(clock - anchor)));
        /* Deselect discards the incomplete following frame, not the last commit. */
        select_chip(&rtc);
        const uint8_t command = wholeDate ? 0x26 : 0x66;
        bits(&rtc, &command, 8); bits(&rtc, third, length);
        expect(&rtc, 2, wholeDate); ++scenarios;
    }
}
static void final_edge_and_repeated_frames(bool wholeDate, bool hour24, uint64_t epoch) {
    const unsigned length = wholeDate ? 56 : 24;
    uint8_t data[3][7];
    for (unsigned i = 0; i < 3; ++i) payload(i, wholeDate, hour24, data[i]);
    struct CupidRTC rtc; begin(&rtc, wholeDate, hour24, epoch);
    bits(&rtc, data[0], length); bits(&rtc, data[1], length - 1);
    const bool last = (data[1][(length - 1) / 8] >> 7) & 1;
    const uint8_t pins = (uint8_t)(4 | (last ? 2 : 0));
    drive(&rtc, pins); drive(&rtc, pins);
    expect(&rtc, 0, wholeDate);
    drive(&rtc, pins | 1); expect(&rtc, 1, wholeDate);
    CHECK(rtc.fraction == 0); ++scenarios;
    /* No new command or chip-select edge separates these complete frames. */
    bits(&rtc, data[2], length); expect(&rtc, 2, wholeDate);
    bits(&rtc, data[0], length); expect(&rtc, 0, wholeDate); ++scenarios;
}
static void clock_runs_during_reception(bool wholeDate, bool hour24, uint64_t epoch) {
    const unsigned length = wholeDate ? 56 : 24;
    uint8_t first[7], second[7];
    payload(0, wholeDate, hour24, first); payload(1, wholeDate, hour24, second);
    struct CupidRTC rtc; begin(&rtc, wholeDate, hour24, epoch);
    bits(&rtc, first, length); bits(&rtc, second, length / 2);
    clock += CUPID_RTC_CLOCK_HZ;
    cupid_rtc_sync(&rtc, clock);
    struct CupidRTCDateTime advanced = dates[0];
    ++advanced.second;
    CHECK(!memcmp(&rtc.date, &advanced, sizeof(advanced)));
    /* A long pause clocks the battery without accepting the buffered payload. */
    for (unsigned i = length / 2; i < length; ++i) bit(&rtc, (second[i / 8] >> (i % 8)) & 1);
    expect(&rtc, 1, wholeDate); CHECK(rtc.fraction == 128); ++scenarios;
}
int main(void) {
    for (unsigned date = 0; date < 2; ++date) for (unsigned mode = 0; mode < 2; ++mode)
      for (unsigned epoch = 0; epoch < 3; ++epoch) {
        partial_and_abort(date != 0, mode != 0, epochs[epoch]);
        final_edge_and_repeated_frames(date != 0, mode != 0, epochs[epoch]);
        clock_runs_during_reception(date != 0, mode != 0, epochs[epoch]);
      }
    CHECK(scenarios == 528);
    printf("RTC calendar framing scenarios: %u\n", scenarios);
    return 0;
}
