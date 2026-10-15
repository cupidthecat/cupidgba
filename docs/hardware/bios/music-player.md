# BIOS music player

SWIs `20h` through `24h` execute player lifecycle operations in the replacement
firmware. SWI `2Ah` copies its 36 callable helper pointers. These interfaces
are undocumented; the names below describe controlled software observations.
They do not establish a universal library ABI or physical timing measurements.

## Players, tracks and ownership

MusicPlayerOpen takes a player address in R0, a track-array address in R1 and
a signed track capacity in R2. Nonpositive capacities return without writes;
positive capacities clamp to sixteen. It clears exactly 64 player bytes,
stores the track pointer, sets the pause bit and clears each accepted track's
status byte. Other track bytes remain intact. The caller must supply valid
voice-list links before subsequent operations walk them.

When the attached sound work area has identifier `68736D53h`, Open marks it
busy with `68736D54h` and installs the player frame callback and argument.
It retains the previous callback and argument in the new player so multiple
players form a callable chain. It restores the sound identifier and gives the
player the ready identifier. If the parent sound area is busy or invalid,
the initial player/track writes still occur, but attachment and the player's
ready store do not. This ordering is observable during execution.

The player fields exercised by the native fixtures are:

| Offset | Observed role |
| --- | --- |
| `00h`, `30h` | Song-header and voice-group pointers |
| `04h` | Track status mask with bit 31 used for pause |
| `08h`, `09h`, `0Ah` | Capacity, song priority and current command index |
| `1Ch` through `22h` | Tempo, tempo scale, effective tempo and accumulator |
| `24h`, `26h`, `28h` | Fade interval, counter and level |
| `2Ch`, `34h` | Track-array pointer and ready/busy identifier |
| `38h`, `3Ch` | Previous frame callback and its argument |

Each track occupies 80 bytes. It holds status, wait/running-command state,
note operands, signed controller fields, a voice-list head at `20h`, tone
data at `24h`, script cursor at `40h` and three pattern return addresses at
`44h` through `4Ch`. Voice records have separate owner, previous and next
links. Opening a player does not initialize all of these retained fields.

## Lifecycle and sequencing

MusicPlayerStart takes the player in R0 and song header in R1. A ready player
becomes busy while it stops old voices and installs the song state. Track
count clips to the player's capacity; selected tracks become pending
initialization, and unused tracks are stopped. The song header supplies
priority, voice-group pointer, track-script pointers and an optional sound
mode/reverb byte. Mode selectors retain the sound driver's existing masks.

MusicPlayerStop sets the pause bit and stops owned live voices. It clears
their owner fields and the track's voice-list head while retaining unrelated
record payload. Attached legacy channels call the supplied stop hook.
MusicPlayerContinue clears the pause bit. MusicPlayerFadeOut stores R1's low
halfword as both interval and counter, and starts the fade level at 256.
Each operation retains the observed ready/busy guard behavior.

SoundDriverMain calls the installed player chain through normal instructions.
Each player owns its ready/busy identifier while it calls its predecessor,
advances the fade and accumulates tempo. The sequencer consumes ticks in units
of 150, advances waits, decodes commands and updates linked voices. It supports
running operands, Goto, Pattern/PatternEnd, counted Repeat, priority, tempo,
key shift, voice selection, volume, pan, bend/range, LFO speed/delay, modulation
and type, tune, port writes, tied notes and EndTie. A fourth nested Pattern
ends the track through Fine without writing beyond the three-entry stack.

Notes use the duration table, optional key/velocity/length operands and the
selected tone. PCM allocation considers inactive channels, release state,
priority and owning-track order. Tone records support key splitting and drum
mapping. PCM voices use the timed integer frequency helper and software mixer.
Legacy channel types use the attached channel records and frequency hook.

The observed all-tracks-inactive path retains the working register value used
by the firmware status store. It is not replaced with an inferred ideal mask.
Malformed pointers, nonterminating scripts and library-specific extensions
outside the covered inputs remain unqualified.

## Discovery and library hooks

SWI `2Ah` copies 36 words, exactly 144 bytes, through individual loads and
stores. R0 advances by 144 and R1 becomes zero. Following bytes remain intact.
Descriptions that specify a larger buffer do not change the observed write
extent. Every returned pointer has its Thumb state bit. Internal function
addresses describe the replacement image's layout and need not match a
separate firmware image.

| Entries | Operation |
| --- | --- |
| `0`, `5`–`8`, `21`, `22`, `24`–`26`, `28` | Fine, with shared function identity |
| `1`–`4` | Goto, Pattern, PatternEnd, Repeat |
| `9`–`20` | Priority, tempo, key shift, voice, volume, pan, bend, bend range, LFO speed, LFO delay, modulation, modulation type |
| `23`, `27`, `29` | Tune, port write, EndTie |
| `30`–`35` | Playback-rate configuration, track stop, fade step, track-field update, voice unlink, 64-byte clear |

The sound work area's `34h` pointer selects the command table, and `38h`
selects the note handler. Both are live after initialization. Libraries may
install a RAM command table or wrap the note handler. The operand source
guard admits RAM and the tested BIOS data range beginning at `3738h`; other
protected BIOS operands produce zero. Direct helper calls retain their
observed scratch-register behavior rather than the SWI wrapper's convention.

The sound area's `1Ch` pointer attaches legacy channel records. Hooks at `28h`,
`2Ch`, `30h` and `3Ch` provide the frame extension, stop, frequency and track
extension operations. Their initialized return functions indicate detached
library extensions. The built-in player calls installed ARM or Thumb hooks;
it does not invent a default library PSG synthesizer. The owned callback
fixture supplies deterministic channel state and frequency results to test
this boundary.

## Qualification

Thirty-nine native groups cover lifecycle guards, complete retained images,
all exported entries and aliases, ARM/Thumb callers, System/IRQ banks and
EWRAM/IWRAM. Supplying `CUPID_TEST_BIOS` repeats the groups with an authorized
local input. No external firmware, game, disassembly or capture is distributed.
Existing sound-driver expectations, instruction guards and ten-second test
limits remain intact.

Independent observations cover 18 lifecycle stages, 96 track-field inputs,
132 song frames, 24 callback frames, 36 live-output frames and nine partial
service executions. The owned songs exercise multiple tracks and players,
running state, patterns and overflow, repeats, competing voices, mapping,
modulation, pause/continue, fade and ties. Callback cases check both instruction
states, actual call counts/arguments, busy ownership and a wrapped RAM command
table. A nested timer IRQ attempts Stop while the player is busy.

Image signatures include the prepared 4 KiB sound area, two 64-byte players
and 1,536 track bytes. Song/callback cases also include the 256-byte legacy
channel block and 256-byte callback context. Only nonzero internal BIOS
function pointers in the documented hook fields are normalized; RAM hooks,
data pointers, retained payload and guard bytes remain in the signature.
Separate assertions verify function bounds, state bits, roles and aliases.

Three twelve-frame output cases keep timer 0 and both FIFO DMA channels active.
They check every timestamp and stereo sample, 39,502 samples in total, plus
per-frame state, elapsed clocks, DMA source advancement and latched samples.
The host queue is consumed each frame with zero overruns. Partial-service
checks observe ordered player clearing, track writes, busy transitions and
all 36 discovery stores while timer 1 and DMA0 run. They include late scheduler
events, 32-bit clock wrap and clocks above that range.

These observations qualify the listed software inputs and modeled digital
output. Physical audio, undocumented library variants, arbitrary malformed
state, complete BIOS behavior and full audio-system acceptance remain open.
