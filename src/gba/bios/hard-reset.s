@ SPDX-License-Identifier: GPL-3.0-or-later
@ Warm reboot masks requests, resets the BIOS work banks and starts firmware.
.arm
.balign 4, 0
HardReset:
    mov r0, #0xDF
    msr cpsr_fc, r0
    mov r4, #0x04000000
    strb r4, [r4, #0x208]
    bl softResetBanks
    adr r0, hardResetIRQ
    str r0, [sp, #0xFC]
    ldr r0, =hardResetStartup
    mov lr, pc
    bx r0
    b SoftReset

@ Default interrupt service accumulates the BIOS flags and acknowledges IF.
hardResetIRQ:
    ldr r1, [r0, #0x200]
    and r1, r1, r1, lsr #16
    add r3, r0, #0x200
    ldrh r2, [r0, #-8]
    orr r2, r2, r1
    strh r2, [r0, #-8]
    strh r1, [r3, #2]
    bx lr
.ltorg

@ The built-in startup keeps its fast-entry policy. Memory/device resets and
@ the bias ramp still execute through the CPU; no animation time is invented.
.thumb
.thumb_func
hardResetStartup:
    push {r4-r7, lr}
    movs r0, #0xFF
    bl RegisterRamReset
    ldr r1, =0x04000300
    movs r0, #1
    strb r0, [r1]
    bl SoundBias
    movs r0, #0xFF
    bl RegisterRamReset
    pop {r4-r7}
    pop {r3}
    bx r3
.arm
.balign 4, 0
.ltorg
