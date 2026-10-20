/**
 * @file audio.c
 * @brief GBA audio mixing: PSG channels plus Direct Sound FIFO channels.
 *
 * Architecture:
 *   - PSG engine: ch1 (square+sweep), ch2 (square), ch3 (wave), ch4 (noise)
 *   - @c timingFactor = 4 so PSG periods are scaled to the GBA CPU clock
 *   - Two Direct Sound FIFO channels (A and B) backed by ring buffers
 *   - A mixer event captures PSG and FIFO values on the same sample clock
 *   - Captures retain their elapsed clocks across sampling-rate changes
 *   - FIFO bytes latch on timer overflow and hold between overflows
 *   - A 512 Hz frame sequencer updates length, envelope, and sweep state
 */

#include "cupid/gba.h"
#include <string.h>
#include <stdarg.h>

/** @section audio_constants Constants */

/** @brief Square-wave duty table indexed by duty mode and phase. */
static const int8_t _squareChannelDuty[4][8] = {
    { 0, 0, 0, 0, 0, 0, 0, 1 },  /* 12.5% */
    { 1, 0, 0, 0, 0, 0, 0, 1 },  /* 25%   */
    { 1, 0, 0, 0, 0, 1, 1, 1 },  /* 50%   */
    { 0, 1, 1, 1, 1, 1, 1, 0 },  /* 75%   */
};

/** @section audio_forward_decls Forward declarations (static helpers) */
static void _sampleCallback(struct CupidTiming* timing, void* context,
                             uint32_t cyclesLate);
static void _frameCallback(struct CupidTiming* timing, void* context,
                            uint32_t cyclesLate);

static void _updateSquareSample(struct CupidAudioSquareChannel* ch);
static void _resetEnvelope(struct CupidAudioEnvelope* envelope);
static void _updateEnvelopeDead(struct CupidAudioEnvelope* envelope);
static bool _writeEnvelope(struct CupidAudioEnvelope* envelope, uint8_t value);
static void _updateEnvelope(struct CupidAudioEnvelope* envelope);
static bool _updateSweep(struct CupidAudioSquareChannel* ch, bool initial);
static void _resetSweep(struct CupidAudioSweep* sweep);

static int16_t _applyBias(struct CupidAudio* audio, int32_t sample);

/**
 * @section audio_debug Logging (opt-in)
 *
 * Enable with:
 *   @c CUPID_AUDIO_DEBUG=1
 *
 * Optional file:
 *   @c CUPID_AUDIO_DEBUG_FILE=/path/to/file.txt
 */
static bool _audioDebugEnabled;
static bool _audioDebugInitDone;
static FILE* _audioDebugOut;
static uint32_t _audioDebugSampleCbCount;

/**
 * @brief Initialise optional audio debug logging from environment variables.
 */
static void _audioDebugInit(void) {
    if (_audioDebugInitDone) {
        return;
    }
    _audioDebugInitDone = true;

    const char* enable = getenv("CUPID_AUDIO_DEBUG");
    if (!enable || !*enable || (*enable == '0' && !enable[1])) {
        _audioDebugEnabled = false;
        return;
    }

    _audioDebugEnabled = true;
    _audioDebugOut = stderr;

    const char* path = getenv("CUPID_AUDIO_DEBUG_FILE");
    if (path && *path) {
        FILE* f = fopen(path, "w");
        if (f) {
            _audioDebugOut = f;
        }
    }
}

/**
 * @brief Write a formatted audio debug log line if debugging is enabled.
 *
 * @param audio Audio subsystem state, used to include the current timestamp.
 * @param fmt   printf-style format string.
 * @param ...   Format arguments.
 */
static void _audioDebugLog(struct CupidAudio* audio, const char* fmt, ...) {
    if (!_audioDebugEnabled || !_audioDebugOut) {
        return;
    }

    int32_t t = 0;
    if (audio && audio->gba && audio->gba->timing.relativeCycles) {
        t = cupid_timing_current_time(&audio->gba->timing);
    }

    fprintf(_audioDebugOut, "[AUDIO t=%d] ", t);

    va_list args;
    va_start(args, fmt);
    vfprintf(_audioDebugOut, fmt, args);
    va_end(args);

    fputc('\n', _audioDebugOut);
    fflush(_audioDebugOut);
}

/** @section audio_init_reset Init and reset */
/**
 * @brief Initialise the audio subsystem state for a GBA instance.
 *
 * Clears the audio state, binds it to @p gba, configures the periodic timing
 * events, and installs default mixing parameters.
 *
 * @param audio Audio subsystem state to initialise.
 * @param gba   Owning GBA instance.
 */
void cupid_audio_init(struct CupidAudio* audio, struct CupidGBA* gba) {
    memset(audio, 0, sizeof(*audio));
    audio->gba = gba;
    _audioDebugInit();

    /** Mixer capture event. */
    audio->sampleEvent.name     = "CupidAudio Sample";
    audio->sampleEvent.callback = _sampleCallback;
    audio->sampleEvent.context  = audio;
    audio->sampleEvent.priority = 0x18;

    /** Frame sequencer event (512 Hz). */
    audio->psg.frameEvent.name     = "CupidAudio Frame";
    audio->psg.frameEvent.callback = _frameCallback;
    audio->psg.frameEvent.context  = audio;
    audio->psg.frameEvent.priority = 0x10;

    /** PSG timing factor. */
    audio->psg.timingFactor = GBA_AUDIO_TIMING_FACTOR;

    /** Default master volume. */
    audio->masterVolume = GBA_AUDIO_VOLUME_MAX;

    /** Default sample interval (32768 Hz, resolution 0). */
    audio->sampleInterval = GBA_CLOCK_FREQ / 0x8000;  /* 512 */

    _audioDebugLog(audio, "init: sampleInterval=%d masterVolume=%d", (int)audio->sampleInterval, audio->masterVolume);
}

/**
 * @brief Reset the full audio subsystem to power-on state.
 *
 * Clears PSG channels, FIFO state, sample buffers, mixer controls, and
 * reschedules the frame sequencer and sample events from time zero.
 *
 * @param audio Audio subsystem state to reset.
 */
void cupid_audio_reset(struct CupidAudio* audio) {
    struct CupidGBA* gba = audio->gba;
    struct CupidTiming* timing = &gba->timing;

    /** Reset PSG. */
    memset(&audio->psg.ch1, 0, sizeof(audio->psg.ch1));
    memset(&audio->psg.ch2, 0, sizeof(audio->psg.ch2));
    memset(&audio->psg.ch3, 0, sizeof(audio->psg.ch3));
    memset(&audio->psg.ch4, 0, sizeof(audio->psg.ch4));
    audio->psg.ch4.lfsr = 0;

    audio->psg.volumeRight = 0;
    audio->psg.volumeLeft  = 0;
    audio->psg.ch1Right = audio->psg.ch2Right = audio->psg.ch3Right = audio->psg.ch4Right = false;
    audio->psg.ch1Left  = audio->psg.ch2Left  = audio->psg.ch3Left  = audio->psg.ch4Left  = false;
    audio->psg.playingCh1 = audio->psg.playingCh2 = false;
    audio->psg.playingCh3 = audio->psg.playingCh4 = false;
    audio->psg.frame = 0;
    audio->psg.skipFrame = false;
    audio->psg.enable = false;
    audio->psg.timingFactor = GBA_AUDIO_TIMING_FACTOR;

    /** Schedule frame and sample events at time 0. */
    cupid_timing_deschedule(timing, &audio->psg.frameEvent);
    cupid_timing_deschedule(timing, &audio->sampleEvent);
    cupid_timing_schedule(timing, &audio->psg.frameEvent, 0);
    cupid_timing_schedule(timing, &audio->sampleEvent, 512);

    /** Reset FIFO A. */
    audio->chA.fifoWrite = 0;
    audio->chA.fifoRead  = 0;
    audio->chA.fifoCount = 0;
    audio->chA.internalSample = 0;
    audio->chA.internalRemaining = 0;
    audio->chA.latchedSample = 0;
    memset(audio->chA.fifo, 0, sizeof(audio->chA.fifo));

    /** Reset FIFO B. */
    audio->chB.fifoWrite = 0;
    audio->chB.fifoRead  = 0;
    audio->chB.fifoCount = 0;
    audio->chB.internalSample = 0;
    audio->chB.internalRemaining = 0;
    audio->chB.latchedSample = 0;
    memset(audio->chB.fifo, 0, sizeof(audio->chB.fifo));

    /** SOUNDBIAS default = 0x200. */
    audio->soundbias = 0x200;

    /** SOUNDCNT_H fields. */
    audio->volume     = 0;
    audio->volumeChA  = false;
    audio->volumeChB  = false;
    audio->chARight   = false;
    audio->chALeft    = false;
    audio->chATimer   = false;
    audio->chBRight   = false;
    audio->chBLeft    = false;
    audio->chBTimer   = false;
    audio->enable     = false;

    audio->soundcntL  = 0;
    audio->soundcntH  = 0;

    audio->lastSample   = 0;
    audio->nextSample = 512;
    audio->sampleOrigin = 0;

    /** Reset sample interval to default. */
    audio->sampleInterval = GBA_CLOCK_FREQ / 0x8000;  /* 512 */

    /** Clear output ring buffer. */
    audio->sampleWritePos = 0;
    audio->sampleReadPos  = 0;
    audio->sampleOverruns = 0;
    memset(audio->sampleBuffer, 0, sizeof(audio->sampleBuffer));
    memset(audio->sampleClocks, 0, sizeof(audio->sampleClocks));
    memset(audio->currentSamples, 0, sizeof(audio->currentSamples));

    _audioDebugSampleCbCount = 0;
    _audioDebugLog(audio, "reset: bias=0x%03X sampleInterval=%d", audio->soundbias & 0x3FF, (int)audio->sampleInterval);
}

/**
 * @brief Audio tick stub.
 *
 * Audio timing is event-driven, so this function currently performs no work.
 * It exists to satisfy the main-loop interface.
 *
 * @param audio  Audio subsystem state.
 * @param cycles CPU cycles elapsed since the last tick.
 */
void cupid_audio_tick(struct CupidAudio* audio, int32_t cycles) {
    (void)audio;
    (void)cycles;
}

/**
 * @brief Advance one or more PSG channels to a target timestamp.
 *
 * The @p channels bitmask selects which channels to run: bit 0 = ch1,
 * bit 1 = ch2, bit 2 = ch3, bit 3 = ch4.
 *
 * @param psg       PSG state.
 * @param timestamp Target timestamp in CPU cycles.
 * @param channels  Bitmask selecting which PSG channels to advance.
 */
void cupid_audio_psg_run(struct CupidAudioPSG* psg, uint64_t timestamp,
                         int channels)
{
    if (!psg->enable) return;

    int timingFactor = psg->timingFactor;

    /** Channel 1 (Square + Sweep). */
    if (channels & 0x1) {
        struct CupidAudioSquareChannel* ch = &psg->ch1;
        if (psg->playingCh1 && ch->envelope.dead != 2) {
            int32_t period = 4 * (2048 - ch->control.frequency) * timingFactor;
            if (period > 0) {
                uint64_t diff = timestamp >= ch->lastUpdate ? timestamp - ch->lastUpdate : 0;
                if (diff >= (uint64_t)period) {
                    uint64_t steps = diff / period;
                    ch->index = (ch->index + (uint8_t)steps) & 7;
                    ch->lastUpdate += steps * period;
                }
            }
            _updateSquareSample(ch);
        }
    }

    /** Channel 2 (Square). */
    if (channels & 0x2) {
        struct CupidAudioSquareChannel* ch = &psg->ch2;
        if (psg->playingCh2 && ch->envelope.dead != 2) {
            int32_t period = 4 * (2048 - ch->control.frequency) * timingFactor;
            if (period > 0) {
                uint64_t diff = timestamp >= ch->lastUpdate ? timestamp - ch->lastUpdate : 0;
                if (diff >= (uint64_t)period) {
                    uint64_t steps = diff / period;
                    ch->index = (ch->index + (uint8_t)steps) & 7;
                    ch->lastUpdate += steps * period;
                }
            }
            _updateSquareSample(ch);
        }
    }

    /** Channel 3 (Wave). */
    if (channels & 0x4) {
        struct CupidAudioWaveChannel* ch3 = &psg->ch3;
        if (psg->playingCh3) {
            int32_t cycles = 2 * (2048 - ch3->rate) * timingFactor;
            if (cycles > 0) {
                uint64_t diff = timestamp - ch3->nextUpdate;
                if (timestamp >= ch3->nextUpdate) {
                    uint64_t steps = diff / cycles + 1;

                    /** GBA mode: rotate through wavedata. */
                    int start, end;
                    if (ch3->size) {
                        /** 64-sample mode: all 8 words. */
                        start = 0;
                        end = 8;
                    } else if (ch3->bank) {
                        /** Bank 1: words 4-7. */
                        start = 4;
                        end = 8;
                    } else {
                        /** Bank 0: words 0-3. */
                        start = 0;
                        end = 4;
                    }

                    int8_t sample = 0;
                    for (uint64_t s = 0; s < steps; s++) {
                        /**
                         * Rotate nibbles through the wavedata array.
                         * The first byte's high nibble is read first. Its
                         * value wraps into the final byte's low nibble.
                         */
                        uint32_t bitsCarry = ch3->wavedata32[start] & 0xF0;
                        uint32_t old;

                        for (int j = end - 1; j >= start; j--) {
                            old = ch3->wavedata32[j];
                            ch3->wavedata32[j] =
                                ((old & 0x0F0F0F0F) << 4) |
                                ((old & 0xF0F0F000) >> 12) |
                                (bitsCarry << 20);
                            bitsCarry = old & 0xF0;
                        }
                        sample = (int8_t)(bitsCarry >> 4);
                    }

                    /** Apply volume. */
                    int vol = ch3->volume;
                    if (vol == 0) {
                        sample >>= 4;  /* mute */
                    } else if (vol > 3) {
                        /** GBA 75% mode: sample * 3 / 4. */
                        sample = (int8_t)(sample * 3);
                        sample >>= 2;
                    } else {
                        /** vol 1 = 100% (shift 0), vol 2 = 50% (shift 1), vol 3 = 25% (shift 2). */
                        static const int volShift[4] = { 4, 0, 1, 2 };
                        sample >>= volShift[vol];
                    }
                    ch3->sample = sample;
                    ch3->nextUpdate = timestamp + cycles - (diff % cycles);
                    ch3->readable = true;
                }
            }
        }
    }

    /** Channel 4 (Noise). */
    if (channels & 0x8) {
        struct CupidAudioNoiseChannel* ch4 = &psg->ch4;
        if (psg->playingCh4) {
            int ratio = ch4->ratio;
            int freq  = ch4->frequency;
            int32_t noiseCycles = ((ratio ? 2 * ratio : 1) << freq) *
                                   8 * timingFactor;
            if (noiseCycles > 0) {
                uint64_t diff = timestamp >= ch4->lastEvent ? timestamp - ch4->lastEvent : 0;
                if (diff >= (uint64_t)noiseCycles) {
                    uint64_t steps = diff / noiseCycles;
                    ch4->lastEvent += steps * noiseCycles;

                    uint32_t lfsr = ch4->lfsr;
                    uint32_t coeff = ch4->power ? 0x4040 : 0x4000;
                    int lsb = 0;

                    for (uint64_t i = 0; i < steps; i++) {
                        lsb = ((int)lfsr ^ ((int)lfsr >> 1) ^ 1) & 1;
                        lfsr >>= 1;
                        if (lsb) {
                            lfsr |= coeff;
                        } else {
                            lfsr &= ~coeff;
                        }
                    }
                    ch4->lfsr = lfsr & 0x7FFF;
                    ch4->sample = (int8_t)(lsb * ch4->envelope.currentVolume);
                }
            }
        }
    }
}

/**
 * @brief Mix the current PSG output into left and right sample values.
 *
 * Applies per-channel routing and master PSG left/right volume to produce a
 * stereo sample pair.
 *
 * @param psg   PSG state.
 * @param left  Output pointer for the mixed left sample.
 * @param right Output pointer for the mixed right sample.
 */
void cupid_audio_psg_sample(struct CupidAudioPSG* psg,
                            int16_t* left, int16_t* right)
{
    int16_t sampleLeft  = 0;
    int16_t sampleRight = 0;

    /** DC offset: 0 for GBA mode. */
    int dcOffset = 0;

    /** Channels 1-4: samples are 0..15, shift left 3 for mixing. */
    if (psg->playingCh1 && !psg->forceDisableCh[0]) {
        if (psg->ch1Left)  sampleLeft  += psg->ch1.sample;
        if (psg->ch1Right) sampleRight += psg->ch1.sample;
    }
    if (psg->playingCh2 && !psg->forceDisableCh[1]) {
        if (psg->ch2Left)  sampleLeft  += psg->ch2.sample;
        if (psg->ch2Right) sampleRight += psg->ch2.sample;
    }
    if (psg->playingCh3 && !psg->forceDisableCh[2]) {
        if (psg->ch3Left)  sampleLeft  += psg->ch3.sample;
        if (psg->ch3Right) sampleRight += psg->ch3.sample;
    }

    sampleLeft  += dcOffset;
    sampleRight += dcOffset;
    sampleLeft  *= 8;
    sampleRight *= 8;

    /** Ch4: added separately after shift. */
    if (psg->playingCh4 && !psg->forceDisableCh[3]) {
        int16_t noiseSample = (int16_t)(psg->ch4.sample * 8);
        if (psg->ch4Left)  sampleLeft  += noiseSample;
        if (psg->ch4Right) sampleRight += noiseSample;
    }

    /** Apply NR50 volume (0-7 → multiply by 1-8). */
    sampleLeft  *= (int16_t)(1 + psg->volumeLeft);
    sampleRight *= (int16_t)(1 + psg->volumeRight);

    *left  = sampleLeft;
    *right = sampleRight;
}

/**
 * @brief Step the PSG frame sequencer.
 *
 * Runs the 512 Hz frame sequencer logic, updating length counters, channel 1
 * sweep, and envelopes according to the current frame phase.
 *
 * @param psg PSG state.
 */
void cupid_audio_psg_frame_step(struct CupidAudioPSG* psg) {
    if (!psg->enable) return;
    if (psg->skipFrame) {
        psg->skipFrame = false;
        return;
    }

    psg->frame = (psg->frame + 1) & 7;

    /** Frames 0, 2, 4, 6: length counters. */
    if (!(psg->frame & 1)) {
        /** Ch1 length. */
        if (psg->ch1.control.length > 0 && psg->ch1.control.stop) {
            if (--psg->ch1.control.length == 0) {
                psg->playingCh1 = false;
            }
        }
        /** Ch2 length. */
        if (psg->ch2.control.length > 0 && psg->ch2.control.stop) {
            if (--psg->ch2.control.length == 0) {
                psg->playingCh2 = false;
            }
        }
        /** Ch3 length. */
        if (psg->ch3.length > 0 && psg->ch3.stop) {
            if (--psg->ch3.length == 0) {
                psg->playingCh3 = false;
            }
        }
        /** Ch4 length. */
        if (psg->ch4.length > 0 && psg->ch4.stop) {
            if (--psg->ch4.length == 0) {
                psg->playingCh4 = false;
            }
        }
    }

    /** Frames 2, 6: sweep (ch1 only). */
    if (psg->frame == 2 || psg->frame == 6) {
        if (psg->ch1.sweep.enable) {
            if (psg->ch1.sweep.step > 0) {
                --psg->ch1.sweep.step;
            }
            if (psg->ch1.sweep.step == 0) {
                if (!_updateSweep(&psg->ch1, false)) {
                    psg->playingCh1 = false;
                }
            }
        }
    }

    /** Frame 7: envelope (ch1, ch2, ch4). */
    if (psg->frame == 7) {
        /** Ch1 envelope. */
        if (psg->playingCh1 && psg->ch1.envelope.dead == 0) {
            if (psg->ch1.envelope.nextStep > 0) {
                --psg->ch1.envelope.nextStep;
            }
            if (psg->ch1.envelope.nextStep == 0) {
                _updateEnvelope(&psg->ch1.envelope);
            }
            _updateSquareSample(&psg->ch1);
        }
        /** Ch2 envelope. */
        if (psg->playingCh2 && psg->ch2.envelope.dead == 0) {
            if (psg->ch2.envelope.nextStep > 0) {
                --psg->ch2.envelope.nextStep;
            }
            if (psg->ch2.envelope.nextStep == 0) {
                _updateEnvelope(&psg->ch2.envelope);
            }
            _updateSquareSample(&psg->ch2);
        }
        /** Ch4 envelope. */
        if (psg->playingCh4 && psg->ch4.envelope.dead == 0) {
            int oldSample = psg->ch4.sample;
            if (psg->ch4.envelope.nextStep > 0) {
                --psg->ch4.envelope.nextStep;
            }
            if (psg->ch4.envelope.nextStep == 0) {
                _updateEnvelope(&psg->ch4.envelope);
            }
            psg->ch4.sample = (int8_t)((oldSample > 0) *
                               psg->ch4.envelope.currentVolume);
        }
    }
}

/** @section audio_fifo_ops FIFO operations */
/* Queue reset is independent of the word currently shifting into the mixer.
 * Clear old storage lanes as well as occupancy so a later narrow write cannot
 * recover samples discarded by reset or overflow. */
static void clear_fifo_queue(struct CupidAudioFIFO* fifo) {
    fifo->fifoWrite = fifo->fifoRead = fifo->fifoCount = 0;
    memset(fifo->fifo, 0, sizeof(fifo->fifo));
}

/**
 * @brief Enqueue one word, or discard queued storage on an overflowing write.
 *
 * @param fifo  FIFO state to update.
 * @param value 32-bit packed sample word to enqueue.
 */
void cupid_audio_fifo_write(struct CupidAudioFIFO* fifo, uint32_t value) {
    /* The eighth queued write resets storage and is itself discarded. The
     * independent byte stage and held output keep their current phase. */
    if (fifo->fifoCount == GBA_AUDIO_FIFO_SIZE) {
        clear_fifo_queue(fifo);
        return;
    }
    fifo->fifo[fifo->fifoWrite] = value;
    fifo->fifoWrite = (fifo->fifoWrite + 1) % GBA_AUDIO_FIFO_SIZE;
    ++fifo->fifoCount;
}

/**
 * @brief Reset a Direct Sound FIFO channel.
 *
 * Clears queued words, FIFO indices and the internal staged sample byte state.
 *
 * @param fifo FIFO state to reset.
 */
void cupid_audio_fifo_reset(struct CupidAudioFIFO* fifo) {
    clear_fifo_queue(fifo);
    fifo->internalSample = 0;
    fifo->internalRemaining = 0;
}

/**
 * @brief Consume one Direct Sound FIFO byte on timer overflow.
 *
 * Captures due mixer values before replacing the byte held by this channel,
 * pops from its staged word, and requests DMA refill at the existing threshold.
 *
 * @param audio  Audio subsystem state.
 * @param fifoId FIFO selector: 0 for channel A, 1 for channel B.
 */
void cupid_audio_sample_fifo(struct CupidAudio* audio, int fifoId, uint64_t timestamp) {
    /* Capture the old hold value before this timer changes either FIFO latch. */
    cupid_audio_sample(audio, timestamp);
    struct CupidAudioFIFO* channel;
    if (fifoId == 0) {
        channel = &audio->chA;
    } else {
        channel = &audio->chB;
    }

    /** Compute FIFO fill level. */
    int fifoSize = channel->fifoCount;

    /** Sample the queued words before loading the independent byte stage.
     * Loading a stage from four queued words must not request a burst yet. */
    if (fifoSize <= 3) {
        struct CupidGBA* gba = audio->gba;
        /* The request wires are fixed: FIFO A drives DMA1 and FIFO B drives
         * DMA2. A destination write never reroutes either request line, even
         * when the accepted burst writes the other FIFO or ordinary memory. */
        int dmaCh = fifoId ? 2 : 1;
        struct CupidDMA* dma = &gba->memory.dma[dmaCh];
        if (dma->enable && dma->timing == DMA_TIMING_CUSTOM) {
            _audioDebugLog(audio, "fifo%d dma refill: dma=%d fifoSize=%d", fifoId, dmaCh, fifoSize);
            cupid_dma_request_fifo(gba, dmaCh, timestamp);
        }
    }

    /** Pop next 32-bit word into internal buffer if empty. */
    if (channel->internalRemaining == 0) {
        if (fifoSize > 0) {
            channel->internalSample = channel->fifo[channel->fifoRead];
            channel->internalRemaining = 4;
            channel->fifoRead = (channel->fifoRead + 1) % GBA_AUDIO_FIFO_SIZE;
            --channel->fifoCount;
            --fifoSize;
            _audioDebugLog(audio, "fifo%d pop word=0x%08X remainingWords=%d", fifoId,
                channel->internalSample, fifoSize);
        }
    }

    int8_t currentByte = (int8_t)(channel->internalSample & 0xFF);
    channel->latchedSample = currentByte;
    _audioDebugLog(audio, "fifo%d byte=%d", fifoId, (int)currentByte);

    /** Advance to next byte in the internal 4-byte buffer. */
    if (channel->internalRemaining > 0) {
        channel->internalSample >>= 8;
        --channel->internalRemaining;
    }
}

/**
 * @brief Handle a timer overflow for Direct Sound playback.
 *
 * Routes timer 0 or timer 1 overflows to FIFO A and/or FIFO B depending on
 * the current SOUNDCNT_H timer selection bits.
 *
 * @param audio Audio subsystem state.
 * @param timer Timer index that overflowed.
 */
void cupid_audio_timer_overflow(struct CupidAudio* audio, int timer, uint64_t timestamp) {
    if (!audio->enable) return;

    _audioDebugLog(audio, "timer overflow: timer=%d (A:t%d B:t%d)", timer,
        audio->chATimer ? 1 : 0, audio->chBTimer ? 1 : 0);

    if (!audio->chATimer && timer == 0) {
        /** Channel A uses timer 0 (chATimer == false means timer 0). */
        cupid_audio_sample_fifo(audio, 0, timestamp);
    } else if (audio->chATimer && timer == 1) {
        /** Channel A uses timer 1. */
        cupid_audio_sample_fifo(audio, 0, timestamp);
    }

    if (!audio->chBTimer && timer == 0) {
        /** Channel B uses timer 0. */
        cupid_audio_sample_fifo(audio, 1, timestamp);
    } else if (audio->chBTimer && timer == 1) {
        /** Channel B uses timer 1. */
        cupid_audio_sample_fifo(audio, 1, timestamp);
    }
}

/** @section audio_apply_bias Apply SOUNDBIAS and master volume scaling */
static int16_t _applyBias(struct CupidAudio* audio, int32_t sample) {
    int32_t bias = audio->soundbias & 0x3FF;
    sample += bias;
    if (sample >= 0x400) {
        sample = 0x3FF;
    } else if (sample < 0) {
        sample = 0;
    }
    /* The DAC midpoint is fixed. Removing the programmed bias here would
     * erase its output shift and could wrap valid levels in signed PCM. */
    return (int16_t)(((sample - 0x200) * audio->masterVolume * 3) >> 4);
}

/**
 * @brief Capture every pending mixer deadline up to @p timestamp.
 *
 * Advances the PSG state, mixes PSG and FIFO channels, applies bias and master
 * volume, and appends stereo frames with their elapsed clocks to the ring.
 *
 * @param audio     Audio subsystem state.
 * @param timestamp Target timestamp in CPU cycles.
 */
void cupid_audio_sample(struct CupidAudio* audio, uint64_t timestamp) {
    while (audio->nextSample <= timestamp) {
        uint64_t sampleClock = audio->nextSample;
        /** PSG volume shift: SOUNDCNT_H bits 0-1. */
        int psgShift = 4 - audio->volume;
        if (psgShift < 0) psgShift = 0;
        if (psgShift > 4) psgShift = 4;

        /** Run PSG channels to this capture deadline. */
        cupid_audio_psg_run(&audio->psg, sampleClock, 0xF);

        /** Read PSG mixed output. */
        int16_t psgLeft, psgRight;
        cupid_audio_psg_sample(&audio->psg, &psgLeft, &psgRight);

        int32_t sampleLeft  = psgLeft >> psgShift;
        int32_t sampleRight = psgRight >> psgShift;

        /** Mix FIFO A. */
        {
            int8_t fifoSample = audio->chA.latchedSample;
            int32_t scaled = (int32_t)fifoSample * 4;
            if (!audio->volumeChA) scaled >>= 1;  /* 50% */
            if (audio->chALeft)  sampleLeft  += scaled;
            if (audio->chARight) sampleRight += scaled;
        }

        /** Mix FIFO B. */
        {
            int8_t fifoSample = audio->chB.latchedSample;
            int32_t scaled = (int32_t)fifoSample * 4;
            if (!audio->volumeChB) scaled >>= 1;  /* 50% */
            if (audio->chBLeft)  sampleLeft  += scaled;
            if (audio->chBRight) sampleRight += scaled;
        }

        /** Apply bias and master volume. */
        audio->currentSamples[0].left = audio->enable ? _applyBias(audio, sampleLeft) : 0;
        audio->currentSamples[0].right = audio->enable ? _applyBias(audio, sampleRight) : 0;
        int wp = audio->sampleWritePos;
        audio->sampleBuffer[wp] = audio->currentSamples[0].left;
        audio->sampleBuffer[wp + 1] = audio->currentSamples[0].right;
        audio->sampleClocks[wp / 2] = (uint16_t)(sampleClock - audio->lastSample);
        int next = (wp + 2) % (GBA_AUDIO_SAMPLES * 2);
        if (next == audio->sampleReadPos) {
            audio->sampleReadPos = (audio->sampleReadPos + 2) % (GBA_AUDIO_SAMPLES * 2);
            ++audio->sampleOverruns;
        }
        audio->sampleWritePos = next;
        audio->lastSample = sampleClock;
        /* Keep an already queued deadline through a resolution write, then
         * align later captures to the selected divider. */
        uint64_t interval = (unsigned)audio->sampleInterval;
        audio->nextSample = sampleClock + interval -
            ((sampleClock - audio->sampleOrigin) & (interval - 1));
        if (audio->sampleSink) audio->sampleSink(audio->sampleSinkContext, sampleClock,
            audio->currentSamples[0].left, audio->currentSamples[0].right);
    }
}

/**
 * @brief Timing callback for the periodic audio sample event.
 *
 * Captures due frames and schedules the next divider deadline.
 *
 * @param timing     Timing scheduler.
 * @param context    Pointer to the owning audio subsystem.
 * @param cyclesLate Number of cycles by which the callback is late.
 */
static void _sampleCallback(struct CupidTiming* timing, void* context,
                             uint32_t cyclesLate)
{
    struct CupidAudio* audio = (struct CupidAudio*)context;

    uint64_t currentTime = timing->globalCycles - cyclesLate;
    cupid_audio_sample(audio, currentTime);

    ++_audioDebugSampleCbCount;
    if (_audioDebugEnabled && (_audioDebugSampleCbCount & 0xFF) == 0) {
        _audioDebugLog(audio,
            "sample cb #%u: interval=%d wr=%d rd=%d lastL=%d lastR=%d",
            _audioDebugSampleCbCount,
            audio->sampleInterval,
            audio->sampleWritePos,
            audio->sampleReadPos,
            audio->currentSamples[0].left,
            audio->currentSamples[0].right);
    }

    /** Reschedule. */
    cupid_timing_schedule_absolute(timing, &audio->sampleEvent,
                                   (int32_t)(uint32_t)audio->nextSample);
}

/**
 * @brief Timing callback for the PSG frame sequencer.
 *
 * Flushes pending audio, reschedules the frame event, and advances the PSG
 * frame sequencer state.
 *
 * @param timing     Timing scheduler.
 * @param context    Pointer to the owning audio subsystem.
 * @param cyclesLate Number of cycles by which the callback is late.
 */
static void _frameCallback(struct CupidTiming* timing, void* context,
                            uint32_t cyclesLate)
{
    struct CupidAudio* audio = (struct CupidAudio*)context;

    /** Flush pending audio samples up to now. */
    uint64_t currentTime = timing->globalCycles - cyclesLate;
    cupid_audio_sample(audio, currentTime);

    /** Reschedule frame event. */
    cupid_timing_schedule_absolute(timing, &audio->psg.frameEvent,
        (int32_t)(uint32_t)(currentTime + audio->psg.timingFactor * GBA_AUDIO_FRAME_CYCLES));

    /** Step the frame sequencer. */
    cupid_audio_psg_frame_step(&audio->psg);
}

/** @section audio_psg_helpers PSG helpers */

/**
 * @brief Recompute the current square-wave sample from duty and envelope.
 *
 * @param ch Square-wave channel state.
 */
static void _updateSquareSample(struct CupidAudioSquareChannel* ch) {
    ch->sample = (int8_t)(_squareChannelDuty[ch->envelope.duty][ch->index] *
                          ch->envelope.currentVolume);
}

/**
 * @brief Reset an envelope generator to its initial state.
 *
 * @param envelope Envelope state to reset.
 */
static void _resetEnvelope(struct CupidAudioEnvelope* envelope) {
    envelope->currentVolume = envelope->initialVolume;
    envelope->nextStep = envelope->stepTime;
    _updateEnvelopeDead(envelope);
}

/**
 * @brief Re-evaluate whether an envelope is active, saturated, or muted.
 *
 * @param envelope Envelope state to update.
 */
static void _updateEnvelopeDead(struct CupidAudioEnvelope* envelope) {
    if (!envelope->stepTime) {
        envelope->dead = envelope->currentVolume ? 1 : 2;
    } else if (!envelope->direction && envelope->currentVolume == 0) {
        envelope->dead = 2;
    } else if (envelope->direction && envelope->currentVolume == 0xF) {
        envelope->dead = 1;
    } else {
        /** Was dead, now revived (GBA style: reload nextStep). */
        if (envelope->dead) {
            envelope->nextStep = envelope->stepTime;
        }
        envelope->dead = 0;
    }
}

/**
 * @brief Decode an NRx2-style envelope register into envelope state.
 *
 * @param envelope Envelope state to modify.
 * @param value    Register value to decode.
 * @return @c true if the DAC remains enabled, @c false otherwise.
 */
static bool _writeEnvelope(struct CupidAudioEnvelope* envelope, uint8_t value) {
    envelope->stepTime      = value & 0x7;
    envelope->direction     = (value >> 3) & 1;
    envelope->initialVolume = (value >> 4) & 0xF;
    _updateEnvelopeDead(envelope);
    return envelope->initialVolume || envelope->direction;
}

/**
 * @brief Advance an envelope by one step.
 *
 * @param envelope Envelope state to update.
 */
static void _updateEnvelope(struct CupidAudioEnvelope* envelope) {
    if (envelope->direction) {
        ++envelope->currentVolume;
        if (envelope->currentVolume >= 15) {
            envelope->currentVolume = 15;
            envelope->dead = 1;
            return;
        }
    } else {
        --envelope->currentVolume;
        if (envelope->currentVolume <= 0) {
            envelope->currentVolume = 0;
            envelope->dead = 2;
            return;
        }
    }
    envelope->nextStep = envelope->stepTime;
}

/**
 * @brief Apply one channel-1 frequency sweep calculation.
 *
 * @param ch      Channel 1 square-wave state.
 * @param initial Whether this is the initial overflow-check pass.
 * @return @c true if the resulting frequency remains valid, @c false if the
 *         channel should be disabled.
 */
static bool _updateSweep(struct CupidAudioSquareChannel* ch, bool initial) {
    struct CupidAudioSweep* sweep = &ch->sweep;

    if (initial || sweep->time != 8) {
        int frequency = sweep->realFrequency;
        if (sweep->direction) {
            /** Subtract. */
            frequency -= frequency >> sweep->shift;
            if (!initial && frequency >= 0) {
                ch->control.frequency = frequency;
                sweep->realFrequency = frequency;
            }
        } else {
            /** Add. */
            frequency += frequency >> sweep->shift;
            if (frequency < 2048) {
                if (!initial && sweep->shift > 0) {
                    ch->control.frequency = frequency;
                    sweep->realFrequency = frequency;
                    /** Double-check: recursive call to test overflow. */
                    if (!_updateSweep(ch, true)) {
                        return false;
                    }
                }
            }
        }
        if (frequency >= 2048) {
            return false;
        }
    }

    sweep->occurred = true;
    sweep->step = sweep->time;
    return true;
}

/**
 * @brief Reset the sweep unit timing state.
 *
 * @param sweep Sweep state to reset.
 */
static void _resetSweep(struct CupidAudioSweep* sweep) {
    sweep->step = sweep->time;
    sweep->enable = (sweep->step != 8) || sweep->shift;
    sweep->occurred = false;
}

/** @section audio_psg_reg_writes NRxx PSG register write helpers */

/**
 * @brief Handle a write to `NR10` / `SOUND1CNT_L`.
 *
 * @param psg   PSG state.
 * @param value Low-byte register value.
 */
static void _writeNR10(struct CupidAudioPSG* psg, uint8_t value) {
    struct CupidAudioSweep* sweep = &psg->ch1.sweep;

    bool oldDirection = sweep->direction;

    sweep->shift     = value & 0x7;
    sweep->direction = (value >> 3) & 1;
    sweep->time      = (value >> 4) & 0x7;
    if (sweep->time == 0) sweep->time = 8;

    /** If negate was used and direction changed from subtract to add, disable. */
    if (sweep->occurred && oldDirection && !sweep->direction) {
        psg->playingCh1 = false;
    }
    sweep->occurred = false;
}

/**
 * @brief Handle a write to `NR11`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR11(struct CupidAudioPSG* psg, uint8_t value) {
    psg->ch1.envelope.length = value & 0x3F;
    psg->ch1.envelope.duty   = (value >> 6) & 0x3;
    psg->ch1.control.length  = 64 - psg->ch1.envelope.length;
}

/**
 * @brief Handle a write to `NR12`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR12(struct CupidAudioPSG* psg, uint8_t value) {
    if (!_writeEnvelope(&psg->ch1.envelope, value)) {
        psg->playingCh1 = false;
    }
}

/**
 * @brief Handle a write to `NR13`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR13(struct CupidAudioPSG* psg, uint8_t value) {
    psg->ch1.control.frequency &= 0x700;
    psg->ch1.control.frequency |= value;
}

/**
 * @brief Handle a write to `NR14`.
 *
 * @param psg       PSG state.
 * @param value     Register value.
 * @param timestamp Current audio timestamp.
 */
static void _writeNR14(struct CupidAudioPSG* psg, uint8_t value,
                       uint64_t timestamp)
{
    struct CupidAudioSquareChannel* ch = &psg->ch1;

    /** Upper 3 bits of frequency. */
    ch->control.frequency &= 0xFF;
    ch->control.frequency |= (value & 0x7) << 8;

    /** Stop (length enable). */
    bool oldStop = ch->control.stop;
    ch->control.stop = (value >> 6) & 1;

    /** Extra length counter tick on stop transition. */
    if (!oldStop && ch->control.stop && ch->control.length > 0 &&
        !(psg->frame & 1)) {
        --ch->control.length;
        if (ch->control.length == 0) {
            psg->playingCh1 = false;
        }
    }

    /** Trigger (restart). */
    if (value & 0x80) {
        _resetEnvelope(&ch->envelope);
        bool dacOn = ch->envelope.initialVolume || ch->envelope.direction;

        ch->sweep.realFrequency = ch->control.frequency;
        _resetSweep(&ch->sweep);

        /** If shift nonzero, do initial sweep check. */
        if (ch->sweep.shift) {
            if (!_updateSweep(ch, true)) {
                psg->playingCh1 = false;
                return;
            }
        }

        /** Reload length if zero. */
        if (ch->control.length == 0) {
            ch->control.length = 64;
            if (ch->control.stop && !(psg->frame & 1)) {
                --ch->control.length;
            }
        }

        ch->lastUpdate = timestamp;
        _updateSquareSample(ch);

        if (dacOn) {
            psg->playingCh1 = true;
        } else {
            psg->playingCh1 = false;
        }
    }
}

/**
 * @brief Handle a write to `NR21`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR21(struct CupidAudioPSG* psg, uint8_t value) {
    psg->ch2.envelope.length = value & 0x3F;
    psg->ch2.envelope.duty   = (value >> 6) & 0x3;
    psg->ch2.control.length  = 64 - psg->ch2.envelope.length;
}

/**
 * @brief Handle a write to `NR22`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR22(struct CupidAudioPSG* psg, uint8_t value) {
    if (!_writeEnvelope(&psg->ch2.envelope, value)) {
        psg->playingCh2 = false;
    }
}

/**
 * @brief Handle a write to `NR23`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR23(struct CupidAudioPSG* psg, uint8_t value) {
    psg->ch2.control.frequency &= 0x700;
    psg->ch2.control.frequency |= value;
}

/**
 * @brief Handle a write to `NR24`.
 *
 * @param psg       PSG state.
 * @param value     Register value.
 * @param timestamp Current audio timestamp.
 */
static void _writeNR24(struct CupidAudioPSG* psg, uint8_t value,
                       uint64_t timestamp)
{
    struct CupidAudioSquareChannel* ch = &psg->ch2;

    ch->control.frequency &= 0xFF;
    ch->control.frequency |= (value & 0x7) << 8;

    bool oldStop = ch->control.stop;
    ch->control.stop = (value >> 6) & 1;

    if (!oldStop && ch->control.stop && ch->control.length > 0 &&
        !(psg->frame & 1)) {
        --ch->control.length;
        if (ch->control.length == 0) {
            psg->playingCh2 = false;
        }
    }

    if (value & 0x80) {
        _resetEnvelope(&ch->envelope);
        bool dacOn = ch->envelope.initialVolume || ch->envelope.direction;

        if (ch->control.length == 0) {
            ch->control.length = 64;
            if (ch->control.stop && !(psg->frame & 1)) {
                --ch->control.length;
            }
        }

        ch->lastUpdate = timestamp;
        _updateSquareSample(ch);

        if (dacOn) {
            psg->playingCh2 = true;
        } else {
            psg->playingCh2 = false;
        }
    }
}

/**
 * @brief Handle a write to `NR30`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR30(struct CupidAudioPSG* psg, uint8_t value) {
    psg->ch3.enable = (value >> 7) & 1;
    if (!psg->ch3.enable) {
        psg->playingCh3 = false;
    }
}

/**
 * @brief Handle a write to `NR31`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR31(struct CupidAudioPSG* psg, uint8_t value) {
    psg->ch3.length = 256 - value;
}

/**
 * @brief Handle a write to `NR33`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR33(struct CupidAudioPSG* psg, uint8_t value) {
    psg->ch3.rate &= 0x700;
    psg->ch3.rate |= value;
}

/**
 * @brief Handle a write to `NR34`.
 *
 * @param psg       PSG state.
 * @param value     Register value.
 * @param timestamp Current audio timestamp.
 */
static void _writeNR34(struct CupidAudioPSG* psg, uint8_t value,
                       uint64_t timestamp)
{
    struct CupidAudioWaveChannel* ch3 = &psg->ch3;

    ch3->rate &= 0xFF;
    ch3->rate |= (value & 0x7) << 8;

    bool oldStop = ch3->stop;
    ch3->stop = (value >> 6) & 1;

    if (!oldStop && ch3->stop && ch3->length > 0 && !(psg->frame & 1)) {
        --ch3->length;
        if (ch3->length == 0) {
            psg->playingCh3 = false;
        }
    }

    if (value & 0x80) {
        /** Trigger. */
        if (ch3->length == 0) {
            ch3->length = 256;
            if (ch3->stop && !(psg->frame & 1)) {
                --ch3->length;
            }
        }

        ch3->window = 0;
        ch3->readable = true;

        int32_t cycles = 2 * (2048 - ch3->rate) * psg->timingFactor;
        ch3->nextUpdate = timestamp + (6 * psg->timingFactor) + cycles;

        if (ch3->enable) {
            psg->playingCh3 = true;
        }
    }
}

/**
 * @brief Handle a write to `NR41`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR41(struct CupidAudioPSG* psg, uint8_t value) {
    psg->ch4.length = 64 - (value & 0x3F);
}

/**
 * @brief Handle a write to `NR42`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR42(struct CupidAudioPSG* psg, uint8_t value) {
    if (!_writeEnvelope(&psg->ch4.envelope, value)) {
        psg->playingCh4 = false;
    }
}

/**
 * @brief Handle a write to `NR43`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR43(struct CupidAudioPSG* psg, uint8_t value) {
    psg->ch4.ratio     = value & 0x7;
    psg->ch4.power     = (value >> 3) & 1;
    psg->ch4.frequency = (value >> 4) & 0xF;
}

/**
 * @brief Handle a write to `NR44`.
 *
 * @param psg       PSG state.
 * @param value     Register value.
 * @param timestamp Current audio timestamp.
 */
static void _writeNR44(struct CupidAudioPSG* psg, uint8_t value,
                       uint64_t timestamp)
{
    bool oldStop = psg->ch4.stop;
    psg->ch4.stop = (value >> 6) & 1;

    if (!oldStop && psg->ch4.stop && psg->ch4.length > 0 &&
        !(psg->frame & 1)) {
        --psg->ch4.length;
        if (psg->ch4.length == 0) {
            psg->playingCh4 = false;
        }
    }

    if (value & 0x80) {
        _resetEnvelope(&psg->ch4.envelope);
        bool dacOn = psg->ch4.envelope.initialVolume || psg->ch4.envelope.direction;

        psg->ch4.lfsr = 0;

        if (psg->ch4.length == 0) {
            psg->ch4.length = 64;
            if (psg->ch4.stop && !(psg->frame & 1)) {
                --psg->ch4.length;
            }
        }

        psg->ch4.lastEvent = timestamp;

        if (dacOn) {
            psg->playingCh4 = true;
        }
    }
}

/**
 * @brief Handle a write to `NR50`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR50(struct CupidAudioPSG* psg, uint8_t value) {
    psg->volumeRight = value & 0x7;
    psg->volumeLeft  = (value >> 4) & 0x7;
}

/**
 * @brief Handle a write to `NR51`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR51(struct CupidAudioPSG* psg, uint8_t value) {
    psg->ch1Right = (value >> 0) & 1;
    psg->ch2Right = (value >> 1) & 1;
    psg->ch3Right = (value >> 2) & 1;
    psg->ch4Right = (value >> 3) & 1;
    psg->ch1Left  = (value >> 4) & 1;
    psg->ch2Left  = (value >> 5) & 1;
    psg->ch3Left  = (value >> 6) & 1;
    psg->ch4Left  = (value >> 7) & 1;
}

/**
 * @brief Handle a write to `NR52`.
 *
 * @param psg   PSG state.
 * @param value Register value.
 */
static void _writeNR52(struct CupidAudioPSG* psg, uint8_t value) {
    bool wasEnabled = psg->enable;
    psg->enable = (value >> 7) & 1;

    if (!psg->enable) {
        psg->playingCh1 = false;
        psg->playingCh2 = false;
        psg->playingCh3 = false;
        psg->playingCh4 = false;

        /** Sound mode registers reset; both waveform banks remain stored. */
        uint8_t wave[32];
        memcpy(wave, psg->ch3.wavedata8, sizeof(wave));
        memset(&psg->ch1, 0, sizeof(psg->ch1));
        memset(&psg->ch2, 0, sizeof(psg->ch2));
        memset(&psg->ch3, 0, sizeof(psg->ch3));
        memset(&psg->ch4, 0, sizeof(psg->ch4));
        memcpy(psg->ch3.wavedata8, wave, sizeof(wave));

        psg->volumeLeft = 0;
        psg->volumeRight = 0;

        /** Clear routing. */
        psg->ch1Right = psg->ch2Right = psg->ch3Right = psg->ch4Right = false;
        psg->ch1Left  = psg->ch2Left  = psg->ch3Left  = psg->ch4Left  = false;
    } else if (!wasEnabled) {
        /** Re-enabled: reset frame counter. */
        psg->frame = 7;
    }
}

/**
 * @brief Handle a 16-bit write to an audio I/O register.
 *
 * Flushes pending audio to the current timestamp, updates the relevant PSG,
 * FIFO, or mixer state, and mirrors the value into I/O memory for later reads.
 *
 * @param audio Audio subsystem state.
 * @param addr  I/O register offset.
 * @param val   16-bit value being written.
 */
void cupid_audio_write_register(struct CupidAudio* audio, uint32_t addr,
                                uint16_t val)
{
    if (addr >= REG_SOUND1CNT_L && addr < REG_SOUNDCNT_H && !audio->enable) return;
    struct CupidAudioPSG* psg = &audio->psg;
    uint64_t timestamp = cupid_timing_global_time(&audio->gba->timing);

    /** Flush pending audio. */
    cupid_audio_sample(audio, timestamp);
    if (addr >= REG_SOUND1CNT_L && addr < REG_SOUNDCNT_L)
        cupid_audio_psg_run(psg, timestamp, 1 << ((addr - REG_SOUND1CNT_L) >> 3));

    _audioDebugLog(audio, "reg write: addr=0x%03X val=0x%04X", (unsigned)addr, val);

    switch (addr) {

    /** Channel 1 (Square + Sweep). */
    case REG_SOUND1CNT_L:
        _writeNR10(psg, (uint8_t)val);
        break;

    case REG_SOUND1CNT_H:
        _writeNR11(psg, (uint8_t)(val & 0xFF));
        _writeNR12(psg, (uint8_t)(val >> 8));
        break;

    case REG_SOUND1CNT_X:
        _writeNR13(psg, (uint8_t)(val & 0xFF));
        _writeNR14(psg, (uint8_t)(val >> 8), timestamp);
        break;

    /** Channel 2 (Square). */
    case REG_SOUND2CNT_L:
        _writeNR21(psg, (uint8_t)(val & 0xFF));
        _writeNR22(psg, (uint8_t)(val >> 8));
        break;

    case REG_SOUND2CNT_H:
        _writeNR23(psg, (uint8_t)(val & 0xFF));
        _writeNR24(psg, (uint8_t)(val >> 8), timestamp);
        break;

    /** Channel 3 (Wave). */
    case REG_SOUND3CNT_L:
        /** GBA-specific: size and bank select from the 16-bit value. */
        psg->ch3.size = (val >> 5) & 1;
        psg->ch3.bank = (val >> 6) & 1;
        _writeNR30(psg, (uint8_t)(val >> 0));
        break;

    case REG_SOUND3CNT_H:
        _writeNR31(psg, (uint8_t)(val & 0xFF));
        /** GBA volume = bits 15:13 of the 16-bit reg = bits 7:5 of high byte. */
        psg->ch3.volume = (val >> 13) & 0x7;
        break;

    case REG_SOUND3CNT_X:
        _writeNR33(psg, (uint8_t)(val & 0xFF));
        _writeNR34(psg, (uint8_t)(val >> 8), timestamp);
        break;

    /** Channel 4 (Noise). */
    case REG_SOUND4CNT_L:
        _writeNR41(psg, (uint8_t)(val & 0xFF));
        _writeNR42(psg, (uint8_t)(val >> 8));
        break;

    case REG_SOUND4CNT_H:
        _writeNR43(psg, (uint8_t)(val & 0xFF));
        _writeNR44(psg, (uint8_t)(val >> 8), timestamp);
        break;

    /** SOUNDCNT_L (NR50 + NR51). */
    case REG_SOUNDCNT_L:
        val &= 0xFF77;
        audio->soundcntL = val;
        _writeNR50(psg, (uint8_t)(val & 0xFF));
        _writeNR51(psg, (uint8_t)(val >> 8));
        break;

    /** SOUNDCNT_H (DMA sound control). */
    case REG_SOUNDCNT_H: {
        audio->soundcntH = val & 0x770F;
        audio->volume    = val & 0x3;
        audio->volumeChA = (val >> 2) & 1;
        audio->volumeChB = (val >> 3) & 1;
        audio->chARight  = (val >> 8) & 1;
        audio->chALeft   = (val >> 9) & 1;
        audio->chATimer  = (val >> 10) & 1;
        bool chAReset    = (val >> 11) & 1;
        audio->chBRight  = (val >> 12) & 1;
        audio->chBLeft   = (val >> 13) & 1;
        audio->chBTimer  = (val >> 14) & 1;
        bool chBReset    = (val >> 15) & 1;

        if (chAReset) {
            clear_fifo_queue(&audio->chA);
        }
        if (chBReset) {
            clear_fifo_queue(&audio->chB);
        }
        _audioDebugLog(audio,
            "SOUNDCNT_H: psgVol=%d A(vol=%d L=%d R=%d T=%d rst=%d) B(vol=%d L=%d R=%d T=%d rst=%d)",
            audio->volume,
            audio->volumeChA ? 100 : 50,
            audio->chALeft,
            audio->chARight,
            audio->chATimer ? 1 : 0,
            chAReset ? 1 : 0,
            audio->volumeChB ? 100 : 50,
            audio->chBLeft,
            audio->chBRight,
            audio->chBTimer ? 1 : 0,
            chBReset ? 1 : 0);
        val &= 0x770F;
        break;
    }

    /** SOUNDCNT_X (NR52 / master enable). */
    case REG_SOUNDCNT_X: {
        bool oldEnable = audio->enable;
        audio->enable = (val >> 7) & 1;
        _writeNR52(psg, (uint8_t)val);

        if (!audio->enable && oldEnable) {
            /* Master power discards queued words. The staged word and held
             * output have separate state and retain their current phase. */
            clear_fifo_queue(&audio->chA);
            clear_fifo_queue(&audio->chB);
            audio->soundcntL = 0;
            /** Disable: clear SOUNDCNT_H fields too. */
            audio->volume    = 0;
            audio->volumeChA = false;
            audio->volumeChB = false;

            /** Clear GBA-specific ch3 fields. */
            psg->ch3.size = false;
            psg->ch3.bank = false;
            psg->ch3.volume = 0;

            /** Zero IO regs from SOUND1CNT_L to just before SOUNDCNT_H. */
            uint16_t* io = audio->gba->memory.io;
            for (uint32_t r = REG_SOUND1CNT_L; r < REG_SOUNDCNT_H; r += 2) {
                io[r >> 1] = 0;
            }
        }
        _audioDebugLog(audio, "SOUNDCNT_X: enable=%d", audio->enable ? 1 : 0);
        break;
    }

    /** SOUNDBIAS. */
    case REG_SOUNDBIAS: {
        audio->soundbias = val;
        int resolution = (val >> 14) & 3;
        int32_t newInterval = 0x200 >> resolution;

        audio->sampleInterval = newInterval;
        _audioDebugLog(audio, "SOUNDBIAS: val=0x%04X bias=%d res=%d interval=%d",
            val,
            val & 0x3FF,
            resolution,
            (int)audio->sampleInterval);
        break;
    }

    /** Wave RAM. */
    case REG_WAVE_RAM0_LO: case REG_WAVE_RAM0_HI:
    case REG_WAVE_RAM1_LO: case REG_WAVE_RAM1_HI:
    case REG_WAVE_RAM2_LO: case REG_WAVE_RAM2_HI:
    case REG_WAVE_RAM3_LO: case REG_WAVE_RAM3_HI: {
        /** Run ch3 to current time before modifying wave RAM. */
        cupid_audio_psg_run(psg, timestamp, 0x4);

        /**
         * GBA: write to the non-selected bank.
         * If audio is disabled, write to bank 1.
         */
        unsigned bankOffset = audio->enable && psg->ch3.bank ? 0 : 16;
        unsigned byteOff = addr - REG_WAVE_RAM0_LO + bankOffset;
        psg->ch3.wavedata8[byteOff] = (uint8_t)val;
        psg->ch3.wavedata8[byteOff + 1] = (uint8_t)(val >> 8);

        /** Also mirror to I/O register space. */
        audio->gba->memory.io[addr >> 1] = val;
        break;
    }

    default:
        break;
    }

    /** Mirror to I/O register space for reads (except wave RAM handled above). */
    if (addr < REG_WAVE_RAM0_LO || addr > REG_WAVE_RAM3_HI) {
        audio->gba->memory.io[addr >> 1] = val;
    }
}

/* Byte accesses update only the addressed PSG field. Wave RAM merges against
 * the actual CPU-visible bank; mixer controls retain their masked shadow. */
void cupid_audio_write_register8(struct CupidAudio* audio, uint32_t addr, uint8_t val) {
    if (addr >= REG_SOUND1CNT_L && addr < REG_SOUNDCNT_H && !audio->enable) return;
    if (addr >= REG_WAVE_RAM0_LO && addr <= REG_WAVE_RAM3_HI + 1) {
        uint32_t half = addr & ~1u;
        uint16_t old = cupid_audio_read_register(audio, half);
        uint16_t merged = addr & 1 ? (old & 255) | ((uint16_t)val << 8)
                                  : (old & 0xFF00) | val;
        cupid_audio_write_register(audio, half, merged);
        return;
    }
    struct CupidAudioPSG* psg = &audio->psg;
    uint64_t timestamp = cupid_timing_global_time(&audio->gba->timing);
    cupid_audio_sample(audio, timestamp);
    if (addr >= REG_SOUND1CNT_L && addr < REG_SOUNDCNT_L)
        cupid_audio_psg_run(psg, timestamp, 1 << ((addr - REG_SOUND1CNT_L) >> 3));
    switch (addr) {
    case REG_SOUND1CNT_L: _writeNR10(psg, val); break;
    case REG_SOUND1CNT_H: _writeNR11(psg, val); break;
    case REG_SOUND1CNT_H + 1: _writeNR12(psg, val); break;
    case REG_SOUND1CNT_X: _writeNR13(psg, val); break;
    case REG_SOUND1CNT_X + 1: _writeNR14(psg, val, timestamp); break;
    case REG_SOUND2CNT_L: _writeNR21(psg, val); break;
    case REG_SOUND2CNT_L + 1: _writeNR22(psg, val); break;
    case REG_SOUND2CNT_H: _writeNR23(psg, val); break;
    case REG_SOUND2CNT_H + 1: _writeNR24(psg, val, timestamp); break;
    case REG_SOUND3CNT_L:
        psg->ch3.size = (val >> 5) & 1;
        psg->ch3.bank = (val >> 6) & 1;
        _writeNR30(psg, val);
        break;
    case REG_SOUND3CNT_H: _writeNR31(psg, val); break;
    case REG_SOUND3CNT_H + 1: psg->ch3.volume = (val >> 5) & 7; break;
    case REG_SOUND3CNT_X: _writeNR33(psg, val); break;
    case REG_SOUND3CNT_X + 1: _writeNR34(psg, val, timestamp); break;
    case REG_SOUND4CNT_L: _writeNR41(psg, val); break;
    case REG_SOUND4CNT_L + 1: _writeNR42(psg, val); break;
    case REG_SOUND4CNT_H: _writeNR43(psg, val); break;
    case REG_SOUND4CNT_H + 1: _writeNR44(psg, val, timestamp); break;
    case REG_SOUNDCNT_L: case REG_SOUNDCNT_L + 1:
    case REG_SOUNDCNT_H: case REG_SOUNDCNT_H + 1:
    case REG_SOUNDCNT_X:
    case REG_SOUNDBIAS: case REG_SOUNDBIAS + 1: {
        uint32_t half = addr & ~1u;
        uint16_t old = audio->gba->memory.io[half >> 1];
        uint16_t merged = addr & 1 ? (old & 255) | ((uint16_t)val << 8)
                                  : (old & 0xFF00) | val;
        cupid_audio_write_register(audio, half, merged);
        return;
    }
    default: return;
    }
    uint16_t* shadow = &audio->gba->memory.io[(addr & ~1u) >> 1];
    *shadow = addr & 1 ? (*shadow & 255) | ((uint16_t)val << 8)
                      : (*shadow & 0xFF00) | val;
}

/**
 * @brief Read the current value of an audio I/O register.
 *
 * Synthesises readable register state for PSG control registers and falls back
 * to mirrored I/O memory for all other addresses.
 *
 * @param audio Audio subsystem state.
 * @param addr  I/O register offset.
 * @return The 16-bit register value visible to software.
 */
uint16_t cupid_audio_read_register(struct CupidAudio* audio, uint32_t addr) {
    struct CupidAudioPSG* psg = &audio->psg;
    if (addr >= REG_WAVE_RAM0_LO && addr <= REG_WAVE_RAM3_HI) {
        uint64_t timestamp = cupid_timing_global_time(&audio->gba->timing);
        cupid_audio_sample(audio, timestamp);
        cupid_audio_psg_run(psg, timestamp, 0x4);
        unsigned bankOffset = audio->enable && psg->ch3.bank ? 0 : 16;
        unsigned byteOff = addr - REG_WAVE_RAM0_LO + bankOffset;
        return psg->ch3.wavedata8[byteOff] | ((uint16_t)psg->ch3.wavedata8[byteOff + 1] << 8);
    }

    switch (addr) {

    /** SOUND1CNT_L: only sweep bits readable (bits 6:0). */
    case REG_SOUND1CNT_L: {
        uint16_t v = 0;
        v |= psg->ch1.sweep.shift & 0x7;
        v |= (psg->ch1.sweep.direction ? 1 : 0) << 3;
        int time = psg->ch1.sweep.time;
        if (time >= 8) time = 0;
        v |= (time & 0x7) << 4;
        return v;
    }

    /** SOUND1CNT_H: duty + envelope fields (length not readable). */
    case REG_SOUND1CNT_H: {
        uint16_t v = 0;
        /** Low byte: only duty readable (bits 7:6), length is write-only. */
        v |= (psg->ch1.envelope.duty & 0x3) << 6;
        /** High byte: envelope. */
        v |= (uint16_t)(psg->ch1.envelope.stepTime & 0x7) << 8;
        v |= (uint16_t)(psg->ch1.envelope.direction ? 1 : 0) << 11;
        v |= (uint16_t)(psg->ch1.envelope.initialVolume & 0xF) << 12;
        return v;
    }

    /** SOUND1CNT_X: frequency not readable, only bit 14 (stop). */
    case REG_SOUND1CNT_X:
        return (psg->ch1.control.stop ? 1 : 0) << 14;

    /** SOUND2CNT_L: same layout as SOUND1CNT_H. */
    case REG_SOUND2CNT_L: {
        uint16_t v = 0;
        v |= (psg->ch2.envelope.duty & 0x3) << 6;
        v |= (uint16_t)(psg->ch2.envelope.stepTime & 0x7) << 8;
        v |= (uint16_t)(psg->ch2.envelope.direction ? 1 : 0) << 11;
        v |= (uint16_t)(psg->ch2.envelope.initialVolume & 0xF) << 12;
        return v;
    }

    /** SOUND2CNT_H: only stop bit readable. */
    case REG_SOUND2CNT_H:
        return (psg->ch2.control.stop ? 1 : 0) << 14;

    /** SOUND3CNT_L: bank + size + enable. */
    case REG_SOUND3CNT_L: {
        uint16_t v = 0;
        v |= (psg->ch3.size ? 1 : 0) << 5;
        v |= (psg->ch3.bank ? 1 : 0) << 6;
        v |= (psg->ch3.enable ? 1 : 0) << 7;
        return v;
    }

    /** SOUND3CNT_H: length (write-only), volume readable. */
    case REG_SOUND3CNT_H:
        return ((uint16_t)(psg->ch3.volume & 0x7)) << 13;

    /** SOUND3CNT_X: only stop bit readable. */
    case REG_SOUND3CNT_X:
        return (psg->ch3.stop ? 1 : 0) << 14;

    /** SOUND4CNT_L: envelope fields. */
    case REG_SOUND4CNT_L: {
        uint16_t v = 0;
        /** Length not readable. */
        v |= (uint16_t)(psg->ch4.envelope.stepTime & 0x7) << 8;
        v |= (uint16_t)(psg->ch4.envelope.direction ? 1 : 0) << 11;
        v |= (uint16_t)(psg->ch4.envelope.initialVolume & 0xF) << 12;
        return v;
    }

    /** SOUND4CNT_H: noise params + stop. */
    case REG_SOUND4CNT_H: {
        uint16_t v = 0;
        v |= psg->ch4.ratio & 0x7;
        v |= (psg->ch4.power ? 1 : 0) << 3;
        v |= (psg->ch4.frequency & 0xF) << 4;
        v |= (psg->ch4.stop ? 1 : 0) << 14;
        return v;
    }

    /** SOUNDCNT_L: NR50 + NR51. */
    case REG_SOUNDCNT_L:
        return audio->soundcntL;

    /** SOUNDCNT_H: DMA sound control. */
    case REG_SOUNDCNT_H:
        return audio->soundcntH;

    /** SOUNDCNT_X: master enable + channel playing status. */
    case REG_SOUNDCNT_X: {
        uint16_t v = 0;
        if (psg->playingCh1) v |= (1 << 0);
        if (psg->playingCh2) v |= (1 << 1);
        if (psg->playingCh3) v |= (1 << 2);
        if (psg->playingCh4) v |= (1 << 3);
        if (audio->enable)   v |= (1 << 7);
        return v;
    }

    /** SOUNDBIAS. */
    case REG_SOUNDBIAS:
        return audio->soundbias;

    default:
        return audio->gba->memory.io[addr >> 1];
    }
}
