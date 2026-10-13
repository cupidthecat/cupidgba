# BIOS affine conversion

BgAffineSet (`0E`) and ObjAffineSet (`0F`) execute ARM instructions in the
replacement BIOS. Calls from ARM or Thumb enter the shared SWI dispatcher and
return with the caller's status, register banks, stack and saved registers.
Loads, stores and multiplication clocks use the ordinary CPU, memory bus and
peripheral scheduler. A DMA transfer can change an entry before the routine
loads it, and timer and DMA interrupts can run during a call.

## Calculation and output

Both services use only the high byte of the 16-bit angle. The 256 sine values
represent a full turn in signed 1.14 format, truncated toward zero. Cosine uses
the sine value one quarter-turn ahead. Each signed scale product undergoes an
arithmetic right shift by 14 bits. The second coefficient is negated after
that shift. This order matters for products with a fractional remainder.

The four coefficients are:

```text
A = (xScale * cosine) >> 14
B = -((xScale * sine) >> 14)
C = (yScale * sine) >> 14
D = (yScale * cosine) >> 14
```

BgAffineSet reads 20-byte entries. Each contains two signed 32-bit texture
origins, two signed 16-bit display centers, two signed 16-bit scales, the
16-bit angle and a padding halfword. It writes a 16-byte result: four
coefficient halfwords followed by two 32-bit origins. Origin calculations use
the shifted coefficients before truncating their halfword stores:

```text
startX = xOrigin - A * xCenter - B * yCenter
startY = yOrigin - C * xCenter - D * yCenter
```

Origin arithmetic retains its low 32 bits. Matrix stores retain their low 16
bits. The source advances by 20 bytes per entry and the destination by 16.
R3 returns the last entry's full shifted A value, which can be outside the
signed halfword range.

ObjAffineSet reads eight-byte entries containing two signed scales, the angle
and a padding halfword. It stores A, B, C and D with the byte stride in R3
between stores. A stride of two produces consecutive coefficients; a stride
of eight places them among OAM attributes. Zero, odd and negative strides use
the CPU's halfword addressing rules. The source advances by eight bytes per
entry and the destination advances by four strides. R3 remains unchanged.

R2 supplies the signed entry count and is restored by the dispatcher. Zero
and negative counts leave output and both pointers unchanged. An empty
background call also preserves R3. Other caller registers and status are
preserved by both services.

## Verification

`tests/bios/test_affine.c` checks 382 background and 1,102 object observations.
Each service covers every 16-bit angle in 256-entry batches, with varying
signed scales, centers and origins. An independent numeric model checks
complete output buffers, untouched gaps and source contents. Other cases
exercise zero and negative counts, zero/odd/negative object strides, work-RAM
and cartridge sources, cartridge wait controls, work-RAM/VRAM/OAM output,
ARM/Thumb callers, preserved registers, timers and elapsed clocks.

Four interrupted cases sample every output change at scheduled clock
boundaries. Each requires timer and DMA IRQs to be serviced during the call.
DMA overwrites a future source entry; the final conversion must use that
changed entry, and the source check requires exactly the DMA write extent.
With `CUPID_TEST_BIOS` configured, all observations also run against a local
external BIOS and compare caller state, elapsed clocks and the interrupted
output chronology. That input is not distributed.

The signed rounding fixture uses scales 257 and -513 with angle `1980`. Its
coefficients are 210, -147, -296 and -420. The EWRAM-to-IWRAM ARM background
call takes 179 clocks through exception return; the object call with stride
two takes 124. These are fixture durations, not constant costs for a service.

CTest separates background and object cases, including their optional
external comparisons, and retains a ten-second deadline for each. The sine
table has a separate mathematical reproduction check. See
[rebuilding the BIOS](../development/rebuilding-bios.md).

These checks establish the listed calls on the current CPU, bus and
peripheral model. They are reproducible local BIOS comparisons, without new
physical hardware captures. Arbitrary invalid addresses, every overlapping
input/output arrangement and all concurrent peripheral combinations have no
general qualification claim. Other BIOS services and full emulator accuracy
remain separate work.
