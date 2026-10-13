@ SPDX-License-Identifier: GPL-3.0-or-later
@ The Thumb reset helper enters the ordinary ARM block-fill routine directly.
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

