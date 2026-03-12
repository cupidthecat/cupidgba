/**
 * @file mode4.c
 * @brief Bitmap mode 4: 240×160, 8-bit paletted, double-buffered.
 *
 * BG2 only, uses the affine transform for rotation and scaling.
 *
 * VRAM layout:
 *   - Page 0: byte offset 0x00000 (240 × 160 = 38,400 bytes)
 *   - Page 1: byte offset 0x0A000
 *
 * @c DISPCNT bit 4 selects the active frame. Each pixel is an 8-bit index
 * into the BG palette (@c palette[0..255]); index 0 is transparent.
 */

#include "cupid/gba.h"

/**
 * @brief Read a single byte from the VRAM word array.
 *
 * @param vram       Pointer to the VRAM array (array of @c uint16_t).
 * @param byteOffset Byte offset into VRAM.
 * @return The byte value at @p byteOffset.
 */
static inline uint8_t vram_byte(const uint16_t* vram, uint32_t byteOffset) {
    uint16_t word = vram[byteOffset >> 1];
    return (byteOffset & 1) ? (word >> 8) : (word & 0xFF);
}

/**
 * @brief Render one scanline of bitmap mode 4 (BG2, 240×160 paletted).
 *
 * Samples the 8-bit paletted framebuffer through the BG2 affine transform,
 * looks up each palette index, and composites the resulting pixel into
 * @c video.scanline[]. Pixels that map outside the 240×160 canvas or have
 * palette index 0 are treated as transparent. The active page is selected by
 * @c DISPCNT bit 4. Optional mosaic is supported.
 *
 * @param gba    GBA context.
 * @param y      Scanline row being rendered (unused internally; reserved).
 * @param flags  Pre-built compositing flags for BG2 (priority, target bits,
 *               etc.).
 * @param startX First pixel column to render (inclusive).
 * @param endX   Last pixel column to render (exclusive).
 */
void cupid_render_bg_mode4(struct CupidGBA* gba, int y,
                           uint32_t flags, int startX, int endX) {
    (void)y;
    struct CupidVideo* v = &gba->video;
    struct CupidBackground* b = &v->bg[2];
    bool mosaic = BGCNT_MOSAIC(b->cnt);

    uint32_t page = DISPCNT_FRAME_SELECT(v->dispcnt) ? 0xA000 : 0;

    /* Affine coordinates */
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

        /* Bounds check: 240×160 */
        if (texX < 0 || texX >= GBA_SCREEN_WIDTH || texY < 0 || texY >= GBA_SCREEN_HEIGHT) {
            mosaicPixel = FLAG_UNWRITTEN;
            continue;
        }

        uint32_t offset = page + texY * GBA_SCREEN_WIDTH + texX;
        uint8_t palIndex = vram_byte(v->vram, offset);

        if (palIndex == 0) {
            mosaicPixel = FLAG_UNWRITTEN;
            continue;
        }

        uint16_t color = v->palette[palIndex] & 0x7FFF;
        uint32_t pixel = flags | color;
        uint32_t current = v->scanline[x];
        if (IS_WRITABLE(current)) {
            cupid_composite(&v->scanline[x], pixel, current, blendActive, objwinActive, blda, bldb);
        }
        mosaicPixel = pixel;
    }
}
