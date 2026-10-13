/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_CARTRIDGE_H
#define CUPID_CARTRIDGE_H

#include "cupid/rtc.h"
#include "cupid/timing.h"

struct CupidGBA;
struct CupidARM;

struct CupidCartridge {
    struct CupidRTC rtc;
    uint64_t identity;
    uint32_t opcodeAddress, opcodeValue;
    struct CupidTimingEvent prefetchEvent;
    uint32_t prefetchBase, prefetchAddress;
    uint16_t prefetchPorts[3];
    uint8_t prefetchMask;
    uint8_t output, direction;
    bool rtcPresent, readable, opcodeValid;
    bool prefetchScheduled;
    uint8_t (*inputPins)(void* context);
    void* inputContext;
};

void cupid_cartridge_init(struct CupidGBA* gba);
void cupid_cartridge_attach(struct CupidGBA* gba, const uint8_t* rom, size_t size);
void cupid_cartridge_enable_rtc(struct CupidGBA* gba, bool enabled);
void cupid_cartridge_set_input(struct CupidGBA* gba, uint8_t (*input)(void*), void* context);
void cupid_cartridge_reset_bus(struct CupidGBA* gba);
bool cupid_cartridge_gpio_address(const struct CupidGBA* gba, uint32_t address);
bool cupid_cartridge_gpio_read(struct CupidGBA* gba, uint32_t address, uint16_t* value);
bool cupid_cartridge_gpio_write(struct CupidGBA* gba, uint32_t address, uint16_t value);
bool cupid_cartridge_fetch_opcode(struct CupidARM* cpu, uint32_t address, unsigned width, uint32_t* value);
bool cupid_cartridge_fetch_bus(struct CupidGBA* gba, uint32_t address, unsigned width, unsigned cycles);
void cupid_cartridge_prefetch_schedule(struct CupidGBA* gba);
void cupid_cartridge_prefetch_cancel(struct CupidGBA* gba);
/* Battery state uses a separate file; the caller supplies any host timestamp. */
bool cupid_cartridge_load_rtc(struct CupidGBA* gba, const char* path, uint64_t* hostStamp);
bool cupid_cartridge_save_rtc(struct CupidGBA* gba, const char* path, uint64_t hostStamp);

#endif
