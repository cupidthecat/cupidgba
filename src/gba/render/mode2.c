/** @file mode2.c
 * @brief Affine/rotation background rendering (modes 1/2 affine layers, BG2/BG3).
 * Handles affine transform, pixel sampling, wrapping/clamping, mosaic.
 *
 * Affine BG format:
 *   - Always 8bpp (256 color, single palette)
 *   - Map entries are single bytes (tile index)
 *   - Map dimensions: 16/32/64/128 tiles (128/256/512/1024 pixels)
 *   - Uses running affine coordinates (sx, sy) updated per scanline
 *   - Each pixel: texCoord = (pa*dx + pb*dy + sx, pc*dx + pd*dy + sy) >> 8
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
 * @brief Render one scanline of an affine background layer.
 *
 * Samples the affine-transformed texture for each pixel column in
 * [@p startX, @p endX), compositing results into @c video.scanline[].
 * Supports wrapping/clamping at the map boundary, optional mosaic, and
 * the priority-packed alpha-blend compositing path.
 *
 * The affine coordinates @c bg.sx / @c bg.sy are consumed as-is (they must
 * already reflect the current scanline). Updating them for the next scanline
 * is the caller's responsibility.
 *
 * @param gba    GBA context.
 * @param bg     Background layer index (2 or 3 for affine layers).
 * @param y      Scanline row being rendered (unused internally; reserved).
 * @param flags  Pre-built compositing flags for this layer (priority, target
 *               bits, etc.).
 * @param startX First pixel column to render (inclusive).
 * @param endX   Last pixel column to render (exclusive).
 */
void cupid_render_bg_mode2(struct CupidGBA* gba, int bg, int y,
                           uint32_t flags, int startX, int endX) {
    (void)y;
    struct CupidVideo* v = &gba->video;
    struct CupidBackground* b = &v->bg[bg];
    uint16_t cnt = b->cnt;

    int charBase   = BGCNT_CHARBASE(cnt) * 0x4000;
    int screenBase = BGCNT_SCREENBASE(cnt) * 0x800;
    int screenSize = BGCNT_SIZE(cnt);
    bool overflow  = BGCNT_OVERFLOW(cnt);  /* 1 = wrap, 0 = transparent outside */
    bool mosaic    = BGCNT_MOSAIC(cnt);

    int mapDim = 16 << screenSize;   /* tiles: 16, 32, 64, or 128 */
    int pixDim = mapDim * 8;         /* pixels per dimension */
    int sizeMask = pixDim - 1;       /* for wrapping: pixDim is always power of 2 */

    /* Compositing state: blend composite when the BG has TARGET_2, so that
     * a losing BG pixel can alpha-blend with a winning TARGET_1 pixel
     * (e.g., semi-transparent sprite).
     */
    bool blendActive = (flags & FLAG_TARGET_2) != 0;
    bool objwinActive = DISPCNT_OBJWIN_ENABLE(v->dispcnt);
    int blda = v->blend.bldalpha & 0x1F;
    int bldb = (v->blend.bldalpha >> 8) & 0x1F;
    if (blda > 16) blda = 16;
    if (bldb > 16) bldb = 16;

    /* Use running affine coordinates.
     * sx, sy are 24.8 fixed-point, set at VBlank to refx/refy,
     * then incremented by pb/pd each scanline.
     */
    int32_t cx = b->sx;
    int32_t cy = b->sy;

    /* Per-pixel increment along the scanline */
    int16_t pa = b->pa;
    int16_t pc = b->pc;

    /* Advance to startX */
    cx += pa * startX;
    cy += pc * startX;

    /** Mosaic */
    int mosaicH = 1;
    int mosaicWait = 0;
    uint32_t mosaicPixel = FLAG_UNWRITTEN;
    if (mosaic) {
        mosaicH = MOSAIC_BG_H(v->mosaic) + 1;
    }

    for (int x = startX; x < endX; x++) {
        int texX = cx >> 8;
        int texY = cy >> 8;

        cx += pa;
        cy += pc;

        /* Mosaic: replicate pixel */
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

        if (overflow) {
            /* Wrap: mask coordinates to valid range */
            texX = texX & sizeMask;
            texY = texY & sizeMask;
        } else {
            /* Clamp: pixels outside the map are transparent */
            if (texX < 0 || texX >= pixDim || texY < 0 || texY >= pixDim) {
                mosaicPixel = FLAG_UNWRITTEN;
                continue;
            }
        }

        int tileX = texX >> 3;
        int tileY = texY >> 3;
        int pixPosX = texX & 7;
        int pixPosY = texY & 7;

        /* Map is single-byte entries, mapDim tiles per row */
        uint32_t mapAddr = screenBase + tileY * mapDim + tileX;
        uint8_t tileNum = vram_byte(v->vram, mapAddr);

        /* 8bpp tile data: 64 bytes per tile, 8 bytes per row.
         * BG tile addresses are clipped at 0x10000 (OBJ VRAM boundary).
         */
        uint32_t charAddr = charBase + tileNum * 64 + pixPosY * 8 + pixPosX;
        if (charAddr >= 0x10000) {
            mosaicPixel = FLAG_UNWRITTEN;
            continue;
        }
        uint8_t palIndex = vram_byte(v->vram, charAddr);

        if (palIndex == 0) {
            mosaicPixel = FLAG_UNWRITTEN;
            continue;
        }

        uint16_t color = v->palette[palIndex];
        uint32_t pixel = flags | (color & 0x7FFF);
        uint32_t current = v->scanline[x];
        if (IS_WRITABLE(current)) {
            cupid_composite(&v->scanline[x], pixel, current, blendActive, objwinActive, blda, bldb);
        }
        mosaicPixel = pixel;
    }

    /* Affine coordinates for the next scanline are updated by the caller. */
}
