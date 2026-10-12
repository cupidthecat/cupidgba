# BIOS execution

An external BIOS runs through the ARM/Thumb CPU and memory bus. Without one,
CupidGBA uses its embedded replacement image and C implementations of the
supported software interrupts. The replacement assembly and embedded bytes
are checked together; see [rebuilding the BIOS](../development/rebuilding-bios.md).

Halt, IntrWait and VBlankIntrWait execute the replacement assembly through
the normal CPU pipeline. IntrWait takes a discard-old-flags argument in R0
and an interrupt mask in R1. With R0 clear, flags already recorded in the BIOS
mirror can satisfy the wait immediately. With R0 set, the routine clears
the selected old flags and waits for a new matching request. It consumes
matching flags on return and preserves unrelated flags. VBlankIntrWait
sets the arguments to discard an old VBlank flag and wait for a new one.

The application's IRQ handler records requests in the BIOS mirror at
03007FF8. Waking HALT from an enabled request does not itself satisfy
IntrWait: the routine checks that mirror against its mask and sleeps again
if the requested flags are absent. Native regressions execute both ARM and
Thumb calls, check preserved registers and stack state, and exercise an
unrelated interrupt followed by a matching one.

The replacement wait path still differs in timing from the external BIOS.
The local count-up suite passes 828/936 checks with the replacement and
936/936 with the external image; both paths pass 90/90 IRQ checks. The
remaining 108 divided-clock failures need instruction and bus timing
comparisons. Functional wait results do not establish BIOS cycle parity.
STOP behavior and the remaining SWI routines need separate validation.
