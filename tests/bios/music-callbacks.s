@ SPDX-License-Identifier: GPL-3.0-or-later
@ Owned callbacks loaded at 02018000h. R0-R2 and callee registers are retained.
.syntax unified
.cpu arm7tdmi
.arm
.macro arm_hook number
    .org \number * 32
    mov r12, #\number
    b record
.endm
arm_hook 0
arm_hook 1
arm_hook 2
arm_hook 3
arm_hook 4
arm_hook 5
.thumb
.macro thumb_hook number
    .org 0x400 + \number * 32
    movs r3, #\number
    mov r12, r3
    ldr r3, =0x02018600
    bx r3
    .ltorg
.endm
thumb_hook 0
thumb_hook 1
thumb_hook 2
thumb_hook 3
thumb_hook 4
thumb_hook 5
.org 0x600
.arm
record:
    push {r0-r7, r12, lr}
    ldr r4, =0x02003500
    add r5, r4, r12, lsl #5
    ldr r6, [r5]
    add r6, r6, #1
    str r6, [r5]
    str r0, [r5, #4]
    str r1, [r5, #8]
    str r2, [r5, #12]
    ldr r6, =0x03007FF0
    ldr r6, [r6]
    ldr r7, [r6]
    str r7, [r5, #16]
    ldr r7, =0x02003000
    ldr r7, [r7, #52]
    str r7, [r5, #20]
    ldr r7, [r4, #192]
    add r7, r7, r7, lsl #4
    add r7, r7, r12
    add r7, r7, #1
    str r7, [r4, #192]
    cmp r12, #0
    bne frequency
    @ The attached frame hook owns its four legacy-channel state records.
    ldr r4, [r6, #28]
    mov r5, #4
frame_voice:
    ldrb r7, [r4]
    tst r7, #0x80
    movne r7, #1
    tst r7, #0x40
    movne r7, #0
    strb r7, [r4]
    add r4, r4, #64
    subs r5, r5, #1
    bne frame_voice
frequency:
    cmp r12, #1
    bne dispatch
    @ A deterministic library-supplied frequency result, not a PSG model.
    add r0, r0, #16
    mov r0, r0, lsl #8
    add r0, r0, r1, lsl #2
    add r0, r0, r2
    str r0, [sp]
dispatch:
    cmp r12, #4
    beq note
    cmp r12, #5
    beq command
    pop {r0-r7, r12, lr}
    bx lr
note:
    pop {r0-r7, r12, lr}
    ldr r3, =0x02018F00
    ldr r3, [r3]
    bx r3
command:
    pop {r0-r7, r12, lr}
    ldr r3, =0x02018F04
    ldr r3, [r3]
    bx r3
.balign 4
.ltorg
