# Multiply flags

ARM7TDMI multiplication exposes a deterministic carry flag even though the
architecture leaves that flag undefined. CupidGBA models this output for ARM
MULS, MLAS, UMULLS, UMLALS, SMULLS and SMLALS, and Thumb MUL. The flag depends on
the multiplier's internal adder state; it is not an arithmetic overflow flag
and does not inherit the previous barrel-shifter output. The
[hardware investigation](https://bmchtech.github.io/post/multiply/) describes
the Booth recoding, carry-save stages and early termination that produce it.

N and Z still describe the completed result, V retains its incoming value,
and ARM multiplication without S preserves all four arithmetic flags.
Signed long multiplication sign-extends both operands. Short multiplication
uses the same signed early termination rule, while unsigned long
multiplication only terminates when the remaining multiplier bytes are zero.

The carry helper follows the two finite-width ports of the multiplier's
carry-save adder. Each internal cycle processes four two-bit Booth digits.
Discarded result bits leave the ports, upper accumulator bits enter their
sign-correction positions, and the final port supplies the exposed carry.
The helper only computes that flag; ordinary C arithmetic computes the
32-bit or 64-bit result. All intermediate port operations use unsigned
64-bit values and explicit 33-bit input masking.

The CPU callers use the operands already sampled by their existing register
phases. MLA and long accumulation retain their original accumulator words
when result registers overlap an input. Thumb MUL samples the old destination
as its multiplier. Flag calculation adds no emulated clocks, bus accesses,
scheduler events or extra register reads.

## Regression coverage

The six `arm.multiply-*` groups have ten-second deadlines.

| Group | Coverage |
| --- | --- |
| hardware | 72 independently pinned long-multiply hardware observations, repeated with all 16 incoming NZCV values and both prior shifter carries |
| arithmetic | All six ARM forms without S, exact results, preserved registers and flags, and one through four multiplier cycles |
| short | 32 fixed model comparison vectors across all six flag-setting ARM forms and Thumb MUL, including accumulator effects and signed extension |
| aliases | Existing source, destination and accumulator overlaps, using the values sampled before result writes |
| conditions | Every ARM predicate and NZCV input, passing and skipped flag-setting forms, following ARM carry predicates and a Thumb carry branch |
| chronology | Paired S/non-S executions across both clock wraps and a large clock epoch, with exact idle timings, DMA completion data and DMA IRQ publication |

The hardware group uses the six operand values `0`, `1`, `FFFFFFFF`, `7FFFFFFF`,
`80000000` and `80000001` in every ordered pair, for signed and unsigned
long multiplication. These are existing recorded observations; no new
physical hardware captures were made for this change. The 32 additional
vectors are independent software-model comparisons, not extra hardware
observations. A private comparison also checks the shipping carry helper
against an independently structured model over 1,800,000 deterministic
cases spanning early termination and accumulation.

The chronology group deliberately compares timing independently of C, so
the preceding implementation remains a useful timing control. The arithmetic
group likewise retains the behavior of non-S instructions. The hardware,
short, alias and condition groups reject the preceding carry implementation.

Existing skipped behavior for PC destination or accumulator encodings is
covered explicitly. These tests do not qualify unpredictable PC encodings,
equal long-result destinations, other ARM implementations, or every possible
operand pair. Wider CPU pipeline and bus accuracy remain separate work.
