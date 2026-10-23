# Generated CPU corpus

Five pinned cartridges provide 10,000 records each: ARM data processing,
ARM mixed operations, Thumb data processing, Thumb mixed operations, and
mixed instruction states. This is arithmetic and load/store qualification.
It does not measure instruction timing, exception entry or peripheral phase.
The original program excludes multiply carry from its flag comparisons;
passing it does not qualify that flag.

Fetch the immutable inputs and their permission notice separately from source:

```sh
python3 tests/roms/check_cpu_corpus.py --fetch --directory .local-research/cpu-corpus
cmake -S . -B build -DCUPID_CPU_CORPUS_DIR="$PWD/.local-research/cpu-corpus"
cmake --build build
ctest --test-dir build -R 'cpu-corpus' --output-on-failure
```

The manifest pins the source revision, each complete file's SHA-256 and length,
and its instruction sites. Fetching rejects an existing file with different
bytes. Inputs are downloaded only on request; no cartridges or BIOS files are
included in the source tree. `CUPID_TEST_BIOS` adds the same five checks through
ordinary external firmware boot.

## Observations

The observer checks the actual issued instruction, mode and order at every
record load, all three original result comparisons, each following conditional
failure branch and the final loop. Every record address and descending counter
must match. All five words loaded for a record must match the immutable table.
Both operands at each result comparison are checked against that table, so
matching corruption of the result and expected register cannot pass.

Each cartridge requires 10,000 complete record loads, 30,000 original CMPs,
30,000 original BNEs and an actually issued terminal. Missing or repeated sites,
wrong opcodes, failed comparisons and taken failure branches are rejected.
The observer does not modify CPU state or advance emulated time. A complete
EWRAM image is retained by the launcher during validation.

`runner.cpu-corpus-observer` tests the host protocol with a complete synthetic
transcript and sixteen invalid transcripts. These include incorrect modes,
addresses, counters, loaded data, opcodes, results, expected registers and
branch flags; skipped/repeated sites; missing and premature terminals; and a
wrong terminal opcode. Every call must leave the complete CPU object unchanged.
These protocol vectors complement the original cartridges' CPU execution.

`runner.cpu-corpus-inputs` checks length, digest, instruction sites and the
record count with an owned validation fixture. It also checks read-only and
unrecognized existing files, nonregular paths, and rejection before the runner
starts. It performs no downloads and does not alter any original cartridge.

## Bounds and scope

Each complete cartridge keeps a 1,500-frame, 150-million-step and 90-second
process limit, with a 95-second CTest deadline. The frame allowance covers the
original counter drawing and external firmware startup. An earlier private
600-frame diagnostic expired partway through every cartridge without a
comparison failure. Those outcomes remain incomplete; the complete corpus
profile preserves every original instruction, expected word and case count.
It does not change any pre-existing timing cartridge's guards.

Hosted full profiles run all 50,000 records. The unoptimized native selection
also runs the observer protocol checks. Complete unoptimized cartridge runs
remain separate from that selection. A process timeout is reported as
incomplete, and is never converted into a passing result.

Passing this corpus establishes its generated instruction results and flags
within the stated mask. Wider CPU/bus behavior, the retained strict timing
failures and complete emulator accuracy remain under
[issue 1](https://github.com/cupidthecat/cupidgba/issues/1).
