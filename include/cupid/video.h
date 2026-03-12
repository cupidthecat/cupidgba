/**
 * @file video.h
 * @brief GBA video subsystem: timing, palette, VRAM, OAM, renderer interface.
 */

#ifndef CUPID_VIDEO_H
#define CUPID_VIDEO_H

#include "cupid/types.h"
#include "cupid/timing.h"

struct CupidGBA;

/** @name Screen Dimensions */
#define GBA_SCREEN_WIDTH   240
#define GBA_SCREEN_HEIGHT  160

/** @name Video Timing Constants */
#define VIDEO_HDRAW_LENGTH     1008        /**< Cycles per scanline (visible) */
#define VIDEO_HBLANK_LENGTH    224         /**< Cycles per HBlank */
#define VIDEO_HORIZONTAL_LENGTH (VIDEO_HDRAW_LENGTH + VIDEO_HBLANK_LENGTH)  /**< 1232 */

#define VIDEO_HBLANK_PIXELS    68          /**< Pixels in HBlank */
#define VIDEO_VDRAW_LINES      160         /**< Visible scanlines */
#define VIDEO_VBLANK_LINES     68          /**< VBlank scanlines */
#define VIDEO_VERTICAL_TOTAL   228         /**< Total scanlines per frame */

#define VIDEO_TOTAL_LENGTH     (VIDEO_HORIZONTAL_LENGTH * VIDEO_VERTICAL_TOTAL) /**< 280896 */

/** OBJ timing budget */
#define OBJ_HBLANK_FREE_LENGTH 954
#define OBJ_LENGTH             1210

/** Tile base offset */
#define BASE_TILE              0x00010000

/** @name Memory Sizes */
#define GBA_SIZE_PALETTE       0x00000400   /**< 1 KiB = 512 uint16 */
#define GBA_SIZE_VRAM          0x00018000   /**< 96 KiB */
#define GBA_SIZE_OAM           0x00000400   /**< 1 KiB = 128 OBJ + 32 matrices */

/** @name Enums */

/** OBJ rendering modes (Attr0 bits 10-11) */
enum CupidObjMode {
    OBJ_MODE_NORMAL          = 0,
    OBJ_MODE_SEMITRANSPARENT = 1,
    OBJ_MODE_OBJWIN          = 2,
    OBJ_MODE_HIDDEN          = 3   /**< mGBA calls this OBJ_MODE_STEREO / forbidden */
};

/** OBJ shapes (Attr0 bits 14-15) */
enum CupidObjShape {
    OBJ_SHAPE_SQUARE     = 0,
    OBJ_SHAPE_HORIZONTAL = 1,
    OBJ_SHAPE_VERTICAL   = 2
};

/** Blend effects (BLDCNT bits 6-7) */
enum CupidBlendEffect {
    BLEND_NONE     = 0,
    BLEND_ALPHA    = 1,
    BLEND_BRIGHTEN = 2,
    BLEND_DARKEN   = 3
};

/** Video layer identifiers */
enum CupidVideoLayer {
    LAYER_BG0 = 0,
    LAYER_BG1,
    LAYER_BG2,
    LAYER_BG3,
    LAYER_OBJ,
    LAYER_WIN0,
    LAYER_WIN1,
    LAYER_OBJWIN
};

/**
 * @name OBJ Attribute Bitfield Extraction Macros
 */

/** Attr 0 (a) */
#define OBJ_ATTR0_Y(a)           ((a) & 0xFF)
#define OBJ_ATTR0_TRANSFORMED(a) (((a) >> 8) & 1)
#define OBJ_ATTR0_DISABLE(a)     (((a) >> 9) & 1)     /**< Only when !Transformed */
#define OBJ_ATTR0_DOUBLE(a)      (((a) >> 9) & 1)     /**< Only when Transformed */
#define OBJ_ATTR0_MODE(a)        (((a) >> 10) & 3)
#define OBJ_ATTR0_MOSAIC(a)      (((a) >> 12) & 1)
#define OBJ_ATTR0_256COLOR(a)    (((a) >> 13) & 1)
#define OBJ_ATTR0_SHAPE(a)       (((a) >> 14) & 3)

/** Attr 1 (b) */
#define OBJ_ATTR1_X(b)           ((b) & 0x1FF)
#define OBJ_ATTR1_MATINDEX(b)    (((b) >> 9) & 0x1F)  /**< Only when Transformed */
#define OBJ_ATTR1_HFLIP(b)       (((b) >> 12) & 1)    /**< Only when !Transformed */
#define OBJ_ATTR1_VFLIP(b)       (((b) >> 13) & 1)    /**< Only when !Transformed */
#define OBJ_ATTR1_SIZE(b)        (((b) >> 14) & 3)

/** Attr 2 (c) */
#define OBJ_ATTR2_TILE(c)        ((c) & 0x3FF)
#define OBJ_ATTR2_PRIORITY(c)    (((c) >> 10) & 3)
#define OBJ_ATTR2_PALETTE(c)     (((c) >> 12) & 0xF)

/** @name Text BG Map Entry Macros */
#define BG_MAP_TILE(entry)    ((entry) & 0x03FF)
#define BG_MAP_HFLIP(entry)   ((entry) & 0x0400)
#define BG_MAP_VFLIP(entry)   ((entry) & 0x0800)
#define BG_MAP_PALETTE(entry) (((entry) & 0xF000) >> 12)

/** @name DISPCNT Bitfield Macros */
#define DISPCNT_MODE(v)               ((v) & 7)
#define DISPCNT_CGB(v)                (((v) >> 3) & 1)
#define DISPCNT_FRAME_SELECT(v)       (((v) >> 4) & 1)
#define DISPCNT_HBLANK_FREE(v)        (((v) >> 5) & 1)
#define DISPCNT_OBJ_1D(v)             (((v) >> 6) & 1)
#define DISPCNT_FORCED_BLANK(v)       (((v) >> 7) & 1)
#define DISPCNT_BG0_ENABLE(v)         (((v) >> 8) & 1)
#define DISPCNT_BG1_ENABLE(v)         (((v) >> 9) & 1)
#define DISPCNT_BG2_ENABLE(v)         (((v) >> 10) & 1)
#define DISPCNT_BG3_ENABLE(v)         (((v) >> 11) & 1)
#define DISPCNT_OBJ_ENABLE(v)         (((v) >> 12) & 1)
#define DISPCNT_WIN0_ENABLE(v)        (((v) >> 13) & 1)
#define DISPCNT_WIN1_ENABLE(v)        (((v) >> 14) & 1)
#define DISPCNT_OBJWIN_ENABLE(v)      (((v) >> 15) & 1)
#define DISPCNT_BG_ENABLE(v, n)       (((v) >> (8 + (n))) & 1)

/** @name DISPSTAT Bitfield Macros */
#define DISPSTAT_IN_VBLANK(v)         ((v) & 1)
#define DISPSTAT_IN_HBLANK(v)         (((v) >> 1) & 1)
#define DISPSTAT_VCOUNTER(v)          (((v) >> 2) & 1)
#define DISPSTAT_VBLANK_IRQ(v)        (((v) >> 3) & 1)
#define DISPSTAT_HBLANK_IRQ(v)        (((v) >> 4) & 1)
#define DISPSTAT_VCOUNTER_IRQ(v)      (((v) >> 5) & 1)
#define DISPSTAT_VCOUNT_SETTING(v)    (((v) >> 8) & 0xFF)

/** @name BGCNT Bitfield Macros */
#define BGCNT_PRIORITY(v)       ((v) & 3)
#define BGCNT_CHARBASE(v)       (((v) >> 2) & 3)
#define BGCNT_MOSAIC(v)         (((v) >> 6) & 1)
#define BGCNT_256COLOR(v)       (((v) >> 7) & 1)
#define BGCNT_SCREENBASE(v)     (((v) >> 8) & 0x1F)
#define BGCNT_OVERFLOW(v)       (((v) >> 13) & 1)
#define BGCNT_SIZE(v)           (((v) >> 14) & 3)

/** @name BLDCNT Bitfield Macros */
#define BLDCNT_TARGET1_BG(v, n) (((v) >> (n)) & 1)
#define BLDCNT_TARGET1_OBJ(v)   (((v) >> 4) & 1)
#define BLDCNT_TARGET1_BD(v)    (((v) >> 5) & 1)
#define BLDCNT_EFFECT(v)        (((v) >> 6) & 3)
#define BLDCNT_TARGET2_BG(v, n) (((v) >> (8 + (n))) & 1)
#define BLDCNT_TARGET2_OBJ(v)   (((v) >> 12) & 1)
#define BLDCNT_TARGET2_BD(v)    (((v) >> 13) & 1)

/** @name MOSAIC Bitfield Macros */
#define MOSAIC_BG_H(v)  (((v) >>  0) & 0xF)
#define MOSAIC_BG_V(v)  (((v) >>  4) & 0xF)
#define MOSAIC_OBJ_H(v) (((v) >>  8) & 0xF)
#define MOSAIC_OBJ_V(v) (((v) >> 12) & 0xF)

/** @name Window Control Macros */
#define WINCTRL_BG_ENABLE(v, n) (((v) >> (n)) & 1)
#define WINCTRL_OBJ_ENABLE(v)   (((v) >> 4) & 1)
#define WINCTRL_BLEND_ENABLE(v) (((v) >> 5) & 1)

/** Pixel compositing flags are defined in render.h */

/** @name OAM Structures */

/**
 * Single 8-byte OAM entry (4 x uint16).
 * Fields are extracted through the OBJ_ATTR macros above.
 * The fourth halfword (d) is padding; in the affine-matrix interleave
 * those bytes hold the matrix coefficients.
 */
struct CupidOBJ {
    uint16_t a;  /**< Attr0: Y, mode, shape, etc. */
    uint16_t b;  /**< Attr1: X, flip/affine, size */
    uint16_t c;  /**< Attr2: tile, priority, palette */
    uint16_t d;  /**< Padding / affine param interleave */
};

/** 32-byte affine matrix (interleaved with 4 OBJ entries) */
struct CupidOAMMatrix {
    int16_t padding0[3];
    int16_t a;
    int16_t padding1[3];
    int16_t b;
    int16_t padding2[3];
    int16_t c;
    int16_t padding3[3];
    int16_t d;
};

/** 1 KiB OAM: 128 OBJ entries = 32 affine matrices = 512 raw halfwords */
union CupidOAM {
    struct CupidOBJ obj[128];
    struct CupidOAMMatrix mat[32];
    uint16_t raw[512];
};

/** @name Sprite Size Lookup */
/** Indexed by [shape * 4 + size], returns { width, height } */
extern const int CupidObjSizes[16][2];

/** @name Renderer Sprite */
struct CupidRendererSprite {
    struct CupidOBJ obj;
    int16_t y;       /**< Effective Y (sign-extended, etc.) */
    int16_t endY;    /**< Last visible scanline + 1 */
    int16_t cycles;  /**< Rendering cost (for H-blank free check) */
    int8_t  index;   /**< Original OAM index (0-127) */
};

/** @name Background */
struct CupidBackground {
    uint16_t cnt;      /**< BGxCNT register */
    int16_t  hofs;     /**< Horizontal scroll */
    int16_t  vofs;     /**< Vertical scroll */
    /** Affine parameters (BG2/BG3 only) */
    int32_t  refx;     /**< Reference point X (28.4 fixed point, latched) */
    int32_t  refy;     /**< Reference point Y */
    int16_t  pa, pb, pc, pd;  /**< Affine matrix elements */
    int32_t  sx, sy;   /**< Running affine coordinates (internal, updated per scanline) */
};

/** @name Blend State */
struct CupidBlend {
    uint16_t bldcnt;
    uint16_t bldalpha;
    uint16_t bldy;
};

/** @name Window State */
struct CupidWindowRegion {
    uint8_t start;
    uint8_t end;
};

struct CupidWindow {
    uint8_t h[2][2];  /**< WINxH: [win#][left, right] */
    uint8_t v[2][2];  /**< WINxV: [win#][top, bottom] */
    uint16_t winin;
    uint16_t winout;
};

/** @name Video State */
struct CupidVideo {
    struct CupidGBA* gba;

    /** IO registers */
    uint16_t dispcnt;        /**< Display control (0x04000000) */
    uint16_t dispstat;       /**< Display status  (0x04000004) */
    uint16_t vcount;         /**< Current scanline (0x04000006) */
    uint16_t mosaic;         /**< MOSAIC register (0x0400004C) */

    struct CupidBackground bg[4];
    struct CupidBlend blend;
    struct CupidWindow win;

    /** Memory */
    /** Palette RAM: 256 BG + 256 OBJ = 512 x uint16 = 1 KiB  (inline) */
    uint16_t palette[512];

    /** VRAM: 96 KiB  (heap-allocated, pointer) */
    uint16_t* vram;

    /** OAM: 128 OBJ + 32 affine matrices = 1 KiB  (inline union) */
    union CupidOAM oam;

    /** Output */
    /** Per-frame RGBA8888 output buffer (240 x 160) — inline */
    uint32_t outputBuffer[GBA_SCREEN_WIDTH * GBA_SCREEN_HEIGHT];

    /** Timing (event-driven) */
    struct CupidTimingEvent event;  /**< Single event; callback alternates _startHdraw/_startHblank */
    uint32_t frameCounter;

    /** Scanline compositing buffers (software renderer) */
    uint32_t scanline[GBA_SCREEN_WIDTH];
    uint32_t objBuffer[GBA_SCREEN_WIDTH];
    uint32_t objPrio[GBA_SCREEN_WIDTH];

    /** Sorted sprite list for current frame */
    struct CupidRendererSprite sprites[128];
    int spriteCount;
    bool oamDirty;
    int32_t spriteCyclesRemaining;
};

/** @name API */
void cupid_video_init(struct CupidVideo* video, struct CupidGBA* gba);
void cupid_video_reset(struct CupidVideo* video);
void cupid_video_skip_bios(struct CupidVideo* video);
void cupid_video_deinit(struct CupidVideo* video);

/** Called when IO registers are written */
void cupid_video_write_register(struct CupidVideo* video, uint32_t addr, uint16_t val);
uint16_t cupid_video_read_register(struct CupidVideo* video, uint32_t addr);

/** Palette / VRAM / OAM writes (for dirty tracking if needed) */
void cupid_video_write_palette(struct CupidVideo* video, uint32_t offset, uint16_t val);
void cupid_video_write_vram(struct CupidVideo* video, uint32_t offset, uint16_t val);
void cupid_video_write_oam(struct CupidVideo* video, uint32_t offset, uint16_t val);

/** OAM sorting for renderer */
int cupid_video_clean_oam(struct CupidOBJ* oam, struct CupidRendererSprite* sprites, int offsetY);

#endif /* CUPID_VIDEO_H */
