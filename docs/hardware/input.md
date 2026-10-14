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
