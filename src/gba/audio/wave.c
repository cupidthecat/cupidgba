/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      wave.c
 * @author    cupidthecat
 * @brief     Bulk advancement of wave-channel sample and bank state.
 * @date      2026-11-16
 *
 * - Advance wave position from elapsed sample clocks.
 * - Retain the same sample rotation across grouped clock updates.
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
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
