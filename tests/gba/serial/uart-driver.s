/* Owned UART driver. r0/r1 select control/data, r2/r3 are source/destination,
 * r4/r5 hold the byte counts and r6 selects the UART configuration. Each
 * iteration sends when capacity permits and drains an available receive byte.
 * With neither operation ready, HALT waits for the real serial interrupt. */
.syntax unified
.arch armv4t
.section .text
.arm
    mov r7, #0
    strh r7, [r0, #12]
    mov r7, #0x3000
    strh r7, [r0]
    strh r6, [r0]
    mov r6, #0x80
    ldr r7, =0x04000200
    strh r6, [r7]
arm_loop:
    mov r6, #0x80
    ldr r7, =0x04000202
    strh r6, [r7]
    cmp r4, #0
    beq arm_receive
    ldrh r7, [r0]
    tst r7, #0x10
    bne arm_receive
    ldrb r7, [r2], #1
    strb r7, [r1]
    sub r4, r4, #1
arm_receive:
    ldrh r7, [r0]
    tst r7, #0x20
    beq arm_data
    cmp r4, #0
    beq arm_wait
    tst r7, #0x10
    beq arm_loop
arm_wait:
    /* A partial final FIFO cannot raise the receive-full interrupt. */
    cmp r5, #4
    blt arm_loop
    swi 0x020000
    b arm_loop
arm_data:
    ldrb r7, [r1]
    strb r7, [r3], #1
    subs r5, r5, #1
    bne arm_loop
    mov r12, #0
    b arm_terminal
    .ltorg
.org 0xFC
arm_terminal:
    b arm_terminal
.org 0x100
.thumb
    movs r7, #0
    strh r7, [r0, #12]
    movs r7, #0x30
    lsls r7, r7, #8
    strh r7, [r0]
    strh r6, [r0]
    movs r6, #0x80
    ldr r7, =0x04000200
    strh r6, [r7]
thumb_loop:
    movs r6, #0x80
    ldr r7, =0x04000202
    strh r6, [r7]
    cmp r4, #0
    beq thumb_receive
    ldrh r7, [r0]
    lsls r7, r7, #27
    bmi thumb_receive
    ldrb r7, [r2]
    strb r7, [r1]
    adds r2, #1
    subs r4, #1
thumb_receive:
    ldrh r7, [r0]
    lsls r7, r7, #26
    bpl thumb_data
    cmp r4, #0
    beq thumb_wait
    ldrh r7, [r0]
    lsls r7, r7, #27
    bpl thumb_loop
thumb_wait:
    cmp r5, #4
    blt thumb_loop
    swi 2
    b thumb_loop
thumb_data:
    ldrb r7, [r1]
    strb r7, [r3]
    adds r3, #1
    subs r5, #1
    bne thumb_loop
    movs r7, #0
    mov r12, r7
    b thumb_terminal
    .ltorg
.org 0x1FC
thumb_terminal:
    b thumb_terminal
.org 0x200
