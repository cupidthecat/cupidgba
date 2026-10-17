/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../test.h"

static struct CupidGBA* machine(uint64_t clock, unsigned line) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    cupid_video_reset(&gba->video);
    gba->video.vcount = (uint16_t)line;
    gba->memory.io[REG_VCOUNT >> 1] = (uint16_t)line;
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void tick(struct CupidGBA* gba, unsigned clocks) {
    CHECK(gba->cpu.cycles == 0);
    cupid_timing_tick(&gba->timing, (int32_t)clocks);
}

static void flags_and_irqs_have_separate_deadlines(void) {
    struct CupidGBA* gba = machine(0, 0);
    cupid_io_write(gba, REG_DISPSTAT, 0x0130);
    tick(gba, 1006);
    CHECK(cupid_io_read(gba, REG_DISPSTAT) == 0x0130);
    tick(gba, 1);
    CHECK(cupid_io_read(gba, REG_DISPSTAT) == 0x0132 && gba->irqf == 0);
    tick(gba, 1);
    CHECK(gba->irqf == 0);
    tick(gba, 1);
    CHECK(gba->irqf == IRQ_HBLANK);
    cupid_io_write(gba, REG_IF, 0xFFFF);
    tick(gba, 222);
    CHECK(gba->video.vcount == 0 && (gba->video.dispstat & 2));
    tick(gba, 1);
    CHECK(cupid_io_read(gba, REG_VCOUNT) == 1);
    CHECK(cupid_io_read(gba, REG_DISPSTAT) == 0x0130);
    tick(gba, 1);
    CHECK(cupid_io_read(gba, REG_DISPSTAT) == 0x0134 && gba->irqf == 0);
    tick(gba, 1);
    CHECK(gba->irqf == 0);
    tick(gba, 1);
    CHECK(gba->irqf == IRQ_VCOUNT);
    destroy(gba);

    gba = machine(0, 159);
    cupid_io_write(gba, REG_DISPSTAT, 0xA038);
    tick(gba, 1230);
    cupid_io_write(gba, REG_IF, 0xFFFF);
    tick(gba, 2);
    CHECK(gba->video.vcount == 160 && (gba->video.dispstat & 7) == 1);
    CHECK(gba->irqf == 0 && gba->frameReady);
    tick(gba, 1);
    CHECK((gba->video.dispstat & 7) == 5 && gba->irqf == 0);
    tick(gba, 1);
    CHECK(gba->irqf == IRQ_VBLANK);
    tick(gba, 1);
    CHECK(gba->irqf == (IRQ_VBLANK | IRQ_VCOUNT));
    destroy(gba);
}

static void match_writes_masks_and_frame_edges(void) {
    struct CupidGBA* gba = machine(0, 7);
    cupid_io_write(gba, REG_DISPSTAT, 0x07FF);
    CHECK(cupid_io_read(gba, REG_DISPSTAT) == 0x073C);
    tick(gba, 2);
    CHECK(gba->irqf == IRQ_VCOUNT);
    cupid_io_write(gba, REG_IF, IRQ_VCOUNT);
    tick(gba, 1);
    cupid_io_write8(gba, REG_DISPSTAT, 0x20);
    tick(gba, 2);
    CHECK(gba->irqf == 0); /* Enabling an already matching target creates no new edge. */
    cupid_io_write8(gba, REG_DISPSTAT + 1, 8);
    CHECK(!(gba->video.dispstat & 4));
    cupid_io_write8(gba, REG_DISPSTAT + 1, 7);
    CHECK(gba->video.dispstat & 4);
    tick(gba, 2);
    CHECK(gba->irqf == IRQ_VCOUNT);
    destroy(gba);

    gba = machine(0, 226);
    gba->video.dispstat = 1;
    tick(gba, 1232);
    CHECK(gba->video.vcount == 227 && !(gba->video.dispstat & 1));
    tick(gba, 1232);
    CHECK(gba->video.vcount == 0 && !(gba->video.dispstat & 7));
    tick(gba, 1);
    CHECK((gba->video.dispstat & 7) == 4);
    destroy(gba);
}

static void display_dma_latches_before_the_next_frame_window(void) {
    struct CupidGBA* gba = machine(0, 162);
    cupid_memory_store16(&gba->cpu, GBA_BASE_IWRAM, 0x1234, NULL);
    cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM);
    cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM);
    cupid_dma_write_cnt_lo(gba, 3, 1);
    cupid_dma_write_cnt(gba, 3, 0xB200);
    tick(gba, 1232);
    CHECK(gba->video.vcount == 163 && gba->video.displayDmaLatched);
    CHECK(!gba->memory.dmaRunning);
    tick(gba, (228 - 163 + 2) * 1232);
    CHECK(gba->video.vcount == 2 && !gba->memory.dmaRunning);
    CHECK(cupid_timing_is_scheduled(&gba->timing, &gba->video.displayEvent));
    tick(gba, 4);
    CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].ready);
    tick(gba, 1);
    CHECK(gba->memory.dmaRunning);
    tick(gba, 6);
    CHECK(cupid_memory_load16(&gba->cpu, GBA_BASE_EWRAM, NULL) == 0x1234);
    CHECK(gba->memory.dma[3].enable && !gba->memory.dmaRunning);
    tick(gba, 160 * 1232 - 11);
    CHECK(gba->video.vcount == 162 && !gba->memory.dma[3].enable);
    tick(gba, 1232);
    CHECK(!gba->video.displayDmaLatched);
    cupid_timing_schedule(&gba->timing, &gba->video.displayEvent, 3);
    cupid_video_reset(&gba->video);
    CHECK(!gba->video.displayDmaLatched);
    CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->video.displayEvent));
    destroy(gba);
}

static void late_dispatch_matches_clock_steps_across_wrap(void) {
    const uint64_t clocks[] = {0, UINT32_MAX - 1000u, UINT64_C(0x300000000)};
    for (unsigned c = 0; c < 3; c++) {
        struct CupidGBA* late = machine(clocks[c], 159);
        struct CupidGBA* punctual = machine(clocks[c], 159);
        struct CupidGBA* machines[] = {late, punctual};
        for (unsigned i = 0; i < 2; i++) {
            struct CupidGBA* gba = machines[i];
            cupid_io_write(gba, REG_DISPSTAT, 0xA038);
            cupid_io_write(gba, REG_TM0CNT_L, 0xFFF0);
            cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
            cupid_dma_write_sad(gba, 0, GBA_BASE_IO + REG_TM0CNT_L);
            cupid_dma_write_dad(gba, 0, GBA_BASE_EWRAM);
            cupid_dma_write_cnt_lo(gba, 0, 1);
            cupid_dma_write_cnt(gba, 0, 0xA000);
        }
        tick(late, 10000);
        for (unsigned i = 0; i < 10000; i++) tick(punctual, 1);
        CHECK(late->video.vcount == punctual->video.vcount);
        CHECK(late->video.dispstat == punctual->video.dispstat);
        CHECK(late->video.event.when == punctual->video.event.when);
        CHECK(late->video.matchEvent.when == punctual->video.matchEvent.when);
        CHECK(late->irqf == punctual->irqf && late->irq.line == punctual->irq.line);
        CHECK(!memcmp(late->memory.ewram, punctual->memory.ewram, 2));
        CHECK(late->memory.dma[0].busyUntil == punctual->memory.dma[0].busyUntil);
        CHECK(cupid_io_read(late, REG_TM0CNT_L) == cupid_io_read(punctual, REG_TM0CNT_L));
        destroy(late);
        destroy(punctual);
    }
}

struct FrameCapture {
    struct CupidGBA* gba;
    uint64_t nextClock;
    unsigned calls;
    uint32_t pixels[GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT];
};

static void capture_image(void* context, const uint32_t* pixels, uint64_t clock) {
    struct FrameCapture* frame = context;
    CHECK(clock == frame->nextClock && clock == cupid_timing_global_time(&frame->gba->timing));
    CHECK(frame->gba->video.vcount == VIDEO_VDRAW_LINES && frame->gba->frameReady);
    CHECK(frame->gba->memory.io[REG_VCOUNT >> 1] == VIDEO_VDRAW_LINES);
    CHECK(frame->gba->memory.io[REG_DISPSTAT >> 1] & 1);
    frame->nextClock += VIDEO_TOTAL_LENGTH;
    ++frame->calls;
    memcpy(frame->pixels, pixels, sizeof(frame->pixels));
}

static void image_is(const uint32_t* pixels, uint16_t color) {
    for (unsigned i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++i)
        CHECK(pixels[i] == color16_to_rgba(color));
}

static void completed_images_survive_drawing_stop_late_dispatch_and_reset(void) {
    const uint64_t epochs[] = {0, UINT32_MAX - UINT64_C(512)};
    for (unsigned epoch = 0; epoch < sizeof(epochs) / sizeof(*epochs); ++epoch) {
        struct CupidGBA* gba = machine(epochs[epoch], 225);
        struct FrameCapture* frame = calloc(1, sizeof(*frame)); CHECK(frame);
        frame->gba = gba;
        cupid_io_write(gba, REG_DISPCNT, 0x0403);
        for (unsigned i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++i) gba->video.vram[i] = 0x001F;
        /* Settle all three display latches during VBlank before asserting a
         * uniform completed image. No visible line or frame handoff is skipped. */
        tick(gba, 3 * VIDEO_HORIZONTAL_LENGTH);
        CHECK(gba->video.vcount == 0);
        frame->nextClock = cupid_timing_global_time(&gba->timing) + VIDEO_VDRAW_LINES * VIDEO_HORIZONTAL_LENGTH;
        cupid_video_set_frame_callback(&gba->video, capture_image, frame);
        gba->video.frameCounter = UINT32_MAX;
        tick(gba, VIDEO_TOTAL_LENGTH);
        CHECK(frame->calls == 1 && gba->video.frameCounter == 0);
        image_is(frame->pixels, 0x001F);
        for (unsigned i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++i) gba->video.vram[i] = 0x03E0;
        tick(gba, VIDEO_HORIZONTAL_LENGTH * 80);
        CHECK(frame->calls == 1);
        /* The next visible frame is a live-buffer negative control. Its top
         * has changed, while the published complete image remains intact. */
        CHECK(gba->video.outputBuffer[0] == color16_to_rgba(0x03E0));
        CHECK(gba->video.outputBuffer[(GBA_SCREEN_HEIGHT - 1) * GBA_SCREEN_WIDTH] == color16_to_rgba(0x001F));
        image_is(frame->pixels, 0x001F);
        cupid_gba_stop(gba); CHECK(gba->stopped);
        const uint64_t stoppedClock = cupid_timing_global_time(&gba->timing);
        cupid_gba_run_interval(gba, VIDEO_TOTAL_LENGTH);
        CHECK(gba->stopped && cupid_timing_global_time(&gba->timing) == stoppedClock && frame->calls == 1);
        image_is(frame->pixels, 0x001F);
        gba->stopped = false;
        tick(gba, VIDEO_TOTAL_LENGTH);
        CHECK(frame->calls == 2);
        image_is(frame->pixels, 0x03E0);
        tick(gba, VIDEO_TOTAL_LENGTH * 2);
        CHECK(frame->calls == 4 && gba->video.frameCounter == 3);
        image_is(frame->pixels, 0x03E0);
        cupid_gba_reset(gba);
        CHECK(gba->video.frameCallback == capture_image && gba->video.frameContext == frame);
        cupid_timing_clear(&gba->timing);
        gba->timing.globalCycles = epochs[epoch];
        gba->timing.masterCycles = (uint32_t)epochs[epoch];
        cupid_video_reset(&gba->video);
        gba->video.vcount = 225;
        gba->memory.io[REG_VCOUNT >> 1] = 225;
        frame->nextClock = epochs[epoch] + (3 + VIDEO_VDRAW_LINES) * VIDEO_HORIZONTAL_LENGTH;
        cupid_io_write(gba, REG_DISPCNT, 0x0403);
        for (unsigned i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++i) gba->video.vram[i] = 0x001F;
        tick(gba, 3 * VIDEO_HORIZONTAL_LENGTH);
        CHECK(gba->video.vcount == 0 && frame->calls == 4);
        tick(gba, VIDEO_TOTAL_LENGTH);
        CHECK(frame->calls == 5);
        image_is(frame->pixels, 0x001F);
        cupid_video_set_frame_callback(&gba->video, NULL, NULL);
        for (unsigned i = 0; i < GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT; ++i) gba->video.vram[i] = 0x03E0;
        tick(gba, VIDEO_TOTAL_LENGTH);
        CHECK(frame->calls == 5);
        image_is(frame->pixels, 0x001F);
        destroy(gba); free(frame);
    }
}

int main(void) {
    RUN(completed_images_survive_drawing_stop_late_dispatch_and_reset);
    RUN(flags_and_irqs_have_separate_deadlines);
    RUN(match_writes_masks_and_frame_edges);
    RUN(display_dma_latches_before_the_next_frame_window);
    RUN(late_dispatch_matches_clock_steps_across_wrap);
    return 0;
}
