/* Owned normal and multiplayer callers. r0 is SIOCNT, r1 is the data port,
 * r2 is output RAM, r3 is the transmitted word, r4 is control and r5 is ID.
 * The normal master waits for the child's low SO before starting its clock.
 * Multiplayer clients set their words before declaring their modes ready. */
.syntax unified
.arch armv4t
.section .text
.arm
    mov r6, #0
    strh r6, [r0, #12]
    orr r6, r4, #8
    strh r6, [r0]
    tst r4, #0x1000
    strne r3, [r1]
    strheq r3, [r1]
    cmp r5, #0
    bne normal_child
normal_ready:
    ldrh r6, [r0]
    tst r6, #4
    bne normal_ready
    orr r6, r4, #0x88
    b normal_start
normal_child:
    orr r6, r4, #0x80
normal_start:
    strh r6, [r0]
normal_wait:
    ldrh r6, [r0]
    tst r6, #0x80
    bne normal_wait
    tst r4, #0x1000
    ldrne r7, [r1]
    ldrbeq r7, [r1]
    str r7, [r2]
    mov r12, #0
    b normal_terminal
.org 0xFC
normal_terminal:
    b normal_terminal
.org 0x100
    strh r3, [r0, #2]
    mov r6, #0
    strh r6, [r0, #12]
    strh r4, [r0]
    cmp r5, #0
    bne multi_child
multi_ready:
    ldrh r6, [r0]
    tst r6, #8
    beq multi_ready
    orr r6, r4, #0x80
    strh r6, [r0]
    b multi_wait
multi_child:
    ldrh r6, [r0]
    tst r6, #0x80
    beq multi_child
multi_wait:
    ldrh r6, [r0]
    tst r6, #0x80
    bne multi_wait
    sub r1, r0, #8
    ldmia r1, {r6, r7}
    stmia r2, {r6, r7}
    mov r12, #0
    b multi_terminal
.org 0x1FC
multi_terminal:
    b multi_terminal
.org 0x200
