@ @file      power-control.s
@ @author    cupidthecat
@ @brief     Firmware sleep entry, timed control writes and SWI return.
@ @date      2026-11-16
@
@ Sleep entrypoints share the timed byte strobe and ordinary SWI return.
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
.org 0x1800
.arm
Stop:
    mov r2, #0x80
CustomHalt:
    mov r12, #0x04000000
    strb r2, [r12, #0x301]
    bx lr
