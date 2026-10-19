/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static const uint64_t origins[] = {
    0, INT32_MAX - 16u, UINT32_MAX - 16u, UINT64_C(0x300000000) - 16
};
static const uint32_t queued[] = {0x44332211, 0x88776655, 0xCCBBAA99, 0x10FFEEDD,
                                0x241302F1, 0x68574635, 0xAC9B8A79};
static const uint32_t incoming[] = {0x76543210, 0xFEDCBA98, 0x67452301, 0xEFCDAB89};
static unsigned observations;

static struct CupidGBA* machine(uint64_t origin, unsigned fifo, unsigned channel, unsigned timer) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.masterCycles = (uint32_t)origin;
    gba->timing.globalCycles = origin;
    gba->audio.lastSample = gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + 512;
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    /* Keep the other FIFO on the other timer, including destination controls. */
    unsigned timerA = fifo ? timer ^ 1 : timer;
    unsigned timerB = fifo ? timer : timer ^ 1;
    cupid_io_write(gba, REG_SOUNDCNT_H, (uint16_t)((timerA << 10) | (timerB << 14)));
    for (unsigned i = 0; i < 4; i++)
        cupid_memory_store32(&gba->cpu, GBA_BASE_IWRAM + 0x400 + 4 * i, (int32_t)incoming[i], NULL);
    cupid_dma_write_sad(gba, channel, GBA_BASE_IWRAM + 0x400);
    cupid_dma_write_dad(gba, channel, GBA_BASE_IO + (fifo ? REG_FIFO_B_LO : REG_FIFO_A_LO));
    cupid_dma_write_cnt_lo(gba, channel, 99);
    cupid_dma_write_cnt(gba, channel, 0xF640);
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static struct CupidAudioFIFO* selected(struct CupidGBA* gba, unsigned fifo) {
    return fifo ? &gba->audio.chB : &gba->audio.chA;
}

static void refill_samples_the_queue_before_loading_the_staged_word(void) {
    /* Rows are the queued-word count before the timer. A loaded stage retains
     * its existing bytes; an empty stage consumes the oldest complete word. */
    const bool emptyStage[] = {true, true, true, true, false, false, false, false};
    const bool occupiedStage[] = {true, true, true, true, false, false, false, false};
    const uint32_t stages[] = {0, 0x80, 0x807F, 0x807F34};
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned fifo = 0; fifo < 2; fifo++)
    for (unsigned channel = 1; channel <= 2; channel++) for (unsigned timer = 0; timer < 2; timer++)
    for (unsigned words = 0; words < 8; words++) for (unsigned stage = 0; stage < 4; stage++) {
        struct CupidGBA* gba = machine(origins[epoch], fifo, channel, timer);
        struct CupidAudioFIFO* output = selected(gba, fifo);
        for (unsigned word = 0; word < words; word++) cupid_audio_fifo_write(output, queued[word]);
        output->internalSample = stages[stage];
        output->internalRemaining = (int)stage;
        cupid_audio_timer_overflow(&gba->audio, timer, origins[epoch]);
        const bool pop = !stage && words;
        const uint32_t sample = pop ? queued[0] : stages[stage];
        CHECK(output->latchedSample == (int8_t)sample);
        CHECK(output->fifoRead == (int)pop && output->fifoWrite == (int)(words % GBA_AUDIO_FIFO_SIZE));
        CHECK(output->fifoCount == (int)(words - pop));
        CHECK(output->internalRemaining == (stage ? (int)stage - 1 : pop ? 3 : 0));
        CHECK(output->internalSample == sample >> 8);
        CHECK(!memcmp(output->fifo, queued, words * sizeof(*queued)));
        const bool requested = stage ? occupiedStage[words] : emptyStage[words];
        struct CupidDMA* dma = &gba->memory.dma[channel];
        CHECK(cupid_timing_is_scheduled(&gba->timing, &dma->requestEvent) == requested);
        CHECK(!dma->ready && !gba->memory.dmaRunning);
        CHECK(dma->nextCount == 4 && dma->nextSource == GBA_BASE_IWRAM + 0x400);
        if (requested) CHECK(dma->requestEvent.when == (uint32_t)(origins[epoch] + 3));
        observations++;
        destroy(gba);
    }
}

static void timer_dma_and_irq_edges_follow_the_audio_request_phase(void) {
    /* Four queued words delay the request until the second timer overflow:
     * overflow 10, request 13, bus head 14, then four read/write pairs. */
    const unsigned writes[] = {16, 18, 20, 22};
    const int8_t samples[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned fifo = 0; fifo < 2; fifo++)
    for (unsigned channel = 1; channel <= 2; channel++) for (unsigned timer = 0; timer < 2; timer++) {
        struct CupidGBA* punctual = machine(origins[epoch], fifo, channel, timer);
        struct CupidGBA* late = machine(origins[epoch], fifo, channel, timer);
        struct CupidGBA* machines[] = {punctual, late};
        for (unsigned i = 0; i < 2; i++) {
            struct CupidGBA* gba = machines[i];
            for (unsigned word = 0; word < 4; word++) cupid_audio_fifo_write(selected(gba, fifo), queued[word]);
            cupid_io_write(gba, REG_TM2CNT_L, 0);
            cupid_io_write(gba, REG_TM2CNT_H, 0x80);
            cupid_io_write(gba, REG_TM0CNT_L + timer * 4, 0xFFFC);
            cupid_io_write(gba, REG_TM0CNT_H + timer * 4, 0xC0);
        }
        for (unsigned clock = 1; clock <= 32; clock++) {
            cupid_timing_tick(&punctual->timing, 1);
            struct CupidDMA* dma = &punctual->memory.dma[channel];
            struct CupidAudioFIFO* output = selected(punctual, fifo);
            if (clock < 10) CHECK(!cupid_timing_is_scheduled(&punctual->timing, &dma->requestEvent));
            if (clock == 6) {
                CHECK(output->fifoRead == 1 && output->internalRemaining == 3);
                CHECK(!cupid_timing_is_scheduled(&punctual->timing, &dma->requestEvent));
            }
            if (clock == 10) {
                CHECK(output->fifoRead == 1 && output->internalRemaining == 2);
                CHECK(cupid_timing_is_scheduled(&punctual->timing, &dma->requestEvent));
                CHECK(dma->requestEvent.when == (uint32_t)(origins[epoch] + 13));
            }
            CHECK(punctual->memory.dmaRunning == (clock >= 13 && clock < 23));
            unsigned published = 0;
            for (unsigned word = 0; word < 4; word++) {
                if (clock >= writes[word]) {
                    CHECK(output->fifo[(4 + word) % GBA_AUDIO_FIFO_SIZE] == incoming[word]);
                    ++published;
                } else {
                    unsigned slot = (4 + word) % GBA_AUDIO_FIFO_SIZE;
                    CHECK(output->fifo[slot] == (slot < 4 ? queued[slot] : 0));
                }
            }
            CHECK(dma->nextSource == GBA_BASE_IWRAM + 0x400 + published * 4);
            if (clock >= 6) CHECK(output->latchedSample == samples[(clock - 6) / 4]);
        }
        cupid_timing_tick(&late->timing, 32);
        CHECK(!memcmp(selected(punctual, fifo), selected(late, fifo), sizeof(struct CupidAudioFIFO)));
        CHECK(punctual->memory.dma[channel].nextSource == late->memory.dma[channel].nextSource);
        CHECK(punctual->memory.dma[channel].nextCount == late->memory.dma[channel].nextCount);
        CHECK(punctual->memory.dma[channel].busyUntil == origins[epoch] + 22);
        CHECK(late->memory.dma[channel].busyUntil == punctual->memory.dma[channel].busyUntil);
        CHECK(cupid_io_read(punctual, REG_TM2CNT_L) == 30 && cupid_io_read(late, REG_TM2CNT_L) == 30);
        CHECK(punctual->irqf == ((IRQ_TIMER0 << timer) | (IRQ_DMA0 << channel)));
        CHECK(late->irqf == punctual->irqf && late->irq.line == punctual->irq.line);
        observations++;
        destroy(punctual);
        destroy(late);
    }
}

static void inactive_sources_do_not_publish_refill_requests(void) {
    for (unsigned epoch = 0; epoch < 4; epoch++) for (unsigned fifo = 0; fifo < 2; fifo++)
    for (unsigned channel = 1; channel <= 2; channel++) for (unsigned timer = 0; timer < 2; timer++)
    for (unsigned state = 0; state < 6; state++) {
        struct CupidGBA* gba = machine(origins[epoch], fifo, channel, timer);
        struct CupidAudioFIFO* output = selected(gba, fifo);
        for (unsigned word = 0; word < 4; word++) cupid_audio_fifo_write(output, queued[word]);
        struct CupidDMA* dma = &gba->memory.dma[channel];
        unsigned overflow = timer;
        if (state == 0) cupid_io_write(gba, REG_SOUNDCNT_X, 0);
        else if (state == 1) overflow ^= 1;
        else if (state == 2) cupid_dma_write_cnt(gba, channel, dma->reg & 0x7FFF);
        else if (state == 3) cupid_dma_write_cnt(gba, channel, (dma->reg & ~0x3000u) | 0x2000);
        else {
            cupid_dma_write_cnt(gba, channel, dma->reg & 0x7FFF);
            cupid_dma_write_dad(gba, channel, state == 4 ? GBA_BASE_IO + (fifo ? REG_FIFO_A_LO : REG_FIFO_B_LO)
                                                       : GBA_BASE_IWRAM + 0x100);
            cupid_dma_write_cnt(gba, channel, 0xF640);
        }
        const struct CupidAudioFIFO before = *output;
        cupid_audio_timer_overflow(&gba->audio, overflow, origins[epoch]);
        CHECK(!cupid_timing_is_scheduled(&gba->timing, &dma->requestEvent));
        CHECK(!dma->ready && !gba->memory.dmaRunning);
        if (state < 2) CHECK(!memcmp(output, &before, sizeof(before)));
        observations++;
        destroy(gba);
    }
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "levels")) RUN(refill_samples_the_queue_before_loading_the_staged_word);
    else if (!strcmp(argv[1], "chronology")) RUN(timer_dma_and_irq_edges_follow_the_audio_request_phase);
    else if (!strcmp(argv[1], "selection")) RUN(inactive_sources_do_not_publish_refill_requests);
    else return 2;
    printf("observations=%u\n", observations);
    return 0;
}
