/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_wave_advance.c
 * @brief Bulk wave advancement against scalar sample stepping.
 */
#include "../../test.h"
#include "cupid/gba.h"

static const uint64_t origins[] = {0, INT32_MAX - 16u, UINT32_MAX - 16u,
                                   UINT64_C(0x300000000) - 16};
static unsigned scenes;

static int8_t level(unsigned nibble, unsigned volume) {
    if (volume >= 4)
        return (int8_t)(nibble * 3 / 4);
    return volume ? (int8_t)(nibble >> (volume - 1)) : 0;
}

static struct CupidAudioPSG initial(unsigned pattern, unsigned bank, unsigned size, unsigned volume,
                                    unsigned rate, uint64_t origin) {
    struct CupidAudioPSG psg = {0};
    psg.enable = true;
    psg.timingFactor = 4;
    psg.playingCh3 = true;
    psg.ch3.enable = true;
    psg.ch3.bank = bank != 0;
    psg.ch3.size = size != 0;
    psg.ch3.volume = (int)volume;
    psg.ch3.rate = (int)rate;
    psg.ch3.length = 137;
    psg.ch3.rawSample = 9;
    psg.ch3.sample = level(9, volume);
    psg.ch3.nextUpdate = origin + 32;
    uint32_t random = UINT32_C(0xA93F270D) + pattern;
    for (unsigned i = 0; i < 32; ++i) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        psg.ch3.wavedata8[i] = pattern == 0   ? 0
                               : pattern == 1 ? 255
                               : pattern == 2 ? (uint8_t)(i * 37 + 11)
                               : pattern == 3 ? (uint8_t)(i < 16 ? 0x3C : 0xA5)
                                              : (uint8_t)random;
    }
    return psg;
}

/* Expand storage into individual samples and shift one sample at a time.
 * This oracle does not use the production word/byte rotation expressions. */
static void scalar(struct CupidAudioPSG *psg, uint64_t steps, bool reduce) {
    if (!steps)
        return;
    struct CupidAudioWaveChannel *wave = &psg->ch3;
    const unsigned first = !wave->size && wave->bank ? 32 : 0;
    const unsigned count = wave->size ? 64 : 32;
    const unsigned tap = (wave->bank ? 32 : 0) - first;
    uint8_t samples[64];
    for (unsigned i = 0; i < 64; ++i)
        samples[i] = (wave->wavedata8[i / 2] >> (i % 2 ? 0 : 4)) & 15;
    /* A complete cycle restores storage but still replaces the held sample.
     * Keep one full cycle for an exact multiple in the large-interval oracle. */
    const uint64_t shifts = reduce ? (steps - 1) % count + 1 : steps;
    for (uint64_t step = 0; step < shifts; ++step) {
        wave->rawSample = samples[first + tap];
        const uint8_t front = samples[first];
        memmove(samples + first, samples + first + 1, count - 1);
        samples[first + count - 1] = front;
    }
    for (unsigned i = 0; i < 32; ++i)
        wave->wavedata8[i] = (uint8_t)((samples[i * 2] << 4) | samples[i * 2 + 1]);
    wave->sample = level(wave->rawSample, (unsigned)wave->volume);
    wave->nextUpdate += steps * 8 * (2048 - (unsigned)wave->rate);
    wave->readable = true;
}

static void observe(struct CupidAudioPSG before, uint64_t steps, unsigned tail, bool reduce) {
    struct CupidAudioPSG expected = before, actual = before;
    const uint64_t period = 8 * (2048 - (unsigned)before.ch3.rate);
    const uint64_t target =
        steps ? before.ch3.nextUpdate + (steps - 1) * period + tail : before.ch3.nextUpdate - 1;
    CHECK(tail < period);
    scalar(&expected, steps, reduce);
    cupid_audio_psg_run(&actual, target, 4);
    CHECK(!memcmp(&actual, &expected, sizeof(actual)));
    /* Reobserving the same clock must not rotate storage or replace output. */
    cupid_audio_psg_run(&actual, target, 4);
    CHECK(!memcmp(&actual, &expected, sizeof(actual)));
    ++scenes;
}

static void rotations(void) {
    for (unsigned pattern = 0; pattern < 8; ++pattern)
        for (unsigned bank = 0; bank < 2; ++bank)
            for (unsigned size = 0; size < 2; ++size)
                for (unsigned steps = 0; steps <= 257; ++steps)
                    observe(initial(pattern, bank, size, steps % 8, 2047, 0), steps, steps % 8,
                            false);
    CHECK(scenes == 8256);
}

static void clocks(void) {
    const unsigned rates[] = {0, 1, 255, 1023, 1792, 2046, 2047};
    const uint64_t steps[] = {0, 1, 2, 3, 4, 31, 32, 33, 63, 64, 65, 127, 128, 129};
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned bank = 0; bank < 2; ++bank)
            for (unsigned size = 0; size < 2; ++size)
                for (unsigned volume = 0; volume < 8; ++volume)
                    for (unsigned rate = 0; rate < sizeof(rates) / sizeof(*rates); ++rate)
                        for (unsigned n = 0; n < sizeof(steps) / sizeof(*steps); ++n) {
                            unsigned period = 8 * (2048 - rates[rate]);
                            observe(
                                initial(4 + n % 4, bank, size, volume, rates[rate], origins[epoch]),
                                steps[n], n & 1 ? period - 1 : 0, false);
                        }
    CHECK(scenes == 12544);
}

static void large(void) {
    const uint64_t steps[] = {UINT32_MAX, UINT64_C(0x100000000), UINT64_C(0x10000000001),
                              UINT64_C(0x100000000003F), UINT64_C(0x1000000000040)};
    for (unsigned pattern = 0; pattern < 8; ++pattern)
        for (unsigned bank = 0; bank < 2; ++bank)
            for (unsigned size = 0; size < 2; ++size)
                for (unsigned volume = 0; volume < 8; ++volume)
                    for (unsigned epoch = 0; epoch < 4; ++epoch)
                        for (unsigned n = 0; n < sizeof(steps) / sizeof(*steps); ++n)
                            observe(initial(pattern, bank, size, volume, 2047 - n, origins[epoch]),
                                    steps[n], 1, true);
    CHECK(scenes == 5120);
}

static void partitions(void) {
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned pattern = 0; pattern < 8; ++pattern)
            for (unsigned bank = 0; bank < 2; ++bank)
                for (unsigned size = 0; size < 2; ++size)
                    for (unsigned volume = 0; volume < 8; ++volume) {
                        struct CupidAudioPSG whole =
                            initial(pattern, bank, size, volume, 2047, origins[epoch]);
                        struct CupidAudioPSG split = whole;
                        uint64_t clock = whole.ch3.nextUpdate;
                        for (unsigned stage = 0; stage < 8; ++stage) {
                            clock += 8 * (31 + stage);
                            cupid_audio_psg_run(&whole, clock, 4);
                            for (uint64_t part = split.ch3.nextUpdate; part <= clock; ++part)
                                cupid_audio_psg_run(&split, part, 4);
                            CHECK(!memcmp(&whole, &split, sizeof(whole)));
                            /* Mid-period changes keep the old pending edge.
                             * The next reload uses the newly selected rate. */
                            whole.ch3.rate = split.ch3.rate = 2047 - stage;
                            whole.ch3.bank = split.ch3.bank = (stage & 1) != 0;
                            whole.ch3.size = split.ch3.size = (stage & 2) != 0;
                        }
                        ++scenes;
                    }
    CHECK(scenes == 1024);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "rotations"))
        rotations();
    else if (!strcmp(argv[1], "clocks"))
        clocks();
    else if (!strcmp(argv[1], "large"))
        large();
    else if (!strcmp(argv[1], "partitions"))
        partitions();
    else
        CHECK(false);
    printf("PASS %u complete wave storage/output/clock scenes\n", scenes);
    return 0;
}
