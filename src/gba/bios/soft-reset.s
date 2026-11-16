@ @file      soft-reset.s
@ @author    cupidthecat
@ @brief     Firmware restart selection, work-area clearing and stack reset.
@ @date      2026-11-16
@
@ SPDX-License-Identifier: GPL-3.0-or-later
@ Latch the restart byte before clearing the BIOS work area and reset stacks.
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
.arm
.balign 4, 0
SoftReset:
    mov r4, #0x04000000
    ldrb r2, [r4, #-6]
    bl softResetBanks
    cmp r2, #0
    ldmdb r4, {r0-r12}
    movne lr, #0x02000000
    moveq lr, #0x08000000
    mov r0, #0x1F
    msr cpsr_fc, r0
    mov r0, #0
    bx lr

@ Write zero saved status and links, then establish supervisor and IRQ stacks.
softResetBanks:
    mov r0, #0xD3
    msr cpsr_fc, r0
    ldr sp, =0x03007FE0
    mov lr, #0
    msr spsr_fc, lr
    mov r0, #0xD2
    msr cpsr_fc, r0
    ldr sp, =0x03007FA0
    mov lr, #0
    msr spsr_fc, lr
    mov r0, #0x5F
    msr cpsr_fc, r0
    ldr sp, =0x03007F00
    adr r0, softResetClear + 1
    bx r0

.thumb
.thumb_func
softResetClear:
    movs r0, #0
    ldr r1, =-0x200
softResetWord:
    str r0, [r4, r1]
    adds r1, #4
    blt softResetWord
    bx lr
.arm
.balign 4, 0
.ltorg
