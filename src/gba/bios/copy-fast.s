@ @file      copy-fast.s
@ @author    cupidthecat
@ @brief     Firmware block-copy and fill service with a Thumb reset entry.
@ @date      2026-11-16
@
@ SPDX-License-Identifier: GPL-3.0-or-later
@ The Thumb reset helper enters the ordinary ARM block-fill routine directly.
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
.balign 4, 0
.thumb
.thumb_func
resetFastSet:
    mov r3, pc
    bx r3
.arm
CpuFastSet:
stmfd  sp!, {r4-r10, lr}
mov    r10, r2, lsl #11
movs   r12, r10, lsr #9
bl     copySourceValid
beq    2f
add    r10, r1, r10, lsr #9
movs   r2, r2, lsr #25
bcc    0f
@ Fill
ldr    r2, [r0]
mov    r3, r2
mov    r4, r2
mov    r5, r2
mov    r6, r2
mov    r7, r2
mov    r8, r2
mov    r9, r2
1:
cmp    r1, r10
stmialt r1!, {r2-r9}
blt    1b
b      2f
@ Copy
0:
cmp    r1, r10
ldmialt r0!, {r2-r9}
stmialt r1!, {r2-r9}
blt    0b
2:
ldmfd  sp!, {r4-r10, lr}
bx     lr

