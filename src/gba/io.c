/**
 * @file io.c
 * @brief GBA IO register read and write dispatch.
 *
 * The IO array (gba->memory.io) is a flat uint16_t[0x400/2] addressed as
 * io[byte_offset >> 1]. Subsystem structs shadow the registers they care
 * about; writes dispatch through the switch into the appropriate subsystem,
 * then the (possibly modified) value is stored into the IO array.
 */

#include "cupid/gba.h"

static int32_t affine_high(int32_t previous, uint16_t high) {
    uint32_t raw = ((uint32_t)previous & 0xFFFFu) | ((uint32_t)(high & 0x0FFFu) << 16);
    return (int32_t)(raw ^ 0x08000000u) - 0x08000000;
}

/**
 * @brief Initialize IO registers and shadow state.
 */
void cupid_io_init(struct CupidGBA* gba) {
    memset(gba->memory.io, 0, sizeof(gba->memory.io));

    /** Default register values. */
    gba->memory.io[REG_DISPCNT   >> 1] = 0x0080;  /** forced blank */
    gba->memory.io[REG_KEYINPUT  >> 1] = 0x03FF;  /** all keys released */
    gba->memory.io[REG_SOUNDBIAS >> 1] = 0x0200;
    gba->memory.io[REG_BG2PA     >> 1] = 0x0100;
    gba->memory.io[REG_BG2PD     >> 1] = 0x0100;
    gba->memory.io[REG_BG3PA     >> 1] = 0x0100;
    gba->memory.io[REG_BG3PD     >> 1] = 0x0100;
    gba->memory.io[REG_RCNT      >> 1] = 0x8000;  /** RCNT_INITIAL */

    /** Mirror to subsystem structs. */
    gba->video.dispcnt = 0x0080;
    gba->video.dispstat = 0;
    gba->video.bg[2].pa = 0x100;
    gba->video.bg[2].pd = 0x100;
    gba->video.bg[3].pa = 0x100;
    gba->video.bg[3].pd = 0x100;
    gba->audio.soundbias = 0x0200;

    /** Interrupt state. */
    gba->ime = 0;
    gba->ie = 0;
    gba->irqf = 0;

    /** If no BIOS, mark post-boot. */
    if (!gba->memory.biosLoaded) {
        gba->memory.io[REG_VCOUNT >> 1] = 0x7E;
        gba->memory.io[REG_POSTFLG >> 1] = 1;
    }
}

/**
 * @brief Write a 16-bit IO register.
 *
 * Pattern: switch dispatches to subsystems. Subsystems may modify `value`
 * (masking write-only bits, etc.). `break` stores to io[], `return` skips
 * storage for read-only or self-managed registers.
 */
void cupid_io_write(struct CupidGBA* gba, uint32_t addr, uint16_t value) {
    if (addr >= GBA_SIZE_IO) return;
    addr &= ~1u;

    switch (addr) {
    /** VIDEO 0x000-0x056. */
    case REG_DISPCNT:
        gba->video.dispcnt = value;
        break;

    case REG_GREENSWAP:
        /** Rarely used; just store. */
        break;

    case REG_DISPSTAT:
        /**
         * IRQ enables and the VCOUNT target are writable; bits 6-7 are unused.
         * Bits 0-2 (VBlank/HBlank/VCount flags) are read-only.
         */
        value = (gba->video.dispstat & 0x0007) | (value & 0xFF38);
        gba->video.dispstat = value;
        cupid_video_update_match(&gba->video);
        return;

    case REG_VCOUNT:
        /** Read-only; ignore writes. */
        return;

    case REG_BG0CNT: value &= 0xDFFF; gba->video.bg[0].cnt = value; break;
    case REG_BG1CNT: value &= 0xDFFF; gba->video.bg[1].cnt = value; break;
    case REG_BG2CNT: gba->video.bg[2].cnt = value; break;
    case REG_BG3CNT: gba->video.bg[3].cnt = value; break;

    /** BG scroll (write-only, 9-bit). */
    case REG_BG0HOFS: gba->video.bg[0].hofs = value & 0x1FF; break;
    case REG_BG0VOFS: gba->video.bg[0].vofs = value & 0x1FF; break;
    case REG_BG1HOFS: gba->video.bg[1].hofs = value & 0x1FF; break;
    case REG_BG1VOFS: gba->video.bg[1].vofs = value & 0x1FF; break;
    case REG_BG2HOFS: gba->video.bg[2].hofs = value & 0x1FF; break;
    case REG_BG2VOFS: gba->video.bg[2].vofs = value & 0x1FF; break;
    case REG_BG3HOFS: gba->video.bg[3].hofs = value & 0x1FF; break;
    case REG_BG3VOFS: gba->video.bg[3].vofs = value & 0x1FF; break;

    /** BG2 affine. */
    case REG_BG2PA: gba->video.bg[2].pa = (int16_t)value; break;
    case REG_BG2PB: gba->video.bg[2].pb = (int16_t)value; break;
    case REG_BG2PC: gba->video.bg[2].pc = (int16_t)value; break;
    case REG_BG2PD: gba->video.bg[2].pd = (int16_t)value; break;
    case REG_BG2X_LO:
        gba->video.bg[2].refx = (gba->video.bg[2].refx & 0xFFFF0000) | value;
        gba->video.bg[2].sx = gba->video.bg[2].refx;
        break;
    case REG_BG2X_HI:
        gba->video.bg[2].refx = affine_high(gba->video.bg[2].refx, value);
        gba->video.bg[2].sx = gba->video.bg[2].refx;
        break;
    case REG_BG2Y_LO:
        gba->video.bg[2].refy = (gba->video.bg[2].refy & 0xFFFF0000) | value;
        gba->video.bg[2].sy = gba->video.bg[2].refy;
        break;
    case REG_BG2Y_HI:
        gba->video.bg[2].refy = affine_high(gba->video.bg[2].refy, value);
        gba->video.bg[2].sy = gba->video.bg[2].refy;
        break;

    /** BG3 affine. */
    case REG_BG3PA: gba->video.bg[3].pa = (int16_t)value; break;
    case REG_BG3PB: gba->video.bg[3].pb = (int16_t)value; break;
    case REG_BG3PC: gba->video.bg[3].pc = (int16_t)value; break;
    case REG_BG3PD: gba->video.bg[3].pd = (int16_t)value; break;
    case REG_BG3X_LO:
        gba->video.bg[3].refx = (gba->video.bg[3].refx & 0xFFFF0000) | value;
        gba->video.bg[3].sx = gba->video.bg[3].refx;
        break;
    case REG_BG3X_HI:
        gba->video.bg[3].refx = affine_high(gba->video.bg[3].refx, value);
        gba->video.bg[3].sx = gba->video.bg[3].refx;
        break;
    case REG_BG3Y_LO:
        gba->video.bg[3].refy = (gba->video.bg[3].refy & 0xFFFF0000) | value;
        gba->video.bg[3].sy = gba->video.bg[3].refy;
        break;
    case REG_BG3Y_HI:
        gba->video.bg[3].refy = affine_high(gba->video.bg[3].refy, value);
        gba->video.bg[3].sy = gba->video.bg[3].refy;
        break;

    /** Windows. */
    case REG_WIN0H:
        gba->video.win.h[0][0] = value >> 8;
        gba->video.win.h[0][1] = value & 0xFF;
        break;
    case REG_WIN1H:
        gba->video.win.h[1][0] = value >> 8;
        gba->video.win.h[1][1] = value & 0xFF;
        break;
    case REG_WIN0V:
        gba->video.win.v[0][0] = value >> 8;
        gba->video.win.v[0][1] = value & 0xFF;
        break;
    case REG_WIN1V:
        gba->video.win.v[1][0] = value >> 8;
        gba->video.win.v[1][1] = value & 0xFF;
        break;
    case REG_WININ:  value &= 0x3F3F; gba->video.win.winin = value;  break;
    case REG_WINOUT: value &= 0x3F3F; gba->video.win.winout = value; break;

    /** Mosaic (write-only). */
    case REG_MOSAIC:
        break;

    /** Blending. */
    case REG_BLDCNT:   value &= 0x3FFF; gba->video.blend.bldcnt = value;   break;
    case REG_BLDALPHA: value &= 0x1F1F; gba->video.blend.bldalpha = value;  break;
    case REG_BLDY:     gba->video.blend.bldy = value;      break;

    /** AUDIO 0x060-0x0A6. */
    case REG_SOUND1CNT_L: case REG_SOUND1CNT_H: case REG_SOUND1CNT_X:
    case REG_SOUND2CNT_L: case REG_SOUND2CNT_H:
    case REG_SOUND3CNT_L: case REG_SOUND3CNT_H: case REG_SOUND3CNT_X:
    case REG_SOUND4CNT_L: case REG_SOUND4CNT_H:
    case REG_SOUNDCNT_L:  case REG_SOUNDCNT_H:  case REG_SOUNDCNT_X:
    case REG_SOUNDBIAS:
        cupid_audio_write_register(&gba->audio, addr, value);
        return;

    /** Wave RAM; store raw and let audio handle it. */
    case REG_WAVE_RAM0_LO: case REG_WAVE_RAM0_HI:
    case REG_WAVE_RAM1_LO: case REG_WAVE_RAM1_HI:
    case REG_WAVE_RAM2_LO: case REG_WAVE_RAM2_HI:
    case REG_WAVE_RAM3_LO: case REG_WAVE_RAM3_HI:
        cupid_audio_write_register(&gba->audio, addr, value);
        break;

    /**
     * FIFO writes: store32 splits into two 16-bit writes (LO then HI).
     * Buffer the LO half, push the full 32-bit word on the HI write.
     * This avoids double-pushing each DMA word.
     */
    case REG_FIFO_A_LO:
        gba->memory.io[addr >> 1] = value;
        return;
    case REG_FIFO_A_HI:
        cupid_audio_fifo_write(&gba->audio.chA,
            (uint32_t)gba->memory.io[REG_FIFO_A_LO >> 1] | ((uint32_t)value << 16));
        return;
    case REG_FIFO_B_LO:
        gba->memory.io[addr >> 1] = value;
        return;
    case REG_FIFO_B_HI:
        cupid_audio_fifo_write(&gba->audio.chB,
            (uint32_t)gba->memory.io[REG_FIFO_B_LO >> 1] | ((uint32_t)value << 16));
        return;

    /**
     * DMA 0x0B0-0x0DE.
     * SAD and DAD are write-only 32-bit assembled from two 16-bit writes.
     * Each halfword write assembles the full 32-bit and routes through
     * cupid_dma_write_sad/dad which apply per-channel address masks.
     * CNT_LO routes through cupid_dma_write_cnt_lo (normalizes count).
     */
    case REG_DMA0SAD_LO:
        gba->memory.io[REG_DMA0SAD_LO >> 1] = value;
        cupid_dma_write_sad(gba, 0, (uint32_t)value | ((uint32_t)gba->memory.io[REG_DMA0SAD_HI >> 1] << 16));
        return;
    case REG_DMA0SAD_HI:
        gba->memory.io[REG_DMA0SAD_HI >> 1] = value;
        cupid_dma_write_sad(gba, 0, (uint32_t)gba->memory.io[REG_DMA0SAD_LO >> 1] | ((uint32_t)value << 16));
        return;
    case REG_DMA0DAD_LO:
        gba->memory.io[REG_DMA0DAD_LO >> 1] = value;
        cupid_dma_write_dad(gba, 0, (uint32_t)value | ((uint32_t)gba->memory.io[REG_DMA0DAD_HI >> 1] << 16));
        return;
    case REG_DMA0DAD_HI:
        gba->memory.io[REG_DMA0DAD_HI >> 1] = value;
        cupid_dma_write_dad(gba, 0, (uint32_t)gba->memory.io[REG_DMA0DAD_LO >> 1] | ((uint32_t)value << 16));
        return;
    case REG_DMA0CNT_LO: cupid_dma_write_cnt_lo(gba, 0, value & 0x3FFF); return;
    case REG_DMA0CNT_HI: cupid_dma_write_cnt(gba, 0, value); return;

    case REG_DMA1SAD_LO:
        gba->memory.io[REG_DMA1SAD_LO >> 1] = value;
        cupid_dma_write_sad(gba, 1, (uint32_t)value | ((uint32_t)gba->memory.io[REG_DMA1SAD_HI >> 1] << 16));
        return;
    case REG_DMA1SAD_HI:
        gba->memory.io[REG_DMA1SAD_HI >> 1] = value;
        cupid_dma_write_sad(gba, 1, (uint32_t)gba->memory.io[REG_DMA1SAD_LO >> 1] | ((uint32_t)value << 16));
        return;
    case REG_DMA1DAD_LO:
        gba->memory.io[REG_DMA1DAD_LO >> 1] = value;
        cupid_dma_write_dad(gba, 1, (uint32_t)value | ((uint32_t)gba->memory.io[REG_DMA1DAD_HI >> 1] << 16));
        return;
    case REG_DMA1DAD_HI:
        gba->memory.io[REG_DMA1DAD_HI >> 1] = value;
        cupid_dma_write_dad(gba, 1, (uint32_t)gba->memory.io[REG_DMA1DAD_LO >> 1] | ((uint32_t)value << 16));
        return;
    case REG_DMA1CNT_LO: cupid_dma_write_cnt_lo(gba, 1, value & 0x3FFF); return;
    case REG_DMA1CNT_HI: cupid_dma_write_cnt(gba, 1, value); return;

    case REG_DMA2SAD_LO:
        gba->memory.io[REG_DMA2SAD_LO >> 1] = value;
        cupid_dma_write_sad(gba, 2, (uint32_t)value | ((uint32_t)gba->memory.io[REG_DMA2SAD_HI >> 1] << 16));
        return;
    case REG_DMA2SAD_HI:
        gba->memory.io[REG_DMA2SAD_HI >> 1] = value;
        cupid_dma_write_sad(gba, 2, (uint32_t)gba->memory.io[REG_DMA2SAD_LO >> 1] | ((uint32_t)value << 16));
        return;
    case REG_DMA2DAD_LO:
        gba->memory.io[REG_DMA2DAD_LO >> 1] = value;
        cupid_dma_write_dad(gba, 2, (uint32_t)value | ((uint32_t)gba->memory.io[REG_DMA2DAD_HI >> 1] << 16));
        return;
    case REG_DMA2DAD_HI:
        gba->memory.io[REG_DMA2DAD_HI >> 1] = value;
        cupid_dma_write_dad(gba, 2, (uint32_t)gba->memory.io[REG_DMA2DAD_LO >> 1] | ((uint32_t)value << 16));
        return;
    case REG_DMA2CNT_LO: cupid_dma_write_cnt_lo(gba, 2, value & 0x3FFF); return;
    case REG_DMA2CNT_HI: cupid_dma_write_cnt(gba, 2, value); return;

    case REG_DMA3SAD_LO:
        gba->memory.io[REG_DMA3SAD_LO >> 1] = value;
        cupid_dma_write_sad(gba, 3, (uint32_t)value | ((uint32_t)gba->memory.io[REG_DMA3SAD_HI >> 1] << 16));
        return;
    case REG_DMA3SAD_HI:
        gba->memory.io[REG_DMA3SAD_HI >> 1] = value;
        cupid_dma_write_sad(gba, 3, (uint32_t)gba->memory.io[REG_DMA3SAD_LO >> 1] | ((uint32_t)value << 16));
        return;
    case REG_DMA3DAD_LO:
        gba->memory.io[REG_DMA3DAD_LO >> 1] = value;
        cupid_dma_write_dad(gba, 3, (uint32_t)value | ((uint32_t)gba->memory.io[REG_DMA3DAD_HI >> 1] << 16));
        return;
    case REG_DMA3DAD_HI:
        gba->memory.io[REG_DMA3DAD_HI >> 1] = value;
        cupid_dma_write_dad(gba, 3, (uint32_t)gba->memory.io[REG_DMA3DAD_LO >> 1] | ((uint32_t)value << 16));
        return;
    case REG_DMA3CNT_LO: cupid_dma_write_cnt_lo(gba, 3, value); return;  /** DMA3: 16-bit, no mask */
    case REG_DMA3CNT_HI: cupid_dma_write_cnt(gba, 3, value); return;

    /** TIMERS 0x100-0x10E. */
    case REG_TM0CNT_L: cupid_timer_write_reload(gba, 0, value); return;
    case REG_TM1CNT_L: cupid_timer_write_reload(gba, 1, value); return;
    case REG_TM2CNT_L: cupid_timer_write_reload(gba, 2, value); return;
    case REG_TM3CNT_L: cupid_timer_write_reload(gba, 3, value); return;

    case REG_TM0CNT_H:
        cupid_timer_write_control(gba, 0, value);
        return;
    case REG_TM1CNT_H:
        cupid_timer_write_control(gba, 1, value);
        return;
    case REG_TM2CNT_H:
        cupid_timer_write_control(gba, 2, value);
        return;
    case REG_TM3CNT_H:
        cupid_timer_write_control(gba, 3, value);
        return;

    /** SIO 0x120-0x15A. */
    case REG_SIOCNT:
        value &= 0x7FFF;
        break;
    case REG_RCNT:
        value &= 0xC1FF;
        break;
    case REG_SIODATA32_LO:
    case REG_SIODATA32_HI:
    case REG_SIOMULTI2:
    case REG_SIOMULTI3:
    case REG_SIOMLT_SEND:
    case REG_JOYCNT:
    case REG_JOY_RECV_LO:
    case REG_JOY_RECV_HI:
    case REG_JOY_TRANS_LO:
    case REG_JOY_TRANS_HI:
    case REG_JOYSTAT:
        /** SIO stub; just store. */
        break;

    /** INPUT 0x130-0x132. */
    case REG_KEYINPUT:
        /** Read-only; ignore writes. */
        return;

    case REG_KEYCNT:
        value &= 0xC3FF;
        gba->memory.io[REG_KEYCNT >> 1] = value;
        gba->keysLast = gba->keysActive;
        cupid_gba_test_keypad_irq(gba);
        return;  /** already stored */

    /** INTERRUPTS and SYSTEM 0x200-0x301. */
    case REG_IE: case REG_IF: case REG_IME:
        cupid_irq_write(gba, addr, value);
        return;

    case REG_WAITCNT:
        value &= 0x5FFF;
        cupid_memory_update_waitstates(gba, value);
        break;

    case REG_MAX:
        /** Some bad interrupt libraries write here; ignore silently. */
        break;

    case REG_POSTFLG:
        /**
         * Only writable from the BIOS region on a real GBA.
         * HALTCNT (0x301) is handled separately in cupid_io_write8.
         */
        if (gba->memory.activeRegion == 0x00) {  /** BIOS region */
            if (gba->memory.io[addr >> 1]) {
                if (value & 0x8000) {
                    /** Stop (deep halt). */
                    cupid_gba_halt(gba);
                } else {
                    cupid_gba_halt(gba);
                }
            }
            value &= ~0x8000;
        } else {
            /** Writes from outside BIOS are ignored. */
            return;
        }
        break;

    default:
        /**
         * Unknown or unused register; store anyway for compatibility,
         * but only if it is below REG_MAX.
         */
        if (addr >= REG_MAX) {
            return;
        }
        break;
    }

    /** Store the possibly modified value into the IO array. */
    gba->memory.io[addr >> 1] = value;
}

/**
 * @brief Write an 8-bit IO register.
 *
 * Most 8-bit writes merge into the 16-bit register and dispatch through
 * cupid_io_write. Special cases: HALTCNT at 0x301 is byte-only and should
 * not be merged.
 */
void cupid_io_write8(struct CupidGBA* gba, uint32_t addr, uint8_t value) {
    if (addr >= GBA_SIZE_IO) return;
    if (addr >= REG_TM0CNT_L && addr <= REG_TM3CNT_H + 1) {
        cupid_timer_write_byte(gba, (addr - REG_TM0CNT_L) >> 2, addr & 3, value);
        return;
    }

    if ((addr >= REG_IE && addr <= REG_IF + 1) || (addr >= REG_IME && addr <= REG_IME + 1)) {
        cupid_irq_write8(gba, addr, value);
        return;
    }

    /** HALTCNT is at odd byte 0x301; entering halt or stop. */
    if (addr == (REG_HALTCNT & 0x3FF)) {
        if (value & 0x80) {
            /** Stop mode (deep sleep); treat as halt for now. */
            cupid_gba_halt(gba);
        } else {
            cupid_gba_halt(gba);
        }
        return;
    }

    /** Merge byte into the 16-bit halfword and dispatch. */
    uint16_t old = gba->memory.io[(addr & ~1u) >> 1];
    uint16_t merged;
    if (addr & 1) {
        merged = (old & 0x00FF) | ((uint16_t)value << 8);
    } else {
        merged = (old & 0xFF00) | value;
    }
    cupid_io_write(gba, addr & ~1u, merged);
}

/**
 * @brief Read a 16-bit IO register.
 */
uint16_t cupid_io_read(struct CupidGBA* gba, uint32_t addr) {
    if (addr >= GBA_SIZE_IO) {
        return (uint16_t)(cupid_memory_open_bus(&gba->cpu) >> ((addr & 2) * 8));
    }
    addr &= ~1u;

    switch (addr) {
    /** Video. */
    case REG_DISPCNT:   return gba->memory.io[addr >> 1];
    case REG_GREENSWAP: return gba->memory.io[addr >> 1];
    case REG_DISPSTAT:  return gba->video.dispstat;
    case REG_VCOUNT:    return gba->video.vcount;

    case REG_BG0CNT:    return gba->video.bg[0].cnt;
    case REG_BG1CNT:    return gba->video.bg[1].cnt;
    case REG_BG2CNT:    return gba->video.bg[2].cnt;
    case REG_BG3CNT:    return gba->video.bg[3].cnt;

    /** Write-only video registers expose the last value on the bus. */
    case REG_BG0HOFS: case REG_BG0VOFS:
    case REG_BG1HOFS: case REG_BG1VOFS:
    case REG_BG2HOFS: case REG_BG2VOFS:
    case REG_BG3HOFS: case REG_BG3VOFS:
    case REG_BG2PA: case REG_BG2PB: case REG_BG2PC: case REG_BG2PD:
    case REG_BG2X_LO: case REG_BG2X_HI: case REG_BG2Y_LO: case REG_BG2Y_HI:
    case REG_BG3PA: case REG_BG3PB: case REG_BG3PC: case REG_BG3PD:
    case REG_BG3X_LO: case REG_BG3X_HI: case REG_BG3Y_LO: case REG_BG3Y_HI:
    case REG_WIN0H: case REG_WIN1H: case REG_WIN0V: case REG_WIN1V:
    case REG_MOSAIC:
    case REG_BLDY:
        return (uint16_t)(cupid_memory_open_bus(&gba->cpu) >> ((addr & 2) * 8));

    case REG_WININ:    return gba->memory.io[addr >> 1];
    case REG_WINOUT:   return gba->memory.io[addr >> 1];
    case REG_BLDCNT:   return gba->memory.io[addr >> 1];
    case REG_BLDALPHA: return gba->memory.io[addr >> 1];

    /** Audio. */
    case REG_SOUND1CNT_L: case REG_SOUND1CNT_H: case REG_SOUND1CNT_X:
    case REG_SOUND2CNT_L: case REG_SOUND2CNT_H:
    case REG_SOUND3CNT_L: case REG_SOUND3CNT_H: case REG_SOUND3CNT_X:
    case REG_SOUND4CNT_L: case REG_SOUND4CNT_H:
    case REG_SOUNDCNT_L: case REG_SOUNDCNT_H: case REG_SOUNDCNT_X:
    case REG_SOUNDBIAS:
        return cupid_audio_read_register(&gba->audio, addr);

    /** Wave RAM. */
    case REG_WAVE_RAM0_LO: case REG_WAVE_RAM0_HI:
    case REG_WAVE_RAM1_LO: case REG_WAVE_RAM1_HI:
    case REG_WAVE_RAM2_LO: case REG_WAVE_RAM2_HI:
    case REG_WAVE_RAM3_LO: case REG_WAVE_RAM3_HI:
        return gba->memory.io[addr >> 1];

    /** FIFO is write-only and exposes the existing bus value. */
    case REG_FIFO_A_LO: case REG_FIFO_A_HI:
    case REG_FIFO_B_LO: case REG_FIFO_B_HI:
        return (uint16_t)(cupid_memory_open_bus(&gba->cpu) >> ((addr & 2) * 8));

    /** DMA. */
    /** DMA SAD/DAD are write-only. */
    case REG_DMA0SAD_LO: case REG_DMA0SAD_HI:
    case REG_DMA0DAD_LO: case REG_DMA0DAD_HI:
    case REG_DMA1SAD_LO: case REG_DMA1SAD_HI:
    case REG_DMA1DAD_LO: case REG_DMA1DAD_HI:
    case REG_DMA2SAD_LO: case REG_DMA2SAD_HI:
    case REG_DMA2DAD_LO: case REG_DMA2DAD_HI:
    case REG_DMA3SAD_LO: case REG_DMA3SAD_HI:
    case REG_DMA3DAD_LO: case REG_DMA3DAD_HI:
        return (uint16_t)(cupid_memory_open_bus(&gba->cpu) >> ((addr & 2) * 8));

    /** DMA CNT_LO is write-only; reads return 0. */
    case REG_DMA0CNT_LO: case REG_DMA1CNT_LO:
    case REG_DMA2CNT_LO: case REG_DMA3CNT_LO:
        return 0;

    /** DMA CNT_HI is readable (enable bit, etc.). */
    case REG_DMA0CNT_HI: case REG_DMA1CNT_HI:
    case REG_DMA2CNT_HI: case REG_DMA3CNT_HI:
        return gba->memory.io[addr >> 1];

    /** Timers. */
    case REG_TM0CNT_L: return cupid_timer_read_counter(gba, 0);
    case REG_TM1CNT_L: return cupid_timer_read_counter(gba, 1);
    case REG_TM2CNT_L: return cupid_timer_read_counter(gba, 2);
    case REG_TM3CNT_L: return cupid_timer_read_counter(gba, 3);
    case REG_TM0CNT_H: case REG_TM1CNT_H:
    case REG_TM2CNT_H: case REG_TM3CNT_H:
        return gba->memory.io[addr >> 1];

    /** SIO. */
    case REG_SIOCNT:   return gba->memory.io[addr >> 1];
    case REG_RCNT:     return gba->memory.io[addr >> 1];
    case REG_SIODATA32_LO: case REG_SIODATA32_HI:
    case REG_SIOMULTI2: case REG_SIOMULTI3:
    case REG_SIOMLT_SEND:
    case REG_JOYCNT:
    case REG_JOY_RECV_LO: case REG_JOY_RECV_HI:
    case REG_JOY_TRANS_LO: case REG_JOY_TRANS_HI:
    case REG_JOYSTAT:
        return gba->memory.io[addr >> 1];

    /** Input. */
    case REG_KEYINPUT: {
        /**
         * Return active-low key state (bit=0 means pressed).
         * gba->keysActive holds pressed keys as set bits.
         * GBA hardware returns ~pressed & 0x3FF.
         */
        gba->memory.io[REG_KEYINPUT >> 1] = 0x03FF ^ gba->keysActive;
        return gba->memory.io[REG_KEYINPUT >> 1];
    }
    case REG_KEYCNT:
        return gba->memory.io[addr >> 1];

    /** Interrupts and system. */
    case REG_IE:       return gba->ie;
    case REG_IF:       return gba->irqf;
    case REG_IME:      return gba->ime;
    case REG_WAITCNT:  return gba->memory.io[addr >> 1];
    case REG_POSTFLG:  return gba->memory.io[addr >> 1];

    case REG_MAX:
        /** Some bad interrupt libraries read here; return 0. */
        return 0;

    /** Unused registers within the IO range. */
    case 0x066: case 0x06A: case 0x06E:
    case 0x076: case 0x07A: case 0x07E:
    case 0x086: case 0x08A:
    case 0x136: case 0x142: case 0x15A:
    case 0x206: case 0x302:
        return 0;

    default:
        /** Unmapped I/O does not drive the bus. */
        return (uint16_t)(cupid_memory_open_bus(&gba->cpu) >> ((addr & 2) * 8));
    }
}
