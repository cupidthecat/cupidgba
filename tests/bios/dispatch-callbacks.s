@ @file dispatch-callbacks.s
@ @brief Owned callback instruction templates for ABI and peripheral tests.
.syntax unified
.arch armv4t
.arm
.text
.macro snapshot
stmia r0, {r0-r14}
mrs r1, cpsr
str r1, [r0, #60]
mov r1, #0
ldr r1, [r1]
str r1, [r0, #64]
.endm
.macro result
mov r0, #0x11
mov r1, #0x22
mov r3, #0x33
.endm
.macro finish
mov r2, #0x66
mov r11, #0x77
mov r12, #0x88
bx lr
.endm
.global plain, nested, dma, timer, handler
plain:
snapshot
result
finish
.org 0x100
nested:
snapshot
stmfd sp!, {lr}
mov r0, #17
mov r1, #3
swi 0x060000
ldr r12, =0x02001300
stmia r12, {r0-r3}
ldmfd sp!, {lr}
finish
.ltorg
.org 0x200
dma:
snapshot
ldr r1, =0x040000D4
ldr r2, =0x08000100
str r2, [r1]
ldr r2, =0x02001400
str r2, [r1, #4]
ldr r2, =0x84000004
str r2, [r1, #8]
result
finish
.ltorg
.org 0x300
timer:
snapshot
ldr r1, =0x04000100
ldr r2, =0x00C0FFF0
str r2, [r1]
mov r3, #64
1:
subs r3, r3, #1
bne 1b
result
finish
.ltorg
.org 0x400
handler:
ldr r0, =0x04000100
mov r1, #0
strh r1, [r0, #2]
mov r0, #0x04000000
add r0, r0, #0x200
mov r1, #8
strh r1, [r0, #2]
ldr r0, =0x02001200
ldr r1, [r0]
add r1, r1, #1
str r1, [r0]
bx lr
.ltorg
