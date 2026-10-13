/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"

void cupid_cartridge_init(struct CupidGBA* gba) {
    memset(&gba->cartridge, 0, sizeof(gba->cartridge));
    cupid_rtc_init(&gba->cartridge.rtc, cupid_timing_global_time(&gba->timing));
}

void cupid_cartridge_enable_rtc(struct CupidGBA* gba, bool enabled) {
    gba->cartridge.rtcPresent = enabled;
    cupid_cartridge_reset_bus(gba);
}

void cupid_cartridge_set_input(struct CupidGBA* gba, uint8_t (*input)(void*), void* context) {
    gba->cartridge.inputPins = input;
    gba->cartridge.inputContext = context;
}

void cupid_cartridge_attach(struct CupidGBA* gba, const uint8_t* rom, size_t size) {
    static const char marker[] = "SIIRTC_V";
    bool detected = false;
    for (size_t i = 0; i + sizeof(marker) + 2 <= size; i++) {
        if (rom[i] != 'S' || memcmp(rom + i, marker, sizeof(marker) - 1)) continue;
        const uint8_t* version = rom + i + sizeof(marker) - 1;
        detected = version[0] >= '0' && version[0] <= '9' &&
                   version[1] >= '0' && version[1] <= '9' &&
                   version[2] >= '0' && version[2] <= '9';
        if (detected) break;
    }
    cupid_cartridge_init(gba);
    gba->cartridge.identity = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < size; i++) {
        gba->cartridge.identity ^= rom[i];
        gba->cartridge.identity *= UINT64_C(1099511628211);
    }
    cupid_cartridge_enable_rtc(gba, detected);
}

void cupid_cartridge_reset_bus(struct CupidGBA* gba) {
    gba->cartridge.output = gba->cartridge.direction = 0;
    gba->cartridge.readable = false;
    gba->cpu.memory.readOpcode = NULL;
    cupid_rtc_reset_bus(&gba->cartridge.rtc);
}

bool cupid_cartridge_gpio_address(const struct CupidGBA* gba, uint32_t address) {
    if ((!gba->cartridge.rtcPresent && !gba->cartridge.inputPins) || !gba->memory.rom) return false;
    const unsigned region = address >> GBA_BASE_OFFSET;
    if (region < GBA_REGION_ROM0 || region > GBA_REGION_ROM2_EX) return false;
    const uint32_t physical = address & (GBA_SIZE_ROM - 2);
    return physical >= 0xC4 && physical <= 0xC8;
}

static uint8_t port_pins(const struct CupidCartridge* cart) {
    uint8_t input = cart->inputPins ? cart->inputPins(cart->inputContext) : 0;
    if (cart->rtcPresent) input = (input & ~2) | cupid_rtc_output_pins(&cart->rtc);
    return (cart->output & cart->direction) | (input & ~cart->direction & 15);
}

bool cupid_cartridge_gpio_read(struct CupidGBA* gba, uint32_t address, uint16_t* value) {
    if (!gba->cartridge.readable || !cupid_cartridge_gpio_address(gba, address)) return false;
    switch (address & (GBA_SIZE_ROM - 2)) {
    case 0xC4: *value = port_pins(&gba->cartridge); break;
    case 0xC6: *value = gba->cartridge.direction; break;
    case 0xC8: *value = 1; break;
    default: return false;
    }
    return true;
}

bool cupid_cartridge_gpio_write(struct CupidGBA* gba, uint32_t address, uint16_t value) {
    if (!cupid_cartridge_gpio_address(gba, address)) return false;
    switch (address & (GBA_SIZE_ROM - 2)) {
    case 0xC4: gba->cartridge.output = value & 15; break;
    case 0xC6: gba->cartridge.direction = value & 15; break;
    case 0xC8:
        gba->cartridge.readable = value & 1;
        gba->cpu.memory.readOpcode = gba->cartridge.readable ? cupid_cartridge_fetch_opcode : NULL;
        return true;
    default: return false;
    }
    if (gba->cartridge.rtcPresent && cupid_rtc_write_pins(&gba->cartridge.rtc, port_pins(&gba->cartridge),
                             cupid_timing_global_time(&gba->timing)))
        cupid_raise_irq(gba, IRQ_GAMEPAK, 0);
    return true;
}

bool cupid_cartridge_fetch_opcode(struct CupidARM* cpu, uint32_t address, unsigned width, uint32_t* value) {
    struct CupidGBA* gba = cpu->master;
    if (!cupid_cartridge_gpio_address(gba, address)) return false;
    *value = width == 4 ? cupid_memory_load32(cpu, address, NULL) : cupid_memory_loadU16(cpu, address, NULL);
    return true;
}
