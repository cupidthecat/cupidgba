/**
 * @file dma.c
 * @brief GBA DMA transfer logic for 4 DMA channels.
 */

#include "cupid/gba.h"

/**
 * @brief Address offset per dest/src control mode: increment, decrement, fixed, increment/reload.
 */
static const int DMA_OFFSET[] = { 1, -1, 0, 1 };

/**
 * @brief Initialize DMA channel state.
 */
void cupid_dma_init(struct CupidGBA* gba) {
    for (int i = 0; i < 4; i++) {
        memset(&gba->memory.dma[i], 0, sizeof(struct CupidDMA));
    }
}

/**
 * @brief Reset DMA channel registers and latched values.
 */
void cupid_dma_reset(struct CupidGBA* gba) {
    for (int i = 0; i < 4; i++) {
        gba->memory.dma[i].enable = false;
        gba->memory.dma[i].source = 0;
        gba->memory.dma[i].dest = 0;
        gba->memory.dma[i].count = 0;
        gba->memory.dma[i].nextSource = 0;
        gba->memory.dma[i].nextDest = 0;
        gba->memory.dma[i].nextCount = 0;
        gba->memory.dma[i].srcControl = 0;
        gba->memory.dma[i].destControl = 0;
        gba->memory.dma[i].timing = DMA_TIMING_NOW;
        gba->memory.dma[i].wordTransfer = false;
        gba->memory.dma[i].repeat = false;
        gba->memory.dma[i].doIrq = false;
    }
}

/**
 * @brief Write DMA source address for a channel.
 */
void cupid_dma_write_sad(struct CupidGBA* gba, int ch, uint32_t address) {
    static const uint32_t MASKS[] = { 0x07FFFFFF, 0x0FFFFFFF, 0x0FFFFFFF, 0x0FFFFFFF };
    gba->memory.dma[ch].source = address & MASKS[ch];
}

/**
 * @brief Write DMA destination address for a channel.
 */
void cupid_dma_write_dad(struct CupidGBA* gba, int ch, uint32_t address) {
    static const uint32_t MASKS[] = { 0x07FFFFFF, 0x07FFFFFF, 0x07FFFFFF, 0x0FFFFFFF };
    gba->memory.dma[ch].dest = address & MASKS[ch];
}

/**
 * @brief Write DMA transfer count for a channel.
 */
void cupid_dma_write_cnt_lo(struct CupidGBA* gba, int ch, uint16_t count) {
    gba->memory.dma[ch].count = count;
}

/**
 * @brief Write DMA control register for a channel.
 *
 * When enable transitions from 0 to 1, latch addresses and prepare transfer.
 */
void cupid_dma_write_cnt(struct CupidGBA* gba, int ch, uint16_t value) {
    struct CupidDMA* dma = &gba->memory.dma[ch];
    bool wasEnabled = dma->enable;

    dma->reg = value;
    dma->destControl = (value >> 5) & 3;
    dma->srcControl  = (value >> 7) & 3;
    dma->repeat      = (value >> 9) & 1;
    dma->wordTransfer = (value >> 10) & 1;
    dma->timing      = (value >> 12) & 3;
    dma->doIrq       = (value >> 14) & 1;
    dma->enable       = (value >> 15) & 1;

    if (!wasEnabled && dma->enable) {
        /** Latch source and dest addresses. */
        dma->nextSource = dma->source;
        dma->nextDest = dma->dest;

        /** Latch count (0 means max: 0x4000 for ch0-2, 0x10000 for ch3). */
        dma->nextCount = dma->count;
        if (dma->nextCount == 0) {
            dma->nextCount = (ch == 3) ? 0x10000 : 0x4000;
        }

        /** If timing is immediate, start right away. */
        if (dma->timing == DMA_TIMING_NOW) {
            cupid_dma_transfer(gba, ch);
        }

        /** DMA 1/2 with TIMING_CUSTOM uses FIFO mode. */
        if (dma->timing == DMA_TIMING_CUSTOM && (ch == 1 || ch == 2)) {
            cupid_audio_schedule_fifo_dma(&gba->audio, ch, dma);
        }
    }

    if (!dma->enable) {
        /** DMA disabled. */
        dma->nextCount = 0;
    }
}

/**
 * @brief Execute a DMA transfer for a channel.
 */
void cupid_dma_transfer(struct CupidGBA* gba, int ch) {
    struct CupidDMA* dma = &gba->memory.dma[ch];
    if (!dma->enable) return;

    int srcOff  = DMA_OFFSET[dma->srcControl];
    int destOff = DMA_OFFSET[dma->destControl];

    uint32_t src = dma->nextSource;
    uint32_t dst = dma->nextDest;
    uint32_t count = dma->nextCount;

    gba->performingDMA = 1 | (ch << 1);
    gba->dmaPC = gba->cpu.gprs[ARM_PC];

    if (dma->wordTransfer) {
        /** 32-bit transfers. */
        src &= ~3;
        dst &= ~3;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t val = cupid_memory_load32(&gba->cpu, src, NULL);
            cupid_memory_store32(&gba->cpu, dst, val, NULL);
            gba->bus = val;  /** DMA updates open bus (32-bit). */
            src += srcOff * 4;
            dst += destOff * 4;
        }
        gba->cpu.cycles += count * 2; /** Rough timing. */
    } else {
        /** 16-bit transfers. */
        src &= ~1;
        dst &= ~1;
        int srcRegion  = src >> GBA_BASE_OFFSET;
        int destRegion = dst >> GBA_BASE_OFFSET;
        for (uint32_t i = 0; i < count; i++) {
            uint16_t val;
            /** EEPROM read: DMA from ROM2_EX region. */
            if (srcRegion == GBA_REGION_ROM2_EX &&
                (gba->memory.savedata.type == SAVE_EEPROM ||
                 gba->memory.savedata.type == SAVE_EEPROM512)) {
                val = cupid_savedata_read_eeprom(&gba->memory.savedata);
            } else {
                val = (uint16_t)cupid_memory_loadU16(&gba->cpu, src, NULL);
            }
            /** EEPROM write: DMA to ROM2_EX region. */
            if (destRegion == GBA_REGION_ROM2_EX) {
                if (gba->memory.savedata.type == SAVE_AUTODETECT) {
                    printf("CUPID: Detected EEPROM savegame (DMA to ROM2_EX)\n");
                    gba->memory.savedata.type = SAVE_EEPROM512;
                    cupid_savedata_init_eeprom(&gba->memory.savedata);
                }
                if (gba->memory.savedata.type == SAVE_EEPROM ||
                    gba->memory.savedata.type == SAVE_EEPROM512) {
                    cupid_savedata_write_eeprom(&gba->memory.savedata, val, count - i);
                } else {
                    cupid_memory_store16(&gba->cpu, dst, (int16_t)val, NULL);
                }
            } else {
                cupid_memory_store16(&gba->cpu, dst, (int16_t)val, NULL);
            }
            gba->bus = ((uint32_t)val | ((uint32_t)val << 16));  /** 16-bit DMA: duplicate to 32 bits. */
            src += srcOff * 2;
            dst += destOff * 2;
        }
        gba->cpu.cycles += count * 2;
    }

    gba->performingDMA = 0;

    /** Update latched positions. */
    dma->nextSource = src;
    if (dma->destControl != 3) {
        dma->nextDest = dst;
    }

    /** Repeat handling. */
    if (dma->repeat && dma->timing != DMA_TIMING_NOW) {
        /** Reload count, and reload dest if destControl == 3 (increment/reload). */
        dma->nextCount = dma->count;
        if (dma->nextCount == 0) {
            dma->nextCount = (ch == 3) ? 0x10000 : 0x4000;
        }
        if (dma->destControl == 3) {
            dma->nextDest = dma->dest;
        }
    } else {
        dma->enable = false;
    }

    /** Fire IRQ if requested. */
    if (dma->doIrq) {
        cupid_raise_irq(gba, IRQ_DMA0 + ch, 0);
    }
}

/**
 * @brief Run all DMA channels matching the given timing.
 */
void cupid_dma_run(struct CupidGBA* gba, enum CupidDMATiming timing) {
    for (int i = 0; i < 4; i++) {
        if (gba->memory.dma[i].enable && gba->memory.dma[i].timing == (int)timing) {
            cupid_dma_transfer(gba, i);
        }
    }
}

/**
 * @brief Trigger HBlank DMA scheduling.
 */
void cupid_dma_run_hblank(struct CupidGBA* gba, int32_t cycles) {
    (void)cycles;
    cupid_dma_run(gba, GBA_DMA_TIMING_HBLANK);
}

/**
 * @brief Trigger VBlank DMA scheduling.
 */
void cupid_dma_run_vblank(struct CupidGBA* gba, int32_t cycles) {
    (void)cycles;
    cupid_dma_run(gba, GBA_DMA_TIMING_VBLANK);
}
