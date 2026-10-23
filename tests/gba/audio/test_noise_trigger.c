/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static const uint64_t origins[] = {0, INT32_MAX - 8u, UINT32_MAX - 8u,
                                  UINT64_C(0x300000000) - 8};
static const unsigned states[] = {0, 1, 2, 0x7F, 0x80, 0x7FFF};
static unsigned scenes;

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    return gba;
}

static void start(struct CupidGBA* gba, uint64_t origin, unsigned state,
                  unsigned oldVolume, unsigned width) {
    gba->timing.globalCycles = origin;
    gba->timing.masterCycles = (uint32_t)origin;
    gba->audio.lastSample = gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + UINT64_C(0x10000000000);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    cupid_io_write(gba, REG_SOUND4CNT_L, 0xF000);
    cupid_io_write(gba, REG_SOUND4CNT_H, (uint16_t)(0x8000 | (width << 3)));
    gba->audio.psg.ch4.lfsr = state;
    gba->audio.psg.ch4.sample = (int8_t)((state & 1) * oldVolume);
    gba->audio.psg.ch4.envelope.currentVolume = (int)oldVolume;
}

static void trigger(struct CupidGBA* gba, unsigned volume, unsigned width,
                    unsigned access, unsigned stop) {
    /* Zero increasing volume keeps the DAC enabled. */
    uint8_t envelope = (uint8_t)(volume ? volume << 4 : 8);
    if (access == 1) {
        cupid_audio_write_register8(&gba->audio, REG_SOUND4CNT_L + 1, envelope);
        cupid_audio_write_register8(&gba->audio, REG_SOUND4CNT_H, (uint8_t)(width << 3));
        cupid_audio_write_register8(&gba->audio, REG_SOUND4CNT_H + 1, (uint8_t)(0x80 | (stop << 6)));
    } else {
        cupid_io_write(gba, REG_SOUND4CNT_L, (uint16_t)(envelope << 8));
        cupid_io_write(gba, REG_SOUND4CNT_H, (uint16_t)(0x8000 | (stop << 14) | (width << 3)));
    }
}

static void active_retrigger_reloads_volume(void) {
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < 4; ++epoch)
    for (unsigned state = 0; state < sizeof(states) / sizeof(states[0]); ++state)
    for (unsigned old = 0; old < 16; ++old)
    for (unsigned volume = 0; volume < 16; ++volume)
    for (unsigned width = 0; width < 2; ++width)
    for (unsigned access = 1; access <= 2; ++access)
    for (unsigned stop = 0; stop < 2; ++stop) {
        start(gba, origins[epoch], states[state], old, width);
        gba->audio.psg.ch4.length = stop ? 1 : 32;
        gba->audio.psg.frame = 0;
        trigger(gba, volume, width, access, stop);
        struct CupidAudioNoiseChannel* ch = &gba->audio.psg.ch4;
        CHECK(gba->audio.psg.playingCh4);
        CHECK(ch->envelope.currentVolume == (int)volume);
        CHECK(ch->sample == (int)((states[state] & 1) * volume));
        CHECK(ch->lfsr == 0 && ch->lastEvent == origins[epoch]);
        CHECK(ch->remaining == 32 && ch->lastUpdate == origins[epoch]);
        ++scenes;
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void inactive_restart_clears_old_amplitude(void) {
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < 4; ++epoch)
    for (unsigned state = 0; state < sizeof(states) / sizeof(states[0]); ++state)
    for (unsigned old = 0; old < 16; ++old)
    for (unsigned volume = 0; volume < 16; ++volume)
    for (unsigned width = 0; width < 2; ++width)
    for (unsigned access = 1; access <= 2; ++access) {
        start(gba, origins[epoch], states[state], old, width);
        /* DAC shutdown gates playback but can leave a cached amplitude. */
        cupid_audio_write_register8(&gba->audio, REG_SOUND4CNT_L + 1, 0);
        CHECK(!gba->audio.psg.playingCh4);
        trigger(gba, volume, width, access, 0);
        CHECK(gba->audio.psg.playingCh4);
        CHECK(gba->audio.psg.ch4.envelope.currentVolume == (int)volume);
        CHECK(gba->audio.psg.ch4.sample == 0);
        CHECK(gba->audio.psg.ch4.lfsr == 0);
        ++scenes;
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void mixer_observes_reloaded_amplitude(void) {
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < 4; ++epoch)
    for (unsigned old = 0; old < 16; ++old)
    for (unsigned volume = 0; volume < 16; ++volume)
    for (unsigned width = 0; width < 2; ++width)
    for (unsigned access = 1; access <= 2; ++access) {
        start(gba, origins[epoch], 1, old, width);
        cupid_io_write(gba, REG_SOUNDCNT_L, 0x8877);
        cupid_io_write(gba, REG_SOUNDCNT_H, 2);
        trigger(gba, volume, width, access, 0);
        int16_t left = 0, right = 0;
        cupid_audio_psg_sample(&gba->audio.psg, &left, &right);
        CHECK(left == right && left == (int16_t)(volume * 64));
        /* Reload timing stays separate: no new polynomial edge before 32. */
        cupid_audio_psg_run(&gba->audio.psg, origins[epoch] + 31, 8);
        CHECK(gba->audio.psg.ch4.sample == (int)volume);
        cupid_audio_psg_run(&gba->audio.psg, origins[epoch] + 32, 8);
        CHECK(gba->audio.psg.ch4.sample == 0);
        ++scenes;
    }
    cupid_gba_destroy(gba);
    free(gba);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "active")) RUN(active_retrigger_reloads_volume);
    else if (!strcmp(argv[1], "inactive")) RUN(inactive_restart_clears_old_amplitude);
    else if (!strcmp(argv[1], "mixer")) RUN(mixer_observes_reloaded_amplitude);
    else CHECK(false);
    printf("PASS %u noise-trigger scenes\n", scenes);
    return 0;
}
