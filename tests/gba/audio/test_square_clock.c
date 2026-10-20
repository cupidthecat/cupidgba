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

static uint32_t frequency_register(unsigned channel) { return channel ? REG_SOUND2CNT_H : REG_SOUND1CNT_X; }
static uint32_t envelope_register(unsigned channel) { return channel ? REG_SOUND2CNT_L : REG_SOUND1CNT_H; }
static unsigned period(unsigned frequency) { return 16 * (2048 - frequency); }

static struct CupidAudioSquareChannel* square(struct CupidGBA* gba, unsigned channel) {
    return channel ? &gba->audio.psg.ch2 : &gba->audio.psg.ch1;
}

static void clock_at(struct CupidGBA* gba, uint64_t clock) {
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
}

static void start(struct CupidGBA* gba, unsigned channel, uint64_t origin, unsigned frequency) {
    clock_at(gba, origin);
    gba->audio.lastSample = gba->audio.sampleOrigin = origin;
    /* Arithmetic groups advance channels directly; mixer behavior has its own group. */
    gba->audio.nextSample = origin + 0x10000000;
    cupid_io_write(gba, REG_SOUNDCNT_X, 0);
    cupid_io_write(gba, REG_SOUNDCNT_X, 0x80);
    cupid_io_write(gba, envelope_register(channel), 0xF080);
    cupid_io_write(gba, frequency_register(channel), (uint16_t)(0x8000 | frequency));
}

static void write_frequency(struct CupidGBA* gba, unsigned channel, unsigned width, unsigned frequency) {
    uint32_t reg = frequency_register(channel);
    if (width == 1) {
        cupid_audio_write_register8(&gba->audio, reg, (uint8_t)frequency);
        cupid_audio_write_register8(&gba->audio, reg + 1, (uint8_t)(frequency >> 8));
    } else cupid_io_write(gba, reg, (uint16_t)frequency);
}

static void check_phase(struct CupidGBA* gba, unsigned channel, uint64_t clock,
                        uint64_t reload, unsigned phase) {
    cupid_audio_psg_run(&gba->audio.psg, clock, 1 << channel);
    CHECK(square(gba, channel)->index == (phase & 7));
    CHECK(square(gba, channel)->lastUpdate == reload);
}

static void frequency_writes(unsigned channel) {
    static const unsigned frequencies[] = {0, 1, 1000, 1023, 1024, 2040, 2046, 2047};
    struct CupidGBA* gba = machine();
    for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(origins[0]); ++epoch) {
        for (unsigned old = 0; old < 2048; ++old) {
            for (unsigned next = 0; next < sizeof(frequencies) / sizeof(frequencies[0]); ++next) {
                unsigned oldPeriod = period(old), newPeriod = period(frequencies[next]);
                for (unsigned fraction = 0; fraction < 4; ++fraction) {
                    for (unsigned width = 1; width <= 2; ++width) {
                        uint64_t origin = origins[epoch], write = origin + oldPeriod * fraction / 4;
                        start(gba, channel, origin, old);
                        clock_at(gba, write);
                        write_frequency(gba, channel, width, frequencies[next]);
                        CHECK(square(gba, channel)->control.frequency == (int)frequencies[next]);
                        check_phase(gba, channel, write, origin, 0);
                        check_phase(gba, channel, origin + oldPeriod - 1, origin, 0);
                        check_phase(gba, channel, origin + oldPeriod, origin + oldPeriod, 1);
                        check_phase(gba, channel, origin + oldPeriod + newPeriod - 1, origin + oldPeriod, 1);
                        check_phase(gba, channel, origin + oldPeriod + newPeriod, origin + oldPeriod + newPeriod, 2);
                        check_phase(gba, channel, origin + oldPeriod + 2 * newPeriod + 7,
                                    origin + oldPeriod + 2 * newPeriod, 3);
                        ++scenes;
                    }
                }
            }
        }
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void repeated_writes_and_retrigger(void) {
    static const unsigned first[] = {0, 1, 1000, 1024, 2040, 2047};
    struct CupidGBA* gba = machine();
    for (unsigned channel = 0; channel < 2; ++channel) {
        for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(origins[0]); ++epoch) {
            for (unsigned frequency = 0; frequency < 2048; ++frequency) {
                for (unsigned initial = 0; initial < sizeof(first) / sizeof(first[0]); ++initial) {
                    for (unsigned width = 1; width <= 2; ++width) {
                        uint64_t origin = origins[epoch];
                        unsigned oldPeriod = period(first[initial]), newPeriod = period(frequency);
                        start(gba, channel, origin, first[initial]);
                        check_phase(gba, channel, origin + oldPeriod, origin + oldPeriod, 1);
                        clock_at(gba, origin + oldPeriod + oldPeriod / 4);
                        write_frequency(gba, channel, width, frequency ^ 0x7FF);
                        clock_at(gba, origin + oldPeriod + oldPeriod * 3 / 4);
                        write_frequency(gba, channel, width, frequency);
                        check_phase(gba, channel, origin + 2 * oldPeriod, origin + 2 * oldPeriod, 2);
                        check_phase(gba, channel, origin + 2 * oldPeriod + newPeriod,
                                    origin + 2 * oldPeriod + newPeriod, 3);
                        /* An explicit trigger reloads the timer and retains the duty position. */
                        uint64_t trigger = origin + 2 * oldPeriod + newPeriod + 1;
                        clock_at(gba, trigger);
                        cupid_io_write(gba, frequency_register(channel), (uint16_t)(0x8000 | frequency));
                        check_phase(gba, channel, trigger + newPeriod - 1, trigger, 3);
                        check_phase(gba, channel, trigger + newPeriod, trigger + newPeriod, 4);
                        ++scenes;
                    }
                }
            }
        }
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void silent_envelopes_keep_clocking(void) {
    struct CupidGBA* gba = machine();
    for (unsigned channel = 0; channel < 2; ++channel) {
        for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(origins[0]); ++epoch) {
            for (unsigned frequency = 0; frequency < 2048; ++frequency) {
                uint64_t origin = origins[epoch];
                unsigned clocks = period(frequency);
                start(gba, channel, origin, frequency);
                cupid_io_write(gba, envelope_register(channel), 0x0880); /* Active generator, zero volume. */
                cupid_io_write(gba, frequency_register(channel), (uint16_t)(0x8000 | frequency));
                CHECK(square(gba, channel)->envelope.dead == 2);
                check_phase(gba, channel, origin + clocks * 9 / 2, origin + clocks * 4, 4);
                CHECK(square(gba, channel)->sample == 0);
                clock_at(gba, origin + clocks * 9 / 2);
                cupid_audio_write_register8(&gba->audio, envelope_register(channel) + 1, 0x09);
                gba->audio.psg.frame = 6;
                cupid_audio_psg_frame_step(&gba->audio.psg);
                CHECK(square(gba, channel)->envelope.currentVolume == 1);
                CHECK(square(gba, channel)->index == 4);
                check_phase(gba, channel, origin + clocks * 5, origin + clocks * 5, 5);
                ++scenes;
            }
        }
    }
    cupid_gba_destroy(gba);
    free(gba);
}

static void scheduled_sweep_retains_the_pending_edge(void) {
    for (unsigned epoch = 0; epoch < sizeof(origins) / sizeof(origins[0]); ++epoch) {
        struct CupidGBA* gba = machine();
        uint64_t origin = origins[epoch];
        start(gba, 0, origin, 1000);
        cupid_io_write(gba, REG_SOUND1CNT_L, 0x19);
        cupid_io_write(gba, REG_SOUND1CNT_X, 0x8000 | 1000);
        gba->audio.nextSample = origin + 512;
        cupid_timing_schedule(&gba->timing, &gba->audio.psg.frameEvent, 32768);
        cupid_timing_schedule(&gba->timing, &gba->audio.sampleEvent, 512);
        for (unsigned i = 0; i < 192; ++i) cupid_timing_tick(&gba->timing, 512);
        CHECK(gba->audio.psg.ch1.control.frequency == 500);
        check_phase(gba, 0, origin + 100607, origin + 83840, 5);
        check_phase(gba, 0, origin + 100608, origin + 100608, 6);
        check_phase(gba, 0, origin + 125375, origin + 100608, 6);
        check_phase(gba, 0, origin + 125376, origin + 125376, 7);
        ++scenes;
        cupid_gba_destroy(gba);
        free(gba);
    }
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "writes-1")) frequency_writes(0);
    else if (!strcmp(argv[1], "writes-2")) frequency_writes(1);
    else if (!strcmp(argv[1], "sequences")) RUN(repeated_writes_and_retrigger);
    else if (!strcmp(argv[1], "silence")) RUN(silent_envelopes_keep_clocking);
    else if (!strcmp(argv[1], "sweep")) RUN(scheduled_sweep_retains_the_pending_edge);
    else CHECK(false);
    printf("PASS %u square-clock scenes\n", scenes);
    return 0;
}
