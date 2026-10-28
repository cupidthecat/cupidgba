/**
 * @file io.h
 * @brief GBA IO register addresses and dispatch.
 */

#ifndef CUPID_IO_H
#define CUPID_IO_H

#include "cupid/types.h"

struct CupidGBA;

/** @name IO Register Offsets (from 0x04000000) */

/** @name Display */
#define REG_DISPCNT     0x000
#define REG_GREENSWAP   0x002
#define REG_DISPSTAT    0x004
#define REG_VCOUNT      0x006

/** @name Backgrounds */
#define REG_BG0CNT      0x008
#define REG_BG1CNT      0x00A
#define REG_BG2CNT      0x00C
#define REG_BG3CNT      0x00E
#define REG_BG0HOFS     0x010
#define REG_BG0VOFS     0x012
#define REG_BG1HOFS     0x014
#define REG_BG1VOFS     0x016
#define REG_BG2HOFS     0x018
#define REG_BG2VOFS     0x01A
#define REG_BG3HOFS     0x01C
#define REG_BG3VOFS     0x01E

/** @name BG2/3 Affine */
#define REG_BG2PA       0x020
#define REG_BG2PB       0x022
#define REG_BG2PC       0x024
#define REG_BG2PD       0x026
#define REG_BG2X_LO     0x028
#define REG_BG2X_HI     0x02A
#define REG_BG2Y_LO     0x02C
#define REG_BG2Y_HI     0x02E
#define REG_BG3PA       0x030
#define REG_BG3PB       0x032
#define REG_BG3PC       0x034
#define REG_BG3PD       0x036
#define REG_BG3X_LO     0x038
#define REG_BG3X_HI     0x03A
#define REG_BG3Y_LO     0x03C
#define REG_BG3Y_HI     0x03E

/** @name Windows */
#define REG_WIN0H       0x040
#define REG_WIN1H       0x042
#define REG_WIN0V       0x044
#define REG_WIN1V       0x046
#define REG_WININ       0x048
#define REG_WINOUT      0x04A

/** @name Mosaic */
#define REG_MOSAIC      0x04C

/** @name Blending */
#define REG_BLDCNT      0x050
#define REG_BLDALPHA    0x052
#define REG_BLDY        0x054

/** @name Sound */
#define REG_SOUND1CNT_L 0x060
#define REG_SOUND1CNT_H 0x062
#define REG_SOUND1CNT_X 0x064
#define REG_SOUND2CNT_L 0x068
#define REG_SOUND2CNT_H 0x06C
#define REG_SOUND3CNT_L 0x070
#define REG_SOUND3CNT_H 0x072
#define REG_SOUND3CNT_X 0x074
#define REG_SOUND4CNT_L 0x078
#define REG_SOUND4CNT_H 0x07C
#define REG_SOUNDCNT_L  0x080
#define REG_SOUNDCNT_H  0x082
#define REG_SOUNDCNT_X  0x084
#define REG_SOUNDBIAS   0x088
#define REG_WAVE_RAM0_LO 0x090
#define REG_WAVE_RAM0_HI 0x092
#define REG_WAVE_RAM1_LO 0x094
#define REG_WAVE_RAM1_HI 0x096
#define REG_WAVE_RAM2_LO 0x098
#define REG_WAVE_RAM2_HI 0x09A
#define REG_WAVE_RAM3_LO 0x09C
#define REG_WAVE_RAM3_HI 0x09E
#define REG_FIFO_A_LO   0x0A0
#define REG_FIFO_A_HI   0x0A2
#define REG_FIFO_B_LO   0x0A4
#define REG_FIFO_B_HI   0x0A6

/** @name DMA */
#define REG_DMA0SAD_LO  0x0B0
#define REG_DMA0SAD_HI  0x0B2
#define REG_DMA0DAD_LO  0x0B4
#define REG_DMA0DAD_HI  0x0B6
#define REG_DMA0CNT_LO  0x0B8
#define REG_DMA0CNT_HI  0x0BA
#define REG_DMA1SAD_LO  0x0BC
#define REG_DMA1SAD_HI  0x0BE
#define REG_DMA1DAD_LO  0x0C0
#define REG_DMA1DAD_HI  0x0C2
#define REG_DMA1CNT_LO  0x0C4
#define REG_DMA1CNT_HI  0x0C6
#define REG_DMA2SAD_LO  0x0C8
#define REG_DMA2SAD_HI  0x0CA
#define REG_DMA2DAD_LO  0x0CC
#define REG_DMA2DAD_HI  0x0CE
#define REG_DMA2CNT_LO  0x0D0
#define REG_DMA2CNT_HI  0x0D2
#define REG_DMA3SAD_LO  0x0D4
#define REG_DMA3SAD_HI  0x0D6
#define REG_DMA3DAD_LO  0x0D8
#define REG_DMA3DAD_HI  0x0DA
#define REG_DMA3CNT_LO  0x0DC
#define REG_DMA3CNT_HI  0x0DE

/** @name Timers */
#define REG_TM0CNT_L    0x100
#define REG_TM0CNT_H    0x102
#define REG_TM1CNT_L    0x104
#define REG_TM1CNT_H    0x106
#define REG_TM2CNT_L    0x108
#define REG_TM2CNT_H    0x10A
#define REG_TM3CNT_L    0x10C
#define REG_TM3CNT_H    0x10E

/** @name Serial / Keypad */
#define REG_SIODATA32_LO 0x120
#define REG_SIODATA32_HI 0x122
#define REG_SIOMULTI2   0x124
#define REG_SIOMULTI3   0x126
#define REG_SIOCNT      0x128
#define REG_SIOMLT_SEND 0x12A
#define REG_KEYINPUT    0x130
#define REG_KEYCNT      0x132
#define REG_RCNT        0x134
#define REG_JOYCNT      0x140
#define REG_JOY_RECV_LO 0x150
#define REG_JOY_RECV_HI 0x152
#define REG_JOY_TRANS_LO 0x154
#define REG_JOY_TRANS_HI 0x156
#define REG_JOYSTAT     0x158

/** @name Interrupts */
#define REG_IE          0x200
#define REG_IF          0x202
#define REG_WAITCNT     0x204
#define REG_IME         0x208
#define REG_MAX         0x20A
#define REG_POSTFLG     0x300
#define REG_HALTCNT     0x301

/** @name Internal memory timing (outside the ordinary I/O array) */
#define REG_MEMCNT_LO   0x800
#define REG_MEMCNT_HI   0x802

/** @name IRQ Bit Definitions */
enum CupidIRQ {
    IRQ_VBLANK  = (1 << 0),
    IRQ_HBLANK  = (1 << 1),
    IRQ_VCOUNT  = (1 << 2),
    IRQ_TIMER0  = (1 << 3),
    IRQ_TIMER1  = (1 << 4),
    IRQ_TIMER2  = (1 << 5),
    IRQ_TIMER3  = (1 << 6),
    IRQ_SIO     = (1 << 7),
    IRQ_DMA0    = (1 << 8),
    IRQ_DMA1    = (1 << 9),
    IRQ_DMA2    = (1 << 10),
    IRQ_DMA3    = (1 << 11),
    IRQ_KEYPAD  = (1 << 12),
    IRQ_GAMEPAK = (1 << 13)
};

/** @name API */
void cupid_io_init(struct CupidGBA* gba);
void cupid_io_write(struct CupidGBA* gba, uint32_t addr, uint16_t value);
void cupid_io_write32(struct CupidGBA* gba, uint32_t addr, uint32_t value);
void cupid_io_write8(struct CupidGBA* gba, uint32_t addr, uint8_t value);
uint16_t cupid_io_read(struct CupidGBA* gba, uint32_t addr);
void cupid_raise_irq(struct CupidGBA* gba, enum CupidIRQ irq, uint32_t cyclesLate);
void cupid_raise_irq_at(struct CupidGBA* gba, enum CupidIRQ irq, uint64_t timestamp);
void cupid_test_irq(struct CupidGBA* gba, uint32_t cyclesLate);

#endif /* CUPID_IO_H */
