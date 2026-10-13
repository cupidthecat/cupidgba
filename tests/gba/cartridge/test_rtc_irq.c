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

static void phase(struct CupidGBA* gba, unsigned second, bool low, unsigned until) {
    const struct CupidRTCDateTime date = {26, 10, 13, 2, 12, 34, (uint8_t)second};
    CHECK(cupid_rtc_set_datetime(&gba->cartridge.rtc, &date));
    gba->cartridge.rtc.minuteLow = low;
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
        CHECK(!gba->cartridge.rtc.minuteLow && !gba->irq.pendingIF);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->cartridge.rtc.minuteLow && !gba->irq.pendingIF);
        CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) == 30 * CUPID_RTC_CLOCK_HZ);
        phase(gba, 29, true, 12);
        cupid_timing_tick(&gba->timing, 11);
        CHECK(gba->cartridge.rtc.minuteLow && !gba->irq.pendingIF);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(!gba->cartridge.rtc.minuteLow && gba->irq.pendingIF == IRQ_GAMEPAK);
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
        CHECK(gba->cartridge.rtc.minuteLow && !gba->irq.pendingIF);
        CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent));
        /* A control read or an aborted write must not acknowledge the latch. */
        CHECK(cupid_rtc_read_control(&gba->cartridge.rtc) == 0x48);
        port_write(gba, thumb != 0, 0, 1);
        port_write(gba, thumb != 0, 0, 5);
        send_byte(gba, thumb != 0, 0x46);
        port_write(gba, thumb != 0, 0, 1);
        CHECK(gba->cartridge.rtc.minuteLow && !gba->irqf);
        control_write(gba, thumb != 0, 0x48);
        CHECK(!gba->cartridge.rtc.minuteLow && gba->irqf == IRQ_GAMEPAK);
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
    CHECK(gba->cartridge.rtc.minuteLow && gba->cartridge.rtc.control == 0x4A);
    CHECK(!gba->cartridge.readable && !gba->cartridge.direction);
    CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) == 7);
    cupid_timing_tick(&gba->timing, 7);
    CHECK(!gba->cartridge.rtc.minuteLow && gba->irq.pendingIF == IRQ_GAMEPAK);
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
};

static void observe(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct Trace* trace = context;
    CHECK(trace->count < 48);
    trace->flags[trace->count] = trace->gba->irqf;
    trace->low[trace->count++] = trace->gba->cartridge.rtc.minuteLow;
    if (trace->count < 48) cupid_timing_schedule(timing, &trace->event, 1);
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

int main(void) {
    RUN(steady_edges_enter_the_irq_pipeline_through_the_board_inverter);
    RUN(held_minute_output_releases_on_a_complete_control_write);
    RUN(console_reset_disable_and_cartridge_replacement_preserve_ownership);
    RUN(late_dispatch_clock_wrap_timer_and_dma_preserve_every_edge);
    RUN(cartridge_irq_wakes_halt_with_ime_clear);
    RUN(forced_requests_retain_the_original_gpio_strobe_when_dispatched_late);
    return 0;
}
