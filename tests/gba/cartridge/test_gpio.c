/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"

static uint8_t input_pins(void* context) {
    return *(uint8_t*)context;
}

static struct CupidGBA* machine(bool rtc) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 0x4000);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 0x4000;
    gba->memory.romMask = 0x3FFF;
    const uint8_t raw[] = {0x33, 0x44, 0x77, 0x88, 0xCC, 0xDD, 0x5A, 0xA5};
    memcpy(gba->memory.rom + 0xC4, raw, sizeof(raw));
    cupid_gba_reset(gba);
    cupid_cartridge_enable_rtc(gba, rtc);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void directions_latches_input_and_pristine_cartridge(void) {
    struct CupidGBA* gba = machine(false);
    uint8_t input = 0;
    cupid_cartridge_set_input(gba, input_pins, &input);
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    for (unsigned direction = 0; direction < 16; direction++) {
        cupid_memory_store16(&gba->cpu, 0x080000C6, (int16_t)(0xFFF0 | direction), NULL);
        CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C6, NULL) == direction);
        for (unsigned output = 0; output < 16; output++) {
            cupid_memory_store16(&gba->cpu, 0x080000C4, (int16_t)(0xFFF0 | output), NULL);
            for (input = 0; input < 16; input++) {
                const unsigned expected = (output & direction) | (input & ~direction & 15);
                CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) == expected);
                CHECK(cupid_memory_load8(&gba->cpu, 0x080000C4, NULL) == expected);
                CHECK(cupid_memory_load8(&gba->cpu, 0x080000C5, NULL) == 0);
            }
        }
    }
    const uint8_t raw[] = {0x33, 0x44, 0x77, 0x88, 0xCC, 0xDD, 0x5A, 0xA5};
    CHECK(!memcmp(gba->memory.rom + 0xC4, raw, sizeof(raw)));
    cupid_memory_store16(&gba->cpu, 0x080000C8, 0xFFFE, NULL);
    CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) == 0x4433);
    CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C6, NULL) == 0x8877);
    CHECK(cupid_memory_load32(&gba->cpu, 0x080000C8, NULL) == 0xA55ADDCC);
    destroy(gba);
}

static void word_lanes_opcode_fetch_and_read_control(void) {
    struct CupidGBA* gba = machine(true);
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    cupid_memory_store32(&gba->cpu, 0x080000C4, 0x00070005, NULL);
    CHECK(cupid_memory_load32(&gba->cpu, 0x080000C4, NULL) == 0x00070005);
    CHECK(cupid_memory_load32(&gba->cpu, 0x080000C8, NULL) == 0xA55A0001);
    gba->cpu.cpsr.t = 0;
    _CupidSetMode(&gba->cpu, MODE_ARM);
    gba->cpu.gprs[ARM_PC] = 0x080000C4;
    CupidARMWritePC(&gba->cpu);
    CHECK(gba->cpu.prefetch[0] == 0x00070005 && gba->cpu.prefetch[1] == 0xA55A0001);
    gba->cpu.cpsr.t = 1;
    _CupidSetMode(&gba->cpu, MODE_THUMB);
    gba->cpu.gprs[ARM_PC] = 0x080000C4;
    CupidThumbWritePC(&gba->cpu);
    CHECK(gba->cpu.prefetch[0] == 5 && gba->cpu.prefetch[1] == 7);
    cupid_memory_store16(&gba->cpu, 0x080000C8, 0, NULL);
    gba->cpu.gprs[ARM_PC] = 0x080000C4;
    CupidThumbWritePC(&gba->cpu);
    CHECK(gba->cpu.prefetch[0] == 0x4433 && gba->cpu.prefetch[1] == 0x8877);
    destroy(gba);
}

struct Observation {
    struct CupidGBA* gba;
    uint8_t input;
    uint16_t data, direction;
};

static void observe_write(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    struct Observation* observed = context;
    observed->data = (uint16_t)cupid_memory_loadU16(&observed->gba->cpu, 0x080000C4, NULL);
    observed->direction = (uint16_t)cupid_memory_loadU16(&observed->gba->cpu, 0x080000C6, NULL);
}

static void change_input(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    struct Observation* observed = context;
    observed->input = 9;
}

static void timed_word_ports_have_separate_bus_endpoints(void) {
    struct CupidGBA* gba = machine(true);
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 15, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 1, NULL);
    struct Observation observed = {.gba = gba};
    struct CupidTimingEvent event = {.context = &observed, .callback = observe_write, .priority = 0x60};
    cupid_timing_schedule(&gba->timing, &event, 6);
    gba->cpu.memory.timed = true;
    int cycles = 0;
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    cupid_memory_store32(&gba->cpu, 0x080000C4, 0x0007000D, &cycles);
    CHECK(cycles == 0 && cupid_timing_global_time(&gba->timing) - start == 8);
    CHECK(observed.data == 13 && observed.direction == 15);
    CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C6, NULL) == 7);
    destroy(gba);

    gba = machine(false);
    observed = (struct Observation){.gba = gba, .input = 1};
    cupid_cartridge_set_input(gba, input_pins, &observed.input);
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    event = (struct CupidTimingEvent){.context = &observed, .callback = change_input, .priority = 0x60};
    cupid_timing_schedule(&gba->timing, &event, 6);
    gba->cpu.memory.timed = true;
    cycles = 0;
    const uint32_t value = cupid_memory_load32(&gba->cpu, 0x080000C4, &cycles);
    CHECK(value == 1 && cycles == 1);
    CHECK(observed.input == 9 && cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) == 9);
    destroy(gba);
}

static void console_reset_preserves_clock_and_resets_only_the_port(void) {
    struct CupidGBA* gba = machine(true);
    const struct CupidRTCDateTime date = {26, 10, 13, 5, 12, 34, 56};
    CHECK(cupid_rtc_set_datetime(&gba->cartridge.rtc, &date));
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 7, NULL);
    cupid_timing_tick(&gba->timing, (int32_t)CUPID_RTC_CLOCK_HZ / 2);
    cupid_gba_reset(gba);
    CHECK(!gba->cartridge.readable && gba->cartridge.direction == 0 && gba->cartridge.output == 0);
    CHECK(gba->cartridge.rtc.control == 0x40 && gba->cartridge.rtc.date.second == 56);
    CHECK(gba->cartridge.rtc.fraction == CUPID_RTC_CLOCK_HZ / 2);
    cupid_timing_tick(&gba->timing, (int32_t)CUPID_RTC_CLOCK_HZ / 2);
    cupid_rtc_sync(&gba->cartridge.rtc, cupid_timing_global_time(&gba->timing));
    CHECK(gba->cartridge.rtc.date.second == 57);
    destroy(gba);
}

static void dma_word_ports_sample_each_strobe_before_completion_irq(void) {
    struct CupidGBA* gba = machine(false);
    struct Observation observed = {.gba = gba, .input = 1};
    cupid_cartridge_set_input(gba, input_pins, &observed.input);
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 4, NULL);
    struct CupidTimingEvent event = {.context = &observed, .callback = change_input, .priority = 0x60};
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    cupid_timing_schedule(&gba->timing, &event, 9);
    cupid_dma_write_sad(gba, 3, 0x080000C4);
    cupid_dma_write_dad(gba, 3, GBA_BASE_IWRAM);
    cupid_dma_write_cnt_lo(gba, 3, 1);
    cupid_dma_write_cnt(gba, 3, 0xC400);
    cupid_timing_tick(&gba->timing, 11);
    CHECK(observed.input == 9 && gba->memory.dma[3].latch == 0x00040001);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM, NULL) == 0);
    CHECK(!(gba->irqf & IRQ_DMA3));
    cupid_timing_tick(&gba->timing, 1);
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM, NULL) == 0x00040001);
    CHECK(!(gba->irqf & IRQ_DMA3) && (gba->irq.pendingIF & IRQ_DMA3));
    CHECK(gba->memory.dma[3].busyUntil == start + 12);
    cupid_timing_tick(&gba->timing, 1);
    CHECK((gba->irqf & IRQ_DMA3) && !gba->memory.dmaRunning && !gba->cpuBlocked);
    destroy(gba);

    gba = machine(true);
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 15, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 1, NULL);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM, 0x0007000D, NULL);
    observed = (struct Observation){.gba = gba};
    event = (struct CupidTimingEvent){.context = &observed, .callback = observe_write, .priority = 0x60};
    cupid_timing_schedule(&gba->timing, &event, 10);
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM);
    cupid_dma_write_dad(gba, 3, 0x080000C4);
    cupid_dma_write_cnt_lo(gba, 3, 1);
    cupid_dma_write_cnt(gba, 3, 0xC400);
    cupid_timing_tick(&gba->timing, 11);
    CHECK(observed.data == 13 && observed.direction == 15);
    CHECK(gba->cartridge.direction == 15 && !(gba->irqf & IRQ_DMA3));
    cupid_timing_tick(&gba->timing, 1);
    CHECK(gba->cartridge.direction == 7 && !(gba->irqf & IRQ_DMA3));
    CHECK(gba->irq.pendingIF & IRQ_DMA3);
    cupid_timing_tick(&gba->timing, 1);
    CHECK((gba->irqf & IRQ_DMA3) && !gba->memory.dmaRunning && !gba->cpuBlocked);
    destroy(gba);
}

struct Driver { struct CupidGBA* gba; bool thumb; };

static void dma_gpio_uses_the_cartridge_burst_counter(void) {
    for (unsigned word = 0; word < 2; word++) {
        for (unsigned hits = 0; hits < 2; hits++) {
            struct CupidGBA* gba = machine(true);
            cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
            cupid_memory_store16(&gba->cpu, 0x080000C6, 15, NULL);
            cupid_memory_store16(&gba->cpu, 0x080000C4, 9, NULL);
            const uint32_t source = hits ? 0x080000C4 - (word ? 4 : 2) : 0x080000D0;
            const uint32_t value = word ? 0x0007000D : 13;
            memcpy(gba->memory.rom + (source & 0xFFFF), &value, word ? 4 : 2);
            cupid_dma_write_sad(gba, 3, source);
            cupid_dma_write_dad(gba, 3, hits ? 0x08000100 : 0x080000C4);
            cupid_dma_write_cnt_lo(gba, 3, 1);
            cupid_dma_write_cnt(gba, 3, (uint16_t)(0x8000 | (word ? 0x400 : 0)));
            cupid_timing_tick(&gba->timing, 18);
            CHECK(!gba->memory.dmaRunning);
            CHECK(gba->cartridge.output == (hits ? 13 : 9));
            CHECK(gba->cartridge.direction == (hits && word ? 7 : 15));
            destroy(gba);
        }
    }
}

static uint16_t instruction_access(struct Driver* driver, uint32_t address, uint16_t value, bool load) {
    struct CupidGBA* gba = driver->gba;
    gba->cpu.memory.timed = true;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    gba->cpu.cpsr.t = driver->thumb;
    _CupidSetMode(&gba->cpu, driver->thumb ? MODE_THUMB : MODE_ARM);
    if (driver->thumb) {
        const uint16_t code[] = {load ? 0x8801 : 0x8001, 0x46C0, 0x46C0}; /* LDRH/STRH r1,[r0] */
        memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        CupidThumbWritePC(&gba->cpu);
    } else {
        const uint32_t code[] = {load ? 0xE1D010B0 : 0xE1C010B0, 0xE1A00000, 0xE1A00000};
        memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        CupidARMWritePC(&gba->cpu);
    }
    gba->cpu.gprs[0] = (int32_t)address;
    gba->cpu.gprs[1] = value;
    cupid_gba_step(gba);
    cupid_timing_tick(&gba->timing, 128); /* More than five microseconds per wire phase. */
    return (uint16_t)gba->cpu.gprs[1];
}

static void port_write(struct Driver* driver, unsigned offset, uint16_t value) {
    instruction_access(driver, 0x080000C4 + offset, value, false);
}

static void bus_select(struct Driver* driver) {
    port_write(driver, 2, 7);
    port_write(driver, 0, 1);
    port_write(driver, 0, 5);
}

static void bus_send(struct Driver* driver, uint8_t value) {
    for (unsigned bit = 0; bit < 8; bit++) {
        uint16_t pins = (uint16_t)(4 | ((value >> bit) & 1) << 1);
        port_write(driver, 0, pins);
        port_write(driver, 0, pins | 1);
    }
}

static uint8_t bus_receive(struct Driver* driver) {
    uint8_t value = 0;
    for (unsigned bit = 0; bit < 8; bit++) {
        port_write(driver, 0, 4);
        port_write(driver, 0, 5);
        if (instruction_access(driver, 0x080000C4, 0, true) & 2) value |= (uint8_t)(1u << bit);
    }
    return value;
}

static void rtc_driver_runs_through_arm_and_thumb_instructions(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(true);
        struct Driver driver = {.gba = gba, .thumb = thumb != 0};
        port_write(&driver, 4, 1);
        bus_select(&driver); bus_send(&driver, 0x46); bus_send(&driver, 0x40);
        const uint8_t date[] = {0x26, 0x10, 0x13, 5, 0x14, 0x34, 0x56};
        bus_select(&driver); bus_send(&driver, 0x26);
        for (unsigned i = 0; i < sizeof(date); i++) bus_send(&driver, date[i]);
        bus_select(&driver); bus_send(&driver, 0xC6); port_write(&driver, 2, 5);
        CHECK(bus_receive(&driver) == 0x40);
        bus_select(&driver); bus_send(&driver, 0xA6); port_write(&driver, 2, 5);
        for (unsigned i = 0; i < sizeof(date); i++) CHECK(bus_receive(&driver) == (i == 4 ? 0x94 : date[i]));
        bus_select(&driver); bus_send(&driver, 0x46); bus_send(&driver, 0);
        const uint8_t time[] = {0x81, 0x02, 0x03};
        bus_select(&driver); bus_send(&driver, 0x66);
        for (unsigned i = 0; i < sizeof(time); i++) bus_send(&driver, time[i]);
        bus_select(&driver); bus_send(&driver, 0xE6); port_write(&driver, 2, 5);
        for (unsigned i = 0; i < sizeof(time); i++) CHECK(bus_receive(&driver) == time[i]);
        CHECK(gba->cartridge.rtc.date.day == 13 && gba->cartridge.rtc.date.weekday == 5);
        CHECK(gba->cartridge.rtc.date.hour == 13);
        bus_select(&driver); bus_send(&driver, 0x36);
        CHECK(gba->irqf & IRQ_GAMEPAK);
        instruction_access(&driver, GBA_BASE_IO + REG_IF, IRQ_GAMEPAK, false);
        for (unsigned i = 0; i < 8; i++) { port_write(&driver, 0, 4); port_write(&driver, 0, 5); }
        CHECK(!(gba->irqf & IRQ_GAMEPAK));
        bus_select(&driver); bus_send(&driver, 0xB6);
        CHECK(gba->irqf & IRQ_GAMEPAK);
        bus_select(&driver); bus_send(&driver, 0x06);
        CHECK(gba->cartridge.rtc.control == 0 && gba->cartridge.rtc.date.day == 1);
        CHECK(gba->cartridge.rtc.date.weekday == 0 && gba->cartridge.rtc.date.hour == 0);
        destroy(gba);
    }
}

static void marker_bounds_and_physical_rom_windows(void) {
    struct CupidGBA* gba = machine(false);
    const uint8_t marker[] = "SIIRTC_V001";
    cupid_cartridge_attach(gba, marker, sizeof(marker) - 2);
    CHECK(!gba->cartridge.rtcPresent);
    cupid_cartridge_attach(gba, marker, sizeof(marker) - 1);
    CHECK(gba->cartridge.rtcPresent);
    const uint8_t invalid[] = "SIIRTC_V00x";
    cupid_cartridge_attach(gba, invalid, sizeof(invalid));
    CHECK(!gba->cartridge.rtcPresent);
    uint16_t ordinary[6];
    for (unsigned region = 8; region <= 13; region++)
        ordinary[region - 8] = (uint16_t)cupid_memory_loadU16(&gba->cpu, ((uint32_t)region << 24) + 0xC4, NULL);
    cupid_cartridge_enable_rtc(gba, true);
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 15, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C4, 9, NULL);
    for (unsigned region = 8; region <= 13; region++) {
        uint32_t base = (uint32_t)region << 24;
        CHECK(cupid_cartridge_gpio_address(gba, base + 0xC4) == !(region & 1));
        CHECK(cupid_memory_loadU16(&gba->cpu, base + 0xC4, NULL) == (region & 1 ? ordinary[region - 8] : 9));
        CHECK(!cupid_cartridge_gpio_address(gba, base + 0xC2));
        CHECK(!cupid_cartridge_gpio_address(gba, base + 0xCA));
    }
    CHECK(!cupid_cartridge_gpio_address(gba, 0x020000C4));
    CHECK(!cupid_cartridge_gpio_address(gba, 0x0E0000C4));
    cupid_memory_store8(&gba->cpu, 0x080000C6, 0, NULL);
    cupid_memory_store8(&gba->cpu, 0x080000C7, 0, NULL);
    CHECK(gba->cartridge.direction == 15);
    destroy(gba);
}

static void uncached_arm_gpio_fetch_samples_the_low_halfword_first(void) {
    for (unsigned bank = 0; bank < 3; bank++) {
        for (unsigned nonseq = 0; nonseq < 4; nonseq++) {
            for (unsigned sequential = 0; sequential < 2; sequential++) {
                for (unsigned prefetch = 0; prefetch < 2; prefetch++) {
                    struct CupidGBA* gba = machine(false);
                    struct Observation observed = {.gba = gba, .input = 1};
                    cupid_cartridge_set_input(gba, input_pins, &observed.input);
                    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
                    const unsigned shift = bank * 3 + 2;
                    const uint16_t waitcnt = (uint16_t)(nonseq << shift | sequential << (shift + 2) | prefetch << 14);
                    cupid_memory_update_waitstates(gba, waitcnt);
                    const unsigned first = (unsigned)GBA_ROM_WAITSTATES[nonseq] + 1;
                    const unsigned second = (unsigned)GBA_ROM_WAITSTATES_SEQ[bank * 2 + sequential] + 1;
                    struct CupidTimingEvent event = {.context = &observed, .callback = change_input, .priority = 0x60};
                    cupid_timing_schedule(&gba->timing, &event, (int32_t)first + 1);
                    gba->cpu.cpsr.t = 0;
                    _CupidSetMode(&gba->cpu, MODE_ARM);
                    gba->cpu.memory.timed = true;
                    gba->cpu.gprs[ARM_PC] = (int32_t)(0x080000C4 + bank * 0x02000000);
                    CupidARMWritePC(&gba->cpu);
                    CHECK(observed.input == 9);
                    CHECK(gba->cpu.prefetch[0] == 1);
                    CHECK(gba->cpu.prefetch[1] == 0xA55A0001);
                    CHECK(cupid_timing_global_time(&gba->timing) == first + second * 3);
                    destroy(gba);
                }
            }
        }
    }
}

static struct CupidGBA* prefetch_machine(unsigned width, unsigned bank, unsigned sequential,
                                         unsigned distance, struct Observation* observed) {
    struct CupidGBA* gba = machine(false);
    *observed = (struct Observation){.gba = gba, .input = 1};
    cupid_cartridge_set_input(gba, input_pins, &observed->input);
    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
    cupid_memory_store16(&gba->cpu, 0x080000C6, 4, NULL);
    cupid_memory_update_waitstates(gba, (uint16_t)(0x4000 | sequential << (bank * 3 + 4)));
    gba->cpu.cpsr.t = width == 2;
    _CupidSetMode(&gba->cpu, width == 2 ? MODE_THUMB : MODE_ARM);
    const uint32_t preceding = 0x080000C4 + bank * 0x02000000 - width * (distance + 1);
    gba->cpu.gprs[ARM_PC] = (int32_t)preceding;
    gba->cpu.memory.setActiveRegion(&gba->cpu, preceding);
    gba->cpu.memory.timed = true;
    cupid_bus_fetch(&gba->cpu, preceding, width, false);
    (void)CupidReadOpcode(&gba->cpu, preceding, width);
    cupid_bus_finish(&gba->cpu);
    return gba;
}

static void cached_gpio_preserves_each_strobe_and_consumes_the_sample(void) {
    for (unsigned width = 2; width <= 4; width += 2) {
        for (unsigned bank = 0; bank < 3; bank++) {
            for (unsigned sequential = 0; sequential < 2; sequential++) {
                const unsigned capacity = 16 / width;
                for (unsigned distance = 0; distance < capacity; distance++) {
                    struct Observation observed;
                    struct CupidGBA* gba = prefetch_machine(width, bank, sequential, distance, &observed);
                    const unsigned half = (unsigned)GBA_ROM_WAITSTATES_SEQ[bank * 2 + sequential] + 1;
                    const unsigned duty = gba->memory.prefetchUnit.duty;
                    struct CupidTimingEvent event = {.context = &observed, .callback = change_input, .priority = 0x60};
                    cupid_timing_schedule(&gba->timing, &event, (int32_t)(distance * duty + half + 1));
                    cupid_timing_tick(&gba->timing, (int32_t)(duty * capacity));
                    for (unsigned i = 0; i <= distance; i++) {
                        const uint32_t address = gba->memory.prefetchUnit.head;
                        cupid_bus_fetch(&gba->cpu, address, width, true);
                        const uint32_t opcode = CupidReadOpcode(&gba->cpu, address, width);
                        cupid_bus_finish(&gba->cpu);
                        if (i == distance) CHECK(opcode == (width == 4 ? 0x00040001u : 1u));
                    }
                    CHECK(observed.input == 9);
                    CHECK(!(gba->cartridge.prefetchMask & 1));
                    CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) == 9);
                    destroy(gba);
                }
            }
        }
    }
}

static void pending_gpio_sampling_keeps_the_original_strobe_deadlines(void) {
    for (unsigned width = 2; width <= 4; width += 2) {
        for (unsigned before = 0; before < 3; before++) {
            struct Observation observed;
            struct CupidGBA* gba = prefetch_machine(width, 0, 0, 0, &observed);
            struct CupidTimingEvent event = {.context = &observed, .callback = change_input, .priority = 0x60};
            const uint64_t start = cupid_timing_global_time(&gba->timing);
            cupid_timing_schedule(&gba->timing, &event, 4);
            cupid_timing_tick(&gba->timing, (int32_t)before);
            cupid_bus_fetch(&gba->cpu, 0x080000C4, width, true);
            CHECK((gba->cartridge.prefetchMask & (width == 4 ? 3 : 1)) == (width == 4 ? 3 : 1));
            const uint32_t opcode = CupidReadOpcode(&gba->cpu, 0x080000C4, width);
            cupid_bus_finish(&gba->cpu);
            CHECK(opcode == (width == 4 ? 0x00040001u : 1u));
            CHECK(cupid_timing_global_time(&gba->timing) == start + (width == 4 ? 6 : 3));
            if (width == 4) CHECK(!(gba->cartridge.prefetchMask & 3));
            CHECK(gba->cartridge.prefetchScheduled);
            CHECK(gba->cartridge.prefetchEvent.when == (uint32_t)(start + (width == 4 ? 9 : 6)));
            destroy(gba);
        }
    }
}

static void prefetch_sampling_has_identical_bulk_and_single_clock_dispatch(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - 12u, UINT64_C(0x300000000)};
    for (unsigned i = 0; i < sizeof(clocks) / sizeof(*clocks); i++) {
        for (unsigned width = 2; width <= 4; width += 2) {
            struct Observation a, b;
            struct CupidGBA* bulk = prefetch_machine(width, 0, 0, 0, &a);
            struct CupidGBA* one = prefetch_machine(width, 0, 0, 0, &b);
            struct CupidGBA* machines[] = {bulk, one};
            for (unsigned m = 0; m < 2; m++) {
                struct CupidGBA* gba = machines[m];
                const uint64_t previous = cupid_timing_global_time(&gba->timing);
                cupid_timing_clear(&gba->timing);
                gba->timing.globalCycles = clocks[i];
                gba->timing.masterCycles = (uint32_t)clocks[i];
                gba->memory.prefetchUnit.epoch = clocks[i];
                gba->cartridge.prefetchScheduled = false;
                gba->cartridge.prefetchMask = 0;
                CHECK(previous > 0);
                cupid_cartridge_prefetch_schedule(gba);
            }
            struct CupidTimingEvent eventA = {.context = &a, .callback = change_input, .priority = 0x60};
            struct CupidTimingEvent eventB = {.context = &b, .callback = change_input, .priority = 0x60};
            cupid_timing_schedule(&bulk->timing, &eventA, 4);
            cupid_timing_schedule(&one->timing, &eventB, 4);
            cupid_timing_tick(&bulk->timing, 24);
            for (unsigned clock = 0; clock < 24; clock++) cupid_timing_tick(&one->timing, 1);
            CHECK(bulk->cartridge.prefetchMask == 7 && one->cartridge.prefetchMask == 7);
            CHECK(!memcmp(bulk->cartridge.prefetchPorts, one->cartridge.prefetchPorts,
                          sizeof(bulk->cartridge.prefetchPorts)));
            CHECK(bulk->cartridge.prefetchPorts[0] == 1 && bulk->cartridge.prefetchPorts[1] == 4);
            CHECK(bulk->cartridge.prefetchPorts[2] == 1);
            CHECK(!bulk->cartridge.prefetchScheduled && !one->cartridge.prefetchScheduled);
            destroy(bulk); destroy(one);
        }
    }
}

static void stopping_resetting_or_replacing_a_cartridge_cancels_prefetch_samples(void) {
    for (unsigned action = 0; action < 5; action++) {
        struct Observation observed;
        struct CupidGBA* gba = prefetch_machine(4, 0, 0, 0, &observed);
        CHECK(gba->cartridge.prefetchScheduled);
        if (action == 0) {
            cupid_prefetch_before_data(gba, 0x08000100);
            cupid_prefetch_after_data(gba);
        } else if (action == 1) cupid_gba_reset(gba);
        else if (action == 2) cupid_cartridge_enable_rtc(gba, false);
        else if (action == 3) {
            const uint8_t image[] = "SIIRTC_V001";
            cupid_cartridge_attach(gba, image, sizeof(image));
        } else {
            cupid_bus_fetch(&gba->cpu, 0x08000100, 2, false);
            (void)CupidReadOpcode(&gba->cpu, 0x08000100, 2);
            cupid_bus_finish(&gba->cpu);
        }
        CHECK(!gba->cartridge.prefetchScheduled && !gba->cartridge.prefetchMask);
        CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.prefetchEvent));
        cupid_timing_tick(&gba->timing, 24);
        CHECK(!gba->cartridge.prefetchMask);
        destroy(gba);
    }
}

static void ram_access_keeps_sampling_and_gamepak_dma_discards_the_fill(void) {
    struct Observation observed;
    struct CupidGBA* gba = prefetch_machine(4, 0, 0, 0, &observed);
    struct CupidTimingEvent event = {.context = &observed, .callback = change_input, .priority = 0x60};
    cupid_timing_schedule(&gba->timing, &event, 4);
    cupid_memory_store32(&gba->cpu, GBA_BASE_EWRAM, 0x12345678, NULL);
    int cycles = 0;
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_EWRAM, &cycles) == 0x12345678);
    CHECK(cycles == 1 && (gba->cartridge.prefetchMask & 3) == 3);
    cupid_bus_fetch(&gba->cpu, 0x080000C4, 4, true);
    CHECK(CupidReadOpcode(&gba->cpu, 0x080000C4, 4) == 0x00040001);
    cupid_bus_finish(&gba->cpu);
    destroy(gba);

    gba = prefetch_machine(4, 0, 0, 0, &observed);
    event = (struct CupidTimingEvent){.context = &observed, .callback = change_input, .priority = 0x60};
    cupid_timing_schedule(&gba->timing, &event, 4);
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM);
    cupid_dma_write_dad(gba, 3, 0x08000100);
    cupid_dma_write_cnt_lo(gba, 3, 1);
    cupid_dma_write_cnt(gba, 3, 0x8000);
    cupid_timing_tick(&gba->timing, 20);
    CHECK(!gba->memory.dmaRunning && !gba->memory.prefetchUnit.active);
    CHECK(!gba->cartridge.prefetchScheduled && !gba->cartridge.prefetchMask);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.prefetchEvent));
    cupid_bus_fetch(&gba->cpu, 0x080000C4, 4, false);
    CHECK(CupidReadOpcode(&gba->cpu, 0x080000C4, 4) == 0x00040009);
    cupid_bus_finish(&gba->cpu);
    destroy(gba);
}

int main(void) {
    RUN(directions_latches_input_and_pristine_cartridge);
    RUN(word_lanes_opcode_fetch_and_read_control);
    RUN(timed_word_ports_have_separate_bus_endpoints);
    RUN(console_reset_preserves_clock_and_resets_only_the_port);
    RUN(dma_word_ports_sample_each_strobe_before_completion_irq);
    RUN(dma_gpio_uses_the_cartridge_burst_counter);
    RUN(rtc_driver_runs_through_arm_and_thumb_instructions);
    RUN(marker_bounds_and_physical_rom_windows);
    RUN(uncached_arm_gpio_fetch_samples_the_low_halfword_first);
    RUN(cached_gpio_preserves_each_strobe_and_consumes_the_sample);
    RUN(pending_gpio_sampling_keeps_the_original_strobe_deadlines);
    RUN(prefetch_sampling_has_identical_bulk_and_single_clock_dispatch);
    RUN(stopping_resetting_or_replacing_a_cartridge_cancels_prefetch_samples);
    RUN(ram_access_keeps_sampling_and_gamepak_dma_discards_the_fill);
    return 0;
}
