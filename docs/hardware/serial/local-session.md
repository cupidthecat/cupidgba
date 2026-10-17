# Local console sessions

Start two consoles with `cupidgba first.gba --link second.gba`. Repeat
`--link` for a third and fourth console in multiplayer mode. Each console has
its own CPU, clock, memory, display, audio queue and battery. C11 and SDL2
remain the core and frontend interfaces. Windows uses cooperative fibers;
Linux uses native context entry points when Boost.Context is available, with
cooperative `ucontext` stacks as the portable fallback. All application and
core code stays C11. All emulation stays on the creating
thread. The SDL playback callback consumes the mixed audio separately.

The window shows two columns, with a second row for three or four consoles.
Tab or a click selects the console that receives keyboard input. Moving focus
releases that console's held keyboard keys. Controllers belong to separate
consoles in attachment order, with each instance assigned once. F9 disconnects
or reconnects the local connection. Escape or closing the window exits and
flushes every battery. The title identifies keyboard focus and connection.
The selected console also receives sensor keys: F1 through F4 adjust tilt,
F5/F6 adjust sunlight, F7/F8 adjust the gyroscope, F10 centers tilt and F11
centers the gyroscope. F9 retains its connection action. Each controller's
rumble follows its own console and stops on removal or shutdown.
Each viewport retains the last image completed at its console's existing
VBlank handoff. Drawing the next scanline cannot change that published image.
A stopped console keeps its last complete image while the others continue.

Local batteries use `.p1.sav` through `.p4.sav` alongside each slot's ROM.
For example, the first slot using `game.gba` owns `game.p1.sav`. Opening the
same cartridge in several slots still creates separate batteries. Ordinary
single-console play keeps `game.sav`; local play does not import or overwrite
it. Every ROM, BIOS and existing slot battery is checked before any console
runs. A failed startup flushes none of them. Automatic flushes occur every
300 session frame intervals and on a successful session's shutdown.
RTC batteries use the matching `.p1.rtc` through `.p4.rtc` paths. RTC, clock
seed, offline-clock and other cartridge options apply separately to every
slot. Existing single-console `.rtc` files retain their paths. All RTC files
are validated before startup, including when a later slot contains bad data.

`--frames N` ends a local session after N frame intervals. It provides bounded
runs without relying on window input. Omit it for normal play. BIOS, scale
and save-type options apply to every slot.

## Connection behavior

A pair cross-connects SO to peer SI. Normal and GPIO modes share SC and SD;
UART mode receives peer SD on SC for flow control. Normal 8-bit and 32-bit
transfers retain the clock owner and shift every bit through the core's pin
interface. UART retains baud selection, frame format, FIFO state, errors and
CTS behavior. Mode changes update the routing even when driven levels remain
the same. These are logical peripheral connections. A physical multiplayer
cable has different, one-way normal wiring; its electrical behavior is not
modeled by this pairing.

Multiplayer assigns player IDs in command-line order and uses the existing
connected-word engine. The master latches each available word at the start;
each console publishes its receive slots on its own serial deadlines.
Readiness requires every console to select multiplayer and run its clock.
An unavailable console produces a transfer error. Clearing Start, changing a
participating mode or disconnecting cancels the connected multiplayer frame
without a completion IRQ. Reconnection retains the console order.

Disconnect releases pair input pins. Normal external-clock transfers can
remain waiting for further clocks, and a partial UART frame can finish with
the core's existing error policy. Guest software owns timeout and retry.
JOY host commands, network transport and physical cable calibration remain
separate interfaces. Paired pins are provided for two consoles; three and
four console sessions use multiplayer.

## Clock coordination

Each CPU runs on a cooperative stack. Before consuming a fetch, data or
internal interval, it yields its requested clock boundary to the session.
Zero-clock boundaries dispatch due events on the executing stack. They consume
no emulated time and need no context switch, including nested MMIO flushes
inside peripheral callbacks.
The host advances running console clocks together to the earliest CPU
boundary, peripheral event or requested endpoint. It dispatches all events
at that instant in priority order before releasing a CPU. Equal-priority
events and equal-time CPU boundaries use player order. Pin changes propagate
synchronously at that instant. A long instruction or DMA hold cannot advance
a peer past an earlier output and then inject the output into its past.

HALT and DMA waits are interruptible clock requests. A newly arrived peer
event can shorten that wait; fixed-duration CPU bus phases retain their full
intervals. STOP freezes only that console's local clock, while session time
and the other consoles continue. An eligible GPIO serial or keypad source
can wake it through the existing IRQ path. UART does not wake STOP.
If STOP arrives during an unfinished interval, the remaining clocks stay
pending until wake; elapsed session time does not consume them.
The cartridge RTC continues on its separate oscillator. Its next output edge
participates in the session's deadline selection, and an eligible Game Pak
request can wake that console at the selected endpoint. The other consoles
keep running; no frozen CPU or system-peripheral interval consumes battery
clocks. A disabled wake source can retain a pending request during STOP.
All running clocks reach that endpoint first. The session completes every
stopped battery interval before publishing wake-caused pin changes. A UART
start therefore reaches its peer at the endpoint, and a GPIO-woken peer
retains its preceding frozen system interval.

The coordinator adds no fitted delay or game-specific rule. It preserves the
core's existing bit, completion, timer, IRQ and bus durations. Same-clock
ordering is deterministic software policy; physical divider phase, clock
contention, coincident electrical transitions and multiplayer subframe phase
estimates retain the limits in the core serial documentation.

## Embedding and verification

`cupid/cable.h` borrows two to four initialized, idle cores. Set their inputs
and program state before the first run. Their current clocks become separate
local epochs. Clock and serial callbacks must be unassigned, and transfers
must be idle. The caller must keep every core alive and call the session on
its creating thread. A session does not share a thread with another active
session or with an external Windows fiber host.

Run calls return at clock requests, including inside an instruction. Reading
memory or video, changing keys, and flushing batteries is safe while paused.
Do not run or reset a borrowed CPU separately. Destroying the session stops
and frees its cooperative stacks. Reset a core before executing it again,
because an in-flight instruction has been abandoned. Frontend shutdown
flushes batteries before destroying the session and its cores.

Owned tests require complete normal/multiplayer guest records and every
duplex UART byte, actual issued success-terminal branches, HALT progress,
active DMA, chronology across clock wrap, cancellation and STOP wake. A
whole-step control must expose stale peer clocks; corrupted payload controls
must complete their terminals while their complete records reject the data.
SDL tests check input ownership, tiled pixels, mixed PCM, live playback and
complete independent battery images. Source reconstruction verifies every
byte of the owned guest drivers. Sanitizer builds use fiber switch hooks;
AddressSanitizer still prints its standard `makecontext/swapcontext` support
warning when using the portable Linux backend.

On Ubuntu, install `libboost-context-dev` (version 1.61 or later) for native
stack switching. This avoids the portable backend's per-switch signal-mask
work; it changes no
emulated clock. `-DCUPID_FAST_CONTEXT=OFF` explicitly selects the portable
backend. Both paths use the same clock coordinator and sanitizer hooks.

These tests qualify the logical session and the recorded guest exchanges.
They do not establish electrical timing or complete game link compatibility.
