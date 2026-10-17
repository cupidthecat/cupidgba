/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"

static const uint64_t origins[] = {
    0, INT32_MAX - 16u, UINT32_MAX - 16u, UINT64_C(0x300000000) - 16
};
static unsigned observations;

static struct CupidGBA* machine(uint64_t origin) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.masterCycles = (uint32_t)origin;
    gba->timing.globalCycles = origin;
    gba->audio.lastSample = origin;
    gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + 512;
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static struct CupidAudioFIFO* channel(struct CupidGBA* gba, unsigned fifo) {
    return fifo ? &gba->audio.chB : &gba->audio.chA;
}

static uint32_t address(unsigned fifo) {
    return GBA_BASE_IO + (fifo ? REG_FIFO_B_LO : REG_FIFO_A_LO);
}

static void store(struct CupidGBA* gba, unsigned width, uint32_t target, uint32_t value) {
    if (width == 1) cupid_memory_store8(&gba->cpu, target, value, NULL);
    else if (width == 2) cupid_memory_store16(&gba->cpu, target, value, NULL);
    else cupid_memory_store32(&gba->cpu, target, (int32_t)value, NULL);
}

static void powered_off_writes_do_not_enter_either_fifo(void) {
    for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(*origins); epoch++) {
        struct CupidGBA* gba = machine(origins[epoch]);
        const struct CupidAudioFIFO before[] = {gba->audio.chA, gba->audio.chB};
        for (unsigned width = 1; width <= 4; width *= 2) {
            for (unsigned fifo = 0; fifo < 2; fifo++) {
                for (unsigned lane = 0; lane < 4; lane += width) {
                    store(gba, width, address(fifo) + lane, UINT32_C(0x87654321));
                    CHECK(!memcmp(channel(gba, fifo), &before[fifo], sizeof(before[fifo])));
                    observations++;
                }
            }
        }
        /* Ignored words must not become playable when sound is enabled. */
        cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
        cupid_audio_timer_overflow(&gba->audio, 0, origins[epoch]);
        CHECK(gba->audio.chA.latchedSample == 0 && gba->audio.chB.latchedSample == 0);
        destroy(gba);
    }
}

static void arm_and_thumb_stores_obey_the_sound_power_state(void) {
    const uint32_t arm[] = {0xE5C01000, 0xE1C010B0, 0xE5801000};
    const uint16_t thumb[] = {0x7001, 0x8001, 0x6001}; /* STRB/STRH/STR r1,[r0]. */
    for (unsigned mode = 0; mode < 2; mode++) {
        for (unsigned size = 0; size < 3; size++) {
            for (unsigned fifo = 0; fifo < 2; fifo++) {
                for (unsigned lane = 0; lane < 4; lane += 1u << size) {
                    struct CupidGBA* gba = machine(0);
                    _CupidSetMode(&gba->cpu, mode ? MODE_THUMB : MODE_ARM);
                    if (mode) memcpy(gba->memory.iwram, &thumb[size], sizeof(*thumb));
                    else memcpy(gba->memory.iwram, &arm[size], sizeof(*arm));
                    gba->cpu.gprs[0] = address(fifo) + lane;
                    gba->cpu.gprs[1] = UINT32_C(0x87654321);
                    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
                    if (mode) {
                        CupidThumbWritePC(&gba->cpu);
                        cupid_thumb_step(&gba->cpu);
                    } else {
                        CupidARMWritePC(&gba->cpu);
                        cupid_arm_step(&gba->cpu);
                    }
                    CHECK(channel(gba, fifo)->fifoWrite == 0);
                    CHECK(channel(gba, fifo)->fifoRead == 0);
                    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
                    cupid_audio_timer_overflow(&gba->audio, 0, cupid_timing_global_time(&gba->timing));
                    CHECK(channel(gba, fifo)->latchedSample == 0);
                    observations++;
                    destroy(gba);
                }
            }
        }
    }
}

static void immediate_dma_completes_even_when_fifo_writes_are_ignored(void) {
    const uint32_t payload[] = {0x04030201, 0x88776655, 0xFFEEDDCC, 0x12345678};
    for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(*origins); epoch++) {
        for (unsigned dma = 1; dma <= 2; dma++) {
            for (unsigned fifo = 0; fifo < 2; fifo++) {
                for (unsigned enabled = 0; enabled < 2; enabled++) {
                    struct CupidGBA* gba = machine(origins[epoch]);
                    cupid_io_write(gba, REG_SOUNDCNT_X, enabled ? 0x80 : 0);
                    memcpy(gba->memory.iwram, payload, sizeof(payload));
                    cupid_dma_write_sad(gba, dma, GBA_BASE_IWRAM);
                    cupid_dma_write_dad(gba, dma, address(fifo));
                    cupid_dma_write_cnt_lo(gba, dma, 4);
                    cupid_dma_write_cnt(gba, dma, 0xC440); /* Word, fixed destination, IRQ. */
                    cupid_timing_tick(&gba->timing, 32);
                    CHECK(!gba->memory.dmaRunning && !gba->memory.dma[dma].enable);
                    CHECK(gba->memory.dma[dma].nextSource == GBA_BASE_IWRAM + sizeof(payload));
                    CHECK(gba->irqf == (IRQ_DMA0 << dma));
                    CHECK(channel(gba, fifo)->fifoWrite == (enabled ? 4 : 0));
                    if (enabled) CHECK(!memcmp(channel(gba, fifo)->fifo, payload, sizeof(payload)));
                    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
                    cupid_audio_timer_overflow(&gba->audio, 0, origins[epoch] + 32);
                    CHECK(channel(gba, fifo)->latchedSample == (enabled ? 1 : 0));
                    observations++;
                    destroy(gba);
                }
            }
        }
    }
}

static void power_transitions_discard_queued_words_and_accept_new_words(void) {
    for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(*origins); epoch++) {
        for (unsigned width = 1; width <= 4; width *= 2) {
            struct CupidGBA* gba = machine(origins[epoch]);
            store(gba, width, GBA_BASE_IO + REG_SOUNDCNT_X, 0x80);
            for (unsigned fifo = 0; fifo < 2; fifo++) {
                for (unsigned word = 0; word < 6; word++) {
                    store(gba, 4, address(fifo), UINT32_C(0x11223344));
                }
                CHECK(channel(gba, fifo)->fifoWrite == 6);
            }
            store(gba, width, GBA_BASE_IO + REG_SOUNDCNT_X, 0);
            store(gba, width, GBA_BASE_IO + REG_SOUNDCNT_X, 0);
            store(gba, width, GBA_BASE_IO + REG_SOUNDCNT_X, 0x80);
            store(gba, width, GBA_BASE_IO + REG_SOUNDCNT_X, 0x80);
            for (unsigned fifo = 0; fifo < 2; fifo++) {
                CHECK(channel(gba, fifo)->fifoWrite == 0 && channel(gba, fifo)->fifoRead == 0);
                store(gba, 4, address(fifo), UINT32_C(0x7F0180FF));
            }
            const int8_t expected[] = {-1, -128, 1, 127};
            for (unsigned byte = 0; byte < sizeof(expected); byte++) {
                cupid_audio_timer_overflow(&gba->audio, 0, origins[epoch] + byte);
                CHECK(gba->audio.chA.latchedSample == expected[byte]);
                CHECK(gba->audio.chB.latchedSample == expected[byte]);
            }
            /* Rewriting the enabled bit retains queued words. */
            for (unsigned fifo = 0; fifo < 2; fifo++) store(gba, 4, address(fifo), 0x12345678);
            store(gba, width, GBA_BASE_IO + REG_SOUNDCNT_X, 0x80);
            cupid_audio_timer_overflow(&gba->audio, 0, origins[epoch] + 4);
            CHECK(gba->audio.chA.latchedSample == 0x78 && gba->audio.chB.latchedSample == 0x78);
            observations++;
            destroy(gba);
        }
    }
}

static void power_edges_during_dma_preserve_transfer_and_irq_progress(void) {
    const uint32_t payload[] = {0x04030201, 0x88776655, 0xFFEEDDCC, 0x12345678};
    for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(*origins); epoch++) {
        for (unsigned dma = 1; dma <= 2; dma++) {
            for (unsigned fifo = 0; fifo < 2; fifo++) {
                for (unsigned enabling = 0; enabling < 2; enabling++) {
                    struct CupidGBA* gba = machine(origins[epoch]);
                    cupid_io_write(gba, REG_SOUNDCNT_X, enabling ? 0 : 0x80);
                    memcpy(gba->memory.iwram, payload, sizeof(payload));
                    cupid_dma_write_sad(gba, dma, GBA_BASE_IWRAM);
                    cupid_dma_write_dad(gba, dma, address(fifo));
                    cupid_dma_write_cnt_lo(gba, dma, 4);
                    cupid_dma_write_cnt(gba, dma, 0xC440);
                    /* Change power after exactly one completed destination
                     * strobe. The accepted DMA keeps its remaining beats. */
                    unsigned clocks = 0;
                    while (gba->memory.dma[dma].nextSource == GBA_BASE_IWRAM && clocks < 16) {
                        cupid_timing_tick(&gba->timing, 1);
                        clocks++;
                    }
                    CHECK(gba->memory.dma[dma].nextSource == GBA_BASE_IWRAM + 4);
                    CHECK(channel(gba, fifo)->fifoWrite == (enabling ? 0 : 1));
                    cupid_io_write(gba, REG_SOUNDCNT_X, enabling ? 0x80 : 0);
                    cupid_timing_tick(&gba->timing, 32);
                    CHECK(!gba->memory.dmaRunning && !gba->memory.dma[dma].enable);
                    CHECK(gba->memory.dma[dma].nextSource == GBA_BASE_IWRAM + sizeof(payload));
                    CHECK(gba->irqf == (IRQ_DMA0 << dma));
                    CHECK(channel(gba, fifo)->fifoWrite == (enabling ? 3 : 0));
                    if (enabling) CHECK(!memcmp(channel(gba, fifo)->fifo, payload + 1, 3 * sizeof(*payload)));
                    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
                    cupid_audio_timer_overflow(&gba->audio, 0, origins[epoch] + clocks + 32);
                    CHECK(channel(gba, fifo)->latchedSample == (enabling ? 0x55 : 0));
                    observations++;
                    destroy(gba);
                }
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "writes")) RUN(powered_off_writes_do_not_enter_either_fifo);
    else if (!strcmp(argv[1], "cpu")) RUN(arm_and_thumb_stores_obey_the_sound_power_state);
    else if (!strcmp(argv[1], "dma")) RUN(immediate_dma_completes_even_when_fifo_writes_are_ignored);
    else if (!strcmp(argv[1], "phases")) RUN(power_edges_during_dma_preserve_transfer_and_irq_progress);
    else if (!strcmp(argv[1], "transitions")) RUN(power_transitions_discard_queued_words_and_accept_new_words);
    else return 2;
    printf("observations=%u\n", observations);
    return 0;
}
