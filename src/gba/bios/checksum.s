@ Read every firmware word, preserving the service's mask and address results.
.org 0x1840
.arm
GetBiosChecksum:
    mov r0, #0
    mov r3, #0
checksumWord:
    mov r12, #0xDF
    ldmia r3!, {r2}
    msr cpsr, r12
    add r0, r0, r2
    movs r1, r3, lsr #14
    beq checksumWord
    bx lr
