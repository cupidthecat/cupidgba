# Display bus waits and CPU IRQ sampling

Palette, VRAM and OAM accesses can wait for the pixel pipeline to release their
memory port. Peripheral clocks, IF acceptance and HALT availability continue
through the wait. The CPU IRQ synchronizer completes one clock at the request's
final ready edge. A palette or VRAM word uses two bridge beats; the first beat
can transfer data while the CPU word request remains held.

The bus owner supplies a readiness predicate to the scheduled CPU clock-release
event. At each candidate edge the pixel pipeline determines whether the port
is free. An unavailable edge retains the wait. A free intermediate beat lets
the bridge transfer its halfword, and the final beat releases the CPU clock.
The ready event precedes register acceptance on the same edge, matching the
ordering of ordinary stretched CPU transfers. A captured IRQ stage survives
the hold; an input pulse that disappears before sampling does not enter it.

Three `gba.display-irq-*` groups check 3,456 owned transactions. They cover
halfword and word reads, writes and opcode fetches in all three display regions,
sixteen port phases, four clock epochs, short pulses, persistent levels and
already captured stages. Waveforms sample every system clock through the wait
and the following two CPU clocks. Full load values, partial and complete store
payloads, elapsed clocks and internal-cycle accounting remain checked. Of these
transactions, 1,692 wait for more than one clock. The preserved composition
without dynamic CPU readiness fails all 1,692 waveform checks; the corrected
owner preserves the same data and physical-clock fingerprint.

The unchanged external-firmware HALT/OAM cartridge also passes both original
comparisons, its pre-display result and terminal when the CPU and pixel models
are combined. This establishes that covered interaction. The separate DMA/HALT
timer comparison, built-in entry phase, remaining bus cases and broader pixel
pipeline acceptance remain unfinished. The CPU and PPU drafts remain separate
delivery steps; this PPU draft depends on the CPU branch.
