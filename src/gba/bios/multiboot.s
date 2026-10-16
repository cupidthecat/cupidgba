@ SPDX-License-Identifier: GPL-3.0-or-later
@ Download words pass through serial registers, a rolling key and a bitwise CRC.
.org 0x2D20
.thumb
.thumb_func
MultiBoot:
    push {r1, r3-r7, lr}
    movs r3, #0xDF
    adr r2, multibootMaskRequests
    bl multibootCallR2
    adds r7, r0, #0
    movs r4, #255
    bl utilitySourceThumb
    beq multibootRejectEarly
    lsrs r4, r7, #20
    movs r3, #0xE8
    ands r3, r4
    cmp r3, #0x20
    bne multibootRejectEarly
    movs r4, #0
    cmp r1, #1
    beq multibootModeReady
    cmp r1, #2
    bgt multibootRejectEarly
    ldr r4, =0xC3871089
    orrs r4, r1
multibootModeReady:
    strh r4, [r7, #58]
    ldr r0, [r7, #32]
    str r0, [r7, #16]
    ldr r4, [r7, #36]
    subs r4, r4, r0
    ldr r3, =0x0003FFF8
    ands r4, r3
    str r4, [r7, #12]
    bl utilitySourceThumb
    beq multibootRejectEarly
    ldr r4, =0x040000B0
    ldrh r0, [r4, #10]
    ldrh r2, [r4, #22]
    orrs r0, r2
    ldrh r2, [r4, #34]
    orrs r0, r2
    ldrh r2, [r4, #46]
    orrs r0, r2
    lsrs r0, r0, #16
    bcs multibootRejectEarly
    ldr r6, =0x04000120
    ldrb r0, [r7, #30]
    lsls r0, r0, #28
    lsrs r0, r0, #29
    ldrh r1, [r6]
    ldrh r2, [r7, #58]
    cmp r2, #0
    beq multibootClientsReady
    lsls r0, r0, #31
    lsrs r0, r0, #31
    ldrb r1, [r7, #20]
multibootClientsReady:
    strb r0, [r7, #8]
    strb r1, [r7, #4]
    ldr r3, =0xC3871089
    lsrs r3, r3, #16
    ldr r1, =0xC37B
    cmp r2, #0
    ldr r4, =0x43202F2F
    bne multibootCipherReady
    ldr r1, =0xA517
    ldr r3, =0x0003FFF8
    ldr r4, =0x6465646F
multibootCipherReady:
    strh r1, [r7, #62]
    strh r3, [r7, #56]
    str r4, [r7, #64]
    ldr r1, [r7, #24]
    str r1, [r7]
    ldrb r1, [r7, #28]
    strb r1, [r7]
    adds r4, r6, #0
multibootCheckClient:
    lsrs r0, r0, #1
    bcc multibootNextClient
    ldrb r1, [r4, #3]
    cmp r1, #0x73
    bne multibootRejectEarly
multibootAdvanceClient:
    adds r4, #2
    b multibootCheckClient
multibootRejectEarly:
    b multibootReject
multibootNextClient:
    bne multibootAdvanceClient
    ldr r5, [r7, #12]
    lsrs r0, r5, #2
    subs r0, #0x34
    ldr r1, [r7, #16]
    adds r1, r1, r5
    str r1, [r7, #12]
    ldr r1, =0x0003FFF8
multibootInitialDelay:
    subs r1, #1
    bne multibootInitialDelay
    bl multibootExchange
    ldrh r1, [r6, #2]
    strb r1, [r7, #5]
    ldrh r1, [r6, #4]
    ldrh r2, [r6, #6]
    ldrh r3, [r7, #58]
    cmp r3, #0
    beq multibootReplySeed
    movs r1, #255
    movs r2, #255
multibootReplySeed:
    strb r1, [r7, #6]
    strb r2, [r7, #7]
    movs r4, #2
    mov r12, r4
    ldr r3, [r7, #16]
multibootPrepareWord:
    ldr r1, [r7, #32]
    subs r1, r1, r3
    lsrs r1, r1, #2
    ldrh r0, [r7, #60]
    bcs multibootWaitWord
    ldr r2, [r3]
    ldrh r0, [r7, #62]
    ldrh r5, [r7, #56]
    bl multibootCRC
    strh r5, [r7, #56]
    ldr r1, [r7]
    ldr r0, =0x6F646573
    muls r1, r0, r1
    adds r1, #1
    str r1, [r7]
    ldr r0, [r3]
    eors r0, r1
    ldr r1, [r7, #32]
    subs r2, r3, r1
    ldr r1, =0x020000C0
    adds r2, r2, r1
    negs r1, r2
    ldr r2, [r7, #64]
    eors r1, r2
    eors r0, r1
    lsrs r2, r0, #16
    strh r2, [r7, #60]
    ldr r6, =0x04000120
multibootWaitWord:
    bl multibootWait
    ldr r1, [r7, #32]
    cmp r1, r3
    beq multibootSendWord
    mov lr, r4
    subs r4, r3, r1
    subs r4, #2
    ldrh r1, [r7, #58]
    cmp r1, #0
    beq multibootExpectedOffset
    subs r4, #2
multibootExpectedOffset:
    ldr r1, =0x020000C0
    adds r4, r4, r1
multibootCompareClients:
    ldrb r2, [r7, #8]
    adds r5, r6, #0
multibootCompareSlot:
    lsrs r2, r2, #1
    bcc multibootCompareNext
    ldrh r1, [r5, #2]
    eors r1, r4
    lsls r1, r1, #16
    bne multibootReject
multibootCompareAdvance:
    adds r5, #2
    b multibootCompareSlot
multibootCompareNext:
    bne multibootCompareAdvance
    mov r4, lr
    cmp r2, r12
    bne multibootSendWord
    movs r0, #0
    b multibootFinish
multibootSendWord:
    bl multibootSend
    cmp r4, #0
    beq multibootHandshake
    adds r3, #2
    ldrh r1, [r7, #58]
    cmp r1, #0
    beq multibootWordAdvanced
    adds r3, #2
multibootWordAdvanced:
    cmp r4, #2
    bne multibootRequestFinal
    ldr r1, [r7, #12]
    cmp r1, r3
    bne multibootPrepareWord
multibootRequestFinal:
    movs r0, #0x65
    subs r4, #1
    b multibootWaitWord
multibootHandshake:
    movs r4, #1
    bl multibootWait
    ldrb r2, [r7, #8]
    adds r3, r6, #0
multibootHandshakeSlot:
    lsrs r2, r2, #1
    bcc multibootHandshakeNext
    ldrh r1, [r3, #2]
    cmp r1, #0x75
    beq multibootHandshakeAdvance
    cmp r0, #0x65
    bne multibootReject
    cmp r1, #0x74
    bne multibootReject
    movs r4, #0
multibootHandshakeAdvance:
    adds r3, #2
    b multibootHandshakeSlot
multibootHandshakeNext:
    bne multibootHandshakeAdvance
    cmp r0, #0x66
    beq multibootFinalCRC
    cmp r4, #0
    beq multibootRepeatFinal
    movs r0, #0x66
multibootRepeatFinal:
    bl multibootSend
    b multibootHandshake
multibootFinalCRC:
    cmp r4, #0
    beq multibootReject
    ldrh r0, [r7, #62]
    ldrh r5, [r7, #56]
    ldr r2, [r7, #4]
    bl multibootCRC
    ldr r6, =0x04000120
    adds r0, r5, #0
    bl multibootExchange
    movs r1, #0
    mov r12, r1
    adds r4, r0, #0
    b multibootCompareClients
multibootReject:
    movs r0, #1
multibootFinish:
    str r0, [r7, #56]
    str r0, [r7, #60]
    str r0, [r7, #64]
    adds r1, r7, #0
    adds r1, #20
multibootClearScratch:
    stmia r7!, {r0}
    cmp r1, r7
    bne multibootClearScratch
    pop {r1, r3-r7}
    pop {r2}
multibootCallR2:
    bx r2

@ CRC polynomial and initial value depend on the selected transfer width.
.thumb_func
multibootCRC:
    movs r6, #32
multibootCRCBit:
    adds r1, r5, #0
    eors r1, r2
    lsrs r5, r5, #1
    lsrs r1, r1, #1
    bcc multibootCRCShift
    eors r5, r0
multibootCRCShift:
    lsrs r2, r2, #1
    subs r6, #1
    bne multibootCRCBit
    bx lr

.thumb_func
multibootExchange:
    push {lr}
    bl multibootSend
    pop {r1}
    mov lr, r1
multibootWait:
    ldrh r1, [r6, #8]
    lsrs r1, r1, #8
    bcs multibootWait
    bx lr

.thumb_func
multibootSend:
    movs r1, #150
multibootWordDelay:
    subs r1, #1
    bne multibootWordDelay
    str r0, [r6]
    strh r0, [r6, #10]
    ldrh r1, [r7, #58]
    cmp r1, #0
    bne multibootStart
    ldr r1, =0xA1C12083
multibootStart:
    strh r1, [r6, #8]
    bx lr
.balign 4, 0
.arm
multibootMaskRequests:
    msr cpsr_fc, r3
    bx lr
.balign 4, 0
.ltorg
