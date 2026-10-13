/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"

static void prefetch_sample(struct CupidTiming* timing, void* context, uint32_t late);
static void rtc_interrupt(struct CupidTiming* timing, void* context, uint32_t late);

static void schedule_rtc_interrupt(struct CupidGBA* gba) {
    struct CupidCartridge* cart = &gba->cartridge;
    cupid_timing_deschedule(&gba->timing, &cart->rtcInterruptEvent);
    const uint32_t delay = cart->rtcPresent ? cupid_rtc_next_interrupt(&cart->rtc) : 0;
    if (delay) cupid_timing_schedule(&gba->timing, &cart->rtcInterruptEvent, (int32_t)delay);
}

static void rtc_output_changed(struct CupidGBA* gba, bool previousLow) {
    /* The cartridge inverter makes RTC release a falling Game Pak IRQ edge. */
    if (gba->cartridge.rtcPresent && previousLow && !gba->cartridge.rtc.minuteLow)
        cupid_raise_irq_at(gba, IRQ_GAMEPAK, cupid_timing_global_time(&gba->timing));
    schedule_rtc_interrupt(gba);
}

void cupid_cartridge_rtc_sync(struct CupidGBA* gba) {
    const bool previousLow = gba->cartridge.rtc.minuteLow;
    cupid_rtc_sync(&gba->cartridge.rtc, cupid_timing_global_time(&gba->timing));
    rtc_output_changed(gba, previousLow);
}

void cupid_cartridge_rtc_reschedule(struct CupidGBA* gba) {
    cupid_rtc_sync(&gba->cartridge.rtc, cupid_timing_global_time(&gba->timing));
    schedule_rtc_interrupt(gba);
}

static void rtc_interrupt(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    /* Scheduler time is this edge's deadline, including during late dispatch. */
    cupid_cartridge_rtc_sync(context);
}

void cupid_cartridge_init(struct CupidGBA* gba) {
    cupid_cartridge_prefetch_cancel(gba);
    cupid_timing_deschedule(&gba->timing, &gba->cartridge.rtcInterruptEvent);
    memset(&gba->cartridge, 0, sizeof(gba->cartridge));
    cupid_rtc_init(&gba->cartridge.rtc, cupid_timing_global_time(&gba->timing));
    gba->cartridge.prefetchEvent = (struct CupidTimingEvent){.context = gba,
        .callback = prefetch_sample, .name = "Cartridge prefetch sample", .priority = 0x70};
    gba->cartridge.rtcInterruptEvent = (struct CupidTimingEvent){.context = gba,
        .callback = rtc_interrupt, .name = "Cartridge RTC interrupt", .priority = 0x40};
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
    cupid_cartridge_prefetch_cancel(gba);
    gba->cartridge.output = gba->cartridge.direction = 0;
    gba->cartridge.readable = false;
    gba->cartridge.opcodeValid = false;
    gba->cpu.memory.readOpcode = NULL;
    cupid_rtc_reset_bus(&gba->cartridge.rtc);
    cupid_cartridge_rtc_reschedule(gba);
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
        gba->cartridge.opcodeValid = false;
        gba->cpu.memory.readOpcode = gba->cartridge.readable ? cupid_cartridge_fetch_opcode : NULL;
        return true;
    default: return false;
    }
    if (gba->cartridge.rtcPresent) {
        const bool previousLow = gba->cartridge.rtc.minuteLow;
        if (cupid_rtc_write_pins_direction(&gba->cartridge.rtc, port_pins(&gba->cartridge),
                                           gba->cartridge.direction, cupid_timing_global_time(&gba->timing)))
            cupid_raise_irq_at(gba, IRQ_GAMEPAK, cupid_timing_global_time(&gba->timing));
        rtc_output_changed(gba, previousLow);
    }
    return true;
}

bool cupid_cartridge_fetch_opcode(struct CupidARM* cpu, uint32_t address, unsigned width, uint32_t* value) {
    struct CupidGBA* gba = cpu->master;
    if (gba->cartridge.opcodeValid) {
        gba->cartridge.opcodeValid = false;
        if (width == 4 && address == gba->cartridge.opcodeAddress) {
            *value = gba->cartridge.opcodeValue;
            return true;
        }
    }
    if (!cupid_cartridge_gpio_address(gba, address)) return false;
    const unsigned slot = ((address & (GBA_SIZE_ROM - 2)) - 0xC4) / 2;
    struct CupidCartridge* cart = &gba->cartridge;
    if (address == cart->prefetchBase + 0xC4 + slot * 2 && (cart->prefetchMask & (1u << slot))) {
        *value = cart->prefetchPorts[slot];
        if (width == 4) {
            const uint16_t high = slot == 0 && (cart->prefetchMask & 2)
                ? cart->prefetchPorts[1] : (uint16_t)cupid_memory_loadU16(cpu, address + 2, NULL);
            *value |= (uint32_t)high << 16;
        }
        cart->prefetchMask &= (uint8_t)~(width == 4 && slot == 0 ? 3u : 1u << slot);
        return true;
    }
    *value = width == 4 ? cupid_memory_load32(cpu, address, NULL) : cupid_memory_loadU16(cpu, address, NULL);
    return true;
}

void cupid_cartridge_prefetch_cancel(struct CupidGBA* gba) {
    struct CupidCartridge* cart = &gba->cartridge;
    if (cart->prefetchScheduled) cupid_timing_deschedule(&gba->timing, &cart->prefetchEvent);
    cart->prefetchScheduled = false;
    cart->prefetchMask = 0;
}

void cupid_cartridge_prefetch_schedule(struct CupidGBA* gba) {
    struct CupidCartridge* cart = &gba->cartridge;
    const struct CupidPrefetch* unit = &gba->memory.prefetchUnit;
    if (cart->prefetchScheduled || !cart->readable || !unit->active || !unit->remaining ||
        (unit->width != 2 && unit->width != 4)) return;
    if (unit->busy && unit->fetchKind != CUPID_PREFETCH_PENDING) return;
    /* Only the next sixteen physical ROM bytes can enter this FIFO. */
    const uint32_t physical = unit->tail & (GBA_SIZE_ROM - 1);
    if (physical > 0xC8 || physical + 16 <= 0xC4) return;
    const uint32_t base = unit->tail & ~(uint32_t)(GBA_SIZE_ROM - 1);
    const unsigned capacity = 16 / unit->width;
    for (unsigned slot = 0; slot < 3; slot++) {
        const uint32_t address = base + 0xC4 + slot * 2;
        if (address < unit->tail || !cupid_cartridge_gpio_address(gba, address) ||
            (cart->prefetchBase == base && (cart->prefetchMask & (1u << slot)))) continue;
        const unsigned distance = (address - unit->tail) / unit->width;
        if (unit->count + distance >= capacity) continue;
        uint64_t when = unit->epoch + unit->remaining + (uint64_t)distance * unit->duty;
        if (unit->width == 4 && !(address & 2))
            when -= unit->duty / 2;
        /* Never reconstruct a past sample from the provider's current level. */
        if (when < cupid_timing_global_time(&gba->timing)) continue;
        if (cart->prefetchBase != base) { cart->prefetchBase = base; cart->prefetchMask = 0; }
        cart->prefetchAddress = address;
        cart->prefetchScheduled = true;
        cupid_timing_schedule_absolute(&gba->timing, &cart->prefetchEvent, (int32_t)(uint32_t)when);
        return;
    }
}

static void prefetch_sample(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    struct CupidGBA* gba = context;
    struct CupidCartridge* cart = &gba->cartridge;
    const struct CupidPrefetch* unit = &gba->memory.prefetchUnit;
    cart->prefetchScheduled = false;
    if (!cart->readable || !unit->active || (unit->busy && unit->fetchKind != CUPID_PREFETCH_PENDING)) return;
    uint16_t value;
    if (!cupid_cartridge_gpio_read(gba, cart->prefetchAddress, &value)) return;
    const unsigned slot = ((cart->prefetchAddress & (GBA_SIZE_ROM - 2)) - 0xC4) / 2;
    cart->prefetchPorts[slot] = value;
    cart->prefetchMask |= (uint8_t)(1u << slot);
    /* PENDING freezes the fill counter while the CPU waits for that access.
     * Its epoch and remaining clocks still identify the original strobes. */
    if (!unit->busy) cupid_prefetch_update(gba, cupid_timing_global_time(&gba->timing));
    cupid_cartridge_prefetch_schedule(gba);
}

bool cupid_cartridge_fetch_bus(struct CupidGBA* gba, uint32_t address, unsigned width, unsigned cycles) {
    struct CupidCartridge* cart = &gba->cartridge;
    cart->opcodeValid = false;
    if (width != 4 || gba->memory.prefetchUnit.fetchKind != CUPID_PREFETCH_START ||
        !cupid_cartridge_gpio_address(gba, address)) return false;
    const unsigned highCycles = (unsigned)gba->memory.waitstatesSeq16[address >> GBA_BASE_OFFSET] + 1;
    if (cycles <= highCycles) return false;
    cupid_bus_advance(&gba->cpu, cycles - highCycles, true);
    const uint32_t low = cupid_memory_loadU16(&gba->cpu, address, NULL);
    cupid_bus_advance(&gba->cpu, highCycles, true);
    const uint32_t high = cupid_memory_loadU16(&gba->cpu, address + 2, NULL);
    cart->opcodeAddress = address;
    cart->opcodeValue = low | high << 16;
    cart->opcodeValid = true;
    return true;
}
