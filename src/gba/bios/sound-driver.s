@ SPDX-License-Identifier: GPL-3.0-or-later
@ Sound work-area and FIFO DMA changes execute through the ordinary CPU bus.
.org 0x1E00
.thumb

.thumb_func
SoundDriverMode:
    push {r4, r5, r7, lr}
    ldr r1, =0x03007FF0
    ldr r5, =0x68736D53
    ldr r7, [r1]
    ldr r1, [r7]
    cmp r1, r5
    bne soundModeReturn
    adds r1, #1
    str r1, [r7]
    lsls r1, r0, #24
    lsrs r1, r1, #24
    beq soundModeChannels
    lsls r1, r1, #25
    lsrs r1, r1, #25
    strb r1, [r7, #5]
soundModeChannels:
    movs r1, #15
    lsls r1, r1, #8
    ands r1, r0
    beq soundModeVolume
    lsrs r1, r1, #8
    strb r1, [r7, #6]
    movs r1, #12
    movs r3, #0
    adds r2, r7, #7
    adds r2, #73
soundModeStopChannel:
    strb r3, [r2]
    adds r2, #64
    subs r1, #1
    bne soundModeStopChannel
soundModeVolume:
    movs r1, #15
    lsls r1, r1, #12
    ands r1, r0
    beq soundModeResolution
    lsrs r1, r1, #12
    strb r1, [r7, #7]
soundModeResolution:
    movs r1, #11
    lsls r1, r1, #20
    ands r1, r0
    beq soundModeRate
    movs r3, #3
    lsls r3, r3, #20
    ldr r2, =0x04000080
    ands r1, r3
    ldrb r3, [r2, #9]
    lsrs r1, r1, #14
    lsls r3, r3, #26
    lsrs r3, r3, #26
    orrs r1, r3
    strb r1, [r2, #9]
soundModeRate:
    movs r4, #15
    lsls r4, r4, #16
    ands r4, r0
    beq soundModeUnlock
    bl SoundDriverVsyncOff
    adds r0, r4, #0
    bl soundConfigureRate
soundModeUnlock:
    str r5, [r7]
soundModeReturn:
    pop {r4, r5, r7}
    pop {r3}
    bx r3
.balign 4, 0
.ltorg

.thumb_func
SoundChannelClear:
    push {r4-r7, lr}
    ldr r0, =0x03007FF0
    ldr r6, =0x68736D53
    ldr r7, [r0]
    ldr r0, [r7]
    cmp r0, r6
    bne soundClearReturn
    adds r0, #1
    str r0, [r7]
    adds r0, r7, #7
    movs r1, #12
    adds r0, #73
soundClearChannel:
    movs r2, #0
    strb r2, [r0]
    adds r0, #64
    subs r1, #1
    cmp r1, #0
    bgt soundClearChannel
    ldr r5, [r7, #28]
    cmp r5, #0
    beq soundClearUnlock
    movs r4, #1
soundClearExtension:
    lsls r0, r4, #24
    lsrs r0, r0, #24
    ldr r1, [r7, #44]
    bl soundCallR1
    adds r4, #1
    adds r5, #64
    cmp r4, #4
    ble soundClearExtension
    movs r2, #0
    strb r2, [r5]
soundClearUnlock:
    str r6, [r7]
soundClearReturn:
    pop {r4-r7}
    pop {r3}
    bx r3
.balign 4, 0
.ltorg

.thumb_func
SoundDriverVsyncOff:
    push {r3, r7, lr}
    ldr r0, =0x03007FF0
    ldr r3, =0x68736D53
    ldr r7, [r0]
    ldr r0, [r7]
    cmp r0, r3
    blo soundOffReturn
    adds r3, #1
    cmp r0, r3
    bhi soundOffReturn
    adds r0, #1
    str r0, [r7]
    movs r0, #0
    ldr r1, =0x040000C0
    movs r3, #53
    strh r0, [r1, #6]
    strh r0, [r1, #18]
    strb r0, [r7, #4]
    lsls r3, r3, #4
    adds r1, r7, r3
    str r0, [sp]
    mov r0, sp
    ldr r2, =0x05000318
    bl CpuSet
    ldr r0, [r7]
    subs r0, #1
    str r0, [r7]
soundOffReturn:
    pop {r3, r7}
    pop {r3}
    bx r3
.balign 4, 0
.ltorg

.thumb_func
SoundDriverVsyncOn:
    movs r1, #91
    ldr r0, =0x040000C0
    lsls r1, r1, #9
    strh r1, [r0, #6]
    strh r1, [r0, #18]
    bx lr
.balign 4, 0
.ltorg

.thumb_func
SoundDriverVsync:
    ldr r0, =0x03007FF0
    ldr r0, [r0]
    ldr r2, =0x68736D53
    ldr r3, [r0]
    cmp r2, r3
    bne soundVsyncReturn
    ldrb r1, [r0, #4]
    subs r1, #1
    strb r1, [r0, #4]
    bgt soundVsyncReturn
    ldrb r1, [r0, #11]
    strb r1, [r0, #4]
    movs r0, #0
    movs r1, #182
    lsls r1, r1, #8
    ldr r2, =0x040000C6
    ldr r3, =0x040000D2
    strh r0, [r2]
    strh r0, [r3]
    strh r1, [r2]
    strh r1, [r3]
soundVsyncReturn:
    bx lr
.balign 4, 0
.ltorg

@ Each nominal rate selects an integer number of samples per 280896-clock frame.
.thumb_func
soundConfigureRate:
    push {r4, r7, lr}
    ldr r1, =0x03007FF0
    movs r3, #15
    lsls r3, r3, #16
    ands r0, r3
    ldr r7, [r1]
    lsrs r0, r0, #16
    strb r0, [r7, #8]
    ldr r1, =soundSamplesPerFrame
    lsls r0, r0, #1
    adds r0, r0, r1
    subs r0, #2
    ldrh r0, [r0]
    movs r1, #99
    lsls r1, r1, #4
    adds r4, r0, #0
    str r0, [r7, #16]
    bl soundRatio
    strb r0, [r7, #11]
    ldr r0, =597275
    ldr r3, =5000
    muls r0, r4, r0
    adds r1, r0, r3
    lsls r0, r3, #1
    bl soundRatio
    movs r1, #1
    lsls r1, r1, #24
    str r0, [r7, #20]
    bl soundRatio
    adds r0, #1
    asrs r0, r0, #1
    str r0, [r7, #24]
    ldr r4, =0x04000100
    movs r0, #0
    strh r0, [r4, #2]
    ldr r0, [r7, #16]
    ldr r1, =280896
    bl soundRatio
    movs r1, #1
    lsls r1, r1, #16
    subs r0, r1, r0
    strh r0, [r4]
    bl SoundDriverVsyncOn
    movs r0, #1
    lsls r0, r0, #26
soundLeaveSyncLine:
    ldrb r1, [r0, #6]
    cmp r1, #159
    beq soundLeaveSyncLine
soundWaitSyncLine:
    ldrb r1, [r0, #6]
    cmp r1, #159
    bne soundWaitSyncLine
    movs r0, #128
    strh r0, [r4, #2]
    pop {r4, r7}
    pop {r3}
    bx r3
.balign 4, 0
.ltorg

.thumb_func
soundCallR1:
    bx r1
.balign 4, 0
.thumb_func
soundRatio:
    bx pc
.balign 4, 0
.arm
    b DivArm
.balign 4, 0
.ltorg
soundSamplesPerFrame:
    .hword 96, 132, 176, 224, 264, 304, 352
    .hword 448, 528, 608, 672, 704, 0, 0, 0
.arm
.balign 4, 0
