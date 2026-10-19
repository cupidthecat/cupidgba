/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static const uint64_t origins[] = {
    0, INT32_MAX - 4u, UINT32_MAX - 4u, UINT64_C(0x300000000) - 4
};
static unsigned observations;

static struct CupidGBA* machine(uint64_t origin, unsigned channel, bool interrupt, bool word) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = origin;
    gba->timing.masterCycles = (uint32_t)origin;
    gba->audio.lastSample = gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + 512;
    cupid_io_write(gba, REG_IE, IRQ_DMA0 << channel);
    cupid_io_write(gba, REG_IME, 1);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM + 0x100, 0x76543210, NULL);
    cupid_dma_write_sad(gba, (int)channel, GBA_BASE_IWRAM + 0x100);
    cupid_dma_write_dad(gba, (int)channel, GBA_BASE_IWRAM + 0x200);
    cupid_dma_write_cnt_lo(gba, (int)channel, 1);
    cupid_dma_write_cnt(gba, (int)channel,
        (uint16_t)(0x8000 | (interrupt ? 0x4000 : 0) | (word ? 0x400 : 0)));
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void completion_has_its_own_signal_and_register_edges(void) {
    /* An internal transfer writes at clock 5, signals completion at 6 and
     * publishes IF at 7. Bus release stays at 6; IRQ input follows at 9. */
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned ch = 0; ch < 4; ch++)
    for (unsigned word = 0; word < 2; word++) for (unsigned interrupt = 0; interrupt < 2; interrupt++) {
        struct CupidGBA* gba = machine(origins[epoch], ch, interrupt != 0, word != 0);
        struct CupidGBA* late = machine(origins[epoch], ch, interrupt != 0, word != 0);
        const uint16_t bit = IRQ_DMA0 << ch;
        for (unsigned clock = 1; clock <= 9; clock++) {
            cupid_timing_tick(&gba->timing, 1);
            struct CupidDMA* dma = &gba->memory.dma[ch];
            const uint32_t payload = cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM + 0x200, NULL);
            CHECK(payload == (clock >= 5 ? (word ? 0x76543210u : 0x3210u) : 0));
            CHECK(dma->enable == (clock < 5));
            CHECK(gba->memory.dmaRunning == (clock >= 2 && clock < 6));
            CHECK(gba->irq.pendingIF == (interrupt && clock >= 6 ? bit : 0));
            CHECK(gba->irqf == (interrupt && clock >= 7 ? bit : 0));
            CHECK(gba->irq.available == (interrupt && clock >= 8));
            CHECK(gba->irq.line == (interrupt && clock >= 9));
            if (clock >= 5) {
                CHECK(dma->busyUntil == origins[epoch] + 5);
                CHECK(dma->nextSource == GBA_BASE_IWRAM + 0x100 + (word ? 4 : 2));
                CHECK(dma->nextCount == 0);
            }
        }
        cupid_timing_tick(&late->timing, 9);
        CHECK(late->irq.pendingIF == gba->irq.pendingIF && late->irqf == gba->irqf);
        CHECK(late->irq.available == gba->irq.available && late->irq.line == gba->irq.line);
        CHECK(late->memory.dma[ch].nextSource == gba->memory.dma[ch].nextSource);
        CHECK(late->memory.dma[ch].busyUntil == gba->memory.dma[ch].busyUntil);
        CHECK(!late->memory.dmaRunning);
        observations++;
        destroy(gba);
        destroy(late);
    }
}

static void accepted_completion_survives_control_changes_but_not_reset(void) {
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned ch = 0; ch < 4; ch++)
    for (unsigned action = 0; action < 3; action++) {
        struct CupidGBA* gba = machine(origins[epoch], ch, true, true);
        cupid_timing_tick(&gba->timing, 5);
        CHECK(gba->irqf == 0 && gba->irq.pendingIF == 0);
        if (action == 0) cupid_dma_write_cnt(gba, (int)ch, 0);
        else if (action == 1) cupid_dma_write_cnt(gba, (int)ch, 0x8400);
        else cupid_dma_reset(gba);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->irqf == 0);
        CHECK(gba->irq.pendingIF == (action < 2 ? IRQ_DMA0 << ch : 0));
        cupid_timing_tick(&gba->timing, 3);
        CHECK(gba->irqf == (action < 2 ? IRQ_DMA0 << ch : 0));
        CHECK(gba->irq.line == (action < 2));
        if (action == 2) CHECK(!gba->memory.dmaRunning);
        observations++;
        destroy(gba);
    }
}

static void register_writes_and_other_sources_keep_their_original_edges(void) {
    /* An acknowledgement before the completion signal cannot erase it.
     * One on the signal edge can clear the pending IF register update. */
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned ch = 0; ch < 4; ch++)
    for (unsigned action = 0; action < 5; action++) {
        struct CupidGBA* gba = machine(origins[epoch], ch, true, true);
        const uint16_t bit = IRQ_DMA0 << ch;
        cupid_timing_tick(&gba->timing, 5);
        if (action == 0) cupid_io_write(gba, REG_IF, bit);
        else if (action == 1) cupid_io_write(gba, REG_IE, 0);
        else if (action == 2) cupid_io_write(gba, REG_IME, 0);
        else if (action == 3) cupid_raise_irq_at(gba, IRQ_TIMER0, origins[epoch] + 5);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->irqf == (action == 3 ? IRQ_TIMER0 : 0));
        CHECK((gba->irq.pendingIF & bit) == bit);
        if (action == 4) cupid_io_write(gba, REG_IF, bit);
        cupid_timing_tick(&gba->timing, 1);
        CHECK(gba->irqf == ((action == 4 ? 0 : bit) | (action == 3 ? IRQ_TIMER0 : 0)));
        CHECK(gba->ie == (action == 1 ? 0 : bit));
        CHECK(gba->ime == (action == 2 ? 0 : 1));
        cupid_timing_tick(&gba->timing, 2);
        CHECK(gba->irq.line == (action == 0 || action == 3));
        observations++;
        destroy(gba);
    }
}

static void channel_handover_retains_both_completion_signals(void) {
    for (unsigned epoch = 0; epoch < 4; epoch++) {
        struct CupidGBA* gba = machine(origins[epoch], 1, true, true);
        cupid_io_write(gba, REG_IE, IRQ_DMA1 | IRQ_DMA2);
        cupid_dma_write_sad(gba, 2, GBA_BASE_IWRAM + 0x100);
        cupid_dma_write_dad(gba, 2, GBA_BASE_IWRAM + 0x204);
        cupid_dma_write_cnt_lo(gba, 2, 1);
        cupid_dma_write_cnt(gba, 2, 0xC400);
        for (unsigned clock = 1; clock <= 9; clock++) {
            cupid_timing_tick(&gba->timing, 1);
            CHECK(gba->irqf == (clock >= 9 ? IRQ_DMA1 | IRQ_DMA2 : clock >= 7 ? IRQ_DMA1 : 0));
            CHECK(gba->memory.dmaRunning == (clock >= 2 && clock < 8));
        }
        CHECK(gba->memory.dma[1].busyUntil == origins[epoch] + 5);
        CHECK(gba->memory.dma[2].busyUntil == origins[epoch] + 7);
        CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM + 0x200, NULL) == 0x76543210u);
        CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IWRAM + 0x204, NULL) == 0x76543210u);
        observations++;
        destroy(gba);
    }
}

int main(void) {
    RUN(completion_has_its_own_signal_and_register_edges);
    RUN(accepted_completion_survives_control_changes_but_not_reset);
    RUN(register_writes_and_other_sources_keep_their_original_edges);
    RUN(channel_handover_retains_both_completion_signals);
    printf("observations=%u\n", observations);
    return 0;
}
