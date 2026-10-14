/**
 * @file bios.c
 * @brief High-Level Emulation of GBA BIOS SWI calls.
 */

#include "cupid/gba.h"
#include <stdlib.h>

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
 * @brief SWI dispatcher.
 */
void cupid_bios_swi(struct CupidGBA* gba, uint8_t comment) {
    switch (comment) {
    case 0x00:
        /* Firmware clears the reset work area and refills the selected entry pipeline. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
    case 0x01:
        /* Reset fills and device writes run through the timed firmware. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
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
    case 0x0E: case 0x0F:
        /* Affine instructions retain integer rounding, caller state and bus timing. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
    case 0x15: case 0x16: case 0x17: case 0x18: case 0x19:
        /* Utility instructions expose their memory accesses and peripheral clocks. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
    case 0x1F:
        /* Fixed-point interpolation and the wave read run through firmware. */
        cupid_arm_raise_swi(&gba->cpu);
        break;
    default: break;    /** Unknown SWI */
    }
}
