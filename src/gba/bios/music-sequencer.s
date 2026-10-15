@ SPDX-License-Identifier: GPL-3.0-or-later
@ A frame chains prior players, advances tempo ticks, then updates live voices.
.org 0x2120
.thumb
.thumb_func
musicPlayerMain:
    ldr r2, =0x68736D53
    ldr r3, [r0, #52]
    cmp r2, r3
    beq musicPlayerAcquire
    bx lr
musicPlayerAcquire:
    adds r3, #1
    str r3, [r0, #52]
    push {r4-r7, lr}
    mov r4, r8
    mov r5, r9
    mov r6, r10
    mov r7, r11
    push {r4-r7}
    adds r7, r0, #0
    ldr r3, [r7, #56]
    cmp r3, #0
    beq musicPlayerStatus
    ldr r0, [r7, #60]
    bl soundFrameCall
musicPlayerStatus:
    ldr r0, [r7, #4]
    cmp r0, #0
    bge musicPlayerClock
    b musicPlayerRelease
musicPlayerClock:
    ldr r0, =0x03007FF0
    ldr r0, [r0]
    mov r8, r0
    adds r0, r7, #0
    bl musicStepFade
    ldrh r0, [r7, #34]
    ldrh r1, [r7, #32]
    adds r0, r0, r1
    b musicTempoTest
musicTempoTick:
    ldrb r2, [r7, #8]
    ldr r5, [r7, #44]
    movs r3, #1
    movs r4, #0
musicTickTrack:
    ldrb r0, [r5]
    movs r1, #128
    tst r1, r0
    bne musicTickActive
    b musicTickNext
musicTickActive:
    mov r9, r2
    mov r10, r3
    orrs r4, r3
    mov r11, r4
    ldr r4, [r5, #32]
    cmp r4, #0
    beq musicTickTrackState
musicTickVoice:
    ldrb r1, [r4]
    movs r0, #0xC7
    tst r0, r1
    beq musicTickUnlink
    ldrb r0, [r4, #16]
    cmp r0, #0
    beq musicTickVoiceNext
    subs r0, #1
    strb r0, [r4, #16]
    bne musicTickVoiceNext
    movs r0, #64
    orrs r1, r0
    strb r1, [r4]
    b musicTickVoiceNext
musicTickUnlink:
    adds r0, r4, #0
    bl musicUnlinkVoice
musicTickVoiceNext:
    ldr r4, [r4, #52]
    cmp r4, #0
    bne musicTickVoice
musicTickTrackState:
    ldrb r3, [r5]
    movs r0, #64
    tst r0, r3
    beq musicTrackWait
    adds r0, r5, #0
    bl musicClearPlayer
    movs r0, #128
    strb r0, [r5]
    movs r0, #2
    strb r0, [r5, #15]
    movs r0, #64
    strb r0, [r5, #19]
    movs r0, #22
    strb r0, [r5, #25]
    movs r0, #1
    adds r1, r5, #6
    strb r0, [r1, #30]
    b musicTrackWait
musicReadEvent:
    ldr r2, [r5, #64]
    ldrb r1, [r2]
    cmp r1, #128
    bhs musicNewEvent
    ldrb r1, [r5, #7]
    b musicDecodeEvent
musicNewEvent:
    adds r2, #1
    str r2, [r5, #64]
    cmp r1, #189
    blo musicDecodeEvent
    strb r1, [r5, #7]
musicDecodeEvent:
    cmp r1, #207
    blo musicCommandEvent
    mov r0, r8
    ldr r3, [r0, #56]
    adds r0, r1, #0
    subs r0, #207
    adds r1, r7, #0
    adds r2, r5, #0
    bl soundFrameCall
    b musicTrackWait
musicCommandEvent:
    cmp r1, #176
    bls musicWaitEvent
    adds r0, r1, #0
    subs r0, #177
    strb r0, [r7, #10]
    mov r3, r8
    ldr r3, [r3, #52]
    lsls r0, r0, #2
    adds r3, r3, r0
    ldr r3, [r3]
    adds r0, r7, #0
    adds r1, r5, #0
    bl soundFrameCall
    ldrb r0, [r5]
    cmp r0, #0
    beq musicModulationDone
    b musicTrackWait
musicWaitEvent:
    ldr r0, =musicTickDurations
    subs r1, #128
    adds r1, r1, r0
    ldrb r0, [r1]
    strb r0, [r5, #1]
musicTrackWait:
    ldrb r0, [r5, #1]
    cmp r0, #0
    beq musicReadEvent
    subs r0, #1
    strb r0, [r5, #1]
    ldrb r1, [r5, #25]
    cmp r1, #0
    beq musicModulationDone
    ldrb r0, [r5, #23]
    cmp r0, #0
    beq musicModulationDone
    ldrb r0, [r5, #28]
    cmp r0, #0
    beq musicModulate
    subs r0, #1
    strb r0, [r5, #28]
    b musicModulationDone
musicModulate:
    ldrb r0, [r5, #26]
    adds r0, r0, r1
    strb r0, [r5, #26]
    adds r1, r0, #0
    subs r0, #64
    lsls r0, r0, #24
    bpl musicModulationReflect
    lsls r2, r1, #24
    asrs r2, r2, #24
    b musicModulationScale
musicModulationReflect:
    movs r0, #128
    subs r2, r0, r1
musicModulationScale:
    ldrb r0, [r5, #23]
    muls r0, r2, r0
    asrs r2, r0, #6
    ldrb r0, [r5, #22]
    eors r0, r2
    lsls r0, r0, #24
    beq musicModulationDone
    strb r2, [r5, #22]
    ldrb r0, [r5]
    ldrb r1, [r5, #24]
    cmp r1, #0
    bne musicModulationVolume
    movs r1, #12
    b musicModulationFlags
musicModulationVolume:
    movs r1, #3
musicModulationFlags:
    orrs r0, r1
    strb r0, [r5]
musicModulationDone:
    mov r2, r9
    mov r3, r10
    mov r4, r11
musicTickNext:
    subs r2, #1
    ble musicTickFinished
    movs r0, #80
    adds r5, r5, r0
    lsls r3, r3, #1
    b musicTickTrack
musicTickFinished:
    mov r6, r11
    cmp r6, #0
    bne musicTickStatus
    movs r0, #128
    lsls r0, r0, #24
    str r0, [r7, #4]
    b musicPlayerRelease
musicTickStatus:
    str r6, [r7, #4]
    ldrh r0, [r7, #34]
    subs r0, #150
musicTempoTest:
    strh r0, [r7, #34]
    cmp r0, #150
    blo musicFrameTracks
    b musicTempoTick
musicFrameTracks:
    ldrb r2, [r7, #8]
    ldr r5, [r7, #44]
musicFrameTrack:
    ldrb r0, [r5]
    movs r1, #128
    tst r1, r0
    beq musicFrameTrackNext
    movs r1, #15
    tst r1, r0
    beq musicFrameTrackNext
    mov r9, r2
    adds r0, r7, #0
    adds r1, r5, #0
    bl musicUpdateTrack
    ldr r4, [r5, #32]
    cmp r4, #0
    beq musicFrameTrackFlags
musicFrameVoice:
    ldrb r1, [r4]
    movs r0, #0xC7
    tst r0, r1
    bne musicFrameVoiceActive
    adds r0, r4, #0
    bl musicUnlinkVoice
    b musicFrameVoiceNext
musicFrameVoiceActive:
    ldrb r0, [r4, #1]
    movs r6, #7
    ands r6, r0
    ldrb r3, [r5]
    movs r0, #3
    tst r0, r3
    beq musicFrameVoicePitch
    ldrb r1, [r4, #18]
    ldrb r0, [r5, #16]
    muls r0, r1, r0
    asrs r0, r0, #7
    strb r0, [r4, #2]
    ldrb r0, [r5, #17]
    muls r0, r1, r0
    asrs r0, r0, #7
    strb r0, [r4, #3]
    cmp r6, #0
    beq musicFrameVoicePitch
    ldrb r0, [r4, #29]
    movs r1, #1
    orrs r0, r1
    strb r0, [r4, #29]
musicFrameVoicePitch:
    movs r0, #12
    tst r0, r3
    beq musicFrameVoiceNext
    ldrb r1, [r4, #8]
    movs r0, #8
    ldrsb r0, [r5, r0]
    adds r2, r1, r0
    bpl musicFrameVoiceKey
    movs r2, #0
musicFrameVoiceKey:
    cmp r6, #0
    beq musicFramePCMFrequency
    mov r0, r8
    ldr r3, [r0, #48]
    adds r1, r2, #0
    ldrb r2, [r5, #9]
    adds r0, r6, #0
    bl soundFrameCall
    str r0, [r4, #32]
    ldrb r0, [r4, #29]
    movs r1, #2
    orrs r0, r1
    strb r0, [r4, #29]
    b musicFrameVoiceNext
musicFramePCMFrequency:
    adds r1, r2, #0
    ldrb r2, [r5, #9]
    ldr r0, [r4, #36]
    bl MidiKey2Freq
    str r0, [r4, #32]
musicFrameVoiceNext:
    ldr r4, [r4, #52]
    cmp r4, #0
    bne musicFrameVoice
musicFrameTrackFlags:
    ldrb r0, [r5]
    movs r1, #240
    ands r0, r1
    strb r0, [r5]
    mov r2, r9
musicFrameTrackNext:
    subs r2, #1
    ble musicPlayerRelease
    movs r0, #80
    adds r5, r5, r0
    bgt musicFrameTrack
musicPlayerRelease:
    ldr r0, =0x68736D53
    str r0, [r7, #52]
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
