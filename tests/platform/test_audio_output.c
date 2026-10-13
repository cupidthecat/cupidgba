/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/platform.h"
#include "../test.h"
#undef main

static struct CupidGBA* ramp(int start, bool varied) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    gba->audio.sampleInterval = 64; /* Different from most queued durations. */
    gba->audio.sampleReadPos = start;
    unsigned clock = 0;
    for (unsigned frame = 0; frame < 96; frame++) {
        unsigned interval = varied && frame % 3 == 0 ? 64 : 512;
        if (frame) clock += interval;
        int pos = (start + (int)frame * 2) % (GBA_AUDIO_SAMPLES * 2);
        gba->audio.sampleBuffer[pos] = (int16_t)(clock / 2);
        gba->audio.sampleBuffer[pos + 1] = -(int16_t)(clock / 2);
        gba->audio.sampleClocks[pos / 2] = (uint16_t)interval;
    }
    gba->audio.sampleWritePos = (start + 96 * 2) % (GBA_AUDIO_SAMPLES * 2);
    return gba;
}

static void interpolation_follows_captured_clocks_through_ring_wrap(void) {
    const int starts[] = {0, GBA_AUDIO_SAMPLES * 2 - 8};
    for (unsigned varied = 0; varied < 2; varied++) {
        for (unsigned start = 0; start < 2; start++) {
            struct CupidGBA* gba = ramp(starts[start], varied != 0);
            struct CupidPlatformAudio output = {.audio = &gba->audio, .hostRate = 48000};
            int16_t pcm[32];
            cupid_platform_audio_callback(&output, (uint8_t*)pcm, sizeof(pcm));
            /* The input is the exact linear function value = clock / 2. */
            for (unsigned frame = 0; frame < 16; frame++) {
                int16_t expected = (int16_t)((double)frame * GBA_CLOCK_FREQ / 96000.0);
                CHECK(pcm[frame * 2] == expected && pcm[frame * 2 + 1] == -expected);
            }
            free(gba);
        }
    }
}

static void callback_partitioning_preserves_pcm_and_clock_position(void) {
    struct CupidGBA* whole = ramp(0, true);
    struct CupidGBA* split = ramp(0, true);
    struct CupidPlatformAudio a = {.audio = &whole->audio, .hostRate = 48000};
    struct CupidPlatformAudio b = {.audio = &split->audio, .hostRate = 48000};
    int16_t first[42], second[42];
    cupid_platform_audio_callback(&a, (uint8_t*)first, sizeof(first));
    const unsigned blocks[] = {1, 3, 2, 7, 8};
    unsigned cursor = 0;
    for (unsigned i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        cupid_platform_audio_callback(&b, (uint8_t*)(second + cursor * 2), (int)blocks[i] * 4);
        cursor += blocks[i];
    }
    CHECK(cursor == 21 && !memcmp(first, second, sizeof(first)));
    CHECK(a.resamplePos == b.resamplePos && whole->audio.sampleReadPos == split->audio.sampleReadPos);
    free(whole);
    free(split);
}

static void underrun_keeps_the_last_valid_anchor(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    gba->audio.sampleInterval = 64;
    gba->audio.sampleBuffer[0] = 100;
    gba->audio.sampleBuffer[2] = 200;
    gba->audio.sampleClocks[1] = 64;
    gba->audio.sampleWritePos = 4;
    struct CupidPlatformAudio output = {.audio = &gba->audio, .hostRate = 48000};
    int16_t pcm[2];
    cupid_platform_audio_callback(&output, (uint8_t*)pcm, sizeof(pcm));
    CHECK(pcm[0] == 100);
    cupid_platform_audio_callback(&output, (uint8_t*)pcm, sizeof(pcm));
    CHECK(pcm[0] == 99 && gba->audio.sampleReadPos == 2);
    /* New data follows the retained anchor; the consumer did not skip it. */
    gba->audio.sampleBuffer[4] = 300;
    gba->audio.sampleClocks[2] = 512;
    gba->audio.sampleWritePos = 6;
    cupid_platform_audio_callback(&output, (uint8_t*)pcm, sizeof(pcm));
    CHECK(pcm[0] == 255 && gba->audio.sampleReadPos == 2);
    free(gba);
}

static void producer_overrun_resets_the_interpolation_anchor(void) {
    struct CupidGBA* gba = ramp(0, true);
    gba->audio.sampleOverruns = 2;
    struct CupidPlatformAudio output = {.audio = &gba->audio, .hostRate = 48000, .resamplePos = 400};
    int16_t pcm[2];
    cupid_platform_audio_callback(&output, (uint8_t*)pcm, sizeof(pcm));
    CHECK(pcm[0] == 0 && pcm[1] == 0);
    CHECK(output.sampleOverruns == 2);
    free(gba);
}

static void live_device_consumes_output_between_producer_frames(void) {
    CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    SDL_SetMainReady();
    CHECK(SDL_Init(SDL_INIT_AUDIO) == 0);
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    gba->cpu.halted = true;
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    cupid_io_write(gba, REG_SOUNDCNT_H, 0x0304);
    cupid_memory_store32(&gba->cpu, GBA_BASE_IO + REG_FIFO_A_LO, 0x40404040, NULL);
    cupid_audio_timer_overflow(&gba->audio, 0, 0);
    struct CupidPlatform platform = {0};
    CHECK(cupid_platform_init_audio(&platform, gba));
    for (unsigned frame = 0; frame < 24; frame++) {
        SDL_LockAudioDevice(platform.audioDevice);
        cupid_gba_run_frame(gba);
        SDL_UnlockAudioDevice(platform.audioDevice);
        SDL_Delay(2);
    }
    SDL_PauseAudioDevice(platform.audioDevice, 1);
    CHECK(platform.platformAudio.lastSampleL == 12288);
    CHECK(platform.platformAudio.lastSampleR == 12288);
    CHECK(platform.platformAudio.resamplePos > 0);
    SDL_CloseAudioDevice(platform.audioDevice);
    SDL_Quit();
    cupid_gba_destroy(gba);
    free(gba);
}

int main(void) {
    RUN(interpolation_follows_captured_clocks_through_ring_wrap);
    RUN(callback_partitioning_preserves_pcm_and_clock_position);
    RUN(underrun_keeps_the_last_valid_anchor);
    RUN(producer_overrun_resets_the_interpolation_anchor);
    RUN(live_device_consumes_output_between_producer_frames);
    return 0;
}
