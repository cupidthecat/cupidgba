/* Owned JOY peer driver. Entry registers select the receive/send/control
 * registers, an EWRAM result buffer, and the first expected receive word.
 * Each complete round checks the input, records it, sends its complement,
 * acknowledges receive/IRQ, and executes the firmware HALT service. */
.syntax unified
.arch armv4t
.section .text
.arm
    mov r3, #0
arm_receive:
    ldrh r0, [r6]
    tst r0, #2
    beq arm_receive
    ldr r0, [r4]
    cmp r0, r2
    bne arm_fail
    str r0, [r7], #4
    mvn r0, r0
    str r0, [r5]
    ldr r1, =0x01010101
    add r2, r2, r1
    mov r0, #0x42
    strh r0, [r6]
    mov r0, #0x80
    ldr r1, =0x04000202
    strh r0, [r1]
    add r3, r3, #1
    str r3, [r7, #0x7C]
    swi 0x020000
    mov r0, #0x44
    strh r0, [r6]
    cmp r3, #32
    blo arm_receive
    mov r12, #0
    b arm_terminal
arm_fail:
    mov r12, #1
    b arm_terminal
    .ltorg
.org 0xFC
arm_terminal:
    b arm_terminal
.thumb
.org 0x100
    movs r3, #0
thumb_receive:
    ldrh r0, [r6]
    movs r1, #2
    tst r0, r1
    beq thumb_receive
    ldr r0, [r4]
    cmp r0, r2
    bne thumb_fail
    str r0, [r7]
    adds r7, #4
    mvns r0, r0
    str r0, [r5]
    ldr r1, =0x01010101
    adds r2, r2, r1
    movs r0, #0x42
    strh r0, [r6]
    movs r0, #0x80
    ldr r1, =0x04000202
    strh r0, [r1]
    adds r3, #1
    str r3, [r7, #0x7C]
    swi 2
    movs r0, #0x44
    strh r0, [r6]
    cmp r3, #32
    blo thumb_receive
    movs r0, #0
    mov r12, r0
    b thumb_terminal
thumb_fail:
    movs r0, #1
    mov r12, r0
    b thumb_terminal
    .ltorg
.org 0x1FC
thumb_terminal:
    b thumb_terminal
    .short 0
