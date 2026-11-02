# Card-reader cartridge

The reader board has a serial camera, a scanline buffer and flash-backed
calibration. The documented cartridge map describes a 320-bit scanline, block
intensity thresholds, image extrema, camera control and Game Pak interrupts.
The original Japanese board has 64 KiB flash; the later boards have 128 KiB.
See the [cartridge register description](https://www.akkit.org/info/gbatek.htm#gbacartereaderioports).

## Input and ownership

Both frontends accept `--card-reader auto|on|off` and repeated `--card FILE`
options. Automatic selection recognizes `PEAJ`, `PSAJ` and `PSAE`; explicit save
hardware choices still take precedence. Up to sixteen strips can be queued.
In local linked play, card files feed the first console.

Accepted single-strip lengths are 1872/2912 bytes for raw blocks, 1344/2112 for
full data fragments and 1308/2076 for reduced headers. The converter reconstructs
address bars, synchronization marks, interleaving and error information as
needed. It also accepts 3520/5456-byte packed dot images and canonical monochrome
BMP strips, 639 or 989 pixels wide and 44 pixels high, with either row order or
palette polarity. Fixed legacy lengths select their corresponding format.
Conventional multi-strip RAW and full-header BIN files use their first header
to select strip geometry. JPEG photographs and arbitrary-resolution optical
recognition are not implemented.

All requested files are checked and converted before guest execution or output
creation. Missing, nonregular, malformed and oversized files fail; the complete
queue remains unchanged on failure. Converted data belongs to the attached
board. Camera reset retains queued input. Board removal, replacement and core
destruction release it. No card, ROM or firmware bytes are distributed.

## Selected camera model

The core retains the camera's MSB-first serial commands, ninth acknowledgement
clocks, repeated starts, register mirrors and sequential indexing. The scanner
uses normalized monochrome dots, three camera samples per dot, and an
acknowledged scanline sequence. The reader's power and clock-enable bits and
the console's WAITCNT PHI output gate that sequence. An unpowered camera ignores
serial commands; the electrical response of its input pins is not modeled.
Per-block thresholds operate on monochrome brightness endpoints; the extrema
registers retain the brightest block samples and darkest image sample.

The scanline flag publishes with the logical line. The selected interrupt delay
is twice the LED-duration count, capped at `0x4000` console clocks at the nominal
PHI setting. The selected counter divides PHI by two, so the slower WAITCNT
outputs scale that delay by two or four. Clock gating pauses the counter;
rate changes retain fractional exposure progress. An owned scheduler event
delivers the Game Pak signal through the existing interrupt pipeline. Turning
off power, scan or LED control, or resetting the camera, cancels an undelivered
signal. An already accepted controller signal remains accepted.

This is a source-informed software model. Continuous camera acquisition,
missed-line/overrun behavior, analogue exposure and physical polarity remain
unqualified. Camera type 2 and unused serial-register combination mirrors are
not implemented. The normalized scan sequence and selected clock-divider model
are not physical measurements; electrical edge alignment remains unqualified.

Before its first flash-bus access, a fresh erased sector receives generated
default calibration with a valid checksum. A partly populated sector is preserved
byte for byte. Initialization runs once per attached battery, so a later guest
erase is not silently repaired.
The original battery input file remains unchanged.

## Bus and regression coverage

CPU and DMA word accesses retain separate halfword strobes. Mapped opcode reads
use the board ports; eight tagged prefetch samples retain the values captured
at their original bus clocks. Updating a sensor buffer after a fill cannot
replace an already buffered instruction.

Native groups check serial transactions, register widths, scan flags and IRQs,
complete DMA payloads, queue ownership, calibration preservation, thresholds,
separate word strobes and mutable prefetched input across four clock epochs.
Clock controls cover all PHI settings, fractional progress across rate changes,
both clock gates, paused deadlines and unpowered serial writes. The preceding
model fails the disabled-clock and unpowered-write controls.
Format controls independently reconstruct raw bytes, verify error-information
syndromes and checksums, and reject malformed images without touching output.
Unused fragment padding is deterministic; reduced short strips retain their
short-strip block header.

Owned ARM and Thumb guests configure the camera, scan queued input, copy all
twenty scanline halfwords through DMA3 and compare the complete payload before
publishing a flash result. Two completed frames, full EWRAM and backup bytes,
deterministic repeats, nonblank calibration inputs, SDL2 persistence and rejected
input/output preservation are checked. These are generated guest programs;
retail reader firmware, purchased cards and full linked applications remain
unqualified.
