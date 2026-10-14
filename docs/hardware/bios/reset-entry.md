# Reset entry

CupidGBA enters an external BIOS at the reset vector in ARM Supervisor mode.
The CPSR control byte is `0xD3`: both interrupt classes are masked and Thumb
state is clear. The active privilege bank and sampled IRQ mask agree with
that control state before the first firmware instruction executes. Firmware
can change the masks and initialize its own stacks during boot.

This follows the reset control defined in the
[ARM7TDMI Technical Reference Manual](https://documentation-service.arm.com/static/5e8e1323fd977155116a3129).
Other register values after hardware reset are indeterminate. CupidGBA clears
them for deterministic initialization; that policy does not specify their
physical power-on values or reset pulse duration.

The built-in fast entry applies a post-boot state: ARM System mode with control
byte `0x1F`, the System, IRQ and Supervisor stacks, and the existing initial
display phase and POST flag. Those settings belong to the explicit BIOS skip
path. A skip request with PC away from the reset vector leaves the CPU alone.

The four `arm.reset-entry-*` groups use owned instructions to read the first
guest-visible CPSR, cover all seven prior privilege modes, both instruction
states, all interrupt-mask combinations and both sampled IRQ masks, and check
repeated reset without replacing firmware, ROM or save storage. A pending IRQ
case verifies that firmware executes while masked and that an IRQ becomes
eligible after firmware unmasks it. It does not add a new IRQ latency claim.

Native BIOS service fixtures inject application code without executing a full
boot. They prepare the caller mode and stack banks explicitly, while keeping
firmware ownership, clocks, peripheral queues and existing assertions intact.
Reset-entry fixtures and source ROMs that execute firmware boot retain the
raw reset state. The separate warm-reset service still needs qualification.
