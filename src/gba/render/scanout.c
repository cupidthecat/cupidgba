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

void cupid_pixel_merge(struct CupidVideo* v, unsigned cycle, uint64_t time) {
    if (cycle < 46 || cycle >= 1006) return;
    struct CupidPixelPipeline* p = &v->pipeline;
    unsigned x = (cycle - 46) >> 2;
    unsigned phase = (cycle - 46) & 3;
    uint8_t control = window_control(v, x);
    if (phase == 0) {
        p->blank = cupid_pixel_blank(v);
        p->semi = false;
        p->layer[0] = p->layer[1] = 5;
        p->color[0] = p->color[1] = 0;
        if (p->blank) {
            p->color[0] = 0x7FFF;
        } else {
            unsigned mode = DISPCNT_MODE(v->dispcnt);
            uint16_t enabled = cupid_pixel_display(v);
            unsigned orders[2] = {32, 32};
            for (unsigned id = 0; id < 4; id++) {
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
    } else if (phase == 2) {
        uint32_t color = p->color[0];
        if (!p->blank) {
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
        v->scanline[x] = color & 0x7FFF;
        if (x & 1) {
            uint16_t left = p->left & 0x7FFF, right = color & 0x7FFF;
            if (v->gba->memory.io[REG_GREENSWAP >> 1] & 1) {
                uint16_t green = left & 0x03E0;
                left = (left & ~0x03E0) | (right & 0x03E0);
                right = (right & ~0x03E0) | green;
            }
            uint32_t* row = v->outputBuffer + v->vcount * GBA_SCREEN_WIDTH;
            row[x - 1] = color16_to_rgba(left);
            row[x] = color16_to_rgba(right);
        } else p->left = (uint16_t)color;
        if (++p->bgMosaicX == (unsigned)MOSAIC_BG_H(v->mosaic) + 1) p->bgMosaicX = 0;
        if (++p->objMosaicX == (unsigned)MOSAIC_OBJ_H(v->mosaic) + 1) p->objMosaicX = 0;
        p->bgMosaicX &= 15;
        p->objMosaicX &= 15;
    }
}

static void step_window(struct CupidVideo* v, unsigned cycle) {
    if (cycle >= 1024 || (cycle & 3)) return;
    unsigned x = cycle >> 2;
    uint8_t flags = 0;
    for (unsigned id = 0; id < 2; id++) {
        if (x == v->win.h[id][0]) v->pipeline.windowH[id] = true;
        if (x == v->win.h[id][1]) v->pipeline.windowH[id] = false;
        if (v->pipeline.windowH[id] && v->pipeline.windowV[id]) flags |= 1 << id;
    }
    if (x < GBA_SCREEN_WIDTH) v->pipeline.windows[x] = flags;
}

void cupid_video_sync(struct CupidVideo* v) {
    cupid_video_sync_to(v, cupid_timing_global_time(&v->gba->timing));
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
            for (unsigned id = 0; id < 4; id++) {
                unsigned fetch = next_phase(cycle, 4, (id + 3) & 3);
                if ((mask & (1 << id)) && fetch < next) next = fetch;
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

void cupid_video_sync_to(struct CupidVideo* v, uint64_t now) {
    struct CupidPixelPipeline* p = &v->pipeline;
    if (now < p->start) return;
    uint64_t elapsed = now - p->start + p->initialCycle;
    if (elapsed > VIDEO_HORIZONTAL_LENGTH) elapsed = VIDEO_HORIZONTAL_LENGTH;
    while (p->cycle < elapsed) {
        unsigned cycle = next_stage(v, p->cycle);
        if (cycle >= elapsed) break;
        p->cycle = cycle + 1;
        uint64_t time = p->start + cycle - p->initialCycle;
        if (v->vcount < GBA_SCREEN_HEIGHT) cupid_pixel_background(v, cycle + 1, time + 1);
        cupid_pixel_object(v, time);
        step_window(v, cycle);
        if (v->vcount < GBA_SCREEN_HEIGHT) cupid_pixel_merge(v, cycle, time);
    }
    p->cycle = (unsigned)elapsed;
}

bool cupid_video_memory_busy(struct CupidVideo* v, uint32_t address) {
    cupid_video_sync(v);
    uint64_t now = cupid_timing_global_time(&v->gba->timing);
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
