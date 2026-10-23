/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
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

static void arm_program(struct CupidGBA* gba, const uint32_t* code, size_t size) {
    memcpy(gba->memory.iwram, code, size);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    CupidARMWritePC(&gba->cpu);
    const uint32_t handler = 0xE3A03063; /* mov r3, #99 */
    memcpy(gba->memory.bios + BASE_IRQ, &handler, sizeof(handler));
}

static void register_visibility_and_signal_stages(void) {
    struct CupidGBA* gba = machine(0);
    cupid_io_write(gba, REG_IE, IRQ_TIMER0);
    cupid_io_write(gba, REG_IME, 1);
    cupid_raise_irq(gba, IRQ_TIMER0, 0);
    CHECK(cupid_io_read(gba, REG_IE) == 0);
    CHECK(cupid_io_read(gba, REG_IF) == 0);
    CHECK(cupid_io_read(gba, REG_IME) == 0);
    tick(gba, 1);
    CHECK(cupid_io_read(gba, REG_IE) == IRQ_TIMER0);
    CHECK(cupid_io_read(gba, REG_IF) == IRQ_TIMER0);
    CHECK(cupid_io_read(gba, REG_IME) == 1);
    CHECK(!gba->irq.available && !gba->irq.line);
    tick(gba, 1);
    CHECK(gba->irq.available && !gba->irq.line);
    tick(gba, 1);
    CHECK(gba->irq.line);
    cupid_io_write(gba, REG_IF, IRQ_TIMER0);
    CHECK(cupid_io_read(gba, REG_IF) == IRQ_TIMER0);
    tick(gba, 1);
    CHECK(cupid_io_read(gba, REG_IF) == 0);
    CHECK(gba->irq.available && gba->irq.line);
    tick(gba, 1);
    CHECK(!gba->irq.available && gba->irq.line);
    tick(gba, 1);
    CHECK(!gba->irq.line);
    destroy(gba);
}

static void byte_masks_and_acknowledgements(void) {
    struct CupidGBA* gba = machine(0);
    cupid_io_write8(gba, REG_IE, 0xA5);
    cupid_io_write8(gba, REG_IE + 1, 0xFF);
    cupid_io_write8(gba, REG_IME, 0xFE);
    cupid_io_write8(gba, REG_IME + 1, 1);
    cupid_raise_irq(gba, IRQ_TIMER0, 0);
    cupid_raise_irq(gba, IRQ_DMA3, 0);
    tick(gba, 1);
    CHECK(cupid_io_read(gba, REG_IE) == 0x3FA5);
    CHECK(cupid_io_read(gba, REG_IME) == 0);
    CHECK(cupid_io_read(gba, REG_IF) == (IRQ_TIMER0 | IRQ_DMA3));
    cupid_io_write8(gba, REG_IF + 1, IRQ_DMA3 >> 8);
    tick(gba, 1);
    CHECK(cupid_io_read(gba, REG_IF) == IRQ_TIMER0);
    cupid_io_write8(gba, REG_IF, 0);
    tick(gba, 1);
    CHECK(cupid_io_read(gba, REG_IF) == IRQ_TIMER0);
    cupid_io_write8(gba, REG_IF, IRQ_TIMER0);
    tick(gba, 1);
    CHECK(cupid_io_read(gba, REG_IF) == 0);
    destroy(gba);
}

static void consecutive_line_changes_keep_their_deadlines(void) {
    struct CupidGBA* gba = machine(0);
    cupid_io_write(gba, REG_IE, IRQ_TIMER0);
    cupid_io_write(gba, REG_IME, 1);
    cupid_raise_irq(gba, IRQ_TIMER0, 0);
    tick(gba, 1);
    cupid_io_write(gba, REG_IME, 0);
    tick(gba, 1);
    CHECK(gba->irq.available && !gba->irq.line);
    cupid_io_write(gba, REG_IME, 1);
    tick(gba, 1);
    CHECK(gba->irq.line);
    tick(gba, 1);
    CHECK(!gba->irq.line);
    tick(gba, 1);
    CHECK(gba->irq.line);
    destroy(gba);
}

static void halt_wakes_with_ime_clear_and_cpsr_masked(void) {
    struct CupidGBA* gba = machine(0);
    const uint32_t code[] = {0xE1A00000, 0xE1A00000, 0xE1A00000};
    arm_program(gba, code, sizeof(code));
    cupid_io_write(gba, REG_IE, IRQ_KEYPAD);
    cupid_raise_irq(gba, IRQ_KEYPAD, 0);
    cupid_gba_halt(gba);
    cupid_gba_step(gba); /* Process the HALT breakpoint at the current clock. */
    CHECK(cupid_timing_global_time(&gba->timing) == 0);
    cupid_gba_step(gba);
    CHECK(gba->cpu.halted && !gba->irq.available);
    cupid_gba_step(gba);
    CHECK(gba->cpu.halted && gba->irq.available);
    cupid_gba_step(gba);
    CHECK(!gba->cpu.halted && !gba->irq.line);
    CHECK(gba->cpu.privilegeMode == MODE_SYS && gba->cpu.cpsr.i);
    CHECK(gba->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + 8);
    CHECK(cupid_timing_global_time(&gba->timing) == 4);
    destroy(gba);
}

static void halt_samples_the_irq_line_before_its_wake_clock(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - UINT64_C(2)};
    for (unsigned wrap = 0; wrap < 2; wrap++) for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned asserted = 0; asserted < 2; asserted++) {
            struct CupidGBA* gba = machine(clocks[wrap]);
            gba->cpu.cpsr.packed = MODE_SYS;
            const uint32_t arm[] = {0xE2855001, 0xE2855001, 0xE1A00000};
            arm_program(gba, arm, sizeof(arm));
            if (thumb) {
                const uint16_t code[] = {0x3501, 0x3501, 0x46C0}; /* add r5, #1 */
                memcpy(gba->memory.iwram, code, sizeof(code));
                _CupidSetMode(&gba->cpu, MODE_THUMB);
                gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
                CupidThumbWritePC(&gba->cpu);
            }
            gba->cpu.gprs[5] = 0;
            cupid_io_write(gba, REG_IE, IRQ_TIMER0);
            cupid_io_write(gba, REG_IME, 1);
            cupid_raise_irq(gba, IRQ_TIMER0, 0);
            tick(gba, asserted ? 3 : 2);
            CHECK(gba->irq.available && gba->irq.line == (asserted != 0));
            cupid_gba_halt(gba);
            cupid_gba_step(gba);
            CHECK(!gba->cpu.halted && gba->irq.line);
            CHECK(gba->cpu.gprs[5] == (asserted ? 0 : 1));
            if (!asserted) {
                CHECK(gba->cpu.privilegeMode == MODE_SYS);
                CHECK(gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM));
                cupid_gba_step(gba);
            }
            CHECK(gba->cpu.privilegeMode == MODE_IRQ && gba->cpu.executionMode == MODE_ARM);
            CHECK(gba->cpu.gprs[3] == 99);
            CHECK(gba->cpu.gprs[ARM_LR] == GBA_BASE_IWRAM + 4 + (asserted ? 0 : thumb ? 2 : 4));
            CHECK(gba->cpu.spsr.t == (thumb != 0));
            destroy(gba);
        }
}

static void halt_control_acceptance_retains_the_following_cpu_phase(void) {
    const uint64_t clocks[] = {10, UINT32_MAX - UINT64_C(2)};
    for (unsigned wrap = 0; wrap < 2; wrap++) for (unsigned thumb = 0; thumb < 2; thumb++)
        for (unsigned asserted = 0; asserted < 2; asserted++) {
            struct CupidGBA* gba = machine(clocks[wrap] - 1);
            gba->cpu.cpsr.packed = MODE_SYS;
            const uint32_t handler = 0xE3A03063;
            memcpy(gba->memory.bios + BASE_IRQ, &handler, sizeof(handler));
            if (thumb) {
                const uint16_t code[] = {0x7008, 0x3501, 0x46C0}; /* strb r0,[r1]; add r5,#1 */
                memcpy(gba->memory.bios + 0x80, code, sizeof(code));
                _CupidSetMode(&gba->cpu, MODE_THUMB);
                gba->cpu.gprs[ARM_PC] = 0x80;
                CupidThumbWritePC(&gba->cpu);
            } else {
                const uint32_t code[] = {0xE5C10000, 0xE2855001, 0xE1A00000};
                memcpy(gba->memory.bios + 0x80, code, sizeof(code));
                gba->cpu.gprs[ARM_PC] = 0x80;
                CupidARMWritePC(&gba->cpu);
            }
            gba->cpu.gprs[0] = gba->cpu.gprs[5] = 0;
            gba->cpu.gprs[1] = GBA_BASE_IO + REG_HALTCNT;
            cupid_io_write(gba, REG_IE, IRQ_TIMER0);
            cupid_io_write(gba, REG_IME, 1);
            /* Accept the masks before announcing the historical source
             * phase. A backdated IF event must not publish newer masks. */
            tick(gba, 1);
            cupid_raise_irq_at(gba, IRQ_TIMER0, clocks[wrap] - asserted);
            cupid_gba_step(gba);
            /* STRB finishes its fetch and data clocks. HALT is accepted on
             * the following fetch, rather than an empty internal clock. */
            CHECK(cupid_timing_global_time(&gba->timing) == clocks[wrap] + 2);
            CHECK(!gba->cpu.halted && !gba->cpuInstructionPending);
            CHECK(gba->irq.available && gba->irq.line == (asserted != 0));
            CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->haltEvent));
            cupid_gba_step(gba);
            CHECK(cupid_timing_global_time(&gba->timing) == clocks[wrap] + 3);
            CHECK(gba->cpu.halted && gba->irq.available && gba->irq.line);
            CHECK(!gba->haltIRQSampleValid);
            CHECK(gba->cpu.gprs[5] == (asserted ? 0 : 1));
            /* An earlier IRQ retains its partial vector refill. Otherwise
             * the following ADD has completed before HALT holds the CPU. */
            CHECK(gba->cpuInstructionPending == (asserted != 0));
            CHECK(gba->cpu.gprs[ARM_PC] == (asserted ? BASE_IRQ : 0x80 + (thumb ? 6 : 12)));
            CHECK(gba->cpu.privilegeMode == (asserted ? MODE_IRQ : MODE_SYS));
            cupid_gba_step(gba);
            CHECK(!gba->cpu.halted && !gba->haltIRQSampleValid);
            CHECK(gba->cpu.gprs[5] == (asserted ? 0 : 1));
            CHECK(!gba->cpuInstructionPending);
            CHECK(cupid_timing_global_time(&gba->timing) == clocks[wrap] + (asserted ? 8 : 9));
            CHECK(gba->cpu.privilegeMode == MODE_IRQ && gba->cpu.gprs[3] == 99);
            CHECK(gba->cpu.gprs[ARM_LR] == 0x80 + 4 + (thumb ? 2 : 4) * (asserted ? 1 : 2));
            CHECK(gba->cpu.spsr.t == (thumb != 0));
            gba->haltIRQSampleValid = true;
            cupid_gba_reset(gba);
            CHECK(!gba->haltIRQSampleValid && !gba->cpu.halted);
            destroy(gba);
        }
}

static void cpsr_unmask_waits_for_the_sampled_instruction(void) {
    struct CupidGBA* gba = machine(0);
    const uint32_t code[] = {
        0xE121F000, /* msr cpsr_c, r0 */
        0xE2822001, /* add r2, r2, #1 */
        0xE2822001
    };
    arm_program(gba, code, sizeof(code));
    gba->cpu.gprs[0] = MODE_SYS;
    gba->irq.line = true;
    cupid_gba_step(gba);
    CHECK(!gba->cpu.cpsr.i && gba->cpu.irqDisableLatch);
    CHECK(gba->cpu.privilegeMode == MODE_SYS);
    cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[2] == 1 && !gba->cpu.irqDisableLatch);
    cupid_gba_step(gba);
    CHECK(gba->cpu.privilegeMode == MODE_IRQ && gba->cpu.cpsr.i);
    CHECK(gba->cpu.gprs[2] == 1 && gba->cpu.gprs[3] == 99);
    CHECK(gba->cpu.gprs[ARM_LR] == GBA_BASE_IWRAM + 12);
    CHECK(gba->cpu.spsr.packed == MODE_SYS);
    CHECK(cupid_timing_global_time(&gba->timing) == 6);
    destroy(gba);
}

static void thumb_irq_saves_state_and_returns_to_the_interrupted_instruction(void) {
    struct CupidGBA* gba = machine(0);
    const uint16_t code[] = {0x2207, 0x2208, 0x2209}; /* mov r2, #7/8/9 */
    memcpy(gba->memory.iwram, code, sizeof(code));
    gba->cpu.cpsr.i = 0;
    _CupidSetMode(&gba->cpu, MODE_THUMB);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    CupidThumbWritePC(&gba->cpu);
    const uint32_t handler = 0xE25EF004; /* subs pc, lr, #4 */
    memcpy(gba->memory.bios + BASE_IRQ, &handler, sizeof(handler));
    int32_t status = gba->cpu.cpsr.packed;
    gba->irq.line = true;
    cupid_gba_step(gba);
    CHECK(gba->cpu.executionMode == MODE_THUMB);
    CHECK(gba->cpu.privilegeMode == MODE_SYS);
    CHECK(gba->cpu.cpsr.packed == status);
    CHECK(gba->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + 2);
    CHECK(gba->cpu.bankedRegisters[BANK_IRQ][1] == GBA_BASE_IWRAM + 4);
    CHECK(gba->cpu.gprs[2] == 0);
    gba->irq.line = false;
    cupid_gba_step(gba);
    CHECK(gba->cpu.gprs[2] == 7);
    destroy(gba);
}

static void mmio_reads_sample_after_the_data_bus_cycle(void) {
    const uint32_t instructions[] = {
        0xE5D10000, /* ldrb r0, [r1] */
        0xE1D100B0, /* ldrh r0, [r1] */
        0xE5910000  /* ldr r0, [r1] */
    };
    for (unsigned i = 0; i < 3; i++) {
        struct CupidGBA* gba = machine(0);
        const uint32_t code[] = {instructions[i], 0xE1A00000, 0xE1A00000};
        arm_program(gba, code, sizeof(code));
        gba->cpu.gprs[1] = GBA_BASE_IO + REG_TM0CNT_L;
        cupid_io_write(gba, REG_TM0CNT_L, 0x00FE);
        cupid_io_write(gba, REG_TM0CNT_H, 0x80);
        tick(gba, 2);
        cupid_gba_step(gba);
        CHECK((uint32_t)gba->cpu.gprs[0] == (i == 0 ? 0 : i == 1 ? 0x100 : 0x800100));
        CHECK(cupid_timing_global_time(&gba->timing) == 5);
        destroy(gba);
    }
}

static void arm_and_thumb_mmio_writes_accept_all_access_widths(void) {
    const uint32_t arm[] = {0xE5C10000, 0xE1C100B0, 0xE5810000}; /* strb/strh/str r0, [r1] */
    const uint16_t thumb[] = {0x7008, 0x8008, 0x6008};
    for (unsigned mode = 0; mode < 2; mode++) {
        for (unsigned width = 0; width < 3; width++) {
            struct CupidGBA* gba = machine(0);
            if (mode) {
                const uint16_t code[] = {thumb[width], 0x46C0, 0x46C0};
                memcpy(gba->memory.iwram, code, sizeof(code));
                _CupidSetMode(&gba->cpu, MODE_THUMB);
                gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
                CupidThumbWritePC(&gba->cpu);
            } else {
                const uint32_t code[] = {arm[width], 0xE1A00000, 0xE1A00000};
                arm_program(gba, code, sizeof(code));
            }
            gba->cpu.gprs[0] = 0xFFFF;
            gba->cpu.gprs[1] = GBA_BASE_IO + REG_IE;
            cupid_gba_step(gba);
            CHECK(cupid_timing_global_time(&gba->timing) == 2);
            CHECK(cupid_io_read(gba, REG_IE) == 0);
            cupid_gba_step(gba);
            CHECK(cupid_timing_global_time(&gba->timing) == 3);
            CHECK(cupid_io_read(gba, REG_IE) == (width ? 0x3FFF : 0xFF));
            destroy(gba);
        }
    }
}

static void timer_irq_late_dispatch_and_clock_wrap(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 1u, UINT32_MAX - 1u, UINT64_C(0x300000000)};
    for (unsigned i = 0; i < sizeof(clocks) / sizeof(*clocks); i++) {
        struct CupidGBA* late = machine(clocks[i]);
        struct CupidGBA* punctual = machine(clocks[i]);
        struct CupidGBA* machines[] = {late, punctual};
        for (unsigned j = 0; j < 2; j++) {
            cupid_io_write(machines[j], REG_IE, IRQ_TIMER0);
            cupid_io_write(machines[j], REG_IME, 1);
            cupid_io_write(machines[j], REG_TM0CNT_L, 0xFFFF);
            cupid_io_write(machines[j], REG_TM0CNT_H, 0xC0);
        }
        tick(late, 50);
        for (int cycle = 0; cycle < 50; cycle++) tick(punctual, 1);
        CHECK(late->irqf == IRQ_TIMER0 && late->irq.line && late->irq.available);
        CHECK(late->irqf == punctual->irqf && late->irq.line == punctual->irq.line);
        CHECK(late->irq.available == punctual->irq.available);
        CHECK(late->timers[0].event.when == punctual->timers[0].event.when);
        cupid_io_write(late, REG_TM0CNT_H, 0);
        cupid_io_write(punctual, REG_TM0CNT_H, 0);
        tick(late, 1);
        tick(punctual, 1);
        cupid_io_write(late, REG_IF, IRQ_TIMER0);
        cupid_io_write(punctual, REG_IF, IRQ_TIMER0);
        tick(late, 3);
        for (int cycle = 0; cycle < 3; cycle++) tick(punctual, 1);
        CHECK(late->irqf == 0 && !late->irq.line && !late->irq.available);
        CHECK(late->irqf == punctual->irqf && late->irq.line == punctual->irq.line);
        destroy(late);
        destroy(punctual);
    }
}

static void dma_completion_requests_the_channel_bit(void) {
    for (int channel = 0; channel < 4; channel++) {
        struct CupidGBA* gba = machine(0);
        const uint16_t value = 0x5A81;
        memcpy(gba->memory.iwram + 0x100, &value, sizeof(value));
        cupid_io_write(gba, REG_IE, IRQ_DMA0 << channel);
        cupid_io_write(gba, REG_IME, 1);
        cupid_dma_write_sad(gba, channel, GBA_BASE_IWRAM + 0x100);
        cupid_dma_write_dad(gba, channel, GBA_BASE_EWRAM);
        cupid_dma_write_cnt_lo(gba, channel, 1);
        cupid_dma_write_cnt(gba, channel, 0xC000);
        CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0);
        CHECK(gba->cpu.cycles == 0);
        tick(gba, 3); /* Two request clocks and the bus handover clock. */
        CHECK(gba->memory.dmaRunning && gba->memory.dma[channel].latch == 0);
        tick(gba, 1); /* IWRAM read completes; EWRAM write is still pending. */
        CHECK(gba->memory.dma[channel].latch == UINT32_C(0x5A815A81));
        CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0);
        tick(gba, 3);
        CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM, NULL) == value);
        CHECK(gba->cpu.cycles == 0 && gba->memory.dmaRunning);
        CHECK(gba->irqf == 0);
        tick(gba, 1);
        CHECK(!gba->memory.dmaRunning);
        CHECK(gba->irqf == 0 && (gba->irq.pendingIF & (IRQ_DMA0 << channel)));
        tick(gba, 1); /* IF accepts the completed channel's signal. */
        CHECK(gba->irqf == (IRQ_DMA0 << channel) && !gba->irq.line);
        tick(gba, 2);
        CHECK(gba->irq.line);
        destroy(gba);
    }
}

int main(void) {
    RUN(dma_completion_requests_the_channel_bit);
    RUN(register_visibility_and_signal_stages);
    RUN(byte_masks_and_acknowledgements);
    RUN(consecutive_line_changes_keep_their_deadlines);
    RUN(halt_wakes_with_ime_clear_and_cpsr_masked);
    RUN(halt_samples_the_irq_line_before_its_wake_clock);
    RUN(halt_control_acceptance_retains_the_following_cpu_phase);
    RUN(cpsr_unmask_waits_for_the_sampled_instruction);
    RUN(thumb_irq_saves_state_and_returns_to_the_interrupted_instruction);
    RUN(mmio_reads_sample_after_the_data_bus_cycle);
    RUN(arm_and_thumb_mmio_writes_accept_all_access_widths);
    RUN(timer_irq_late_dispatch_and_clock_wrap);
    return 0;
}
