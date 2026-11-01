# UART

CupidGBA exchanges UART bytes through the serial pin API. SO carries transmit
data, SI receives data, SD is low while the receiver has capacity, and optional
SC flow control permits a new transmission while low. A frame already shifting
finishes if SC rises. Transmit and receive operate independently.

SIOCNT selects 9,600, 38,400, 57,600 or 115,200 baud, seven or eight data bits,
and optional even or odd parity. Frames contain a low start bit, least
significant data bit first, optional parity and one high stop bit. Format is
latched for each frame. Deadlines use cumulative rational rounding from the
documented nominal baud rate and 16,777,216 Hz system clock. Physical divider
phase, oversampling and glitch rejection remain unqualified.

FIFO mode provides four queued transmit bytes in addition to the active shift
register, and four received bytes. Without FIFO mode, a busy transmitter cannot
accept another byte and the receiver retains one unread byte. Software must
check the transmit-full and receive-empty flags. Reading the data register
consumes the oldest received byte. An unused upper-byte write does not enqueue
data. Disabling FIFO resets its sequencers.

With FIFO enabled, a transmit interrupt occurs when loading the shift register
empties the transmit FIFO, and a receive interrupt occurs when the receive FIFO
fills. A partial final receive FIFO must be polled. Without FIFO, completion
requests the corresponding interrupt. Parity, framing and overrun errors also
request an enabled interrupt; an invalid frame preserves earlier unread data.
Reading SIOCNT acknowledges the error flag. Interrupts use the core's ordinary
request publication and HALT wake path.

The host owns synchronization between connected cores. Dispatch preceding
events before applying an input change with `cupid_serial_set_input`, and use
the timestamp supplied to `cupid_serial_set_output_callback`. Cross SO to SI
and SD to SC for flow control. The SDL2 frontend provides this pairing through
[local sessions](local-session.md). STOP freezes pending UART deadlines; UART inputs do not wake STOP.
Mode changes and reset cancel active frames.

Seven native groups check 12,288 complete format-matrix frames and 124,928
bit-center samples, FIFO capacity, flow control, errors and interrupt sources.
Connected peers exchange distinct bytes in both directions and execution
orders. DMA/timer cases compare complete pin transcripts and data under late
and single-clock dispatch across clock wrap. Lifecycle cases cover HALT, STOP,
reset, mode changes and byte accesses.

Owned ARM and Thumb guests each transfer 1,024 bytes across all rates and FIFO
settings, use the BIOS HALT service, acknowledge interrupts and require complete
RAM output, guards and a zero-result terminal. Two controls corrupt one input
sample: both guests must still finish all bytes, and their complete output must
reject the corruption. An optional external BIOS repeats the unchanged guests.
These fixtures enter application code directly and do not qualify BIOS boot.

Reconstruct the owned 512-byte driver with Clang and LLD:

```sh
clang --target=arm-none-eabi -march=armv4t -fuse-ld=lld -nostdlib \
  '-Wl,-Ttext=0x03001000,--image-base=0x03000000,--oformat=binary,-e,0' \
  tests/gba/serial/uart-driver.s -o uart-driver.bin
python3 tests/gba/serial/generate_uart_driver.py uart-driver.bin
```

The programming manual's UART FIFO diagram and detailed interrupt conditions
define the queue behavior. Its low stop-bit wording conflicts with working
standard UART hardware communication; this implementation uses a high stop bit.
Exact physical edge timing, undocumented writes while full, configuration
changes during a frame and complete electrical compatibility need further
measurement. See [testing](../../development/testing.md).
