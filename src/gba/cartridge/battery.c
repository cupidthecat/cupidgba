/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/file.h"
#include <errno.h>

enum { BATTERY_SIZE = 44 };

static uint64_t little(const uint8_t* data, unsigned bytes) {
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; i++) value |= (uint64_t)data[i] << (8 * i);
    return value;
}

static void encode(uint8_t* data, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; i++) data[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t checksum(const uint8_t* data) {
    uint32_t value = UINT32_C(2166136261);
    for (unsigned i = 0; i < BATTERY_SIZE - 4; i++) value = (value ^ data[i]) * UINT32_C(16777619);
    return value;
}

bool cupid_cartridge_load_rtc(struct CupidGBA* gba, const char* path, uint64_t* hostStamp) {
    if (!gba->cartridge.rtcPresent) { if (hostStamp) *hostStamp = 0; errno = 0; return true; }
    uint8_t data[BATTERY_SIZE];
    if (!cupid_file_read_exact(path, data, sizeof(data))) return false;
    const bool version1 = !memcmp(data, "CPDRTC01", 8);
    const bool version2 = !memcmp(data, "CPDRTC02", 8);
    if ((!version1 && !version2) || little(data + 8, 8) != gba->cartridge.identity ||
        little(data + 40, 4) != checksum(data) || (data[23] & ~0xEA) ||
        (data[28] & ~7) || ((data[28] & 4) && !(data[23] & (version1 ? 8 : 0x2A))) ||
        (version1 && (data[29] || data[30])) || data[31] ||
        little(data + 24, 4) >= CUPID_RTC_CLOCK_HZ) { errno = EINVAL; return false; }
    struct CupidRTC restored;
    cupid_rtc_init(&restored, cupid_timing_global_time(&gba->timing));
    struct CupidRTCDateTime date = {data[16], data[17], data[18], data[19], data[20], data[21], data[22]};
    const bool invalidSecond = data[28] & 1;
    const unsigned rawSecond = date.second;
    if (invalidSecond) {
        if (rawSecond > 0x7F || ((rawSecond & 15) < 10 && (rawSecond >> 4) * 10 + (rawSecond & 15) < 60)) {
            errno = EINVAL; return false;
        }
        date.second = 0;
    }
    if (!cupid_rtc_set_datetime(&restored, &date)) { errno = EINVAL; return false; }
    restored.date.second = (uint8_t)rawSecond;
    restored.invalidSecond = invalidSecond;
    restored.control = data[23];
    restored.interruptParameters = version2 ? (uint16_t)little(data + 29, 2) : 0;
    restored.fraction = (uint32_t)little(data + 24, 4);
    restored.powerSampling = (data[28] & 2) != 0;
    restored.interruptLow = (data[28] & 4) != 0;
    gba->cartridge.rtc = restored;
    cupid_cartridge_reset_bus(gba);
    if (hostStamp) *hostStamp = little(data + 32, 8);
    errno = 0;
    return true;
}

bool cupid_cartridge_save_rtc(struct CupidGBA* gba, const char* path, uint64_t hostStamp) {
    if (!gba->cartridge.rtcPresent) { errno = 0; return true; }
    struct CupidRTC* rtc = &gba->cartridge.rtc;
    cupid_cartridge_rtc_sync(gba);
    uint8_t data[BATTERY_SIZE] = {0};
    const bool version2 = rtc->interruptParameters || (rtc->interruptLow && !(rtc->control & 8));
    memcpy(data, version2 ? "CPDRTC02" : "CPDRTC01", 8);
    encode(data + 8, gba->cartridge.identity, 8);
    data[16] = rtc->date.year; data[17] = rtc->date.month; data[18] = rtc->date.day;
    data[19] = rtc->date.weekday; data[20] = rtc->date.hour; data[21] = rtc->date.minute;
    data[22] = rtc->date.second; data[23] = rtc->control;
    encode(data + 24, rtc->fraction, 4);
    data[28] = (rtc->invalidSecond ? 1 : 0) | (rtc->powerSampling ? 2 : 0) | (rtc->interruptLow ? 4 : 0);
    encode(data + 29, rtc->interruptParameters, 2);
    encode(data + 32, hostStamp, 8);
    encode(data + 40, checksum(data), 4);
    return cupid_file_replace(path, data, sizeof(data));
}
