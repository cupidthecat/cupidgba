/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 4096);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 4096;
    gba->memory.romMask = 4095;
    cupid_gba_reset(gba);
    cupid_cartridge_enable_rtc(gba, true);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void flush_cpu(struct CupidGBA* gba) {
    const int32_t cycles = gba->cpu.cycles;
    gba->cpu.cycles = 0;
    if (cycles) cupid_timing_tick(&gba->timing, cycles);
}

static void port_write(struct CupidGBA* gba, bool thumb, unsigned offset, uint16_t value) {
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    if (thumb) {
        const uint16_t code[] = {0x8001, 0x46C0, 0x46C0}; /* STRH r1,[r0] */
        memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        CupidThumbWritePC(&gba->cpu);
    } else {
        const uint32_t code[] = {0xE1C010B0, 0xE1A00000, 0xE1A00000};
        memcpy(gba->memory.iwram + 0x1000, code, sizeof(code));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        CupidARMWritePC(&gba->cpu);
    }
    gba->cpu.gprs[0] = (int32_t)(0x080000C4 + offset);
    gba->cpu.gprs[1] = value;
    cupid_gba_step(gba);
    flush_cpu(gba);
    cupid_timing_tick(&gba->timing, 128);
}

static void send_byte(struct CupidGBA* gba, bool thumb, uint8_t value) {
    for (unsigned bit = 0; bit < 8; bit++) {
        const uint16_t pins = (uint16_t)(4 | ((value >> bit) & 1) << 1);
        port_write(gba, thumb, 0, pins);
        port_write(gba, thumb, 0, pins | 1);
    }
}

static void control_write(struct CupidGBA* gba, bool thumb, uint8_t value) {
    port_write(gba, thumb, 2, 7);
    port_write(gba, thumb, 0, 1);
    port_write(gba, thumb, 0, 5);
    send_byte(gba, thumb, 0x46);
    send_byte(gba, thumb, value);
    CHECK(gba->cartridge.rtc.control == value);
}

static void parameter_write(struct CupidGBA* gba, bool thumb, uint16_t value) {
    port_write(gba, thumb, 2, 7);
    port_write(gba, thumb, 0, 1);
    port_write(gba, thumb, 0, 5);
    send_byte(gba, thumb, 0x16);
    send_byte(gba, thumb, (uint8_t)value);
    send_byte(gba, thumb, (uint8_t)(value >> 8));
    CHECK(gba->cartridge.rtc.interruptParameters == value);
}

static void phase(struct CupidGBA* gba, unsigned second, bool low, unsigned until) {
    const struct CupidRTCDateTime date = {26, 10, 13, 2, 12, 34, (uint8_t)second};
    CHECK(cupid_rtc_set_datetime(&gba->cartridge.rtc, &date));
    gba->cartridge.rtc.interruptLow = low;
    gba->cartridge.rtc.fraction = (uint32_t)(CUPID_RTC_CLOCK_HZ - until);
    cupid_rtc_rebase(&gba->cartridge.rtc, cupid_timing_global_time(&gba->timing));
    cupid_cartridge_rtc_reschedule(gba);
    CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) == (int32_t)until);
}

static void steady_edges_enter_the_irq_pipeline_through_the_board_inverter(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine();
        control_write(gba, thumb != 0, 0x4A);
        cupid_irq_write(gba, REG_IE, IRQ_GAMEPAK);
        cupid_irq_write(gba, REG_IME, 1);
        cupid_timing_tick(&gba->timing, 2);
        phase(gba, 59, false, 12);
        cupid_timing_tick(&gba->timing, 11);
        CHECK(!gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) == 30 * CUPID_RTC_CLOCK_HZ);
        phase(gba, 29, true, 12);
        cupid_timing_tick(&gba->timing, 11);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(!gba->cartridge.rtc.interruptLow && gba->irq.pendingIF == IRQ_GAMEPAK);
        CHECK(!gba->irqf && !gba->irq.available && !gba->irq.line);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->irqf == IRQ_GAMEPAK && !gba->irq.available && !gba->irq.line);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->irq.available && !gba->irq.line);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->irq.line);
        cupid_irq_write(gba, REG_IF, IRQ_GAMEPAK);
        cupid_timing_tick(&gba->timing, 8);
        CHECK(!gba->irqf && !gba->irq.pendingIF && !gba->irq.line);
        destroy(gba);
    }
}

static void held_minute_output_releases_on_a_complete_control_write(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine();
        control_write(gba, thumb != 0, 0x48);
        phase(gba, 59, false, 1);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent));
        /* A control read or an aborted write must not acknowledge the latch. */
        CHECK(cupid_rtc_read_control(&gba->cartridge.rtc) == 0x48);
        port_write(gba, thumb != 0, 0, 1);
        port_write(gba, thumb != 0, 0, 5);
        send_byte(gba, thumb != 0, 0x46);
        port_write(gba, thumb != 0, 0, 1);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irqf);
        /* The retained carry reasserts output during the first 10 ms. This
         * existing release case acknowledges after that interval. */
        cupid_timing_tick(&gba->timing, CUPID_RTC_MINUTE_CARRY_CLOCKS);
        control_write(gba, thumb != 0, 0x48);
        CHECK(!gba->cartridge.rtc.interruptLow && gba->irqf == IRQ_GAMEPAK);
        CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent));
        cupid_irq_write(gba, REG_IF, IRQ_GAMEPAK);
        cupid_timing_tick(&gba->timing, 8);
        control_write(gba, thumb != 0, 0x40);
        CHECK(!gba->irqf && !cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent));
        destroy(gba);
    }
}

static void console_reset_disable_and_cartridge_replacement_preserve_ownership(void) {
    struct CupidGBA* gba = machine();
    control_write(gba, false, 0x4A);
    phase(gba, 29, true, 12);
    cupid_timing_tick(&gba->timing, 5);
    cupid_gba_reset(gba);
    CHECK(gba->cartridge.rtc.interruptLow && gba->cartridge.rtc.control == 0x4A);
    CHECK(!gba->cartridge.readable && !gba->cartridge.direction);
    CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) == 7);
    cupid_timing_tick(&gba->timing, 7);
    CHECK(!gba->cartridge.rtc.interruptLow && gba->irq.pendingIF == IRQ_GAMEPAK);
    cupid_timing_tick(&gba->timing, 1);
    CHECK(gba->irqf == IRQ_GAMEPAK);
    cupid_irq_write(gba, REG_IF, IRQ_GAMEPAK);
    cupid_timing_tick(&gba->timing, 2);
    cupid_cartridge_enable_rtc(gba, false);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent));
    cupid_cartridge_enable_rtc(gba, true);
    CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent));
    const uint8_t plain[] = "ordinary cartridge";
    cupid_cartridge_attach(gba, plain, sizeof(plain));
    CHECK(!gba->cartridge.rtcPresent && !gba->irq.pendingIF);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent));
    destroy(gba);
}

struct Trace {
    struct CupidGBA* gba;
    struct CupidTimingEvent event;
    uint16_t flags[48];
    uint8_t low[48];
    unsigned count;
    unsigned interval;
    bool acknowledge;
};

static void observe(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct Trace* trace = context;
    CHECK(trace->count < 48);
    trace->flags[trace->count] = trace->gba->irqf;
    trace->low[trace->count++] = trace->gba->cartridge.rtc.interruptLow;
    if (trace->acknowledge) cupid_irq_write(trace->gba, REG_IF, IRQ_GAMEPAK);
    if (trace->count < 48) cupid_timing_schedule(timing, &trace->event, trace->interval ? (int32_t)trace->interval : 1);
}

static void late_dispatch_clock_wrap_timer_and_dma_preserve_every_edge(void) {
    struct Trace results[2] = {0};
    for (unsigned bulk = 0; bulk < 2; bulk++) {
        struct CupidGBA* gba = machine();
        /* Isolate the event chronology from unrelated recurring render/audio work. */
        cupid_timing_clear(&gba->timing);
        const uint64_t start = UINT64_C(0xFFFFFFF8);
        gba->timing.globalCycles = start;
        gba->timing.masterCycles = (uint32_t)start;
        cupid_irq_init(gba);
        cupid_timer_init(gba);
        cupid_rtc_write_control(&gba->cartridge.rtc, 0x4A);
        phase(gba, 29, true, 8);
        cupid_timer_write_reload(gba, 0, 0xFFF8);
        cupid_timer_write_control(gba, 0, 0xC0);
        const uint32_t payload[] = {0x12345678, 0xA5C0FFEE, 0x10203040};
        memcpy(gba->memory.iwram + 0x100, payload, sizeof(payload));
        cupid_dma_write_sad(gba, 0, GBA_BASE_IWRAM + 0x100);
        cupid_dma_write_dad(gba, 0, GBA_BASE_IWRAM + 0x200);
        cupid_dma_write_cnt_lo(gba, 0, 3);
        cupid_dma_write_cnt(gba, 0, 0xC400);
        struct Trace* trace = &results[bulk];
        trace->gba = gba;
        trace->event = (struct CupidTimingEvent){.context = trace, .callback = observe, .priority = 0x70};
        cupid_timing_schedule(&gba->timing, &trace->event, 1);
        if (bulk) cupid_timing_tick(&gba->timing, 48);
        else for (unsigned i = 0; i < 48; i++) cupid_timing_tick(&gba->timing, 1);
        CHECK(trace->count == 48 && !trace->low[7] && trace->low[6]);
        CHECK(!(trace->flags[7] & IRQ_GAMEPAK) && (trace->flags[8] & IRQ_GAMEPAK));
        CHECK(gba->irqf == (IRQ_GAMEPAK | IRQ_TIMER0 | IRQ_DMA0));
        CHECK(!memcmp(gba->memory.iwram + 0x200, payload, sizeof(payload)));
        CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked);
        CHECK(cupid_timing_global_time(&gba->timing) == start + 48);
        destroy(gba);
        trace->gba = NULL;
    }
    CHECK(!memcmp(results[0].flags, results[1].flags, sizeof(results[0].flags)));
    CHECK(!memcmp(results[0].low, results[1].low, sizeof(results[0].low)));
}

static void cartridge_irq_wakes_halt_with_ime_clear(void) {
    struct CupidGBA* gba = machine();
    cupid_rtc_write_control(&gba->cartridge.rtc, 0x4A);
    cupid_irq_write(gba, REG_IE, IRQ_GAMEPAK);
    cupid_irq_write(gba, REG_IME, 0);
    cupid_timing_tick(&gba->timing, 2);
    phase(gba, 29, true, 12);
    cupid_gba_halt(gba);
    CHECK(gba->cpu.halted);
    for (unsigned i = 0; i < 12 && gba->cpu.halted; i++) cupid_gba_step(gba);
    CHECK(!gba->cpu.halted && gba->irq.available && !gba->irq.line && !gba->ime);
    CHECK(gba->irqf == IRQ_GAMEPAK);
    destroy(gba);
}

static void complete_force_command(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    CHECK(cupid_cartridge_gpio_write(context, 0x080000C4, 5));
}

static void forced_requests_retain_the_original_gpio_strobe_when_dispatched_late(void) {
    for (unsigned bulk = 0; bulk < 2; bulk++) {
        struct CupidGBA* gba = machine();
        cupid_timing_clear(&gba->timing);
        cupid_irq_init(gba);
        CHECK(cupid_cartridge_gpio_write(gba, 0x080000C6, 7));
        CHECK(cupid_cartridge_gpio_write(gba, 0x080000C4, 1));
        CHECK(cupid_cartridge_gpio_write(gba, 0x080000C4, 5));
        for (unsigned bit = 0; bit < 7; bit++) {
            const uint16_t pins = (uint16_t)(4 | ((0x36 >> bit) & 1) << 1);
            CHECK(cupid_cartridge_gpio_write(gba, 0x080000C4, pins));
            CHECK(cupid_cartridge_gpio_write(gba, 0x080000C4, pins | 1));
        }
        CHECK(cupid_cartridge_gpio_write(gba, 0x080000C4, 4));
        CHECK(!gba->irq.pendingIF);
        struct Trace trace = {.gba = gba};
        trace.event = (struct CupidTimingEvent){.context = &trace, .callback = observe, .priority = 0x70};
        struct CupidTimingEvent event = {.context = gba, .callback = complete_force_command, .priority = 0x40};
        cupid_timing_schedule(&gba->timing, &event, 8);
        cupid_timing_schedule(&gba->timing, &trace.event, 1);
        if (bulk) cupid_timing_tick(&gba->timing, 48);
        else for (unsigned clock = 0; clock < 48; clock++) cupid_timing_tick(&gba->timing, 1);
        CHECK(trace.count == 48 && !trace.flags[7] && trace.flags[8] == IRQ_GAMEPAK);
        CHECK(gba->irqf == IRQ_GAMEPAK);
        destroy(gba);
    }
}

static void every_frequency_releases_through_arm_thumb_gpio_and_the_irq_pipeline(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) for (unsigned bit = 0; bit < 16; bit++) {
        struct CupidGBA* gba = machine();
        cupid_timing_clear(&gba->timing);
        cupid_irq_init(gba);
        parameter_write(gba, thumb != 0, (uint16_t)(1u << bit));
        control_write(gba, thumb != 0, 0x42);
        /* Acknowledge free-running edges produced during serial setup. */
        cupid_irq_write(gba, REG_IF, IRQ_GAMEPAK);
        cupid_irq_write(gba, REG_IE, IRQ_GAMEPAK);
        cupid_irq_write(gba, REG_IME, 1);
        const uint32_t half = 256u << bit;
        gba->cartridge.rtc.fraction = half - 14;
        cupid_rtc_rebase(&gba->cartridge.rtc, cupid_timing_global_time(&gba->timing));
        cupid_cartridge_rtc_reschedule(gba);
        cupid_timing_tick(&gba->timing, 2);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF && !gba->irqf);
        CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) == 12);
        cupid_timing_tick(&gba->timing, 11);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(!gba->cartridge.rtc.interruptLow && gba->irq.pendingIF == IRQ_GAMEPAK);
        CHECK(!gba->irqf && !gba->irq.available && !gba->irq.line);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->irqf == IRQ_GAMEPAK && !gba->irq.available);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->irq.available && !gba->irq.line);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->irq.line);
        cupid_irq_write(gba, REG_IF, IRQ_GAMEPAK);
        cupid_timing_tick(&gba->timing, (int32_t)half - 3);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF && !gba->irqf);
        destroy(gba);
    }
}

static void alarm_gpio_acknowledgement_and_long_wait_checkpoints_keep_the_latch(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine();
        cupid_timing_clear(&gba->timing);
        cupid_irq_init(gba);
        parameter_write(gba, thumb != 0, 0x3492);
        control_write(gba, thumb != 0, 0x60);
        const struct CupidRTCDateTime date = {26, 10, 13, 2, 12, 33, 59};
        CHECK(cupid_rtc_set_datetime(&gba->cartridge.rtc, &date));
        gba->cartridge.rtc.fraction = CUPID_RTC_CLOCK_HZ - 12;
        cupid_rtc_rebase(&gba->cartridge.rtc, cupid_timing_global_time(&gba->timing));
        cupid_cartridge_rtc_reschedule(gba);
        cupid_timing_tick(&gba->timing, 11);
        CHECK(!gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent));
        control_write(gba, thumb != 0, 0x60);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irqf);
        control_write(gba, thumb != 0, 0x40);
        CHECK(!gba->cartridge.rtc.interruptLow && gba->irqf == IRQ_GAMEPAK);
        cupid_irq_write(gba, REG_IF, IRQ_GAMEPAK);
        cupid_timing_tick(&gba->timing, 8);
        control_write(gba, thumb != 0, 0x60);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF && !gba->irqf);
        control_write(gba, thumb != 0, 0x40);
        cupid_irq_write(gba, REG_IF, IRQ_GAMEPAK);
        cupid_timing_tick(&gba->timing, 8);
        parameter_write(gba, thumb != 0, 0x3792);
        control_write(gba, thumb != 0, 0x60);
        gba->cartridge.rtc.fraction = 0;
        cupid_rtc_rebase(&gba->cartridge.rtc, cupid_timing_global_time(&gba->timing));
        cupid_cartridge_rtc_reschedule(gba);
        CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) == INT32_MAX);
        cupid_timing_tick(&gba->timing, INT32_MAX);
        CHECK(!gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        const uint32_t remaining = cupid_rtc_next_interrupt(&gba->cartridge.rtc);
        CHECK(remaining > 0 && remaining < INT32_MAX);
        cupid_timing_tick(&gba->timing, (int32_t)remaining - 1);
        CHECK(!gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        destroy(gba);
    }
}

static void repeated_frequency_edges_keep_timer_dma_and_late_wrap_chronology(void) {
    struct Trace results[2] = {0};
    for (unsigned bulk = 0; bulk < 2; bulk++) {
        struct CupidGBA* gba = machine();
        cupid_timing_clear(&gba->timing);
        const uint64_t start = UINT64_C(0xFFFFFFF0);
        gba->timing.globalCycles = start;
        gba->timing.masterCycles = (uint32_t)start;
        cupid_irq_init(gba);
        cupid_timer_init(gba);
        cupid_rtc_write_interrupt_parameters(&gba->cartridge.rtc, 1);
        cupid_rtc_write_control(&gba->cartridge.rtc, 0x42);
        gba->cartridge.rtc.fraction = 248;
        cupid_rtc_rebase(&gba->cartridge.rtc, start);
        cupid_cartridge_rtc_reschedule(gba);
        cupid_timer_write_reload(gba, 0, 0xFFF8);
        cupid_timer_write_control(gba, 0, 0xC0);
        const uint32_t payload[] = {0xA5F01032, 0x18372645, 0xF0FFABC0};
        memcpy(gba->memory.iwram + 0x100, payload, sizeof(payload));
        cupid_dma_write_sad(gba, 0, GBA_BASE_IWRAM + 0x100);
        cupid_dma_write_dad(gba, 0, GBA_BASE_IWRAM + 0x200);
        cupid_dma_write_cnt_lo(gba, 0, 3);
        cupid_dma_write_cnt(gba, 0, 0xC400);
        struct Trace* trace = &results[bulk];
        trace->gba = gba; trace->interval = 16; trace->acknowledge = true;
        trace->event = (struct CupidTimingEvent){.context = trace, .callback = observe, .priority = 0x70};
        cupid_timing_schedule(&gba->timing, &trace->event, 16);
        if (bulk) cupid_timing_tick(&gba->timing, 768);
        else for (unsigned clock = 0; clock < 768; clock++) cupid_timing_tick(&gba->timing, 1);
        CHECK(trace->count == 48 && !trace->low[0] && !trace->low[15]);
        CHECK(trace->low[16] && trace->low[31] && !trace->low[32]);
        for (unsigned i = 0; i < 48; i++)
            CHECK(((trace->flags[i] & IRQ_GAMEPAK) != 0) == (i == 0 || i == 32));
        CHECK(!memcmp(gba->memory.iwram + 0x200, payload, sizeof(payload)));
        CHECK((gba->irqf & (IRQ_TIMER0 | IRQ_DMA0)) == (IRQ_TIMER0 | IRQ_DMA0));
        CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked);
        CHECK(cupid_timing_global_time(&gba->timing) == start + 768);
        destroy(gba);
        trace->gba = NULL;
    }
    CHECK(!memcmp(results[0].flags, results[1].flags, sizeof(results[0].flags)));
    CHECK(!memcmp(results[0].low, results[1].low, sizeof(results[0].low)));
}

static void frequency_and_alarm_outputs_survive_console_reset_and_cancel_on_replacement(void) {
    for (unsigned frequency = 0; frequency < 2; frequency++) {
        struct CupidGBA* gba = machine();
        cupid_rtc_write_interrupt_parameters(&gba->cartridge.rtc, frequency ? 0x8000 : 0);
        cupid_rtc_write_control(&gba->cartridge.rtc, frequency ? 0x42 : 0x60);
        if (frequency) gba->cartridge.rtc.fraction = CUPID_RTC_CLOCK_HZ / 2 - 12;
        cupid_cartridge_rtc_reschedule(gba);
        cupid_timing_tick(&gba->timing, 5);
        const uint32_t fraction = gba->cartridge.rtc.fraction;
        cupid_gba_reset(gba);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF && !gba->irqf);
        CHECK(gba->cartridge.rtc.interruptParameters == (frequency ? 0x8000 : 0));
        CHECK(gba->cartridge.rtc.fraction == fraction + 5);
        CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent) == (frequency != 0));
        if (frequency) {
            CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) == 7);
            cupid_timing_tick(&gba->timing, 7);
            CHECK(!gba->cartridge.rtc.interruptLow && gba->irq.pendingIF == IRQ_GAMEPAK);
        }
        cupid_cartridge_enable_rtc(gba, false);
        CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent));
        cupid_cartridge_enable_rtc(gba, true);
        CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent) == (frequency != 0));
        const uint8_t image[] = "replacement cartridge";
        cupid_cartridge_attach(gba, image, sizeof(image));
        CHECK(!gba->cartridge.rtcPresent && !gba->cartridge.rtc.interruptParameters);
        CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent));
        destroy(gba);
    }
}

struct ControlCompletion {
    struct CupidGBA* gba;
    uint64_t clock;
};

static void finish_control_byte(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct ControlCompletion* completion = context;
    completion->clock = cupid_timing_global_time(timing);
    CHECK(cupid_cartridge_gpio_write(completion->gba, 0x080000C4, 5));
}

static void minute_rearm_uses_the_final_gpio_strobe_through_late_dispatch_and_clock_wrap(void) {
    const unsigned late[] = {0, 1, 2, 3, 64};
    for (unsigned thumb = 0; thumb < 2; thumb++) for (unsigned steady = 0; steady < 2; steady++)
        for (unsigned outside = 0; outside < 2; outside++) for (unsigned wrap = 0; wrap < 2; wrap++)
            for (unsigned sample = 0; sample < sizeof(late) / sizeof(*late); sample++) {
                struct CupidGBA* gba = machine();
                if (wrap) {
                    cupid_timing_clear(&gba->timing);
                    gba->timing.globalCycles = UINT32_MAX - CUPID_RTC_MINUTE_CARRY_CLOCKS / 2;
                    gba->timing.masterCycles = (uint32_t)gba->timing.globalCycles;
                    cupid_irq_init(gba);
                }
                const uint8_t control = steady ? 0x4A : 0x48;
                control_write(gba, thumb != 0, control);
                cupid_irq_write(gba, REG_IE, IRQ_GAMEPAK);
                cupid_irq_write(gba, REG_IME, 1);
                cupid_timing_tick(&gba->timing, 4);
                phase(gba, 59, false, 1);
                cupid_timing_tick(&gba->timing, 1);
                CHECK(gba->cartridge.rtc.minuteCarryActive && gba->cartridge.rtc.interruptLow);
                port_write(gba, thumb != 0, 2, 7);
                port_write(gba, thumb != 0, 0, 1);
                port_write(gba, thumb != 0, 0, 5);
                send_byte(gba, thumb != 0, 0x46);
                for (unsigned bit = 0; bit < 7; bit++) {
                    const uint16_t pins = (uint16_t)(4 | ((control >> bit) & 1) << 1);
                    port_write(gba, thumb != 0, 0, pins);
                    port_write(gba, thumb != 0, 0, pins | 1);
                }
                port_write(gba, thumb != 0, 0, 4);
                cupid_cartridge_rtc_sync(gba);
                CHECK(gba->cartridge.rtc.minuteCarryActive && !gba->irq.pendingIF);
                CHECK(gba->cartridge.rtc.fraction < CUPID_RTC_MINUTE_CARRY_CLOCKS - 1);
                const uint32_t delay = CUPID_RTC_MINUTE_CARRY_CLOCKS - gba->cartridge.rtc.fraction -
                    (outside ? 0u : 1u);
                const uint64_t expected = cupid_timing_global_time(&gba->timing) + delay;
                struct ControlCompletion completion = {.gba = gba};
                struct CupidTimingEvent event = {.context = &completion, .callback = finish_control_byte,
                    .name = "RTC status completion", .priority = 0x30};
                cupid_timing_schedule(&gba->timing, &event, (int32_t)delay);
                cupid_timing_tick(&gba->timing, (int32_t)(delay + late[sample]));
                CHECK(completion.clock == expected && gba->cartridge.rtc.control == control);
                CHECK(gba->cartridge.rtc.interruptLow == !outside);
                CHECK(gba->irq.pendingIF == (outside ? IRQ_GAMEPAK : 0));
                CHECK(gba->irqf == (outside && late[sample] >= 1 ? IRQ_GAMEPAK : 0));
                CHECK(gba->irq.available == (outside && late[sample] >= 2));
                CHECK(gba->irq.line == (outside && late[sample] >= 3));
                if (!outside) {
                    /* Catch up to observation time without acknowledging output. */
                    cupid_cartridge_rtc_sync(gba);
                    CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
                    if (late[sample]) CHECK(!gba->cartridge.rtc.minuteCarryActive);
                }
                destroy(gba);
            }
}

static void live_stop_and_console_reset_preserve_the_remaining_carry_window(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) for (unsigned steady = 0; steady < 2; steady++) {
        struct CupidGBA* gba = machine();
        const uint8_t control = steady ? 0x4A : 0x48;
        control_write(gba, thumb != 0, control);
        phase(gba, 59, false, 1);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->cartridge.rtc.minuteCarryActive);
        cupid_gba_stop(gba);
        const uint64_t clock = cupid_timing_global_time(&gba->timing);
        CHECK(cupid_gba_advance_stopped_time(gba, 1000) == 1000);
        CHECK(gba->stopped && gba->cartridge.rtc.minuteCarryActive);
        CHECK(cupid_timing_global_time(&gba->timing) == clock && !gba->irq.pendingIF);
        cupid_gba_reset(gba);
        CHECK(gba->cartridge.rtc.minuteCarryActive && gba->cartridge.rtc.fraction == 1000);
        control_write(gba, thumb != 0, control);
        CHECK(gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
        cupid_gba_stop(gba);
        cupid_cartridge_rtc_sync(gba);
        const uint32_t remaining = CUPID_RTC_MINUTE_CARRY_CLOCKS - gba->cartridge.rtc.fraction;
        CHECK(cupid_gba_advance_stopped_time(gba, remaining) == remaining);
        CHECK(gba->stopped && !gba->cartridge.rtc.minuteCarryActive && !gba->irq.pendingIF);
        cupid_gba_reset(gba);
        control_write(gba, thumb != 0, control);
        CHECK(!gba->cartridge.rtc.interruptLow && gba->irqf == IRQ_GAMEPAK);
        destroy(gba);
    }
}

int main(void) {
    RUN(minute_rearm_uses_the_final_gpio_strobe_through_late_dispatch_and_clock_wrap);
    RUN(live_stop_and_console_reset_preserve_the_remaining_carry_window);
    RUN(frequency_and_alarm_outputs_survive_console_reset_and_cancel_on_replacement);
    RUN(repeated_frequency_edges_keep_timer_dma_and_late_wrap_chronology);
    RUN(every_frequency_releases_through_arm_thumb_gpio_and_the_irq_pipeline);
    RUN(alarm_gpio_acknowledgement_and_long_wait_checkpoints_keep_the_latch);
    RUN(steady_edges_enter_the_irq_pipeline_through_the_board_inverter);
    RUN(held_minute_output_releases_on_a_complete_control_write);
    RUN(console_reset_disable_and_cartridge_replacement_preserve_ownership);
    RUN(late_dispatch_clock_wrap_timer_and_dma_preserve_every_edge);
    RUN(cartridge_irq_wakes_halt_with_ime_clear);
    RUN(forced_requests_retain_the_original_gpio_strobe_when_dispatched_late);
    return 0;
}
