@ Utility services run through the ordinary CPU, bus and peripheral scheduler.
.org UTILITY_SOURCE_OFFSET - 8
.thumb
.thumb_func
utilitySourceThumb:
    adr r3, utilitySourceValid
    mov r12, r4
    bx r3
.balign 4, 0
.arm
@ Diff16 returns this address in R3 when no subsequent delta is loaded.
utilitySourceValid:
    cmp r12, #0
    beq utilitySourceReturn
    bic r12, r12, #0xFE000000
    add r12, r0, r12
    tst r0, #0x0E000000
    tstne r12, #0x0E000000
utilitySourceReturn:
    bx lr
.arm
.balign 4
BitUnPack:
    push {r4-r11, lr}
    sub sp, sp, #8
    ldrh r7, [r2]
    movs r12, r7
    bl utilitySourceValid
    beq unpackDone
    ldrb r6, [r2, #2]
    rsb r10, r6, #8
    mov lr, #0
    ldr r11, [r2, #4]
    mov r8, r11, lsr #31
    ldr r11, [r2, #4]
    mov r11, r11, lsl #1
    mov r11, r11, lsr #1
    str r11, [sp]
    ldrb r2, [r2, #3]
    mov r3, #0
unpackByte:
    subs r7, r7, #1
    blt unpackDone
    mov r11, #255
    mov r4, r11, asr r10
    ldrb r9, [r0], #1
    mov r5, #0
unpackField:
    cmp r5, #8
    bge unpackByte
    and r11, r9, r4
    movs r12, r11, lsr r5
    cmpeq r8, #0
    beq unpackPack
    ldr r11, [sp]
    add r12, r12, r11
unpackPack:
    orr lr, lr, r12, lsl r3
    add r3, r3, r2
    cmp r3, #32
    blt unpackNext
    str lr, [r1], #4
    mov lr, #0
    mov r3, #0
unpackNext:
    mov r4, r4, lsl r6
    add r5, r5, r6
    b unpackField
unpackDone:
    add sp, sp, #8
    pop {r4-r11, lr}
    bx lr

@ Tree nodes encode an aligned child pair and separate leaf flags.
.balign 4
HuffmanUnComp:
    push {r4-r11, lr}
    sub sp, sp, #8
    movs r12, #0x02000000
    bl utilitySourceValid
    beq huffmanDone
    add r2, r0, #4
    add r7, r2, #1
    ldrb r10, [r0]
    and r4, r10, #15
    mov r3, #0
    mov lr, #0
    and r10, r4, #7
    add r11, r10, #4
    str r11, [sp]
    ldr r10, [r0]
    mov r12, r10, lsr #8
    ldrb r10, [r2]
    add r10, r10, #1
    add r0, r2, r10, lsl #1
    mov r2, r7
huffmanWord:
    cmp r12, #0
    ble huffmanDone
    mov r5, #32
    ldr r8, [r0], #4
huffmanBit:
    subs r5, r5, #1
    blt huffmanWord
    mov r10, #1
    and r9, r10, r8, lsr #31
    ldrb r6, [r2]
    mov r6, r6, lsl r9
    mov r10, r2, lsr #1
    mov r10, r10, lsl #1
    ldrb r11, [r2]
    and r11, r11, #63
    add r11, r11, #1
    add r10, r10, r11, lsl #1
    add r2, r10, r9
    tst r6, #128
    beq huffmanAdvance
    mov r3, r3, lsr r4
    ldrb r10, [r2]
    rsb r11, r4, #32
    orr r3, r3, r10, lsl r11
    mov r2, r7
    add lr, lr, #1
    ldr r11, [sp]
    cmp lr, r11
    streq r3, [r1], #4
    subeq r12, r12, #4
    moveq lr, #0
huffmanAdvance:
    cmp r12, #0
    movgt r8, r8, lsl #1
    bgt huffmanBit
    b huffmanWord
huffmanDone:
    add sp, sp, #8
    pop {r4-r11, lr}
    bx lr

@ LZ byte output copies from the advancing destination, allowing overlap.
.balign 4
Lz77UnCompWram:
    push {r4-r6, lr}
    ldr r6, [r0], #4
    mov r2, r6, lsr #8
    movs r12, r2
    bl utilitySourceValid
    beq lzByteDone
lzByteFlags:
    cmp r2, #0
    ble lzByteDone
    ldrb lr, [r0], #1
    mov r4, #8
lzByteToken:
    subs r4, r4, #1
    blt lzByteFlags
    tst lr, #128
    bne lzByteWindow
    ldrb r5, [r0], #1
    strb r5, [r1], #1
    sub r2, r2, #1
    b lzByteNext
lzByteWindow:
    ldrb r6, [r0]
    mov r5, #3
    add r3, r5, r6, asr #4
    ldrb r5, [r0], #1
    and r6, r5, #15
    mov r12, r6, lsl #8
    ldrb r5, [r0], #1
    orr r6, r5, r12
    add r12, r6, #1
    sub r2, r2, r3
lzByteCopy:
    ldrb r6, [r1, -r12]
    strb r6, [r1], #1
    subs r3, r3, #1
    bgt lzByteCopy
lzByteNext:
    cmp r2, #0
    movgt lr, lr, lsl #1
    bgt lzByteToken
    b lzByteFlags
lzByteDone:
    pop {r4-r6, lr}
    bx lr

@ Halfword copies read the byte selected from an aligned destination pair.
.balign 4
Lz77UnCompVram:
    push {r4-r10, lr}
    mov r3, #0
    ldr r8, [r0], #4
    mov r10, r8, lsr #8
    mov r2, #0
    movs r12, r10
    bl utilitySourceValid
    beq lzPairDone
lzPairFlags:
    cmp r10, #0
    ble lzPairDone
    ldrb r7, [r0], #1
    mov r6, #8
lzPairToken:
    subs r6, r6, #1
    blt lzPairFlags
    tst r7, #128
    bne lzPairWindow
    ldrb r9, [r0], #1
    orr r3, r3, r9, lsl r2
    sub r10, r10, #1
    eors r2, r2, #8
    strheq r3, [r1], #2
    moveq r3, #0
    b lzPairNext
lzPairWindow:
    ldrb r9, [r0]
    mov r8, #3
    add r5, r8, r9, asr #4
    ldrb r9, [r0], #1
    and r8, r9, #15
    mov r4, r8, lsl #8
    ldrb r9, [r0], #1
    orr r8, r9, r4
    add r4, r8, #1
    rsb r8, r2, #8
    and r9, r4, #1
    eor lr, r8, r9, lsl #3
    sub r10, r10, r5
lzPairCopy:
    eor lr, lr, #8
    rsb r8, r2, #8
    add r8, r4, r8, lsr #3
    mov r8, r8, lsr #1
    mov r8, r8, lsl #1
    ldrh r9, [r1, -r8]
    mov r8, #255
    and r8, r9, r8, lsl lr
    mov r8, r8, lsr lr
    orr r3, r3, r8, lsl r2
    eors r2, r2, #8
    strheq r3, [r1], #2
    moveq r3, #0
    subs r5, r5, #1
    bgt lzPairCopy
lzPairNext:
    cmp r10, #0
    movgt r7, r7, lsl #1
    bgt lzPairToken
    b lzPairFlags
lzPairDone:
    pop {r4-r10, lr}
    bx lr

.thumb
.balign 2
.thumb_func
SoundBias:
    movs r1, #128
    lsls r1, r1, #2
    mov r12, r1
    ldr r3, biasRegister
    ldrh r2, [r3]
    ldr r3, biasRegister
    lsls r1, r2, #22
    lsrs r1, r1, #22
    cmp r0, #0
    beq biasDecrease
    cmp r1, r12
    bge biasDone
    adds r2, #2
    b biasWrite
biasDecrease:
    cmp r1, #0
    ble biasDone
    subs r2, #2
biasWrite:
    strh r2, [r3]
    movs r2, #9
biasWait:
    subs r2, #1
    bne biasWait
    b SoundBias
biasDone:
    bx lr
.balign 4
biasRegister:
    .word 0x04000088
.arm

.thumb
.balign 2
.thumb_func
Diff16BitUnFilter:
    push {r4, lr}
    ldmia r0!, {r4}
    lsrs r4, r4, #8
    bl utilitySourceThumb
    beq delta16Done
    ldrh r2, [r0]
    adds r0, #2
delta16Write:
    strh r2, [r1]
    adds r1, #2
    subs r4, #2
    ble delta16Done
    ldrh r3, [r0]
    adds r0, #2
    adds r2, r2, r3
    b delta16Write
delta16Done:
    pop {r4}
    pop {r2}
    bx r2
.arm

.thumb
.balign 2
.thumb_func
Diff8BitUnFilterWram:
    push {r4, lr}
    ldmia r0!, {r4}
    lsrs r4, r4, #8
    bl utilitySourceThumb
    beq delta8Done
    ldrb r2, [r0]
    adds r0, #1
delta8Write:
    strb r2, [r1]
    adds r1, #1
    subs r4, #1
    ble delta8Done
    ldrb r3, [r0]
    adds r0, #1
    adds r2, r2, r3
    b delta8Write
delta8Done:
    pop {r4}
    pop {r3}
    bx r3
.arm

.thumb
.balign 2
.thumb_func
Diff8BitUnFilterVram:
    push {r4-r7, lr}
    ldmia r0!, {r5}
    lsrs r5, r5, #8
    movs r4, r5
    bl utilitySourceThumb
    beq deltaPairDone
    movs r4, #8
    ldrb r6, [r0]
    adds r0, #1
    movs r7, r6
deltaPairWrite:
    subs r5, #1
    ble deltaPairDone
    ldrb r3, [r0]
    adds r6, r6, r3
    adds r0, #1
    lsls r2, r6, #24
    lsrs r2, r2, #24
    lsls r2, r4
    orrs r7, r2
    movs r3, #8
    eors r4, r3
    bne deltaPairWrite
    strh r7, [r1]
    adds r1, #2
    movs r7, #0
    b deltaPairWrite
deltaPairDone:
    pop {r4-r7}
    pop {r3}
    bx r3
.arm

@ Run descriptors select either literal bytes or one repeated byte.
.thumb
.balign 2
.thumb_func
RlUnCompWram:
    push {r4-r7, lr}
    ldmia r0!, {r7}
    lsrs r7, r7, #8
    movs r4, r7
    bl utilitySourceThumb
    beq runByteDone
runByteHeader:
    cmp r7, #0
    ble runByteDone
    ldrb r4, [r0]
    adds r0, #1
    movs r2, #127
    ands r2, r4
    cmp r4, #128
    bcs runByteRepeated
    adds r2, #1
    subs r7, r7, r2
runByteLiteral:
    ldrb r3, [r0]
    strb r3, [r1]
    adds r0, #1
    adds r1, #1
    subs r2, #1
    bgt runByteLiteral
    b runByteHeader
runByteRepeated:
    adds r2, #3
    subs r7, r7, r2
    ldrb r5, [r0]
    adds r0, #1
runByteRepeatWrite:
    strb r5, [r1]
    adds r1, #1
    subs r2, #1
    bgt runByteRepeatWrite
    b runByteHeader
runByteDone:
    pop {r4-r7}
    pop {r3}
    bx r3
.arm

@ Halfword output holds an odd final byte without committing it to memory.
.thumb
.balign 2
.thumb_func
RlUnCompVram:
    push {r4-r7, lr}
    sub sp, #12
    movs r7, #0
    ldmia r0!, {r5}
    lsrs r5, r5, #8
    movs r4, r5
    bl utilitySourceThumb
    beq runPairDone
    movs r4, #0
runPairHeader:
    cmp r5, #0
    ble runPairDone
    ldrb r3, [r0]
    str r3, [sp]
    adds r0, #1
    ldr r3, [sp]
    movs r2, #127
    ands r2, r3
    ldr r6, [sp]
    cmp r6, #128
    bcs runPairRepeated
    adds r2, #1
    subs r5, r5, r2
runPairLiteral:
    ldrb r6, [r0]
    lsls r6, r4
    orrs r7, r6
    adds r0, #1
    movs r3, #8
    eors r4, r3
    bne runPairLiteralNext
    strh r7, [r1]
    adds r1, #2
    movs r7, #0
runPairLiteralNext:
    subs r2, #1
    bgt runPairLiteral
    b runPairHeader
runPairRepeated:
    adds r2, #3
    subs r5, r5, r2
    ldrb r6, [r0]
    str r6, [sp, #4]
    adds r0, #1
runPairRepeatWrite:
    ldr r6, [sp, #4]
    lsls r6, r4
    orrs r7, r6
    movs r3, #8
    eors r4, r3
    bne runPairRepeatNext
    strh r7, [r1]
    adds r1, #2
    movs r7, #0
runPairRepeatNext:
    subs r2, #1
    bgt runPairRepeatWrite
    b runPairHeader
runPairDone:
    add sp, #12
    pop {r4-r7}
    pop {r3}
    bx r3
.arm
