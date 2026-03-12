/** @file mode0.c
 * @brief Text background rendering (modes 0/1 text layers).
 * Handles tile fetch, 4bpp/8bpp decode, hflip/vflip, mosaic, scrolling,
 * and multi-screen nametable layouts.
 *
 * The GBA text BG format:
 *   - Tiles: 8x8 pixels, either 4bpp (32 bytes) or 8bpp (64 bytes)
 *   - Tile map: 32x32 entries per screen (2 bytes each)
 *   - Map entry: bits 0-9 = tile number, bit 10 = hflip, bit 11 = vflip,
 *     bits 12-15 = palette bank (4bpp only)
 *   - Screen sizes: 256x256 (1 screen), 512x256, 256x512, 512x512
 *   - BGCNT selects char base (tile data) and screen base (tile map)
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
 * @brief Render one scanline of a text-mode background layer.
 *
 * Handles tile fetch, 4bpp/8bpp palette decode, horizontal/vertical flip,
 * optional mosaic, scrolling, and multi-screen nametable layouts.  The
 * resulting pixels are composited into @c video.scanline[] using the
 * priority-packed compositing helpers.
 *
 * @param gba    GBA context.
 * @param bg     Background layer index (0–3).
 * @param y      Scanline row to render.
 * @param flags  Pre-built compositing flags for this layer (priority, target
 *               bits, etc.).
 * @param startX First pixel column to render (inclusive).
 * @param endX   Last pixel column to render (exclusive).
 */
void cupid_render_bg_mode0(struct CupidGBA* gba, int bg, int y,
                           uint32_t flags, int startX, int endX) {
    struct CupidVideo* v = &gba->video;
    struct CupidBackground* b = &v->bg[bg];
    uint16_t cnt = b->cnt;

    int charBase   = BGCNT_CHARBASE(cnt) * 0x4000;   /* tile data offset in bytes */
    int screenBase = BGCNT_SCREENBASE(cnt) * 0x800;   /* tile map offset in bytes */
    bool color256  = BGCNT_256COLOR(cnt);              /* 0 = 4bpp, 1 = 8bpp */
    int screenSize = BGCNT_SIZE(cnt);
    bool mosaic    = BGCNT_MOSAIC(cnt);

    /* Screen dimensions in tiles */
    int screenW = (screenSize & 1) ? 64 : 32;
    int screenH = (screenSize & 2) ? 64 : 32;

    int scrollX = b->hofs & 0x1FF;
    int scrollY = b->vofs & 0x1FF;

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

    /* Mosaic: snap Y to mosaic grid */
    int mosaicH = 1, mosaicV = 1;
    if (mosaic) {
        mosaicH = MOSAIC_BG_H(v->mosaic) + 1;
        mosaicV = MOSAIC_BG_V(v->mosaic) + 1;
    }
    int srcY = y;
    if (mosaic && mosaicV > 1) {
        srcY = y - (y % mosaicV);
    }
    srcY = (srcY + scrollY) % (screenH * 8);

    int tileY = srcY / 8;
    int pixY  = srcY % 8;

    /* Mosaic: track X sampling */
    int mosaicWait = 0;
    uint32_t mosaicPixel = FLAG_UNWRITTEN;

    for (int x = startX; x < endX; x++) {
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

        int srcX = (x + scrollX) % (screenW * 8);
        int tileX = srcX / 8;
        int pixX  = srcX % 8;

        /* Multi-screen nametable layout:
         *   Size 0: 32x32 (1 screen)
         *   Size 1: 64x32 (2 screens horizontal)
         *   Size 2: 32x64 (2 screens vertical)
         *   Size 3: 64x64 (4 screens, H then V)
         */
        int mapIndex = 0;
        int localTileX = tileX;
        int localTileY = tileY;

        if (screenSize == 1 || screenSize == 3) {
            if (tileX >= 32) {
                mapIndex += 1;
                localTileX -= 32;
            }
        }
        if (screenSize >= 2) {
            if (tileY >= 32) {
                mapIndex += (screenSize == 3) ? 2 : 1;
                localTileY -= 32;
            }
        }

        uint32_t mapAddr = screenBase + mapIndex * 0x800
                         + (localTileY * 32 + localTileX) * 2;
        uint16_t entry = vram_half(v->vram, mapAddr);

        int tileNum = BG_MAP_TILE(entry);
        bool hflip  = BG_MAP_HFLIP(entry) != 0;
        bool vflip  = BG_MAP_VFLIP(entry) != 0;
        int palBank = BG_MAP_PALETTE(entry);

        int fx = hflip ? (7 - pixX) : pixX;
        int fy = vflip ? (7 - pixY) : pixY;

        uint8_t palIndex;
        uint16_t color;

        if (color256) {
            /* 8bpp: 64 bytes per tile, 8 bytes per row */
            uint32_t addr = charBase + tileNum * 64 + fy * 8 + fx;
            /* Skip BG tile data that falls into OBJ VRAM region */
            if (addr >= 0x10000) {
                mosaicPixel = FLAG_UNWRITTEN;
                continue;
            }
            palIndex = vram_byte(v->vram, addr);
            if (palIndex == 0) {
                mosaicPixel = FLAG_UNWRITTEN;
                continue;
            }
            color = v->palette[palIndex];
        } else {
            /* 4bpp: 32 bytes per tile, 4 bytes per row */
            uint32_t addr = charBase + tileNum * 32 + fy * 4 + (fx / 2);
            /* Skip BG tile data that falls into OBJ VRAM region */
            if (addr >= 0x10000) {
                mosaicPixel = FLAG_UNWRITTEN;
                continue;
            }
            uint8_t byte = vram_byte(v->vram, addr);
            palIndex = (fx & 1) ? (byte >> 4) : (byte & 0xF);
            if (palIndex == 0) {
                mosaicPixel = FLAG_UNWRITTEN;
                continue;
            }
            color = v->palette[palBank * 16 + palIndex];
        }

        uint32_t pixel = flags | (color & 0x7FFF);
        uint32_t current = v->scanline[x];
        if (IS_WRITABLE(current)) {
            cupid_composite(&v->scanline[x], pixel, current, blendActive, objwinActive, blda, bldb);
        }
        mosaicPixel = pixel;
    }
}
