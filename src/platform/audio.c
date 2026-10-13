/**
 * @file audio.c
 * @brief SDL2 audio output for the platform layer.
 *
 * Outputs stereo S16 audio at the host rate. Captured frame durations preserve
 * the emulated timeline when the sound sampling resolution changes.
 */

#include "cupid/platform.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief Requested host output sample rate.
 */
#define CUPID_HOST_SAMPLE_RATE   48000

/**
 * @brief Ring-buffer helpers.
 *
 * The emulator writes stereo-interleaved int16_t samples into
 * audio->sampleBuffer[0 .. GBA_AUDIO_SAMPLES*2 - 1]
 * with a clock duration for each captured frame.
 */

static inline int ring_available(int writePos, int readPos) {
    int cap = GBA_AUDIO_SAMPLES * 2;
    return (writePos - readPos + cap) % cap;
}

/**
 * @brief Peek a sample in the ring buffer at a stereo-frame offset.
 */
static inline int16_t ring_peek(const int16_t* buf, int readPos, int frame, int channel) {
    int cap = GBA_AUDIO_SAMPLES * 2;
    int idx = (readPos + frame * 2 + channel) % cap;
    return buf[idx];
}

/**
 * @brief SDL audio callback.
 *
 * Produces `len` bytes of stereo S16 output at the host rate by linear
 * interpolation of the emulator sample ring buffer.
 */

void cupid_platform_audio_callback(void* userdata, uint8_t* stream, int len) {
    struct CupidPlatformAudio* pa = (struct CupidPlatformAudio*)userdata;
    struct CupidAudio* audio = pa->audio;
    if (pa->sampleOverruns != audio->sampleOverruns) {
        pa->sampleOverruns = audio->sampleOverruns;
        pa->resamplePos = 0;
    }
    int16_t* out = (int16_t*)stream;
    int outSamples = len / (int)sizeof(int16_t);         /** total int16 values */
    int outFrames  = outSamples / 2;                      /** stereo frames */

    double step = (double)GBA_CLOCK_FREQ / (double)pa->hostRate;
    pa->resampleStep  = step;
    double srcPos     = pa->resamplePos;    /** fractional position in source */

    int readPos = audio->sampleReadPos;
    int avail = ring_available(audio->sampleWritePos, readPos);
    int availFrames = avail / 2;   /** stereo frames available */

    int framesWritten = 0;
    int16_t lastL = pa->lastSampleL;
    int16_t lastR = pa->lastSampleR;

    for (int i = 0; i < outFrames; i++) {
        int duration = 512;
        while (availFrames > 1) {
            int next = (readPos + 2) % (GBA_AUDIO_SAMPLES * 2);
            duration = audio->sampleClocks[next / 2];
            if (duration <= 0) duration = audio->sampleInterval > 0 ? audio->sampleInterval : 512;
            if (srcPos < duration) break;
            srcPos -= duration;
            readPos = next;
            --availFrames;
        }
        if (availFrames < 2) break;
        double frac = srcPos / duration;

        for (int ch = 0; ch < 2; ch++) {
            int16_t s0 = ring_peek(audio->sampleBuffer, readPos, 0, ch);
            int16_t s1 = ring_peek(audio->sampleBuffer, readPos, 1, ch);
            /** Linear interpolation. */
            double v = s0 + (s1 - s0) * frac;
            int32_t clamped = (int32_t)v;
            if (clamped >  32767) clamped =  32767;
            if (clamped < -32768) clamped = -32768;
            out[i * 2 + ch] = (int16_t)clamped;
        }

        lastL = out[i * 2];
        lastR = out[i * 2 + 1];
        srcPos += step;
        framesWritten++;
    }

    /**
     * On underrun, fade from the last good sample toward zero.
     * This avoids a hard click from a sudden jump to silence.
     */
    if (framesWritten < outFrames) {
        int remaining = outFrames - framesWritten;
        for (int i = 0; i < remaining; i++) {
            /** Exponential decay: multiply by 255/256 each sample. */
            lastL = (int16_t)((int32_t)lastL * 255 / 256);
            lastR = (int16_t)((int32_t)lastR * 255 / 256);
            out[(framesWritten + i) * 2]     = lastL;
            out[(framesWritten + i) * 2 + 1] = lastR;
        }
    }

    pa->lastSampleL = lastL;
    pa->lastSampleR = lastR;

    audio->sampleReadPos = readPos;
    pa->resamplePos = srcPos;
}

/**
 * @brief Initialize and deinitialize platform audio output.
 */

bool cupid_platform_init_audio(struct CupidPlatform* plat, struct CupidGBA* gba) {
    struct CupidPlatformAudio* pa = &plat->platformAudio;
    memset(pa, 0, sizeof(*pa));
    pa->audio = &gba->audio;

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq     = CUPID_HOST_SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = 1024;
    want.callback = cupid_platform_audio_callback;
    want.userdata = pa;

    plat->audioDevice = SDL_OpenAudioDevice(
        NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (plat->audioDevice == 0) {
        fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        return false;
    }

    pa->hostRate = have.freq;
    pa->resampleStep = (double)GBA_CLOCK_FREQ / (double)have.freq;
    pa->resamplePos  = 0.0;

    printf("CUPID: Audio opened — host %d Hz, %.6f clocks per output frame\n",
           have.freq, pa->resampleStep);

    /** Start playback. */
    SDL_PauseAudioDevice(plat->audioDevice, 0);
    return true;
}
