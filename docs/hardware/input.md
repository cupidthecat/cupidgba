# Keyboard and controller input

The SDL2 frontend combines held keyboard keys, selected-controller buttons
and the controller's left stick. A GBA button stays pressed while any of
these sources holds it. Releasing one source leaves the others intact, and
a neutral stick does not clear digital D-pad holds.

The bindings remain Z/X for A/B, Enter for Start, Backspace for Select,
arrows for the D-pad and A/S for L/R. Controllers use A/B, Back, Start,
D-pad and shoulder buttons. Stick values must exceed the existing strict
dead zone of ±16,384 before they add a direction. Opposite directions from
different sources remain separate pressed bits.

The frontend keeps the selected controller's instance ID. Button and stick
state comes from that controller's handle; unselected devices do not change
the GBA keys. On removal, its state drops out of the combined mask. The first
remaining available controller becomes the owner, or a newly connected one
is selected when none remains. Keyboard holds survive controller changes.

Losing the window's focus clears keyboard holds, preventing a missed key-up
from leaving a key pressed. Valid controller holds remain. The frontend
updates the combined mask once per event poll and then checks the existing
keypad IRQ logic. This change preserves the core's register and interrupt
timing; it does not add cycle-specific host input scheduling.

Five SDL2 virtual-device groups check all ten bindings and both release
orders, all 1,024 complementary keyboard/controller masks, 81 stick position
pairs including dead-zone boundaries, selected-device ownership, focus,
removal, fallback and reconnection. Integration cases check active-low
KEYINPUT, a combined-source KEYCNT AND request, and keypad STOP wake with
IME clear and the CPU IRQ mask set. They verify software input handling;
they do not establish complete hardware interrupt timing accuracy.

## Keypad control requests

KEYINPUT reports the ten buttons as active-low bits. Writes to it have no
effect. KEYCNT stores the selected buttons, request enable and AND/OR mode;
reserved bits read as zero. An aligned word store at KEYINPUT can therefore
configure KEYCNT through its upper halfword without changing the held buttons.

An enabled OR condition requests an interrupt whenever a check finds any
selected button held. An enabled AND condition matches when all selected
buttons are held, including when additional unselected buttons are held.
The empty AND selection also matches in the selected software model. The
documented selected-button conditions appear in the
[AGB Programming Manual, section 14.2.1](https://www.manuallib.com/download/2023-10-17/AGB%20Programming%20Manual.pdf#page=156).

AND checks retain the last matching held-button state to suppress repeated
requests for that state. A failed condition arms the next match. Selecting a
new button invalidates that button's contribution to the retained state.
KEYCNT writes preserve this history instead of replacing it with the current
buttons before checking the condition. Thus a program can enable AND mode
while its selected buttons are already held and receive a request. Rewriting
the same matching selection retains suppression; adding a held button to the
selection allows a new request. These retained-history rules are the selected
logical model, with no claim of electrical edge calibration.

Each byte store checks its intermediate control value. An accepted request
remains pending if a later byte produces a nonmatching selection. Programs
that need to replace both bytes without an intermediate request can disable
requests first, write the selection, then enable the final control value.

Requests use the existing IRQ acceptance stages: IF publication after one
system clock, availability after the next clock and the CPU line after the
third clock. IE gates availability; IME additionally gates the CPU line.
Neither IME nor the CPU's interrupt mask suppresses IF publication. An accepted
keypad request can prevent STOP when its IE bit is enabled. General CPU bus
stall rules still govern CPU line sampling.

Seven native groups check every selected-mask/held-mask pair in both modes
through halfword, byte and word paths: 6,291,456 observations. They also retain
1,448 control/input transition observations, 240 decoded ARM/Thumb stores and
96 IRQ/STOP cases. The latter groups cover four clock anchors, including
32-bit clock wrap. The preceding core fails every group. The SDL2 input tests
remain separate. These checks qualify the control-write repair and its logical
request behavior; physical input sampling, switch bounce and complete hardware
interrupt timing remain unmeasured.
