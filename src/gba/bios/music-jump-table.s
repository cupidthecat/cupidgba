@ SPDX-License-Identifier: GPL-3.0-or-later
@ The source guard admits this table and the remaining BIOS data range.
.org 0x3738
.balign 4, 0
soundInternalJumpTable:
    .word musicCommandFine
    .word musicCommandGoto
    .word musicCommandPattern
    .word musicCommandPatternEnd
    .word musicCommandRepeat
    .rept 4
    .word musicCommandFine
    .endr
    .word musicCommandPriority
    .word musicCommandTempo
    .word musicCommandKeyShift
    .word musicCommandVoice
    .word musicCommandVolume
    .word musicCommandPan
    .word musicCommandBend
    .word musicCommandBendRange
    .word musicCommandLFOSpeed
    .word musicCommandLFODelay
    .word musicCommandModulation
    .word musicCommandModulationType
    .word musicCommandFine
    .word musicCommandFine
    .word musicCommandTune
    .rept 3
    .word musicCommandFine
    .endr
    .word musicCommandPort
    .word musicCommandFine
    .word musicCommandEndTie
    .word soundConfigureRate
    .word musicStopTrack
    .word musicStepFade
    .word musicUpdateTrack
    .word musicUnlinkVoice
    .word musicClearPlayer
