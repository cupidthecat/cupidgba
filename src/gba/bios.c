/**
 * @file bios.c
 * @brief High-Level Emulation of GBA BIOS SWI calls.
 */

#include "cupid/gba.h"
#include <math.h>
#include <stdlib.h>

/**
 * @brief SWI 0x00: SoftReset.
 */
static void _SoftReset(struct CupidGBA* gba) {
    /** Clear 200h bytes of IWRAM (0x03007E00..0x03007FFF) */
    memset(gba->memory.iwram + GBA_SIZE_IWRAM - 0x200, 0, 0x200);

    /** Reset stack pointers (matches the HLE BIOS stub behavior) */
    gba->cpu.gprs[13] = GBA_SP_BASE_SYSTEM;
    cupid_arm_set_priv_mode(&gba->cpu, MODE_IRQ);
    gba->cpu.gprs[13] = GBA_SP_BASE_IRQ;
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SVC);
    gba->cpu.gprs[13] = GBA_SP_BASE_SUPERVISOR;
    cupid_arm_set_priv_mode(&gba->cpu, MODE_SYS);

    /** Check reset flag at 0x03007FFA (0=ROM, non-zero=RAM) */
    uint8_t flag = gba->memory.iwram[GBA_SIZE_IWRAM - 6];
    uint32_t dest = (flag == 0) ? 0x08000000 : 0x02000000;

    gba->cpu.gprs[15] = dest;
    gba->cpu.cpsr.packed = MODE_SYS;
    gba->cpu.executionMode = MODE_ARM;
}

/**
 * @brief SWI 0x01: RegisterRamReset.
 *
 * Matches the reference implementation behavior for register reset.
 */
static void _RegisterRamReset(struct CupidGBA* gba) {
    uint32_t reg = gba->cpu.gprs[0];

    /** Always force blank first */
    cupid_io_write(gba, REG_DISPCNT, 0x0080);

    if (reg & 0x01) memset(gba->memory.ewram, 0, GBA_SIZE_EWRAM);
    if (reg & 0x02) memset(gba->memory.iwram, 0, GBA_SIZE_IWRAM - 0x200);
    if (reg & 0x04) memset(gba->video.palette, 0, GBA_SIZE_PALETTE);
    if (reg & 0x08) memset(gba->video.vram, 0, GBA_SIZE_VRAM);
    if (reg & 0x10) memset(gba->video.oam.raw, 0, GBA_SIZE_OAM);

    if (reg & 0x20) {
        /** Serial IO */
        cupid_io_write(gba, REG_SIOCNT, 0);
        cupid_io_write(gba, REG_RCNT, 0x8000);  /** RCNT_INITIAL */
        cupid_io_write(gba, REG_SIOMLT_SEND, 0);
        cupid_io_write(gba, REG_JOYCNT, 0);
        cupid_io_write(gba, REG_JOY_RECV_LO, 0);
        cupid_io_write(gba, REG_JOY_RECV_HI, 0);
        cupid_io_write(gba, REG_JOY_TRANS_LO, 0);
        cupid_io_write(gba, REG_JOY_TRANS_HI, 0);
    }

    if (reg & 0x40) {
        /** Sound */
        cupid_io_write(gba, REG_SOUND1CNT_L, 0);
        cupid_io_write(gba, REG_SOUND1CNT_H, 0);
        cupid_io_write(gba, REG_SOUND1CNT_X, 0);
        cupid_io_write(gba, REG_SOUND2CNT_L, 0);
        cupid_io_write(gba, REG_SOUND2CNT_H, 0);
        cupid_io_write(gba, REG_SOUND3CNT_L, 0);
        cupid_io_write(gba, REG_SOUND3CNT_H, 0);
        cupid_io_write(gba, REG_SOUND3CNT_X, 0);
        cupid_io_write(gba, REG_SOUND4CNT_L, 0);
        cupid_io_write(gba, REG_SOUND4CNT_H, 0);
        cupid_io_write(gba, REG_SOUNDCNT_L, 0);
        cupid_io_write(gba, REG_SOUNDCNT_H, 0);
        cupid_io_write(gba, REG_SOUNDCNT_X, 0);
        cupid_io_write(gba, REG_SOUNDBIAS, 0x0200);
        /** Zero wave RAM */
        cupid_io_write(gba, REG_WAVE_RAM0_LO, 0);
        cupid_io_write(gba, REG_WAVE_RAM0_HI, 0);
        cupid_io_write(gba, REG_WAVE_RAM1_LO, 0);
        cupid_io_write(gba, REG_WAVE_RAM1_HI, 0);
        cupid_io_write(gba, REG_WAVE_RAM2_LO, 0);
        cupid_io_write(gba, REG_WAVE_RAM2_HI, 0);
        cupid_io_write(gba, REG_WAVE_RAM3_LO, 0);
        cupid_io_write(gba, REG_WAVE_RAM3_HI, 0);
    }

    if (reg & 0x80) {
        /** All other registers */
        cupid_io_write(gba, REG_DISPSTAT, 0);
        /** BG control + scroll */
        cupid_io_write(gba, REG_BG0CNT, 0);
        cupid_io_write(gba, REG_BG1CNT, 0);
        cupid_io_write(gba, REG_BG2CNT, 0);
        cupid_io_write(gba, REG_BG3CNT, 0);
        cupid_io_write(gba, REG_BG0HOFS, 0);
        cupid_io_write(gba, REG_BG0VOFS, 0);
        cupid_io_write(gba, REG_BG1HOFS, 0);
        cupid_io_write(gba, REG_BG1VOFS, 0);
        cupid_io_write(gba, REG_BG2HOFS, 0);
        cupid_io_write(gba, REG_BG2VOFS, 0);
        cupid_io_write(gba, REG_BG3HOFS, 0);
        cupid_io_write(gba, REG_BG3VOFS, 0);
        /** BG2/3 affine (identity) */
        cupid_io_write(gba, REG_BG2PA, 0x0100);
        cupid_io_write(gba, REG_BG2PB, 0);
        cupid_io_write(gba, REG_BG2PC, 0);
        cupid_io_write(gba, REG_BG2PD, 0x0100);
        cupid_io_write(gba, REG_BG2X_LO, 0);
        cupid_io_write(gba, REG_BG2X_HI, 0);
        cupid_io_write(gba, REG_BG2Y_LO, 0);
        cupid_io_write(gba, REG_BG2Y_HI, 0);
        cupid_io_write(gba, REG_BG3PA, 0x0100);
        cupid_io_write(gba, REG_BG3PB, 0);
        cupid_io_write(gba, REG_BG3PC, 0);
        cupid_io_write(gba, REG_BG3PD, 0x0100);
        cupid_io_write(gba, REG_BG3X_LO, 0);
        cupid_io_write(gba, REG_BG3X_HI, 0);
        cupid_io_write(gba, REG_BG3Y_LO, 0);
        cupid_io_write(gba, REG_BG3Y_HI, 0);
        /** Windows */
        cupid_io_write(gba, REG_WIN0H, 0);
        cupid_io_write(gba, REG_WIN1H, 0);
        cupid_io_write(gba, REG_WIN0V, 0);
        cupid_io_write(gba, REG_WIN1V, 0);
        cupid_io_write(gba, REG_WININ, 0);
        cupid_io_write(gba, REG_WINOUT, 0);
        /** Blend / mosaic */
        cupid_io_write(gba, REG_MOSAIC, 0);
        cupid_io_write(gba, REG_BLDCNT, 0);
        cupid_io_write(gba, REG_BLDALPHA, 0);
        cupid_io_write(gba, REG_BLDY, 0);
        /** DMA: zero all 4 channels */
        for (int i = 0; i < 4; i++) {
            uint16_t base = REG_DMA0SAD_LO + i * 12;
            for (int r = 0; r < 6; r++) {
                cupid_io_write(gba, base + r * 2, 0);
            }
        }
        /** Timers: zero all 4 */
        for (int i = 0; i < 4; i++) {
            cupid_io_write(gba, REG_TM0CNT_L + i * 4, 0);
            cupid_io_write(gba, REG_TM0CNT_H + i * 4, 0);
        }
        /** Interrupt */
        cupid_io_write(gba, REG_IE, 0);
        cupid_io_write(gba, REG_IF, 0xFFFF);
        cupid_io_write(gba, REG_WAITCNT, 0);
        cupid_io_write(gba, REG_IME, 0);
    }
}

/**
 * @brief SWI 0x02: Halt.
 */
static void _Halt(struct CupidGBA* gba) {
    cupid_gba_halt(gba);
}

/**
 * @brief SWI 0x04: IntrWait.
 *
 * R0 = discard old flags, R1 = interrupt mask.
 * The HLE stub halts and loops checking the BIOS IF mirror. For HLE, we
 * halt and let the CPU resume on any matching IRQ.
 */
static void _IntrWait(struct CupidGBA* gba) {
    uint32_t flags = gba->cpu.gprs[1];

    if (gba->cpu.gprs[0]) {
        /** Discard matching bits from BIOS IF mirror at 0x03FFFFF8 */
        uint16_t* biosIF = (uint16_t*)(gba->memory.iwram + GBA_SIZE_IWRAM - 8);
        *biosIF &= ~flags;
    }

    cupid_gba_halt(gba);
}

/**
 * @brief SWI 0x05: VBlankIntrWait.
 */
static void _VBlankIntrWait(struct CupidGBA* gba) {
    gba->cpu.gprs[0] = 1;
    gba->cpu.gprs[1] = 1;  /** VBlank IRQ bit */
    _IntrWait(gba);
}

/**
 * @brief SWI 0x06: Div (r0 / r1), SWI 0x07: DivArm (r1 / r0).
 *
 * Returns r0=quot, r1=rem, r3=abs(quot).
 * Matches the reference implementation behavior.
 */
static void _Div(struct CupidGBA* gba, int32_t num, int32_t denom) {
    if (denom == 0) {
        /** Return a sign-based result instead of hanging */
        gba->cpu.gprs[0] = (uint32_t)((num < 0) ? -1 : 1);
        gba->cpu.gprs[1] = (uint32_t)num;
        gba->cpu.gprs[3] = 1;
        return;
    }
    if (denom == -1 && num == INT32_MIN) {
        gba->cpu.gprs[0] = (uint32_t)INT32_MIN;
        gba->cpu.gprs[1] = 0;
        gba->cpu.gprs[3] = (uint32_t)INT32_MIN;
        return;
    }
    div_t result = div(num, denom);
    gba->cpu.gprs[0] = (uint32_t)result.quot;
    gba->cpu.gprs[1] = (uint32_t)result.rem;
    gba->cpu.gprs[3] = (uint32_t)abs(result.quot);
}

/**
 * @brief SWI 0x08: Sqrt.
 *
 * Matches the reference implementation behavior (Newton's method, integer).
 */
static void _Sqrt(struct CupidGBA* gba) {
    uint32_t x = gba->cpu.gprs[0];
    if (!x) { gba->cpu.gprs[0] = 0; return; }

    uint32_t upper = x;
    uint32_t bound = 1;
    while (bound < upper) {
        upper >>= 1;
        bound <<= 1;
    }
    while (1) {
        upper = x;
        uint32_t accum = 0;
        uint32_t lower = bound;
        while (1) {
            uint32_t oldLower = lower;
            if (lower <= upper >> 1) lower <<= 1;
            if (oldLower >= upper >> 1) break;
        }
        while (1) {
            accum <<= 1;
            if (upper >= lower) { ++accum; upper -= lower; }
            if (lower == bound) break;
            lower >>= 1;
        }
        uint32_t oldBound = bound;
        bound += accum;
        bound >>= 1;
        if (bound >= oldBound) { bound = oldBound; break; }
    }
    gba->cpu.gprs[0] = bound;
}

/**
 * @brief SWI 0x09: ArcTan.
 *
 * Matches the reference implementation behavior (Taylor series in fixed point).
 */
static int16_t _ArcTan(int32_t i, int32_t* r1, int32_t* r3) {
    int32_t a = -((i * i) >> 14);
    int32_t b = ((0xA9 * a) >> 14) + 0x390;
    b = ((b * a) >> 14) + 0x91C;
    b = ((b * a) >> 14) + 0xFB6;
    b = ((b * a) >> 14) + 0x16AA;
    b = ((b * a) >> 14) + 0x2081;
    b = ((b * a) >> 14) + 0x3651;
    b = ((b * a) >> 14) + 0xA2F9;
    if (r1) *r1 = a;
    if (r3) *r3 = b;
    return (int16_t)((i * b) >> 16);
}

/**
 * @brief SWI 0x0A: ArcTan2.
 *
 * Matches reference implementation _ArcTan2.
 */
static int16_t _ArcTan2(int32_t x, int32_t y, int32_t* r1) {
    if (!y) {
        return (x >= 0) ? 0 : (int16_t)0x8000;
    }
    if (!x) {
        return (y >= 0) ? (int16_t)0x4000 : (int16_t)0xC000;
    }
    if (y >= 0) {
        if (x >= 0) {
            if (x >= y) {
                return _ArcTan((y << 14) / x, r1, NULL);
            }
        } else if (-x >= y) {
            return _ArcTan((y << 14) / x, r1, NULL) + (int16_t)0x8000;
        }
        return (int16_t)0x4000 - _ArcTan((x << 14) / y, r1, NULL);
    } else {
        if (x <= 0) {
            if (-x > -y) {
                return _ArcTan((y << 14) / x, r1, NULL) + (int16_t)0x8000;
            }
        } else if (x >= -y) {
            return _ArcTan((y << 14) / x, r1, NULL) + (int16_t)0x10000;
        }
        return (int16_t)0xC000 - _ArcTan((x << 14) / y, r1, NULL);
    }
}

/**
 * @brief SWI 0x0B: CpuSet.
 *
 * r0=src, r1=dst, r2=control (bit24=fill, bit26=word, [20:0]=count).
 */
static void _CpuSet(struct CupidGBA* gba) {
    uint32_t src  = gba->cpu.gprs[0];
    uint32_t dst  = gba->cpu.gprs[1];
    uint32_t ctrl = gba->cpu.gprs[2];
    uint32_t count = ctrl & 0x1FFFFF;
    bool fill = (ctrl >> 24) & 1;
    bool word = (ctrl >> 26) & 1;

    if (word) {
        src &= ~3u;
        dst &= ~3u;
        uint32_t fillVal = cupid_memory_load32(&gba->cpu, src, NULL);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t val = fill ? fillVal : cupid_memory_load32(&gba->cpu, src + i * 4, NULL);
            cupid_memory_store32(&gba->cpu, dst + i * 4, (int32_t)val, NULL);
        }
    } else {
        src &= ~1u;
        dst &= ~1u;
        uint16_t fillVal = (uint16_t)cupid_memory_loadU16(&gba->cpu, src, NULL);
        for (uint32_t i = 0; i < count; i++) {
            uint16_t val = fill ? fillVal : (uint16_t)cupid_memory_loadU16(&gba->cpu, src + i * 2, NULL);
            cupid_memory_store16(&gba->cpu, dst + i * 2, (int16_t)val, NULL);
        }
    }
    gba->cpu.gprs[3] = 0x170;  /** match official BIOS clobber */
}

/**
 * @brief SWI 0x0C: CpuFastSet (always 32-bit, rounds to 8-word blocks).
 */
static void _CpuFastSet(struct CupidGBA* gba) {
    uint32_t src  = gba->cpu.gprs[0] & ~3u;
    uint32_t dst  = gba->cpu.gprs[1] & ~3u;
    uint32_t ctrl = gba->cpu.gprs[2];
    uint32_t count = ctrl & 0x1FFFFF;
    bool fill = (ctrl >> 24) & 1;

    count = (count + 7) & ~7u;  /** round up to 8 */

    uint32_t fillVal = cupid_memory_load32(&gba->cpu, src, NULL);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t val = fill ? fillVal : cupid_memory_load32(&gba->cpu, src + i * 4, NULL);
        cupid_memory_store32(&gba->cpu, dst + i * 4, (int32_t)val, NULL);
    }
}

/**
 * @brief SWI 0x0D: GetBiosChecksum.
 */
#define GBA_BIOS_CHECKSUM 0xBAAE187Fu

static void _GetBiosChecksum(struct CupidGBA* gba) {
    gba->cpu.gprs[0] = GBA_BIOS_CHECKSUM;
    gba->cpu.gprs[1] = 1;
    gba->cpu.gprs[3] = GBA_SIZE_BIOS;
}

/**
 * @brief SWI 0x0E: BgAffineSet.
 *
 * Matches reference implementation _BgAffineSet.
 */
static void _BgAffineSet(struct CupidGBA* gba) {
    int i = (int)gba->cpu.gprs[2];
    uint32_t offset = gba->cpu.gprs[0];
    uint32_t destination = gba->cpu.gprs[1];

    while (i--) {
        float ox = (int32_t)cupid_memory_load32(&gba->cpu, offset, NULL) / 256.f;
        float oy = (int32_t)cupid_memory_load32(&gba->cpu, offset + 4, NULL) / 256.f;
        float cx = (int16_t)cupid_memory_load16(&gba->cpu, offset + 8, NULL);
        float cy = (int16_t)cupid_memory_load16(&gba->cpu, offset + 10, NULL);
        float sx = (int16_t)cupid_memory_load16(&gba->cpu, offset + 12, NULL) / 256.f;
        float sy = (int16_t)cupid_memory_load16(&gba->cpu, offset + 14, NULL) / 256.f;
        float theta = (cupid_memory_loadU16(&gba->cpu, offset + 16, NULL) >> 8) / 128.f * (float)M_PI;
        offset += 20;

        float a = cosf(theta) * sx;
        float b = -sinf(theta) * sx;
        float c = sinf(theta) * sy;
        float d = cosf(theta) * sy;
        float rx = ox - (a * cx + b * cy);
        float ry = oy - (c * cx + d * cy);

        cupid_memory_store16(&gba->cpu, destination + 0, (int16_t)(a * 256), NULL);
        cupid_memory_store16(&gba->cpu, destination + 2, (int16_t)(b * 256), NULL);
        cupid_memory_store16(&gba->cpu, destination + 4, (int16_t)(c * 256), NULL);
        cupid_memory_store16(&gba->cpu, destination + 6, (int16_t)(d * 256), NULL);
        cupid_memory_store32(&gba->cpu, destination + 8, (int32_t)(rx * 256), NULL);
        cupid_memory_store32(&gba->cpu, destination + 12, (int32_t)(ry * 256), NULL);
        destination += 16;
    }
}

/**
 * @brief SWI 0x0F: ObjAffineSet.
 *
 * Matches reference implementation _ObjAffineSet.
 */
static void _ObjAffineSet(struct CupidGBA* gba) {
    int i = (int)gba->cpu.gprs[2];
    uint32_t offset = gba->cpu.gprs[0];
    uint32_t destination = gba->cpu.gprs[1];
    int diff = (int)gba->cpu.gprs[3];

    while (i--) {
        float sx = (int16_t)cupid_memory_load16(&gba->cpu, offset, NULL) / 256.f;
        float sy = (int16_t)cupid_memory_load16(&gba->cpu, offset + 2, NULL) / 256.f;
        float theta = (cupid_memory_loadU16(&gba->cpu, offset + 4, NULL) >> 8) / 128.f * (float)M_PI;
        offset += 8;

        float a = cosf(theta) * sx;
        float b = -sinf(theta) * sx;
        float c = sinf(theta) * sy;
        float d = cosf(theta) * sy;

        cupid_memory_store16(&gba->cpu, destination, (int16_t)(a * 256), NULL);
        cupid_memory_store16(&gba->cpu, destination + diff, (int16_t)(b * 256), NULL);
        cupid_memory_store16(&gba->cpu, destination + diff * 2, (int16_t)(c * 256), NULL);
        cupid_memory_store16(&gba->cpu, destination + diff * 3, (int16_t)(d * 256), NULL);
        destination += diff * 4;
    }
}

/**
 * @brief SWI 0x10: BitUnPack.
 *
 * Matches reference implementation _unBitPack.
 */
static void _BitUnPack(struct CupidGBA* gba) {
    uint32_t source = gba->cpu.gprs[0];
    uint32_t dest   = gba->cpu.gprs[1];
    uint32_t info   = gba->cpu.gprs[2];

    unsigned sourceLen   = (unsigned)cupid_memory_loadU16(&gba->cpu, info, NULL);
    unsigned sourceWidth = (unsigned)cupid_memory_load8(&gba->cpu, info + 2, NULL);
    unsigned destWidth   = (unsigned)cupid_memory_load8(&gba->cpu, info + 3, NULL);

    /** Validate widths */
    switch (sourceWidth) {
    case 1: case 2: case 4: case 8: break;
    default: return;
    }
    switch (destWidth) {
    case 1: case 2: case 4: case 8: case 16: case 32: break;
    default: return;
    }

    uint32_t bias = cupid_memory_load32(&gba->cpu, info + 4, NULL);
    uint8_t in = 0;
    uint32_t out = 0;
    int bitsRemaining = 0;
    int bitsEaten = 0;

    while (sourceLen > 0 || bitsRemaining) {
        if (!bitsRemaining) {
            in = (uint8_t)cupid_memory_load8(&gba->cpu, source, NULL);
            bitsRemaining = 8;
            ++source;
            --sourceLen;
        }
        unsigned scaled = in & ((1 << sourceWidth) - 1);
        in >>= sourceWidth;
        if (scaled || (bias & 0x80000000)) {
            scaled += bias & 0x7FFFFFFF;
        }
        bitsRemaining -= (int)sourceWidth;
        out |= scaled << bitsEaten;
        bitsEaten += (int)destWidth;
        if (bitsEaten == 32) {
            cupid_memory_store32(&gba->cpu, dest, (int32_t)out, NULL);
            bitsEaten = 0;
            out = 0;
            dest += 4;
        }
    }
    gba->cpu.gprs[0] = source;
    gba->cpu.gprs[1] = dest;
}

/**
 * @brief SWI 0x11/0x12: LZ77UnCompWram/Vram.
 *
 * SWI 0x11 uses width=1 (byte writes). SWI 0x12 uses width=2 (halfword writes).
 * Matches reference implementation _unLz77.
 */
static void _unLz77(struct CupidGBA* gba, int width) {
    uint32_t source = gba->cpu.gprs[0];
    uint32_t dest   = gba->cpu.gprs[1];
    int remaining = (int)((cupid_memory_load32(&gba->cpu, source, NULL) & 0xFFFFFF00) >> 8);
    source += 4;

    int blocksRemaining = 0;
    int blockheader = 0;
    int halfword = 0;

    while (remaining > 0) {
        if (blocksRemaining) {
            if (blockheader & 0x80) {
                /** Compressed */
                uint8_t b0 = (uint8_t)cupid_memory_load8(&gba->cpu, source, NULL);
                uint8_t b1 = (uint8_t)cupid_memory_load8(&gba->cpu, source + 1, NULL);
                source += 2;
                uint32_t disp = dest - (uint32_t)((b0 & 0x0F) << 8 | b1) - 1;
                int bytes = (b0 >> 4) + 3;
                while (bytes--) {
                    if (remaining > 0) --remaining;
                    int byte;
                    if (width == 2) {
                        byte = (int16_t)cupid_memory_load16(&gba->cpu, disp & ~1u, NULL);
                        if (dest & 1) {
                            byte >>= (disp & 1) * 8;
                            halfword |= byte << 8;
                            cupid_memory_store16(&gba->cpu, dest ^ 1, (int16_t)halfword, NULL);
                        } else {
                            byte >>= (disp & 1) * 8;
                            halfword = byte & 0xFF;
                        }
                    } else {
                        byte = (uint8_t)cupid_memory_load8(&gba->cpu, disp, NULL);
                        cupid_memory_store8(&gba->cpu, dest, (int8_t)byte, NULL);
                    }
                    ++disp;
                    ++dest;
                }
            } else {
                /** Uncompressed */
                int byte = (uint8_t)cupid_memory_load8(&gba->cpu, source, NULL);
                ++source;
                if (width == 2) {
                    if (dest & 1) {
                        halfword |= byte << 8;
                        cupid_memory_store16(&gba->cpu, dest ^ 1, (int16_t)halfword, NULL);
                    } else {
                        halfword = byte;
                    }
                } else {
                    cupid_memory_store8(&gba->cpu, dest, (int8_t)byte, NULL);
                }
                ++dest;
                --remaining;
            }
            blockheader <<= 1;
            --blocksRemaining;
        } else {
            blockheader = (uint8_t)cupid_memory_load8(&gba->cpu, source, NULL);
            ++source;
            blocksRemaining = 8;
        }
    }
    gba->cpu.gprs[0] = source;
    gba->cpu.gprs[1] = dest;
    gba->cpu.gprs[3] = 0;
}

/**
 * @brief SWI 0x13: HuffUnComp.
 *
 * Matches reference implementation _unHuffman.
 */
static void _unHuffman(struct CupidGBA* gba) {
    uint32_t source = gba->cpu.gprs[0] & 0xFFFFFFFC;
    uint32_t dest   = gba->cpu.gprs[1];
    uint32_t header = cupid_memory_load32(&gba->cpu, source, NULL);
    int remaining = (int)(header >> 8);
    unsigned bits = header & 0xF;
    if (bits == 0) bits = 8;
    if (32 % bits || bits == 1) return;  /** unsupported */

    int treesize = ((int)cupid_memory_load8(&gba->cpu, source + 4, NULL) << 1) + 1;
    uint32_t treeBase = source + 5;
    source += 5 + (uint32_t)treesize;

    uint32_t nPointer = treeBase;
    uint8_t node = (uint8_t)cupid_memory_load8(&gba->cpu, nPointer, NULL);
    int block = 0;
    int bitsSeen = 0;

    while (remaining > 0) {
        uint32_t bitstream = cupid_memory_load32(&gba->cpu, source, NULL);
        source += 4;
        for (int bitsRemaining = 32; bitsRemaining > 0 && remaining > 0; --bitsRemaining, bitstream <<= 1) {
            uint32_t offset = (uint32_t)(node & 0x3F);
            uint32_t next = (nPointer & ~1u) + offset * 2 + 2;
            int readBits;

            if (bitstream & 0x80000000) {
                /** Right */
                if (node & 0x40) {  /** RTerm */
                    readBits = (int)cupid_memory_load8(&gba->cpu, next + 1, NULL);
                } else {
                    nPointer = next + 1;
                    node = (uint8_t)cupid_memory_load8(&gba->cpu, nPointer, NULL);
                    continue;
                }
            } else {
                /** Left */
                if (node & 0x80) {  /** LTerm */
                    readBits = (int)cupid_memory_load8(&gba->cpu, next, NULL);
                } else {
                    nPointer = next;
                    node = (uint8_t)cupid_memory_load8(&gba->cpu, nPointer, NULL);
                    continue;
                }
            }

            block |= (readBits & ((1 << bits) - 1)) << bitsSeen;
            bitsSeen += (int)bits;
            nPointer = treeBase;
            node = (uint8_t)cupid_memory_load8(&gba->cpu, nPointer, NULL);
            if (bitsSeen == 32) {
                bitsSeen = 0;
                cupid_memory_store32(&gba->cpu, dest, (int32_t)block, NULL);
                dest += 4;
                remaining -= 4;
                block = 0;
            }
        }
    }
    gba->cpu.gprs[0] = source;
    gba->cpu.gprs[1] = dest;
}

/**
 * @brief SWI 0x14/0x15: RLUnCompWram/Vram.
 *
 * SWI 0x14 uses width=1, SWI 0x15 uses width=2.
 * Matches reference implementation _unRl.
 */
static void _unRl(struct CupidGBA* gba, int width) {
    uint32_t source = gba->cpu.gprs[0];
    uint32_t dest   = gba->cpu.gprs[1];
    int remaining = (int)(cupid_memory_load32(&gba->cpu, source, NULL) >> 8);
    source += 4;
    int halfword = 0;

    while (remaining > 0) {
        uint8_t flag = (uint8_t)cupid_memory_load8(&gba->cpu, source++, NULL);
        int length;
        if (flag & 0x80) {
            /** Run */
            length = (flag & 0x7F) + 3;
            uint8_t val = (uint8_t)cupid_memory_load8(&gba->cpu, source++, NULL);
            for (int j = 0; j < length && remaining > 0; j++) {
                if (width == 2) {
                    if (dest & 1) {
                        halfword |= val << 8;
                        cupid_memory_store16(&gba->cpu, dest ^ 1, (int16_t)halfword, NULL);
                    } else {
                        halfword = val;
                    }
                } else {
                    cupid_memory_store8(&gba->cpu, dest, (int8_t)val, NULL);
                }
                ++dest;
                --remaining;
            }
        } else {
            /** Literal */
            length = (flag & 0x7F) + 1;
            for (int j = 0; j < length && remaining > 0; j++) {
                uint8_t val = (uint8_t)cupid_memory_load8(&gba->cpu, source++, NULL);
                if (width == 2) {
                    if (dest & 1) {
                        halfword |= val << 8;
                        cupid_memory_store16(&gba->cpu, dest ^ 1, (int16_t)halfword, NULL);
                    } else {
                        halfword = val;
                    }
                } else {
                    cupid_memory_store8(&gba->cpu, dest, (int8_t)val, NULL);
                }
                ++dest;
                --remaining;
            }
        }
    }
    gba->cpu.gprs[0] = source;
    gba->cpu.gprs[1] = dest;
}

/**
 * @brief SWI 0x16/0x17/0x18: Diff*UnFilter.
 *
 * SWI 0x16 uses in=1, out=1. SWI 0x17 uses in=1, out=2. SWI 0x18 uses in=2, out=2.
 * Matches reference implementation _unFilter.
 */
static void _unFilter(struct CupidGBA* gba, int inwidth, int outwidth) {
    uint32_t source = gba->cpu.gprs[0] & 0xFFFFFFFC;
    uint32_t dest   = gba->cpu.gprs[1];
    int remaining = (int)(cupid_memory_load32(&gba->cpu, source, NULL) >> 8);
    uint16_t halfword = 0;
    uint16_t old = 0;
    source += 4;

    while (remaining > 0) {
        uint16_t new_val;
        if (inwidth == 1) {
            new_val = (uint8_t)cupid_memory_load8(&gba->cpu, source, NULL);
        } else {
            new_val = (uint16_t)cupid_memory_loadU16(&gba->cpu, source, NULL);
        }
        new_val = (uint16_t)(new_val + old);
        if (outwidth > inwidth) {
            halfword >>= 8;
            halfword |= (uint16_t)(new_val << 8);
            if (source & 1) {
                cupid_memory_store16(&gba->cpu, dest, (int16_t)halfword, NULL);
                dest += (uint32_t)outwidth;
                remaining -= outwidth;
            }
        } else if (outwidth == 1) {
            cupid_memory_store8(&gba->cpu, dest, (int8_t)new_val, NULL);
            dest += (uint32_t)outwidth;
            remaining -= outwidth;
        } else {
            cupid_memory_store16(&gba->cpu, dest, (int16_t)new_val, NULL);
            dest += (uint32_t)outwidth;
            remaining -= outwidth;
        }
        old = new_val;
        source += (uint32_t)inwidth;
    }
    gba->cpu.gprs[0] = source;
    gba->cpu.gprs[1] = dest;
}

/**
 * @brief SWI 0x1F: MidiKey2Freq.
 *
 * Matches reference implementation _MidiKey2Freq.
 */
static void _MidiKey2Freq(struct CupidGBA* gba) {
    uint32_t key = cupid_memory_load32(&gba->cpu, gba->cpu.gprs[0] + 4, NULL);
    gba->cpu.gprs[0] = (uint32_t)(key / exp2f((180.f - gba->cpu.gprs[1] - gba->cpu.gprs[2] / 256.f) / 12.f));
}

/**
 * @brief SWI dispatcher.
 */
void cupid_bios_swi(struct CupidGBA* gba, uint8_t comment) {
    switch (comment) {
    case 0x00: _SoftReset(gba); break;
    case 0x01: _RegisterRamReset(gba); break;
    case 0x02: _Halt(gba); break;
    case 0x03: _Halt(gba); break;  /** Stop -- treat as halt */
    case 0x04: _IntrWait(gba); break;
    case 0x05: _VBlankIntrWait(gba); break;
    case 0x06: _Div(gba, (int32_t)gba->cpu.gprs[0], (int32_t)gba->cpu.gprs[1]); break;
    case 0x07: _Div(gba, (int32_t)gba->cpu.gprs[1], (int32_t)gba->cpu.gprs[0]); break;
    case 0x08: _Sqrt(gba); break;
    case 0x09:
        gba->cpu.gprs[0] = (uint32_t)_ArcTan((int32_t)(int16_t)gba->cpu.gprs[0],
                                               (int32_t*)&gba->cpu.gprs[1],
                                               (int32_t*)&gba->cpu.gprs[3]);
        break;
    case 0x0A:
        gba->cpu.gprs[0] = (uint16_t)_ArcTan2((int32_t)(int16_t)gba->cpu.gprs[0],
                                                (int32_t)(int16_t)gba->cpu.gprs[1],
                                                (int32_t*)&gba->cpu.gprs[1]);
        gba->cpu.gprs[3] = 0x170;
        break;
    case 0x0B: _CpuSet(gba); break;
    case 0x0C: _CpuFastSet(gba); break;
    case 0x0D: _GetBiosChecksum(gba); break;
    case 0x0E: _BgAffineSet(gba); break;
    case 0x0F: _ObjAffineSet(gba); break;
    case 0x10: _BitUnPack(gba); break;
    case 0x11: _unLz77(gba, 1); break;   /** WRAM */
    case 0x12: _unLz77(gba, 2); break;   /** VRAM */
    case 0x13: _unHuffman(gba); break;
    case 0x14: _unRl(gba, 1); break;     /** WRAM */
    case 0x15: _unRl(gba, 2); break;     /** VRAM */
    case 0x16: _unFilter(gba, 1, 1); break;  /** Diff8bit WRAM */
    case 0x17: _unFilter(gba, 1, 2); break;  /** Diff8bit VRAM */
    case 0x18: _unFilter(gba, 2, 2); break;  /** Diff16bit */
    case 0x19: break;  /** SoundBias -- stub */
    case 0x1F: _MidiKey2Freq(gba); break;
    default: break;    /** Unknown SWI */
    }
}
