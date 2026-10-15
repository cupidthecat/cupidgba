@ MIDI conversion interpolates adjacent fixed-point semitone factors.
.org 0x18D8
.thumb
.thumb_func
MidiKey2Freq:
    push {r4-r7, lr}
    lsls r2, r2, #24
    adds r7, r0, #0
    cmp r1, #178
    ble musicKeyReady
    ldr r2, =0xFF000000
    movs r1, #178
musicKeyReady:
    ldr r0, =musicPitchIndices
    ldrb r3, [r0, r1]
    lsls r4, r3, #28
    lsrs r4, r4, #28
    lsls r4, r4, #2
    adds r5, r0, #4
    adds r5, #176
    ldr r4, [r5, r4]
    lsrs r6, r3, #4
    lsrs r4, r4, r6
    adds r0, r0, r1
    ldrb r0, [r0, #1]
    lsls r1, r0, #28
    lsrs r1, r1, #28
    lsls r1, r1, #2
    ldr r1, [r5, r1]
    lsrs r0, r0, #4
    lsrs r1, r1, r0
    subs r0, r1, r4
    adds r1, r2, #0
    bl musicMultiplyHigh
    adds r1, r4, r0
    ldr r0, [r7, #4]
    bl musicMultiplyHigh
    pop {r4-r7}
    pop {r3}
    bx r3
.balign 4, 0
.ltorg

.include "src/gba/bios/music-player.s"

.org 0x1DB4
.thumb_func
musicMultiplyHigh:
    adr r2, musicMultiplyARM
    bx r2
.balign 4, 0
.arm
musicMultiplyARM:
    umull r2, r3, r0, r1
    adds r0, r3, #0
    bx lr

.include "src/gba/bios/sound-mixer.s"
.include "src/gba/bios/music-sequencer.s"
.include "src/gba/bios/sound-driver.s"
.include "src/gba/bios/sound-init.s"
.include "src/gba/bios/music-commands.s"

.org 0x3104
.include "src/gba/bios/music-pitch.inc"
