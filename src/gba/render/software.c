/**
 * @file software.c
 * @brief Per-scanline renderer entry point: window setup, layer dispatch, and compositing.
 *
 * This is the main visible-scanline renderer called from video timing.
 * For each scanline it:
 *   1. Builds window spans to determine which layers are visible where.
 *   2. Fills the scanline with the backdrop colour.
 *   3. Renders sprites into the OBJ staging buffers.
 *   4. For each window span and priority level, composites OBJ and BG layers.
 *   5. Applies any remaining blend effects and converts the scanline to RGBA.
 *   6. Advances affine background coordinates for the next scanline.
 */

#include "cupid/gba.h"
#include <stdio.h>

/* ---------- Video debug (compile with -DCUPID_VIDEO_DEBUG=1) ---------- */
#ifndef CUPID_VIDEO_DEBUG
#define CUPID_VIDEO_DEBUG 0
#endif

#if CUPID_VIDEO_DEBUG
static int _vdbg_frame = 0;
static uint16_t _vdbg_last_dispcnt = 0xFFFF;
#define VIDEO_DBG(y, maxframe, line_interval, fmt, ...) do { \
    if (_vdbg_frame < (maxframe) && (y) % (line_interval) == 0) { \
        fprintf(stderr, "[VID f%d y%d] " fmt "\n", _vdbg_frame, (y), ##__VA_ARGS__); \
    } \
} while (0)
/* Log when DISPCNT changes (any frame, line 0 only) */
#define VIDEO_DBG_DISPCNT_CHANGE(dispcnt, y) do { \
    if ((y) == 0 && (dispcnt) != _vdbg_last_dispcnt) { \
        fprintf(stderr, "[VID f%d] DISPCNT changed: 0x%04X -> 0x%04X\n", \
                _vdbg_frame, _vdbg_last_dispcnt, (dispcnt)); \
        _vdbg_last_dispcnt = (dispcnt); \
    } \
} while (0)
#else
#define VIDEO_DBG(y, maxframe, line_interval, fmt, ...) ((void)0)
#define VIDEO_DBG_DISPCNT_CHANGE(dispcnt, y) ((void)0)
#endif

/**
 * @brief Fill a scanline span with the backdrop pixel and optional blend state.
 *
 * Implemented in @c composite.c.
 *
 * @param gba       GBA context.
 * @param startX    First pixel column to initialise (inclusive).
 * @param endX      Last pixel column to initialise (exclusive).
 * @param target1Bd Whether the backdrop should be marked as blend target 1.
 */
void cupid_render_preprocess_buffer(struct CupidGBA* gba, int startX, int endX,
                                    bool target1Bd);

/**
 * @brief Build horizontal window spans for scanline @p y.
 *
 * Each span stores a half-open X range and a control structure describing
 * which BG layers, OBJ pixels, and blend effects are enabled within that
 * region.
 *
 * Window priority is resolved as: @c WIN0 > @c WIN1 > @c OBJ_WIN > @c WINOUT.
 *
 * @param gba   GBA context.
 * @param y     Scanline row to evaluate.
 * @param spans Output array of window spans for that scanline.
 * @return Number of valid entries written to @p spans.
 */
int cupid_render_build_windows(struct CupidGBA* gba, int y,
                               struct CupidWinSpan spans[MAX_WIN_SPANS]) {
    struct CupidVideo* v = &gba->video;
    uint16_t dispcnt = v->dispcnt;

    bool useWin0   = DISPCNT_WIN0_ENABLE(dispcnt);
    bool useWin1   = DISPCNT_WIN1_ENABLE(dispcnt);
    bool useObjWin = DISPCNT_OBJWIN_ENABLE(dispcnt);

    if (!useWin0 && !useWin1 && !useObjWin) {
        /* No windows active — entire scanline uses all layers, all effects */
        spans[0].startX = 0;
        spans[0].endX = GBA_SCREEN_WIDTH;
        for (int i = 0; i < 4; i++) spans[0].ctrl.bg[i] = true;
        spans[0].ctrl.obj = true;
        spans[0].ctrl.blend = true;
        return 1;
    }

    /* Decode window controls:
     * index 0=WINOUT (outside), 1=WIN0, 2=WIN1, 3=OBJWIN (WINOUT high byte).
     */
    struct CupidWinCtrl ctrls[4];

    uint8_t wout = v->win.winout & 0x3F;
    for (int i = 0; i < 4; i++) ctrls[0].bg[i] = WINCTRL_BG_ENABLE(wout, i);
    ctrls[0].obj   = WINCTRL_OBJ_ENABLE(wout);
    ctrls[0].blend = WINCTRL_BLEND_ENABLE(wout);

    uint8_t w0c = v->win.winin & 0x3F;
    for (int i = 0; i < 4; i++) ctrls[1].bg[i] = WINCTRL_BG_ENABLE(w0c, i);
    ctrls[1].obj   = WINCTRL_OBJ_ENABLE(w0c);
    ctrls[1].blend = WINCTRL_BLEND_ENABLE(w0c);

    uint8_t w1c = (v->win.winin >> 8) & 0x3F;
    for (int i = 0; i < 4; i++) ctrls[2].bg[i] = WINCTRL_BG_ENABLE(w1c, i);
    ctrls[2].obj   = WINCTRL_OBJ_ENABLE(w1c);
    ctrls[2].blend = WINCTRL_BLEND_ENABLE(w1c);

    uint8_t owc = (v->win.winout >> 8) & 0x3F;
    for (int i = 0; i < 4; i++) ctrls[3].bg[i] = WINCTRL_BG_ENABLE(owc, i);
    ctrls[3].obj   = WINCTRL_OBJ_ENABLE(owc);
    ctrls[3].blend = WINCTRL_BLEND_ENABLE(owc);

    /* Per-pixel window assignment: 0=WINOUT, 1=WIN0, 2=WIN1, 3=OBJWIN.
     * Apply in reverse priority order so WIN0 (highest) overwrites WIN1. */
    uint8_t winId[GBA_SCREEN_WIDTH];
    memset(winId, 0, sizeof(winId));

    for (int win = 1; win >= 0; win--) {
        bool useWin = (win == 0) ? useWin0 : useWin1;
        if (!useWin) continue;

        /* Vertical range check */
        int vTop = v->win.v[win][0];
        int vBot = v->win.v[win][1];
        bool vInRange;
        if (vTop <= vBot) {
            vInRange = (y >= vTop && y < vBot);
        } else {
            vInRange = (y >= vTop || y < vBot);
        }
        if (!vInRange) continue;

        /* Horizontal range */
        int hLeft  = v->win.h[win][0];
        int hRight = v->win.h[win][1];
        if (hLeft > GBA_SCREEN_WIDTH) hLeft = GBA_SCREEN_WIDTH;
        if (hRight > GBA_SCREEN_WIDTH) hRight = GBA_SCREEN_WIDTH;

        uint8_t id = (uint8_t)(win + 1);
        if (hLeft <= hRight) {
            for (int x = hLeft; x < hRight; x++) winId[x] = id;
        } else {
            /* Wrapping: [0, hRight) ∪ [hLeft, 240) */
            for (int x = 0; x < hRight; x++) winId[x] = id;
            for (int x = hLeft; x < GBA_SCREEN_WIDTH; x++) winId[x] = id;
        }
    }

    /* Apply OBJ window only where WIN0/WIN1 are not already active.
     * OBJ-window mask is produced by sprite rendering in scanline FLAG_OBJWIN.
     */
    if (useObjWin) {
        for (int x = 0; x < GBA_SCREEN_WIDTH; x++) {
            if (winId[x] == 0 && (v->scanline[x] & FLAG_OBJWIN)) {
                winId[x] = 3;
            }
        }
    }

    /* Coalesce adjacent pixels with the same window ID into spans */
    int nSpans = 0;
    int spanStart = 0;
    uint8_t prevId = winId[0];

    for (int x = 1; x <= GBA_SCREEN_WIDTH; x++) {
        uint8_t id = (x < GBA_SCREEN_WIDTH) ? winId[x] : 0xFF;
        if (id != prevId) {
            if (nSpans < MAX_WIN_SPANS) {
                spans[nSpans].startX = spanStart;
                spans[nSpans].endX = x;
                spans[nSpans].ctrl = ctrls[prevId];
                nSpans++;
            }
            spanStart = x;
            prevId = id;
        }
    }

    return nSpans;
}

/**
 * @brief Build packed compositing flags for a background layer.
 *
 * Encodes priority, layer index, background marker, and blend target bits.
 *
 * @param v  Video state.
 * @param bg Background layer index.
 * @return Packed compositing flags for the specified background.
 */
static inline uint32_t _buildBGFlags(struct CupidVideo* v, int bg) {
    uint16_t bldcnt = v->blend.bldcnt;
    int priority = BGCNT_PRIORITY(v->bg[bg].cnt);
    bool t1 = BLDCNT_TARGET1_BG(bldcnt, bg);
    bool t2 = BLDCNT_TARGET2_BG(bldcnt, bg);

    uint32_t flags = ((uint32_t)priority << OFFSET_PRIORITY)
                   | ((uint32_t)bg << OFFSET_INDEX)
                   | FLAG_IS_BACKGROUND;
    if (t1) flags |= FLAG_TARGET_1;
    if (t2) flags |= FLAG_TARGET_2;
    return flags;
}

/**
 * @brief Render one visible scanline into the output framebuffer.
 *
 * Handles forced blank, window span construction, OBJ staging, per-priority
 * BG/OBJ compositing, final postprocessing, and affine background stepping.
 *
 * @param gba GBA context.
 * @param y   Visible scanline row to render.
 */
void cupid_render_scanline(struct CupidGBA* gba, int y) {
    if (y < 0 || y >= GBA_SCREEN_HEIGHT) return;

    struct CupidVideo* v = &gba->video;
    uint16_t dispcnt = v->dispcnt;
    int mode = DISPCNT_MODE(dispcnt);

#if CUPID_VIDEO_DEBUG
    bool _dispcnt_changed = (y == 0 && dispcnt != _vdbg_last_dispcnt);
    if (_dispcnt_changed) {
        fprintf(stderr, "[VID f%d] DISPCNT changed: 0x%04X -> 0x%04X\n",
                _vdbg_frame, _vdbg_last_dispcnt, dispcnt);
        _vdbg_last_dispcnt = dispcnt;
    }
#endif

    /* Forced blank: white screen */
    if (DISPCNT_FORCED_BLANK(dispcnt)) {
        uint32_t* outRow = &v->outputBuffer[y * GBA_SCREEN_WIDTH];
        for (int x = 0; x < GBA_SCREEN_WIDTH; x++) {
            outRow[x] = 0xFFFFFFFF; /* white */
        }
        return;
    }

    /* Determine which BG layers are active for this video mode */
    bool bgActive[4] = {false, false, false, false};
    bool showOBJ = DISPCNT_OBJ_ENABLE(dispcnt);

    switch (mode) {
    case 0:
        for (int i = 0; i < 4; i++)
            bgActive[i] = DISPCNT_BG_ENABLE(dispcnt, i);
        break;
    case 1:
        bgActive[0] = DISPCNT_BG0_ENABLE(dispcnt);
        bgActive[1] = DISPCNT_BG1_ENABLE(dispcnt);
        bgActive[2] = DISPCNT_BG2_ENABLE(dispcnt);
        break;
    case 2:
        bgActive[2] = DISPCNT_BG2_ENABLE(dispcnt);
        bgActive[3] = DISPCNT_BG3_ENABLE(dispcnt);
        break;
    case 3: case 4: case 5:
        bgActive[2] = DISPCNT_BG2_ENABLE(dispcnt);
        break;
    }

    /* Debug: log video state on first 5 frames OR when DISPCNT changes */
#if CUPID_VIDEO_DEBUG
    {
        /* Log state once per frame at y=80 and y=130, every 50 frames after frame 50 */
        bool _logThis = ((y == 80 || y == 130) && _vdbg_frame >= 50 && (_vdbg_frame % 50 == 0));
        if (_logThis) {
            fprintf(stderr, "[VID f%d y%d] mode=%d dispcnt=0x%04X bg[%d,%d,%d,%d] obj=%d bld=0x%04X bldy=0x%04X backdrop=0x%04X\n",
                    _vdbg_frame, y, mode, dispcnt,
                    bgActive[0], bgActive[1], bgActive[2], bgActive[3], showOBJ,
                    v->blend.bldcnt, v->blend.bldy, v->palette[0] & 0x7FFF);
            fprintf(stderr, "[VID f%d y%d]   windows: n=%d WIN0=%d(h:%d-%d v:%d-%d) WIN1=%d(h:%d-%d v:%d-%d) OBJWIN=%d winin=0x%04X winout=0x%04X\n",
                    _vdbg_frame, y, nSpans,
                    DISPCNT_WIN0_ENABLE(dispcnt),
                    v->win.h[0][0], v->win.h[0][1], v->win.v[0][0], v->win.v[0][1],
                    DISPCNT_WIN1_ENABLE(dispcnt),
                    v->win.h[1][0], v->win.h[1][1], v->win.v[1][0], v->win.v[1][1],
                    DISPCNT_OBJWIN_ENABLE(dispcnt),
                    v->win.winin, v->win.winout);
            for (int _s = 0; _s < nSpans; _s++) {
                fprintf(stderr, "[VID f%d y%d]   span[%d]: x=%d..%d bg[%d,%d,%d,%d] obj=%d blend=%d\n",
                        _vdbg_frame, y, _s, spans[_s].startX, spans[_s].endX,
                        spans[_s].ctrl.bg[0], spans[_s].ctrl.bg[1],
                        spans[_s].ctrl.bg[2], spans[_s].ctrl.bg[3],
                        spans[_s].ctrl.obj, spans[_s].ctrl.blend);
            }
            /* Count visible OBJ pixels at this scanline */
            int objPixCount = 0;
            for (int x = 0; x < GBA_SCREEN_WIDTH; x++) {
                if ((v->objBuffer[x] & FLAG_UNWRITTEN) != FLAG_UNWRITTEN) objPixCount++;
            }
            fprintf(stderr, "[VID f%d y%d]   OBJ pixels at y=%d: %d/240\n",
                    _vdbg_frame, y, y, objPixCount);
            /* Check output: count non-black pixels */
            uint32_t* outRow = &v->outputBuffer[y * GBA_SCREEN_WIDTH];
            int nonBlack = 0;
            for (int x = 0; x < GBA_SCREEN_WIDTH; x++) {
                if ((outRow[x] & 0x00FFFFFF) != 0) nonBlack++;
            }
            fprintf(stderr, "[VID f%d y%d]   output: %d/240 non-black\n",
                    _vdbg_frame, y, nonBlack);
        }
    }
#endif

    /* Precompute BG flags */
    uint32_t bgFlags[4];
    for (int i = 0; i < 4; i++) {
        bgFlags[i] = bgActive[i] ? _buildBGFlags(v, i) : 0;
    }

    /* Blend state */
    bool target1Bd = BLDCNT_TARGET1_BD(v->blend.bldcnt);
    int blendEffect = BLDCNT_EFFECT(v->blend.bldcnt);

    /* Clear transient scanline flags used by window masking. */
    for (int x = 0; x < GBA_SCREEN_WIDTH; x++) {
        v->scanline[x] = 0;
    }

    /* Render sprites into objBuffer/objPrio and OBJWIN mask bits. */
    if (showOBJ) {
        cupid_render_sprites(gba, y);
    } else {
        for (int x = 0; x < GBA_SCREEN_WIDTH; x++) {
            v->objBuffer[x] = FLAG_UNWRITTEN;
            v->objPrio[x] = 0xFFFFFFFF;
        }
    }

    /* Build window spans for this scanline after OBJWIN mask is ready. */
    struct CupidWinSpan spans[MAX_WIN_SPANS];
    int nSpans = cupid_render_build_windows(gba, y, spans);

    /* For each window span, render layers by priority.
     * The order is: for each window, for each priority (0-3),
     * postprocess sprites at that priority, then draw BGs at that priority.
     */
    for (int w = 0; w < nSpans; w++) {
        int startX = spans[w].startX;
        int endX   = spans[w].endX;
        struct CupidWinCtrl* ctrl = &spans[w].ctrl;
        bool blendEnabled = ctrl->blend &&
                            (blendEffect != BLEND_NONE);

        /* Preprocess: fill this span with the backdrop colour.
         * Only pre-apply brighten/darken when blending is enabled in this
         * window span.
         */
        cupid_render_preprocess_buffer(gba, startX, endX,
                                       target1Bd && ctrl->blend);

        /* Compute per-span BG flags: when blending is disabled in this span,
         * strip FLAG_TARGET_1 from BG pixel flags so the postprocess
         * brighten/darken pass does not affect those pixels.
         */
        uint32_t spanBgFlags[4];
        for (int i = 0; i < 4; i++) {
            spanBgFlags[i] = bgFlags[i];
            if (!ctrl->blend) {
                spanBgFlags[i] &= ~FLAG_TARGET_1;
            }
        }

        for (unsigned prio = 0; prio < 4; prio++) {
            /* Postprocess sprites at this priority level */
            if (showOBJ && ctrl->obj) {
                cupid_render_postprocess_sprites(gba, prio, startX, endX,
                                                 blendEnabled);
            }

            /* Draw BG layers at this priority */
            for (int bg = 0; bg < 4; bg++) {
                if (!bgActive[bg]) continue;
                if (!ctrl->bg[bg]) continue;
                if (BGCNT_PRIORITY(v->bg[bg].cnt) != (int)prio) continue;

                switch (mode) {
                case 0:
                    cupid_render_bg_mode0(gba, bg, y, spanBgFlags[bg], startX, endX);
                    break;
                case 1:
                    if (bg < 2) {
                        cupid_render_bg_mode0(gba, bg, y, spanBgFlags[bg], startX, endX);
                    } else if (bg == 2) {
                        cupid_render_bg_mode2(gba, bg, y, spanBgFlags[bg], startX, endX);
                    }
                    break;
                case 2:
                    if (bg >= 2) {
                        cupid_render_bg_mode2(gba, bg, y, spanBgFlags[bg], startX, endX);
                    }
                    break;
                case 3:
                    if (bg == 2) {
                        cupid_render_bg_mode3(gba, y, spanBgFlags[bg], startX, endX);
                    }
                    break;
                case 4:
                    if (bg == 2) {
                        cupid_render_bg_mode4(gba, y, spanBgFlags[bg], startX, endX);
                    }
                    break;
                case 5:
                    if (bg == 2) {
                        cupid_render_bg_mode5(gba, y, spanBgFlags[bg], startX, endX);
                    }
                    break;
                }
            }
        }

        /* When blend is disabled in this window span, also strip FLAG_TARGET_1
         * from the backdrop (set during preprocess) so brightness effects
         * won't apply to unchanged backdrop pixels in this region either. */
        if (!ctrl->blend) {
            for (int x = startX; x < endX; x++) {
                v->scanline[x] &= ~FLAG_TARGET_1;
            }
        }
    }

    /* Postprocess: apply remaining blending, convert to RGBA */
    cupid_render_postprocess(gba, y);

    /* Update affine running coordinates for the next scanline.
     * sx += pb and sy += pd.
     */
    if (mode != 0) {
        if (bgActive[2]) {
            v->bg[2].sx += v->bg[2].pb;
            v->bg[2].sy += v->bg[2].pd;
        }
        if (bgActive[3]) {
            v->bg[3].sx += v->bg[3].pb;
            v->bg[3].sy += v->bg[3].pd;
        }
    }

#if CUPID_VIDEO_DEBUG
    /* Bump frame counter on last visible scanline */
    if (y == GBA_SCREEN_HEIGHT - 1) {
        _vdbg_frame++;
    }
#endif
}
