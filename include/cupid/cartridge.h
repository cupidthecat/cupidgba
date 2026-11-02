/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file cartridge.h
 * @brief Cartridge devices, board selection and battery ownership.
 *
 * - Group RTC, solar, gyro, tilt, rumble and card-reader state per cartridge.
 * - Declare pin routing, lifecycle and battery persistence interfaces.
 */
#ifndef CUPID_CARTRIDGE_H
#define CUPID_CARTRIDGE_H

#include "cupid/card-reader.h"
#include "cupid/gyro.h"
#include "cupid/rtc.h"
#include "cupid/solar.h"
#include "cupid/tilt.h"
#include "cupid/timing.h"

struct CupidGBA;
struct CupidARM;

struct CupidCartridge {
    struct CupidRTC rtc;
    struct CupidSolar solar;
    struct CupidGyro gyro;
    struct CupidTilt tilt;
    struct CupidCardReader cardReader;
    uint64_t identity;
    uint32_t opcodeAddress, opcodeValue;
    struct CupidTimingEvent prefetchEvent;
    struct CupidTimingEvent rtcInterruptEvent;
    uint32_t prefetchBase, prefetchAddress;
    uint16_t prefetchPorts[3];
    uint8_t prefetchMask;
    uint32_t cardPrefetchAddress[8];
    uint16_t cardPrefetchValue[8];
    uint8_t cardPrefetchMask;
    uint8_t output, direction;
    bool rtcPresent, readable, opcodeValid;
    bool solarPresent, rumblePresent, rumbleActive;
    bool gyroPresent;
    bool tiltPresent;
    bool cardReaderPresent;
    bool prefetchScheduled;
    uint8_t (*inputPins)(void *context);
    void *inputContext;
    void (*rumbleChanged)(void *context, bool active, uint64_t clock);
    void *rumbleContext;
};

void cupid_cartridge_init(struct CupidGBA *gba);
void cupid_cartridge_deinit(struct CupidGBA *gba);
void cupid_cartridge_attach(struct CupidGBA *gba, const uint8_t *rom, size_t size);
void cupid_cartridge_enable_rtc(struct CupidGBA *gba, bool enabled);
void cupid_cartridge_enable_solar(struct CupidGBA *gba, bool enabled);
void cupid_cartridge_set_solar_level(struct CupidGBA *gba, uint8_t level);
void cupid_cartridge_enable_gyro(struct CupidGBA *gba, bool enabled);
bool cupid_cartridge_set_gyro_sample(struct CupidGBA *gba, uint16_t sample);
void cupid_cartridge_enable_tilt(struct CupidGBA *gba, bool enabled);
bool cupid_cartridge_set_tilt_sample(struct CupidGBA *gba, uint16_t x, uint16_t y);
void cupid_cartridge_enable_card_reader(struct CupidGBA *gba, bool enabled);
bool cupid_cartridge_queue_card(struct CupidGBA *gba, const void *data, size_t size);
bool cupid_cartridge_card_address(const struct CupidGBA *gba, uint32_t address);
bool cupid_cartridge_card_read(struct CupidGBA *gba, uint32_t address, uint16_t *value);
bool cupid_cartridge_card_write(struct CupidGBA *gba, uint32_t address, uint16_t value);
bool cupid_cartridge_card_read_port(struct CupidGBA *gba, uint32_t address, uint8_t *value);
bool cupid_cartridge_card_write_port(struct CupidGBA *gba, uint32_t address, uint8_t value);
bool cupid_cartridge_tilt_read(struct CupidGBA *gba, uint32_t address, uint8_t *value);
bool cupid_cartridge_tilt_write(struct CupidGBA *gba, uint32_t address, uint8_t value);
void cupid_cartridge_enable_rumble(struct CupidGBA *gba, bool enabled);
void cupid_cartridge_set_rumble_output(struct CupidGBA *gba, void (*output)(void *, bool, uint64_t),
                                       void *context);
void cupid_cartridge_set_input(struct CupidGBA *gba, uint8_t (*input)(void *), void *context);
void cupid_cartridge_reset_bus(struct CupidGBA *gba);
/* Reconfigure the battery clock without replaying edges from powered-off time. */
void cupid_cartridge_rtc_reschedule(struct CupidGBA *gba);
void cupid_cartridge_rtc_sync(struct CupidGBA *gba);
uint64_t cupid_cartridge_advance_stopped_time(struct CupidGBA *gba, uint64_t clocks);
bool cupid_cartridge_gpio_address(const struct CupidGBA *gba, uint32_t address);
bool cupid_cartridge_gpio_read(struct CupidGBA *gba, uint32_t address, uint16_t *value);
bool cupid_cartridge_gpio_write(struct CupidGBA *gba, uint32_t address, uint16_t value);
bool cupid_cartridge_fetch_opcode(struct CupidARM *cpu, uint32_t address, unsigned width,
                                  uint32_t *value);
bool cupid_cartridge_fetch_bus(struct CupidGBA *gba, uint32_t address, unsigned width,
                               unsigned cycles);
void cupid_cartridge_prefetch_schedule(struct CupidGBA *gba);
void cupid_cartridge_prefetch_cancel(struct CupidGBA *gba);
/* Battery state uses a separate file; the caller supplies any host timestamp. */
bool cupid_cartridge_load_rtc(struct CupidGBA *gba, const char *path, uint64_t *hostStamp);
bool cupid_cartridge_save_rtc(struct CupidGBA *gba, const char *path, uint64_t hostStamp);

#endif
