/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pipeline.h"

uint16_t cupid_pixel_read_background(struct CupidVideo* v, uint32_t address, uint64_t time) {
    if (cupid_pixel_blank(v)) return 0;
    address &= 0x1FFFE;
    unsigned boundary = DISPCNT_MODE(v->dispcnt) < 3 ? 0x10000 : 0x14000;
    if (address < boundary) {
        v->pipeline.backgroundAccess = time;
        v->pipeline.vramLatch = v->vram[address >> 1];
    }
    return v->pipeline.vramLatch;
}

static unsigned read_byte(struct CupidVideo* v, uint32_t address, uint64_t time) {
    return (cupid_pixel_read_background(v, address, time) >> ((address & 1) * 8)) & 255;
}

static void fetch_text(struct CupidVideo* v, unsigned id, unsigned cycle, uint64_t time) {
    struct CupidTextFetch* t = &v->pipeline.text[id];
    const struct CupidBackground* b = &v->bg[id];
    bool full = BGCNT_256COLOR(b->cnt);
    if (t->fetches > 0 && t->remaining == 0) {
        t->data = cupid_pixel_read_background(v, t->address, time);
        if (t->flip) {
            t->data = (uint16_t)((t->data >> 8) | (t->data << 8));
            if (!full) t->data = (uint16_t)(((t->data & 0xF0F0) >> 4) | ((t->data & 0x0F0F) << 4));
            t->address -= 2;
        } else t->address += 2;
        t->remaining = 4;
        --t->fetches;
    }
    unsigned index = t->data & (full ? 255 : 15);
    if (index && !full) index |= t->palette << 4;
    t->data >>= full ? 8 : 4;
    t->remaining -= full ? 2 : 1;
    int x = (int)(cycle >> 2) - 9;
    if (x >= 0 && x < GBA_SCREEN_WIDTH) v->pipeline.backgrounds[id][x] = index;

    unsigned scroll = (unsigned)b->hofs;
    unsigned step = (cycle >> 2) + (scroll & 7);
    if (cycle >= 1007 || step < 8 || (step & 7)) return;
    unsigned y = v->vcount + (unsigned)b->vofs;
    if (BGCNT_MOSAIC(b->cnt)) y -= v->pipeline.bgMosaicY;
    unsigned tileX = (scroll >> 3) + (step >> 3) - 1;
    unsigned tileY = y >> 3;
    unsigned block = BGCNT_SCREENBASE(b->cnt);
    unsigned size = BGCNT_SIZE(b->cnt);
    if (size & 1) block += (tileX >> 5) & 1;
    if (size & 2) block += ((tileY >> 5) & 1) << (size & 1);
    uint32_t address = block * 0x800 + (tileY & 31) * 64 + (tileX & 31) * 2;
    unsigned entry = cupid_pixel_read_background(v, address, time);
    if (cycle >= 1004) return;
    unsigned row = y & 7;
    if (BG_MAP_VFLIP(entry)) row ^= 7;
    t->palette = BG_MAP_PALETTE(entry);
    t->flip = BG_MAP_HFLIP(entry) != 0;
    t->address = BGCNT_CHARBASE(b->cnt) * 0x4000 + BG_MAP_TILE(entry) * (full ? 64 : 32)
        + row * (full ? 8 : 4) + (t->flip ? (full ? 6 : 2) : 0);
    t->fetches = full ? 4 : 2;
    t->remaining = 0;
}

static void fetch_affine(struct CupidVideo* v, unsigned id, unsigned cycle, uint64_t time) {
    if (cycle < 32) return;
    struct CupidPixelPipeline* p = &v->pipeline;
    struct CupidBackground* b = &v->bg[id + 2];
    if (!(cycle & 1)) {
        int32_t x = p->affine[id].x >> 8;
        int32_t y = p->affine[id].y >> 8;
        p->affine[id].x = cupid_pixel_add(p->affine[id].x, b->pa);
        p->affine[id].y = cupid_pixel_add(p->affine[id].y, b->pc);
        unsigned log = BGCNT_SIZE(b->cnt);
        int size = 128 << log;
        p->affine[id].outside = false;
        if (BGCNT_OVERFLOW(b->cnt)) {
            x &= size - 1;
            y &= size - 1;
        } else p->affine[id].outside = x < 0 || x >= size || y < 0 || y >= size;
        uint32_t address = BGCNT_SCREENBASE(b->cnt) * 0x800
            + ((uint32_t)(y >> 3) << (4 + log)) + (uint32_t)(x >> 3);
        unsigned tile = read_byte(v, address, time);
        p->affine[id].address = BGCNT_CHARBASE(b->cnt) * 0x4000 + tile * 64
            + ((unsigned)y & 7) * 8 + ((unsigned)x & 7);
    } else {
        unsigned index = read_byte(v, p->affine[id].address, time);
        unsigned x = (cycle - 32) >> 2;
        if (x < GBA_SCREEN_WIDTH) p->backgrounds[id + 2][x] = p->affine[id].outside ? 0 : index;
    }
}

static void fetch_bitmap(struct CupidVideo* v, unsigned mode, unsigned cycle, uint64_t time) {
    if (cycle < 32 || (cycle & 3) != 3) return;
    struct CupidPixelPipeline* p = &v->pipeline;
    struct CupidBackground* b = &v->bg[2];
    unsigned screenX = (cycle - 32) >> 2;
    int32_t x = p->affine[0].x >> 8, y = p->affine[0].y >> 8;
    unsigned width = mode == 5 ? 160 : 240;
    unsigned height = mode == 5 ? 128 : 160;
    uint32_t address = (uint32_t)y * width + (uint32_t)x;
    if (mode != 4) address *= 2;
    if (mode != 3 && DISPCNT_FRAME_SELECT(v->dispcnt)) address += 0xA000;
    uint32_t pixel = mode == 4 ? read_byte(v, address, time)
        : cupid_pixel_read_background(v, address, time) | FETCH_DIRECT;
    if (x < 0 || (unsigned)x >= width || y < 0 || (unsigned)y >= height) pixel = 0;
    if (screenX < GBA_SCREEN_WIDTH) p->backgrounds[2][screenX] = pixel;
    p->affine[0].x = cupid_pixel_add(p->affine[0].x, b->pa);
    p->affine[0].y = cupid_pixel_add(p->affine[0].y, b->pc);
}

void cupid_pixel_background(struct CupidVideo* v, unsigned cycle, uint64_t time) {
    unsigned mode = DISPCNT_MODE(v->dispcnt);
    uint16_t enabled = cupid_pixel_display(v);
    if (mode < 2) {
        unsigned id = cycle & 3;
        if ((mode == 0 || id < 2) && DISPCNT_BG_ENABLE(enabled, id)) fetch_text(v, id, cycle, time);
    }
    if (cycle < 1007) {
        if (mode == 1 || mode == 2) {
            unsigned id = (~(cycle >> 1)) & 1;
            if ((mode == 2 || id == 0) && DISPCNT_BG_ENABLE(enabled, id + 2)) fetch_affine(v, id, cycle, time);
        } else if (mode >= 3 && mode <= 5 && DISPCNT_BG2_ENABLE(enabled)) fetch_bitmap(v, mode, cycle, time);
    }
    if (cycle != VIDEO_HORIZONTAL_LENGTH) return;
    unsigned mosaicHeight = MOSAIC_BG_V(v->mosaic) + 1;
    if (v->vcount < 159) {
        if (++v->pipeline.bgMosaicY == mosaicHeight) v->pipeline.bgMosaicY = 0;
        v->pipeline.bgMosaicY &= 15;
    } else v->pipeline.bgMosaicY = 0;
    for (unsigned id = 0; id < 2; id++) {
        if (!(mode >= 1 && mode <= 5 && (id == 0 || mode == 2)) || !DISPCNT_BG_ENABLE(enabled, id + 2)) continue;
        struct CupidBackground* b = &v->bg[id + 2];
        unsigned step = 1;
        if (BGCNT_MOSAIC(b->cnt)) {
            if (v->pipeline.bgMosaicY) continue;
            step = mosaicHeight;
        }
        b->sx = cupid_pixel_add(b->sx, b->pb * (int)step);
        b->sy = cupid_pixel_add(b->sy, b->pd * (int)step);
    }
}
