@ SPDX-License-Identifier: GPL-3.0-or-later
@ Note allocation respects release state, priority and the owning track list.
.org 0x3800
.thumb
.thumb_func
musicStartNote:
    push {r4-r7, lr}
    mov r4, r8
    mov r5, r9
    mov r6, r10
    mov r7, r11
    push {r4-r7}
    sub sp, #20
    str r1, [sp]
    adds r5, r2, #0
    ldr r1, =0x03007FF0
    ldr r1, [r1]
    str r1, [sp, #4]
    ldr r1, =musicTickDurations
    adds r0, r0, r1
    ldrb r0, [r0]
    strb r0, [r5, #4]
    ldr r3, [r5, #64]
    ldrb r0, [r3]
    cmp r0, #128
    bhs musicNoteTone
    strb r0, [r5, #5]
    adds r3, #1
    ldrb r0, [r3]
    cmp r0, #128
    bhs musicNoteStoreOperand
    strb r0, [r5, #6]
    adds r3, #1
    ldrb r0, [r3]
    cmp r0, #128
    bhs musicNoteStoreOperand
    ldrb r1, [r5, #4]
    adds r1, r1, r0
    strb r1, [r5, #4]
    adds r3, #1
musicNoteStoreOperand:
    str r3, [r5, #64]
musicNoteTone:
    adds r4, r5, #0
    adds r4, #36
    ldrb r2, [r4]
    movs r0, #0xC0
    tst r0, r2
    beq musicNoteDirectTone
    ldrb r3, [r5, #5]
    movs r0, #64
    tst r0, r2
    beq musicNoteMappedKey
    ldr r1, [r5, #44]
    adds r1, r1, r3
    ldrb r0, [r1]
    b musicNoteMappedRecord
musicNoteMappedKey:
    adds r0, r3, #0
musicNoteMappedRecord:
    lsls r1, r0, #1
    adds r1, r1, r0
    lsls r1, r1, #2
    ldr r0, [r5, #40]
    adds r1, r1, r0
    mov r9, r1
    mov r6, r9
    ldrb r1, [r6]
    movs r0, #0xC0
    tst r0, r1
    beq musicNoteMappedType
    b musicNoteReturn
musicNoteMappedType:
    movs r0, #128
    tst r0, r2
    beq musicNotePriority
    ldrb r1, [r6, #3]
    movs r0, #128
    tst r0, r1
    beq musicNoteDrumKey
    subs r1, #192
    lsls r1, r1, #1
    strb r1, [r5, #21]
    ldrb r0, [r5]
    movs r1, #3
    orrs r0, r1
    strb r0, [r5]
musicNoteDrumKey:
    ldrb r3, [r6, #1]
    b musicNotePriority
musicNoteDirectTone:
    mov r9, r4
    ldrb r3, [r5, #5]
musicNotePriority:
    str r3, [sp, #8]
    ldr r6, [sp]
    ldrb r1, [r6, #9]
    ldrb r0, [r5, #29]
    adds r0, r0, r1
    cmp r0, #255
    bls musicNotePriorityReady
    movs r0, #255
musicNotePriorityReady:
    str r0, [sp, #16]
    mov r6, r9
    ldrb r0, [r6]
    movs r6, #7
    ands r6, r0
    str r6, [sp, #12]
    beq musicNoteFindPCM
    ldr r0, [sp, #4]
    ldr r4, [r0, #28]
    cmp r4, #0
    bne musicNoteCGBChannel
    b musicNoteReturn
musicNoteCGBChannel:
    subs r6, #1
    lsls r0, r6, #6
    adds r4, r4, r0
    ldrb r1, [r4]
    movs r0, #0xC7
    tst r0, r1
    beq musicNoteClaim
    movs r0, #64
    tst r0, r1
    bne musicNoteClaim
    ldrb r1, [r4, #19]
    ldr r0, [sp, #16]
    cmp r1, r0
    blo musicNoteClaim
    beq musicNoteCGBEqual
    b musicNoteReturn
musicNoteCGBEqual:
    ldr r0, [r4, #44]
    cmp r0, r5
    bhs musicNoteClaim
    b musicNoteReturn
musicNoteFindPCM:
    ldr r6, [sp, #16]
    adds r7, r5, #0
    movs r2, #0
    mov r8, r2
    ldr r4, [sp, #4]
    ldrb r3, [r4, #6]
    adds r4, #80
musicNoteCandidate:
    ldrb r1, [r4]
    movs r0, #0xC7
    tst r0, r1
    beq musicNoteClaim
    movs r0, #64
    tst r0, r1
    beq musicNoteCandidateLive
    cmp r2, #0
    bne musicNoteCandidatePriority
    adds r2, #1
    ldrb r6, [r4, #19]
    ldr r7, [r4, #44]
    b musicNoteChooseCandidate
musicNoteCandidateLive:
    cmp r2, #0
    bne musicNoteNextCandidate
musicNoteCandidatePriority:
    ldrb r0, [r4, #19]
    cmp r0, r6
    bhs musicNoteCandidateEqual
    adds r6, r0, #0
    ldr r7, [r4, #44]
    b musicNoteChooseCandidate
musicNoteCandidateEqual:
    bhi musicNoteNextCandidate
    ldr r0, [r4, #44]
    cmp r0, r7
    bls musicNoteCandidateOwner
    adds r7, r0, #0
    b musicNoteChooseCandidate
musicNoteCandidateOwner:
    blo musicNoteNextCandidate
musicNoteChooseCandidate:
    mov r8, r4
musicNoteNextCandidate:
    adds r4, #64
    subs r3, #1
    bgt musicNoteCandidate
    mov r4, r8
    cmp r4, #0
    beq musicNoteReturn
musicNoteClaim:
    adds r0, r4, #0
    bl musicUnlinkVoice
    movs r1, #0
    str r1, [r4, #48]
    ldr r3, [r5, #32]
    str r3, [r4, #52]
    cmp r3, #0
    beq musicNoteTrackHead
    str r4, [r3, #48]
musicNoteTrackHead:
    str r4, [r5, #32]
    str r5, [r4, #44]
    ldrb r0, [r5, #27]
    strb r0, [r5, #28]
    cmp r0, r1
    beq musicNoteTrackGain
    strb r1, [r5, #26]
    strb r1, [r5, #22]
musicNoteTrackGain:
    ldr r0, [sp]
    adds r1, r5, #0
    bl musicUpdateTrack
    ldr r0, [r5, #4]
    str r0, [r4, #16]
    ldr r0, [sp, #16]
    strb r0, [r4, #19]
    ldr r0, [sp, #8]
    strb r0, [r4, #8]
    mov r6, r9
    ldrb r0, [r6]
    strb r0, [r4, #1]
    ldr r7, [r6, #4]
    str r7, [r4, #36]
    ldr r0, [r6, #8]
    str r0, [r4, #4]
    ldrh r0, [r5, #30]
    strh r0, [r4, #12]
    ldrb r1, [r4, #18]
    ldrb r0, [r5, #16]
    muls r0, r1, r0
    asrs r0, r0, #7
    strb r0, [r4, #2]
    ldrb r0, [r5, #17]
    muls r0, r1, r0
    asrs r0, r0, #7
    strb r0, [r4, #3]
    ldrb r1, [r4, #8]
    movs r0, #8
    ldrsb r0, [r5, r0]
    adds r3, r1, r0
    bpl musicNoteKeyReady
    movs r3, #0
musicNoteKeyReady:
    ldr r6, [sp, #12]
    cmp r6, #0
    beq musicNotePCMFrequency
    mov r6, r9
    ldrb r0, [r6, #2]
    strb r0, [r4, #30]
    ldrb r1, [r6, #3]
    movs r0, #128
    tst r0, r1
    bne musicNoteCGBFrequency
    strb r1, [r4, #31]
musicNoteCGBFrequency:
    ldrb r2, [r5, #9]
    adds r1, r3, #0
    ldr r0, [sp, #12]
    ldr r3, [sp, #4]
    ldr r3, [r3, #48]
    bl soundFrameCall
    b musicNoteStartVoice
musicNotePCMFrequency:
    ldrb r2, [r5, #9]
    adds r1, r3, #0
    adds r0, r7, #0
    bl MidiKey2Freq
musicNoteStartVoice:
    str r0, [r4, #32]
    movs r0, #128
    strb r0, [r4]
    ldrb r1, [r5]
    movs r0, #240
    ands r0, r1
    strb r0, [r5]
musicNoteReturn:
    add sp, #20
    pop {r0-r7}
    mov r8, r0
    mov r9, r1
    mov r10, r2
    mov r11, r3
    pop {r0}
    bx r0
.balign 4, 0
.ltorg
.arm
