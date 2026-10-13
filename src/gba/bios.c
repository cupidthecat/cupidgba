/**
 * @file bios.c
 * @brief High-Level Emulation of GBA BIOS SWI calls.
 */

#include "cupid/gba.h"
#include <math.h>
#include <stdlib.h>

/* C11 does not require the nonstandard M_PI macro. */
static const float CUPID_PI = 3.14159265358979323846f;

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
 * @brief SWI 0x03: Stop the system clock.
 */
static void _Stop(struct CupidGBA* gba) {
    cupid_gba_stop(gba);
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
        float theta = (cupid_memory_loadU16(&gba->cpu, offset + 16, NULL) >> 8) / 128.f * CUPID_PI;
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
 */
static void _ObjAffineSet(struct CupidGBA* gba) {
    int i = (int)gba->cpu.gprs[2];
    uint32_t offset = gba->cpu.gprs[0];
    uint32_t destination = gba->cpu.gprs[1];
    int diff = (int)gba->cpu.gprs[3];

    while (i--) {
        float sx = (int16_t)cupid_memory_load16(&gba->cpu, offset, NULL) / 256.f;
        float sy = (int16_t)cupid_memory_load16(&gba->cpu, offset + 2, NULL) / 256.f;
        float theta = (cupid_memory_loadU16(&gba->cpu, offset + 4, NULL) >> 8) / 128.f * CUPID_PI;
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
 * @brief SWI 0x1F: MidiKey2Freq.
 *
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
    case 0x02: case 0x04: case 0x05:
        /* Execute the replacement BIOS wait loop through the CPU pipeline. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
    case 0x03: _Stop(gba); break;
    case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A:
    case 0x0B: case 0x0C:
        /* Execute math and copy services through the timed CPU and bus. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
    case 0x0D: _GetBiosChecksum(gba); break;
    case 0x0E: _BgAffineSet(gba); break;
    case 0x0F: _ObjAffineSet(gba); break;
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
    case 0x15: case 0x16: case 0x17: case 0x18: case 0x19:
        /* Utility instructions expose their memory accesses and peripheral clocks. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
    case 0x1F: _MidiKey2Freq(gba); break;
    default: break;    /** Unknown SWI */
    }
}
