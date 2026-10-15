@ SPDX-License-Identifier: GPL-3.0-or-later
@ Players attach a frame callback and own an array of 80-byte tracks.
.org 0x194C
.thumb
.thumb_func
MusicPlayerOpen:
    push {r4, r5, r7, lr}
    adds r4, r2, #0
    adds r5, r1, #0
    adds r7, r0, #0
    cmp r2, #1
    blt musicOpenReturn
    cmp r4, #16
    ble musicOpenClear
    movs r4, #16
musicOpenClear:
    adds r0, r7, #0
    bl musicClearPlayer
    str r5, [r7, #44]
    ldr r0, =0x80000000
    strb r4, [r7, #8]
    str r0, [r7, #4]
    movs r0, #0
    b musicOpenTrackTest
musicOpenTrack:
    subs r1, r4, #1
    lsls r4, r1, #24
    lsrs r4, r4, #24
    strb r0, [r5]
    adds r5, #80
musicOpenTrackTest:
    cmp r4, #0
    bgt musicOpenTrack
    ldr r1, =0x03007FC0
    ldr r4, =0x68736D53
    ldr r1, [r1, #48]
    ldr r2, [r1]
    cmp r2, r4
    bne musicOpenReturn
    adds r2, #1
    str r2, [r1]
    ldr r2, [r1, #32]
    cmp r2, #0
    beq musicOpenAttach
    str r2, [r7, #56]
    ldr r2, [r1, #36]
    str r2, [r7, #60]
    str r0, [r1, #32]
musicOpenAttach:
    ldr r0, =musicPlayerMain
    str r7, [r1, #36]
    str r0, [r1, #32]
    str r4, [r1]
    str r4, [r7, #52]
musicOpenReturn:
    pop {r4, r5, r7}
    pop {r3}
    bx r3
.balign 4, 0
.ltorg

.thumb_func
MusicPlayerStart:
    push {r4-r7, lr}
    adds r7, r0, #0
    ldr r0, [r0, #52]
    ldr r3, =0x68736D53
    adds r4, r1, #0
    cmp r0, r3
    bne musicStartReturn
    adds r0, #1
    str r0, [r7, #52]
    movs r1, #0
    str r1, [r7, #4]
    str r4, [r7]
    ldr r0, [r4, #4]
    str r0, [r7, #48]
    ldrb r0, [r4, #2]
    strb r0, [r7, #9]
    movs r0, #150
    strh r0, [r7, #28]
    strh r0, [r7, #32]
    movs r0, #255
    adds r0, #1
    strh r0, [r7, #30]
    strh r1, [r7, #34]
    strh r1, [r7, #36]
    ldr r5, [r7, #44]
    movs r6, #0
    b musicStartTrackTest
musicStartTrack:
    adds r0, r7, #0
    adds r1, r5, #0
    bl musicStopTrack
    movs r0, #0xC0
    strb r0, [r5]
    lsls r0, r6, #2
    adds r0, r0, r4
    ldr r0, [r0, #8]
    str r0, [r5, #64]
    adds r5, #80
    adds r6, #1
musicStartTrackTest:
    ldrb r0, [r4]
    cmp r6, r0
    bge musicStartUnusedTest
    ldrb r0, [r7, #8]
    cmp r0, r6
    bgt musicStartTrack
    b musicStartUnusedTest
musicStartUnused:
    adds r0, r7, #0
    adds r1, r5, #0
    bl musicStopTrack
    movs r0, #0
    strb r0, [r5]
    adds r5, #80
    adds r6, #1
musicStartUnusedTest:
    ldrb r0, [r7, #8]
    cmp r0, r6
    bgt musicStartUnused
    ldrb r0, [r4, #3]
    lsrs r1, r0, #8
    blo musicStartRelease
    bl SoundDriverMode
musicStartRelease:
    ldr r0, =0x68736D53
    str r0, [r7, #52]
musicStartReturn:
    pop {r4-r7}
    pop {r3}
    bx r3
.balign 4, 0
.ltorg

.thumb_func
MusicPlayerStop:
    push {r4-r7, lr}
    adds r7, r0, #0
    ldr r0, [r0, #52]
    ldr r6, =0x68736D53
    cmp r0, r6
    bne musicStopReturn
    adds r0, #1
    str r0, [r7, #52]
    ldr r0, [r7, #4]
    lsls r3, r6, #31
    orrs r0, r3
    str r0, [r7, #4]
    ldrb r5, [r7, #8]
    ldr r4, [r7, #44]
    b musicStopTrackTest
musicStopOneTrack:
    adds r0, r7, #0
    adds r1, r4, #0
    bl musicStopTrack
    adds r4, #80
    subs r5, #1
musicStopTrackTest:
    cmp r5, #0
    bgt musicStopOneTrack
    str r6, [r7, #52]
musicStopReturn:
    pop {r4-r7}
    pop {r3}
    bx r3
.balign 4, 0
.ltorg

.thumb_func
MusicPlayerContinue:
    ldr r2, [r0, #52]
    ldr r1, =0x68736D53
    cmp r2, r1
    bne musicContinueReturn
    ldr r2, [r0, #4]
    str r1, [r0, #52]
    lsls r2, r2, #1
    lsrs r2, r2, #1
    str r2, [r0, #4]
musicContinueReturn:
    bx lr
.balign 4, 0
.ltorg

.thumb_func
MusicPlayerFadeOut:
    push {r7}
    ldr r7, [r0, #52]
    ldr r2, =0x68736D53
    cmp r7, r2
    bne musicFadeReturn
    strh r1, [r0, #38]
    strh r1, [r0, #36]
    movs r1, #255
    adds r1, #1
    strh r1, [r0, #40]
    str r2, [r0, #52]
musicFadeReturn:
    pop {r7}
    bx lr
.balign 4, 0
.ltorg

@ Fade state changes track gain only on the selected frame interval.
.thumb_func
musicStepFade:
    push {r4-r7, lr}
    adds r7, r0, #0
    ldrh r0, [r0, #36]
    cmp r0, #0
    beq musicFadeStepReturn
    ldrh r1, [r7, #38]
    subs r1, #1
    lsls r1, r1, #16
    lsrs r1, r1, #16
    strh r1, [r7, #38]
    bne musicFadeStepReturn
    ldrh r1, [r7, #40]
    subs r1, #16
    strh r1, [r7, #40]
    lsls r1, r1, #16
    asrs r1, r1, #16
    cmp r1, #0
    bgt musicFadeApply
    ldrb r5, [r7, #8]
    ldr r4, [r7, #44]
    movs r6, #0
    b musicFadeStopTest
musicFadeStop:
    adds r0, r7, #0
    adds r1, r4, #0
    bl musicStopTrack
    strb r6, [r4]
    adds r4, #80
    subs r5, #1
musicFadeStopTest:
    cmp r5, #0
    bgt musicFadeStop
musicFadeStepReturn:
    pop {r4-r7}
    pop {r3}
    bx r3
musicFadeApply:
    strh r0, [r7, #38]
    ldrb r1, [r7, #8]
    ldr r0, [r7, #44]
    b musicFadeGainTest
musicFadeGain:
    ldrb r2, [r0]
    lsrs r3, r2, #8
    blo musicFadeGainNext
    ldrh r3, [r7, #40]
    lsrs r3, r3, #2
    strb r3, [r0, #19]
    movs r3, #3
    orrs r2, r3
    strb r2, [r0]
musicFadeGainNext:
    adds r0, #80
    subs r1, #1
musicFadeGainTest:
    cmp r1, #0
    bgt musicFadeGain
    b musicFadeStepReturn

@ Derived gain and pitch retain signed byte fields and integer truncation.
.thumb_func
musicUpdateTrack:
    push {r4, r5, r7, lr}
    ldrb r5, [r1]
    adds r7, r1, #0
    lsrs r1, r5, #1
    blo musicTrackPitch
    ldrb r1, [r7, #18]
    ldrb r2, [r7, #19]
    ldrb r4, [r7, #24]
    muls r1, r2, r1
    lsrs r2, r1, #5
    cmp r4, #1
    bne musicTrackPan
    movs r3, #22
    ldrsb r1, [r7, r3]
    adds r2, r1, r2
musicTrackPan:
    movs r3, #20
    ldrsb r1, [r7, r3]
    lsls r1, r1, #1
    movs r3, #21
    ldrsb r3, [r7, r3]
    adds r1, r1, r3
    cmp r4, #2
    bne musicTrackPanLimit
    movs r3, #22
    ldrsb r3, [r7, r3]
    adds r1, r3, r1
musicTrackPanLimit:
    movs r3, #128
    cmn r1, r3
    bge musicTrackPanUpper
    negs r1, r3
    b musicTrackRightGain
musicTrackPanUpper:
    cmp r1, #127
    ble musicTrackRightGain
    movs r1, #127
musicTrackRightGain:
    adds r3, r1, #7
    adds r3, #121
    muls r3, r2, r3
    lsrs r3, r3, #8
    lsls r3, r3, #24
    lsrs r3, r3, #24
    cmp r3, #255
    bls musicTrackStoreRight
    movs r3, #255
musicTrackStoreRight:
    strb r3, [r7, #16]
    movs r3, #127
    subs r1, r3, r1
    muls r1, r2, r1
    lsrs r1, r1, #8
    lsls r1, r1, #24
    lsrs r1, r1, #24
    cmp r1, #255
    bls musicTrackStoreLeft
    movs r1, #255
musicTrackStoreLeft:
    strb r1, [r7, #17]
musicTrackPitch:
    lsrs r1, r5, #3
    blo musicTrackExtension
    movs r3, #14
    ldrsb r1, [r7, r3]
    ldrb r2, [r7, #15]
    muls r1, r2, r1
    lsls r1, r1, #2
    movs r3, #12
    ldrsb r2, [r7, r3]
    lsls r2, r2, #2
    adds r1, r1, r2
    movs r3, #10
    ldrsb r2, [r7, r3]
    lsls r2, r2, #8
    adds r1, r1, r2
    movs r3, #11
    ldrsb r2, [r7, r3]
    lsls r2, r2, #8
    adds r1, r1, r2
    ldrb r2, [r7, #13]
    adds r1, r1, r2
    ldrb r2, [r7, #24]
    cmp r2, #0
    bne musicTrackStorePitch
    movs r3, #22
    ldrsb r2, [r7, r3]
    lsls r2, r2, #4
    adds r1, r2, r1
musicTrackStorePitch:
    asrs r2, r1, #8
    strb r2, [r7, #8]
    strb r1, [r7, #9]
musicTrackExtension:
    ldr r2, =0x03007FC0
    adds r1, r7, #0
    ldr r2, [r2, #48]
    ldr r2, [r2, #60]
    bl musicCallR2
    ldrb r0, [r7]
    movs r3, #5
    bics r0, r3
    strb r0, [r7]
    pop {r4, r5, r7}
    pop {r3}
    bx r3
.balign 4, 0
.ltorg

.thumb_func
musicClearPlayer:
    mov r12, r4
    movs r1, #0
    movs r2, #0
    movs r3, #0
    movs r4, #0
    stmia r0!, {r1-r4}
    stmia r0!, {r1-r4}
    stmia r0!, {r1-r4}
    stmia r0!, {r1-r4}
    mov r4, r12
    bx lr

.thumb_func
musicUnlinkVoice:
    ldr r3, [r0, #44]
    cmp r3, #0
    beq musicUnlinkReturn
    ldr r1, [r0, #52]
    ldr r2, [r0, #48]
    cmp r2, #0
    beq musicUnlinkHead
    str r1, [r2, #52]
    b musicUnlinkNext
musicUnlinkHead:
    str r1, [r3, #32]
musicUnlinkNext:
    cmp r1, #0
    beq musicUnlinkOwner
    str r2, [r1, #48]
musicUnlinkOwner:
    movs r1, #0
    str r1, [r0, #44]
musicUnlinkReturn:
    bx lr

.thumb_func
musicStopTrack:
    push {r4-r6, lr}
    adds r5, r1, #0
    ldrb r1, [r5]
    movs r0, #128
    tst r0, r1
    beq musicStopTrackReturn
    ldr r4, [r5, #32]
    cmp r4, #0
    beq musicStopTrackClear
    movs r6, #0
musicStopVoice:
    ldrb r0, [r4]
    cmp r0, #0
    beq musicStopVoiceNext
    ldrb r0, [r4, #1]
    movs r3, #7
    ands r0, r3
    beq musicStopVoiceClear
    ldr r3, =0x03007FF0
    ldr r3, [r3]
    ldr r3, [r3, #44]
    bl soundFrameCall
musicStopVoiceClear:
    strb r6, [r4]
    str r6, [r4, #44]
musicStopVoiceNext:
    ldr r4, [r4, #52]
    cmp r4, #0
    bne musicStopVoice
musicStopTrackClear:
    str r4, [r5, #32]
musicStopTrackReturn:
    pop {r4-r6}
    pop {r0}
    bx r0
.thumb_func
musicCallR2:
    bx r2
.balign 4, 0
.ltorg
.arm
