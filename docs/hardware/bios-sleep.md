# BIOS sleep entrypoints

CupidGBA executes HALT (SWI 02), STOP (SWI 03) and CustomHalt (SWI 27) through
its normal exception, CPU and bus paths. The firmware saves the caller state,
selects system mode, writes one byte to HALTCNT and returns through the shared
SWI dispatcher after waking. HALT writes zero; STOP writes 80h. CustomHalt
uses R2's low byte, with bit 7 selecting STOP. The upper bits do not affect
the byte strobe, and the caller's original R2 is restored on return.

HALT suspends instruction execution while scheduled peripherals and DMA keep
running. Enabled requests wake it even with IME clear or the CPU IRQ mask set.
IRQ delivery still requires IME and the CPU mask to permit it. A ready enabled
request can satisfy the sleep without waiting for another interrupt.

STOP freezes the system clock and retains peripheral deadlines. Enabled
keypad or cartridge requests can wake it; a serial GPIO input request can
also wake it in GPIO mode. Internal timer requests, disabled external sources
and ordinary serial transfers do not wake STOP. After an eligible request,
the firmware completes the ordinary SWI return.

## Regression coverage

`gba.bios-power` executes 1,716 ARM/Thumb cases, including all 256 control
bytes and four upper-bit aliases, system and IRQ callers, saved status and
registers, both CPU IRQ masks, IME, ready requests and clock wrap. It checks
the entry boundary separately from the actual unhalted return. For the
isolated IWRAM caller, these entry clocks are required:

| Call | Entry clocks |
| --- | ---: |
| HALT | 38 |
| STOP | 34 |
| CustomHalt selecting HALT | 34 |
| CustomHalt selecting STOP | 33 |

The same isolated cases require 24 clocks from a new eligible request to
the restored caller. Ready requests and delivered IRQs have separate checks.
These counts include the emulated exception and firmware instructions; they
are not measurements of physical power or oscillator recovery.

STOP cases preserve the timer, scanline, audio sample position, video/audio
deadlines and CPU event breakpoint during repeated step and frame calls.
They reject ineligible requests before testing the enabled external wake.
HALT cases require timer overflow, advancing video and audio, and an HBlank
DMA write before return. Other cases transfer data with DMA during sleep
entry or while halted, record IRQ handler requests, and check an overdue
callback and deadlines across two clock-wrap boundaries.

Configuring `CUPID_TEST_BIOS` adds `gba.bios-power-external`. It executes the
same corpus with a local external BIOS and compares entry/return clocks,
all registers, status, protected BIOS reads, timer/IF state, handler results
and DMA callback lateness for the paired interaction cases. The BIOS input
is not distributed.

The regressions qualify these firmware entrypoints within CupidGBA's CPU,
bus and sleep model. Physical oscillator restart, electrical interrupt
edges and full-system sleep/wake timing remain unmeasured.
