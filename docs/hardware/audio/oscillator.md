# Square-channel duty timers

Each square channel stores its programmed frequency separately from the period
loaded into its duty-step timer. The running period completes before a frequency
write takes effect. At the next duty edge, the timer reloads from the latest
frequency value. Repeated writes before that edge replace the next reload value
and retain the pending edge.

An active channel continues to advance its duty position when its envelope
reaches zero volume. Raising the envelope later uses that running position.
An explicit trigger reloads the timer from the current frequency and retains
the duty position. Trigger startup latency remains a separate requirement.

Register accesses advance the affected channel through the access clock using
its preceding state before applying the write. A frequency sweep updates the
same programmed frequency used by software writes. Its new period takes effect
after the already pending duty edge.

## Regression coverage

Five native test groups cover 1,261,572 scenes. Frequency-write groups exercise
both channels, every 11-bit starting frequency, eight next frequencies, four
write positions within a period, byte and halfword writes, and four clock
origins. Sequence groups cover repeated writes and triggers, silent envelopes
and a scheduled sweep crossing an in-flight duty period. Checks observe the
duty position and the exact reload clock before and after each boundary.

The counter model and these checks cover active square-channel period reloads.
Physical startup phase, trigger latency, bus acceptance timing and the output
signal still require qualification under
[issue 1](https://github.com/cupidthecat/cupidgba/issues/1).
