/** @file mode3.c
 * @brief Bitmap mode 3: 240×160, 16-bit direct color (BGR555).
 * BG2 only, uses affine transform for rotation/scaling.
 *
 * VRAM layout: 240 * 160 * 2 = 76,800 bytes starting at 0x06000000.
 * Each pixel is a 15-bit BGR555 color (bit 15 unused).
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
 * @brief Render one scanline of bitmap mode 3 (BG2, 240×160 direct colour).
 *
 * Samples the 15-bit BGR555 framebuffer through the BG2 affine transform and
 * composites each pixel into @c video.scanline[]. Pixels that map outside the
 * 240×160 canvas are treated as transparent. Optional mosaic is supported.
 *
 * @param gba    GBA context.
 * @param y      Scanline row being rendered (unused internally; reserved for
 *               future use).
 * @param flags  Pre-built compositing flags for BG2 (priority, target bits,
 *               etc.).
 * @param startX First pixel column to render (inclusive).
 * @param endX   Last pixel column to render (exclusive).
 */
void cupid_render_bg_mode3(struct CupidGBA* gba, int y,
                           uint32_t flags, int startX, int endX) {
    (void)y;
    struct CupidVideo* v = &gba->video;
    struct CupidBackground* b = &v->bg[2];
    bool mosaic = BGCNT_MOSAIC(b->cnt);

    /* Affine coordinates for the starting column */
    int32_t cx = b->sx + b->pa * startX;
    int32_t cy = b->sy + b->pc * startX;
    int16_t pa = b->pa;
    int16_t pc = b->pc;

    /* Compositing state */
    bool blendActive = (flags & FLAG_TARGET_2) != 0;
    bool objwinActive = DISPCNT_OBJWIN_ENABLE(v->dispcnt);
    int blda = v->blend.bldalpha & 0x1F;
    int bldb = (v->blend.bldalpha >> 8) & 0x1F;
    if (blda > 16) blda = 16;
    if (bldb > 16) bldb = 16;

    /* Mosaic */
    int mosaicH = 1, mosaicWait = 0;
    uint32_t mosaicPixel = FLAG_UNWRITTEN;
    if (mosaic) {
        mosaicH = MOSAIC_BG_H(v->mosaic) + 1;
    }

    for (int x = startX; x < endX; x++) {
        int texX = cx >> 8;
        int texY = cy >> 8;
        cx += pa;
        cy += pc;

        if (mosaic && mosaicH > 1) {
            if (mosaicWait > 0) {
                mosaicWait--;
                uint32_t current = v->scanline[x];
                if (mosaicPixel != FLAG_UNWRITTEN && IS_WRITABLE(current)) {
                    cupid_composite(&v->scanline[x], mosaicPixel, current, blendActive, objwinActive, blda, bldb);
                }
                continue;
            }
            mosaicWait = mosaicH - 1;
        }

        /* Bounds check: 240×160 canvas */
        if (texX < 0 || texX >= GBA_SCREEN_WIDTH || texY < 0 || texY >= GBA_SCREEN_HEIGHT) {
            mosaicPixel = FLAG_UNWRITTEN;
            continue;
        }

        uint32_t offset = (texY * GBA_SCREEN_WIDTH + texX) * 2;
        uint16_t color = vram_half(v->vram, offset) & 0x7FFF;

        uint32_t pixel = flags | color;
        uint32_t current = v->scanline[x];
        if (IS_WRITABLE(current)) {
            cupid_composite(&v->scanline[x], pixel, current, blendActive, objwinActive, blda, bldb);
        }
        mosaicPixel = pixel;
    }
}
