/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"

static const char* externalBios;

static void clear_and_clock_edges(void) {
    struct CupidSolar solar;
    cupid_solar_init(&solar);
    cupid_solar_set_level(&solar, 255);
    CHECK(!cupid_solar_output_pins(&solar));
    cupid_solar_write_pins(&solar, 1);
    cupid_solar_write_pins(&solar, 1);
    CHECK(!solar.counter);
    cupid_solar_write_pins(&solar, 0);
    CHECK(solar.counter == 1 && cupid_solar_output_pins(&solar) == 8);
    cupid_solar_write_pins(&solar, 0);
    CHECK(solar.counter == 1);
    cupid_solar_write_pins(&solar, 3);
    cupid_solar_write_pins(&solar, 2); /* Clear dominates this falling edge. */
    CHECK(!solar.counter && !cupid_solar_output_pins(&solar));
    cupid_solar_write_pins(&solar, 0);
    CHECK(!solar.counter);
    cupid_solar_write_pins(&solar, 5);
    cupid_solar_write_pins(&solar, 4); /* Pin 2 does not select this circuit. */
    CHECK(solar.counter == 1);
    cupid_solar_reset_bus(&solar);
    CHECK(!solar.counter && !solar.clockHigh && solar.threshold == 0);
}

static void all_levels_and_counter_converter_wrap(void) {
    unsigned observations = 0;
    for (unsigned level = 0; level < 256; level++) {
        struct CupidSolar solar;
        cupid_solar_init(&solar);
        cupid_solar_set_level(&solar, (uint8_t)level);
        unsigned high = 0;
        for (unsigned pulse = 1; pulse <= 8192; pulse++) {
            cupid_solar_write_pins(&solar, 1);
            cupid_solar_write_pins(&solar, 0);
            CHECK(solar.counter == pulse % 4096);
            if (!(pulse % 256)) CHECK(!cupid_solar_output_pins(&solar));
            else if (pulse % 256 == 256 - level) CHECK(cupid_solar_output_pins(&solar) == 8);
            else if (pulse % 256 == 255 - level) CHECK(!cupid_solar_output_pins(&solar));
            high += cupid_solar_output_pins(&solar) != 0;
            observations++;
        }
        /* Each complete DAC cycle has exactly 'level' high slots. */
        CHECK(high == level * 32);
    }
    CHECK(observations == 2097152);
}

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 0x4000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 0x4000;
    gba->memory.romMask = 0x3FFF;
    const uint8_t raw[] = {0x33, 0x44, 0x77, 0x88, 0xCC, 0xDD, 0x5A, 0xA5};
    memcpy(gba->memory.rom + 0xC4, raw, sizeof(raw));
    if (externalBios) CHECK(cupid_load_bios(gba, externalBios));
    cupid_gba_reset(gba);
    cupid_cartridge_enable_solar(gba, true);
    return gba;
}

static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

static uint32_t access_port(struct CupidGBA* gba, bool thumb, unsigned width,
                            uint32_t address, uint32_t value, bool load) {
    static const uint16_t thumbLoad[] = {0x7801, 0x8801, 0x6801};
    static const uint16_t thumbStore[] = {0x7001, 0x8001, 0x6001};
    static const uint32_t armLoad[] = {0xE5D01000, 0xE1D010B0, 0xE5901000};
    static const uint32_t armStore[] = {0xE5C01000, 0xE1C010B0, 0xE5801000};
    const unsigned slot = width == 4 ? 2 : width - 1;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.cpsr.t = thumb;
    if (thumb) {
        const uint16_t code[] = {load ? thumbLoad[slot] : thumbStore[slot], 0x46C0, 0x46C0};
        memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        CupidThumbWritePC(&gba->cpu);
    } else {
        const uint32_t code[] = {load ? armLoad[slot] : armStore[slot], 0xE1A00000, 0xE1A00000};
        memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        CupidARMWritePC(&gba->cpu);
    }
    gba->cpu.gprs[0] = (int32_t)address;
    gba->cpu.gprs[1] = (int32_t)value;
    cupid_gba_step(gba);
    return (uint32_t)gba->cpu.gprs[1];
}

static void instruction_driver_and_physical_windows(void) {
    const uint8_t levels[] = {0, 1, 64, 128, 255};
    unsigned complete = 0;
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        for (unsigned bank = 0; bank < 3; bank++) {
            for (unsigned width = 2; width <= 4; width += 2) {
                for (unsigned prefetch = 0; prefetch < 2; prefetch++) {
                    struct CupidGBA* gba = machine();
                    const uint32_t port = 0x080000C4 + bank * 0x02000000;
                    const uint32_t ordinaryUpper = access_port(gba, thumb, 2,
                        port + 0x01000000, 0, true);
                    cupid_memory_update_waitstates(gba, (uint16_t)(prefetch << 14));
                    access_port(gba, thumb, 2, port + 4, 1, false);
                    access_port(gba, thumb, 2, port + 2, 3, false);
                    /* Byte writes cannot clock or clear the cartridge port. */
                    access_port(gba, thumb, 1, port, 3, false);
                    CHECK(!gba->cartridge.output && gba->cartridge.direction == 3);
                    for (unsigned l = 0; l < sizeof(levels); l++) {
                        cupid_cartridge_set_solar_level(gba, levels[l]);
                        access_port(gba, thumb, width, port, 2 | (width == 4 ? 3u << 16 : 0), false);
                        access_port(gba, thumb, 2, port, 0, false);
                        for (unsigned pulse = 1; pulse <= 256; pulse++) {
                            access_port(gba, thumb, 2, port, 1, false);
                            access_port(gba, thumb, 2, port, 0, false);
                            uint32_t value = access_port(gba, thumb, width, port, 0, true);
                            const unsigned flag = pulse < 256 && pulse >= 256u - levels[l] ? 8 : 0;
                            CHECK(value == (flag | (width == 4 ? 3u << 16 : 0)));
                            CHECK(access_port(gba, thumb, 1, port, 0, true) == flag);
                        }
                        complete++;
                    }
                    const uint8_t raw[] = {0x33, 0x44, 0x77, 0x88, 0xCC, 0xDD, 0x5A, 0xA5};
                    CHECK(!memcmp(gba->memory.rom + 0xC4, raw, sizeof(raw)));
                    access_port(gba, thumb, 2, port + 4, 0, false);
                    CHECK(access_port(gba, thumb, 2, port, 0, true) == 0x4433);
                    CHECK(!cupid_cartridge_gpio_address(gba, port + 0x01000000));
                    CHECK(access_port(gba, thumb, 2, port + 0x01000000, 0, true) == ordinaryUpper);
                    destroy(gba);
                }
            }
        }
    }
    CHECK(complete == 120);
}

static void live_input_direction_reset_and_shared_rtc(void) {
    struct CupidGBA* gba = machine();
    cupid_cartridge_enable_rtc(gba, true);
    const struct CupidRTCDateTime date = {26, 10, 14, 3, 12, 34, 56};
    CHECK(cupid_rtc_set_datetime(&gba->cartridge.rtc, &date));
    cupid_rtc_write_control(&gba->cartridge.rtc, 0x40);
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 3, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 1, NULL);
    /* Disabling the clock output lowers the pin and clocks the counter. */
    cupid_memory_store16(&gba->cpu, 0x080000C6, 2, NULL);
    CHECK(gba->cartridge.solar.counter == 1);
    cupid_cartridge_set_solar_level(gba, 255);
    CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) == 8);
    cupid_cartridge_set_solar_level(gba, 0);
    CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) == 0);
    cupid_cartridge_set_solar_level(gba, 255);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 2, NULL);
    CHECK(!gba->cartridge.solar.counter);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 11, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 8, NULL);
    CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) == 8); /* Output wins. */
    cupid_gba_reset(gba);
    CHECK(gba->cartridge.solarPresent && gba->cartridge.rtcPresent);
    CHECK(!gba->cartridge.solar.counter && gba->cartridge.solar.threshold == 0);
    CHECK(!memcmp(&gba->cartridge.rtc.date, &date, sizeof(date)));
    /* Run the RTC status driver with solar still connected to pins 0/1/3. */
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 7, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 1, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 5, NULL);
    for (unsigned bit = 0; bit < 8; bit++) {
        const uint16_t pins = (uint16_t)(4 | ((0xC6 >> bit) & 1) << 1);
        cupid_memory_store16(&gba->cpu, 0x080000C4, pins, NULL);
        cupid_timing_tick(&gba->timing, 128);
        cupid_memory_store16(&gba->cpu, 0x080000C4, pins | 1, NULL);
        cupid_timing_tick(&gba->timing, 128);
    }
    cupid_memory_store16(&gba->cpu, 0x080000C6, 5, NULL);
    unsigned control = 0;
    for (unsigned bit = 0; bit < 8; bit++) {
        cupid_memory_store16(&gba->cpu, 0x080000C4, 4, NULL);
        cupid_timing_tick(&gba->timing, 128);
        cupid_memory_store16(&gba->cpu, 0x080000C4, 5, NULL);
        cupid_timing_tick(&gba->timing, 128);
        if (cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) & 2) control |= 1u << bit;
    }
    CHECK(control == 0x40);
    destroy(gba);
}

struct LightChange { struct CupidGBA* gba; uint64_t clock; unsigned calls; };
static void change_light(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct LightChange* event = context;
    event->clock = cupid_timing_global_time(timing);
    event->calls++;
    cupid_cartridge_set_solar_level(event->gba, 255);
}

static void dma_and_prefetch_keep_original_sample_clocks(void) {
    for (unsigned wrap = 0; wrap < 2; wrap++) {
        struct CupidGBA* gba = machine();
        cupid_cartridge_set_solar_level(gba, 0);
        cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
        cupid_memory_store16(&gba->cpu, 0x080000C6, 3, NULL);
        cupid_memory_store16(&gba->cpu, 0x080000C4, 1, NULL);
        cupid_memory_store16(&gba->cpu, 0x080000C4, 0, NULL);
        cupid_timing_clear(&gba->timing);
        const uint64_t base = wrap ? UINT32_MAX - 6u : 0;
        gba->timing.globalCycles = base;
        gba->timing.masterCycles = (uint32_t)base;
        struct LightChange change = {.gba = gba};
        struct CupidTimingEvent event = {.context = &change, .callback = change_light, .priority = 0x60};
        cupid_timing_schedule(&gba->timing, &event, 9);
        cupid_dma_write_sad(gba, 3, 0x080000C4);
        cupid_dma_write_dad(gba, 3, GBA_BASE_IWRAM);
        cupid_dma_write_cnt_lo(gba, 3, 1);
        cupid_dma_write_cnt(gba, 3, 0xC400);
        cupid_timing_tick(&gba->timing, 32); /* Dispatch all deadlines in one call. */
        CHECK(change.calls == 1 && change.clock == base + 9);
        CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM, NULL) == 0x00030000);
        CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) == 8);
        CHECK(gba->irqf & IRQ_DMA3);
        destroy(gba);
    }
    for (unsigned width = 2; width <= 4; width += 2) {
        struct CupidGBA* gba = machine();
        cupid_cartridge_set_solar_level(gba, 0);
        cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
        cupid_memory_store16(&gba->cpu, 0x080000C6, 3, NULL);
        cupid_memory_store16(&gba->cpu, 0x080000C4, 1, NULL);
        cupid_memory_store16(&gba->cpu, 0x080000C4, 0, NULL);
        cupid_memory_update_waitstates(gba, 0x4000);
        _CupidSetMode(&gba->cpu, width == 2 ? MODE_THUMB : MODE_ARM);
        gba->cpu.memory.timed = true;
        const uint32_t preceding = 0x080000C4 - width;
        gba->cpu.memory.setActiveRegion(&gba->cpu, preceding);
        cupid_bus_fetch(&gba->cpu, preceding, width, false);
        (void)CupidReadOpcode(&gba->cpu, preceding, width);
        cupid_bus_finish(&gba->cpu);
        cupid_timing_tick(&gba->timing, 24);
        CHECK(gba->cartridge.prefetchMask & 1);
        cupid_cartridge_set_solar_level(gba, 255);
        cupid_bus_fetch(&gba->cpu, 0x080000C4, width, true);
        const uint32_t value = CupidReadOpcode(&gba->cpu, 0x080000C4, width);
        cupid_bus_finish(&gba->cpu);
        CHECK(value == (width == 4 ? 0x00030000u : 0u));
        CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) == 8);
        destroy(gba);
    }
}

int main(int argc, char** argv) {
    if (argc == 3) externalBios = argv[2];
    else if (argc != 2) return 2;
    if (!strcmp(argv[1], "--component")) {
        RUN(clear_and_clock_edges);
        RUN(all_levels_and_counter_converter_wrap);
    } else if (!strcmp(argv[1], "--bus")) {
        RUN(instruction_driver_and_physical_windows);
    } else if (!strcmp(argv[1], "--interactions")) {
        RUN(live_input_direction_reset_and_shared_rtc);
        RUN(dma_and_prefetch_keep_original_sample_clocks);
    } else return 2;
    return 0;
}
