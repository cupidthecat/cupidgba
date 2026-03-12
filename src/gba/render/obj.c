/**
 * @file obj.c
 * @brief Sprite (OBJ) rendering: normal, affine/transformed, priorities, and mosaic.
 *
 * Sprites are rendered into @c objBuffer and @c objPrio buffers. The main
 * compositing loop then merges them with the background layers at each
 * priority level.
 *
 * OBJ format:
 *   - 128 OBJ entries in OAM, each 8 bytes (4 × @c uint16_t)
 *   - Attributes decoded via the @c OBJ_ATTR macros
 *   - 32 affine matrices interleaved in OAM
 *   - Tiles stored at VRAM byte offsets 0x10000-0x17FFF (OBJ character data)
 *   - OBJ palette stored at @c palette[256..511]
 *   - 1D or 2D tile mapping selected by @c DISPCNT bit 6
 */

#include "cupid/gba.h"

/**
 * @brief Read a 16-bit halfword from the VRAM word array.
 *
 * @param vram       Pointer to the VRAM array (array of @c uint16_t).
 * @param byteOffset Byte offset into VRAM (must be halfword-aligned).
 * @return The halfword value at @p byteOffset.
 */
static inline uint16_t vram_half(const uint16_t* vram, uint32_t byteOffset) {
    return vram[byteOffset >> 1];
}

/**
 * @brief Render all OBJ pixels that intersect scanline @p y.
 *
 * Writes packed sprite pixels into @c video->objBuffer[] and per-pixel
 * ordering keys into @c video->objPrio[]. Each packed pixel contains
 * priority, blend flags, and the final 15-bit colour value.
 *
 * Priority among sprites of the same OBJ priority level is resolved by OAM
 * index: a lower OAM index wins. The loop therefore iterates from 127 down to
 * 0 so lower indices arrive later and overwrite higher ones when eligible.
 *
 * @param gba GBA context.
 * @param y   Scanline row to render.
 */
void cupid_render_sprites(struct CupidGBA* gba, int y) {
    struct CupidVideo* v = &gba->video;
    bool objMapping1D = DISPCNT_OBJ_1D(v->dispcnt);
    int mode = DISPCNT_MODE(v->dispcnt);

    /* Target flags for OBJ */
    uint16_t bldcnt = v->blend.bldcnt;
    bool target1Obj = BLDCNT_TARGET1_OBJ(bldcnt);
    bool target2Obj = BLDCNT_TARGET2_OBJ(bldcnt);
    int blendEffect = BLDCNT_EFFECT(bldcnt);

    /* Mosaic parameters */
    int mosaicH = MOSAIC_OBJ_H(v->mosaic) + 1;
    int mosaicV = MOSAIC_OBJ_V(v->mosaic) + 1;

    /* Initialize sprite scanline to unwritten */
    for (int x = 0; x < GBA_SCREEN_WIDTH; x++) {
        v->objBuffer[x] = FLAG_UNWRITTEN;
        v->objPrio[x] = 0xFFFFFFFF;
    }

    /* H-blank free sprite cycle budget */
    int32_t cyclesRemaining = DISPCNT_HBLANK_FREE(v->dispcnt)
                            ? OBJ_HBLANK_FREE_LENGTH : OBJ_LENGTH;

    /* Process all 128 OBJ entries, lowest index = highest priority */
    for (int i = 127; i >= 0; i--) {
        uint16_t a = v->oam.obj[i].a;
        uint16_t b = v->oam.obj[i].b;
        uint16_t c = v->oam.obj[i].c;

        bool isTransformed = OBJ_ATTR0_TRANSFORMED(a);
        bool isDisableOrDouble = OBJ_ATTR0_DISABLE(a);

        /* If not transformed and disable bit set → hidden */
        if (!isTransformed && isDisableOrDouble) continue;

        int objModeField = OBJ_ATTR0_MODE(a);
        if (objModeField == OBJ_MODE_HIDDEN) continue;

        bool isSemiTransparent = (objModeField == OBJ_MODE_SEMITRANSPARENT);
        bool isObjWin = (objModeField == OBJ_MODE_OBJWIN);
        bool isDouble = isTransformed && isDisableOrDouble;

        int shape = OBJ_ATTR0_SHAPE(a);
        int size  = OBJ_ATTR1_SIZE(b);
        int sizeIndex = shape * 4 + size;

        int objW = CupidObjSizes[sizeIndex][0];
        int objH = CupidObjSizes[sizeIndex][1];
        if (objW == 0 || objH == 0) continue;

        int boundW = isDouble ? objW * 2 : objW;
        int boundH = isDouble ? objH * 2 : objH;

        /* Y position (sign extend from 8 bits, wraps at 256) */
        int objY = OBJ_ATTR0_Y(a);
        if (objY >= 160) objY -= 256;

        /* Check if this sprite is on the current scanline */
        int localY = y - objY;
        if (localY < 0 || localY >= boundH) continue;

        /* X position (sign extend from 9 bits, wraps at 512) */
        int objX = OBJ_ATTR1_X(b);
        if (objX >= 240) objX -= 512;

        int priority  = OBJ_ATTR2_PRIORITY(c);
        int tileNum   = OBJ_ATTR2_TILE(c);
        int palBank   = OBJ_ATTR2_PALETTE(c);
        bool color256 = OBJ_ATTR0_256COLOR(a);
        bool useMosaic = OBJ_ATTR0_MOSAIC(a);

        /* In bitmap modes (3-5), OBJ tiles below 512 are blocked */
        if (mode >= 3 && tileNum < 512) continue;

        /* Deduct sprite cycles (2 cycles per OBJ index gap, h-pixels for rendering) */
        cyclesRemaining -= boundW;
        if (isTransformed) cyclesRemaining -= 10; /* extra for affine math */
        if (cyclesRemaining < 0) break;

        /* Hflip/Vflip (only for non-affine) */
        bool hflip = !isTransformed && OBJ_ATTR1_HFLIP(b);
        bool vflip = !isTransformed && OBJ_ATTR1_VFLIP(b);

        /* Affine matrix */
        int16_t pa_mat = 0x100, pb_mat = 0, pc_mat = 0, pd_mat = 0x100;
        if (isTransformed) {
            int matIdx = OBJ_ATTR1_MATINDEX(b);
            pa_mat = v->oam.mat[matIdx].a;
            pb_mat = v->oam.mat[matIdx].b;
            pc_mat = v->oam.mat[matIdx].c;
            pd_mat = v->oam.mat[matIdx].d;
        }

        int halfW = boundW / 2;
        int halfH = boundH / 2;

        /* Mosaic: snap Y to mosaic grid */
        int effectiveLocalY = localY;
        if (useMosaic && mosaicV > 1) {
            int mosaicY = y - (y % mosaicV);
            effectiveLocalY = mosaicY - objY;
            if (effectiveLocalY < 0) effectiveLocalY = 0;
            if (effectiveLocalY >= boundH) effectiveLocalY = boundH - 1;
        }

        /* Build flags for this sprite pixel.
         * OBJ pixels carry only priority in bits 31-30, with no layer index.
         * This ensures OBJ < BG at the same priority because BG pixels also
         * set the background/index bits, so sprites appear in front of BGs.
         */
        uint32_t spriteFlags = (uint32_t)priority << OFFSET_PRIORITY;

        if (isSemiTransparent || (target1Obj && blendEffect == BLEND_ALPHA)) {
            spriteFlags |= FLAG_TARGET_1;
            if (isSemiTransparent) spriteFlags |= FLAG_REBLEND;
        }
        if (target2Obj) {
            spriteFlags |= FLAG_TARGET_2;
        }

        /* Pre-compute VRAM addressing variables.
         * These encode 1D vs 2D mapping and handle column wrapping.
         */
        unsigned align = color256 && !objMapping1D;
        unsigned charBase = (tileNum & ~(int)align) * 0x20;
        unsigned maskLo = objMapping1D ? 0x7FFEu : 0x3FEu;
        unsigned maskHi = objMapping1D ? 0 : (charBase & 0x7C00u);
        int stride = objMapping1D
                   ? (color256 ? objW : (objW >> 1))
                   : 0x80;
        /* vramBase points to OBJ character data (VRAM + 0x10000) */
        const uint16_t* vramBase = &v->vram[0x10000 >> 1];

        int mosaicWait = 0;
        uint32_t mosaicSpritePixel = FLAG_UNWRITTEN;

        for (int sx = 0; sx < boundW; sx++) {
            int screenX = objX + sx;
            if (screenX < 0 || screenX >= GBA_SCREEN_WIDTH) continue;

            /* Mosaic: replicate sprite pixel */
            if (useMosaic && mosaicH > 1) {
                if (mosaicWait > 0) {
                    mosaicWait--;
                    if (mosaicSpritePixel != FLAG_UNWRITTEN) {
                        uint32_t prioKey = ((uint32_t)priority << 8) | (uint32_t)i;
                        if (prioKey < v->objPrio[screenX]) {
                            v->objBuffer[screenX] = mosaicSpritePixel;
                            v->objPrio[screenX] = prioKey;
                        }
                    }
                    continue;
                }
                mosaicWait = mosaicH - 1;
            }

            int texX, texY;
            if (isTransformed) {
                int dx = sx - halfW;
                int dy = effectiveLocalY - halfH;
                texX = ((pa_mat * dx + pb_mat * dy) >> 8) + objW / 2;
                texY = ((pc_mat * dx + pd_mat * dy) >> 8) + objH / 2;
            } else {
                texX = hflip ? (objW - 1 - sx) : sx;
                texY = vflip ? (objH - 1 - effectiveLocalY) : effectiveLocalY;
            }

            if (texX < 0 || texX >= objW || texY < 0 || texY >= objH) {
                mosaicSpritePixel = FLAG_UNWRITTEN;
                continue;
            }

            /* Fetch a pixel using mask-based VRAM addressing.
             * This handles both 1D and 2D tile mapping, including
             * column wrapping in 2D mode.
             */
            uint8_t palIndex;
            uint16_t color;
            unsigned tileData;

            if (color256) {
                /* 8bpp: SPRITE_XBASE_256 / SPRITE_YBASE_256 */
                unsigned xBase = (texX & ~0x7) * 8 + (texX & 6);
                unsigned yBase = (texY & ~0x7) * (unsigned)stride + (texY & 0x7) * 8 + maskHi;
                unsigned addr = (yBase + ((xBase + charBase) & maskLo)) & 0x7FFEu;
                tileData = vram_half(vramBase, addr);
                palIndex = (tileData >> ((texX & 1) << 3)) & 0xFF;
                if (palIndex == 0) { mosaicSpritePixel = FLAG_UNWRITTEN; continue; }
                color = v->palette[256 + palIndex];
            } else {
                /* 4bpp: SPRITE_XBASE_16 / SPRITE_YBASE_16 */
                unsigned xBase = (texX & ~0x7) * 4 + ((texX >> 1) & 2);
                unsigned yBase = (texY & ~0x7) * (unsigned)stride + (texY & 0x7) * 4 + maskHi;
                unsigned addr = (yBase + ((xBase + charBase) & maskLo)) & 0x7FFEu;
                tileData = vram_half(vramBase, addr);
                palIndex = (tileData >> ((texX & 3) << 2)) & 0xF;
                if (palIndex == 0) { mosaicSpritePixel = FLAG_UNWRITTEN; continue; }
                color = v->palette[256 + palBank * 16 + palIndex];
            }

            uint32_t pixel = spriteFlags | (color & 0x7FFF);

            /* OBJ window: set flag on row rather than drawing */
            if (isObjWin) {
                v->scanline[screenX] |= FLAG_OBJWIN;
                mosaicSpritePixel = FLAG_UNWRITTEN;
                continue;
            }

            /* Priority comparison: lower key = higher priority.
             * Key = (priority << 8) | oamIndex
             * Lower OAM index → smaller key → higher priority.
             * Since we iterate 127→0, lower indices come later and
             * their smaller keys satisfy the < check, overwriting. */
            uint32_t prioKey = ((uint32_t)priority << 8) | (uint32_t)i;
            if (prioKey < v->objPrio[screenX]) {
                v->objBuffer[screenX] = pixel;
                v->objPrio[screenX] = prioKey;
            }

            mosaicSpritePixel = pixel;
        }
    }
}
