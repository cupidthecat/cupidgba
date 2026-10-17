/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"

static const char* bios;

static struct CupidGBA* machine(bool thumb) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 4096);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 4096;
    gba->memory.romMask = 4095;
    if (bios) CHECK(cupid_load_bios(gba, bios));
    cupid_gba_reset(gba);
    cupid_gba_skip_bios(gba);
    cupid_cartridge_enable_rtc(gba, true);
    const uint32_t arm[] = {0xE1A00000, 0xE1A00000, 0xEAFFFFFE};
    const uint16_t small[] = {0x46C0, 0x46C0, 0xE7FE};
    memcpy(gba->memory.iwram, thumb ? (const void*)small : (const void*)arm,
           thumb ? sizeof(small) : sizeof(arm));
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb) CupidThumbWritePC(&gba->cpu);
    else CupidARMWritePC(&gba->cpu);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void stop(struct CupidGBA* gba, uint16_t ie, bool ime) {
    cupid_irq_write(gba, REG_IE, ie);
    cupid_irq_write(gba, REG_IME, ime);
    gba->cpu.cycles += 8;
    cupid_gba_stop(gba);
    CHECK(gba->stopped && gba->cpu.halted);
    CHECK(!gba->irq.pendingIF && !gba->irqf);
}

static void frequency(struct CupidGBA* gba, uint16_t mask, uint32_t fraction) {
    cupid_rtc_write_interrupt_parameters(&gba->cartridge.rtc, mask);
    gba->cartridge.rtc.fraction = fraction;
    cupid_rtc_write_control(&gba->cartridge.rtc, 0x42);
    cupid_cartridge_rtc_reschedule(gba);
}

static void calendar_advances_while_system_peripherals_and_deadlines_are_frozen(void) {
    struct CupidGBA* gba = machine(false);
    cupid_io_write(gba, REG_TM0CNT_L, 0x8000);
    cupid_io_write(gba, REG_TM0CNT_H, 0x80);
    cupid_io_write(gba, REG_SIOCNT, 0x4081);
    stop(gba, IRQ_KEYPAD | IRQ_TIMER0 | IRQ_SIO, true);
    const struct CupidRTCDateTime date = {4, 2, 28, 6, 23, 59, 59};
    CHECK(cupid_rtc_set_datetime(&gba->cartridge.rtc, &date));
    const uint64_t clock = cupid_timing_global_time(&gba->timing);
    const uint32_t timerDeadline = gba->timers[0].event.when;
    const uint16_t timer = cupid_timer_read_counter(gba, 0);
    const uint16_t line = cupid_io_read(gba, REG_VCOUNT);
    const int samples = gba->audio.sampleWritePos;
    const unsigned remaining = gba->serial.remaining;
    const int32_t pc = gba->cpu.gprs[ARM_PC];
    for (unsigned i = 0; i < 20; i++) cupid_gba_step(gba);
    CHECK(!gba->cartridge.rtc.fraction && gba->cartridge.rtc.date.second == 59);
    const uint64_t elapsed = 2 * CUPID_RTC_CLOCK_HZ + 13;
    CHECK(cupid_gba_advance_stopped_time(gba, elapsed) == elapsed);
    CHECK(gba->stopped && gba->cpu.gprs[ARM_PC] == pc);
    CHECK(cupid_timing_global_time(&gba->timing) == clock);
    CHECK(cupid_timer_read_counter(gba, 0) == timer && gba->timers[0].event.when == timerDeadline);
    CHECK(cupid_io_read(gba, REG_VCOUNT) == line && gba->audio.sampleWritePos == samples);
    CHECK(gba->serial.remaining == remaining && !gba->irq.pendingIF && !gba->frameReady);
    CHECK(gba->cartridge.rtc.date.month == 2 && gba->cartridge.rtc.date.day == 29);
    CHECK(gba->cartridge.rtc.date.weekday == 0 && gba->cartridge.rtc.date.hour == 0);
    CHECK(gba->cartridge.rtc.date.minute == 0 && gba->cartridge.rtc.date.second == 1);
    CHECK(gba->cartridge.rtc.fraction == 13 && gba->cartridge.rtc.lastClock == clock);
    cupid_gba_reset(gba);
    CHECK(!gba->stopped && gba->cartridge.rtc.fraction == 13);
    CHECK(gba->cartridge.rtc.date.day == 29 && gba->cartridge.rtc.date.second == 1);
    destroy(gba);
}

static void each_divider_wakes_at_its_release_and_uses_the_resumed_irq_pipeline(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned bit = 0; bit < 16; bit++)
            for (unsigned masks = 0; masks < 4; masks++) {
                struct CupidGBA* gba = machine(thumb != 0);
                const bool ime = (masks & 1) != 0, masked = (masks & 2) != 0;
                gba->cpu.cpsr.i = masked;
                gba->cpu.irqDisableLatch = masked;
                stop(gba, IRQ_GAMEPAK, ime);
                const uint32_t half = 256u << bit;
                frequency(gba, (uint16_t)(1u << bit), half - 12);
                const uint64_t clock = cupid_timing_global_time(&gba->timing);
                CHECK(cupid_gba_advance_stopped_time(gba, 11) == 11);
                CHECK(gba->stopped && gba->cartridge.rtc.interruptLow && !gba->irq.pendingIF);
                CHECK(cupid_gba_advance_stopped_time(gba, CUPID_RTC_CLOCK_HZ) == 1);
                CHECK(!gba->stopped && !gba->cartridge.rtc.interruptLow && gba->cpu.halted);
                CHECK(cupid_timing_global_time(&gba->timing) == clock);
                CHECK(gba->irq.pendingIF == IRQ_GAMEPAK && !gba->irqf && !gba->irq.available && !gba->irq.line);
                CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) == (int32_t)half);
                cupid_timing_tick(&gba->timing, 1);
                CHECK(gba->irqf == IRQ_GAMEPAK && !gba->irq.available && !gba->irq.line);
                cupid_timing_tick(&gba->timing, 1);
                CHECK(gba->irq.available && !gba->irq.line);
                cupid_timing_tick(&gba->timing, 1);
                CHECK(gba->irq.line == ime);
                const uint32_t nop = 0xE1A00000;
                memcpy(gba->memory.bios + BASE_IRQ, &nop, sizeof(nop));
                cupid_gba_step(gba);
                CHECK(!gba->cpu.halted);
                CHECK((gba->cpu.privilegeMode == MODE_IRQ) == (ime && !masked));
                destroy(gba);
            }
}

static void disabled_wake_latches_one_pending_request_and_handles_large_elapsed_time(void) {
    struct CupidGBA* gba = machine(false);
    stop(gba, IRQ_KEYPAD, false);
    frequency(gba, 1, 251);
    CHECK(cupid_gba_advance_stopped_time(gba, 5) == 5);
    CHECK(gba->stopped && gba->irq.pendingIF == IRQ_GAMEPAK && !gba->irqf);
    const struct CupidRTC before = gba->cartridge.rtc;
    const uint64_t elapsed = UINT64_MAX;
    CHECK(cupid_gba_advance_stopped_time(gba, elapsed) == elapsed);
    CHECK(gba->stopped && gba->irq.pendingIF == IRQ_GAMEPAK && !gba->irqf);
    struct CupidRTC expected = before;
    cupid_rtc_advance_seconds(&expected, elapsed / CUPID_RTC_CLOCK_HZ);
    cupid_rtc_advance_clocks(&expected, elapsed % CUPID_RTC_CLOCK_HZ);
    CHECK(!memcmp(&gba->cartridge.rtc.date, &expected.date, sizeof(expected.date)));
    CHECK(gba->cartridge.rtc.fraction == expected.fraction);
    CHECK(gba->cartridge.rtc.interruptLow == expected.interruptLow);
    CHECK(gba->cartridge.rtc.lastClock == before.lastClock);
    cupid_raise_irq(gba, IRQ_KEYPAD, 0);
    CHECK(!gba->stopped);
    cupid_timing_tick(&gba->timing, 3);
    CHECK(gba->irqf == (IRQ_GAMEPAK | IRQ_KEYPAD));
    destroy(gba);
}

static void minute_and_alarm_latches_remain_asleep_until_a_release(void) {
    for (unsigned mode = 0; mode < 3; mode++) {
        struct CupidGBA* gba = machine(false);
        stop(gba, IRQ_GAMEPAK, false);
        const struct CupidRTCDateTime date = {26, 10, 13, 2, 12, 33, 59};
        CHECK(cupid_rtc_set_datetime(&gba->cartridge.rtc, &date));
        gba->cartridge.rtc.fraction = CUPID_RTC_CLOCK_HZ - 12;
        cupid_rtc_write_interrupt_parameters(&gba->cartridge.rtc, 0x3492);
        cupid_rtc_write_control(&gba->cartridge.rtc, mode == 0 ? 0x48 : mode == 1 ? 0x4A : 0x60);
        cupid_cartridge_rtc_reschedule(gba);
        const uint64_t interval = 30 * CUPID_RTC_CLOCK_HZ + 100;
        const uint64_t elapsed = cupid_gba_advance_stopped_time(gba, interval);
        if (mode == 1) {
            CHECK(elapsed == 30 * CUPID_RTC_CLOCK_HZ + 12);
            CHECK(!gba->stopped && !gba->cartridge.rtc.interruptLow);
            CHECK(gba->cartridge.rtc.date.second == 30 && !gba->cartridge.rtc.fraction);
            CHECK(gba->irq.pendingIF == IRQ_GAMEPAK && !gba->irqf);
        } else {
            CHECK(elapsed == interval && gba->stopped && gba->cartridge.rtc.interruptLow);
            CHECK(!gba->irq.pendingIF && !gba->irqf);
            CHECK(cupid_gba_advance_stopped_time(gba, 3 * 86400 * CUPID_RTC_CLOCK_HZ) ==
                  3 * 86400 * CUPID_RTC_CLOCK_HZ);
            CHECK(gba->stopped && gba->cartridge.rtc.date.day == 16 && !gba->irq.pendingIF);
        }
        destroy(gba);
    }
}

static void external_clock_anchor_wrap_and_absent_device_preserve_ownership(void) {
    struct CupidRTC rtc;
    cupid_rtc_init(&rtc, UINT64_MAX - 5);
    rtc.fraction = CUPID_RTC_CLOCK_HZ - 3;
    cupid_rtc_advance_clocks(&rtc, 10);
    CHECK(rtc.lastClock == UINT64_MAX - 5 && rtc.fraction == 7 && rtc.date.second == 1);
    cupid_rtc_sync(&rtc, UINT64_MAX);
    CHECK(rtc.fraction == 12 && rtc.date.second == 1);
    cupid_rtc_sync(&rtc, 0);
    cupid_rtc_sync(&rtc, CUPID_RTC_CLOCK_HZ);
    CHECK(rtc.fraction == 12 && rtc.date.second == 2);
    struct CupidGBA* gba = machine(false);
    CHECK(cupid_gba_advance_stopped_time(gba, 100) == 0);
    stop(gba, 0, false);
    frequency(gba, 1, 251);
    CHECK(cupid_gba_advance_stopped_time(gba, 0) == 0 && gba->cartridge.rtc.fraction == 251);
    cupid_cartridge_enable_rtc(gba, false);
    CHECK(cupid_gba_advance_stopped_time(gba, CUPID_RTC_CLOCK_HZ) == CUPID_RTC_CLOCK_HZ);
    CHECK(gba->cartridge.rtc.fraction == 251 && !gba->irq.pendingIF);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->cartridge.rtcInterruptEvent));
    destroy(gba);
}

static void paced_intervals_resume_with_only_the_remaining_system_clocks(void) {
    struct CupidGBA* gba = machine(false);
    stop(gba, IRQ_GAMEPAK, false);
    frequency(gba, 1, 246);
    const uint64_t clock = cupid_timing_global_time(&gba->timing);
    cupid_gba_run_interval(gba, 8);
    CHECK(gba->stopped && gba->cartridge.rtc.fraction == 254);
    cupid_gba_run_interval(gba, 32);
    const uint64_t running = cupid_timing_global_time(&gba->timing) - clock;
    CHECK(!gba->stopped && !gba->cpu.halted && running >= 30 && running <= 33);
    cupid_cartridge_rtc_sync(gba);
    CHECK(gba->cartridge.rtc.fraction == 256 + running);
    CHECK(!gba->frameReady && gba->irqf == IRQ_GAMEPAK);
    destroy(gba);
}

static void stop_swi_returns_in_both_instruction_sets_and_bios_paths(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(thumb != 0);
        const uint32_t arm[] = {0xEF030000, 0xE3A0402A, 0xEAFFFFFE};
        const uint16_t small[] = {0xDF03, 0x242A, 0xE7FE};
        memcpy(gba->memory.iwram, thumb ? (const void*)small : (const void*)arm,
               thumb ? sizeof(small) : sizeof(arm));
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
        if (thumb) CupidThumbWritePC(&gba->cpu);
        else CupidARMWritePC(&gba->cpu);
        cupid_irq_write(gba, REG_IE, IRQ_GAMEPAK);
        for (unsigned steps = 0; !gba->stopped && steps < 1000; steps++) cupid_gba_step(gba);
        CHECK(gba->stopped);
        frequency(gba, 0x8000, (uint32_t)(CUPID_RTC_CLOCK_HZ / 2 - 12));
        CHECK(cupid_gba_advance_stopped_time(gba, CUPID_RTC_CLOCK_HZ) == 12);
        for (unsigned steps = 0; gba->cpu.gprs[4] != 42 && steps < 1000; steps++) cupid_gba_step(gba);
        CHECK(gba->cpu.gprs[4] == 42 && !gba->stopped && !gba->cpu.halted);
        CHECK(gba->cpu.privilegeMode == MODE_SYS && gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM));
        destroy(gba);
    }
}

static void chunked_intervals_preserve_execution_and_do_not_accumulate_overshoot(void) {
    struct CupidGBA* bulk = machine(false);
    struct CupidGBA* chunks = machine(false);
    stop(bulk, IRQ_GAMEPAK, false);
    stop(chunks, IRQ_GAMEPAK, false);
    frequency(bulk, 0x800A, 0x800A * 256u - 12);
    frequency(chunks, 0x800A, 0x800A * 256u - 12);
    const uint64_t clock = cupid_timing_global_time(&bulk->timing);
    CHECK(clock == cupid_timing_global_time(&chunks->timing));
    const uint32_t interval = 12345;
    cupid_gba_run_interval(bulk, interval);
    for (uint32_t elapsed = 0; elapsed < interval;) {
        const uint32_t amount = interval - elapsed > 17 ? 17 : interval - elapsed;
        cupid_gba_run_interval(chunks, amount);
        elapsed += amount;
    }
    CHECK(!bulk->stopped && !chunks->stopped);
    CHECK(cupid_timing_global_time(&bulk->timing) == cupid_timing_global_time(&chunks->timing));
    CHECK(cupid_timing_global_time(&bulk->timing) - clock == interval - 12 + bulk->intervalOvershoot);
    CHECK(bulk->intervalOvershoot == chunks->intervalOvershoot);
    CHECK(!memcmp(bulk->cpu.gprs, chunks->cpu.gprs, sizeof(bulk->cpu.gprs)));
    cupid_cartridge_rtc_sync(bulk);
    cupid_cartridge_rtc_sync(chunks);
    CHECK(bulk->cartridge.rtc.fraction == chunks->cartridge.rtc.fraction);
    CHECK(bulk->cartridge.rtc.interruptLow == chunks->cartridge.rtc.interruptLow);
    cupid_gba_reset(bulk);
    CHECK(!bulk->intervalOvershoot);
    destroy(bulk);
    destroy(chunks);
}

static void stopped_release_reschedules_across_system_clock_wrap(void) {
    struct CupidGBA* gba = machine(false);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = UINT32_MAX - 12;
    gba->timing.masterCycles = (uint32_t)gba->timing.globalCycles;
    cupid_irq_init(gba);
    stop(gba, IRQ_GAMEPAK, true);
    CHECK((uint32_t)cupid_timing_global_time(&gba->timing) == UINT32_MAX - 4);
    frequency(gba, 1, 251);
    CHECK(cupid_gba_advance_stopped_time(gba, 1024) == 5);
    CHECK(!gba->stopped && gba->irq.pendingIF == IRQ_GAMEPAK && !gba->irqf);
    cupid_timing_tick(&gba->timing, 3);
    CHECK(gba->irqf == IRQ_GAMEPAK && gba->irq.available && gba->irq.line);
    CHECK(cupid_timing_until(&gba->timing, &gba->cartridge.rtcInterruptEvent) == 253);
    cupid_timing_tick(&gba->timing, 253);
    CHECK(gba->cartridge.rtc.interruptLow && gba->cartridge.rtc.fraction == 512);
    CHECK(cupid_timing_global_time(&gba->timing) > UINT32_MAX);
    destroy(gba);
}

int main(int argc, char** argv) {
    if (argc == 2) bios = argv[1];
    else CHECK(argc == 1);
    RUN(calendar_advances_while_system_peripherals_and_deadlines_are_frozen);
    RUN(each_divider_wakes_at_its_release_and_uses_the_resumed_irq_pipeline);
    RUN(disabled_wake_latches_one_pending_request_and_handles_large_elapsed_time);
    RUN(minute_and_alarm_latches_remain_asleep_until_a_release);
    RUN(external_clock_anchor_wrap_and_absent_device_preserve_ownership);
    RUN(paced_intervals_resume_with_only_the_remaining_system_clocks);
    RUN(stop_swi_returns_in_both_instruction_sets_and_bios_paths);
    RUN(chunked_intervals_preserve_execution_and_do_not_accumulate_overshoot);
    RUN(stopped_release_reschedules_across_system_clock_wrap);
    return 0;
}
