/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/**
 * @file audio.h
 * @brief GBA audio: 4 PSG channels + 2 DMA FIFO channels.
 *
 *
 * Architecture:
 *   - PSG engine (inherited from GB): ch1 (square+sweep), ch2 (square),
 *     ch3 (wave), ch4 (noise), mixed via NR50/NR51
 *   - timingFactor = 4 (GBA ARM runs 4x SM83 speed, all PSG periods x4)
 *   - Two Direct Sound FIFO channels (A, B): 8x uint32_t ring buffers
 *     that feed int8_t samples one byte at a time
 *   - _sample event fires every SAMPLE_INTERVAL (1024) CPU cycles
 *   - Inside _sample, cupid_audio_sample produces 2<<resolution sub-samples
 *     spaced sampleInterval (default 512) cycles apart
 *   - FIFO bytes are distributed across sub-sample slots by timer overflow
 *   - Frame sequencer at 512 Hz updates length, envelope, sweep
 */

#ifndef CUPID_AUDIO_H
#define CUPID_AUDIO_H

#include "cupid/types.h"
#include "cupid/timing.h"

struct CupidGBA;
struct CupidDMA;

/** @name Constants */
#define GBA_AUDIO_FIFO_SIZE      8       /**< 8 x uint32_t = 32 bytes per FIFO */
#define GBA_MAX_SAMPLES          16      /**< Max sub-samples per 1024-cycle window */
#define GBA_AUDIO_SAMPLES        8192    /**< SDL output ring buffer size (stereo frames) */
#define GBA_AUDIO_SAMPLE_RATE    32768   /**< Default output rate at resolution 0 */
#define GBA_AUDIO_VOLUME_MAX     0x100   /**< Master volume 1.0 in fixed point */

/** SAMPLE_INTERVAL: the _sample event period = GBA_CLOCK_FREQ / 0x4000 = 1024 */
#define GBA_AUDIO_SAMPLE_INTERVAL  (GBA_CLOCK_FREQ / 0x4000)  /**< 1024 */

/**
 * Frame sequencer: SM83 FRAME_CYCLES = DMG_FREQ >> 9 = 8192 SM83 cycles.
 * GBA timingFactor = 4, so GBA frame interval = 8192 * 4 = 32768 ARM cycles.
 */
#define DMG_SM83_FREQUENCY       0x400000u
#define GBA_AUDIO_TIMING_FACTOR  4
#define GBA_AUDIO_FRAME_CYCLES   (DMG_SM83_FREQUENCY >> 9)  /**< 8192 SM83 cycles */
#define GBA_AUDIO_FRAME_INTERVAL (GBA_AUDIO_FRAME_CYCLES * GBA_AUDIO_TIMING_FACTOR)

/** Stereo sample pair */
struct CupidStereoSample {
    int16_t left;
    int16_t right;
};

/** @name PSG Sub-structures */

/** Volume envelope — ch1, ch2, ch4 */
struct CupidAudioEnvelope {
    int length;           /**< Sound length value from NRx1 */
    int duty;             /**< Duty cycle index 0-3 */
    int stepTime;         /**< Envelope step period (0 = disabled) */
    int initialVolume;    /**< Volume on trigger (NRx2 bits 4-7) */
    int currentVolume;    /**< Running volume 0-15 */
    bool direction;       /**< False = decrease, true = increase */
    int dead;             /**< 0 = active, 1 = max, 2 = zero (dead states) */
    int nextStep;         /**< Steps until next envelope tick */
};

/** Square wave frequency/length control — ch1, ch2 */
struct CupidAudioSquareControl {
    int frequency;        /**< 11-bit frequency register */
    int length;           /**< Remaining length counter */
    bool stop;            /**< True = stop when length expires */
};

/** Frequency sweep — ch1 only */
struct CupidAudioSweep {
    int shift;
    int time;             /**< Sweep period */
    int step;
    bool direction;       /**< False = increase, true = decrease */
    bool enable;
    bool occurred;        /**< Negate has been used */
    int realFrequency;    /**< Shadow frequency */
};

/** Square channel (ch1 and ch2) */
struct CupidAudioSquareChannel {
    struct CupidAudioSweep sweep;
    struct CupidAudioEnvelope envelope;
    struct CupidAudioSquareControl control;
    int32_t lastUpdate;   /**< Absolute timestamp of last run */
    uint8_t index;        /**< Duty cycle position (0-7) */
    int8_t sample;        /**< Current output sample (0..15) */
};

/** Wave channel (ch3) */
struct CupidAudioWaveChannel {
    bool size;            /**< False = one 32-sample bank, true = 64 samples */
    bool bank;            /**< Current bank select (GBA only) */
    bool enable;          /**< DAC enable (NR30 bit 7) */

    int8_t sample;        /**< Current output sample */
    int length;           /**< Remaining length counter */
    int volume;           /**< Volume code: 0=mute, 1=100%, 2=50%, 3=25%, >3=75% */

    int rate;             /**< 11-bit frequency/rate register */
    bool stop;            /**< Length-enabled flag */

    int window;           /**< Current wave RAM position */
    bool readable;        /**< Wave RAM accessible while playing */
    union {
        uint32_t wavedata32[8]; /**< 8 x 32-bit = 64 nybble samples */
        uint8_t  wavedata8[32]; /**< Byte-addressable view */
    };
    int32_t nextUpdate;   /**< Absolute timestamp of next wave step */
};

/** Noise channel (ch4) */
struct CupidAudioNoiseChannel {
    struct CupidAudioEnvelope envelope;

    int ratio;
    int frequency;
    bool power;           /**< False = 15-bit LFSR, true = 7-bit */
    bool stop;
    int length;

    uint32_t lfsr;
    int nSamples;         /**< Accumulated samples for coalescence */
    int samples;          /**< Accumulated positive samples x volume */
    uint32_t lastEvent;   /**< Absolute timestamp of last noise step */

    int8_t sample;
};

/** @name PSG Engine */
struct CupidAudioPSG {
    struct CupidAudioSquareChannel ch1;
    struct CupidAudioSquareChannel ch2;
    struct CupidAudioWaveChannel   ch3;
    struct CupidAudioNoiseChannel  ch4;

    uint8_t volumeRight;
    uint8_t volumeLeft;

    bool ch1Right, ch2Right, ch3Right, ch4Right;
    bool ch1Left,  ch2Left,  ch3Left,  ch4Left;

    bool playingCh1, playingCh2, playingCh3, playingCh4;

    int frame;
    bool skipFrame;

    int timingFactor;     /**< 4 for GBA */

    bool enable;
    bool forceDisableCh[4];

    struct CupidTimingEvent frameEvent;
};

/** @name DMA FIFO Channel */
struct CupidAudioFIFO {
    uint32_t fifo[GBA_AUDIO_FIFO_SIZE];
    int fifoWrite;
    int fifoRead;
    uint32_t internalSample;
    int internalRemaining;
    int dmaSource;
    int8_t samples[GBA_MAX_SAMPLES]; /**< Per-sub-sample output */
};

/** @name GBA Audio */
struct CupidAudio {
    struct CupidGBA* gba;

    struct CupidAudioPSG psg;

    struct CupidAudioFIFO chA;
    struct CupidAudioFIFO chB;

    /** SOUNDCNT_H */
    int volume;
    bool volumeChA;
    bool volumeChB;
    bool chARight, chALeft;
    bool chATimer;
    bool chBRight, chBLeft;
    bool chBTimer;

    /** SOUNDCNT_X */
    bool enable;

    /** SOUNDBIAS */
    uint16_t soundbias;

    /** Sub-sampling state */
    int32_t sampleInterval;  /**< Cycles per sub-sample (default 512) */
    int32_t lastSample;      /**< Absolute timestamp of current window start */
    int sampleIndex;         /**< Next sub-sample index within window */

    struct CupidStereoSample currentSamples[GBA_MAX_SAMPLES];

    /** Output ring buffer for SDL */
    int16_t sampleBuffer[GBA_AUDIO_SAMPLES * 2];
    volatile int sampleWritePos;
    volatile int sampleReadPos;

    uint16_t soundcntL;
    uint16_t soundcntH;

    int masterVolume;

    struct CupidTimingEvent sampleEvent;
};

/** @name API */

void cupid_audio_init(struct CupidAudio* audio, struct CupidGBA* gba);
void cupid_audio_reset(struct CupidAudio* audio);
void cupid_audio_tick(struct CupidAudio* audio, int32_t cycles);

void cupid_audio_timer_overflow(struct CupidAudio* audio, int timer);
void cupid_audio_sample_fifo(struct CupidAudio* audio, int fifoId);

void cupid_audio_write_register(struct CupidAudio* audio, uint32_t addr, uint16_t val);
uint16_t cupid_audio_read_register(struct CupidAudio* audio, uint32_t addr);

void cupid_audio_fifo_write(struct CupidAudioFIFO* fifo, uint32_t value);
void cupid_audio_fifo_reset(struct CupidAudioFIFO* fifo);
void cupid_audio_schedule_fifo_dma(struct CupidAudio* audio, int dmaNumber,
                                   struct CupidDMA* dma);

void cupid_audio_sample(struct CupidAudio* audio, int32_t timestamp);

void cupid_audio_psg_run(struct CupidAudioPSG* psg, int32_t timestamp, int channels);
void cupid_audio_psg_sample(struct CupidAudioPSG* psg, int16_t* left, int16_t* right);
void cupid_audio_psg_frame_step(struct CupidAudioPSG* psg);

#endif /* CUPID_AUDIO_H */
