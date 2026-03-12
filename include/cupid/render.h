/**
 * @file render.h
 * @brief Software renderer interface: draws scanlines into the framebuffer.
 *
 * The renderer uses a priority-packed uint32_t scanline row[] buffer.
 * Upper bits encode priority + layer + blend flags; lower bits hold color.
 * Layers are drawn in priority order (0 = highest) and composed into
 * the row buffer. A postprocess pass applies brightness/alpha blending.
 */

#ifndef CUPID_RENDER_H
#define CUPID_RENDER_H

#include "cupid/types.h"
#include "cupid/video.h"

struct CupidGBA;

/**
 * @name Pixel Flag Packing
 *
 * Layout of a packed pixel in the row[] / spriteLayer[] buffers:
 *   bits 31-30 : priority (0-3, lower = higher priority)
 *   bits 29-28 : layer index (0-3 BG, 4 OBJ)
 *   bit  27    : FLAG_IS_BACKGROUND
 *   bit  26    : FLAG_REBLEND (semi-transparent OBJ, force re-blend)
 *   bit  25    : FLAG_TARGET_1
 *   bit  24    : FLAG_TARGET_2 / FLAG_OBJWIN (shared bit)
 *   bits 23-0  : 24-bit color (we store 15-bit GBA color in lower bits,
 *                or pre-converted native color)
 *
 */
#define OFFSET_PRIORITY    30
#define OFFSET_INDEX       28

#define FLAG_PRIORITY      0xC0000000u
#define FLAG_INDEX         0x30000000u
#define FLAG_IS_BACKGROUND 0x08000000u
#define FLAG_UNWRITTEN     0xFC000000u
#define FLAG_REBLEND       0x04000000u
#define FLAG_TARGET_1      0x02000000u
#define FLAG_TARGET_2      0x01000000u
#define FLAG_OBJWIN        0x01000000u
#define FLAG_ORDER_MASK    0xF8000000u

#define IS_WRITABLE(PIXEL) ((PIXEL) & 0xFE000000u)

/** Convenience macros for building packed pixels */
#define PIXEL_MAKE_BG(color, prio, bg_idx, target1, target2) \
    (((uint32_t)(prio) << OFFSET_PRIORITY)                   \
   | ((uint32_t)(bg_idx) << OFFSET_INDEX)                    \
   | FLAG_IS_BACKGROUND                                      \
   | ((target1) ? FLAG_TARGET_1 : 0u)                        \
   | ((target2) ? FLAG_TARGET_2 : 0u)                        \
   | (uint32_t)(color))

#define PIXEL_MAKE_OBJ(color, prio, target1) \
    (((uint32_t)(prio) << OFFSET_PRIORITY)   \
   | ((target1) ? FLAG_TARGET_1 : 0u)        \
   | (uint32_t)(color))

/** Legacy compat macros (used by existing render.c, will remove later) */
#define PIXEL_FLAG_PRIORITY_SHIFT  OFFSET_PRIORITY
#define PIXEL_FLAG_LAYER_SHIFT     OFFSET_INDEX
#define PIXEL_FLAG_BG              FLAG_IS_BACKGROUND
#define PIXEL_FLAG_OBJ             0x04000000u
#define PIXEL_FLAG_TARGET1         FLAG_TARGET_1
#define PIXEL_FLAG_TARGET2         FLAG_TARGET_2
#define PIXEL_FLAG_UNWRITTEN       FLAG_UNWRITTEN

#define PIXEL_MAKE(color, prio, layer) \
    (((uint32_t)(prio) << OFFSET_PRIORITY) | \
     ((uint32_t)(layer) << OFFSET_INDEX) | \
     FLAG_IS_BACKGROUND | (uint32_t)(color))

/**
 * @name Color Blending Helpers
 *
 * Operate on 15-bit BGR555 colors packed in the lower bits of uint32_t.
 */

/** Brightness increase: component += (31 - component) * evy / 16 */
static inline uint32_t cupid_brighten(uint32_t color, int evy) {
    unsigned c = 0;
    unsigned a;
    a = color & 0x1F;
    c |= (a + ((0x1F - a) * evy) / 16) & 0x1F;
    a = color & 0x3E0;
    c |= (a + ((0x3E0 - a) * evy) / 16) & 0x3E0;
    a = color & 0x7C00;
    c |= (a + ((0x7C00 - a) * evy) / 16) & 0x7C00;
    return c;
}

/** Brightness decrease: component -= component * evy / 16 */
static inline uint32_t cupid_darken(uint32_t color, int evy) {
    unsigned c = 0;
    unsigned a;
    a = color & 0x1F;
    c |= (a - (a * evy) / 16) & 0x1F;
    a = color & 0x3E0;
    c |= (a - (a * evy) / 16) & 0x3E0;
    a = color & 0x7C00;
    c |= (a - (a * evy) / 16) & 0x7C00;
    return c;
}

/**
 * Alpha blend two 15-bit colors: out = (a * eva + b * evb) / 16, clamped.
 */
static inline uint32_t cupid_color_mix(int eva, uint32_t colorA, int evb, uint32_t colorB) {
    unsigned r = ((colorA & 0x1F) * eva + (colorB & 0x1F) * evb) >> 4;
    unsigned g = (((colorA >> 5) & 0x1F) * eva + ((colorB >> 5) & 0x1F) * evb) >> 4;
    unsigned b = (((colorA >> 10) & 0x1F) * eva + ((colorB >> 10) & 0x1F) * evb) >> 4;
    if (r > 31) r = 31;
    if (g > 31) g = 31;
    if (b > 31) b = 31;
    return r | (g << 5) | (b << 10);
}

/**
 * @name Compositing Helpers
 *
 * Four functions for {Blend,NoBlend} x {Objwin,NoObjwin}.
 *
 * These compare packed pixel `color` against `current` in the row buffer.
 * Since priority is in the top bits, color < current means color has higher
 * priority and should replace.
 *
 * KEY: all four functions ALWAYS write to *pixel. When the new pixel "loses"
 * (color >= current), the existing pixel is "locked in" by stripping its
 * upper priority/target flags: current & (0x00FFFFFF | FLAG_REBLEND | FLAG_OBJWIN).
 * This prevents subsequent lower-priority layers from disturbing it.
 */

#define COMPOSITE_KEEP_MASK (0x00FFFFFFu | FLAG_REBLEND | FLAG_OBJWIN)

/** {Blend, Objwin} — alpha blend + OBJ window flag propagation */
static inline void cupid_composite_blend_objwin(uint32_t* pixel, uint32_t color,
                                                uint32_t current, int blda, int bldb) {
    if (color >= current) {
        if ((current & FLAG_TARGET_1) && (color & FLAG_TARGET_2)) {
            color = cupid_color_mix(blda, current, bldb, color);
        } else {
            color = current & COMPOSITE_KEEP_MASK;
        }
    } else {
        color = (color & ~FLAG_TARGET_2) | (current & FLAG_OBJWIN);
    }
    *pixel = color;
}

/** {Blend, NoObjwin} — alpha blend, no OBJ window handling */
static inline void cupid_composite_blend_no_objwin(uint32_t* pixel, uint32_t color,
                                                   uint32_t current, int blda, int bldb) {
    if (color >= current) {
        if ((current & FLAG_TARGET_1) && (color & FLAG_TARGET_2)) {
            color = cupid_color_mix(blda, current, bldb, color);
        } else {
            color = current & COMPOSITE_KEEP_MASK;
        }
    } else {
        color = color & ~FLAG_TARGET_2;
    }
    *pixel = color;
}

/** {NoBlend, Objwin} — no alpha blend, propagate OBJ window flag */
static inline void cupid_composite_no_blend_objwin(uint32_t* pixel, uint32_t color,
                                                   uint32_t current) {
    if (color < current) {
        color |= (current & FLAG_OBJWIN);
    } else {
        color = current & COMPOSITE_KEEP_MASK;
    }
    *pixel = color;
}

/** {NoBlend, NoObjwin} — simplest: just priority comparison */
static inline void cupid_composite_no_blend_no_objwin(uint32_t* pixel, uint32_t color,
                                                      uint32_t current) {
    if (color >= current) {
        color = current & COMPOSITE_KEEP_MASK;
    }
    *pixel = color;
}

/**
 * Runtime composite dispatcher.
 * Compiler can hoist the loop-invariant blend/objwin bools.
 */
static inline void cupid_composite(uint32_t* pixel, uint32_t color,
                                   uint32_t current, bool blend, bool objwin,
                                   int blda, int bldb) {
    if (blend && objwin) {
        cupid_composite_blend_objwin(pixel, color, current, blda, bldb);
    } else if (blend) {
        cupid_composite_blend_no_objwin(pixel, color, current, blda, bldb);
    } else if (objwin) {
        cupid_composite_no_blend_objwin(pixel, color, current);
    } else {
        cupid_composite_no_blend_no_objwin(pixel, color, current);
    }
}

/** @name Window Control */

/** Window flags: which layers and effects are enabled inside a window region */
struct CupidWinCtrl {
    bool bg[4];
    bool obj;
    bool blend;
};

/** A horizontal window span: from startX (inclusive) to endX (exclusive) */
struct CupidWinSpan {
    int startX;
    int endX;
    struct CupidWinCtrl ctrl;
};

#define MAX_WIN_SPANS GBA_SCREEN_WIDTH

/** @name Public API */

/** Draw one scanline (called during HBlank from video timing) */
void cupid_render_scanline(struct CupidGBA* gba, int y);

/** Individual mode renderers — draw into the row[] buffer */
void cupid_render_bg_mode0(struct CupidGBA* gba, int bg, int y,
                           uint32_t flags, int startX, int endX);
void cupid_render_bg_mode2(struct CupidGBA* gba, int bg, int y,
                           uint32_t flags, int startX, int endX);
void cupid_render_bg_mode3(struct CupidGBA* gba, int y,
                           uint32_t flags, int startX, int endX);
void cupid_render_bg_mode4(struct CupidGBA* gba, int y,
                           uint32_t flags, int startX, int endX);
void cupid_render_bg_mode5(struct CupidGBA* gba, int y,
                           uint32_t flags, int startX, int endX);
void cupid_render_sprites(struct CupidGBA* gba, int y);
void cupid_render_postprocess_sprites(struct CupidGBA* gba, unsigned priority,
                                      int startX, int endX, bool blendEnabled);

/** Compositing: finalize the row[] buffer into outputBuffer */
void cupid_render_postprocess(struct CupidGBA* gba, int y);

/** Window evaluation for a given scanline */
int cupid_render_build_windows(struct CupidGBA* gba, int y,
                               struct CupidWinSpan spans[MAX_WIN_SPANS]);

#endif /* CUPID_RENDER_H */
