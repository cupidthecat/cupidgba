/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/rtc.h"

static unsigned month_length(unsigned year, unsigned month) {
    static const uint8_t lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return lengths[month - 1] + (month == 2 && !(year & 3));
}

static uint8_t bcd(unsigned value) {
    return (uint8_t)((value / 10) * 16 + value % 10);
}

static unsigned decimal(uint8_t value, unsigned minimum, unsigned maximum, unsigned fallback) {
    const unsigned low = value & 15, high = value >> 4;
    const unsigned result = high * 10 + low;
    return low > 9 || high > 9 || result < minimum || result > maximum ? fallback : result;
}

static void advance_month(struct CupidRTCDateTime* date) {
    if (++date->month == 13) {
        date->month = 1;
        date->year = (uint8_t)((date->year + 1) % 100);
    }
}

static void advance_days(struct CupidRTCDateTime* date, uint64_t days) {
    date->weekday = (uint8_t)((date->weekday + days % 7) % 7);
    /* The two-digit year repeats after 36,525 days; weekday is independent. */
    days %= UINT64_C(36525);
    while (days) {
        const unsigned remaining = month_length(date->year, date->month) - date->day + 1;
        if (days < remaining) {
            date->day = (uint8_t)(date->day + days);
            return;
        }
        days -= remaining;
        date->day = 1;
        advance_month(date);
    }
}

static void advance_seconds(struct CupidRTC* rtc, uint64_t seconds) {
    if (!seconds) return;
    const bool minuteCarry = seconds >= (rtc->invalidSecond ? 1u : 60u - rtc->date.second);
    const unsigned toRelease = rtc->date.second < 30 ? 30u - rtc->date.second : 90u - rtc->date.second;
    const bool release = rtc->invalidSecond || seconds >= toRelease;
    uint64_t minutes = 0;
    if (rtc->invalidSecond && seconds) {
        rtc->invalidSecond = false;
        rtc->date.second = 0;
        seconds--;
        minutes = 1;
    }
    const unsigned second = (unsigned)(seconds % 60) + rtc->date.second;
    rtc->date.second = (uint8_t)(second % 60);
    minutes += seconds / 60 + second / 60 + rtc->date.minute;
    rtc->date.minute = (uint8_t)(minutes % 60);
    const uint64_t hours = minutes / 60 + rtc->date.hour;
    rtc->date.hour = (uint8_t)(hours % 24);
    advance_days(&rtc->date, hours / 24);
    if (rtc->control & 8) {
        if (rtc->control & 2) {
            /* Steady output starts with a minute carry, then releases after 30s. */
            if (minuteCarry) rtc->minuteLow = rtc->date.second < 30;
            else if (release) rtc->minuteLow = false;
        } else if (minuteCarry) {
            rtc->minuteLow = true;
        }
    }
}

void cupid_rtc_init(struct CupidRTC* rtc, uint64_t clock) {
    memset(rtc, 0, sizeof(*rtc));
    /* A deterministic healthy virtual battery; the caller can supply a seed. */
    rtc->date = (struct CupidRTCDateTime){0, 1, 1, 6, 0, 0, 0};
    rtc->control = 0x40;
    rtc->powerSampling = true;
    rtc->lastClock = clock;
    cupid_rtc_reset_bus(rtc);
}

void cupid_rtc_sync(struct CupidRTC* rtc, uint64_t clock) {
    if (clock < rtc->lastClock) {
        rtc->lastClock = clock;
        return;
    }
    const uint64_t elapsed = clock - rtc->lastClock;
    rtc->lastClock = clock;
    const uint64_t remainder = rtc->fraction + elapsed % CUPID_RTC_CLOCK_HZ;
    rtc->fraction = (uint32_t)(remainder % CUPID_RTC_CLOCK_HZ);
    advance_seconds(rtc, elapsed / CUPID_RTC_CLOCK_HZ + remainder / CUPID_RTC_CLOCK_HZ);
}

void cupid_rtc_rebase(struct CupidRTC* rtc, uint64_t clock) {
    rtc->lastClock = clock;
}

uint32_t cupid_rtc_next_interrupt(const struct CupidRTC* rtc) {
    if (!(rtc->control & 8) || (rtc->minuteLow && !(rtc->control & 2))) return 0;
    unsigned seconds;
    if (rtc->invalidSecond) seconds = 1;
    else if (rtc->minuteLow)
        seconds = rtc->date.second < 30 ? 30u - rtc->date.second : 90u - rtc->date.second;
    else seconds = 60u - rtc->date.second;
    return (uint32_t)(seconds * CUPID_RTC_CLOCK_HZ - rtc->fraction);
}

bool cupid_rtc_set_datetime(struct CupidRTC* rtc, const struct CupidRTCDateTime* date) {
    if (date->year > 99 || !date->month || date->month > 12 || !date->day ||
        date->day > month_length(date->year, date->month) || date->weekday > 6 ||
        date->hour > 23 || date->minute > 59 || date->second > 59) return false;
    rtc->date = *date;
    rtc->fraction = 0;
    rtc->invalidSecond = false;
    return true;
}

void cupid_rtc_advance_seconds(struct CupidRTC* rtc, uint64_t seconds) {
    advance_seconds(rtc, seconds);
}

bool cupid_rtc_parse_datetime(const char* text, struct CupidRTCDateTime* date) {
    if (strlen(text) != 19 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':' || text[0] != '2' || text[1] != '0') return false;
    static const unsigned offsets[] = {2, 5, 8, 11, 14, 17};
    uint8_t values[6];
    for (unsigned i = 0; i < 6; i++) {
        const unsigned at = offsets[i];
        if (text[at] < '0' || text[at] > '9' || text[at + 1] < '0' || text[at + 1] > '9') return false;
        values[i] = (uint8_t)((text[at] - '0') * 10 + text[at + 1] - '0');
    }
    struct CupidRTCDateTime parsed = {values[0], values[1], values[2], 0, values[3], values[4], values[5]};
    struct CupidRTC check = {0};
    if (!cupid_rtc_set_datetime(&check, &parsed)) return false;
    unsigned days = parsed.day - 1;
    for (unsigned year = 0; year < parsed.year; year++) days += 365 + !(year & 3);
    for (unsigned month = 1; month < parsed.month; month++) days += month_length(parsed.year, month);
    parsed.weekday = (uint8_t)((6 + days) % 7);
    *date = parsed;
    return true;
}

void cupid_rtc_reset_registers(struct CupidRTC* rtc) {
    rtc->date = (struct CupidRTCDateTime){0, 1, 1, 0, 0, 0, 0};
    rtc->control = 0;
    rtc->fraction = 0;
    rtc->invalidSecond = false;
    rtc->powerSampling = true;
    rtc->minuteLow = false;
}

uint8_t cupid_rtc_read_control(struct CupidRTC* rtc) {
    const uint8_t value = rtc->control;
    rtc->powerSampling = true;
    return value;
}

void cupid_rtc_write_control(struct CupidRTC* rtc, uint8_t value) {
    rtc->control = (rtc->control & 0x80) | (value & 0x6A);
    /* A status write releases the minute latch, even if the bits are unchanged. */
    rtc->minuteLow = false;
}

void cupid_rtc_sample_power(struct CupidRTC* rtc, bool aboveThreshold) {
    if (!rtc->powerSampling) return;
    rtc->control = (rtc->control & 0x7F) | (aboveThreshold ? 0 : 0x80);
    rtc->powerSampling = aboveThreshold;
}

void cupid_rtc_read_datetime(const struct CupidRTC* rtc, uint8_t data[7]) {
    data[0] = bcd(rtc->date.year);
    data[1] = bcd(rtc->date.month);
    data[2] = bcd(rtc->date.day);
    data[3] = rtc->date.weekday;
    data[4] = bcd(rtc->control & 0x40 ? rtc->date.hour : rtc->date.hour % 12);
    if (rtc->date.hour >= 12) data[4] |= 0x80;
    data[5] = bcd(rtc->date.minute);
    data[6] = rtc->invalidSecond ? rtc->date.second : bcd(rtc->date.second);
}

static void write_clock(struct CupidRTC* rtc, const uint8_t data[3]) {
    unsigned hour = decimal(data[0] & 0x3F, 0, rtc->control & 0x40 ? 23 : 11, 0);
    if (!(rtc->control & 0x40) && (data[0] & 0x80)) hour += 12;
    rtc->date.hour = (uint8_t)hour;
    rtc->date.minute = (uint8_t)decimal(data[1] & 0x7F, 0, 59, 0);
    const uint8_t second = data[2] & 0x7F;
    const unsigned decoded = decimal(second, 0, 59, 60);
    rtc->invalidSecond = decoded == 60;
    rtc->date.second = rtc->invalidSecond ? second : (uint8_t)decoded;
    rtc->fraction = 0;
}

void cupid_rtc_write_datetime(struct CupidRTC* rtc, const uint8_t data[7]) {
    rtc->date.year = (uint8_t)decimal(data[0], 0, 99, 0);
    rtc->date.month = (uint8_t)decimal(data[1] & 0x1F, 1, 12, 1);
    rtc->date.day = (uint8_t)decimal(data[2] & 0x3F, 1, 31, 1);
    rtc->date.weekday = (data[3] & 7) == 7 ? 0 : data[3] & 7;
    if (rtc->date.day > month_length(rtc->date.year, rtc->date.month)) {
        rtc->date.day = 1;
        advance_month(&rtc->date);
    }
    write_clock(rtc, data + 4);
}

void cupid_rtc_write_time(struct CupidRTC* rtc, const uint8_t data[3]) {
    write_clock(rtc, data);
}

enum { RTC_IDLE, RTC_COMMAND, RTC_RECEIVE, RTC_SEND };

void cupid_rtc_reset_bus(struct CupidRTC* rtc) {
    rtc->pins = 1;
    rtc->inputLatch = rtc->inputByte = rtc->bit = rtc->byte = rtc->length = 0;
    rtc->state = RTC_IDLE;
    rtc->output = true;
}

uint8_t cupid_rtc_output_pins(const struct CupidRTC* rtc) {
    return rtc->output ? 2 : 0;
}

static bool receive_command(struct CupidRTC* rtc) {
    const uint8_t command = rtc->inputByte;
    rtc->state = RTC_IDLE;
    rtc->bit = rtc->byte = 0;
    if ((command & 15) != 6) return false;
    rtc->command = (command >> 4) & 7;
    switch (rtc->command) {
    case 0:
        cupid_rtc_reset_registers(rtc);
        return false;
    case 3:
        return true;
    case 2: rtc->length = 7; break;
    case 4: rtc->length = 1; break;
    case 6: rtc->length = 3; break;
    default: rtc->length = 1; break;
    }
    memset(rtc->transfer, 0xFF, sizeof(rtc->transfer));
    if (command & 0x80) {
        uint8_t date[7];
        switch (rtc->command) {
        case 2: cupid_rtc_read_datetime(rtc, rtc->transfer); break;
        case 4: rtc->transfer[0] = cupid_rtc_read_control(rtc); break;
        case 6:
            cupid_rtc_read_datetime(rtc, date);
            memcpy(rtc->transfer, date + 4, 3);
            break;
        default: break;
        }
        rtc->state = RTC_SEND;
    } else {
        rtc->inputByte = 0;
        rtc->state = RTC_RECEIVE;
    }
    return false;
}

static void receive_payload(struct CupidRTC* rtc, bool data) {
    if (data) rtc->inputByte |= (uint8_t)(1u << rtc->bit);
    if (++rtc->bit != 8) return;
    rtc->transfer[rtc->byte++] = rtc->inputByte;
    rtc->inputByte = rtc->bit = 0;
    if (rtc->byte != rtc->length) return;
    switch (rtc->command) {
    case 2: cupid_rtc_write_datetime(rtc, rtc->transfer); break;
    case 4: cupid_rtc_write_control(rtc, rtc->transfer[0]); break;
    case 6: cupid_rtc_write_time(rtc, rtc->transfer); break;
    default: break;
    }
    rtc->state = RTC_IDLE;
}

bool cupid_rtc_write_pins(struct CupidRTC* rtc, uint8_t pins, uint64_t clock) {
    cupid_rtc_sync(rtc, clock);
    pins &= 7;
    const uint8_t previous = rtc->pins;
    rtc->pins = pins;
    if (!(pins & 4)) {
        cupid_rtc_reset_bus(rtc);
        return false;
    }
    if (!(previous & 4)) {
        rtc->state = RTC_COMMAND;
        rtc->inputByte = rtc->bit = rtc->byte = 0;
        rtc->output = true;
        rtc->inputLatch = (pins >> 1) & 1;
        return false;
    }
    const bool falling = (previous & 1) && !(pins & 1);
    const bool rising = !(previous & 1) && (pins & 1);
    if (rtc->state == RTC_COMMAND) {
        if (!(pins & 1)) rtc->inputLatch = (pins >> 1) & 1;
        if (rising) {
            if (rtc->inputLatch) rtc->inputByte |= (uint8_t)(1u << rtc->bit);
            if (++rtc->bit == 8) return receive_command(rtc);
        }
    } else if (rtc->state == RTC_RECEIVE && falling) {
        receive_payload(rtc, pins & 2);
    } else if (rtc->state == RTC_SEND && falling) {
        rtc->output = (rtc->transfer[rtc->byte] >> rtc->bit) & 1;
        if (++rtc->bit == 8) {
            rtc->bit = 0;
            if (++rtc->byte == rtc->length) rtc->state = RTC_IDLE;
        }
    }
    return false;
}
