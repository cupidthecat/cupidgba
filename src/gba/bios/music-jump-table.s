@ @file      music-jump-table.s
@ @author    cupidthecat
@ @brief     Music command dispatch targets and guarded table layout.
@ @date      2026-11-16
@
@ SPDX-License-Identifier: GPL-3.0-or-later
@ The source guard admits this table and the remaining BIOS data range.
@
@ @copyright Copyright (c) 2026 cupidthecat
@
@ This file is part of CupidGBA.
@
@ CupidGBA is free software: you can redistribute it and/or modify
@ it under the terms of the GNU General Public License as published by
@ the Free Software Foundation, either version 3 of the License, or
@ (at your option) any later version.
@
@ CupidGBA is distributed in the hope that it will be useful,
@ but WITHOUT ANY WARRANTY; without even the implied warranty of
@ MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
@ GNU General Public License for more details.
@
@ You should have received a copy of the GNU General Public License
@ along with CupidGBA.  If not, see <https://gnu.org>.
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
