/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include "caller.h"

static const char* externalBios;

static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (externalBios) CHECK(cupid_load_bios(gba, externalBios));
    cupid_gba_reset(gba);
    cupid_test_bios_caller(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void prepare(struct CupidGBA* gba, unsigned service, uint32_t control, bool thumb,
                    enum CupidPrivMode mode, bool masked, enum CupidIRQ wake) {
    const uint32_t wide[] = {UINT32_C(0xEF000000) | service << 16, 0xEAFFFFFE};
    const uint16_t narrow[] = {(uint16_t)(0xDF00 | service), 0xE7FE};
    memcpy(gba->memory.iwram, thumb ? (const void*)narrow : (const void*)wide,
        thumb ? sizeof(narrow) : sizeof(wide));
    cupid_arm_set_priv_mode(&gba->cpu, mode);
    for (unsigned r = 0; r < 15; r++) gba->cpu.gprs[r] = (int32_t)(0x11220000 + r);
    gba->cpu.gprs[2] = (int32_t)control;
    gba->cpu.gprs[ARM_SP] = mode == MODE_IRQ ? GBA_SP_BASE_IRQ : GBA_SP_BASE_SYSTEM;
    gba->cpu.cpsr.packed = 0xA0000000u | mode | (masked ? 0x80u : 0u);
    gba->cpu.irqDisableLatch = masked;
    gba->cpu.spsr.packed = 0x6000003F;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb) CupidThumbWritePC(&gba->cpu);
    else CupidARMWritePC(&gba->cpu);
    cupid_io_write(gba, REG_TM0CNT_L, 0);
    cupid_io_write(gba, REG_TM0CNT_H, 0x80);
    cupid_timing_tick(&gba->timing, 2);
    cupid_io_write(gba, REG_IE, wake);
    cupid_io_write(gba, REG_IME, 0);
}

static uint64_t sleep(struct CupidGBA* gba) {
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    unsigned steps;
    for (steps = 0; steps < 2000 && !gba->cpu.halted && !gba->stopped; steps++) cupid_gba_step(gba);
    CHECK(steps < 2000 && gba->cpu.halted);
    CHECK((uint32_t)gba->cpu.gprs[ARM_PC] < GBA_SIZE_BIOS);
    CHECK(gba->cpu.privilegeMode == MODE_SYS && gba->cpu.executionMode == MODE_ARM);
    return cupid_timing_global_time(&gba->timing) - start;
}

static uint64_t return_to_caller(struct CupidGBA* gba, bool thumb) {
    const uint64_t start = cupid_timing_global_time(&gba->timing);
    unsigned steps;
    for (steps = 0; steps < 2000; steps++) {
        cupid_gba_step(gba);
        if (!gba->cpu.halted && !gba->stopped && gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
            (uint32_t)gba->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + (thumb ? 4u : 8u)) break;
    }
    CHECK(steps < 2000);
    return cupid_timing_global_time(&gba->timing) - start;
}

static uint64_t resume(struct CupidGBA* gba, bool thumb, enum CupidIRQ wake) {
    cupid_raise_irq_at(gba, wake, cupid_timing_global_time(&gba->timing));
    return return_to_caller(gba, thumb);
}

static void caller_state(struct CupidGBA* gba, uint32_t control, bool thumb, enum CupidPrivMode mode, bool masked) {
    CHECK(gba->cpu.privilegeMode == mode);
    CHECK((uint32_t)gba->cpu.cpsr.packed == (0xA0000000u | mode | (thumb ? 32u : 0u) | (masked ? 128u : 0u)));
    for (unsigned r = 0; r < 15; r++) {
        const uint32_t expected = r == 2 ? control : r == ARM_SP
            ? (mode == MODE_IRQ ? GBA_SP_BASE_IRQ : GBA_SP_BASE_SYSTEM) : 0x11220000u + r;
        CHECK((uint32_t)gba->cpu.gprs[r] == expected);
    }
    if (mode == MODE_IRQ) CHECK((uint32_t)gba->cpu.spsr.packed == 0x6000003F);
    CHECK(gba->memory.biosPrefetch == 0xE3A02004);
}

static void complete_control_bytes_and_upper_aliases(unsigned batch) {
    CHECK(batch < 4);
    const uint32_t aliases[] = {0x100, 0xFFFFFF80, 0x80000000, 0x1234007F};
    const unsigned first = batch * 64, last = batch == 3 ? 260 : first + 64;
    unsigned count = 0;
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        for (unsigned service = 0; service < 3; service++) {
            for (unsigned i = first; i < last; i++) {
                const uint32_t control = i < 256 ? i : aliases[i - 256];
                const unsigned call = service == 0 ? 2 : service == 1 ? 3 : 0x27;
                const bool stop = service == 1 || (service == 2 && (control & 0x80));
                const unsigned clocks = service == 0 ? 38 : service == 1 ? 34 : stop ? 33 : 34;
                struct CupidGBA* gba = machine(0);
                prepare(gba, call, control, thumb != 0, MODE_SYS, true, IRQ_KEYPAD);
                CHECK(sleep(gba) == clocks);
                CHECK(gba->stopped == stop);
                CHECK(cupid_timer_read_counter(gba, 0) == clocks);
                CHECK(gba->cpu.gprs[11] == 0x9F && gba->cpu.gprs[12] == GBA_BASE_IO);
                CHECK((uint32_t)gba->cpu.gprs[2] == (service == 0 ? 0u : service == 1 ? 128u : control));
                CHECK(resume(gba, thumb != 0, IRQ_KEYPAD) == 24);
                caller_state(gba, control, thumb != 0, MODE_SYS, true);
                CHECK(cupid_timer_read_counter(gba, 0) == clocks + 24);
                destroy(gba);
                count++;
            }
        }
    }
    CHECK(count == 6 * (last - first));
    printf("PASS complete controls: %u cases\n", count);
}

static void caller_masks_banks_and_clock_wrap(void) {
    const uint64_t clocks[] = {0, UINT64_C(0xFFFFFFF0), UINT64_C(0x1FFFFFFF0)};
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        for (unsigned configuration = 0; configuration < 3; configuration++) {
            const enum CupidPrivMode mode = configuration == 2 ? MODE_IRQ : MODE_SYS;
            const bool masked = configuration != 0;
            for (unsigned service = 0; service < 4; service++) {
                for (unsigned c = 0; c < 3; c++) {
                    struct CupidGBA* gba = machine(clocks[c]);
                    const unsigned call = service == 0 ? 2 : service == 1 ? 3 : 0x27;
                    const uint32_t control = service == 3 ? 0xFFFFFF80 : 0x1234007F;
                    const unsigned entry = service == 0 ? 38 : service == 1 ? 34 : service == 2 ? 34 : 33;
                    prepare(gba, call, control, thumb != 0, mode, masked, IRQ_GAMEPAK);
                    CHECK(sleep(gba) == entry);
                    CHECK(resume(gba, thumb != 0, IRQ_GAMEPAK) == 24);
                    caller_state(gba, control, thumb != 0, mode, masked);
                    CHECK(cupid_timing_global_time(&gba->timing) == clocks[c] + 2 + entry + 24);
                    destroy(gba);
                }
            }
        }
    }
}

static uint32_t word(const uint8_t* bytes) {
    uint32_t value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static void install_handler(struct CupidGBA* gba) {
    /* Acknowledge IF and record accepted requests without changing caller state. */
    const uint32_t handler[] = {
        0xE3A00301, 0xE2800C02, 0xE1D010B2, 0xE1C010B2,
        0xE59F2018, 0xE5923000, 0xE2833001, 0xE5823000,
        0xE5923004, 0xE1833001, 0xE5823004, 0xE12FFF1E,
        GBA_BASE_EWRAM + 0x2B00
    };
    const uint32_t entry = GBA_BASE_IWRAM + 0x7000;
    memcpy(gba->memory.iwram + 0x7000, handler, sizeof(handler));
    memcpy(gba->memory.iwram + 0x7FFC, &entry, sizeof(entry));
}

struct PowerProgress {
    uint64_t entry, returned;
    uint32_t registers[16], status, savedStatus, protectedWord, interrupts, requests;
    uint32_t lateness;
    uint16_t timer, pending;
};

static struct PowerProgress progress(struct CupidGBA* gba, uint64_t entry, uint64_t returned, uint32_t late) {
    struct PowerProgress result = {0};
    result.entry = entry;
    result.returned = returned;
    memcpy(result.registers, gba->cpu.gprs, sizeof(result.registers));
    result.status = gba->cpu.cpsr.packed;
    result.savedStatus = gba->cpu.spsr.packed;
    result.protectedWord = gba->memory.biosPrefetch;
    result.interrupts = word(gba->memory.ewram + 0x2B00);
    result.requests = word(gba->memory.ewram + 0x2B04);
    result.lateness = late;
    result.timer = cupid_timer_read_counter(gba, 0);
    result.pending = gba->irqf;
    return result;
}

static void same_progress(const struct PowerProgress* result, const struct PowerProgress* expected) {
    CHECK(result->entry == expected->entry && result->returned == expected->returned);
    CHECK(!memcmp(result->registers, expected->registers, sizeof(result->registers)));
    CHECK(result->status == expected->status && result->savedStatus == expected->savedStatus);
    CHECK(result->protectedWord == expected->protectedWord);
    CHECK(result->interrupts == expected->interrupts && result->requests == expected->requests);
    CHECK(result->lateness == expected->lateness);
    CHECK(result->timer == expected->timer && result->pending == expected->pending);
}

static struct PowerProgress readyProgress[2][4][2][2][2];

static void ready_requests_and_irq_delivery(void) {
    if (externalBios) {
        const char* requested = externalBios;
        externalBios = NULL;
        ready_requests_and_irq_delivery();
        externalBios = requested;
    }
    for (unsigned thumb = 0; thumb < 2; thumb++) {
      for (unsigned service = 0; service < 4; service++) {
        for (unsigned ime = 0; ime < 2; ime++) {
          for (unsigned masked = 0; masked < 2; masked++) {
            for (unsigned ready = 0; ready < 2; ready++) {
                const bool deliveredBeforeSleep = ready && ime && !masked;
                struct CupidGBA* gba = machine(0);
                const unsigned call = service == 0 ? 2 : service == 1 ? 3 : 0x27;
                const uint32_t control = service == 3 ? 0x80 : 0;
                prepare(gba, call, control, thumb != 0, MODE_SYS, masked != 0, IRQ_KEYPAD);
                install_handler(gba);
                cupid_io_write(gba, REG_IME, ime);
                if (ready) {
                    cupid_raise_irq(gba, IRQ_KEYPAD, 0);
                    cupid_timing_tick(&gba->timing, 3);
                    CHECK(gba->irq.available);
                }
                const uint64_t entry = sleep(gba);
                const unsigned ordinaryEntry = service == 0 ? 38u : service == 3 ? 33u : 34u;
                if (deliveredBeforeSleep) {
                    CHECK(entry > ordinaryEntry);
                    CHECK(word(gba->memory.ewram + 0x2B00) == 1);
                    CHECK(word(gba->memory.ewram + 0x2B04) == IRQ_KEYPAD);
                } else CHECK(entry == ordinaryEntry);
                CHECK(gba->stopped == ((service == 1 || service == 3) && (!ready || deliveredBeforeSleep)));
                const uint64_t returned = ready && !deliveredBeforeSleep ? return_to_caller(gba, thumb != 0)
                    : resume(gba, thumb != 0, IRQ_KEYPAD);
                caller_state(gba, control, thumb != 0, MODE_SYS, masked != 0);
                CHECK(word(gba->memory.ewram + 0x2B00) == (ime && !masked ? deliveredBeforeSleep ? 2u : 1u : 0u));
                CHECK(word(gba->memory.ewram + 0x2B04) == (ime && !masked ? IRQ_KEYPAD : 0));
                const struct PowerProgress result = progress(gba, entry, returned, 0);
                if (externalBios) same_progress(&result, &readyProgress[thumb][service][ime][masked][ready]);
                else readyProgress[thumb][service][ime][masked][ready] = result;
                destroy(gba);
            }
          }
        }
      }
    }
}

static void stopped_peripherals_and_external_wake_filters(void) {
    const enum CupidIRQ wakes[] = {IRQ_KEYPAD, IRQ_GAMEPAK, IRQ_SIO};
    for (unsigned thumb = 0; thumb < 2; thumb++) {
      for (unsigned custom = 0; custom < 2; custom++) {
        for (unsigned w = 0; w < 3; w++) {
            struct CupidGBA* gba = machine(0);
            cupid_gba_reset(gba); /* Retain actual video and audio queues. */
            cupid_test_bios_caller(gba);
            prepare(gba, custom ? 0x27 : 3, 0x80, thumb != 0, MODE_SYS, true,
                (enum CupidIRQ)(wakes[w] | IRQ_TIMER0));
            if (w == 2) cupid_io_write(gba, REG_RCNT, 0x8100);
            CHECK(sleep(gba) == (custom ? 33u : 34u));
            CHECK(gba->stopped);
            const uint64_t clock = cupid_timing_global_time(&gba->timing);
            const uint16_t timer = cupid_timer_read_counter(gba, 0);
            const uint16_t line = cupid_io_read(gba, REG_VCOUNT);
            const int samples = gba->audio.sampleWritePos;
            const uint32_t videoWhen = gba->video.event.when, audioWhen = gba->audio.sampleEvent.when;
            const int32_t nextEvent = gba->cpu.nextEvent;
            for (unsigned i = 0; i < 20; i++) {
                cupid_gba_step(gba);
                cupid_gba_run_frame(gba);
            }
            CHECK(gba->stopped && gba->cpu.halted);
            CHECK(cupid_timing_global_time(&gba->timing) == clock);
            CHECK(cupid_timer_read_counter(gba, 0) == timer);
            CHECK(cupid_io_read(gba, REG_VCOUNT) == line && gba->audio.sampleWritePos == samples);
            CHECK(gba->video.event.when == videoWhen && gba->audio.sampleEvent.when == audioWhen);
            CHECK(gba->cpu.nextEvent == nextEvent);
            cupid_raise_irq(gba, IRQ_TIMER0, 0);
            CHECK(gba->stopped); /* Enabled internal requests leave STOP frozen. */
            const enum CupidIRQ disabled = w == 0 ? IRQ_GAMEPAK : IRQ_KEYPAD;
            cupid_raise_irq(gba, disabled, 0);
            CHECK(gba->stopped); /* Disabled external sources cannot wake it. */
            if (w == 2) {
                cupid_io_write(gba, REG_RCNT, 0);
                cupid_raise_irq(gba, IRQ_SIO, 0);
                CHECK(gba->stopped); /* Normal serial mode cannot wake STOP. */
                cupid_io_write(gba, REG_RCNT, 0x8100);
                cupid_serial_set_input(&gba->serial, SERIAL_SI, 0);
                CHECK(!gba->stopped);
            } else cupid_raise_irq(gba, wakes[w], 0);
            CHECK(!gba->stopped && cupid_timing_global_time(&gba->timing) == clock);
            CHECK(return_to_caller(gba, thumb != 0) == 24);
            caller_state(gba, 0x80, thumb != 0, MODE_SYS, true);
            destroy(gba);
        }
      }
    }
}

struct PowerDMA { struct CupidGBA* gba; bool dispatched; uint64_t when; uint32_t late; };

static void request_dma(struct CupidTiming* timing, void* context, uint32_t late) {
    struct PowerDMA* trace = context;
    struct CupidGBA* gba = trace->gba;
    /* A request in the SWI's first bus clock may precede the exception PC
     * update; a request while halted must still observe the BIOS body. */
    if (gba->cpu.halted) CHECK((uint32_t)gba->cpu.gprs[ARM_PC] < GBA_SIZE_BIOS);
    trace->dispatched = true;
    trace->when = cupid_timing_global_time(timing);
    trace->late = late;
    CHECK(timing->globalCycles == trace->when + late);
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x1000);
    cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 4);
    cupid_dma_write_cnt_lo(gba, 3, 1);
    cupid_dma_write_cnt(gba, 3, 0xC400);
}

static struct PowerProgress dmaProgress[2][2][3];

static void dma_and_irq_during_entry_and_halt(void) {
    if (externalBios) {
        const char* requested = externalBios;
        externalBios = NULL;
        dma_and_irq_during_entry_and_halt();
        externalBios = requested;
    }
    const uint64_t clocks[] = {0, UINT64_C(0xFFFFFFF0), UINT64_C(0x1FFFFFFF0)};
    for (unsigned thumb = 0; thumb < 2; thumb++) {
      for (unsigned phase = 0; phase < 2; phase++) {
        for (unsigned c = 0; c < 3; c++) {
            struct CupidGBA* gba = machine(clocks[c]);
            prepare(gba, 2, 0x1234007F, thumb != 0, MODE_SYS, false, IRQ_TIMER0 | IRQ_DMA3);
            install_handler(gba);
            cupid_io_write(gba, REG_TM0CNT_L, 0xFF00);
            cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
            cupid_io_write(gba, REG_IME, 1);
            const uint32_t replacement = 0x2468ACE0;
            memcpy(gba->memory.iwram + 0x1000, &replacement, sizeof(replacement));
            struct PowerDMA trace = {.gba = gba};
            struct CupidTimingEvent event = {.context = &trace, .callback = request_dma, .priority = 0x20};
            /* Cover an overdue request at entry and a future request in HALT. */
            const int32_t delay = phase ? 60 : -1;
            cupid_timing_schedule(&gba->timing, &event, delay);
            const uint64_t entry = sleep(gba);
            const uint64_t returned = return_to_caller(gba, thumb != 0);
            CHECK(trace.dispatched && trace.when == clocks[c] + (phase ? 62u : 1u));
            if (!phase) CHECK(trace.late > 0);
            CHECK(word(gba->memory.ewram + 4) == replacement);
            CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable);
            CHECK(word(gba->memory.ewram + 0x2B00) >= 1);
            CHECK(word(gba->memory.ewram + 0x2B04) & IRQ_DMA3);
            caller_state(gba, 0x1234007F, thumb != 0, MODE_SYS, false);
            const struct PowerProgress result = progress(gba, entry, returned, trace.late);
            if (externalBios) same_progress(&result, &dmaProgress[thumb][phase][c]);
            else dmaProgress[thumb][phase][c] = result;
            destroy(gba);
        }
      }
    }
}

static void halted_video_audio_timer_and_dma_continue(void) {
    for (unsigned thumb = 0; thumb < 2; thumb++) {
      for (unsigned custom = 0; custom < 2; custom++) {
        struct CupidGBA* gba = machine(0);
        cupid_gba_reset(gba);
        cupid_test_bios_caller(gba);
        prepare(gba, custom ? 0x27 : 2, 0, thumb != 0, MODE_SYS, true, IRQ_TIMER0);
        cupid_io_write(gba, REG_TM0CNT_L, 0xF000);
        cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
        const uint32_t marker = 0x13579BDF;
        memcpy(gba->memory.iwram + 0x1000, &marker, sizeof(marker));
        cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x1000);
        cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 4);
        cupid_dma_write_cnt_lo(gba, 3, 1);
        cupid_dma_write_cnt(gba, 3, 0xA400); /* HBlank, no DMA IRQ. */
        CHECK(sleep(gba) == (custom ? 34u : 38u));
        CHECK(!gba->stopped && gba->cpu.halted);
        CHECK(word(gba->memory.ewram + 4) == 0);
        const int samples = gba->audio.sampleWritePos;
        CHECK(return_to_caller(gba, thumb != 0) > 4000);
        CHECK(cupid_io_read(gba, REG_VCOUNT) >= 3);
        CHECK(gba->audio.sampleWritePos > samples);
        CHECK(word(gba->memory.ewram + 4) == marker && !gba->memory.dma[3].enable);
        CHECK(gba->irqf == IRQ_TIMER0);
        caller_state(gba, 0, thumb != 0, MODE_SYS, true);
        destroy(gba);
      }
    }
}

int main(int argc, char** argv) {
    int batch = -1;
    bool state = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--external-bios") && i + 1 < argc) externalBios = argv[++i];
        else if (!strcmp(argv[i], "--controls") && i + 1 < argc) {
            const char* value = argv[++i];
            if (strlen(value) != 1 || *value < '0' || *value > '3') return 2;
            batch = *value - '0';
        } else if (!strcmp(argv[i], "--state")) state = true;
        else return 2;
    }
    if (batch >= 0) {
        if (state) return 2;
        complete_control_bytes_and_upper_aliases((unsigned)batch);
        return 0;
    }
    if (!state) {
        for (unsigned group = 0; group < 4; group++) complete_control_bytes_and_upper_aliases(group);
    }
    RUN(caller_masks_banks_and_clock_wrap);
    RUN(ready_requests_and_irq_delivery);
    RUN(stopped_peripherals_and_external_wake_filters);
    RUN(dma_and_irq_during_entry_and_halt);
    RUN(halted_video_audio_timer_and_dma_continue);
    return 0;
}
