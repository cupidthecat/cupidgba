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
 * @brief SWI 0x02: Halt.
 */
static void _Halt(struct CupidGBA* gba) {
    cupid_gba_halt(gba);
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
    case 0x01:
        /* Reset fills and device writes run through the timed firmware. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
    case 0x02: case 0x04: case 0x05:
        /* Execute the replacement BIOS wait loop through the CPU pipeline. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
    case 0x03: _Halt(gba); break;  /** Stop -- treat as halt */
    case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A:
    case 0x0B: case 0x0C:
        /* Execute math and copy services through the timed CPU and bus. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
    case 0x0D: _GetBiosChecksum(gba); break;
    case 0x0E: case 0x0F:
        /* Affine instructions retain integer rounding, caller state and bus timing. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
    case 0x15: case 0x16: case 0x17: case 0x18: case 0x19:
        /* Utility instructions expose their memory accesses and peripheral clocks. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
    case 0x1F: _MidiKey2Freq(gba); break;
    default: break;    /** Unknown SWI */
    }
}
