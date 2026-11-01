# Display startup comparison checks

Five pinned cartridges check the initial scanline, first VBlank, IRQ delivery
with polling or HALT, and the normal object texture deadline. They run with
both the built-in firmware and an authorized external BIOS.

The runner requires these original comparisons at their issued instructions:

| Case | Compared operands | Result register |
| --- | --- | --- |
| `startup-line` | r1 = 7Fh, r0 = 44h | r12 |
| `startup-vblank` | r0 = 4Eh, r1 = 9Eh | r12 |
| `startup-irq` | r7 = 1, r0 = 35h, r1 = 9Eh | r7 |
| `startup-halt` | r7 = 1, r0 = 31h, r1 = 9Eh | r7 |
| `object-tail-normal` | r2 = 8Eh | r12 |

These values belong to the programs' original boot, wait-state and timer setup.
They are not universal IRQ latencies or expectations for arbitrary launch
points. The normal object case complements the existing
[HBlank-free texture-tail check](object-tail.md).

## Comparison and result guards

`tests/roms/protocols/comparisons.py` verifies the complete input digest, encoded
comparisons, the nine-instruction VBlank wait and the original result-register
selection and restoration of r0-r12. It accepts the comparison forms used here: Thumb immediate CMP,
and unconditional ARM CMP with an unrotated eight-bit immediate. PC operands
and other encodings need their own protocol.

The two IRQ cartridges report through r7. Checking r12 instead would inspect
the wrong result. The verifier reads the encoded result selection and requires
the same register at both the pre-display checkpoint and completed terminal.
It also rejects missing, reordered or conflicting observations and result
records. A zero result without the original comparison visits is insufficient.

The shared checks cover 36 comparisons across nineteen serial, HALT and display
cartridges. Owned ARM and Thumb streams exercise r12 and r7 result transport.
Each has a correct path, a wrong-zero measurement and a skipped comparison.
All twelve streams can reach a passing-looking zero terminal without the
comparison guard; the guarded run accepts only the four correct paths. The
original cartridge bytes and expected values are unchanged.

## Running the checks

```sh
python3 tests/roms/check_display_probe.py --fetch --directory .local-research/test-roms
cmake -S . -B build \
  -DCUPID_DISPLAY_PROBE_DIR="$PWD/.local-research/test-roms"
ctest --test-dir build -R '^rom[.]display-' --output-on-failure
```

Set `CUPID_TEST_BIOS` to an authorized local firmware file to add the external
runs. Both paths keep the 360-frame, 150-million-step and 90-second process
limits, with a 100-second CTest deadline. Linux and Windows CI fetch and run
all five built-in-firmware cases and preserve the input license notice.

Fetching verifies existing files before downloading, stages all requested
inputs before publication, and atomically publishes each new file. Verified
cached inputs need no network request. Changed files, incomplete downloads,
invalid result programs and paths outside the input directory are rejected.
`runner.display-probe-inputs` checks those failure paths and staging cleanup.
The shared `runner.immediate-comparison-protocol` and
`runner.immediate-comparison-execution` groups cover input contracts and actual
false-zero instruction streams.

These checks qualify their specified startup and access boundaries. They do
not establish every mid-line display transition or physical pixel-pipeline
phase. Wider rendering requirements remain separate.
