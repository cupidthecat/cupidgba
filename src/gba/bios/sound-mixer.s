@ SPDX-License-Identifier: GPL-3.0-or-later
@ The frame mixer owns the work area while callbacks and sample writes run.
.org 0x1DC4
.thumb
.thumb_func
SoundDriverMain:
    ldr r0, =0x03007FF0
    ldr r0, [r0]
    ldr r2, =0x68736D53
    ldr r3, [r0]
    cmp r2, r3
    beq soundFrameAcquire
    bx lr
soundFrameAcquire:
    adds r3, #1
    str r3, [r0]
    push {r4-r7, lr}
    mov r1, r8
    mov r2, r9
    mov r3, r10
    mov r4, r11
    push {r0-r4}
    sub sp, #20
    ldr r3, [r0, #32]
    cmp r3, #0
    beq soundFrameExtension
    ldr r0, [r0, #36]
    bl soundFrameCall
    ldr r0, [sp, #20]
soundFrameExtension:
    ldr r3, [r0, #40]
    bl soundFrameCall
    ldr r0, [sp, #20]
    ldr r3, [r0, #16]
    mov r8, r3
    ldr r5, =0x350
    adds r5, r5, r0
    ldrb r4, [r0, #4]
    subs r7, r4, #1
    bls soundFrameBuffer
    ldrb r1, [r0, #11]
    subs r1, r1, r7
    mov r2, r8
    muls r2, r1, r2
    adds r5, r5, r2
soundFrameBuffer:
    str r5, [sp, #8]
    ldr r6, =1584
    ldrb r3, [r0, #5]
    cmp r3, #0
    beq soundFrameZero
    adr r1, soundFrameReverbARM
    bx r1
.balign 4, 0
.arm
soundFrameReverbARM:
    cmp r4, #2
    addeq r7, r0, #0x350
    addne r7, r5, r8
    mov r4, r8
soundReverbSample:
    ldrsb r0, [r5, r6]
    ldrsb r1, [r5]
    add r0, r0, r1
    ldrsb r1, [r7, r6]
    add r0, r0, r1
    ldrsb r1, [r7], #1
    add r0, r0, r1
    mul r1, r0, r3
    mov r0, r1, asr #9
    tst r0, #0x80
    addne r0, r0, #1
    strb r0, [r5, r6]
    strb r0, [r5], #1
    subs r4, r4, #1
    bgt soundReverbSample
    adr r0, soundFrameChannelSetup + 1
    bx r0
.thumb
soundFrameZero:
    movs r0, #0
    mov r1, r8
    adds r6, r6, r5
    lsrs r1, r1, #3
    blo soundZeroPairs
    stmia r5!, {r0}
    stmia r6!, {r0}
soundZeroPairs:
    lsrs r1, r1, #1
    blo soundZeroBlock
    stmia r5!, {r0}
    stmia r6!, {r0}
    stmia r5!, {r0}
    stmia r6!, {r0}
soundZeroBlock:
    stmia r5!, {r0}
    stmia r6!, {r0}
    stmia r5!, {r0}
    stmia r6!, {r0}
    stmia r5!, {r0}
    stmia r6!, {r0}
    stmia r5!, {r0}
    stmia r6!, {r0}
    subs r1, #1
    bgt soundZeroBlock
soundFrameChannelSetup:
    ldr r4, [sp, #20]
    ldr r0, [r4, #20]
    mov r9, r0
    ldr r0, [r4, #24]
    mov r12, r0
    ldrb r0, [r4, #6]
    adds r4, #80
soundFrameChannelCount:
    str r0, [sp, #4]
    ldr r3, [r4, #36]
    ldrb r6, [r4]
    movs r0, #0xC7
    tst r0, r6
    bne soundEnvelopeStart
    b soundFrameNextChannel
soundEnvelopeStart:
    movs r0, #0x80
    tst r0, r6
    beq soundEnvelopeExisting
    movs r0, #0x40
    tst r0, r6
    bne soundChannelStop
    movs r6, #3
    strb r6, [r4]
    adds r0, r3, #0
    adds r0, #16
    str r0, [r4, #40]
    ldr r0, [r3, #12]
    str r0, [r4, #24]
    movs r5, #0
    strb r5, [r4, #9]
    str r5, [r4, #28]
    ldrb r2, [r3, #3]
    movs r0, #0xC0
    tst r0, r2
    beq soundEnvelopeAttack
    movs r0, #16
    orrs r6, r0
    strb r6, [r4]
    b soundEnvelopeAttack
soundEnvelopeExisting:
    ldrb r5, [r4, #9]
    movs r0, #4
    tst r0, r6
    beq soundEnvelopeRelease
    ldrb r0, [r4, #13]
    subs r0, #1
    strb r0, [r4, #13]
    bhi soundEnvelopeGain
soundChannelStop:
    movs r0, #0
    strb r0, [r4]
    b soundFrameNextChannel
soundEnvelopeRelease:
    movs r0, #0x40
    tst r0, r6
    beq soundEnvelopePhase
    ldrb r0, [r4, #7]
    muls r5, r0, r5
    lsrs r5, r5, #8
    ldrb r0, [r4, #12]
    cmp r5, r0
    bhi soundEnvelopeGain
soundEnvelopeEcho:
    ldrb r5, [r4, #12]
    cmp r5, #0
    beq soundChannelStop
    movs r0, #4
    orrs r6, r0
    strb r6, [r4]
    b soundEnvelopeGain
soundEnvelopePhase:
    movs r2, #3
    ands r2, r6
    cmp r2, #2
    bne soundEnvelopeTestAttack
    ldrb r0, [r4, #5]
    muls r5, r0, r5
    lsrs r5, r5, #8
    ldrb r0, [r4, #6]
    cmp r5, r0
    bhi soundEnvelopeGain
    adds r5, r0, #0
    beq soundEnvelopeEcho
    subs r6, #1
    strb r6, [r4]
    b soundEnvelopeGain
soundEnvelopeTestAttack:
    cmp r2, #3
    bne soundEnvelopeGain
soundEnvelopeAttack:
    ldrb r0, [r4, #4]
    adds r5, r5, r0
    cmp r5, #255
    blo soundEnvelopeGain
    movs r5, #255
    subs r6, #1
    strb r6, [r4]
soundEnvelopeGain:
    strb r5, [r4, #9]
    ldr r0, [sp, #20]
    ldrb r0, [r0, #7]
    adds r0, #1
    muls r0, r5, r0
    lsrs r5, r0, #4
    ldrb r0, [r4, #2]
    muls r0, r5, r0
    lsrs r0, r0, #8
    strb r0, [r4, #10]
    ldrb r0, [r4, #3]
    muls r0, r5, r0
    lsrs r0, r0, #8
    strb r0, [r4, #11]
    movs r0, #16
    ands r0, r6
    str r0, [sp, #16]
    beq soundChannelSamples
    adds r0, r3, #0
    adds r0, #16
    ldr r1, [r3, #8]
    adds r0, r0, r1
    str r0, [sp, #12]
    ldr r0, [r3, #12]
    subs r0, r0, r1
    str r0, [sp, #16]
soundChannelSamples:
    ldr r5, [sp, #8]
    ldr r2, [r4, #24]
    ldr r3, [r4, #40]
    adr r0, soundChannelMixARM
    bx r0
.balign 4, 0
.arm
soundChannelMixARM:
    str r8, [sp]
    ldrb r10, [r4, #10]
    ldrb r11, [r4, #11]
    ldrb r0, [r4, #1]
    tst r0, #8
    beq soundResampleBegin
soundFixedSample:
    ldrsb r6, [r3], #1
    mul r1, r6, r11
    ldrb r0, [r5, #1584]
    add r0, r0, r1, asr #8
    strb r0, [r5, #1584]
    mul r1, r6, r10
    ldrb r0, [r5]
    add r0, r0, r1, asr #8
    strb r0, [r5], #1
    subs r2, r2, #1
    bne soundFixedContinue
    ldr r2, [sp, #16]
    cmp r2, #0
    ldrne r3, [sp, #12]
    bne soundFixedContinue
    strb r2, [r4]
    b soundChannelMixed
soundFixedContinue:
    subs r8, r8, #1
    bgt soundFixedSample
    b soundChannelStorePosition
soundResampleBegin:
    ldr r7, [r4, #28]
    ldr lr, [r4, #32]
soundResampleAdvance:
    cmp r7, r9, lsl #2
    blo soundResampleTwo
soundResampleFour:
    cmp r2, #4
    ble soundResampleOne
    sub r2, r2, #4
    add r3, r3, #4
    sub r7, r7, r9, lsl #2
    cmp r7, r9, lsl #2
    bhs soundResampleFour
soundResampleTwo:
    cmp r7, r9, lsl #1
    blo soundResampleTestOne
    cmp r2, #2
    ble soundResampleOne
    sub r2, r2, #2
    add r3, r3, #2
    sub r7, r7, r9, lsl #1
soundResampleTestOne:
    cmp r7, r9
    blo soundResampleSample
soundResampleOne:
    subs r2, r2, #1
    bne soundResampleStep
    ldr r2, [sp, #16]
    cmp r2, #0
    ldrne r3, [sp, #12]
    bne soundResampleLooped
    strb r2, [r4]
    b soundChannelMixed
soundResampleStep:
    add r3, r3, #1
soundResampleLooped:
    sub r7, r7, r9
    cmp r7, r9
    bhs soundResampleOne
soundResampleSample:
    ldrsb r0, [r3]
    ldrsb r1, [r3, #1]
    sub r1, r1, r0
    mul r6, r1, r7
    mul r1, r6, r12
    add r6, r0, r1, asr #23
    mul r1, r6, r11
    ldrb r0, [r5, #1584]
    add r0, r0, r1, asr #8
    strb r0, [r5, #1584]
    mul r1, r6, r10
    ldrb r0, [r5]
    add r0, r0, r1, asr #8
    strb r0, [r5], #1
    add r7, r7, lr
    subs r8, r8, #1
    beq soundChannelStorePhase
    cmp r7, r9
    blo soundResampleSample
    b soundResampleAdvance
soundChannelStorePhase:
    str r7, [r4, #28]
soundChannelStorePosition:
    str r2, [r4, #24]
    str r3, [r4, #40]
soundChannelMixed:
    ldr r8, [sp]
    adr r0, soundFrameNextChannel + 1
    bx r0
.thumb
soundFrameNextChannel:
    ldr r0, [sp, #4]
    subs r0, #1
    ble soundFrameRelease
    adds r4, #64
    b soundFrameChannelCount
soundFrameRelease:
    ldr r0, [sp, #20]
    ldr r3, =0x68736D53
    str r3, [r0]
    add sp, #24
    pop {r0-r7}
    mov r8, r0
    mov r9, r1
    mov r10, r2
    mov r11, r3
    pop {r3}
soundFrameCall:
    bx r3
.balign 4, 0
.ltorg
.arm
