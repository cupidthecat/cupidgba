# BIOS execution

An external BIOS runs through the ARM/Thumb CPU and memory bus. Without one,
CupidGBA uses its embedded replacement image and C helpers for services that
do not yet run in firmware. The replacement assembly and embedded bytes
are checked together; see [rebuilding the BIOS](../development/rebuilding-bios.md).

Halt, IntrWait and VBlankIntrWait execute the replacement assembly through
the normal CPU pipeline. IntrWait takes a discard-old-flags argument in R0
and an interrupt mask in R1. With R0 clear, flags already recorded in the BIOS
mirror can satisfy the wait immediately. With R0 set, the routine clears
the selected old flags and waits for a new matching request. It consumes
matching flags on return and preserves unrelated flags. VBlankIntrWait
sets the arguments to discard an old VBlank flag and wait for a new one.

The flag checker temporarily disables IME, reads and consumes a matching
request, then enables IME. IntrWait returns that matching request in R0 and
clears R3. It preserves R1, R2, R4 and the other caller registers checked
by the regressions. Calls from IRQ mode retain the caller's banked stack
and saved exception state.

The application's IRQ handler records requests in the BIOS mirror at
03007FF8. Waking HALT from an enabled request does not itself satisfy
IntrWait: the routine checks that mirror against its mask and sleeps again
if the requested flags are absent. Native regressions execute both ARM and
Thumb calls, check preserved registers and stack state, and exercise an
unrelated interrupt followed by a matching one.

The local count-up suite passes 936/936 checks with both BIOS paths, including
the divided clocks, and both pass 90/90 IRQ checks. The same CPU wait
regressions also run against a local external BIOS when one is configured.
This establishes the tested wait and timer interactions; it does not prove
complete BIOS parity. STOP clock suspension and eligible external wake sources
have native regressions; precise wake latency and the remaining SWI routines
need separate hardware validation. See [serial and GPIO](serial/normal-and-gpio.md).

## Math and memory transfers

SWIs 06 through 0C execute through the CPU pipeline. Div, DivArm, Sqrt, ArcTan
and CpuFastSet use ARM instructions; ArcTan2 and CpuSet use Thumb instructions
and enter ARM helpers through BX. Their internal clocks, loads/stores and
pipeline refills participate in normal prefetch, DMA, timer and IRQ execution.
The shared dispatcher preserves the caller's status, register banks and saved
registers. Its return address also supplies CpuSet and ArcTan2's R3 value.

Division returns a signed quotient in R0, remainder in R1 and the quotient's
unsigned magnitude in R3. INT32_MIN divided by -1 retains its wrapped low word.
A zero denominator returns a sign-based quotient for numerators 0, 1 and -1;
larger magnitudes remain in the scale loop while scheduled events continue.
Sqrt treats its input as an unsigned word. The angle routines retain each
multiply's low word and signed fixed-point shift. ArcTan2 uses signed quadrant
comparisons, including wrapped negation at INT32_MIN, and preserves full return
values rather than truncating them to a halfword.

Copy controls use a 21-bit count and a fill flag. CpuSet selects word or
halfword transfers; CpuFastSet always transfers complete eight-word groups.
Zero sizes and source ranges entering the protected low region return before
reading data. Loads and stores use the CPU's alignment behavior, including
rotated unaligned scalar loads. Forward overlap follows the transfer grouping:
CpuSet reads one unit before each write, while CpuFastSet reads eight words
before publishing a block. A timer IRQ can interrupt the routine, and DMA can
change a source word before a later CPU load observes it.

Native regressions cover signed boundaries, square-root limits, angle axes,
quadrants and overflow, copy/fill counts and alignment, source guards, overlap,
caller state, protected BIOS reads after return and selected firmware timings.
A copy interaction check observes partial progress, changes a future source
word with DMA and runs a timer IRQ before return, on both BIOS paths.

The unchanged local math suite passes 615/615 register observations on both
paths. Both also pass the complete 2020/2020 CPU/bus timing matrix, including
its division, square-root, angle and copy cases. The observers reject partial
counts and logged failures. These results cover the listed calls and inputs;
affine, sound-driver, multiboot, STOP and other services still need separate
execution and timing work.

## Data utilities and sound bias

SWIs 10 through 19 also execute replacement instructions through the CPU and
memory bus. They cover bit unpacking, LZ77, Huffman, run-length and differential
data, and the sound-bias ramp. Their memory transfers and internal work allow
timers, IRQs, DMA and audio to advance during the call. See
[data services](bios-data-services.md) for output widths, caller state,
qualification cases and remaining limits.
