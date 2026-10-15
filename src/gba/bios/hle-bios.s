#define nop andeq r0, r0
.syntax unified

@ Fixed ARM offsets are covered by tests/bios/verify_hle.py.
.equ SWI_TABLE_OFFSET, 0x198
.equ STALL_CALL_OFFSET, 0x3FC
.equ UTILITY_SOURCE_OFFSET, 0xBA4

.text
hle_start:

b resetBase
b undefBase
b swiBase
b pabtBase
b dabtBase
nop
b irqBase
b fiqBase

.word 0 @ Padding for back-compat
.word 0
.word 0
.word 0
.word 0
.word 0
.word 0
.word 0

@ The shared SWI return address is also CpuSet's visible R3 clobber.
.org 0x140
swiBase:
stmfd  sp!, {r11, r12, lr}
ldrb   r12, [lr, #-2]
mov    r11, #SWI_TABLE_OFFSET
ldr    r12, [r11, r12, lsl #2]
mrs    r11, spsr
stmfd  sp!, {r11}
and    r11, #0x80
orr    r11, #0x1F
msr    cpsr, r11
stmfd  sp!, {r2, lr}
mov    lr, pc
bx     r12
ldmfd  sp!, {r2, lr}
mov    r12, #0xD3
msr    cpsr, r12
ldmfd  sp!, {r11}
msr    spsr, r11
ldmfd  sp!, {r11, r12, lr}
movs   pc, lr
.word 0
.word 0xE3A02004

.word 0 @ Padding for back-compat

swiTable:
.word SoftReset               @ 0x00
.word RegisterRamReset        @ 0x01
.word Halt                    @ 0x02
.word Stop                    @ 0x03
.word IntrWait                @ 0x04
.word VBlankIntrWait          @ 0x05
.word Div                     @ 0x06
.word DivArm                  @ 0x07
.word Sqrt                    @ 0x08
.word ArcTan                  @ 0x09
.word ArcTan2                 @ 0x0A
.word CpuSet                  @ 0x0B
.word CpuFastSet              @ 0x0C
.word GetBiosChecksum         @ 0x0D
.word BgAffineSet             @ 0x0E
.word ObjAffineSet            @ 0x0F
.word BitUnPack               @ 0x10
.word Lz77UnCompWram          @ 0x11
.word Lz77UnCompVram          @ 0x12
.word HuffmanUnComp           @ 0x13
.word RlUnCompWram            @ 0x14
.word RlUnCompVram            @ 0x15
.word Diff8BitUnFilterWram    @ 0x16
.word Diff8BitUnFilterVram    @ 0x17
.word Diff16BitUnFilter       @ 0x18
.word SoundBias               @ 0x19
.word SoundDriverInit         @ 0x1A
.word SoundDriverMode         @ 0x1B
.word SoundDriverMain         @ 0x1C
.word SoundDriverVsync        @ 0x1D
.word SoundChannelClear       @ 0x1E
.word MidiKey2Freq            @ 0x1F
.word MusicPlayerOpen         @ 0x20
.word MusicPlayerStart        @ 0x21
.word MusicPlayerStop         @ 0x22
.word MusicPlayerContinue     @ 0x23
.word MusicPlayerFadeOut      @ 0x24
.word MultiBoot               @ 0x25
.word HardReset               @ 0x26
.word CustomHalt              @ 0x27
.word SoundDriverVsyncOff     @ 0x28
.word SoundDriverVsyncOn      @ 0x29
.word SoundDriverGetJumpList  @ 0x2A

.ltorg
.word 0 @ Padding for back-compat

irqBase:
stmfd  sp!, {r0-r3, r12, lr}
mov    r0, #0x04000000
add    lr, pc, #0
ldr    pc, [r0, #-4]
ldmfd  sp!, {r0-r3, r12, lr}
subs   pc, lr, #4
.word 0
.word 0xE55EC002

@ Padding for back compat
subs   pc, lr, #4
.word 0
.word 0x03A0E004

@ Unimplemented
MultiBoot:

NopCall:
bx lr

Halt:
mov    r2, #0
b      CustomHalt
@ Retain the fixed internal entry offsets beyond this non-fallthrough branch.
.space 8, 0

VBlankIntrWait:
mov    r0, #1
mov    r1, #1
IntrWait:
stmfd  sp!, {r4, lr}
mov    r3, #0
mov    r4, #1
@ Discard a previously recorded matching request when R0 is nonzero.
cmp    r0, #0
blne   waitFlags
@ Recheck the mirror after each wake-up. An unrelated IRQ keeps the wait active.
0:
strb   r3, [r12, #0x301]
bl     waitFlags
beq    0b
ldmfd  sp!, {r4, lr}
bx     lr

waitFlags:
mov    r12, #0x04000000
strb   r3, [r12, #0x208]
ldrh   r2, [r12, #-8]
ands   r0, r2, r1
bicne  r2, r1
strhne r2, [r12, #-8]
strb   r4, [r12, #0x208]
bx     lr

@ CpuSet uses halfword instruction fetches and word or halfword data phases.
.thumb
.thumb_func
CpuSet:
push {r4, r5, lr}
lsls r4, r2, #11
lsrs r4, r4, #9
bl copySourceThumb
beq copySetDone
movs r5, #0
lsrs r3, r2, #27
bcc copySetHalf
adds r5, r1, r4
lsrs r3, r2, #25
bcc copySetWords
ldmia r0!, {r3}
copyFillWords:
cmp r1, r5
bge copySetDone
stmia r1!, {r3}
b copyFillWords
copySetWords:
cmp r1, r5
bge copySetDone
ldmia r0!, {r3}
stmia r1!, {r3}
b copySetWords
copySetHalf:
lsrs r4, r4, #1
lsrs r3, r2, #25
bcc copySetHalves
ldrh r3, [r0]
copyFillHalves:
cmp r5, r4
bge copySetDone
strh r3, [r1, r5]
adds r5, #2
b copyFillHalves
copySetHalves:
cmp r5, r4
bge copySetDone
ldrh r3, [r0, r5]
strh r3, [r1, r5]
adds r5, #2
b copySetHalves
copySetDone:
pop {r4, r5}
pop {r3}
bx r3
copySourceThumb:
adr r3, copySourceValid
mov r12, r4
bx r3
.ltorg

.balign 4, 0
.arm

@ Keep the fixed stall entry and later firmware offsets in place.
.space 0x68, 0

@ Z rejects a zero size or a source range entering the protected low region.
copySourceValid:
cmp    r12, #0
beq    0f
bic    r12, r12, #0xFE000000
add    r12, r0, r12
tst    r0, #0x0E000000
tstne  r12, #0x0E000000
0:
bx     lr

.ltorg
.org STALL_CALL_OFFSET

StallCall:
subs r11, #4
bhi StallCall
bx lr

resetBase:
mov lr, #0x8000003
ldrb r1, [lr], #-3
cmp r1, #0
movne r1, #0
bne 1f
ldr lr, =0x20000C0
ldr r1, [lr]
cmp r1, #0
mov r1, #0
bne 1f
sub lr, #0xC0
1:
bx lr
.word 0
.word 0xE129F000

.ltorg

@ Signed division uses unsigned magnitudes and a shift/subtract quotient.
@ A zero denominator with a magnitude above one remains in the scale loop.
.balign 4, 0
.thumb
.thumb_func
divideFromThumb:
adr r3, Div
bx r3
.arm
DivArm:
mov r3, r0
mov r0, r1
mov r1, r3
Div:
ands r3, r1, #0x80000000
rsbmi r1, r1, #0
eors r12, r3, r0, asr #32
rsbcs r0, r0, #0
movs r2, r1
0:
cmp r2, r0, lsr #1
movls r2, r2, lsl #1
blo 0b
1:
cmp r0, r2
adc r3, r3, r3
subcs r0, r0, r2
cmp r2, r1
movne r2, r2, lsr #1
bne 1b
mov r1, r0
mov r0, r3
movs r12, r12, lsl #1
rsbcs r0, r0, #0
rsbmi r1, r1, #0
bx lr

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

@ Each multiply retains the low word before the signed fixed-point shift.
.balign 4, 0
.thumb
.thumb_func
angleFromThumb:
adr r3, ArcTan
bx r3
.arm
ArcTan:
mul r1, r0, r0
mov r1, r1, asr #14
rsb r1, r1, #0
mov r3, #0xA9
mul r3, r1, r3
mov r3, r3, asr #14
add r3, r3, #0x390
mul r3, r1, r3
mov r3, r3, asr #14
add r3, r3, #0x900
add r3, r3, #0x1C
mul r3, r1, r3
mov r3, r3, asr #14
add r3, r3, #0xF00
add r3, r3, #0xB6
mul r3, r1, r3
mov r3, r3, asr #14
add r3, r3, #0x1600
add r3, r3, #0xAA
mul r3, r1, r3
mov r3, r3, asr #14
add r3, r3, #0x2000
add r3, r3, #0x81
mul r3, r1, r3
mov r3, r3, asr #14
add r3, r3, #0x3600
add r3, r3, #0x51
mul r3, r1, r3
mov r3, r3, asr #14
add r3, r3, #0xA200
add r3, r3, #0xF9
mul r0, r3, r0
mov r0, r0, asr #16
bx lr

@ Signed quadrant comparisons retain the low-word overflow behavior.
.balign 4, 0
.thumb
.thumb_func
ArcTan2:
push {r4-r7, lr}
cmp r1, #0
bne atanHaveY
cmp r0, #0
blt atanNegativeX
movs r0, #0
b atanDone
atanNegativeX:
movs r0, #0x80
lsls r0, r0, #8
b atanDone
atanHaveY:
cmp r0, #0
bne atanQuadrant
cmp r1, #0
blt atanNegativeY
movs r0, #0x40
lsls r0, r0, #8
b atanDone
atanNegativeY:
movs r0, #0xC0
lsls r0, r0, #8
b atanDone
atanQuadrant:
movs r2, r0
movs r3, r1
lsls r3, r3, #14
lsls r2, r2, #14
rsbs r4, r0, #0
rsbs r5, r1, #0
movs r6, #0x40
lsls r6, r6, #8
lsls r7, r6, #1
cmp r1, #0
blt atanLower
cmp r0, #0
blt atanUpperLeft
cmp r0, r1
blt atanUpperXY
movs r1, r0
movs r0, r3
bl divideFromThumb
bl angleFromThumb
b atanDone
atanUpperLeft:
cmp r4, r1
blt atanUpperXY
atanLeftYX:
movs r1, r0
movs r0, r3
bl divideFromThumb
bl angleFromThumb
adds r0, r0, r7
b atanDone
atanUpperXY:
movs r0, r2
bl divideFromThumb
bl angleFromThumb
subs r0, r6, r0
b atanDone
atanLower:
cmp r0, #0
bgt atanLowerRight
cmp r4, r5
bgt atanLeftYX
atanLowerXY:
movs r0, r2
bl divideFromThumb
bl angleFromThumb
adds r6, r6, r7
subs r0, r6, r0
b atanDone
atanLowerRight:
cmp r0, r5
blt atanLowerXY
movs r1, r0
movs r0, r3
bl divideFromThumb
bl angleFromThumb
adds r7, r7, r7
adds r0, r7, r0
atanDone:
pop {r4-r7}
pop {r3}
bx r3

.balign 4, 0
.arm

undefBase:
pabtBase:
dabtBase:
fiqBase:
ldr sp, =0x03007FF0
stmdb sp!, {r12, lr}
mrs r12, spsr
mrs lr, cpsr
stmdb sp!, {r12, lr}
mov lr, #0x08000000
ldrb r12, [lr, #0x9C]
cmp r12, #0xA5
bne 1f
ldrb r12, [lr, #0xB4]
tst r12, #0x80
adr lr, 1f
ldrne pc, =0x09FE2000
ldreq pc, =0x09FFC000
1:
ldr sp, =0x03007FF0
ldr r12, [sp, #-0x10]
msr spsr, r12
ldmdb sp!, {r12, lr}
subs pc, lr, #4
.word 0
.word 0x03A0E004

.ltorg

.include "src/gba/bios/data-services.s"
.include "src/gba/bios/affine-services.s"
.include "src/gba/bios/register-reset.s"

.include "src/gba/bios/copy-fast.s"
.include "src/gba/bios/soft-reset.s"
.include "src/gba/bios/power-control.s"
.include "src/gba/bios/checksum.s"
.include "src/gba/bios/music-frequency.s"
.include "src/gba/bios/hard-reset.s"
.include "src/gba/bios/music-jump-table.s"
.include "src/gba/bios/music-notes.s"

@ The final word records the embedded image's additive identity.
.org 0x3FFC
.include "src/gba/bios/checksum-identity.inc"
