/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"

static void pins(struct CupidRTC* rtc, uint8_t value, uint8_t direction, uint64_t* clock) {
    CHECK(!cupid_rtc_write_pins_direction(rtc, value, direction, ++*clock));
}

static void start(struct CupidRTC* rtc, bool read, uint64_t* clock) {
    pins(rtc, 1, 7, clock);
    pins(rtc, 5, 7, clock);
    const unsigned command = read ? 0x96 : 0x16;
    for (unsigned bit = 0; bit < 8; bit++) {
        const uint8_t value = (uint8_t)(4 | ((command >> bit) & 1) * 2);
        pins(rtc, value, 7, clock);
        pins(rtc, value | 1, 7, clock);
    }
}

static void partial_parameter_write_changes_the_live_shift_register(void) {
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, 0);
    cupid_rtc_write_interrupt_parameters(&rtc, 0xA5C3);
    uint64_t clock = 0;
    start(&rtc, false, &clock);
    pins(&rtc, 4, 7, &clock);
    pins(&rtc, 5, 7, &clock);
    CHECK(rtc.interruptParameters == 0x52E1);
    pins(&rtc, 1, 7, &clock);
    CHECK(rtc.interruptParameters == 0x52E1);
}

static void nominal_parameter_read_still_writes_released_input(void) {
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, 0);
    cupid_rtc_write_interrupt_parameters(&rtc, 0x3492);
    uint64_t clock = 0;
    start(&rtc, true, &clock);
    for (unsigned bit = 0; bit < 16; bit++) {
        pins(&rtc, 6, 5, &clock);
        CHECK(cupid_rtc_output_pins(&rtc) == 2);
        pins(&rtc, 7, 5, &clock);
        CHECK(cupid_rtc_output_pins(&rtc) == 2);
    }
    CHECK(rtc.interruptParameters == 0xFFFF);
}

static uint16_t shifted(unsigned seed, unsigned payload, unsigned count) {
    return count ? (uint16_t)((seed >> count) | (payload << (16 - count))) : (uint16_t)seed;
}

static void every_parameter_word_has_complete_partial_observations(void) {
    for (unsigned read = 0; read < 2; read++) for (unsigned value = 0; value < 65536; value++) {
        struct CupidRTC rtc;
        cupid_rtc_init(&rtc, 0);
        cupid_rtc_write_interrupt_parameters(&rtc, 0xA5C3);
        uint64_t clock = 0;
        start(&rtc, read != 0, &clock);
        for (unsigned bit = 0; bit < 16; bit++) {
            const uint8_t data = (uint8_t)(4 | ((value >> bit) & 1) * 2);
            pins(&rtc, data, 7, &clock);
            CHECK(rtc.interruptParameters == shifted(0xA5C3, value, bit));
            pins(&rtc, data, 7, &clock);
            CHECK(rtc.interruptParameters == shifted(0xA5C3, value, bit));
            pins(&rtc, data | 1, 7, &clock);
            CHECK(rtc.interruptParameters == shifted(0xA5C3, value, bit + 1));
            pins(&rtc, data | 1, 7, &clock);
            CHECK(rtc.interruptParameters == shifted(0xA5C3, value, bit + 1));
        }
        CHECK(rtc.interruptParameters == value && rtc.control == 0x40);
        CHECK(!rtc.invalidSecond && !rtc.interruptLow && !rtc.minuteCarryActive);
        CHECK(rtc.date.year == 0 && rtc.date.month == 1 && rtc.date.day == 1 && !rtc.date.second);
    }
}

static void chip_select_abort_keeps_every_already_shifted_bit(void) {
    for (unsigned read = 0; read < 2; read++) for (unsigned count = 0; count <= 16; count++) {
        struct CupidRTC rtc;
        cupid_rtc_init(&rtc, 0);
        cupid_rtc_write_interrupt_parameters(&rtc, 0x1357);
        uint64_t clock = 0;
        start(&rtc, read != 0, &clock);
        for (unsigned bit = 0; bit < count; bit++) {
            const uint8_t data = (uint8_t)(4 | ((0xCAFE >> bit) & 1) * 2);
            pins(&rtc, data, 7, &clock);
            pins(&rtc, data | 1, 7, &clock);
        }
        const uint16_t expected = shifted(0x1357, 0xCAFE, count);
        pins(&rtc, 1, 7, &clock);
        CHECK(rtc.interruptParameters == expected && cupid_rtc_output_pins(&rtc) == 2);
        start(&rtc, false, &clock);
        CHECK(rtc.interruptParameters == expected);
        cupid_rtc_reset_bus(&rtc);
        CHECK(rtc.interruptParameters == expected);
    }
}

static struct CupidGBA* machine(uint64_t epoch) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 4096);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 4096;
    gba->memory.romMask = 4095;
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    gba->cpu.cycles = 0;
    cupid_irq_init(gba);
    cupid_cartridge_enable_rtc(gba, true);
    cupid_rtc_init(&gba->cartridge.rtc, epoch);
    CHECK(cupid_cartridge_gpio_write(gba, 0x080000C6, 7));
    CHECK(cupid_cartridge_gpio_write(gba, 0x080000C8, 1));
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void store(struct CupidGBA* gba, bool thumb, unsigned width, unsigned bank,
                  unsigned offset, uint16_t value, uint16_t direction) {
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    if (thumb) {
        const uint16_t code[] = {width == 4 ? 0x6001 : 0x8001, 0x46C0, 0x46C0};
        memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        CupidThumbWritePC(&gba->cpu);
    } else {
        const uint32_t code[] = {width == 4 ? 0xE5801000 : 0xE1C010B0, 0xE1A00000, 0xE1A00000};
        memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        CupidARMWritePC(&gba->cpu);
    }
    gba->cpu.gprs[0] = (int32_t)(0x080000C4 + bank * 0x02000000 + offset);
    gba->cpu.gprs[1] = (int32_t)(value | (uint32_t)direction << 16);
    cupid_gba_step(gba);
    const int32_t cycles = gba->cpu.cycles;
    gba->cpu.cycles = 0;
    if (cycles) cupid_timing_tick(&gba->timing, cycles);
}

static void cpu_frames_retain_each_partial_word_and_released_read_data(void) {
    const unsigned values[] = {0, 0xFFFF, 0xA55A, 0x5AA5};
    for (unsigned thumb = 0; thumb < 2; thumb++) for (unsigned bank = 0; bank < 3; bank++)
        for (unsigned width = 2; width <= 4; width *= 2) for (unsigned read = 0; read < 2; read++)
            for (unsigned sample = 0; sample < 4; sample++) {
                struct CupidGBA* gba = machine(0);
                cupid_rtc_write_interrupt_parameters(&gba->cartridge.rtc, 0x1357);
                store(gba, thumb != 0, width, bank, 0, 1, 7);
                store(gba, thumb != 0, width, bank, 0, 5, 7);
                const unsigned command = read ? 0x96 : 0x16;
                for (unsigned bit = 0; bit < 8; bit++) {
                    const uint16_t data = (uint16_t)(4 | ((command >> bit) & 1) * 2);
                    store(gba, thumb != 0, width, bank, 0, data, 7);
                    store(gba, thumb != 0, width, bank, 0, data | 1, 7);
                }
                const uint16_t direction = read ? 5 : 7;
                store(gba, thumb != 0, 2, bank, 2, direction, direction);
                const unsigned payload = read ? 0xFFFF : values[sample];
                for (unsigned bit = 0; bit < 16; bit++) {
                    const uint16_t data = (uint16_t)(4 | ((payload >> bit) & 1) * 2);
                    store(gba, thumb != 0, width, bank, 0, data, direction);
                    CHECK(gba->cartridge.rtc.interruptParameters == shifted(0x1357, payload, bit));
                    store(gba, thumb != 0, width, bank, 0, data | 1, direction);
                    CHECK(gba->cartridge.rtc.interruptParameters == shifted(0x1357, payload, bit + 1));
                    if (read) CHECK(cupid_memory_loadU16(&gba->cpu, 0x080000C4, NULL) & 2);
                }
                CHECK(gba->memory.savedata.type == SAVE_AUTODETECT && !gba->memory.savedata.dirty);
                CHECK(gba->cartridge.rtc.control == 0x40 && !gba->cartridge.rtc.interruptLow);
                destroy(gba);
            }
}

struct Completion { struct CupidGBA* gba; uint64_t clock; };
static void finish_bit(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct Completion* completion = context;
    completion->clock = cupid_timing_global_time(timing);
    CHECK(cupid_cartridge_gpio_write(completion->gba, 0x080000C4, 5));
}

static void partial_frequency_writes_keep_the_irq_deadline_through_late_dispatch_and_wrap(void) {
    const uint64_t epochs[] = {0, INT32_MAX - 5u, UINT32_MAX - 5u, UINT64_C(0x200000100)};
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned read = 0; read < 2; read++)
        for (unsigned late = 0; late <= 3; late++) {
            struct CupidGBA* gba = machine(epochs[epoch]);
            cupid_irq_write(gba, REG_IE, IRQ_GAMEPAK);
            cupid_irq_write(gba, REG_IME, 1);
            cupid_timing_tick(&gba->timing, 3);
            CHECK(cupid_cartridge_gpio_write(gba, 0x080000C4, 1));
            CHECK(cupid_cartridge_gpio_write(gba, 0x080000C4, 5));
            const unsigned command = read ? 0x96 : 0x16;
            for (unsigned bit = 0; bit < 8; bit++) {
                const uint16_t data = (uint16_t)(4 | ((command >> bit) & 1) * 2);
                CHECK(cupid_cartridge_gpio_write(gba, 0x080000C4, data));
                CHECK(cupid_cartridge_gpio_write(gba, 0x080000C4, data | 1));
            }
            CHECK(cupid_cartridge_gpio_write(gba, 0x080000C6, read ? 5 : 7));
            CHECK(cupid_cartridge_gpio_write(gba, 0x080000C4, 4));
            cupid_rtc_write_interrupt_parameters(&gba->cartridge.rtc, 1);
            gba->cartridge.rtc.fraction = read ? 1u << 23 : 0;
            cupid_rtc_write_control(&gba->cartridge.rtc, 0x42);
            cupid_cartridge_rtc_reschedule(gba);
            CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
            const uint64_t expected = cupid_timing_global_time(&gba->timing) + 17;
            struct Completion completion = {.gba = gba};
            struct CupidTimingEvent event = {.context = &completion, .callback = finish_bit, .priority = 0x30};
            cupid_timing_schedule(&gba->timing, &event, 17);
            cupid_timing_tick(&gba->timing, 17 + late);
            CHECK(completion.clock == expected);
            CHECK(gba->cartridge.rtc.interruptParameters == (read ? 0x8000 : 0));
            CHECK(!gba->cartridge.rtc.interruptLow && gba->irq.pendingIF == IRQ_GAMEPAK);
            CHECK(gba->irqf == (late >= 1 ? IRQ_GAMEPAK : 0));
            CHECK(gba->irq.available == (late >= 2) && gba->irq.line == (late >= 3));
            destroy(gba);
        }
}

static void dma_parameter_strobes_and_timer_irqs_match_bulk_and_single_clock_dispatch(void) {
    const uint64_t epochs[] = {0, UINT32_MAX - 200u};
    for (unsigned epoch = 0; epoch < 2; epoch++) {
        uint16_t timerCounter = 0;
        for (unsigned single = 0; single < 2; single++) {
            struct CupidGBA* gba = machine(epochs[epoch]);
            cupid_rtc_write_interrupt_parameters(&gba->cartridge.rtc, 0x8000);
            cupid_rtc_write_control(&gba->cartridge.rtc, 0x42);
            cupid_cartridge_rtc_reschedule(gba);
            uint16_t stream[49] = {1, 5};
            unsigned count = 2;
            for (unsigned bit = 0; bit < 8; bit++) {
                const uint16_t data = (uint16_t)(4 | ((0x16 >> bit) & 1) * 2);
                stream[count++] = data;
                stream[count++] = data | 1;
            }
            for (unsigned bit = 0; bit < 15; bit++) {
                stream[count++] = 4;
                stream[count++] = 5;
            }
            stream[count++] = 1;
            CHECK(count == 49);
            memcpy(gba->memory.iwram + 0x2000, stream, sizeof(stream));
            cupid_timer_write_reload(gba, 0, 0xFFF0);
            cupid_timer_write_control(gba, 0, 0xC0);
            /* Each pin update starts a new Game Pak burst at the data port. */
            for (unsigned sample = 0; sample < count; sample++) {
                cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x2000 + sample * 2);
                cupid_dma_write_dad(gba, 3, 0x080000C4);
                cupid_dma_write_cnt_lo(gba, 3, 1);
                cupid_dma_write_cnt(gba, 3, 0xC040);
                if (single) for (unsigned clock = 0; clock < 16; clock++) cupid_timing_tick(&gba->timing, 1);
                else cupid_timing_tick(&gba->timing, 16);
                CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable);
            }
            const unsigned remaining = 8192 - count * 16;
            if (single) for (unsigned clock = 0; clock < remaining; clock++) cupid_timing_tick(&gba->timing, 1);
            else cupid_timing_tick(&gba->timing, (int32_t)remaining);
            CHECK(gba->cartridge.rtc.interruptParameters == 1);
            CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable && !gba->cpuBlocked);
            CHECK(gba->irqf == (IRQ_GAMEPAK | IRQ_DMA3 | IRQ_TIMER0));
            CHECK(cupid_timing_global_time(&gba->timing) == epochs[epoch] + 8192);
            const uint16_t counter = cupid_timer_read_counter(gba, 0);
            if (!single) timerCounter = counter;
            else CHECK(counter == timerCounter);
            CHECK(!gba->memory.savedata.dirty && gba->cartridge.direction == 7);
            destroy(gba);
        }
    }
}

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "partial")) {
        RUN(partial_parameter_write_changes_the_live_shift_register);
    } else if (argc == 2 && !strcmp(argv[1], "read")) {
        RUN(nominal_parameter_read_still_writes_released_input);
    } else if (argc == 2 && !strcmp(argv[1], "component")) {
        RUN(partial_parameter_write_changes_the_live_shift_register);
        RUN(nominal_parameter_read_still_writes_released_input);
        RUN(every_parameter_word_has_complete_partial_observations);
        RUN(chip_select_abort_keeps_every_already_shifted_bit);
    } else if (argc == 2 && !strcmp(argv[1], "bus")) {
        RUN(cpu_frames_retain_each_partial_word_and_released_read_data);
        RUN(partial_frequency_writes_keep_the_irq_deadline_through_late_dispatch_and_wrap);
        RUN(dma_parameter_strobes_and_timer_irqs_match_bulk_and_single_clock_dispatch);
    } else {
        return 2;
    }
    return 0;
}
