/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static unsigned scenes;

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    return gba;
}

static void reset(struct CupidGBA* gba, unsigned sweep, unsigned frequency) {
    cupid_io_write(gba, REG_SOUNDCNT_X, 0);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    cupid_io_write(gba, REG_SOUND1CNT_L, (uint16_t)sweep);
    cupid_io_write(gba, REG_SOUND1CNT_H, 0xF080);
    cupid_io_write(gba, REG_SOUND1CNT_X, (uint16_t)(0x8000 | frequency));
}

/* One 128 Hz sweep edge, without advancing the independent waveform clock. */
static void sweep_edge(struct CupidAudioPSG* psg) {
    psg->frame = 1;
    psg->skipFrame = false;
    cupid_audio_psg_frame_step(psg);
}

static unsigned target(unsigned frequency, unsigned shift, bool subtract) {
    unsigned delta = frequency >> shift;
    return subtract ? frequency - delta : frequency + delta;
}

static void trigger_matrix(void) {
    struct CupidGBA* gba = machine();
    struct CupidAudioPSG* psg = &gba->audio.psg;
    for (unsigned frequency = 0; frequency < 2048; ++frequency) {
        for (unsigned sweep = 0; sweep < 128; ++sweep) {
            unsigned pace = (sweep >> 4) & 7, shift = sweep & 7;
            bool subtract = (sweep & 8) != 0;
            reset(gba, sweep, frequency);
            CHECK(psg->ch1.control.frequency == (int)frequency);
            CHECK(psg->ch1.sweep.realFrequency == (int)frequency);
            CHECK(psg->ch1.sweep.step == (int)(pace ? pace : 8));
            CHECK(psg->ch1.sweep.enable == (pace != 0 || shift != 0));
            CHECK(psg->ch1.sweep.occurred == (subtract && shift != 0));
            CHECK(psg->playingCh1 == (!shift || target(frequency, shift, subtract) < 2048));
            ++scenes;
        }
    }

    /* Overflow mutes the channel while the rest of the trigger still runs. */
    for (unsigned phase = 0; phase < 8; ++phase) {
        for (unsigned length = 0; length < 2; ++length) {
            for (unsigned dac = 0; dac < 2; ++dac) {
                reset(gba, 0x11, 1500);
                psg->frame = (int)phase;
                psg->ch1.control.length = 0;
                cupid_io_write(gba, REG_SOUND1CNT_H, dac ? 0xF080 : 0x0080);
                psg->ch1.control.length = 0; /* The preceding length has expired. */
                cupid_io_write(gba, REG_SOUND1CNT_X, (uint16_t)(0x8000 | (length << 14) | 1500));
                CHECK(!psg->playingCh1);
                CHECK(psg->ch1.control.length == (int)(64 - (length && !(phase & 1))));
                CHECK(psg->ch1.envelope.currentVolume == (int)(dac ? 15 : 0));
                CHECK(psg->ch1.sweep.realFrequency == 1500);
                CHECK(psg->ch1.sweep.step == 1);
                ++scenes;
            }
        }
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void clock_matrix(void) {
    struct CupidGBA* gba = machine();
    struct CupidAudioPSG* psg = &gba->audio.psg;
    for (unsigned frequency = 0; frequency < 2048; ++frequency) {
        for (unsigned sweep = 0; sweep < 128; ++sweep) {
            unsigned pace = (sweep >> 4) & 7, shift = sweep & 7;
            bool subtract = (sweep & 8) != 0;
            reset(gba, sweep, frequency);
            unsigned expected = frequency;
            bool playing = !shift || target(frequency, shift, subtract) < 2048;
            bool negate = subtract && shift != 0;
            unsigned interval = pace ? pace : 8;
            for (unsigned tick = 0; tick < interval; ++tick) sweep_edge(psg);
            if (pace) {
                unsigned next = target(frequency, shift, subtract);
                negate |= subtract;
                if (next >= 2048) {
                    playing = false;
                } else if (shift) {
                    expected = next;
                    if (target(next, shift, subtract) >= 2048) playing = false;
                }
            }
            CHECK(psg->ch1.control.frequency == (int)expected);
            CHECK(psg->ch1.sweep.realFrequency == (int)expected);
            CHECK(psg->ch1.sweep.step == (int)interval);
            CHECK(psg->ch1.sweep.occurred == negate);
            CHECK(psg->playingCh1 == playing);
            ++scenes;
        }
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void negate_history(void) {
    static const unsigned frequencies[] = {0, 1, 1023, 1024, 2047};
    struct CupidGBA* gba = machine();
    struct CupidAudioPSG* psg = &gba->audio.psg;
    for (unsigned f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); ++f) {
        for (unsigned sweep = 8; sweep < 128; sweep += 16) {
            for (unsigned shift = 0; shift < 8; ++shift) {
                for (unsigned clocked = 0; clocked < 2; ++clocked) {
                    for (unsigned value = 0; value < 128; ++value) {
                        for (unsigned width = 1; width <= 2; ++width) {
                            reset(gba, sweep | shift, frequencies[f]);
                            if (clocked) for (unsigned i = 0; i < 8; ++i) sweep_edge(psg);
                            bool negate = shift != 0 || (clocked && (sweep >> 4) != 0);
                            unsigned before = (unsigned)psg->ch1.control.frequency;
                            int timer = psg->ch1.sweep.step;
                            /* A subtract-mode write must retain the calculation history. */
                            cupid_audio_write_register8(&gba->audio, REG_SOUND1CNT_L, (uint8_t)(value | 8));
                            if (width == 1) cupid_audio_write_register8(&gba->audio, REG_SOUND1CNT_L, (uint8_t)(value & ~8u));
                            else cupid_io_write(gba, REG_SOUND1CNT_L, (uint16_t)(0xFF00 | (value & ~8u)));
                            CHECK(psg->ch1.sweep.occurred == negate);
                            CHECK(psg->playingCh1 == !negate);
                            CHECK(psg->ch1.control.frequency == (int)before);
                            CHECK(psg->ch1.sweep.step == timer);
                            ++scenes;
                        }
                    }
                }
            }
        }
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void register_sequences(void) {
    static const uint64_t origins[] = {0, INT32_MAX - 8u, UINT32_MAX - 8u, UINT64_C(0x300000000) - 8};
    struct CupidGBA* gba = machine();
    struct CupidAudioPSG* psg = &gba->audio.psg;
    for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(origins[0]); ++epoch) {
        gba->timing.globalCycles = origins[epoch];
        gba->timing.masterCycles = (uint32_t)origins[epoch];
        gba->audio.lastSample = gba->audio.sampleOrigin = origins[epoch];
        gba->audio.nextSample = origins[epoch] + 512;
        for (unsigned pace = 0; pace < 8; ++pace) {
            for (unsigned shift = 0; shift < 8; ++shift) {
                reset(gba, (pace << 4) | 8 | shift, 1000);
                bool negate = shift != 0;
                /* A disabled pace runs its divider without doing a calculation. */
                for (unsigned i = 0; i < 8; ++i) sweep_edge(psg);
                negate |= pace != 0;
                cupid_audio_write_register8(&gba->audio, REG_SOUND1CNT_L, (uint8_t)(8 | shift));
                for (unsigned i = 0; i < 16; ++i) sweep_edge(psg);
                CHECK(psg->ch1.sweep.occurred == negate);
                cupid_audio_write_register8(&gba->audio, REG_SOUND1CNT_L, (uint8_t)shift);
                CHECK(psg->playingCh1 == !negate);

                /* A new trigger clears history before its optional calculation. */
                cupid_io_write(gba, REG_SOUND1CNT_X, 0x8000 | 1000);
                CHECK(!psg->ch1.sweep.occurred);
                CHECK(psg->playingCh1);
                CHECK(psg->ch1.sweep.step == 8);
                int shadow = psg->ch1.sweep.realFrequency;
                cupid_audio_write_register8(&gba->audio, REG_SOUND1CNT_X, 0x34);
                CHECK(psg->ch1.sweep.realFrequency == shadow);
                CHECK(psg->ch1.control.frequency == 0x334);
                /* Register writes keep the old divider until its next expiry. */
                cupid_audio_write_register8(&gba->audio, REG_SOUND1CNT_L, (uint8_t)(0x18 | shift));
                CHECK(psg->ch1.sweep.step == 8);
                for (unsigned i = 0; i < 7; ++i) sweep_edge(psg);
                CHECK(psg->ch1.control.frequency == 0x334);
                sweep_edge(psg);
                if (shift) CHECK(psg->ch1.control.frequency == (int)target((unsigned)shadow, shift, true));
                else CHECK(psg->ch1.control.frequency == 0x334);
                /* With both trigger-time sweep fields zero the unit stays disabled. */
                CHECK(psg->ch1.sweep.enable == (shift != 0));
                CHECK(psg->ch1.sweep.step == (int)(shift ? 1 : 8));
                cupid_io_write(gba, REG_SOUNDCNT_X, 0);
                CHECK(!psg->ch1.sweep.enable && !psg->ch1.sweep.occurred);
                CHECK(!psg->playingCh1);
                ++scenes;
            }
        }
    }
    cupid_gba_destroy(gba);
    free(gba);
}

struct Capture {
    uint64_t origin;
    uint64_t hash;
    unsigned count;
    bool nonzero;
};

static void capture(void* context, uint64_t clock, int16_t left, int16_t right) {
    struct Capture* output = context;
    CHECK(clock == output->origin + (uint64_t)(++output->count) * 512);
    CHECK(left == right);
    output->nonzero |= left != 0;
    output->hash = (output->hash ^ (uint16_t)left) * UINT64_C(1099511628211);
}

static void scheduled_sweep_preserves_stereo_waveform_and_clock(void) {
    static const uint64_t origins[] = {0, INT32_MAX - 8u, UINT32_MAX - 8u, UINT64_C(0x300000000) - 8};
    uint64_t expectedHash = 0;
    for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(origins[0]); ++epoch) {
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            struct CupidGBA* gba = machine();
            gba->timing.globalCycles = origins[epoch];
            gba->timing.masterCycles = (uint32_t)origins[epoch];
            gba->audio.lastSample = gba->audio.sampleOrigin = origins[epoch];
            gba->audio.nextSample = origins[epoch] + 512;
            reset(gba, 0x18, 1000);
            cupid_io_write(gba, REG_SOUND2CNT_L, 0xF080);
            cupid_io_write(gba, REG_SOUND2CNT_H, 0x8000 | 1000);
            /* Identical tones: swept channel 1 on the left, channel 2 on the right. */
            cupid_io_write(gba, REG_SOUNDCNT_L, 0x1277);
            cupid_io_write(gba, REG_SOUNDCNT_H, 2);
            struct Capture output = {origins[epoch], UINT64_C(14695981039346656037), 0, false};
            gba->audio.sampleSink = capture;
            gba->audio.sampleSinkContext = &output;
            cupid_timing_schedule(&gba->timing, &gba->audio.psg.frameEvent, 32768);
            cupid_timing_schedule(&gba->timing, &gba->audio.sampleEvent, 512);
            for (unsigned i = 0; i < 512; ++i) cupid_timing_tick(&gba->timing, 512);
            CHECK(output.count == 512 && output.nonzero);
            CHECK(gba->audio.psg.ch1.control.frequency == 1000);
            CHECK(gba->audio.psg.ch1.sweep.occurred);
            CHECK(gba->audio.psg.playingCh1 && gba->audio.psg.playingCh2);
            if (!epoch && !repeat) expectedHash = output.hash;
            CHECK(output.hash == expectedHash);
            ++scenes;
            cupid_gba_destroy(gba);
            free(gba);
        }
    }
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "trigger")) RUN(trigger_matrix);
    else if (!strcmp(argv[1], "clocks")) RUN(clock_matrix);
    else if (!strcmp(argv[1], "history")) RUN(negate_history);
    else if (!strcmp(argv[1], "registers")) {
        RUN(register_sequences);
        RUN(scheduled_sweep_preserves_stereo_waveform_and_clock);
    }
    else CHECK(false);
    printf("PASS %u PSG sweep scenes\n", scenes);
    return 0;
}
