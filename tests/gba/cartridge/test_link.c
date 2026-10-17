/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/cable.h"
#include "../../test.h"

static struct CupidGBA* machine(uint64_t epoch, bool stopped, bool wake, unsigned fraction) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba); cupid_gba_reset(gba);
    cupid_irq_write(gba, REG_IE, wake ? IRQ_GAMEPAK : 0);
    cupid_timing_tick(&gba->timing, 8);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    gba->cpu.halted = true;
    cupid_cartridge_enable_rtc(gba, true);
    cupid_rtc_rebase(&gba->cartridge.rtc, epoch);
    gba->cartridge.rtc.fraction = fraction;
    cupid_rtc_write_interrupt_parameters(&gba->cartridge.rtc, 1);
    cupid_rtc_write_control(&gba->cartridge.rtc, 0x42);
    cupid_cartridge_rtc_reschedule(gba);
    if (stopped) cupid_gba_stop(gba);
    CHECK(gba->stopped == stopped && gba->ie == (wake ? IRQ_GAMEPAK : 0));
    return gba;
}

static void independent_stopped_oscillators(uint64_t epoch, bool split) {
    struct CupidGBA* cores[] = {
        machine(epoch, true, true, 0), machine(epoch, false, false, 0),
        machine(epoch, true, false, 0), machine(epoch, true, true, 32)
    };
    struct CupidCable* cable = cupid_cable_create(cores, 4); CHECK(cable);
    const unsigned checkpoints[] = {223, 224, 255, 256, 400};
    unsigned previous = 0;
    for (unsigned step = 0; step < sizeof(checkpoints) / sizeof(*checkpoints); ++step) {
        const unsigned until = checkpoints[step];
        if (split) for (unsigned i = previous; i < until; ++i) CHECK(cupid_cable_run(cable, 1));
        else CHECK(cupid_cable_run(cable, until - previous));
        CHECK(cupid_cable_time(cable) == until);
        CHECK(cores[0]->stopped == (until < 256));
        CHECK(cores[3]->stopped == (until < 224));
        CHECK(cores[2]->stopped);
        CHECK(cupid_timing_global_time(&cores[0]->timing) == epoch + (until > 256 ? until - 256 : 0));
        CHECK(cupid_timing_global_time(&cores[1]->timing) == epoch + until);
        CHECK(cupid_timing_global_time(&cores[2]->timing) == epoch);
        CHECK(cupid_timing_global_time(&cores[3]->timing) == epoch + (until > 224 ? until - 224 : 0));
        /* Readback brings running RTCs to their local clock; stopped RTCs
         * must already have consumed the independent session interval. */
        for (unsigned i = 0; i < 4; ++i) {
            cupid_cartridge_rtc_sync(cores[i]);
            CHECK(cores[i]->cartridge.rtc.fraction == until + (i == 3 ? 32 : 0));
        }
        if (until >= 256) CHECK((cores[2]->irq.pendingIF | cores[2]->irqf) & IRQ_GAMEPAK);
        previous = until;
    }
    cupid_cable_destroy(cable);
    for (unsigned i = 0; i < 4; ++i) { cupid_gba_destroy(cores[i]); free(cores[i]); }
}

int main(void) {
    for (unsigned wrap = 0; wrap < 2; ++wrap)
        for (unsigned split = 0; split < 2; ++split)
            independent_stopped_oscillators(wrap ? UINT64_C(0xFFFFFF80) : 0, split != 0);
    return 0;
}
