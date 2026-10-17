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

static void rtc_wake_starts_uart_after_peer_clock(uint64_t epoch, bool split, unsigned sender) {
    struct CupidGBA* cores[2];
    cores[sender] = machine(epoch, false, true, 0);
    cores[sender ^ 1] = machine(epoch, false, false, 0);
    struct CupidGBA* tx = cores[sender];
    struct CupidGBA* rx = cores[sender ^ 1];
    cupid_io_write(tx, REG_RCNT, 0);
    cupid_serial_set_input(&tx->serial, SERIAL_SC, SERIAL_SC);
    cupid_io_write(tx, REG_SIOCNT, 0x3487);
    cupid_io_write(tx, REG_SIOMLT_SEND, 0x95);
    CHECK(tx->serial.uart.transmitReady && !tx->serial.uart.transmitting);
    cupid_gba_stop(tx); CHECK(tx->stopped);
    cupid_io_write(rx, REG_RCNT, 0);
    cupid_io_write(rx, REG_SIOCNT, 0x3883);
    struct CupidCable* cable = cupid_cable_create(cores, 2); CHECK(cable);
    /* Connection releases CTS while TX remains stopped. Its RTC wake must
     * place the start bit at the peer's endpoint, regardless of player order. */
    if (split) for (unsigned i = 0; i < 256; ++i) CHECK(cupid_cable_run(cable, 1));
    else CHECK(cupid_cable_run(cable, 256));
    CHECK(rx->serial.uart.receiving && rx->serial.uart.receiveStart == epoch + 256);
    CHECK(cupid_cable_run(cable, 2000));
    CHECK(!tx->serial.uart.transmitting && !rx->serial.uart.receiving);
    CHECK(rx->serial.uart.receiveReady && !rx->serial.uart.error);
    CHECK(cupid_io_read(rx, REG_SIOMLT_SEND) == 0x95);
    cupid_cable_destroy(cable);
    for (unsigned i = 0; i < 2; ++i) { cupid_gba_destroy(cores[i]); free(cores[i]); }
}

static void rtc_wake_preserves_gpio_peer_stop(uint64_t epoch, bool split, unsigned sender) {
    struct CupidGBA* cores[2];
    cores[sender] = machine(epoch, false, true, 0);
    cores[sender ^ 1] = machine(epoch, false, false, 32);
    struct CupidGBA* tx = cores[sender];
    struct CupidGBA* rx = cores[sender ^ 1];
    cupid_io_write(tx, REG_RCNT, 0);
    cupid_serial_set_input(&tx->serial, SERIAL_SC, SERIAL_SC);
    cupid_io_write(tx, REG_SIOCNT, 0x3487);
    cupid_io_write(tx, REG_SIOMLT_SEND, 0x95);
    cupid_gba_stop(tx); CHECK(tx->stopped);
    cupid_irq_write(rx, REG_IE, IRQ_SIO);
    cupid_timing_tick(&rx->timing, 8);
    cupid_cartridge_rtc_sync(rx);
    rx->cartridge.rtc.fraction = 32;
    cupid_cartridge_rtc_reschedule(rx);
    cupid_io_write(rx, REG_RCNT, 0x8120);
    cupid_gba_stop(rx); CHECK(rx->stopped);
    const uint64_t clock = cupid_timing_global_time(&rx->timing);
    struct CupidCable* cable = cupid_cable_create(cores, 2); CHECK(cable);
    /* The queued UART start wakes GPIO SI at the endpoint. Its peer must
     * consume the preceding interval entirely on the battery oscillator. */
    if (split) for (unsigned i = 0; i < 256; ++i) CHECK(cupid_cable_run(cable, 1));
    else CHECK(cupid_cable_run(cable, 256));
    CHECK(tx->serial.uart.transmitting && !rx->stopped);
    CHECK(cupid_timing_global_time(&rx->timing) == clock);
    CHECK(rx->cartridge.rtc.fraction == 288);
    CHECK((rx->irq.pendingIF | rx->irqf) & IRQ_SIO);
    cupid_cable_destroy(cable);
    for (unsigned i = 0; i < 2; ++i) { cupid_gba_destroy(cores[i]); free(cores[i]); }
}

int main(void) {
    for (unsigned wrap = 0; wrap < 2; ++wrap) for (unsigned split = 0; split < 2; ++split) {
        const uint64_t epoch = wrap ? UINT64_C(0xFFFFFF80) : 0;
        independent_stopped_oscillators(epoch, split != 0);
        for (unsigned sender = 0; sender < 2; ++sender) {
            rtc_wake_starts_uart_after_peer_clock(epoch, split != 0, sender);
            rtc_wake_preserves_gpio_peer_stop(epoch, split != 0, sender);
        }
    }
    puts("PASS RTC local STOP: 4 independent-clock cases, 8 complete UART frames, 8 GPIO wakes");
    return 0;
}
