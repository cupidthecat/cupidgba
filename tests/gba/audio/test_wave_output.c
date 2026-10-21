/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static const uint64_t origins[] = {0, INT32_MAX - 8u, UINT32_MAX - 8u,
                                  UINT64_C(0x300000000) - 8};
static unsigned scenes;

static unsigned scale(unsigned raw, unsigned volume) {
    return volume > 3 ? raw * 3 / 4 : volume ? raw >> (volume - 1) : 0;
}

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    return gba;
}

static void clock_at(struct CupidGBA* gba, uint64_t clock) {
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
}

static void start(struct CupidGBA* gba, uint64_t origin, unsigned raw,
                  unsigned volume, unsigned bank, unsigned size) {
    clock_at(gba, origin);
    gba->audio.lastSample = gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + UINT64_C(0x10000000000);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    cupid_io_write(gba, REG_SOUND3CNT_L, (uint16_t)(0x80 | (bank << 6) | (size << 5)));
    cupid_io_write(gba, REG_SOUND3CNT_H, (uint16_t)(volume << 13));
    memset(gba->audio.psg.ch3.wavedata8, (int)(raw * 17), 32);
    cupid_io_write(gba, REG_SOUND3CNT_X, 0x87FF);
    cupid_audio_psg_run(&gba->audio.psg, origin + 32, 4);
    clock_at(gba, origin + 33);
    CHECK(gba->audio.psg.ch3.sample == (int)scale(raw, volume));
    CHECK(gba->audio.psg.ch3.nextUpdate == origin + 40);
}

static void write_volume(struct CupidGBA* gba, unsigned volume, unsigned access) {
    if (access == 1) cupid_io_write8(gba, REG_SOUND3CNT_H + 1, (uint8_t)(volume << 5));
    else cupid_io_write(gba, REG_SOUND3CNT_H, (uint16_t)((volume << 13) | 0x23));
}

static void volume_writes_rescale_the_held_sample(void) {
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < 4; ++epoch)
    for (unsigned raw = 0; raw < 16; ++raw)
    for (unsigned old = 0; old < 8; ++old)
    for (unsigned next = 0; next < 8; ++next)
    for (unsigned bank = 0; bank < 2; ++bank)
    for (unsigned size = 0; size < 2; ++size)
    for (unsigned access = 1; access <= 2; ++access) {
        start(gba, origins[epoch], raw, old, bank, size);
        struct CupidAudioWaveChannel* ch = &gba->audio.psg.ch3;
        uint8_t ram[32];
        memcpy(ram, ch->wavedata8, sizeof(ram));
        int length = ch->length;
        write_volume(gba, next, access);
        CHECK(ch->sample == (int)scale(raw, next));
        CHECK(ch->volume == (int)next);
        CHECK(ch->length == (access == 1 ? length : 221));
        CHECK(ch->nextUpdate == origins[epoch] + 40);
        CHECK(!memcmp(ram, ch->wavedata8, sizeof(ram)));
        /* Repeated mute/unmute writes must retain the unscaled nibble. */
        write_volume(gba, 0, access);
        CHECK(ch->sample == 0);
        write_volume(gba, next, access);
        CHECK(ch->sample == (int)scale(raw, next));
        ++scenes;
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void stopped_trigger_rescales_the_retained_sample(void) {
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < 4; ++epoch)
    for (unsigned raw = 0; raw < 16; ++raw)
    for (unsigned old = 0; old < 8; ++old)
    for (unsigned next = 0; next < 8; ++next)
    for (unsigned bank = 0; bank < 2; ++bank)
    for (unsigned access = 1; access <= 2; ++access) {
        start(gba, origins[epoch], raw, old, bank, 0);
        struct CupidAudioWaveChannel* ch = &gba->audio.psg.ch3;
        cupid_io_write(gba, REG_SOUND3CNT_L, (uint16_t)(bank << 6));
        CHECK(!gba->audio.psg.playingCh3);
        int held = ch->sample;
        write_volume(gba, next, access);
        CHECK(!gba->audio.psg.playingCh3 && ch->sample == held);
        cupid_io_write(gba, REG_SOUND3CNT_L, (uint16_t)(0x80 | (bank << 6)));
        cupid_io_write(gba, REG_SOUND3CNT_X, 0x87FF);
        CHECK(gba->audio.psg.playingCh3);
        CHECK(ch->sample == (int)scale(raw, next));
        CHECK(ch->nextUpdate == origins[epoch] + 65);
        ++scenes;
    }
    cupid_gba_destroy(gba);
    free(gba);
}

/* A byte-oriented rotation oracle keeps RAM checks separate from word packing. */
static unsigned rotate(uint8_t* ram, unsigned bank, unsigned size) {
    unsigned first = size ? 0 : bank * 16, count = size ? 32 : 16;
    uint8_t copy[32];
    memcpy(copy, ram + first, count);
    for (unsigned i = 0; i < count; ++i)
        ram[first + i] = (uint8_t)((copy[i] << 4) | (copy[(i + 1) % count] >> 4));
    return copy[0] >> 4;
}

static void clocks_and_mixer_preserve_wave_rotation(void) {
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < 4; ++epoch)
    for (unsigned pattern = 0; pattern < 16; ++pattern)
    for (unsigned bank = 0; bank < 2; ++bank)
    for (unsigned size = 0; size < 2; ++size)
    for (unsigned access = 1; access <= 2; ++access) {
        uint64_t origin = origins[epoch];
        start(gba, origin, 0, 0, bank, size);
        struct CupidAudioWaveChannel* ch = &gba->audio.psg.ch3;
        uint8_t expected[32];
        for (unsigned i = 0; i < sizeof(expected); ++i) expected[i] = (uint8_t)(pattern * 17 + i * 29);
        memcpy(ch->wavedata8, expected, sizeof(expected));
        cupid_io_write(gba, REG_SOUNDCNT_L, 0x4477);
        for (unsigned step = 0; step < 128; ++step) {
            uint64_t edge = origin + 40 + step * 8;
            unsigned volume = step & 7;
            clock_at(gba, edge - 1);
            write_volume(gba, volume, access);
            CHECK(ch->nextUpdate == edge);
            cupid_audio_psg_run(&gba->audio.psg, edge, 4);
            unsigned raw = rotate(expected, bank, size);
            CHECK(ch->sample == (int)scale(raw, volume));
            CHECK(!memcmp(ch->wavedata8, expected, sizeof(expected)));
            CHECK(ch->nextUpdate == edge + 8);
            clock_at(gba, edge + 1);
            unsigned next = (volume + 3) & 7;
            write_volume(gba, next, access);
            int16_t left = 0, right = 0;
            cupid_audio_psg_sample(&gba->audio.psg, &left, &right);
            CHECK(left == right && left == (int16_t)(scale(raw, next) * 64));
            CHECK(!memcmp(ch->wavedata8, expected, sizeof(expected)));
            ++scenes;
        }
    }
    cupid_gba_destroy(gba);
    free(gba);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "writes")) RUN(volume_writes_rescale_the_held_sample);
    else if (!strcmp(argv[1], "trigger")) RUN(stopped_trigger_rescales_the_retained_sample);
    else if (!strcmp(argv[1], "timeline")) RUN(clocks_and_mixer_preserve_wave_rotation);
    else CHECK(false);
    printf("PASS %u wave-output scenes\n", scenes);
    return 0;
}
