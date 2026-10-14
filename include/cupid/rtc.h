/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_RTC_H
#define CUPID_RTC_H

#include "cupid/types.h"

#define CUPID_RTC_CLOCK_HZ UINT64_C(16777216)
/* First whole master clock at or beyond the nominal 10 ms carry interval. */
#define CUPID_RTC_MINUTE_CARRY_CLOCKS ((uint32_t)((CUPID_RTC_CLOCK_HZ + 99) / 100))

struct CupidRTCDateTime {
    uint8_t year, month, day, weekday, hour, minute, second;
};

struct CupidRTC {
    struct CupidRTCDateTime date;
    uint64_t lastClock;
    uint32_t fraction;
    uint16_t interruptParameters;
    uint8_t control;
    bool invalidSecond;
    bool powerSampling;
    bool interruptLow;
    bool minuteCarryActive;
    uint8_t pins, driveMask, inputLatch, inputByte, bit, byte, length, command;
    uint8_t transfer[7];
    uint8_t state;
    bool output;
};

void cupid_rtc_init(struct CupidRTC* rtc, uint64_t clock);
void cupid_rtc_sync(struct CupidRTC* rtc, uint64_t clock);
/* Explicit elapsed battery clocks; leaves the console-clock anchor unchanged. */
void cupid_rtc_advance_clocks(struct CupidRTC* rtc, uint64_t clocks);
/* Clocks to the next output transition or long-wait checkpoint; zero if idle. */
uint32_t cupid_rtc_next_interrupt(const struct CupidRTC* rtc);
/* Rebase the execution clock after console reset without resetting the battery. */
void cupid_rtc_rebase(struct CupidRTC* rtc, uint64_t clock);
bool cupid_rtc_set_datetime(struct CupidRTC* rtc, const struct CupidRTCDateTime* date);
bool cupid_rtc_parse_datetime(const char* text, struct CupidRTCDateTime* date);
/* Offline elapsed seconds; no host clock read or console interrupt replay. */
void cupid_rtc_advance_seconds(struct CupidRTC* rtc, uint64_t seconds);
void cupid_rtc_reset_registers(struct CupidRTC* rtc);
uint8_t cupid_rtc_read_control(struct CupidRTC* rtc);
void cupid_rtc_write_control(struct CupidRTC* rtc, uint8_t value);
void cupid_rtc_write_interrupt_parameters(struct CupidRTC* rtc, uint16_t value);
/* The clock/device scheduler supplies each power-detector sample. */
void cupid_rtc_sample_power(struct CupidRTC* rtc, bool aboveThreshold);
void cupid_rtc_read_datetime(const struct CupidRTC* rtc, uint8_t data[7]);
void cupid_rtc_write_datetime(struct CupidRTC* rtc, const uint8_t data[7]);
void cupid_rtc_write_time(struct CupidRTC* rtc, const uint8_t data[3]);
void cupid_rtc_reset_bus(struct CupidRTC* rtc);
/* Physical GPIO levels: bit 0 clock, bit 1 serial data, bit 2 chip select. */
bool cupid_rtc_write_pins(struct CupidRTC* rtc, uint8_t pins, uint64_t clock);
bool cupid_rtc_write_pins_direction(struct CupidRTC* rtc, uint8_t pins, uint8_t driveMask, uint64_t clock);
uint8_t cupid_rtc_output_pins(const struct CupidRTC* rtc);

#endif
