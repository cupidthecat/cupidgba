/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_pwm_precision.c
 * @brief PWM pulse-count precision, stereo conversion and scheduled register changes.
 *
 * - Derive output from a whole number of pulse clocks in each PWM period.
 * - Exhaust every bias/FIFO pair with both even and odd PSG contributions.
 * - Check gain, stereo routing, CPU/DMA writes and all divider transitions.
 * - Retain punctual and late histories across the scheduler clock wrap.
 */
#include "../../test.h"
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"

static const uint64_t epochs[] = {0, INT32_MAX - 4u, UINT32_MAX - 4u, UINT64_C(0x300000000) - 4};

struct Capture {
    unsigned count;
    uint64_t clock[128];
    int16_t left[128], right[128];
};

static void capture(void *context, uint64_t clock, int16_t left, int16_t right) {
    struct Capture *c = context;
    CHECK(c->count < 128);
    c->clock[c->count] = clock;
    c->left[c->count] = left;
    c->right[c->count++] = right;
}

static struct CupidGBA *machine(uint64_t origin) {
    struct CupidGBA *g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    cupid_gba_reset(g);
    cupid_timing_clear(&g->timing);
    g->timing.globalCycles = origin;
    g->timing.masterCycles = (uint32_t)origin;
    g->audio.lastSample = g->audio.sampleOrigin = origin;
    g->audio.nextSample = origin + 512;
    cupid_timing_schedule(&g->timing, &g->audio.sampleEvent, 512);
    g->cpu.cpsr.i = 1;
    g->cpu.irqDisableLatch = true;
    cupid_io_write(g, REG_SOUNDCNT_X, 0x80);
    return g;
}

static void destroy(struct CupidGBA *g) {
    cupid_gba_destroy(g);
    free(g);
}

/* A period has 512, 256, 128 or 64 console clocks. Count complete pulses
 * after ten-bit clipping, then convert that duty ratio to the existing PCM
 * scale. Integer division supplies floor rounding independently of masks. */
static int expected(int input, unsigned bias, unsigned mode, int gain) {
    int voltage = input + (int)bias;
    if (voltage < 0)
        voltage = 0;
    if (voltage > 1023)
        voltage = 1023;
    int period = 512 >> mode;
    int pulses = voltage * period / 1024;
    int64_t scaled = (int64_t)(pulses * 1024 / period - 512) * gain * 3;
    return (int)(scaled < 0 ? -((-scaled + 15) / 16) : scaled / 16);
}

/* Hold a valid square duty/envelope at its current phase. This fixture
 * isolates mixer precision; oscillator transitions have separate coverage. */
static void held_psg(struct CupidGBA *g, unsigned value) {
    cupid_io_write(g, REG_SOUNDCNT_L, 0x1111);
    g->audio.psg.playingCh1 = value != 0;
    g->audio.psg.ch1.envelope.duty = 1;
    g->audio.psg.ch1.envelope.currentVolume = (int)value;
    g->audio.psg.ch1.index = 0;
    g->audio.psg.ch1.lastUpdate = g->audio.nextSample;
}

static void fifo(struct CupidGBA *g, int a, int b, uint16_t control) {
    cupid_io_write(g, REG_SOUNDCNT_H, (uint16_t)(control | 0x8800));
    cupid_memory_store32(&g->cpu, GBA_BASE_IO + REG_FIFO_A_LO,
                         (int32_t)((uint8_t)a * UINT32_C(0x01010101)), NULL);
    cupid_memory_store32(&g->cpu, GBA_BASE_IO + REG_FIFO_B_LO,
                         (int32_t)((uint8_t)b * UINT32_C(0x01010101)), NULL);
    for (unsigned i = 0; i < 4; ++i)
        cupid_audio_timer_overflow(&g->audio, 0, cupid_timing_global_time(&g->timing));
    CHECK(g->audio.chA.latchedSample == a && g->audio.chB.latchedSample == b);
}

static void sample(struct CupidGBA *g, struct Capture *c, int left, int right) {
    c->count = 0;
    uint64_t deadline = g->audio.nextSample;
    uint64_t now = cupid_timing_global_time(&g->timing);
    CHECK(deadline > now && deadline - now <= 512);
    cupid_timing_tick(&g->timing, (int)(deadline - now));
    CHECK(c->count == 1 && c->clock[0] == deadline);
    CHECK(c->left[0] == left && c->right[0] == right);
    CHECK(g->audio.currentSamples[0].left == left && g->audio.currentSamples[0].right == right);
    int previous = (g->audio.sampleWritePos + GBA_AUDIO_SAMPLES * 2 - 2) % (GBA_AUDIO_SAMPLES * 2);
    CHECK(g->audio.sampleBuffer[previous] == left && g->audio.sampleBuffer[previous + 1] == right);
}

static void levels(unsigned mode) {
    struct CupidGBA *g = machine(0);
    struct Capture c = {0};
    g->audio.sampleSink = capture;
    g->audio.sampleSinkContext = &c;
    bool codes[1024] = {false};
    unsigned scenes = 0;
    for (unsigned bias = 0; bias < 1024; bias += 2) {
        cupid_io_write(g, REG_SOUNDBIAS, (uint16_t)(bias | mode << 14));
        for (int input = -128; input <= 127; ++input)
            for (unsigned odd = 0; odd < 2; ++odd) {
                fifo(g, input, input, 0x330C);
                held_psg(g, odd);
                int raw = input * 8 + (int)odd + (int)bias;
                if (raw >= 0 && raw <= 1023)
                    codes[raw] = true;
                int value = expected(input * 8 + (int)odd, bias, mode, 256);
                sample(g, &c, value, value);
                CHECK(value >= -24576 && value <= expected(2048, 0, mode, 256));
                CHECK(value % (48 * (2 << mode)) == 0);
                ++scenes;
            }
    }
    for (unsigned code = 0; code < 1024; ++code)
        CHECK(codes[code]);
    CHECK(scenes == 262144);
    printf("PWM mode=%u precision=%ubit scenes=%u all1024inputcodes\n", mode, 9 - mode, scenes);
    destroy(g);
}

static void routing(void) {
    const int values[][2] = {{127, 127}, {-128, -128}, {1, -1}, {-1, 1}, {0, 0}, {15, -16}};
    const unsigned biases[] = {0, 2, 510, 512, 514, 1022};
    const int gains[] = {0, 1, 85, 128, 255, 256};
    unsigned scenes = 0;
    struct CupidGBA *g = machine(0);
    struct Capture c = {0};
    g->audio.sampleSink = capture;
    g->audio.sampleSinkContext = &c;
    for (unsigned mode = 0; mode < 4; ++mode)
        for (unsigned bi = 0; bi < 6; ++bi)
            for (unsigned gain = 0; gain < 6; ++gain)
                for (unsigned route = 0; route < 16; ++route)
                    for (unsigned volume = 0; volume < 4; ++volume)
                        for (unsigned pair = 0; pair < 6; ++pair) {
                            cupid_io_write(g, REG_SOUNDBIAS, (uint16_t)(biases[bi] | mode << 14));
                            g->audio.masterVolume = gains[gain];
                            uint16_t control =
                                (uint16_t)((route & 3) << 8 | (route & 12) << 10 | volume << 2);
                            fifo(g, values[pair][0], values[pair][1], control);
                            held_psg(g, 1);
                            int a = values[pair][0] * (volume & 1 ? 4 : 2);
                            int b = values[pair][1] * (volume & 2 ? 4 : 2);
                            int left = 1 + (route & 2 ? a : 0) + (route & 8 ? b : 0);
                            int right = 1 + (route & 1 ? a : 0) + (route & 4 ? b : 0);
                            sample(g, &c, expected(left, biases[bi], mode, gains[gain]),
                                   expected(right, biases[bi], mode, gains[gain]));
                            ++scenes;
                        }
    CHECK(scenes == 55296);
    const int maximum[] = {24480, 24384, 24192, 23808};
    for (unsigned mode = 0; mode < 4; ++mode) {
        CHECK(expected(2048, 512, mode, 256) == maximum[mode]);
        CHECK(expected(-2048, 512, mode, 256) == -24576);
        CHECK(expected(1, 512, mode, 256) == 0);
        CHECK(expected(-1, 512, mode, 256) == -(96 << mode));
        cupid_io_write(g, REG_SOUNDCNT_X, 0);
        cupid_io_write(g, REG_SOUNDBIAS, (uint16_t)(1022 | mode << 14));
        sample(g, &c, 0, 0);
        cupid_io_write(g, REG_SOUNDCNT_X, 0x80);
    }
    printf("PWM stereo/gain/routing scenes=%u; fixed endpoints and power mute\n", scenes);
    destroy(g);
}

static void advance(struct CupidGBA *g, unsigned clocks, bool punctual) {
    if (punctual)
        for (unsigned i = 0; i < clocks; ++i)
            cupid_timing_tick(&g->timing, 1);
    else
        cupid_timing_tick(&g->timing, (int)clocks);
}

static void chronology(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned old = 0; old < 4; ++old)
            for (unsigned next = 0; next < 4; ++next)
                for (unsigned phase = 511; phase <= 513; ++phase) {
                    struct Capture c[2] = {{0}, {0}};
                    for (unsigned punctual = 0; punctual < 2; ++punctual) {
                        struct CupidGBA *g = machine(epochs[epoch]);
                        g->audio.sampleSink = capture;
                        g->audio.sampleSinkContext = &c[punctual];
                        cupid_io_write(g, REG_SOUNDBIAS, (uint16_t)(1022 | old << 14));
                        advance(g, phase, punctual != 0);
                        uint64_t retained = g->audio.nextSample;
                        cupid_io_write(g, REG_SOUNDBIAS, (uint16_t)(510 | next << 14));
                        CHECK(g->audio.nextSample == retained);
                        advance(g, 2048 - phase, punctual != 0);
                        uint64_t deadline = epochs[epoch] + 512;
                        unsigned index = 0;
                        while (deadline <= epochs[epoch] + 2048) {
                            CHECK(index < c[punctual].count);
                            CHECK(c[punctual].clock[index] == deadline);
                            unsigned elapsed = (unsigned)(deadline - epochs[epoch]);
                            unsigned mode = elapsed <= phase ? old : next;
                            unsigned bias = elapsed <= phase ? 1022 : 510;
                            int value = expected(0, bias, mode, 256);
                            CHECK(c[punctual].left[index] == value &&
                                  c[punctual].right[index] == value);
                            unsigned interval = 512 >> (elapsed <= phase ? old : next);
                            /* A write retains the already queued deadline. */
                            if (elapsed < phase) {
                                unsigned following = elapsed + interval;
                                deadline = epochs[epoch] + following;
                            } else
                                deadline += interval - ((deadline - epochs[epoch]) % interval);
                            ++index;
                        }
                        CHECK(index == c[punctual].count);
                        destroy(g);
                    }
                    CHECK(c[0].count == c[1].count);
                    CHECK(!memcmp(c[0].clock, c[1].clock, sizeof(c[0].clock)));
                    CHECK(!memcmp(c[0].left, c[1].left, sizeof(c[0].left)));
                    CHECK(!memcmp(c[0].right, c[1].right, sizeof(c[0].right)));
                    ++scenes;
                }
    CHECK(scenes == 192);
    printf("PWM divider chronology pairs=%u across four epochs\n", scenes);
}

static void guest(void) {
    unsigned scenes = 0;
    const uint32_t arm[] = {0xE5C10000, 0xE1C100B0, 0xE5810000};
    const uint16_t narrow[] = {0x7008, 0x8008, 0x6008};
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned thumb = 0; thumb < 2; ++thumb)
            for (unsigned mode = 0; mode < 4; ++mode)
                for (unsigned width = 1; width <= 4; width *= 2)
                    for (unsigned phase = 511; phase <= 513; ++phase) {
                        struct CupidGBA *g = machine(epochs[epoch]);
                        struct Capture c = {0};
                        g->audio.sampleSink = capture;
                        g->audio.sampleSinkContext = &c;
                        cupid_io_write(g, REG_SOUNDBIAS, 0x2FE);
                        unsigned index = width == 1 ? 0 : width == 2 ? 1 : 2;
                        if (thumb) {
                            const uint16_t code[] = {narrow[index], 0xE7FE, 0x46C0};
                            memcpy(g->memory.iwram + 0x100, code, sizeof(code));
                            _CupidSetMode(&g->cpu, MODE_THUMB);
                            g->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x100;
                            CupidThumbWritePC(&g->cpu);
                        } else {
                            const uint32_t code[] = {arm[index], 0xEAFFFFFE, 0xE1A00000};
                            memcpy(g->memory.iwram + 0x100, code, sizeof(code));
                            g->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x100;
                            CupidARMWritePC(&g->cpu);
                        }
                        uint16_t raw = (uint16_t)(0x3FE | mode << 14);
                        g->cpu.gprs[0] = width == 1 ? raw >> 8 : raw;
                        g->cpu.gprs[1] = GBA_BASE_IO + REG_SOUNDBIAS + (width == 1);
                        advance(g, phase, false);
                        cupid_gba_step(g);
                        g->cpu.nextEvent = g->cpu.cycles;
                        g->cpu.irqh.processEvents(&g->cpu);
                        CHECK(cupid_io_read(g, REG_SOUNDBIAS) == raw);
                        CHECK(g->audio.sampleInterval == (512 >> mode));
                        int value = expected(0, 1022, mode, 256);
                        sample(g, &c, value, value);
                        CHECK((g->audio.lastSample - epochs[epoch]) % 64 == 0);
                        CHECK(!g->memory.dmaRunning && g->irqf == 0);
                        ++scenes;
                        destroy(g);
                    }
    CHECK(scenes == 288);
    printf("PWM timed ARM/Thumb byte/halfword/word stores=%u\n", scenes);
}

static void dma(void) {
    unsigned scenes = 0;
    for (unsigned epoch = 0; epoch < 4; ++epoch)
        for (unsigned channel = 0; channel < 4; ++channel)
            for (unsigned mode = 0; mode < 4; ++mode)
                for (unsigned width = 2; width <= 4; width *= 2)
                    for (unsigned power = 0; power < 2; ++power) {
                        struct CupidGBA *g = machine(epochs[epoch]);
                        struct Capture c = {0};
                        g->audio.sampleSink = capture;
                        g->audio.sampleSinkContext = &c;
                        cupid_io_write(g, REG_SOUNDCNT_X, power ? 0x80 : 0);
                        uint32_t raw = UINT32_C(0xB6933FFF) | mode << 14;
                        cupid_memory_store32(&g->cpu, GBA_BASE_IWRAM + 0x1000, (int32_t)raw, NULL);
                        cupid_dma_write_sad(g, (int)channel, GBA_BASE_IWRAM + 0x1000);
                        cupid_dma_write_dad(g, (int)channel, GBA_BASE_IO + REG_SOUNDBIAS);
                        cupid_dma_write_cnt_lo(g, (int)channel, 1);
                        cupid_dma_write_cnt(g, (int)channel,
                                            (uint16_t)(0xC000 | (width == 4 ? 0x400 : 0)));
                        cupid_timing_tick(&g->timing, 16);
                        CHECK(!g->memory.dmaRunning && !g->memory.dma[channel].enable);
                        CHECK(g->memory.dma[channel].latch ==
                              (width == 4 ? raw : (raw & 65535) * UINT32_C(0x10001)));
                        CHECK(g->irqf == (IRQ_DMA0 << channel));
                        CHECK(cupid_io_read(g, REG_SOUNDBIAS) == (0x3FE | mode << 14));
                        CHECK(g->audio.nextSample == epochs[epoch] + 512);
                        int value = power ? expected(0, 1022, mode, 256) : 0;
                        sample(g, &c, value, value);
                        ++scenes;
                        destroy(g);
                    }
    CHECK(scenes == 256);
    printf("PWM DMA source/latch/IRQ/power scenes=%u\n", scenes);
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    if (strlen(argv[1]) == 1 && argv[1][0] >= '0' && argv[1][0] <= '3')
        levels((unsigned)(argv[1][0] - '0'));
    else if (!strcmp(argv[1], "routing"))
        routing();
    else if (!strcmp(argv[1], "chronology"))
        chronology();
    else if (!strcmp(argv[1], "guest"))
        guest();
    else if (!strcmp(argv[1], "dma"))
        dma();
    else
        return 2;
    return 0;
}
