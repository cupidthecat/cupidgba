@ @file      checksum.s
@ @author    cupidthecat
@ @brief     Firmware word checksum service and returned address state.
@ @date      2026-11-16
@
@ Read every firmware word, preserving the service's mask and address results.
@
@ @copyright Copyright (c) 2026 cupidthecat
@
@ This file is part of CupidGBA.
@
@ CupidGBA is free software: you can redistribute it and/or modify
@ it under the terms of the GNU General Public License as published by
@ the Free Software Foundation, either version 3 of the License, or
@ (at your option) any later version.
@
@ CupidGBA is distributed in the hope that it will be useful,
@ but WITHOUT ANY WARRANTY; without even the implied warranty of
@ MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
@ GNU General Public License for more details.
@
@ You should have received a copy of the GNU General Public License
@ along with CupidGBA.  If not, see <https://gnu.org>.
.org 0x1840
.arm
GetBiosChecksum:
    mov r0, #0
    mov r3, #0
checksumWord:
    mov r12, #0xDF
    ldmia r3!, {r2}
    msr cpsr, r12
    add r0, r0, r2
    movs r1, r3, lsr #14
    beq checksumWord
    bx lr
