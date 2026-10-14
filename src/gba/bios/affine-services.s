@ SPDX-License-Identifier: GPL-3.0-or-later
@ Affine memory operations and signed multiplications use the CPU and bus clocks.
.arm
.balign 4, 0

@ Only the high angle byte participates. Cosine is the quarter-turn sine value.
.macro affineTrig index, cosine, temporary
    mov \index, \index, lsr #8
    adr r12, affineSine
    add \temporary, \index, #64
    and \temporary, \temporary, #255
    mov \temporary, \temporary, lsl #1
    ldrsh \cosine, [r12, \temporary]
    mov \temporary, \index, lsl #1
    ldrsh r12, [r12, \temporary]
.endm

BgAffineSet:
    push {r4-r11}
affineBackgroundEntry:
    subs r2, r2, #1
    blt affineBackgroundDone
    ldrh r3, [r0, #16]
    affineTrig r3, r8, r7
    ldrsh r9, [r0, #12]
    ldrsh r10, [r0, #14]
    mul r7, r8, r9
    mov r3, r7, asr #14
    mul r7, r12, r9
    mov r4, r7, asr #14
    mul r7, r12, r10
    mov r5, r7, asr #14
    mul r7, r8, r10
    mov r6, r7, asr #14

    @ Origins use the shifted coefficients before their halfword stores.
    ldmia r0, {r9-r11}
    mov r8, r11, lsl #16
    mov r8, r8, asr #16
    mov r12, r11, asr #16
    rsb r7, r8, #0
    mla r9, r3, r7, r9
    mla r7, r4, r12, r9
    str r7, [r1, #8]
    rsb r7, r8, #0
    mla r10, r5, r7, r10
    rsb r7, r12, #0
    mla r7, r6, r7, r10
    str r7, [r1, #12]
    strh r3, [r1]
    rsb r4, r4, #0
    strh r4, [r1, #2]
    strh r5, [r1, #4]
    strh r6, [r1, #6]
    add r0, r0, #20
    add r1, r1, #16
    b affineBackgroundEntry
affineBackgroundDone:
    pop {r4-r11}
    bx lr

ObjAffineSet:
    push {r8-r11}
affineObjectEntry:
    subs r2, r2, #1
    blt affineObjectDone
    ldrh r8, [r0, #4]
    affineTrig r8, r11, r10
    ldrsh r8, [r0]
    ldrsh r9, [r0, #2]
    mul r10, r11, r8
    mov r10, r10, asr #14
    strh r10, [r1], r3
    mul r10, r12, r8
    mov r10, r10, asr #14
    rsb r10, r10, #0
    strh r10, [r1], r3
    mul r10, r12, r9
    mov r10, r10, asr #14
    strh r10, [r1], r3
    mul r10, r11, r9
    mov r10, r10, asr #14
    strh r10, [r1], r3
    add r0, r0, #8
    b affineObjectEntry
affineObjectDone:
    pop {r8-r11}
    bx lr

.balign 4, 0
affineSine:
.include "src/gba/bios/affine-sine.inc"
