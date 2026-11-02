# FIFO timer comparison conflict

The original FIFO base cartridge still reads `00800062h` at its first
`00800068h` timer comparison. The separately rebuilt source base retains the
same failure. Both BIOS paths fail that issued comparison and complete their
failure terminal. Their inputs, expected operands and execution limits remain
unchanged. This work records the software phase boundary; it does not resolve
the comparison or establish new physical timing.

## Observed phase sequence

The following trace uses the bundled base cartridge and the timed core. Clock
zero is timer 1's accepted counting epoch. The listed deadlines and read values
agree with both BIOS paths. In the built-in path, this epoch is absolute clock
871; in the external-BIOS path, it is 75998864.

| Relative clock | Observed operation |
| ---: | --- |
| 9 | Timer 0 counting epoch after its enable write |
| 41 | Timer 0 overflows with an empty FIFO queue |
| 44 | FIFO request reaches DMA1 |
| 46 | DMA head finishes and selects its first ROM source access |
| 55, 62, 69 | First three DMA destination writes append FIFO words |
| 73 | Another timer overflow loads the queued playback stage during DMA |
| 76 | Fourth DMA destination write appends its FIFO word |
| 77 | First burst releases the bus |
| 98 | CPU timer load at `08000208h` returns `00800062h` |
| 105 | Next timer overflow requests another refill |
| 108 | Request activation and the second CPU timer read share a clock |
| 108 | Load at `0800020Ch` returns `0080006Ch`, then releases bus ownership |
| 109 | Next DMA head finishes |
| 118, 125, 132, 139 | Next burst publishes its four destination writes |
| 140 | Next burst releases the bus |

The first burst reads ROM for eight clocks, then six clocks per remaining
word. Each destination strobe takes one clock. The programmed fixed source
address remains separate from the cartridge's sequential address counter.

The second timer read has already sampled its value when releasing the CPU
bus allows the pending DMA head to proceed. Its returned value therefore
precedes that burst's destination writes. At the first comparison, R0 holds
`00800062h`, R1 holds the unchanged expected `00800068h`, and R7 holds
`0080006Ch`. The original later R7 comparison is not reached on this failure
path; observing R7 here does not qualify that unissued comparison as a pass.

## Observation checks and limits

Private callback wrappers forward the original timer and DMA callbacks and
record their state before and after dispatch. A CPU load wrapper records the
returned timer value. Instrumented and ordinary runs match general registers,
prefetch words, EWRAM, IWRAM, I/O and elapsed clocks at the completed first
comparison prefix on each BIOS path. This checks those fields and that prefix,
not complete emulator-state equivalence or full-game behavior.

The two boot paths retain different inactive DMA-address and timer history
before the new transfer. The complete trace is not claimed to be identical
across boot paths. The table covers the listed active-transfer deadlines.

Moving the later refill before the second load would change its returned
timer value, but that alone would not explain the first load's six-clock
difference. Any correction must retain the full FIFO selection, the original
CPU/DMA/IRQ comparisons, timer behavior, queue/storage semantics and bus
ownership rules. A timing adjustment chosen only to satisfy this value is
insufficient evidence.

The current complete FIFO selection remains 16/20. The four base/source-base
failures stay enabled when configured. See [FIFO behavior](fifo-power.md),
[DMA phases](../dma.md) and [CPU/DMA/IRQ interactions](../cpu/dma-irq-interactions.md).
