/** @file composite.c
 * @brief Priority-packed pixel compositing, alpha blending via
 * cupid_color_mix, and brightness increase/decrease.
 *
 * The compositing model:
 *   1. PreprocessBuffer fills row[] with backdrop (palette[0]) at max priority.
 *   2. BG/sprite renderers write into row[] using packed priority comparison.
 *   3. PostprocessBuffer applies remaining alpha blend (target1 vs backdrop)
 *      and brightness increase/decrease for semi-transparent OBJs.
 *   4. The final pass converts 15-bit colors to RGBA8888 into outputBuffer.
 */

#include "cupid/gba.h"

/**
 * @brief Fill the scanline buffer with the backdrop colour.
 *
 * The backdrop is palette[0] at maximum priority with @c FLAG_UNWRITTEN set.
 * Any real layer pixel has a lower priority value and will therefore replace it
 * during the compositing passes.
 *
 * @param gba       GBA context.
 * @param startX    First pixel column to initialise (inclusive).
 * @param endX      Last pixel column to initialise (exclusive).
 * @param target1Bd Whether the backdrop is blend target 1; when @c true and a
 *                  brightness effect is active, the backdrop pixel is
 *                  pre-brightened or pre-darkened before being written.
 */
void cupid_render_preprocess_buffer(struct CupidGBA* gba, int startX, int endX,
                                    bool target1Bd) {
    struct CupidVideo* v = &gba->video;
    uint16_t backdropColor = v->palette[0] & 0x7FFF;

    /* Build backdrop pixel: highest possible priority + BG flag + unwritten */
    uint32_t backdrop = FLAG_UNWRITTEN | FLAG_PRIORITY | FLAG_IS_BACKGROUND
                      | (uint32_t)backdropColor;

    /* If backdrop is blend target1 and a brightness effect is active, pre-blend */
    if (target1Bd) {
        int effect = BLDCNT_EFFECT(v->blend.bldcnt);
        int evy = v->blend.bldy & 0x1F;
        if (evy > 16) evy = 16;
        if (effect == BLEND_BRIGHTEN) {
            backdrop = (backdrop & 0xFFFF8000u) | cupid_brighten(backdropColor, evy);
        } else if (effect == BLEND_DARKEN) {
            backdrop = (backdrop & 0xFFFF8000u) | cupid_darken(backdropColor, evy);
        }
    }

    for (int x = startX; x < endX; x++) {
        v->scanline[x] = backdrop;
    }
}

/**
 * @brief Merge sprite pixels for a given priority level into the scanline.
 *
 * Iterates @c objBuffer[] and composites every sprite pixel whose priority
 * matches @p priority into @c scanline[]. @c FLAG_REBLEND is preserved on
 * semi-transparent OBJ pixels so that subsequent BG layers can alpha-blend
 * against them through the standard compositing path.
 *
 * @param gba          GBA context.
 * @param priority     Sprite priority level to process (0–3).
 * @param startX       First pixel column to process (inclusive).
 * @param endX         Last pixel column to process (exclusive).
 * @param blendEnabled Whether alpha blending is active for this pass.
 */
void cupid_render_postprocess_sprites(struct CupidGBA* gba, unsigned priority,
                                      int startX, int endX, bool blendEnabled) {
    struct CupidVideo* v = &gba->video;
    int blda = v->blend.bldalpha & 0x1F;
    int bldb = (v->blend.bldalpha >> 8) & 0x1F;
    if (blda > 16) blda = 16;
    if (bldb > 16) bldb = 16;

    uint32_t priorityFlags = (uint32_t)priority << OFFSET_PRIORITY;

    /* FLAG_TARGET_2 is added to OBJ pixels here so that when a BG layer
     * (target1) composites later, it can alpha-blend against the sprite.
     */
    bool target2Obj = BLDCNT_TARGET2_OBJ(v->blend.bldcnt);
    uint32_t objTarget2 = target2Obj ? FLAG_TARGET_2 : 0;

    for (int x = startX; x < endX; x++) {
        uint32_t color = v->objBuffer[x];
        if ((color & FLAG_UNWRITTEN) == FLAG_UNWRITTEN) continue;

        /* Check if this sprite pixel's priority matches */
        if ((color & FLAG_PRIORITY) != priorityFlags) continue;

        uint32_t current = v->scanline[x];

        /* Merge TARGET_2 flag and composite */
        if (blendEnabled) {
            cupid_composite_blend_no_objwin(&v->scanline[x], color | objTarget2,
                                            current, blda, bldb);
        } else {
            cupid_composite_no_blend_no_objwin(&v->scanline[x], color | objTarget2,
                                               current);
        }
    }
}

/**
 * @brief Apply final blending and brightness effects to the completed scanline.
 *
 * Called after all BG layers and sprites have been composited into
 * @c scanline[]. Performs three sequential passes:
 *   1. Alpha blend: any remaining @c FLAG_TARGET_1 pixels are blended against
 *      the backdrop when @c BLDCNT targets the backdrop as target 2.
 *   2. Brightness: semi-transparent sprites (@c FLAG_REBLEND set,
 *      @c FLAG_IS_BACKGROUND clear) that were not consumed by alpha blending
 *      receive a brightness increase or decrease.
 *   3. Brightness: @c FLAG_TARGET_1 pixels in brighten/darken mode receive the
 *      EVY brightness adjustment.
 *
 * @note The compositing functions strip the upper flags of winning pixels down
 *       to @c COMPOSITE_KEEP_MASK, so @c FLAG_TARGET_1 only survives on pixels
 *       that were never overwritten (backdrop or topmost layer).
 *
 * @param gba GBA context.
 * @param y   Scanline row index to finalise and write to @c outputBuffer.
 */
void cupid_render_postprocess(struct CupidGBA* gba, int y) {
    struct CupidVideo* v = &gba->video;
    int effect = BLDCNT_EFFECT(v->blend.bldcnt);
    int evy = v->blend.bldy & 0x1F;
    if (evy > 16) evy = 16;

    int blda = v->blend.bldalpha & 0x1F;
    int bldb = (v->blend.bldalpha >> 8) & 0x1F;
    if (blda > 16) blda = 16;
    if (bldb > 16) bldb = 16;

    bool target2Bd = BLDCNT_TARGET2_BD(v->blend.bldcnt);
    uint16_t backdrop = v->palette[0] & 0x7FFF;

    /* Output row pointer for the final BGR555 → RGBA8888 conversion */
    uint32_t* outRow = &v->outputBuffer[y * GBA_SCREEN_WIDTH];

    /* Pass 1: Alpha blend remaining TARGET_1 pixels with the backdrop.
     * Handles the case where a target1 layer pixel has no target2 behind it
     * (only backdrop).
     */
    if (target2Bd && effect == BLEND_ALPHA) {
        for (int x = 0; x < GBA_SCREEN_WIDTH; x++) {
            uint32_t pixel = v->scanline[x];
            if (pixel & FLAG_TARGET_1) {
                uint16_t color = (uint16_t)(pixel & 0x7FFF);
                uint16_t mixed = (uint16_t)cupid_color_mix(blda, color, bldb, backdrop);
                v->scanline[x] = (pixel & 0xFFFF8000u) | mixed;
            }
        }
    }

    /* Pass 2: Brightness for semi-transparent sprites (FLAG_REBLEND).
     * REBLEND sprites that weren't alpha-blended during compositing
     * get brightness increase/decrease applied here.
     */
    if (effect == BLEND_BRIGHTEN || effect == BLEND_DARKEN) {
        uint32_t mask  = FLAG_REBLEND | FLAG_IS_BACKGROUND;
        uint32_t match = FLAG_REBLEND; /* REBLEND set, IS_BACKGROUND clear = sprite */
        for (int x = 0; x < GBA_SCREEN_WIDTH; x++) {
            uint32_t pixel = v->scanline[x];
            if ((pixel & mask) == match) {
                uint16_t color = (uint16_t)(pixel & 0x7FFF);
                if (effect == BLEND_BRIGHTEN) {
                    color = (uint16_t)cupid_brighten(color, evy);
                } else {
                    color = (uint16_t)cupid_darken(color, evy);
                }
                v->scanline[x] = (pixel & 0xFFFF8000u) | color;
            }
        }
    }

    /* Pass 3: Apply brightness to remaining TARGET_1 pixels (non-alpha mode).
     * In brighten/darken mode, BG/OBJ pixels that still carry FLAG_TARGET_1
     * receive the EVY brightness adjustment.
     */
    if (effect == BLEND_BRIGHTEN || effect == BLEND_DARKEN) {
        for (int x = 0; x < GBA_SCREEN_WIDTH; x++) {
            uint32_t pixel = v->scanline[x];
            if (pixel & FLAG_TARGET_1) {
                uint16_t color = (uint16_t)(pixel & 0x7FFF);
                if (effect == BLEND_BRIGHTEN) {
                    color = (uint16_t)cupid_brighten(color, evy);
                } else {
                    color = (uint16_t)cupid_darken(color, evy);
                }
                v->scanline[x] = (pixel & 0xFFFF8000u) | color;
            }
        }
    }

    /* Final pass: convert 15-bit BGR555 to RGBA8888 for display */
    for (int x = 0; x < GBA_SCREEN_WIDTH; x++) {
        outRow[x] = color16_to_rgba(v->scanline[x] & 0x7FFF);
    }
}
