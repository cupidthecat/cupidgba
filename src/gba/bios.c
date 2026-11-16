/**
 * @file      bios.c
 * @author    cupidthecat
 * @brief     SWI exception entry for the built-in BIOS.
 * @date      2026-11-16
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
#include "cupid/gba.h"
#include <stdlib.h>

/**
 * @brief SWI dispatcher.
 */
void cupid_bios_swi(struct CupidGBA *gba, uint8_t comment) {
    switch (comment) {
        case 0x00:
            /* Firmware clears the reset work area and refills the selected entry pipeline. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        case 0x01:
            /* Reset fills and device writes run through the timed firmware. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        case 0x02:
        case 0x04:
        case 0x05:
            /* Execute the replacement BIOS wait loop through the CPU pipeline. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        case 0x03:
        case 0x27:
            /* Sleep's register strobe and return execute through timed firmware. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B:
        case 0x0C:
            /* Execute math and copy services through the timed CPU and bus. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        case 0x0D:
            /* Read and sum the firmware through the CPU and bus. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        case 0x0E:
        case 0x0F:
            /* Affine instructions retain integer rounding, caller state and bus timing. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19:
            /* Utility instructions expose their memory accesses and peripheral clocks. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        case 0x1A:
        case 0x1B:
        case 0x1C:
        case 0x1D:
        case 0x1E:
        case 0x28:
        case 0x29:
            /* Driver work-area and sound DMA accesses execute as firmware. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        case 0x1F:
            /* Fixed-point interpolation and the wave read run through firmware. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x23:
        case 0x24:
        case 0x2A:
            /* Music state, track commands and callable entries run through firmware. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        case 0x26:
            /* Warm reboot runs firmware reset writes and refills the entry pipeline. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        case 0x25:
            /* Download words and checksums use timed serial register accesses. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
        default:
            /* The CPU traps before firmware interprets the service number. */
            cupid_arm_raise_swi(&gba->cpu);
            break;
    }
}
