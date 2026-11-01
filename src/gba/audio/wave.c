/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wave.h"

void cupid_audio_wave_advance(struct CupidAudioWaveChannel *wave, uint64_t steps) {
    if (!steps)
        return;
    const unsigned first = !wave->size && wave->bank ? 16 : 0;
    const unsigned bytes = wave->size ? 32 : 16;
    const unsigned mask = bytes - 1;
    const unsigned tap = (wave->bank ? 16 : 0) - first;
    const unsigned last = (unsigned)((steps - 1) & (bytes * 2 - 1));
    const uint8_t sample = wave->wavedata8[first + ((tap + last / 2) & mask)];
    wave->rawSample = last & 1 ? sample & 15 : sample >> 4;

    /* Complete waveform cycles restore every storage bit, but still publish
     * their last sample. Reduce only the rotation, never the elapsed clocks. */
    const unsigned rotation = (unsigned)(steps & (bytes * 2 - 1));
    if (!rotation)
        return;
    if (rotation <= 3) {
        /* Ordinary low-frequency observations usually advance one nibble.
         * Keep that short word path instead of copying an entire bank. */
        for (unsigned step = 0; step < rotation; ++step)
            cupid_audio_wave_step(wave);
        return;
    }

    uint8_t before[32];
    memcpy(before, wave->wavedata8 + first, bytes);
    const unsigned wholeBytes = rotation / 2;
    for (unsigned i = 0; i < bytes; ++i) {
        const unsigned source = (i + wholeBytes) & mask;
        wave->wavedata8[first + i] =
            rotation & 1 ? (uint8_t)((before[source] << 4) | (before[(source + 1) & mask] >> 4))
                         : before[source];
    }
}
