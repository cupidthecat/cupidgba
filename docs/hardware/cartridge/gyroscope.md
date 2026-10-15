# Cartridge gyroscope

The serial gyroscope board uses GPIO0 for conversion start, GPIO1 for the
serial clock and GPIO2 for serial data. GPIO3 remains the motor output.
The `RZWJ`, `RZWE` and `RZWP` cartridge headers select both devices. This
selects the board wiring and does not change game execution.

A conversion captures the current twelve-bit input. The response has four
zero prefix bits, then twelve data bits in most-significant-bit order.
The first bit is available before the first falling clock edge. Falling
edges advance the response; reads, rising edges and repeated clock levels
do not advance it. Further clock edges output zero after the complete
response. Input changes affect the next conversion and do not replace an
in-flight snapshot. Asserting start again restarts the response. The
current model also restarts it on GPIO updates while start remains high.

The normal driver reads a bit before lowering its clock, then raises the
clock before reading the next bit. GPIO directions select driven signals
and input data through the ordinary cartridge access path. Console reset,
device configuration and cartridge replacement clear the serial response;
the external input survives. Disabling the gyroscope leaves the board's
motor available. The input is independent of raw saved bytes and RTC state.

## Input

`--gyro auto|on|off` controls device selection in both runners. The default
is `auto`. `--gyro-sample 0..4095` selects the deterministic converter input.
It defaults to 1728 (`0x6c0`), a nominal stationary example. These values
are raw converter levels, not calibrated angular velocity. Increasing
them represents clockwise rotation; decreasing them represents
counterclockwise rotation.

In the SDL2 frontend, F7 lowers input by 128, F8 raises it by 128 and F9
restores the nominal value. Adjustments clamp at 0 and 4095. Host motion
sensors and their calibration are not connected by this input path.
The core exposes `cupid_cartridge_set_gyro_sample` for deterministic live
input. Values outside the twelve-bit range fail without replacing the
previous input or response.

## Validation and limits

Native tests read all 4,096 complete frames, reject out-of-range input,
check retained snapshots, restart, repeated levels, zero overrun, the three
detected boards, all ROM windows, halfword/word accesses, motor ownership,
reset, disable and replacement. DMA reads retain the data at their original
strobe when a later serial edge is dispatched in the same scheduler call,
including clock wrap. Platform checks cover strict parsing and live keys.

Generated ARM and Thumb cartridges each test seventeen converter values,
including every individual data bit and both endpoints. Each performs 256
complete 32-bit observations: four prefix bits, twelve data bits and sixteen
overrun bits. The observer requires all comparisons, the complete RAM record
and the passing terminal. A disabled-device control must fail every
comparison. The owned inputs use explicit guest entry and do not qualify
BIOS boot.

Analog calibration, conversion latency, propagation, held-start electrical
behavior and clocks faster than the documented driver remain unqualified.
The model adds no speculative wire delay. It does not model the separate
memory-mapped X/Y tilt sensor. Delivery is tracked in #74; full cartridge
acceptance remains open in #1.
