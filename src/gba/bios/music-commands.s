@ SPDX-License-Identifier: GPL-3.0-or-later
@ Track commands consume their operands through the firmware's source guard.
.org 0x2AB0
.thumb
.thumb_func
musicCommandFine:
    push {r4, r5, lr}
    adds r5, r1, #0
    ldr r4, [r5, #32]
    cmp r4, #0
    beq musicFineTrack
musicFineVoice:
    ldrb r1, [r4]
    movs r0, #0xC7
    tst r0, r1
    beq musicFineUnlink
    movs r0, #64
    orrs r1, r0
    strb r1, [r4]
musicFineUnlink:
    adds r0, r4, #0
    bl musicUnlinkVoice
    ldr r4, [r4, #52]
    cmp r4, #0
    bne musicFineVoice
musicFineTrack:
    movs r0, #0
    strb r0, [r5]
    pop {r4, r5}
    pop {r0}
    bx r0

.thumb_func
SoundDriverGetJumpList:
    mov r12, lr
    movs r1, #36
    ldr r2, =soundInternalJumpTable
musicCopyJump:
    ldr r3, [r2]
    bl musicReadGuard
    stmia r0!, {r3}
    adds r2, #4
    subs r1, #1
    bgt musicCopyJump
    bx r12

.thumb_func
musicReadGuardByte:
    ldrb r3, [r2]
musicReadGuard:
    push {r0}
    lsrs r0, r2, #25
    bne musicReadAllowed
    ldr r0, =soundInternalJumpTable
    cmp r2, r0
    blo musicReadRejected
    lsrs r0, r2, #14
    beq musicReadAllowed
musicReadRejected:
    movs r3, #0
musicReadAllowed:
    pop {r0}
    bx lr
.balign 4, 0
.ltorg

.thumb_func
musicReadTrackByte:
    ldr r2, [r1, #64]
musicReadTrackAt:
    adds r3, r2, #1
    str r3, [r1, #64]
    ldrb r3, [r2]
    b musicReadGuard

.thumb_func
musicCommandGoto:
    push {lr}
musicGotoOperand:
    ldr r2, [r1, #64]
    ldrb r0, [r2, #3]
    lsls r0, r0, #8
    ldrb r3, [r2, #2]
    orrs r0, r3
    lsls r0, r0, #8
    ldrb r3, [r2, #1]
    orrs r0, r3
    lsls r0, r0, #8
    bl musicReadGuardByte
    orrs r0, r3
    str r0, [r1, #64]
    pop {r0}
    bx r0

.thumb_func
musicCommandPattern:
    ldrb r2, [r1, #2]
    cmp r2, #3
    bhs musicPatternOverflow
    lsls r2, r2, #2
    adds r3, r1, r2
    ldr r2, [r1, #64]
    adds r2, #4
    str r2, [r3, #68]
    ldrb r2, [r1, #2]
    adds r2, #1
    strb r2, [r1, #2]
    b musicCommandGoto
musicPatternOverflow:
    b musicCommandFine
.thumb_func
musicCommandPatternEnd:
    ldrb r2, [r1, #2]
    cmp r2, #0
    beq musicPatternEndReturn
    subs r2, #1
    strb r2, [r1, #2]
    lsls r2, r2, #2
    adds r3, r1, r2
    ldr r2, [r3, #68]
    str r2, [r1, #64]
musicPatternEndReturn:
    bx lr

.thumb_func
musicCommandRepeat:
    push {lr}
    ldr r2, [r1, #64]
    ldrb r3, [r2]
    cmp r3, #0
    bne musicRepeatCount
    adds r2, #1
    str r2, [r1, #64]
    b musicGotoOperand
musicRepeatCount:
    ldrb r3, [r1, #3]
    adds r3, #1
    strb r3, [r1, #3]
    mov r12, r3
    bl musicReadTrackByte
    cmp r12, r3
    bhs musicRepeatFinished
    b musicGotoOperand
musicRepeatFinished:
    movs r3, #0
    strb r3, [r1, #3]
    adds r2, #5
    str r2, [r1, #64]
    pop {r0}
    bx r0

.thumb_func
musicCommandPriority:
    mov r12, lr
    bl musicReadTrackByte
    strb r3, [r1, #29]
    bx r12

.thumb_func
musicCommandTempo:
    mov r12, lr
    bl musicReadTrackByte
    lsls r3, r3, #1
    strh r3, [r0, #28]
    ldrh r2, [r0, #30]
    muls r3, r2, r3
    lsrs r3, r3, #8
    strh r3, [r0, #32]
    bx r12

.thumb_func
musicCommandKeyShift:
    mov r12, lr
    bl musicReadTrackByte
    strb r3, [r1, #10]
    ldrb r3, [r1]
    movs r2, #12
    orrs r3, r2
    strb r3, [r1]
    bx r12

.thumb_func
musicCommandVoice:
    mov r12, lr
    ldr r2, [r1, #64]
    ldrb r3, [r2]
    adds r2, #1
    str r2, [r1, #64]
    lsls r2, r3, #1
    adds r2, r2, r3
    lsls r2, r2, #2
    ldr r3, [r0, #48]
    adds r2, r2, r3
    ldr r3, [r2]
    bl musicReadGuard
    str r3, [r1, #36]
    ldr r3, [r2, #4]
    bl musicReadGuard
    str r3, [r1, #40]
    ldr r3, [r2, #8]
    bl musicReadGuard
    str r3, [r1, #44]
    bx r12

.thumb_func
musicCommandVolume:
    mov r12, lr
    bl musicReadTrackByte
    strb r3, [r1, #18]
    ldrb r3, [r1]
    movs r2, #3
    orrs r3, r2
    strb r3, [r1]
    bx r12

.thumb_func
musicCommandPan:
    mov r12, lr
    bl musicReadTrackByte
    subs r3, #64
    strb r3, [r1, #20]
    ldrb r3, [r1]
    movs r2, #3
    orrs r3, r2
    strb r3, [r1]
    bx r12

.thumb_func
musicCommandBend:
    mov r12, lr
    bl musicReadTrackByte
    subs r3, #64
    strb r3, [r1, #14]
    ldrb r3, [r1]
    movs r2, #12
    orrs r3, r2
    strb r3, [r1]
    bx r12

.thumb_func
musicCommandBendRange:
    mov r12, lr
    bl musicReadTrackByte
    strb r3, [r1, #15]
    ldrb r3, [r1]
    movs r2, #12
    orrs r3, r2
    strb r3, [r1]
    bx r12

.thumb_func
musicCommandLFOSpeed:
    mov r12, lr
    bl musicReadTrackByte
    strb r3, [r1, #25]
    cmp r3, #0
    bne musicLFOSpeedReturn
    strb r3, [r1, #22]
musicLFOSpeedReturn:
    bx r12

.thumb_func
musicCommandLFODelay:
    mov r12, lr
    bl musicReadTrackByte
    strb r3, [r1, #27]
    bx r12

.thumb_func
musicCommandModulation:
    mov r12, lr
    bl musicReadTrackByte
    strb r3, [r1, #23]
    cmp r3, #0
    bne musicModulationReturn
    strb r3, [r1, #22]
musicModulationReturn:
    bx r12

.thumb_func
musicCommandModulationType:
    mov r12, lr
    bl musicReadTrackByte
    ldrb r0, [r1, #24]
    cmp r0, r3
    beq musicModulationTypeReturn
    strb r3, [r1, #24]
    ldrb r3, [r1]
    movs r2, #15
    orrs r3, r2
    strb r3, [r1]
musicModulationTypeReturn:
    bx r12

.thumb_func
musicCommandTune:
    mov r12, lr
    bl musicReadTrackByte
    subs r3, #64
    strb r3, [r1, #12]
    ldrb r3, [r1]
    movs r2, #12
    orrs r3, r2
    strb r3, [r1]
    bx r12

.thumb_func
musicCommandPort:
    mov r12, lr
    ldr r2, [r1, #64]
    ldrb r3, [r2]
    adds r2, #1
    ldr r0, =0x04000060
    adds r0, r3, r0
    bl musicReadTrackAt
    strb r3, [r0]
    bx r12
.balign 4, 0
.ltorg

.thumb_func
musicCommandEndTie:
    push {r4, lr}
    ldr r2, [r1, #64]
    ldrb r3, [r2]
    cmp r3, #128
    bhs musicEndTiePreviousKey
    strb r3, [r1, #5]
    adds r2, #1
    str r2, [r1, #64]
    b musicEndTieVoices
musicEndTiePreviousKey:
    ldrb r3, [r1, #5]
musicEndTieVoices:
    ldr r1, [r1, #32]
    cmp r1, #0
    beq musicEndTieReturn
    movs r4, #0x83
musicEndTieVoice:
    ldrb r2, [r1]
    tst r2, r4
    beq musicEndTieNext
    ldrb r0, [r1, #17]
    cmp r0, r3
    bne musicEndTieNext
    movs r0, #64
    orrs r2, r0
    strb r2, [r1]
    b musicEndTieReturn
musicEndTieNext:
    ldr r1, [r1, #52]
    cmp r1, #0
    bne musicEndTieVoice
musicEndTieReturn:
    pop {r4}
    pop {r0}
    bx r0

.balign 4, 0
musicTickDurations:
    .byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    .byte 16, 17, 18, 19, 20, 21, 22, 23, 24, 28, 30, 32, 36, 40, 42, 44
    .byte 48, 52, 54, 56, 60, 64, 66, 68, 72, 76, 78, 80, 84, 88, 90, 92, 96
@ The 49-byte data table needs three literal zero bytes across assemblers.
.byte 0, 0, 0
.arm
