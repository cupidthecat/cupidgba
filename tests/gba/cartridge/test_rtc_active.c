/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/rtc.h"
#include "../../test.h"

static uint64_t clock;
static void pins(struct CupidRTC* rtc, uint8_t value) {
    clock += 128;
    CHECK(!cupid_rtc_write_pins_direction(rtc, value, 7, clock));
}
static void bit(struct CupidRTC* rtc, bool value) {
    const uint8_t low = (uint8_t)(4 | (value ? 2 : 0));
    pins(rtc, low); pins(rtc, low | 1);
}
static void command(struct CupidRTC* rtc, uint8_t value) {
    pins(rtc, 1); pins(rtc, 5);
    for (unsigned i = 0; i < 8; ++i) bit(rtc, (value >> i) & 1);
}

static void active_calendar_frames(void) {
    static const uint8_t controls[] = {0x48, 0x4A, 0x42, 0x60};
    static const uint64_t epochs[] = {0, UINT32_MAX - 16u, UINT64_C(0x300000000)};
    static const uint8_t payload[] = {0x28, 2, 0x29, 4, 0x94, 0x15, 0x16};
    unsigned cases = 0;
    for (unsigned whole = 0; whole < 2; ++whole) for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned active = 0; active < 2; ++active) for (unsigned epoch = 0; epoch < 3; ++epoch) {
            const unsigned offset = whole ? 0 : 4, length = whole ? 56 : 24;
            for (unsigned count = 0; count <= length; ++count) {
                struct CupidRTC rtc;
                clock = epochs[epoch];
                cupid_rtc_init(&rtc, clock);
                const struct CupidRTCDateTime seed = {26, 10, 17, 6, 7, 8, 59};
                CHECK(cupid_rtc_set_datetime(&rtc, &seed));
                /* Use the documented latch sources, not direct output-field
                 * assignments, to establish each initial output level. */
                if (mode == 2) cupid_rtc_write_interrupt_parameters(&rtc, 0x8000);
                if (mode == 3) cupid_rtc_write_interrupt_parameters(&rtc, active ? 0x0807 : 0x1594);
                cupid_rtc_write_control(&rtc, controls[mode]);
                if ((mode < 2 && active) || (mode == 2 && !active)) {
                    clock += mode < 2 ? CUPID_RTC_CLOCK_HZ : CUPID_RTC_CLOCK_HZ / 2;
                    cupid_rtc_sync(&rtc, clock);
                }
                CHECK(rtc.interruptLow == (active != 0));
                const struct CupidRTCDateTime before = rtc.date;
                const uint16_t parameters = rtc.interruptParameters;
                command(&rtc, whole ? 0x26 : 0x66);
                for (unsigned sample = 0; sample < count; ++sample) {
                    const uint8_t value = (uint8_t)(4 | ((payload[offset + sample / 8] >> (sample % 8)) & 1) * 2);
                    pins(&rtc, value);
                    CHECK(!memcmp(&rtc.date, &before, sizeof(before)));
                    CHECK(rtc.interruptLow == (active != 0));
                    pins(&rtc, value | 1);
                    if (sample + 1 != length) {
                        CHECK(!memcmp(&rtc.date, &before, sizeof(before)));
                        CHECK(rtc.interruptLow == (active != 0));
                    }
                }
                if (count == length) {
                    struct CupidRTCDateTime expected = whole
                        ? (struct CupidRTCDateTime){28, 2, 29, 4, 14, 15, 16}
                        : before;
                    expected.hour = 14; expected.minute = 15; expected.second = 16;
                    CHECK(!memcmp(&rtc.date, &expected, sizeof(expected)));
                    CHECK(!rtc.fraction && !rtc.minuteCarryActive && !rtc.invalidSecond);
                    CHECK(rtc.interruptLow == (mode >= 2 || active));
                    uint32_t next = 0;
                    if (mode == 2) next = (uint32_t)(CUPID_RTC_CLOCK_HZ / 2);
                    else if (mode < 2 && (!active || mode == 1))
                        next = (uint32_t)((active ? 14 : 44) * CUPID_RTC_CLOCK_HZ);
                    CHECK(cupid_rtc_next_interrupt(&rtc) == next);
                } else {
                    CHECK(!memcmp(&rtc.date, &before, sizeof(before)));
                    CHECK(rtc.interruptLow == (active != 0));
                }
                const struct CupidRTCDateTime selected = rtc.date;
                const bool selectedOutput = rtc.interruptLow;
                pins(&rtc, 1);
                CHECK(!memcmp(&rtc.date, &selected, sizeof(selected)));
                CHECK(rtc.interruptLow == selectedOutput);
                CHECK(rtc.control == controls[mode] && rtc.interruptParameters == parameters);
                ++cases;
            }
        }
    CHECK(cases == 1968);
    printf("PASS %u active/inactive calendar frame and abort cases\n", cases);
}

static void parameter_word_boundary(void) {
    static const uint16_t words[] = {0, 0xFFFF, 0xA5C3, 0x8000};
    static const uint64_t epochs[] = {0, UINT32_MAX - 16u, UINT64_C(0x300000000)};
    unsigned cases = 0;
    for (unsigned read = 0; read < 2; ++read) for (unsigned value = 0; value < 4; ++value)
        for (unsigned extra = 0; extra <= 64; ++extra) for (unsigned epoch = 0; epoch < 3; ++epoch)
            for (unsigned frequency = 0; frequency < 2; ++frequency) {
                struct CupidRTC rtc;
                clock = epochs[epoch];
                cupid_rtc_init(&rtc, clock);
                cupid_rtc_write_control(&rtc, frequency ? 0x42 : 0x40);
                command(&rtc, read ? 0x96 : 0x16);
                for (unsigned i = 0; i < 16; ++i) bit(&rtc, (words[value] >> i) & 1);
                CHECK(rtc.interruptParameters == words[value]);
                const struct CupidRTCDateTime before = rtc.date;
                /* Include complete command-shaped bytes after the word. They
                 * cannot become another command without releasing select. */
                for (unsigned i = 0; i < extra; ++i) {
                    bit(&rtc, (UINT64_C(0x0646266696C61636) >> i) & 1);
                    CHECK(rtc.interruptParameters == words[value]);
                    CHECK(rtc.control == (frequency ? 0x42 : 0x40));
                    CHECK(!memcmp(&rtc.date, &before, sizeof(before)));
                    CHECK(cupid_rtc_output_pins(&rtc) == 2);
                }
                CHECK(rtc.fraction == clock - epochs[epoch]);
                command(&rtc, 0x16);
                for (unsigned i = 0; i < 16; ++i) bit(&rtc, (0x1234u >> i) & 1);
                CHECK(rtc.interruptParameters == 0x1234);
                ++cases;
            }
    CHECK(cases == 3120);
    printf("PASS %u fixed parameter-word/overrun/restart cases\n", cases);
}

int main(void) {
    active_calendar_frames();
    parameter_word_boundary();
    return 0;
}
