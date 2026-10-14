@ SPDX-License-Identifier: GPL-3.0-or-later
@ Reset groups expose their zero fills and device writes to the ordinary bus.
.balign 4, 0
.thumb
.thumb_func
RegisterRamReset:
    push {r4-r7, lr}
    sub sp, #4
    movs r4, r0
    ldr r6, =0x85000000
    movs r5, #4
    lsls r5, r5, #24
    movs r3, #0
    str r3, [sp]
    movs r1, #0x80
    strh r1, [r5]

    movs r7, #0x80
    tst r7, r4
    beq resetSerial
    lsrs r1, r5, #17
    adds r1, r1, r5
    movs r2, #8
    bl resetFill
    subs r1, #0x20
    mvns r0, r2
    strh r0, [r1, #2]
    lsrs r1, r5, #16
    adds r1, r1, r5
    strb r0, [r1, #16]
    adds r1, r5, #4
    movs r2, #8
    bl resetFill
    .inst.n 0x1F09 @ subs r1, r1, #4; retain the three-operand Thumb encoding.
    movs r2, #16
    bl resetFill
    movs r1, #0xB0
    adds r1, r1, r5
    movs r2, #24
    bl resetFill
    str r2, [r1, #0x20]
    lsrs r0, r5, #18
    strh r0, [r5, #0x20]
    strh r0, [r5, #0x30]
    strh r0, [r5, #0x26]
    strh r0, [r5, #0x36]

resetSerial:
    movs r7, #0x20
    ldr r1, =0x04000110
    movs r2, #8
    bl resetFill
    @ An absent group leaves R1 at its start, affecting these following writes.
    lsrs r2, r5, #11
    strh r2, [r1, #4]
    adds r1, #0x10
    movs r2, #7
    strb r2, [r1]
    bl resetFill

    movs r7, #0x40
    tst r7, r4
    beq resetExternalRam
    movs r1, #0x80
    adds r1, r1, r5
    ldr r0, =0x880E0000
    strb r0, [r1, #4]
    strb r1, [r1, #4]
    str r0, [r1]
    ldrh r0, [r1, #8]
    lsls r0, r0, #22
    lsrs r0, r0, #22
    strh r0, [r1, #8]
    subs r1, #0x10
    strb r1, [r1]
    adds r1, #0x20
    movs r2, #8
    bl resetFill
    subs r1, #0x40
    strb r2, [r1]
    adds r1, #0x20
    movs r2, #8
    bl resetFill
    movs r2, #0
    movs r1, #0x80
    adds r1, r1, r5
    strb r2, [r1, #4]

resetExternalRam:
    movs r7, #1
    lsrs r1, r5, #1
    lsrs r2, r5, #10
    bl resetFill
    movs r7, #8
    movs r1, #6
    lsls r1, r1, #24
    lsrs r2, r1, #12
    bl resetFill
    movs r7, #16
    movs r1, #7
    lsls r1, r1, #24
    lsrs r2, r5, #18
    bl resetFill
    movs r7, #4
    movs r1, #5
    lsls r1, r1, #24
    lsrs r2, r5, #18
    bl resetFill
    movs r7, #2
    movs r1, #3
    lsls r1, r1, #24
    ldr r2, =0x1F80
    bl resetFill
    add sp, #4
    pop {r4-r7}
    pop {r3}
    bx r3

@ A selected group fills complete eight-word blocks from the local zero word.
.thumb_func
resetFill:
    tst r7, r4
    bne resetSelected
    bx lr
resetSelected:
    mov r0, sp
    orrs r2, r6
    b resetFastSet
.ltorg
.balign 4, 0
.arm
