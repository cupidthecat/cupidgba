/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pipeline.h"

static uint16_t read_oam(struct CupidVideo* v, unsigned address, uint64_t time) {
    v->pipeline.oamAccess = time;
    return v->oam.raw[(address & 0x3FE) >> 1];
}

static uint16_t read_object(struct CupidVideo* v, unsigned address, uint64_t time) {
    unsigned boundary = DISPCNT_MODE(v->dispcnt) < 3 ? 0x10000 : 0x14000;
    address &= 0x1FFFE;
    if (address < boundary || address >= GBA_SIZE_VRAM) return 0;
    v->pipeline.objectAccess = time;
    return v->vram[address >> 1];
}

static void submit(struct CupidObjectFetch* f) {
    f->read ^= 1;
    f->write ^= 1;
    f->drawing = true;
    ++f->index;
    f->step = 0;
    f->wait = f->pendingWait;
    f->deferWait = true;
}

static void fetch_attributes(struct CupidVideo* v, uint64_t time) {
    struct CupidObjectFetch* f = &v->pipeline.objects;
    struct CupidObjectDraw* d = &f->draw[f->write];
    if (f->wait > 0 && !f->deferWait) {
        --f->wait;
        return;
    }
    f->deferWait = false;
    if (f->step == 0) {
        if (f->index == 128) {
            f->step = 6;
            return;
        }
        unsigned a = read_oam(v, f->index * 8, time);
        unsigned b = read_oam(v, f->index * 8 + 2, time);
        unsigned shape = OBJ_ATTR0_SHAPE(a), size = OBJ_ATTR1_SIZE(b);
        if ((!OBJ_ATTR0_TRANSFORMED(a) && OBJ_ATTR0_DISABLE(a)) || shape == 3 || OBJ_ATTR0_MODE(a) == OBJ_MODE_HIDDEN) {
            ++f->index;
            return;
        }
        int width = CupidObjSizes[shape * 4 + size][0];
        int height = CupidObjSizes[shape * 4 + size][1];
        bool affine = OBJ_ATTR0_TRANSFORMED(a);
        int boxWidth = width * (affine && OBJ_ATTR0_DOUBLE(a) ? 2 : 1);
        int boxHeight = height * (affine && OBJ_ATTR0_DOUBLE(a) ? 2 : 1);
        int localY = ((int)f->line - (int)OBJ_ATTR0_Y(a)) & 255;
        if (localY >= boxHeight) {
            ++f->index;
            return;
        }
        *d = (struct CupidObjectDraw){.width = width, .height = height,
            .mode = OBJ_ATTR0_MODE(a), .x = OBJ_ATTR1_X(b), .remaining = boxWidth,
            .affine = affine, .fullPalette = OBJ_ATTR0_256COLOR(a),
            .mosaic = OBJ_ATTR0_MOSAIC(a) && OBJ_ATTR0_MODE(a) != OBJ_MODE_OBJWIN};
        if (d->x >= 240) d->x -= 512;
        if (d->mosaic) {
            localY -= f->mosaicY;
            if (localY < 0) localY = 0;
        }
        if (affine) {
            f->localX = -boxWidth / 2;
            f->localY = localY - boxHeight / 2;
            f->matrixAddress = OBJ_ATTR1_MATINDEX(b) * 32 + 6;
            f->pendingWait = boxWidth - 1;
        } else {
            d->flip = OBJ_ATTR1_HFLIP(b);
            d->tx = 0;
            d->ty = localY ^ (OBJ_ATTR1_VFLIP(b) ? height - 1 : 0);
            f->pendingWait = boxWidth / 2 - 2;
        }
        if (d->x < 0) {
            int clip = -d->x;
            if (!affine) clip &= ~1;
            d->x += clip;
            d->remaining -= clip;
            f->pendingWait -= affine ? clip : clip / 2;
            if (affine) f->localX += clip;
            else d->tx += clip;
        }
        if (d->remaining <= 0) ++f->index;
        else f->step = 1;
    } else if (f->step == 1) {
        unsigned attr = read_oam(v, f->index * 8 + 4, time);
        d->tile = OBJ_ATTR2_TILE(attr);
        d->palette = OBJ_ATTR2_PALETTE(attr);
        d->priority = OBJ_ATTR2_PRIORITY(attr);
        if (d->affine) f->step = 2;
        else submit(f);
    } else if (f->step < 6) {
        d->matrix[f->step - 2] = (int16_t)read_oam(v, f->matrixAddress, time);
        f->matrixAddress += 8;
        if (++f->step == 6) {
            d->tx = d->matrix[0] * f->localX + d->matrix[1] * f->localY + d->width * 128;
            d->ty = d->matrix[2] * f->localX + d->matrix[3] * f->localY + d->height * 128;
            submit(f);
        }
    }
}

static unsigned texture_address(const struct CupidVideo* v, const struct CupidObjectDraw* d, int x, int y) {
    unsigned bx = (unsigned)x >> 3, by = (unsigned)y >> 3;
    unsigned tile;
    if (DISPCNT_OBJ_1D(v->dispcnt)) {
        tile = d->tile + by * (d->width >> (d->fullPalette ? 2 : 3)) + (bx << d->fullPalette);
    } else {
        tile = ((d->tile + by * 32) & 0x3E0)
            | (((d->fullPalette ? d->tile & ~1u : d->tile) + (bx << d->fullPalette)) & 31);
    }
    return 0x10000 + (tile & 1023) * 32 + ((unsigned)y & 7) * (d->fullPalette ? 8 : 4)
        + (d->fullPalette ? (unsigned)x & 7 : ((unsigned)x & 7) >> 1);
}

static void plot(struct CupidVideo* v, const struct CupidObjectDraw* d, int x, unsigned index) {
    if (x < 0 || x >= GBA_SCREEN_WIDTH) return;
    struct CupidObjectFetch* f = &v->pipeline.objects;
    struct CupidObjectPixel* pixel = &f->pixels[f->buffer ^ 1][x];
    if (d->mode == OBJ_MODE_OBJWIN && index) pixel->window = true;
    else if (!pixel->index || d->priority < pixel->priority) {
        if (index) {
            pixel->index = (uint8_t)index;
            pixel->semi = d->mode == OBJ_MODE_SEMITRANSPARENT;
        }
        pixel->mosaic = d->mosaic;
        pixel->priority = (uint8_t)d->priority;
    }
}

static void fetch_texture(struct CupidVideo* v, uint64_t time) {
    struct CupidObjectFetch* f = &v->pipeline.objects;
    if (!f->drawing) return;
    struct CupidObjectDraw* d = &f->draw[f->read];
    int x = d->affine ? d->tx >> 8 : d->tx ^ (d->flip ? d->width - 1 : 0);
    int y = d->affine ? d->ty >> 8 : d->ty;
    if (d->affine) {
        if (f->deferWait) return;
        if (x >= 0 && x < d->width && y >= 0 && y < d->height) {
            unsigned address = texture_address(v, d, x, y);
            unsigned data = (read_object(v, address, time) >> ((address & 1) * 8)) & 255;
            unsigned index = d->fullPalette ? data : (data >> ((x & 1) * 4)) & 15;
            if (index && !d->fullPalette) index |= d->palette << 4;
            plot(v, d, d->x, index);
        }
        ++d->x;
        d->tx += d->matrix[0];
        d->ty += d->matrix[2];
        if (!--d->remaining) f->drawing = false;
    } else {
        unsigned address = texture_address(v, d, x & ~1, y);
        unsigned data = read_object(v, address, time);
        if (!d->fullPalette) data = (data >> ((address & 1) * 8)) & 255;
        for (unsigned i = 0; i < 2; i++) {
            unsigned position = d->flip ? 1 - i : i;
            unsigned index = (data >> (position * (d->fullPalette ? 8 : 4))) & (d->fullPalette ? 255 : 15);
            if (index && !d->fullPalette) index |= d->palette << 4;
            plot(v, d, d->x++, index);
        }
        d->tx += 2;
        d->remaining -= 2;
        if (!d->remaining) f->drawing = false;
    }
}

void cupid_pixel_object(struct CupidVideo* v, uint64_t time) {
    struct CupidObjectFetch* f = &v->pipeline.objects;
    if (!f->active || time < f->start) return;
    unsigned cycle = (unsigned)(time - f->start);
    /* Vertical mosaic keeps its line phase after HBlank-free stops fetching. */
    if (cycle == 1192) {
        if (f->line < 159) {
            if (++v->pipeline.objMosaicY == (unsigned)MOSAIC_OBJ_V(v->mosaic) + 1) v->pipeline.objMosaicY = 0;
            v->pipeline.objMosaicY &= 15;
        } else v->pipeline.objMosaicY = 0;
    }
    if (f->cycle >= f->limit || cycle >= f->limit) return;
    f->cycle = cycle;
    if (DISPCNT_OBJ_ENABLE(v->dispcnt) && !(f->cycle & 1)) {
        fetch_texture(v, time);
        fetch_attributes(v, time);
    }
    ++f->cycle;
}

void cupid_pixel_object_begin(struct CupidVideo* v, uint64_t time) {
    struct CupidObjectFetch* f = &v->pipeline.objects;
    if (v->vcount < 160 || v->vcount == 227) {
        f->buffer ^= 1;
        f->active = v->vcount < 159 || v->vcount == 227;
        if (!f->active) return;
        f->start = time;
        f->cycle = f->index = f->step = 0;
        f->line = (v->vcount + 1) % VIDEO_VERTICAL_TOTAL;
        f->mosaicY = (int)v->pipeline.objMosaicY;
        f->wait = 0;
        f->deferWait = f->drawing = false;
        f->read = 0;
        f->write = 1;
        f->limit = DISPCNT_HBLANK_FREE(v->dispcnt) ? 964 : VIDEO_HORIZONTAL_LENGTH;
        memset(f->pixels[f->buffer ^ 1], 0, sizeof(f->pixels[0]));
    }
}
