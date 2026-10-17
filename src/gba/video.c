/**
 * @file video.c
 * @brief GBA video subsystem: event-driven HBlank and VBlank timing.
 *
 * Uses a single CupidTimingEvent whose callback alternates between
 * _startHblank and _startHdraw via the cycle-accurate timing scheduler.
 *
 * Timing overview:
 * - HBlank flag asserts 1007 clocks after line start
 * - The next line starts 225 clocks later
 * - Total per scanline: 1232 cycles
 * - Visible lines: 0-159
 * - VBlank lines: 160-227
 * - Total per frame: 228 * 1232 = 280896 cycles
 */

#include "cupid/gba.h"

/* Fast entry starts at the first cartridge issue, on line 126 at clock 841.
 * It keeps a relative epoch while matching that full-boot display phase. */
enum { FAST_ENTRY_HDRAW_REMAINING = 166 };

/**
 * @brief Forward declarations for timing callbacks.
 */
static void _startHblank(struct CupidTiming* timing, void* context, uint32_t cyclesLate);
static void _startHdraw(struct CupidTiming* timing, void* context, uint32_t cyclesLate);
static void _updateMatch(struct CupidTiming* timing, void* context, uint32_t cyclesLate);
static void _requestDisplay(struct CupidTiming* timing, void* context, uint32_t cyclesLate);

/**
 * @brief OBJ sprite sizes indexed by [shape * 4 + size][width, height].
 */
const int CupidObjSizes[16][2] = {
    /** Square */     { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 },
    /** Horizontal */ { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 },
    /** Vertical */   { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 },
    /** Invalid */    { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }
};

/**
 * @brief Initialize, reset, and deinitialize video state.
 */
void cupid_video_init(struct CupidVideo* video, struct CupidGBA* gba) {
    video->gba = gba;
    video->frameCallback = NULL;
    video->frameContext = NULL;
    if (!video->vram) video->vram = calloc(GBA_SIZE_VRAM / 2, sizeof(uint16_t));
    /** Palette and outputBuffer are inline arrays zeroed by reset. */

    /** Wire the timing event fields that remain constant. */
    video->event.name     = "GBA Video";
    video->event.context  = video;
    video->event.priority = 8;    /** Event priority. */
    video->event.callback = NULL; /** Set during reset. */
    video->event.next     = NULL;
    video->matchEvent = (struct CupidTimingEvent){.context = video,
        .callback = _updateMatch, .name = "GBA VCOUNT match", .priority = 8};
    video->displayEvent = (struct CupidTimingEvent){.context = video,
        .callback = _requestDisplay, .name = "GBA display DMA", .priority = 8};
}

void cupid_video_reset(struct CupidVideo* video) {
    struct CupidGBA* gba = video->gba;
    cupid_timing_deschedule(&gba->timing, &video->matchEvent);
    cupid_timing_deschedule(&gba->timing, &video->displayEvent);

    video->dispcnt = 0;
    video->dispstat = 0;
    video->vcount = 0;
    video->frameCounter = 0;
    video->displayDmaLatched = false;

    memset(video->bg, 0, sizeof(video->bg));
    memset(&video->blend, 0, sizeof(video->blend));
    memset(&video->win, 0, sizeof(video->win));
    memset(video->palette, 0, sizeof(video->palette));
    memset(video->vram, 0, GBA_SIZE_VRAM);
    memset(&video->oam, 0, sizeof(video->oam));
    memset(video->outputBuffer, 0, sizeof(video->outputBuffer));

    /** BG2 and BG3 affine defaults. */
    video->bg[2].pa = 0x100;
    video->bg[2].pd = 0x100;
    video->bg[3].pa = 0x100;
    video->bg[3].pd = 0x100;

    /**
     * Start in Hdraw, so the first event is _startHblank after
     * VIDEO_HDRAW_LENGTH cycles.
     */
    video->event.callback = _startHblank;
    cupid_timing_schedule(&gba->timing, &video->event, VIDEO_HDRAW_LENGTH);
    cupid_timing_schedule(&gba->timing, &video->matchEvent, 1);
}

/**
 * @brief Reschedule video timing for post-boot state when BIOS is skipped.
 */
void cupid_video_skip_bios(struct CupidVideo* video) {
    struct CupidGBA* gba = video->gba;
    video->vcount = 0x7E;
    cupid_timing_deschedule(&gba->timing, &video->event);
    video->event.callback = _startHblank;
    cupid_timing_schedule(&gba->timing, &video->event, FAST_ENTRY_HDRAW_REMAINING);
}

void cupid_video_deinit(struct CupidVideo* video) {
    video->frameCallback = NULL;
    video->frameContext = NULL;
    free(video->vram);  video->vram = NULL;
    /** Palette and outputBuffer are inline, no free needed. */
}

void cupid_video_set_frame_callback(struct CupidVideo* video,
    void (*callback)(void*, const uint32_t*, uint64_t), void* context) {
    video->frameCallback = callback;
    video->frameContext = context;
}

/**
 * @brief Event-driven video timing state machine.
 */

/**
 * @brief Hdraw ended and HBlank started.
 */
static void _startHblank(struct CupidTiming* timing, void* context, uint32_t cyclesLate) {
    struct CupidVideo* video = (struct CupidVideo*)context;
    struct CupidGBA*   gba   = video->gba;

    /**
     * Schedule _startHdraw after VIDEO_HBLANK_LENGTH.
     * Relative scheduling uses the event's original timestamp.
     */
    video->event.callback = _startHdraw;
    cupid_timing_schedule(timing, &video->event,
                          VIDEO_HBLANK_LENGTH);

    /** Set HBlank flag in DISPSTAT. */
    video->dispstat |= 0x0002;

    /** Render the scanline for visible lines only. */
    if (video->vcount < VIDEO_VDRAW_LINES) {
        cupid_render_scanline(gba, video->vcount);
    }

    /** Run HBlank DMA on visible lines only. */
    if (video->vcount < VIDEO_VDRAW_LINES) {
        cupid_dma_run_hblank(gba, -(int32_t)cyclesLate);
    }

    /** Raise HBlank IRQ when enabled. */
    if (video->dispstat & 0x0010) {
        cupid_raise_irq_at(gba, IRQ_HBLANK, cupid_timing_global_time(timing) + 1);
    }

    /** Write updated DISPSTAT back to IO mirror. */
    gba->memory.io[REG_DISPSTAT >> 1] = video->dispstat;
}

/**
 * @brief HBlank ended and a new Hdraw scanline started.
 */
static void _startHdraw(struct CupidTiming* timing, void* context, uint32_t cyclesLate) {
    struct CupidVideo* video = (struct CupidVideo*)context;
    struct CupidGBA*   gba   = video->gba;

    /** Schedule _startHblank after VIDEO_HDRAW_LENGTH. */
    video->event.callback = _startHblank;
    cupid_timing_schedule(timing, &video->event,
                          VIDEO_HDRAW_LENGTH);

    /* The display request mode is latched during the blanking interval. */
    if (video->vcount == 162) {
        struct CupidDMA* dma = &gba->memory.dma[3];
        video->displayDmaLatched = dma->enable && dma->timing == DMA_TIMING_CUSTOM;
    }

    /** Advance vcount and wrap 227 to 0. */
    ++video->vcount;
    if (video->vcount >= VIDEO_VERTICAL_TOTAL) {
        video->vcount = 0;
    }

    /** Update VCOUNT in IO. */
    gba->memory.io[REG_VCOUNT >> 1] = video->vcount;

    /** Clear HBlank flag. */
    video->dispstat &= ~0x0002;

    cupid_timing_schedule(timing, &video->matchEvent, 1);
    if (video->displayDmaLatched) {
        if (video->vcount == 162) {
            cupid_dma_write_cnt(gba, 3, gba->memory.dma[3].reg & 0x7FFF);
        } else if (video->vcount >= 2 && video->vcount < 162) {
            cupid_timing_schedule(timing, &video->displayEvent, 3);
        }
    }

    /** Per-scanline actions. */
    if (video->vcount == VIDEO_VDRAW_LINES) {
        /** Entering VBlank at vcount 160. */
        video->dispstat |= 0x0001;  /** InVblank flag. */

        /** Latch affine reference points (sx and sy from refx and refy). */
        video->bg[2].sx = video->bg[2].refx;
        video->bg[2].sy = video->bg[2].refy;
        video->bg[3].sx = video->bg[3].refx;
        video->bg[3].sy = video->bg[3].refy;

        /** Run VBlank DMA. */
        cupid_dma_run_vblank(gba, -(int32_t)cyclesLate);

        /** Raise VBlank IRQ when enabled. */
        if (video->dispstat & 0x0008) {
            cupid_raise_irq_at(gba, IRQ_VBLANK, cupid_timing_global_time(timing) + 1);
        }

        /** Signal frame complete. */
        video->frameCounter++;
        gba->frameReady = true;

    } else if (video->vcount == VIDEO_VERTICAL_TOTAL - 1) {
        /** Last VBlank line at vcount 227; clear InVblank before wrap. */
        video->dispstat &= ~0x0001;
    }

    /** Write updated DISPSTAT back to IO mirror. */
    gba->memory.io[REG_DISPSTAT >> 1] = video->dispstat;
    if (video->vcount == VIDEO_VDRAW_LINES && video->frameCallback)
        video->frameCallback(video->frameContext, video->outputBuffer, cupid_timing_global_time(timing));
}

void cupid_video_update_match(struct CupidVideo* video) {
    bool previous = (video->dispstat & 4) != 0;
    bool match = video->vcount == (video->dispstat >> 8);
    video->dispstat = (video->dispstat & ~4u) | (match ? 4 : 0);
    video->gba->memory.io[REG_DISPSTAT >> 1] = video->dispstat;
    if (match && !previous && (video->dispstat & 0x20)) {
        cupid_raise_irq_at(video->gba, IRQ_VCOUNT,
            cupid_timing_global_time(&video->gba->timing) + 1);
    }
}

static void _updateMatch(struct CupidTiming* timing, void* context, uint32_t cyclesLate) {
    (void)timing;
    (void)cyclesLate;
    cupid_video_update_match(context);
}

static void _requestDisplay(struct CupidTiming* timing, void* context, uint32_t cyclesLate) {
    (void)timing;
    (void)cyclesLate;
    struct CupidVideo* video = context;
    cupid_dma_run_display(video->gba);
}

/**
 * @brief Register read and write hooks used by IO dispatch.
 */
void cupid_video_write_register(struct CupidVideo* video, uint32_t addr, uint16_t val) {
    /** Most writes are handled by io.c dispatch; this handles side effects. */
    (void)video; (void)addr; (void)val;
}

uint16_t cupid_video_read_register(struct CupidVideo* video, uint32_t addr) {
    (void)video; (void)addr;
    return 0;
}

void cupid_video_write_palette(struct CupidVideo* video, uint32_t offset, uint16_t val) {
    if (offset < 512) {
        video->palette[offset] = val;
    }
}

void cupid_video_write_vram(struct CupidVideo* video, uint32_t offset, uint16_t val) {
    if (offset < GBA_SIZE_VRAM / 2) video->vram[offset] = val;
}

void cupid_video_write_oam(struct CupidVideo* video, uint32_t offset, uint16_t val) {
    if (offset < 512) video->oam.raw[offset] = val;
}
