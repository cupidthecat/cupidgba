/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      wave.h
 * @author    cupidthecat
 * @brief     Shared wave-channel sample-step helper.
 * @date      2026-11-16
 *
 * - Update sample position and held output for one wave step.
 * - Share the transition logic with bulk wave advancement.
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
#ifndef CUPID_AUDIO_WAVE_INTERNAL_H
#define CUPID_AUDIO_WAVE_INTERNAL_H

#include "cupid/audio.h"

/* Keep the common single-edge path inline. Capture the selected DAC tap
 * before shifting, including when both banks form one 64-sample cycle. */
static inline void cupid_audio_wave_step(struct CupidAudioWaveChannel *wave) {
    const unsigned first = !wave->size && wave->bank ? 4 : 0;
    const unsigned end = wave->size ? 8 : first + 4;
    wave->rawSample = wave->wavedata8[wave->bank ? 16 : 0] >> 4;
    uint32_t carry = wave->wavedata32[first] & 0xF0;
    for (unsigned word = end; word-- > first;) {
        const uint32_t old = wave->wavedata32[word];
        wave->wavedata32[word] =
            ((old & 0x0F0F0F0F) << 4) | ((old & 0xF0F0F000) >> 12) | (carry << 20);
        carry = old & 0xF0;
    }
}

/* Advance cyclic wave storage and retain the last nibble presented to the DAC.
 * The caller owns clock deadlines, register latching and output scaling. */
void cupid_audio_wave_advance(struct CupidAudioWaveChannel *wave, uint64_t steps);

#endif
