/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pipeline.h"

static uint8_t window_control(const struct CupidVideo* v, unsigned x) {
    uint16_t display = v->dispcnt;
    if (!(display & 0xE000)) return 0x3F;
    if (DISPCNT_WIN0_ENABLE(display) && (v->pipeline.windows[x] & 1)) return v->win.winin & 0x3F;
    if (DISPCNT_WIN1_ENABLE(display) && (v->pipeline.windows[x] & 2)) return (v->win.winin >> 8) & 0x3F;
    const struct CupidObjectPixel* obj = &v->pipeline.objects.pixels[v->pipeline.objects.buffer][x];
    if (DISPCNT_OBJWIN_ENABLE(display) && DISPCNT_OBJ_ENABLE(cupid_pixel_display(v)) && obj->window)
        return (v->win.winout >> 8) & 0x3F;
    return v->win.winout & 0x3F;
}

static uint32_t read_palette(struct CupidVideo* v, uint32_t pixel, uint64_t time) {
    if (pixel & FETCH_DIRECT) return pixel & 0xFFFF;
    v->pipeline.paletteAccess = time;
    return v->palette[pixel & 511];
}

static void insert_layer(struct CupidPixelPipeline* p, unsigned layer, uint32_t pixel,
                         unsigned order, unsigned orders[2]) {
    if (order < orders[0]) {
        orders[1] = orders[0];
        p->layer[1] = p->layer[0];
        p->color[1] = p->color[0];
        orders[0] = order;
        p->layer[0] = layer;
        p->color[0] = pixel;
    } else if (order < orders[1]) {
        orders[1] = order;
        p->layer[1] = layer;
        p->color[1] = pixel;
    }
}

/* Layer scratch storage is needed only by active selection. Output and
 * forced-blank phases share the ordinary pair and mosaic publication. */
static void select_layers(struct CupidVideo* v, unsigned x, uint64_t time) {
    struct CupidPixelPipeline* p = &v->pipeline;
    uint8_t control = window_control(v, x);
    unsigned mode = DISPCNT_MODE(v->dispcnt);
    uint16_t enabled = cupid_pixel_display(v);
    unsigned orders[2] = {32, 32};
    /* Empty background selection still samples the backdrop and OBJ
     * latch below, but needs no per-background arbitration. */
    if ((enabled >> 8) & control & 15) for (unsigned id = 0; id < 4; id++) {
        bool active = mode == 0 || (mode == 1 && id < 3)
            || (mode == 2 && id >= 2) || (mode >= 3 && mode <= 5 && id == 2);
        if (!active || !DISPCNT_BG_ENABLE(enabled, id) || !(control & (1 << id))) continue;
        unsigned mx = x - (BGCNT_MOSAIC(v->bg[id].cnt) ? p->bgMosaicX : 0);
        uint32_t pixel = p->backgrounds[id][mx];
        if (pixel) insert_layer(p, id, pixel, BGCNT_PRIORITY(v->bg[id].cnt) * 8 + id + 1, orders);
    }
    struct CupidObjectPixel obj = {0};
    if (DISPCNT_OBJ_ENABLE(enabled)) obj = p->objects.pixels[p->objects.buffer][x];
    if (!obj.mosaic || !p->objectLatch.mosaic || obj.priority < p->objectLatch.priority || !p->objMosaicX)
        p->objectLatch = obj;
    obj = p->objectLatch;
    if (DISPCNT_OBJ_ENABLE(enabled) && (control & 16) && obj.index) {
        insert_layer(p, 4, obj.index | 256, obj.priority * 8, orders);
        p->semi = p->layer[0] == 4 && obj.semi;
    }
    p->color[0] = read_palette(v, p->color[0], time);
}

static void publish_pixel(struct CupidVideo* v, unsigned x, uint32_t color) {
    struct CupidPixelPipeline* p = &v->pipeline;
    v->scanline[x] = color & 0x7FFF;
    if (x & 1) {
        uint32_t* row = v->outputBuffer + v->vcount * GBA_SCREEN_WIDTH;
        if (color == 0x7FFF && p->left == 0x7FFF) {
            /* Equal white pixels retain every component under green swap. */
            uint32_t white = color16_to_rgba(0x7FFF);
            row[x - 1] = row[x] = white;
        } else {
            uint16_t left = p->left & 0x7FFF, right = color & 0x7FFF;
            if (v->gba->memory.io[REG_GREENSWAP >> 1] & 1) {
                uint16_t green = left & 0x03E0;
                left = (left & ~0x03E0) | (right & 0x03E0);
                right = (right & ~0x03E0) | green;
            }
            row[x - 1] = color16_to_rgba(left);
            row[x] = color16_to_rgba(right);
        }
    } else p->left = (uint16_t)color;
    unsigned bgPhase = p->bgMosaicX + 1, objPhase = p->objMosaicX + 1;
    uint16_t mosaic = v->mosaic;
    if (bgPhase == (unsigned)MOSAIC_BG_H(mosaic) + 1) bgPhase = 0;
    if (objPhase == (unsigned)MOSAIC_OBJ_H(mosaic) + 1) objPhase = 0;
    /* A horizontal size write retains the elapsed group counter. If the
     * new terminal count has already passed, that group continues through
     * the rest of this line; line start resets the counter. Layer selection,
     * palette reads and OBJ priority still run at their ordinary phases.
     * Vertical mosaic uses its separate four-bit line counter. */
    p->bgMosaicX = bgPhase;
    p->objMosaicX = objPhase;
}

void cupid_pixel_merge(struct CupidVideo* v, unsigned cycle, uint64_t time) {
    if (cycle < 46 || cycle >= 1006) return;
    struct CupidPixelPipeline* p = &v->pipeline;
    unsigned x = (cycle - 46) >> 2;
    unsigned phase = (cycle - 46) & 3;
    if (phase == 0) {
        p->blank = cupid_pixel_blank(v);
        p->semi = false;
        p->layer[0] = p->layer[1] = 5;
        p->color[0] = p->color[1] = 0;
        if (p->blank) {
            p->color[0] = 0x7FFF;
        } else {
            select_layers(v, x, time);
        }
    } else if (phase == 2) {
        uint32_t color = p->color[0];
        if (!p->blank) {
            uint8_t control = window_control(v, x);
            unsigned effect = BLDCNT_EFFECT(v->blend.bldcnt);
            bool first = (v->blend.bldcnt >> p->layer[0]) & 1;
            bool second = (v->blend.bldcnt >> (8 + p->layer[1])) & 1;
            if (second && (p->semi || ((control & 32) && effect == BLEND_ALPHA && first))) {
                unsigned eva = v->blend.bldalpha & 31, evb = (v->blend.bldalpha >> 8) & 31;
                if (eva > 16) eva = 16;
                if (evb > 16) evb = 16;
                color = cupid_color_mix(eva, color, evb, read_palette(v, p->color[1], time));
            } else if ((control & 32) && first) {
                unsigned evy = v->blend.bldy & 31;
                if (evy > 16) evy = 16;
                if (effect == BLEND_BRIGHTEN) color = cupid_brighten(color, evy);
                if (effect == BLEND_DARKEN) color = cupid_darken(color, evy);
            }
        }
        publish_pixel(v, x, color);
    }
}

static void step_window(struct CupidVideo* v, unsigned cycle) {
    /* Callers select the four-clock comparator stages below clock 1024. */
    unsigned x = cycle >> 2;
    struct CupidPixelPipeline* p = &v->pipeline;
    uint32_t boundaries;
    _Static_assert(sizeof(v->win.h) == sizeof(boundaries), "Four byte-sized horizontal boundaries");
    memcpy(&boundaries, v->win.h, sizeof(boundaries));
    if (!boundaries && !p->windowH[0] && !p->windowH[1]) {
        /* Equal zero boundaries leave cleared latches false. This zero test
         * is byte-order independent; retained true latches use the ordinary
         * comparators until their actual clock-zero boundary is visited. */
        if (x < GBA_SCREEN_WIDTH) p->windows[x] = 0;
        return;
    }
    uint8_t flags = 0;
    if (x == v->win.h[0][0]) p->windowH[0] = true;
    if (x == v->win.h[0][1]) p->windowH[0] = false;
    if (p->windowH[0] && p->windowV[0]) flags |= 1;
    if (x == v->win.h[1][0]) p->windowH[1] = true;
    if (x == v->win.h[1][1]) p->windowH[1] = false;
    if (p->windowH[1] && p->windowV[1]) flags |= 2;
    if (x < GBA_SCREEN_WIDTH) p->windows[x] = flags;
}

static void sync_blank_windows(struct CupidVideo* v, unsigned first, unsigned end) {
    unsigned x = (first + 3) >> 2;
    unsigned stop = ((end < 1024 ? end : 1024) + 3) >> 2;
    struct CupidPixelPipeline* p = &v->pipeline;
    /* Between register writes, horizontal latches can change only at their
     * four programmed boundaries. Visit each boundary in comparator order,
     * then fill the identical flags up to the next one. Comparators beyond
     * pixel 239 still update the latches carried into the next line. */
    while (x < stop) {
        step_window(v, x << 2);
        uint8_t flags = (p->windowH[0] && p->windowV[0] ? 1 : 0)
            | (p->windowH[1] && p->windowV[1] ? 2 : 0);
        unsigned next = stop;
        for (unsigned window = 0; window < 2; ++window) {
            unsigned start = v->win.h[window][0], finish = v->win.h[window][1];
            if (start > x && start < next) next = start;
            if (finish > x && finish < next) next = finish;
        }
        unsigned fill = next < GBA_SCREEN_WIDTH ? next : GBA_SCREEN_WIDTH;
        if (x < fill) memset(p->windows + x, flags, fill - x);
        x = next;
    }
}

void cupid_video_sync(struct CupidVideo* v) {
    uint64_t now = cupid_timing_global_time(&v->gba->timing);
    const struct CupidPixelPipeline* p = &v->pipeline;
    if (now < p->start) return;
    uint64_t elapsed = now - p->start + p->initialCycle;
    if (elapsed > VIDEO_HORIZONTAL_LENGTH) elapsed = VIDEO_HORIZONTAL_LENGTH;
    /* A data strobe often follows the ready predicate on this same clock.
     * Its renderer position is already complete. Other positions retain
     * the general path, including backwards position adjustments. */
    if (p->cycle == elapsed) return;
    cupid_video_sync_to(v, now);
}

static unsigned next_phase(unsigned cycle, unsigned period, unsigned phase) {
    return cycle + ((phase - cycle) & (period - 1));
}

static unsigned next_stage(const struct CupidVideo* v, unsigned cycle) {
    const struct CupidPixelPipeline* p = &v->pipeline;
    unsigned next = next_phase(cycle, 4, 0); /* Horizontal window comparators. */
    if (next >= 1024) next = VIDEO_HORIZONTAL_LENGTH;
    if (v->vcount < GBA_SCREEN_HEIGHT) {
        unsigned merge = cycle < 46 ? 46 : next_phase(cycle, 2, 0);
        if (merge <= 1004 && merge < next) next = merge;
        unsigned mode = DISPCNT_MODE(v->dispcnt);
        uint16_t display = cupid_pixel_display(v);
        if (mode < 2) {
            unsigned mask = (display >> 8) & (mode == 0 ? 15 : 3);
            if (mask) for (unsigned id = 0; id < 4; id++) {
                if (!(mask & (1 << id))) continue;
                unsigned fetch = next_phase(cycle, 4, (id + 3) & 3);
                if (fetch < next) next = fetch;
            }
        }
        if (mode == 1 || mode == 2) {
            unsigned first = cycle < 31 ? 31 : cycle;
            for (unsigned i = 0; i < 4; i++) {
                unsigned fetch = first + i;
                unsigned id = (~((fetch + 1) >> 1)) & 1;
                if ((mode == 2 || id == 0) && DISPCNT_BG_ENABLE(display, id + 2) && fetch < 1006 && fetch < next) next = fetch;
            }
        } else if (mode >= 3 && mode <= 5 && DISPCNT_BG2_ENABLE(display)) {
            unsigned fetch = next_phase(cycle < 34 ? 34 : cycle, 4, 2);
            if (fetch < 1006 && fetch < next) next = fetch;
        }
        if (cycle <= 1231 && next > 1231) next = 1231; /* Affine line step. */
    }
    const struct CupidObjectFetch* f = &p->objects;
    if (f->active) {
        uint64_t time = p->start + cycle - p->initialCycle;
        if (time >= f->start) {
            unsigned fetch = cycle + (unsigned)((time - f->start) & 1);
            uint64_t objectCycle = time - f->start + fetch - cycle;
            if (objectCycle < f->limit && fetch < next) next = fetch;
        }
    }
    return next;
}

/* With OBJ disabled, calls only advance its elapsed-cycle marker and the
 * vertical mosaic boundary. Find the last original stage before its budget
 * ends; retain a separate call at the mosaic clock when that stage exists. */
static void sync_blank_objects(struct CupidVideo* v, unsigned first, unsigned end,
                                uint64_t origin) {
    struct CupidObjectFetch* f = &v->pipeline.objects;
    if (!f->active) return;
    uint64_t lower = origin + first, upper = origin + end - 1;
    uint64_t mosaic = f->start + 1192;
    bool mosaicDue = false;
    if (mosaic >= lower && mosaic <= upper) {
        unsigned cycle = (unsigned)(mosaic - origin);
        mosaicDue = f->limit > 1192 || (cycle < 1024 && !(cycle & 3))
            || (cycle >= 46 && cycle <= 1004 && !(cycle & 1)) || cycle == 1231;
        if (mosaicDue) cupid_pixel_object(v, mosaic);
    }
    if (!f->limit) return;
    if (lower < f->start) lower = f->start;
    uint64_t finish = f->start + f->limit - 1;
    if (upper > finish) upper = finish;
    if (lower > upper) return;
    unsigned low = (unsigned)(lower - origin), high = (unsigned)(upper - origin);
    unsigned last = 0;
    bool found = false;
    unsigned window = (high < 1020 ? high : 1020) & ~3u;
    if (window >= low) { last = window; found = true; }
    unsigned merge = (high < 1004 ? high : 1004) & ~1u;
    if (merge >= 46 && merge >= low && (!found || merge > last)) {
        last = merge;
        found = true;
    }
    if (low <= 1231 && high >= 1231) { last = 1231; found = true; }
    uint64_t fetch = upper - ((upper - f->start) & 1);
    if (fetch >= lower) {
        unsigned cycle = (unsigned)(fetch - origin);
        if (!found || cycle > last) { last = cycle; found = true; }
    }
    if (found && (!mosaicDue || origin + last != mosaic)) cupid_pixel_object(v, origin + last);
}

static unsigned advance_mosaic(unsigned phase, unsigned width, unsigned count) {
    /* Only equality ends a horizontal group. A shortened width must not
     * invent another match by wrapping a retained elapsed counter. */
    if (phase >= width) return phase + count;
    return (phase + count) % width;
}

static void publish_blank_span(struct CupidVideo* v, unsigned first, unsigned end) {
    struct CupidPixelPipeline* p = &v->pipeline;
    unsigned merge = ((first < 48 ? 48 : first) + 3) & ~3u;
    unsigned stop = end < 1005 ? end : 1005;
    if (merge >= stop) return;
    unsigned count = 1 + (stop - 1 - merge) / 4;
    unsigned x = (merge - 46) >> 2;
    /* The leading right pixel may pair with an earlier colored left pixel.
     * Every remaining complete pair consists of two unchanged white pixels. */
    if (x & 1) {
        publish_pixel(v, x++, 0x7FFF);
        --count;
    }
    if (!count) return;
    uint32_t white = color16_to_rgba(0x7FFF);
    uint32_t* row = v->outputBuffer + v->vcount * GBA_SCREEN_WIDTH;
    for (unsigned i = 0; i < count; ++i) {
        unsigned pixel = x + i;
        v->scanline[pixel] = 0x7FFF;
        if (pixel & 1) row[pixel - 1] = row[pixel] = white;
    }
    p->left = 0x7FFF;
    p->bgMosaicX = advance_mosaic(p->bgMosaicX, (unsigned)MOSAIC_BG_H(v->mosaic) + 1, count);
    p->objMosaicX = advance_mosaic(p->objMosaicX, (unsigned)MOSAIC_OBJ_H(v->mosaic) + 1, count);
}

static bool sync_blank_span(struct CupidVideo* v, unsigned end) {
    struct CupidPixelPipeline* p = &v->pipeline;
    unsigned first = p->cycle;
    /* The caller selected a progressing visible line and an already blank
     * pixel: white colors, backdrop layers and no alpha flag. This interval
     * has no memory fetches; an unstarted engine retains the general loop.
     * Register writes synchronize before changing any of these conditions. */
    uint16_t display = v->dispcnt, latched = p->displayLatch[0];
    if (!((display | latched) & 0x80) || (display & latched & 0x0F00) || (display & 0x1000))
        return false;
    struct CupidObjectFetch* f = &p->objects;
    uint64_t origin = p->start - p->initialCycle;
    bool objectActive = f->active;
    uint64_t objectStart = f->start;
    if (objectActive && objectStart > origin + first) return false;
    if (end - first < 16) {
        /* CPU requests often advance one clock. Visit the exact stage union
         * directly rather than searching for the next enabled BG fetch. OBJ
         * start/budget stay fixed in this span, and the other blank helpers
         * do not read its elapsed marker. Publish that marker on return. */
        unsigned objectLimit = f->limit, objectMarker = f->cycle;
        for (unsigned cycle = first; cycle < end; ++cycle) {
            uint64_t time = origin + cycle;
            uint64_t objectElapsed = time - objectStart;
            bool objectStarted = objectActive && time >= objectStart;
            bool window = cycle < 1024 && !(cycle & 3);
            bool merge = cycle >= 46 && cycle <= 1004 && !(cycle & 1);
            bool boundary = cycle == 1231;
            bool object = objectStarted && objectElapsed < objectLimit && !(objectElapsed & 1);
            if (!(window || merge || boundary || object)) continue;
            if (boundary) cupid_pixel_background(v, VIDEO_HORIZONTAL_LENGTH, time + 1);
            /* OBJ is disabled for this interval. Retain its elapsed marker
             * and vertical mosaic edge without entering the texture engine.
             * The counter deliberately has the engine's unsigned width. */
            if (objectStarted) {
                unsigned objectCycle = (unsigned)objectElapsed;
                if (objectCycle == 1192) {
                    if (f->line < 159) {
                        if (++p->objMosaicY == (unsigned)MOSAIC_OBJ_V(v->mosaic) + 1) p->objMosaicY = 0;
                        p->objMosaicY &= 15;
                    } else p->objMosaicY = 0;
                }
                if (objectMarker < objectLimit && objectCycle < objectLimit) objectMarker = objectCycle + 1;
            }
            if (window) step_window(v, cycle);
            if (merge && !(cycle & 3)) publish_pixel(v, (cycle - 46) >> 2, 0x7FFF);
        }
        f->cycle = objectMarker;
        p->cycle = end;
        return true;
    }
    /* Short requests above use the same modular timestamp arithmetic as the
     * stage loop. Closed interval clipping below requires a non-wrapping line
     * origin, object budget and four-bit mosaic phases. */
    if (p->bgMosaicX > 15 || p->objMosaicX > 15 || p->start < p->initialCycle
        || p->start > UINT64_MAX - VIDEO_HORIZONTAL_LENGTH
        || (f->active && (f->start > UINT64_MAX - 1192 || f->start > UINT64_MAX - f->limit)))
        return false;
    /* White selection phases only repeat already stored values. Output phases
     * still use the ordinary pair/green-swap and horizontal mosaic logic. */
    publish_blank_span(v, first, end);
    sync_blank_windows(v, first, end);
    sync_blank_objects(v, first, end, origin);
    if (first <= 1231 && end > 1231)
        cupid_pixel_background(v, VIDEO_HORIZONTAL_LENGTH, origin + VIDEO_HORIZONTAL_LENGTH);
    p->cycle = end;
    return true;
}

void cupid_video_sync_to(struct CupidVideo* v, uint64_t now) {
    struct CupidPixelPipeline* p = &v->pipeline;
    if (now < p->start) return;
    uint64_t elapsed = now - p->start + p->initialCycle;
    if (elapsed > VIDEO_HORIZONTAL_LENGTH) elapsed = VIDEO_HORIZONTAL_LENGTH;
    if (p->cycle >= elapsed) { p->cycle = (unsigned)elapsed; return; }
    if (v->vcount < GBA_SCREEN_HEIGHT && p->blank && sync_blank_span(v, (unsigned)elapsed)) return;
    while (p->cycle < elapsed) {
        unsigned cycle = next_stage(v, p->cycle);
        if (cycle >= elapsed) break;
        p->cycle = cycle + 1;
        uint64_t time = p->start + cycle - p->initialCycle;
        if (v->vcount < GBA_SCREEN_HEIGHT) cupid_pixel_background(v, cycle + 1, time + 1);
        cupid_pixel_object(v, time);
        /* These helpers have no work on other phases. Keep every fetch and
         * comparator deadline while avoiding calls that would only return. */
        if (cycle < 1024 && !(cycle & 3)) step_window(v, cycle);
        if (v->vcount < GBA_SCREEN_HEIGHT && cycle >= 46 && cycle < 1006 && !(cycle & 1))
            cupid_pixel_merge(v, cycle, time);
    }
    p->cycle = (unsigned)elapsed;
}

bool cupid_video_memory_busy(struct CupidVideo* v, uint32_t address) {
    uint64_t now = cupid_timing_global_time(&v->gba->timing);
    /* Fetch synchronization only updates renderer state at this timestamp;
     * it does not advance or dispatch the board clock. */
    cupid_video_sync_to(v, now);
    const struct CupidPixelPipeline* p = &v->pipeline;
    switch (address >> 24) {
    case GBA_REGION_PALETTE:
        return p->paletteAccess != UINT64_MAX && now == p->paletteAccess + 1;
    case GBA_REGION_VRAM: {
        unsigned boundary = DISPCNT_MODE(v->dispcnt) < 3 ? 0x10000 : 0x14000;
        if ((address & 0x1FFFF) < boundary) return now == p->backgroundAccess;
        return p->objectAccess != UINT64_MAX && now == p->objectAccess + 1;
    }
    case GBA_REGION_OAM:
        return p->oamAccess != UINT64_MAX && now == p->oamAccess + 1;
    default:
        return false;
    }
}

void cupid_pixel_begin_line(struct CupidVideo* v, unsigned initialCycle) {
    struct CupidPixelPipeline* p = &v->pipeline;
    p->start = cupid_timing_global_time(&v->gba->timing);
    p->cycle = p->initialCycle = initialCycle;
    p->bgMosaicX = p->objMosaicX = 0;
    p->objectLatch = (struct CupidObjectPixel){0};
    for (unsigned id = 0; id < 2; id++) {
        if (v->vcount == v->win.v[id][0]) p->windowV[id] = true;
        if (v->vcount == v->win.v[id][1]) p->windowV[id] = false;
        if (p->affine[id].writtenX || v->vcount == 0) {
            v->bg[id + 2].sx = v->bg[id + 2].refx;
            p->affine[id].writtenX = false;
        }
        if (p->affine[id].writtenY || v->vcount == 0) {
            v->bg[id + 2].sy = v->bg[id + 2].refy;
            p->affine[id].writtenY = false;
        }
        p->affine[id].x = v->bg[id + 2].sx;
        p->affine[id].y = v->bg[id + 2].sy;
    }
    for (unsigned id = 0; id < 4; id++) p->text[id].fetches = 0;
}

void cupid_pixel_latch(struct CupidVideo* v) {
    struct CupidPixelPipeline* p = &v->pipeline;
    p->displayLatch[0] = p->displayLatch[1];
    p->displayLatch[1] = p->displayLatch[2];
    p->displayLatch[2] = v->dispcnt;
    cupid_pixel_object_begin(v, cupid_timing_global_time(&v->gba->timing));
}
