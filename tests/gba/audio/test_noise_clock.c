/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static const uint64_t origins[] = {0, INT32_MAX - 8u, UINT32_MAX - 8u, UINT64_C(0x300000000) - 8};
static unsigned scenes;

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    return gba;
}

static unsigned period(unsigned rate) {
    unsigned ratio = rate & 7;
    return (ratio ? 64 * ratio : 32) << (rate >> 4);
}

/* Compute expectations through the complementary XOR representation. */
static uint32_t step(uint32_t stored, unsigned shortMode) {
    uint32_t bits = stored ^ 0x7FFF;
    unsigned feedback = (bits ^ (bits >> 1)) & 1;
    bits = (bits >> 1) | (feedback << 14);
    if (shortMode) bits = (bits & ~0x40u) | (feedback << 6);
    return bits ^ 0x7FFF;
}

static void clock_at(struct CupidGBA* gba, uint64_t clock) {
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
}

static void start(struct CupidGBA* gba, uint64_t origin, unsigned rate) {
    clock_at(gba, origin);
    gba->audio.lastSample = gba->audio.sampleOrigin = origin;
    gba->audio.nextSample = origin + UINT64_C(0x10000000000);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    cupid_io_write(gba, REG_SOUND4CNT_L, 0xF000);
    cupid_io_write(gba, REG_SOUND4CNT_H, (uint16_t)(0x8000 | rate));
}

static void write_rate(struct CupidGBA* gba, unsigned rate, unsigned width) {
    if (width == 1) cupid_audio_write_register8(&gba->audio, REG_SOUND4CNT_H, (uint8_t)rate);
    else cupid_io_write(gba, REG_SOUND4CNT_H, (uint16_t)rate);
}

static void observe(struct CupidGBA* gba, uint64_t clock, uint32_t state,
                    unsigned volume, uint64_t edge) {
    cupid_audio_psg_run(&gba->audio.psg, clock, 8);
    CHECK(gba->audio.psg.ch4.lfsr == state);
    CHECK(gba->audio.psg.ch4.sample == (int)((state & 1) * volume));
    CHECK(gba->audio.psg.ch4.lastEvent == edge);
}

static void polynomial_states(void) {
    static const unsigned volumes[] = {0, 1, 7, 15};
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(origins[0]); ++epoch) {
        for (unsigned shortMode = 0; shortMode < 2; ++shortMode) {
            for (unsigned state = 0; state < 0x8000; ++state) {
                for (unsigned v = 0; v < sizeof(volumes) / sizeof(volumes[0]); ++v) {
                    uint64_t origin = origins[epoch];
                    start(gba, origin, shortMode << 3);
                    gba->audio.psg.ch4.lfsr = state;
                    gba->audio.psg.ch4.envelope.currentVolume = (int)volumes[v];
                    gba->audio.psg.ch4.sample = (int8_t)((state & 1) * volumes[v]);
                    uint32_t expected = step(state, shortMode);
                    observe(gba, origin + 32, expected, volumes[v], origin + 32);
                    ++scenes;
                }
            }
        }
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void register_reloads(void) {
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(origins[0]); ++epoch) {
        for (unsigned old = 0; old < 112; ++old) {
            unsigned oldRate = ((old / 8) << 4) | (old & 7);
            for (unsigned next = 0; next < 112; ++next) {
                unsigned nextRate = ((next / 8) << 4) | (next & 7);
                for (unsigned oldMode = 0; oldMode < 2; ++oldMode) {
                    for (unsigned newMode = 0; newMode < 2; ++newMode) {
                        for (unsigned fraction = 0; fraction < 4; ++fraction) {
                            for (unsigned width = 1; width <= 2; ++width) {
                                uint64_t origin = origins[epoch];
                                unsigned before = period(oldRate), after = period(nextRate);
                                start(gba, origin, oldRate | (oldMode << 3));
                                gba->audio.psg.ch4.lfsr = 1;
                                gba->audio.psg.ch4.sample = 15;
                                clock_at(gba, origin + before * fraction / 4);
                                write_rate(gba, nextRate | (newMode << 3), width);
                                observe(gba, origin + before - 1, 1, 15, origin);
                                uint32_t expected = step(1, newMode);
                                observe(gba, origin + before, expected, 15, origin + before);
                                observe(gba, origin + before + after - 1, expected, 15, origin + before);
                                expected = step(expected, newMode);
                                observe(gba, origin + before + after, expected, 15, origin + before + after);
                                ++scenes;
                            }
                        }
                    }
                }
            }
        }
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void disabled_shifts_retain_the_pending_clock(void) {
    static const uint64_t pauses[] = {UINT64_C(0x4000000), UINT32_MAX - 3u};
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(origins[0]); ++epoch) {
        for (unsigned ratio = 0; ratio < 8; ++ratio) {
            for (unsigned shortMode = 0; shortMode < 2; ++shortMode) {
                for (unsigned shift = 14; shift < 16; ++shift) {
                    for (unsigned fraction = 1; fraction < 4; ++fraction) {
                        for (unsigned p = 0; p < sizeof(pauses) / sizeof(pauses[0]); ++p) {
                            for (unsigned width = 1; width <= 2; ++width) {
                                uint64_t origin = origins[epoch];
                                unsigned rate = 0x10 | ratio | (shortMode << 3), before = period(rate);
                                unsigned elapsed = before * fraction / 4;
                                start(gba, origin, rate);
                                gba->audio.psg.ch4.lfsr = 1;
                                gba->audio.psg.ch4.sample = 15;
                                clock_at(gba, origin + elapsed);
                                write_rate(gba, (shift << 4) | ratio | (shortMode << 3), width);
                                uint64_t resume = origin + elapsed + pauses[p];
                                observe(gba, resume, 1, 15, origin);
                                clock_at(gba, resume);
                                write_rate(gba, shortMode << 3, width);
                                uint64_t next = resume + before - elapsed;
                                observe(gba, next - 1, 1, 15, origin);
                                uint32_t expected = step(1, shortMode);
                                observe(gba, next, expected, 15, next);
                                observe(gba, next + 32, step(expected, shortMode), 15, next + 32);
                                ++scenes;
                            }
                        }
                    }
                }
            }
        }
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void envelopes_recompute_from_the_held_bit(void) {
    static const unsigned volumes[] = {0, 1, 14, 15};
    struct CupidGBA* gba = machine();
    for (unsigned direction = 0; direction < 2; ++direction) {
        for (unsigned state = 0; state < 0x8000; ++state) {
            for (unsigned v = 0; v < sizeof(volumes) / sizeof(volumes[0]); ++v) {
                start(gba, 0, 0);
                struct CupidAudioNoiseChannel* ch = &gba->audio.psg.ch4;
                ch->lfsr = state;
                ch->envelope.currentVolume = (int)volumes[v];
                ch->sample = (int8_t)((state & 1) * volumes[v]);
                cupid_audio_write_register8(&gba->audio, REG_SOUND4CNT_L + 1, (uint8_t)(0xF1 | (direction << 3)));
                gba->audio.psg.frame = 6;
                cupid_audio_psg_frame_step(&gba->audio.psg);
                unsigned expected = volumes[v];
                if (direction && expected < 15) ++expected;
                else if (!direction && expected > 0) --expected;
                CHECK(ch->lfsr == state && ch->envelope.currentVolume == (int)expected);
                CHECK(ch->sample == (int)((state & 1) * expected));
                ++scenes;
            }
        }
    }
    cupid_gba_destroy(gba);
    free(gba);
}

struct Capture { uint64_t origin; uint32_t state; unsigned shortMode; unsigned count; };

static void capture(void* context, uint64_t clock, int16_t left, int16_t right) {
    struct Capture* output = context;
    CHECK(clock == output->origin + (uint64_t)(++output->count) * 512);
    for (unsigned i = 0; i < 16; ++i) output->state = step(output->state, output->shortMode);
    CHECK(left == right && left == (int16_t)((output->state & 1) * 9216));
}

static void scheduled_mixer_observes_the_shifted_bit(void) {
    for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(origins[0]); ++epoch) {
        for (unsigned shortMode = 0; shortMode < 2; ++shortMode) {
            struct CupidGBA* gba = machine();
            uint64_t origin = origins[epoch];
            start(gba, origin, shortMode << 3);
            cupid_io_write(gba, REG_SOUND4CNT_L, 0xC000);
            cupid_io_write(gba, REG_SOUND4CNT_H, (uint16_t)(0x8000 | (shortMode << 3)));
            cupid_io_write(gba, REG_SOUNDCNT_L, 0x8877);
            cupid_io_write(gba, REG_SOUNDCNT_H, 2);
            gba->audio.nextSample = origin + 512;
            struct Capture output = {origin, 0, shortMode, 0};
            gba->audio.sampleSink = capture;
            gba->audio.sampleSinkContext = &output;
            cupid_timing_schedule(&gba->timing, &gba->audio.psg.frameEvent, 32768);
            cupid_timing_schedule(&gba->timing, &gba->audio.sampleEvent, 512);
            for (unsigned i = 0; i < 1024; ++i) cupid_timing_tick(&gba->timing, 512);
            CHECK(output.count == 1024);
            CHECK(gba->audio.psg.ch4.lfsr == output.state);
            ++scenes;
            cupid_gba_destroy(gba);
            free(gba);
        }
    }
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "polynomial")) RUN(polynomial_states);
    else if (!strcmp(argv[1], "registers")) RUN(register_reloads);
    else if (!strcmp(argv[1], "gates")) RUN(disabled_shifts_retain_the_pending_clock);
    else if (!strcmp(argv[1], "envelopes")) RUN(envelopes_recompute_from_the_held_bit);
    else if (!strcmp(argv[1], "timeline")) RUN(scheduled_mixer_observes_the_shifted_bit);
    else CHECK(false);
    printf("PASS %u noise-clock scenes\n", scenes);
    return 0;
}
