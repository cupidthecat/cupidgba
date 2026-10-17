/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include "../bios/caller.h"

struct Capture {
    unsigned count;
    uint64_t clock[32];
    int16_t left[32], right[32];
};

static void capture(void* context, uint64_t clock, int16_t left, int16_t right) {
    struct Capture* c = context;
    CHECK(c->count < 32);
    c->clock[c->count] = clock;
    c->left[c->count] = left;
    c->right[c->count++] = right;
}

static struct CupidGBA* machine(uint64_t origin) {
    struct CupidGBA* g = calloc(1, sizeof(*g));
    CHECK(g);
    cupid_gba_create(g);
    cupid_gba_reset(g);
    g->cpu.cycles = 0;
    /* Isolate scheduled DAC captures; firmware tests retain all peripherals. */
    cupid_timing_clear(&g->timing);
    g->timing.globalCycles = origin;
    g->timing.masterCycles = (uint32_t)origin;
    g->audio.lastSample = g->audio.sampleOrigin = origin;
    g->audio.nextSample = origin + 512;
    cupid_timing_schedule(&g->timing, &g->audio.sampleEvent, 512);
    cupid_io_write(g, REG_SOUNDCNT_X, 0x80);
    return g;
}

static void destroy(struct CupidGBA* g) {
    cupid_gba_destroy(g);
    free(g);
}

/* Compute signed DAC position from its clipping thresholds. Use division
 * with explicit floor rounding, independently of the mixer's signed shift. */
static int expected(int input, unsigned bias, int gain) {
    int position;
    if (input < -(int)bias) position = -512;
    else if (input > 1023 - (int)bias) position = 511;
    else position = input + (int)bias - 512;
    int64_t scaled = (int64_t)position * gain * 3;
    return (int)(scaled < 0 ? -((-scaled + 15) / 16) : scaled / 16);
}

static void write_bias(struct CupidGBA* g, unsigned width, uint16_t value) {
    uint32_t addr = GBA_BASE_IO + REG_SOUNDBIAS;
    if (width == 1) {
        cupid_memory_store8(&g->cpu, addr, (uint8_t)value, NULL);
        cupid_memory_store8(&g->cpu, addr + 1, (uint8_t)(value >> 8), NULL);
    } else if (width == 2) cupid_memory_store16(&g->cpu, addr, value, NULL);
    else cupid_memory_store32(&g->cpu, addr, value, NULL);
    CHECK(cupid_io_read(g, REG_SOUNDBIAS) == value);
}

static void fifo_samples(struct CupidGBA* g, int a, int b, uint16_t control) {
    cupid_io_write(g, REG_SOUNDCNT_H, (uint16_t)(control | 0x8800));
    cupid_memory_store32(&g->cpu, GBA_BASE_IO + REG_FIFO_A_LO,
                         (int32_t)((uint8_t)a * UINT32_C(0x01010101)), NULL);
    cupid_memory_store32(&g->cpu, GBA_BASE_IO + REG_FIFO_B_LO,
                         (int32_t)((uint8_t)b * UINT32_C(0x01010101)), NULL);
    /* Consume the complete word through real timer strobes so a preceding
     * case cannot leave bytes in the FIFO's separate staging register. */
    for (unsigned i = 0; i < 4; i++)
        cupid_audio_timer_overflow(&g->audio, 0, cupid_timing_global_time(&g->timing));
    CHECK(g->audio.chA.latchedSample == a && g->audio.chB.latchedSample == b);
    CHECK(!g->audio.chA.internalRemaining && !g->audio.chB.internalRemaining);
}

static void sample_one(struct CupidGBA* g, struct Capture* c, int left, int right) {
    c->count = 0;
    uint64_t next = g->audio.nextSample;
    cupid_timing_tick(&g->timing, 512);
    CHECK(c->count == 1 && c->clock[0] == next);
    CHECK(c->left[0] == left && c->right[0] == right);
}

static void levels(void) {
    struct CupidGBA* g = machine(0);
    struct Capture c = {0};
    g->audio.sampleSink = capture;
    g->audio.sampleSinkContext = &c;
    unsigned count = 0;
    for (unsigned bias = 0; bias < 1024; bias += 2) {
        write_bias(g, 2, (uint16_t)bias);
        int previous = -24576;
        for (int input = -128; input <= 127; input++) {
            fifo_samples(g, input, input, 0x330C);
            int value = expected(input * 8, bias, 256);
            sample_one(g, &c, value, value);
            CHECK(c.left[0] >= previous && c.left[0] >= -24576 && c.left[0] <= 24528);
            previous = c.left[0];
            count++;
        }
    }
    CHECK(count == 131072);
    printf("PASS %u scheduled bias/input pairs, monotonic and bounded\n", count);
    destroy(g);
}

static void routing(void) {
    const unsigned biases[] = {0, 2, 0x200, 0x3FC, 0x3FE};
    const int gains[] = {0, 1, 85, 128, 255, 256};
    const int pairs[][2] = {{-128,-128}, {127,127}, {-128,127}, {127,-128},
        {0,0}, {-1,1}, {1,-1}, {-64,63}, {126,-127}};
    struct CupidGBA* g = machine(0);
    struct Capture c = {0};
    g->audio.sampleSink = capture;
    g->audio.sampleSinkContext = &c;
    unsigned count = 0;
    for (unsigned width = 1; width <= 4; width *= 2)
    for (unsigned bias = 0; bias < 5; bias++)
    for (unsigned gain = 0; gain < 6; gain++)
    for (unsigned route = 0; route < 16; route++)
    for (unsigned volume = 0; volume < 4; volume++)
    for (unsigned pair = 0; pair < 9; pair++) {
        write_bias(g, width, (uint16_t)biases[bias]);
        g->audio.masterVolume = gains[gain];
        uint16_t control = (uint16_t)((route & 3) << 8 | (route & 12) << 10 | volume << 2);
        int a = pairs[pair][0], b = pairs[pair][1];
        fifo_samples(g, a, b, control);
        a *= volume & 1 ? 4 : 2;
        b *= volume & 2 ? 4 : 2;
        int left = (route & 2 ? a : 0) + (route & 8 ? b : 0);
        int right = (route & 1 ? a : 0) + (route & 4 ? b : 0);
        sample_one(g, &c, expected(left, biases[bias], gains[gain]),
                   expected(right, biases[bias], gains[gain]));
        count++;
    }
    CHECK(count == 51840);
    /* Fixed numerical anchors include both former polarity reversals and
     * unchanged default-bias saturation, cancellation and silence. */
    const unsigned bias[] = {0, 0x3FE, 0, 0x3FE, 0x200, 0x200, 0x200, 0x200};
    const int a[] = {127,-128,0,0,127,-128,0,127};
    const int b[] = {127,-128,0,0,127,-128,0,-128};
    const int pcm[] = {24192,-24576,-24576,24480,24528,-24576,0,-192};
    g->audio.masterVolume = 256;
    for (unsigned i = 0; i < 8; i++) {
        write_bias(g, 2, (uint16_t)bias[i]);
        fifo_samples(g, a[i], b[i], 0x330C);
        sample_one(g, &c, pcm[i], pcm[i]);
    }
    for (unsigned width = 1; width <= 4; width *= 2) {
        cupid_io_write(g, REG_SOUNDCNT_X, 0);
        write_bias(g, width, 0x3FE);
        sample_one(g, &c, 0, 0);
        cupid_io_write(g, REG_SOUNDCNT_X, 0x80);
        fifo_samples(g, 0, 0, 0x330C);
        sample_one(g, &c, 24480, 24480);
    }
    printf("PASS %u routing/gain/access cases, 8 fixed vectors and 6 power captures\n", count);
    destroy(g);
}

static void advance(struct CupidGBA* g, unsigned clocks, bool punctual) {
    if (punctual) for (unsigned i = 0; i < clocks; i++) cupid_timing_tick(&g->timing, 1);
    else cupid_timing_tick(&g->timing, (int)clocks);
}

static void chronology(void) {
    const uint64_t origins[] = {0, INT32_MAX - 4u, UINT32_MAX - 4u, UINT64_C(0x300000000) - 4};
    unsigned count = 0;
    for (unsigned origin = 0; origin < 4; origin++)
    for (unsigned mode = 0; mode < 4; mode++)
    for (unsigned phase = 511; phase <= 513; phase++) {
        struct Capture c[2] = {{0}, {0}};
        for (unsigned punctual = 0; punctual < 2; punctual++) {
            struct CupidGBA* g = machine(origins[origin]);
            g->audio.sampleSink = capture;
            g->audio.sampleSinkContext = &c[punctual];
            write_bias(g, 2, 0);
            advance(g, phase, punctual);
            write_bias(g, 2, (uint16_t)(mode << 14 | 0x3FE));
            advance(g, 2048 - phase, punctual);
            unsigned interval = 512 >> mode, index = 0;
            for (unsigned clock = 512; clock <= 2048;
                 clock = clock == 512 && phase >= 512 ? 1024 : clock + interval) {
                CHECK(index < c[punctual].count);
                CHECK(c[punctual].clock[index] == origins[origin] + clock);
                int value = clock <= phase ? -24576 : 24480;
                CHECK(c[punctual].left[index] == value && c[punctual].right[index] == value);
                index++;
            }
            CHECK(index == c[punctual].count);
            destroy(g);
        }
        CHECK(c[0].count == c[1].count);
        CHECK(!memcmp(c[0].clock, c[1].clock, c[0].count * sizeof(c[0].clock[0])));
        CHECK(!memcmp(c[0].left, c[1].left, c[0].count * sizeof(c[0].left[0])));
        CHECK(!memcmp(c[0].right, c[1].right, c[0].count * sizeof(c[0].right[0])));
        count++;
    }
    CHECK(count == 48);
    printf("PASS %u punctual/late pairs across bias writes, divider changes and clock wrap\n", count);
}

struct FirmwareCapture {
    struct CupidGBA* g;
    unsigned count;
    uint64_t lastClock;
    int lastLevel;
    bool increasing;
};

static void firmware_capture(void* context, uint64_t clock, int16_t left, int16_t right) {
    struct FirmwareCapture* c = context;
    int level = cupid_io_read(c->g, REG_SOUNDBIAS) & 0x3FF;
    CHECK(clock > c->lastClock);
    CHECK((clock - c->g->audio.sampleOrigin) % (unsigned)c->g->audio.sampleInterval == 0);
    CHECK(left == (level - 512) * 48 && right == left);
    CHECK(c->increasing ? level >= c->lastLevel : level <= c->lastLevel);
    c->lastClock = clock;
    c->lastLevel = level;
    c->count++;
}

static void firmware(const char* bios) {
    unsigned count = 0, frames = 0;
    for (unsigned thumb = 0; thumb < 2; thumb++)
    for (unsigned mode = 0; mode < 4; mode++)
    for (unsigned increasing = 0; increasing < 2; increasing++) {
        struct CupidGBA* g = calloc(1, sizeof(*g));
        CHECK(g);
        cupid_gba_create(g);
        if (bios) CHECK(cupid_load_bios(g, bios));
        cupid_gba_reset(g);
        cupid_test_bios_caller(g);
        cupid_io_write(g, REG_SOUNDCNT_X, 0x80);
        write_bias(g, 2, (uint16_t)(mode << 14 | (increasing ? 0 : 512)));
        struct FirmwareCapture c = {g, 0, 0, increasing ? 0 : 512, increasing != 0};
        g->audio.sampleSink = firmware_capture;
        g->audio.sampleSinkContext = &c;
        const uint32_t arm[] = {0xEF190000, 0xEAFFFFFE};
        const uint16_t narrow[] = {0xDF19, 0xE7FE};
        memcpy(g->memory.iwram, thumb ? (const void*)narrow : (const void*)arm,
               thumb ? sizeof(narrow) : sizeof(arm));
        g->cpu.gprs[0] = (int32_t)increasing;
        _CupidSetMode(&g->cpu, thumb ? MODE_THUMB : MODE_ARM);
        g->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
        if (thumb) CupidThumbWritePC(&g->cpu);
        else CupidARMWritePC(&g->cpu);
        unsigned steps;
        for (steps = 0; steps < 100000; steps++) {
            cupid_gba_step(g);
            if (g->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
                (uint32_t)g->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + (thumb ? 4 : 8)) break;
        }
        CHECK(steps < 100000 && g->cpu.privilegeMode == MODE_SYS);
        CHECK(cupid_io_read(g, REG_SOUNDBIAS) == (mode << 14 | (increasing ? 512 : 0)));
        /* Commit the CPU's relative clocks before advancing the scheduler
         * directly to the first capture after firmware returns. */
        g->cpu.nextEvent = g->cpu.cycles;
        g->cpu.irqh.processEvents(&g->cpu);
        uint64_t now = cupid_timing_global_time(&g->timing);
        CHECK(g->audio.nextSample > now && g->audio.nextSample - now <= 512);
        cupid_timing_tick(&g->timing, (int)(g->audio.nextSample - now));
        CHECK(c.count > 20 && c.lastLevel == (increasing ? 512 : 0));
        frames += c.count;
        count++;
        destroy(g);
    }
    CHECK(count == 16);
    printf("PASS %u ARM/Thumb firmware ramps; %u scheduled stereo captures\n", count, frames);
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    if (!strcmp(argv[1], "levels") && argc == 2) levels();
    else if (!strcmp(argv[1], "routing") && argc == 2) routing();
    else if (!strcmp(argv[1], "chronology") && argc == 2) chronology();
    else if (!strcmp(argv[1], "firmware")) firmware(argc == 3 ? argv[2] : NULL);
    else return 2;
    return 0;
}
