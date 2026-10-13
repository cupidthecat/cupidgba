/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"

static void reset_and_frame_clock(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba != NULL);
    cupid_gba_create(gba);
    /* An ARM branch-to-self exercises video/audio events without a ROM. */
    const uint32_t branch = 0xEAFFFFFE;
    cupid_gba_reset(gba);
    memcpy(gba->memory.ewram, &branch, sizeof(branch));
    gba->cpu.gprs[ARM_PC] = GBA_BASE_EWRAM;
    CupidARMWritePC(&gba->cpu);
    cupid_gba_run_frame(gba);
    uint64_t first = cupid_timing_global_time(&gba->timing);
    uint32_t firstDeadline = gba->video.event.when;
    CHECK(first > 0 && gba->video.vcount == 160);
    cupid_gba_run_frame(gba);
    uint64_t second = cupid_timing_global_time(&gba->timing);
    /* CPU observations occur at instruction boundaries; video deadlines do not. */
    CHECK(gba->video.event.when - firstDeadline == 280896);
    CHECK(second > first);
    cupid_gba_reset(gba);
    memcpy(gba->memory.ewram, &branch, sizeof(branch));
    gba->cpu.gprs[ARM_PC] = GBA_BASE_EWRAM;
    CupidARMWritePC(&gba->cpu);
    CHECK(cupid_timing_global_time(&gba->timing) == 0);
    cupid_gba_run_frame(gba);
    CHECK(cupid_timing_global_time(&gba->timing) == first);
    cupid_gba_destroy(gba);
    free(gba);
}

static void io_dispatch_bounds_and_halt(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba != NULL);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_io_write(gba, REG_BG2PD, 0x1357);
    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + 0x436, 0x12, NULL);
    cupid_memory_store16(&gba->cpu, GBA_BASE_IO + 0x436, 0x2468, NULL);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IO + 0x434, 0x12345678, NULL);
    CHECK(gba->video.bg[2].pd == 0x1357);
    gba->cpu.prefetch[1] = 0x12345678;
    CHECK(cupid_memory_load32(&gba->cpu, GBA_BASE_IO + 0x400, NULL) == 0x12345678);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_IO + 0x402, NULL) == 0x1234);
    CHECK(cupid_memory_load8(&gba->cpu, GBA_BASE_IO + 0x403, NULL) == 0x12);
    /* HALTCNT is a byte register, and must reach byte dispatch on the bus. */
    gba->memory.activeRegion = GBA_REGION_BIOS;
    cupid_memory_store8(&gba->cpu, GBA_BASE_IO + REG_HALTCNT, 0, NULL);
    CHECK(gba->cpu.halted);
    cupid_gba_destroy(gba);
    free(gba);
}

static void signed_fifo_samples(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba != NULL);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    gba->audio.chALeft = true;
    gba->audio.chBRight = true;
    gba->audio.volumeChA = true;
    gba->audio.volumeChB = true;
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IO + REG_FIFO_A_LO, 0x80, NULL);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IO + REG_FIFO_B_LO, 0xFF, NULL);
    cupid_audio_timer_overflow(&gba->audio, 0, 0);
    cupid_audio_sample(&gba->audio, gba->audio.lastSample + 512);
    CHECK(gba->audio.currentSamples[0].left == -24576);
    CHECK(gba->audio.currentSamples[0].right == -192);
    cupid_gba_destroy(gba);
    free(gba);
}

static void affine_sign_extension(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba != NULL);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_io_write(gba, REG_BG2X_LO, 0xFFFF);
    cupid_io_write(gba, REG_BG2X_HI, 0xFFFF);
    CHECK(gba->video.bg[2].refx == -1 && gba->video.bg[2].sx == -1);
    cupid_io_write(gba, REG_BG2Y_LO, 0);
    cupid_io_write(gba, REG_BG2Y_HI, 0xF800);
    CHECK(gba->video.bg[2].refy == -0x08000000);
    cupid_io_write(gba, REG_BG3X_HI, 0xF7FF);
    CHECK(gba->video.bg[3].refx == 0x07FF0000);
    cupid_io_write(gba, REG_BG3Y_HI, 0xFFFF);
    CHECK(gba->video.bg[3].refy == -65536);
    cupid_gba_destroy(gba);
    free(gba);
}

int main(void) {
    RUN(reset_and_frame_clock);
    RUN(io_dispatch_bounds_and_halt);
    RUN(signed_fifo_samples);
    RUN(affine_sign_extension);
    return 0;
}
