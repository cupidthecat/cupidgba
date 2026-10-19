/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"

static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    gba->audio.lastSample = clock;
    gba->audio.sampleOrigin = clock;
    gba->audio.nextSample = clock + 512;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void tick(struct CupidGBA* gba, int cycles) {
    CHECK(gba->cpu.cycles == 0);
    cupid_timing_tick(&gba->timing, cycles);
}

static void configure(struct CupidGBA* gba, int ch, uint16_t reload, uint16_t control) {
    cupid_io_write(gba, REG_TMCNT_LO(ch), reload);
    cupid_io_write(gba, REG_TMCNT_LO(ch) + 2, control);
}

static void register_latency_and_frozen_counter(void) {
    struct CupidGBA* gba = machine(0);
    configure(gba, 0, 0x1234, 0x80);
    CHECK(cupid_io_read(gba, REG_TM0CNT_H) == 0);
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0);
    tick(gba, 1);
    CHECK(cupid_io_read(gba, REG_TM0CNT_H) == 0x80);
    tick(gba, 1);
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0x1234);
    tick(gba, 3);
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0x1237);
    cupid_io_write(gba, REG_TM0CNT_L, 0x5678);
    cupid_io_write(gba, REG_TM0CNT_H, 0);
    CHECK(cupid_io_read(gba, REG_TM0CNT_H) == 0x80);
    tick(gba, 1);
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0x1238);
    tick(gba, 200);
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0x1238);
    cupid_io_write(gba, REG_TM0CNT_H, 0x80);
    tick(gba, 2);
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0x5678);
    destroy(gba);
}

static void byte_latch_and_control_masks(void) {
    struct CupidGBA* gba = machine(0);
    for (int ch = 0; ch < 4; ch++) {
        uint32_t address = GBA_BASE_IO + REG_TMCNT_LO(ch);
        cupid_memory_store16(&gba->cpu, address, 0x1234, NULL);
        tick(gba, 1);
        /* Merge bytes into the reload latch, never the stopped counter (zero). */
        cupid_memory_store8(&gba->cpu, address, 0x56, NULL);
        cupid_memory_store8(&gba->cpu, address + 1, 0xAB, NULL);
        cupid_memory_store8(&gba->cpu, address + 2, 0xFF, NULL);
        tick(gba, 1);
        CHECK(cupid_memory_loadU16(&gba->cpu, address + 2, NULL) == (ch ? 0xC7u : 0xC3u));
        tick(gba, 1);
        CHECK(cupid_memory_loadU16(&gba->cpu, address, NULL) == 0xAB56);
        cupid_memory_store8(&gba->cpu, address + 3, 0xFF, NULL);
        tick(gba, 1);
        CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->timers[ch].controlEvent));
        CHECK(cupid_memory_loadU16(&gba->cpu, address + 2, NULL) == (ch ? 0xC7u : 0xC3u));
    }
    destroy(gba);
}

static void global_prescaler_phase_and_live_changes(void) {
    const unsigned periods[] = {1, 64, 256, 1024};
    for (unsigned selector = 0; selector < 4; selector++) {
        unsigned period = periods[selector];
        struct CupidGBA* gba = machine(period + 5);
        configure(gba, 0, 0x1000, 0x80 | selector);
        tick(gba, 2);
        uint64_t next = ((gba->timing.globalCycles - 1) / period + 1) * period + 1;
        tick(gba, (int)(next - gba->timing.globalCycles - 1));
        CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0x1000);
        tick(gba, 1);
        CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0x1001);
        /* Changing to /1 preserves the counter instead of reloading it. */
        cupid_io_write(gba, REG_TM0CNT_H, 0x80);
        tick(gba, 1);
        uint16_t stopped = cupid_io_read(gba, REG_TM0CNT_L);
        tick(gba, 7);
        CHECK(cupid_io_read(gba, REG_TM0CNT_L) == (uint16_t)(stopped + 7));
        destroy(gba);
    }
}

static void coincident_reload_and_stop(void) {
    struct CupidGBA* gba = machine(0);
    configure(gba, 0, 0xFFFE, 0xC0);
    tick(gba, 3);
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0xFFFF);
    cupid_io_write(gba, REG_TM0CNT_L, 0x1234);
    cupid_io_write(gba, REG_TM0CNT_H, 0);
    tick(gba, 1);
    CHECK(gba->irqf == 0);
    tick(gba, 1);
    CHECK(gba->irqf == IRQ_TIMER0);
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0xFFFE);
    tick(gba, 100);
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0xFFFE);
    cupid_io_write(gba, REG_TM0CNT_H, 0x80);
    tick(gba, 2);
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0x1234);
    destroy(gba);
}

static void cascade_chain_and_late_overflows(void) {
    struct CupidGBA* gba = machine(0);
    configure(gba, 0, 0xFFFE, 0xC0);
    for (int ch = 1; ch < 4; ch++) configure(gba, ch, 0xFFFE, 0xC4);
    tick(gba, 18); /* Eight timer-0 overflows; cascades receive 8, 4 and 2 ticks. */
    CHECK(gba->irq.pendingIF == (IRQ_TIMER0 | IRQ_TIMER1 | IRQ_TIMER2 | IRQ_TIMER3));
    for (int ch = 0; ch < 4; ch++) CHECK(cupid_io_read(gba, REG_TMCNT_LO(ch)) == 0xFFFE);
    tick(gba, 2);
    CHECK(gba->irqf == (IRQ_TIMER0 | IRQ_TIMER1 | IRQ_TIMER2 | IRQ_TIMER3));
    CHECK(cupid_io_read(gba, REG_TM1CNT_L) == 0xFFFF);
    CHECK(cupid_io_read(gba, REG_TM2CNT_L) == 0xFFFE);
    /* Disabling a cascade preserves its counter and blocks incoming ticks. */
    cupid_io_write(gba, REG_TM1CNT_H, 4);
    tick(gba, 20);
    CHECK(cupid_io_read(gba, REG_TM1CNT_L) == 0xFFFF);
    CHECK(cupid_io_read(gba, REG_TM2CNT_L) == 0xFFFE);
    destroy(gba);
}

static void clock_wrap_and_long_sessions(void) {
    const uint64_t origins[] = {0, INT32_MAX - 4u, UINT32_MAX - 4u,
                               UINT64_C(0x300000000) - 4};
    for (unsigned i = 0; i < sizeof(origins) / sizeof(*origins); i++) {
        struct CupidGBA* gba = machine(origins[i]);
        configure(gba, 0, 0xFFF0, 0xC0);
        configure(gba, 1, 0, 0x84);
        tick(gba, 1602);
        CHECK(cupid_io_read(gba, REG_TM0CNT_L) == 0xFFF0);
        CHECK(cupid_io_read(gba, REG_TM1CNT_L) == 100);
        CHECK(gba->timing.globalCycles == origins[i] + 1602);
        CHECK(gba->timers[0].event.when == (uint32_t)(origins[i] + 1618));
        destroy(gba);
    }
}

static void independent_channels_and_clock_source_changes(void) {
    struct CupidGBA* gba = machine(UINT32_MAX - 4u);
    for (int ch = 0; ch < 4; ch++) configure(gba, ch, 0xFFFC, 0xC0);
    tick(gba, 6);
    CHECK(gba->irq.pendingIF == (IRQ_TIMER0 | IRQ_TIMER1 | IRQ_TIMER2 | IRQ_TIMER3));
    for (int ch = 0; ch < 4; ch++) CHECK(cupid_io_read(gba, REG_TMCNT_LO(ch)) == 0xFFFC);
    tick(gba, 1);
    CHECK(gba->irqf == (IRQ_TIMER0 | IRQ_TIMER1 | IRQ_TIMER2 | IRQ_TIMER3));
    destroy(gba);

    gba = machine(0);
    configure(gba, 0, 0xFFFC, 0x80);
    configure(gba, 1, 0, 0x80);
    tick(gba, 10);
    CHECK(cupid_io_read(gba, REG_TM1CNT_L) == 8);
    cupid_io_write(gba, REG_TM1CNT_H, 0x84);
    tick(gba, 8);
    CHECK(cupid_io_read(gba, REG_TM1CNT_L) == 11);
    cupid_io_write(gba, REG_TM1CNT_H, 0x80);
    tick(gba, 3);
    CHECK(cupid_io_read(gba, REG_TM1CNT_L) == 13);
    destroy(gba);
}

static struct CupidGBA* fifo_machine(uint64_t clock) {
    struct CupidGBA* gba = machine(clock);
    gba->audio.enable = true;
    gba->audio.chALeft = true;
    gba->audio.volumeChA = true;
    gba->audio.chBTimer = true;
    gba->audio.lastSample = clock;
    for (unsigned i = 0; i < 7; i++) {
        uint32_t byte = 4 * i + 1;
        cupid_audio_fifo_write(&gba->audio.chA, byte | ((byte + 1) << 8) |
            ((byte + 2) << 16) | ((byte + 3) << 24));
    }
    cupid_timing_schedule(&gba->timing, &gba->audio.sampleEvent, 1024);
    configure(gba, 0, (uint16_t)(0x10000 - 130), 0x80);
    return gba;
}

static void fifo_late_dispatch_matches_each_cycle(void) {
    struct CupidGBA* punctual = fifo_machine(0);
    struct CupidGBA* late = fifo_machine(0);
    for (int i = 0; i < 3072; i++) tick(punctual, 1);
    tick(late, 3072);
    CHECK(punctual->audio.sampleWritePos == 12);
    CHECK(punctual->audio.sampleBuffer[0] == 3 * 192);
    CHECK(late->audio.sampleWritePos == punctual->audio.sampleWritePos);
    CHECK(!memcmp(late->audio.sampleBuffer, punctual->audio.sampleBuffer,
                  sizeof(punctual->audio.sampleBuffer)));
    CHECK(late->audio.chA.internalSample == punctual->audio.chA.internalSample);
    CHECK(late->audio.chA.internalRemaining == punctual->audio.chA.internalRemaining);
    CHECK(late->audio.chB.fifoRead == 0);
    destroy(punctual);
    destroy(late);
}

static void fifo_and_psg_clock_wrap(void) {
    const uint64_t origins[] = {INT32_MAX - 4u, UINT32_MAX - 4u, UINT64_C(0x300000000) - 4};
    struct CupidGBA* baseline = fifo_machine(0);
    tick(baseline, 3072);
    for (unsigned i = 0; i < sizeof(origins) / sizeof(*origins); i++) {
        struct CupidGBA* gba = fifo_machine(origins[i]);
        tick(gba, 3072);
        CHECK(!memcmp(gba->audio.sampleBuffer, baseline->audio.sampleBuffer,
                      sizeof(gba->audio.sampleBuffer)));
        CHECK(gba->audio.lastSample == origins[i] + 3072);
        destroy(gba);
    }
    destroy(baseline);

    const uint64_t clocks[] = {0, INT32_MAX - 16u, UINT32_MAX - 16u, UINT64_C(0x300000000)};
    struct CupidAudioPSG reference = {0};
    for (unsigned i = 0; i < sizeof(clocks) / sizeof(*clocks); i++) {
        struct CupidGBA* gba = machine(clocks[i]);
        gba->audio.lastSample = clocks[i];
        cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
        cupid_io_write(gba, REG_SOUND1CNT_H, 0xF080);
        cupid_io_write(gba, REG_SOUND1CNT_X, 0x87FF);
        cupid_io_write(gba, REG_SOUND2CNT_L, 0xF080);
        cupid_io_write(gba, REG_SOUND2CNT_H, 0x87FF);
        cupid_io_write(gba, REG_SOUND3CNT_L, 0x80);
        cupid_io_write(gba, REG_SOUND3CNT_H, 0x2000);
        cupid_io_write(gba, REG_SOUND3CNT_X, 0x87FF);
        cupid_io_write(gba, REG_SOUND4CNT_L, 0xF000);
        cupid_io_write(gba, REG_SOUND4CNT_H, 0x8008);
        cupid_audio_psg_run(&gba->audio.psg, clocks[i] + 600, 15);
        if (!i) reference = gba->audio.psg;
        CHECK(gba->audio.psg.ch1.index == reference.ch1.index);
        CHECK(gba->audio.psg.ch2.index == reference.ch2.index);
        CHECK(gba->audio.psg.ch1.lastUpdate - clocks[i] == reference.ch1.lastUpdate);
        CHECK(gba->audio.psg.ch3.nextUpdate - clocks[i] == reference.ch3.nextUpdate);
        CHECK(gba->audio.psg.ch4.lastEvent - clocks[i] == reference.ch4.lastEvent);
        CHECK(gba->audio.psg.ch4.lfsr == reference.ch4.lfsr);
        destroy(gba);
    }
}

static void fifo_dma_latency_cancellation_and_irq(void) {
    struct CupidGBA* gba = machine(0);
    gba->audio.enable = true;
    gba->audio.chBTimer = true;
    for (unsigned i = 0; i < 4; i++) {
        uint32_t value = UINT32_C(0x04030201) + i * UINT32_C(0x04040404);
        memcpy(gba->memory.iwram + i * 4, &value, 4);
    }
    cupid_dma_write_sad(gba, 1, GBA_BASE_IWRAM);
    cupid_dma_write_dad(gba, 1, GBA_BASE_IO + REG_FIFO_A_LO);
    cupid_dma_write_cnt_lo(gba, 1, 99);
    cupid_dma_write_cnt(gba, 1, 0xF200);
    cupid_io_write(gba, REG_IE, IRQ_TIMER0);
    cupid_io_write(gba, REG_IME, 1);
    configure(gba, 0, 0xFFFF, 0xC0);
    configure(gba, 1, 0, 0x84);
    tick(gba, 3);
    CHECK(gba->memory.dma[1].nextSource == GBA_BASE_IWRAM);
    CHECK(gba->audio.chA.latchedSample == 0);
    CHECK(gba->memory.dma[1].requestEvent.when == 6);
    tick(gba, 1);
    CHECK(gba->memory.dma[1].requestEvent.when == 6); /* Repeated requests keep the first deadline. */
    tick(gba, 1);
    CHECK(gba->memory.dma[1].nextSource == GBA_BASE_IWRAM);
    CHECK(gba->memory.dma[1].nextCount == 4 && !gba->memory.dmaRunning);
    CHECK(gba->audio.chA.latchedSample == 0);
    CHECK(gba->irqf == IRQ_TIMER0);
    CHECK(!gba->irq.line);
    CHECK(gba->irq.lineChanges[0].event.when == 6);
    CHECK(cupid_io_read(gba, REG_TM1CNT_L) == 3);
    CHECK(gba->cpu.cycles == 0);
    tick(gba, 1); /* Audio activation and the existing timer IRQ line at clock 6. */
    CHECK(gba->memory.dmaRunning && gba->memory.dma[1].ready && gba->irq.line);
    CHECK(gba->memory.dma[1].nextSource == GBA_BASE_IWRAM);
    CHECK(cupid_io_read(gba, REG_TM1CNT_L) == 4);
    tick(gba, 3); /* Handover, read, first FIFO write. */
    CHECK(gba->memory.dma[1].nextSource == GBA_BASE_IWRAM + 4);
    CHECK(gba->audio.chA.latchedSample == 1 && gba->irq.line);
    CHECK(cupid_io_read(gba, REG_TM1CNT_L) == 7);
    tick(gba, 6); /* Remaining three read/write pairs. */
    CHECK(gba->memory.dma[1].nextSource == GBA_BASE_IWRAM + 16);
    CHECK(gba->memory.dma[1].nextCount == 4 && gba->memory.dma[1].busyUntil == 15);
    CHECK(gba->irqf == IRQ_TIMER0 && gba->memory.dmaRunning);
    CHECK(cupid_io_read(gba, REG_TM1CNT_L) == 13);
    tick(gba, 1); /* Bus release and completion signal; IF is one clock later. */
    CHECK(gba->irq.line);
    CHECK(gba->irqf == IRQ_TIMER0);
    CHECK(gba->irq.pendingIF == (IRQ_TIMER0 | IRQ_DMA1));
    CHECK(cupid_io_read(gba, REG_TM1CNT_L) == 14);
    CHECK(gba->cpu.cycles == 0 && !gba->memory.dmaRunning);
    /* The timer at the final write can queue the next burst during release. */
    CHECK(gba->memory.dma[1].requestEvent.when == 18);
    tick(gba, 1);
    CHECK(gba->irqf == (IRQ_TIMER0 | IRQ_DMA1));
    CHECK(cupid_io_read(gba, REG_TM1CNT_L) == 15);
    destroy(gba);

    gba = machine(0);
    gba->audio.enable = true;
    cupid_dma_write_sad(gba, 2, GBA_BASE_IWRAM);
    cupid_dma_write_dad(gba, 2, GBA_BASE_IO + REG_FIFO_B_LO);
    cupid_dma_write_cnt(gba, 2, 0xB200);
    configure(gba, 0, 0xFFFF, 0x80);
    tick(gba, 3);
    CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->memory.dma[2].requestEvent));
    cupid_dma_write_cnt(gba, 2, 0);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->memory.dma[2].requestEvent));
    tick(gba, 2);
    CHECK(gba->memory.dma[2].nextSource == GBA_BASE_IWRAM && !gba->memory.dmaRunning);
    tick(gba, 2); /* Pass the cancelled three-clock activation deadline too. */
    CHECK(gba->memory.dma[2].nextSource == GBA_BASE_IWRAM);
    CHECK(gba->cpu.cycles == 0);
    destroy(gba);
}

static void fifo_sample_at_mixer_boundary_is_carried_forward(void) {
    struct CupidGBA* gba = machine(0);
    gba->audio.enable = true;
    gba->audio.chALeft = true;
    gba->audio.volumeChA = true;
    cupid_audio_fifo_write(&gba->audio.chA, UINT32_C(0x04030201));
    cupid_timing_schedule(&gba->timing, &gba->audio.sampleEvent, 1024);
    configure(gba, 0, 0xFC02, 0x80); /* First overflow at 1024. */
    tick(gba, 1024);
    CHECK(gba->audio.sampleBuffer[0] == 0 && gba->audio.sampleBuffer[2] == 0);
    CHECK(gba->audio.chA.latchedSample == 1);
    cupid_io_write(gba, REG_TM0CNT_H, 0);
    tick(gba, 1024);
    CHECK(gba->audio.sampleBuffer[4] == 192 && gba->audio.sampleBuffer[6] == 192);
    destroy(gba);
}

int main(void) {
    RUN(register_latency_and_frozen_counter);
    RUN(byte_latch_and_control_masks);
    RUN(global_prescaler_phase_and_live_changes);
    RUN(coincident_reload_and_stop);
    RUN(cascade_chain_and_late_overflows);
    RUN(clock_wrap_and_long_sessions);
    RUN(independent_channels_and_clock_source_changes);
    RUN(fifo_late_dispatch_matches_each_cycle);
    RUN(fifo_and_psg_clock_wrap);
    RUN(fifo_dma_latency_cancellation_and_irq);
    RUN(fifo_sample_at_mixer_boundary_is_carried_forward);
    return 0;
}
