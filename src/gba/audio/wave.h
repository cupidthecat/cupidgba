/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file wave.h
 * @brief Shared wave-channel sample-step helper.
 *
 * - Update sample position and held output for one wave step.
 * - Share the transition logic with bulk wave advancement.
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
