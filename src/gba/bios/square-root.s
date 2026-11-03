@ @file square-root.s
@ @brief Integer square-root service with retained instruction and fetch words.
.arm
.balign 4, 0

@ Newton iteration uses the same unsigned divider at each trial bound.
Sqrt:
stmfd sp!, {r4}
mov r12, r0
mov r1, #1
0:
cmp r1, r0
movlo r0, r0, lsr #1
movlo r1, r1, lsl #1
blo 0b
1:
mov r0, r12
mov r4, r1
mov r3, #0
mov r2, r1
2:
cmp r2, r0, lsr #1
movls r2, r2, lsl #1
blo 2b
3:
cmp r0, r2
adc r3, r3, r3
subcs r0, r0, r2
cmp r2, r1
movne r2, r2, lsr #1
bne 3b
add r1, r1, r3
movs r1, r1, lsr #1
cmp r1, r4
blo 1b
mov r0, r4
ldmfd sp!, {r4}
bx lr
SqrtEnd:
.if (SqrtEnd - Sqrt) != SQRT_LAYOUT_BYTES
.error "Square-root instruction span changed"
.endif
@ Retain the two fetched words following the return at its former location:
@ the Thumb angle bridge and the first ARM angle instruction.
.word 0x4718A300, 0xE0010090
