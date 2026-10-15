@ SPDX-License-Identifier: GPL-3.0-or-later
@ Attach a work area and start its timed FIFO, ring and timer configuration.
.org 0x2A00
.thumb
.thumb_func
SoundDriverInit:
    push {r3, r7, lr}
    adds r7, r0, #0
    ldr r1, =0x040000C0
    movs r0, #0
    strh r0, [r1, #6]
    strh r0, [r1, #18]
    ldr r0, =0x04000080
    movs r2, #0x8F
    strh r2, [r0, #4]
    ldr r2, =0xA90E
    strh r2, [r0, #2]
    ldrb r2, [r0, #9]
    lsls r2, r2, #26
    lsrs r2, r2, #26
    movs r3, #64
    orrs r2, r3
    movs r3, #53
    lsls r3, r3, #4
    strb r2, [r0, #9]
    adds r2, r7, r3
    str r2, [r0, #60]
    ldr r0, =0x040000A0
    movs r3, #19
    lsls r3, r3, #7
    str r0, [r1]
    adds r0, r7, r3
    str r0, [r1, #8]
    ldr r0, =0x040000A4
    ldr r2, =0x050003EC
    str r0, [r1, #12]
    ldr r0, =0x03007FF0
    str r7, [r0]
    movs r0, #0
    str r0, [sp]
    mov r0, sp
    adds r1, r7, #0
    bl CpuSet
    movs r0, #8
    strb r0, [r7, #6]
    movs r0, #15
    strb r0, [r7, #7]
    ldr r0, =musicStartNote
    str r0, [r7, #56]
    ldr r0, =soundDefaultReturn
    str r0, [r7, #40]
    str r0, [r7, #44]
    str r0, [r7, #48]
    str r0, [r7, #60]
    ldr r0, =soundInternalJumpTable
    str r0, [r7, #52]
    movs r0, #1
    lsls r0, r0, #18
    bl soundConfigureRate
    ldr r0, =0x68736D53
    str r0, [r7]
    pop {r3, r7}
    pop {r3}
    bx r3
.balign 4, 0
.ltorg

@ Library extensions start detached; the music table and note helper are live.
.thumb_func
soundDefaultReturn:
    bx lr
.arm
.balign 4, 0
