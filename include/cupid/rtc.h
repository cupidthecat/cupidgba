/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_RTC_H
#define CUPID_RTC_H

#include "cupid/types.h"

#define CUPID_RTC_CLOCK_HZ UINT64_C(16777216)

struct CupidRTCDateTime {
    uint8_t year, month, day, weekday, hour, minute, second;
};

struct CupidRTC {
    struct CupidRTCDateTime date;
    uint64_t lastClock;
    uint32_t fraction;
    uint8_t control;
    bool invalidSecond;
    bool powerSampling;
    uint8_t pins, inputLatch, inputByte, bit, byte, length, command;
    uint8_t transfer[7];
    uint8_t state;
    bool output;
};

void cupid_rtc_init(struct CupidRTC* rtc, uint64_t clock);
void cupid_rtc_sync(struct CupidRTC* rtc, uint64_t clock);
/* Rebase the execution clock after console reset without resetting the battery. */
void cupid_rtc_rebase(struct CupidRTC* rtc, uint64_t clock);
bool cupid_rtc_set_datetime(struct CupidRTC* rtc, const struct CupidRTCDateTime* date);
bool cupid_rtc_parse_datetime(const char* text, struct CupidRTCDateTime* date);
/* Explicit battery-clock input while execution is stopped; no host clock read. */
void cupid_rtc_advance_seconds(struct CupidRTC* rtc, uint64_t seconds);
void cupid_rtc_reset_registers(struct CupidRTC* rtc);
uint8_t cupid_rtc_read_control(struct CupidRTC* rtc);
void cupid_rtc_write_control(struct CupidRTC* rtc, uint8_t value);
/* The clock/device scheduler supplies each power-detector sample. */
void cupid_rtc_sample_power(struct CupidRTC* rtc, bool aboveThreshold);
void cupid_rtc_read_datetime(const struct CupidRTC* rtc, uint8_t data[7]);
void cupid_rtc_write_datetime(struct CupidRTC* rtc, const uint8_t data[7]);
void cupid_rtc_write_time(struct CupidRTC* rtc, const uint8_t data[3]);
void cupid_rtc_reset_bus(struct CupidRTC* rtc);
/* Physical GPIO levels: bit 0 clock, bit 1 serial data, bit 2 chip select. */
bool cupid_rtc_write_pins(struct CupidRTC* rtc, uint8_t pins, uint64_t clock);
uint8_t cupid_rtc_output_pins(const struct CupidRTC* rtc);

#endif
