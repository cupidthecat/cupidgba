# BIOS register and memory reset

RegisterRamReset (`01`) executes Thumb firmware through the CPU and memory
bus. It always selects forced blank, then resets the groups selected by the
low eight bits of R0. Upper bits do not select additional groups.

| Bit | Group |
| --- | --- |
| 0 | All 256 KiB of EWRAM |
| 1 | IWRAM below `03007E00` |
| 2 | Palette RAM |
| 3 | VRAM |
| 4 | OAM |
| 5 | Serial and expansion-port registers |
| 6 | Sound registers, wave RAM and FIFO writes |
| 7 | Display, interrupt, DMA, timer and other register writes |

The register groups run before the memory clears. Memory clears proceed in
EWRAM, VRAM, OAM, palette and IWRAM order. Each selected group fills complete
eight-word blocks through the ordinary ARM transfer routine. The upper 512
IWRAM bytes remain outside the bulk clear; the running service and exception
handlers still use their normal stack frames there.

Register reset uses the actual access widths and order. The serial path leaves
its destination pointer at the start when its flag is absent. A following byte
write then sets the low byte of SIODATA32 to seven, including an empty reset
mask. Selecting the flag clears the serial ranges, enters GPIO mode and
acknowledges the joy-bus control flags.

Sound reset disables and temporarily enables the master control, resets the
mixing controls and FIFOs, selects both wave banks for their zero writes, and
leaves the master disabled. It clears the upper sound-bias bits while retaining
the low ten bits accepted by the register. A caller's bias level therefore
survives this group.

The other-register group clears interrupt enables and pending requests, writes
wait controls, resets display controls, DMA and timer registers, and installs
the four unit affine coefficients. Timer counters continue until the control
write actually stops them. Subsequent memory clearing still consumes clocks
while those stopped counters retain their values.

## Caller state and shared transfers

The service preserves R2, R4 through R12, the caller's stack and return register,
and the caller's status and instruction mode. R1 returns `03000000`, or
`03007E00` when IWRAM was selected. R3 returns the shared SWI return address,
`00000170`. R0 returns the local zero-word address after a selected fill. An
other-register-only reset returns `00000100`; an empty low-byte mask retains
the input R0 value.

The Thumb reset helper enters CpuFastSet's ARM instructions directly through
BX. That routine carries the last block in R2 through R9. Its internal R2
result supplies later reset writes, while the public SWI dispatcher restores
the caller's R2. A nonempty public CpuFastSet copy returns the second word of
its last block in R3; filling returns the fill word. An empty call preserves
R3. The native copy regressions check those results on both BIOS paths.

The shared routine resides beside the reset bridge. Unused space preserves
the existing internal stall entry and the later firmware offsets checked by
the source verifier. The space is not executed by the services.

## Verification

`tests/bios/test_register_reset.c` checks all 256 masks with ARM and Thumb
callers, cartridge wait controls, ignored upper bits and safe callers in both
work-RAM regions. It checks complete memory and retained regions, wave banks,
FIFO state, readable device registers, caller state, timers and elapsed clocks.
The comparison excludes only the scratch bytes used by the running exception
and service stacks; their bounds, preserved surroundings and restored stack
pointers remain checked.

Two interrupted clears require timer and DMA IRQ handlers during EWRAM
clearing. DMA writes a marked word ahead of the clear, and the later CPU fill
must overwrite it. Samples every 64 clocks record partial progress and that
transition. With `CUPID_TEST_BIOS` configured, all 650 observations run against
a local external BIOS and compare complete results and sampled chronology.
That BIOS is not distributed. CTest separates the mask groups and interactions
with a ten-second deadline for each.

The default ARM caller in cartridge ROM takes 207 clocks through exception
return for an empty mask and 434,447 for an EWRAM clear. Caller bus settings,
selected groups and concurrent devices change these durations; they are
fixture values rather than fixed service costs.

These checks cover the listed inputs on the current CPU, bus and peripheral
model. They do not provide new physical hardware captures or establish all
device power transitions and concurrent peripheral combinations. Soft reset,
other BIOS calls and full emulator accuracy remain separate work.
