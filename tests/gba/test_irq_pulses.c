/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static struct CupidGBA* machine_at(uint64_t epoch, bool thumb) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    if (thumb) {
        const uint16_t code[] = {0x235A, 0x46C0, 0x46C0};
        memcpy(gba->memory.iwram, code, sizeof(code));
        CupidThumbWritePC(&gba->cpu);
    } else {
        const uint32_t code[] = {0xE3A0305A, 0xE1A00000, 0xE1A00000};
        memcpy(gba->memory.iwram, code, sizeof(code));
        CupidARMWritePC(&gba->cpu);
    }
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = epoch;
    gba->timing.masterCycles = (uint32_t)epoch;
    cupid_irq_init(gba);
    return gba;
}

static struct CupidGBA* machine(void) { return machine_at(0, false); }

static void acknowledge(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing;
    (void)late;
    cupid_irq_write(context, REG_IF, IRQ_TIMER0);
}

static void pulse(struct CupidGBA* gba, struct CupidTimingEvent* event) {
    *event = (struct CupidTimingEvent){.context = gba, .callback = acknowledge, .priority = 0};
    cupid_timing_schedule(&gba->timing, event, 1);
    cupid_irq_write(gba, REG_IE, IRQ_TIMER0);
    const uint64_t now = cupid_timing_global_time(&gba->timing);
    cupid_raise_irq_at(gba, IRQ_TIMER0, now);
    /* VBlank's source phase can queue acceptance one clock ahead. The first
     * register write must still publish its own later availability value. */
    cupid_raise_irq_at(gba, IRQ_VBLANK, now + 1);
}

static void an_accepted_register_value_keeps_its_availability_deadline(void) {
    struct CupidGBA* gba = machine();
    struct CupidTimingEvent event;
    pulse(gba, &event);
    cupid_timing_tick(&gba->timing, 1);
    CHECK(gba->irqf == IRQ_TIMER0 && !gba->irq.available);
    cupid_timing_tick(&gba->timing, 1);
    CHECK(gba->irqf == IRQ_VBLANK && gba->irq.available);
    cupid_timing_tick(&gba->timing, 1);
    CHECK(!gba->irq.available);
    cupid_gba_destroy(gba);
    free(gba);
}

static void halt_can_observe_the_one_clock_availability_pulse(void) {
    struct CupidGBA* gba = machine();
    struct CupidTimingEvent event;
    pulse(gba, &event);
    gba->cpu.halted = 1;
    cupid_timing_tick(&gba->timing, 2);
    cupid_gba_step(gba);
    CHECK(!gba->cpu.halted && gba->cpu.gprs[3] == 0x5A);
    CHECK(!gba->ime && !gba->irq.line);
    cupid_gba_destroy(gba);
    free(gba);
}

struct Observation {
    struct CupidGBA* gba;
    unsigned count;
    uint64_t clocks[4];
    uint16_t flags[4];
    bool available[4], line[4];
    struct CupidTimingEvent event;
};

static void observe(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct Observation* observation = context;
    CHECK(observation->count < 4);
    const unsigned index = observation->count++;
    observation->clocks[index] = cupid_timing_global_time(timing);
    observation->flags[index] = observation->gba->irqf;
    observation->available[index] = observation->gba->irq.available;
    observation->line[index] = observation->gba->irq.line;
    if (observation->count < 4) cupid_timing_schedule(timing, &observation->event, 1);
}

static void all_deadlines_survive_partitioning_ring_wrap_and_clock_wrap(void) {
    const uint64_t epochs[] = {0, INT32_MAX - 2u, UINT32_MAX - 2u, UINT64_C(0x200000100)};
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned ime = 0; ime < 2; ime++)
        for (unsigned single = 0; single < 2; single++) {
            struct CupidGBA* gba = machine_at(epochs[epoch], false);
            for (unsigned repetition = 0; repetition < 32; repetition++) {
                cupid_irq_write(gba, REG_IF, IRQ_VBLANK);
                cupid_timing_tick(&gba->timing, 1);
                CHECK(!gba->irqf && !gba->irq.available && !gba->irq.line);
                const uint64_t start = cupid_timing_global_time(&gba->timing);
                cupid_irq_write(gba, REG_IME, (uint16_t)ime);
                struct CupidTimingEvent acknowledgement;
                pulse(gba, &acknowledgement);
                struct Observation observation = {.gba = gba};
                observation.event = (struct CupidTimingEvent){.context = &observation,
                    .callback = observe, .priority = 8};
                cupid_timing_schedule(&gba->timing, &observation.event, 2);
                if (single) for (unsigned clock = 0; clock < 5; clock++) cupid_timing_tick(&gba->timing, 1);
                else cupid_timing_tick(&gba->timing, 5);
                CHECK(observation.count == 4);
                for (unsigned index = 0; index < 4; index++) {
                    CHECK(observation.clocks[index] == start + 2 + index);
                    CHECK(observation.flags[index] == IRQ_VBLANK);
                    CHECK(observation.available[index] == (index == 0));
                    CHECK(observation.line[index] == (ime && index == 1));
                }
                CHECK(gba->irq.pendingIF == IRQ_VBLANK && gba->ie == IRQ_TIMER0);
            }
            cupid_gba_destroy(gba);
            free(gba);
        }
}

static void both_execution_modes_resume_with_ime_and_cpu_mask_independent(void) {
    const uint64_t epochs[] = {0, UINT32_MAX - 1u};
    for (unsigned epoch = 0; epoch < 2; epoch++) for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned ime = 0; ime < 2; ime++) for (unsigned mask = 0; mask < 2; mask++) {
            struct CupidGBA* gba = machine_at(epochs[epoch], thumb != 0);
            gba->cpu.cpsr.i = mask;
            gba->cpu.irqDisableLatch = mask != 0;
            cupid_irq_write(gba, REG_IME, (uint16_t)ime);
            struct CupidTimingEvent acknowledgement;
            pulse(gba, &acknowledgement);
            cupid_gba_halt(gba);
            cupid_timing_tick(&gba->timing, 2);
            CHECK(gba->irq.available && !gba->irq.line);
            cupid_gba_step(gba);
            CHECK(!gba->cpu.halted && gba->cpu.gprs[3] == 0x5A);
            CHECK(gba->cpu.privilegeMode != MODE_IRQ);
            CHECK(!gba->irq.available);
            cupid_gba_destroy(gba);
            free(gba);
        }
}

static void a_real_dma_acknowledgement_keeps_the_queued_halt_wake(void) {
    const uint64_t epochs[] = {0, UINT32_MAX - 5u};
    for (unsigned epoch = 0; epoch < 2; epoch++) for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine_at(epochs[epoch], thumb != 0);
        const uint16_t acknowledgement = IRQ_TIMER0;
        memcpy(gba->memory.iwram + 0x2000, &acknowledgement, sizeof(acknowledgement));
        cupid_irq_write(gba, REG_IE, IRQ_TIMER0);
        cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x2000);
        cupid_dma_write_dad(gba, 3, GBA_BASE_IO + REG_IF);
        cupid_dma_write_cnt_lo(gba, 3, 1);
        cupid_dma_write_cnt(gba, 3, 0x8000);
        cupid_timing_tick(&gba->timing, 4);
        CHECK(gba->memory.dmaRunning && gba->memory.dmaBus ==
            (IRQ_TIMER0 | (uint32_t)IRQ_TIMER0 << 16));
        CHECK(cupid_timing_until(&gba->timing, &gba->memory.dmaEvent) == 1);
        const uint64_t now = cupid_timing_global_time(&gba->timing);
        cupid_raise_irq_at(gba, IRQ_TIMER0, now);
        cupid_raise_irq_at(gba, IRQ_VBLANK, now + 1);
        cupid_gba_halt(gba);
        cupid_timing_tick(&gba->timing, 2);
        CHECK(gba->irqf == IRQ_VBLANK && gba->irq.available);
        CHECK(!gba->memory.dmaRunning && !gba->cpuBlocked);
        cupid_gba_step(gba);
        CHECK(!gba->cpu.halted && gba->cpu.gprs[3] == 0x5A);
        cupid_gba_destroy(gba);
        free(gba);
    }
}

static void stop_freezes_and_console_reset_cancels_queued_publications(void) {
    struct CupidGBA* gba = machine();
    struct CupidTimingEvent acknowledgement;
    pulse(gba, &acknowledgement);
    gba->stopped = true;
    cupid_gba_step(gba);
    CHECK(cupid_timing_global_time(&gba->timing) == 0 && !gba->irq.available);
    gba->stopped = false;
    cupid_timing_tick(&gba->timing, 2);
    CHECK(gba->irq.available);
    cupid_gba_reset(gba);
    cupid_timing_tick(&gba->timing, 8);
    CHECK(!gba->irqf && !gba->irq.available && !gba->irq.line && !gba->irq.pendingIF);
    cupid_gba_destroy(gba);
    free(gba);
}

int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "pulse")) {
        RUN(an_accepted_register_value_keeps_its_availability_deadline);
    } else if (argc == 2 && !strcmp(argv[1], "halt")) {
        RUN(halt_can_observe_the_one_clock_availability_pulse);
    } else if (argc == 2 && !strcmp(argv[1], "dma")) {
        RUN(a_real_dma_acknowledgement_keeps_the_queued_halt_wake);
    } else if (argc == 1) {
        RUN(an_accepted_register_value_keeps_its_availability_deadline);
        RUN(halt_can_observe_the_one_clock_availability_pulse);
        RUN(all_deadlines_survive_partitioning_ring_wrap_and_clock_wrap);
        RUN(both_execution_modes_resume_with_ime_and_cpu_mask_independent);
        RUN(a_real_dma_acknowledgement_keeps_the_queued_halt_wake);
        RUN(stop_freezes_and_console_reset_cancels_queued_publications);
    } else return 2;
    return 0;
}
