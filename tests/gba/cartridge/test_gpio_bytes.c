/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static const uint8_t raw[] = {0x33, 0x44, 0x77, 0x88, 0xCC, 0xDD, 0x5A, 0xA5};

static uint8_t input_pins(void* context) { return *(uint8_t*)context; }

static struct CupidGBA* machine(uint8_t* input) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    gba->memory.rom = calloc(1, 4096);
    CHECK(gba->memory.rom);
    gba->memory.romSize = gba->memory.romImageSize = 4096;
    gba->memory.romMask = 4095;
    memcpy(gba->memory.rom + 0xC4, raw, sizeof(raw));
    cupid_gba_reset(gba);
    cupid_cartridge_set_input(gba, input_pins, input);
    return gba;
}

static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }

static void values(void) {
    unsigned frames = 0;
    for (unsigned bank = 0; bank < 3; ++bank) for (unsigned lane = 0; lane < 2; ++lane) {
        uint8_t input = 0;
        struct CupidGBA* gba = machine(&input);
        const uint32_t port = 0x080000C4 + bank * 0x02000000;
        for (unsigned value = 0; value < 256; ++value) {
            cupid_memory_store16(&gba->cpu, port + 4, 1, NULL);
            cupid_memory_store16(&gba->cpu, port + 2, 15, NULL);
            cupid_memory_store16(&gba->cpu, port, (int16_t)(~value & 15), NULL);
            int cycles = 0;
            cupid_memory_store8(&gba->cpu, port + lane, (int8_t)value, &cycles);
            CHECK(cycles == 5);
            CHECK(gba->cartridge.output == (value & 15));
            CHECK(cupid_memory_loadU16(&gba->cpu, port, NULL) == (value & 15));
            input = (uint8_t)((value ^ 10) & 15);
            cupid_memory_store8(&gba->cpu, port + 2 + lane, (int8_t)value, NULL);
            const unsigned direction = value & 15;
            const unsigned pins = (value & direction) | (input & ~direction & 15);
            CHECK(cupid_memory_loadU16(&gba->cpu, port + 2, NULL) == direction);
            CHECK(cupid_memory_loadU16(&gba->cpu, port, NULL) == pins);
            cupid_memory_store8(&gba->cpu, port + 4 + lane, (int8_t)value, NULL);
            CHECK(gba->cartridge.readable == ((value & 1) != 0));
            CHECK(cupid_memory_loadU16(&gba->cpu, port, NULL) == (value & 1 ? pins : 0x4433));
            CHECK(cupid_memory_load8(&gba->cpu, port + 1, NULL) == (value & 1 ? 0 : 0x44));
            CHECK(!memcmp(gba->memory.rom + 0xC4, raw, sizeof(raw)));
            CHECK(gba->memory.savedata.type == SAVE_AUTODETECT && !gba->memory.savedata.dirty);
            ++frames;
        }
        /* Upper-half aliases and neighbouring addresses have no GPIO strobe. */
        cupid_memory_store16(&gba->cpu, port + 4, 1, NULL);
        cupid_memory_store16(&gba->cpu, port + 2, 15, NULL);
        cupid_memory_store16(&gba->cpu, port, 9, NULL);
        cupid_memory_store8(&gba->cpu, port + 0x01000000 + lane, 2, NULL);
        cupid_memory_store8(&gba->cpu, port + 6 + lane, 2, NULL);
        CHECK(gba->cartridge.output == 9 && gba->cartridge.direction == 15 && gba->cartridge.readable);
        cupid_cartridge_set_input(gba, NULL, NULL);
        cupid_memory_store8(&gba->cpu, port + lane, 2, NULL);
        CHECK(gba->cartridge.output == 9);
        CHECK(!memcmp(gba->memory.rom + 0xC4, raw, sizeof(raw)));
        destroy(gba);
    }
    CHECK(frames == 1536);
    printf("PASS %u complete GPIO byte value/direction/control frames\n", frames);
}

struct Sample {
    struct CupidGBA* gba;
    uint64_t clock;
    uint16_t pins;
    bool called;
};

static void observe(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct Sample* sample = context;
    sample->clock = cupid_timing_global_time(timing);
    sample->pins = (uint16_t)cupid_memory_loadU16(&sample->gba->cpu, 0x080000C4, NULL);
    sample->called = true;
}

static void strobes(void) {
    static const unsigned nonseq[] = {5, 4, 3, 9};
    static const unsigned sequential[] = {3, 5, 9};
    static const unsigned nshift[] = {2, 5, 8}, sshift[] = {4, 7, 10};
    static const uint64_t epochs[] = {0, UINT32_MAX - 4u, UINT64_C(0x200000000)};
    unsigned cases = 0;
    for (unsigned bank = 0; bank < 3; ++bank) for (unsigned n = 0; n < 4; ++n)
        for (unsigned s = 0; s < 2; ++s) for (unsigned seq = 0; seq < 2; ++seq)
            for (unsigned prefetch = 0; prefetch < 2; ++prefetch)
                for (unsigned epoch = 0; epoch < 3; ++epoch) for (unsigned lane = 0; lane < 2; ++lane) {
                    uint8_t input = 0;
                    struct CupidGBA* gba = machine(&input);
                    cupid_memory_store16(&gba->cpu, 0x080000C8, 1, NULL);
                    cupid_memory_store16(&gba->cpu, 0x080000C6, 15, NULL);
                    cupid_memory_store16(&gba->cpu, 0x080000C4, 1, NULL);
                    cupid_memory_store16(&gba->cpu, GBA_BASE_IO + REG_WAITCNT,
                        (int16_t)((n << nshift[bank]) | (s << sshift[bank]) | (prefetch << 14)), NULL);
                    cupid_timing_clear(&gba->timing);
                    gba->timing.globalCycles = epochs[epoch];
                    gba->timing.masterCycles = (uint32_t)epochs[epoch];
                    cupid_timer_write_reload(gba, 0, 0xFFFE);
                    cupid_timer_write_control(gba, 0, 0xC0);
                    /* Finish control acceptance and the enable delay before
                     * measuring timer progress through the byte bus cycle. */
                    cupid_timing_tick(&gba->timing, 2);
                    CHECK(cupid_timer_read_counter(gba, 0) == 0xFFFE);
                    const uint64_t start = cupid_timing_global_time(&gba->timing);
                    const unsigned clocks = seq ? (s ? 2 : sequential[bank]) : nonseq[n];
                    struct Sample samples[3] = {{.gba = gba}, {.gba = gba}, {.gba = gba}};
                    struct CupidTimingEvent events[3] = {0};
                    for (unsigned i = 0; i < 3; ++i) {
                        events[i] = (struct CupidTimingEvent){.context = &samples[i], .callback = observe, .priority = 0x60};
                        cupid_timing_schedule(&gba->timing, &events[i], (int32_t)(clocks + i - 1));
                    }
                    gba->cpu.memory.timed = true;
                    gba->cpu.memory.dataSequential = seq != 0;
                    int cycles = 0;
                    cupid_memory_store8(&gba->cpu, 0x080000C4 + bank * 0x02000000 + lane, 9, &cycles);
                    CHECK(cycles == 0 && cupid_timing_global_time(&gba->timing) == start + clocks);
                    CHECK(samples[0].called && samples[1].called && !samples[2].called);
                    CHECK(samples[0].pins == 1 && samples[1].pins == 1 && gba->cartridge.output == 9);
                    CHECK(samples[0].clock == start + clocks - 1 && samples[1].clock == start + clocks);
                    CHECK(cupid_timer_read_counter(gba, 0) == (uint16_t)(0xFFFE + clocks % 2));
                    CHECK((gba->irqf & IRQ_TIMER0) == (clocks >= 3 ? IRQ_TIMER0 : 0));
                    cupid_timing_tick(&gba->timing, 1);
                    CHECK(samples[2].called && samples[2].pins == 9 && samples[2].clock == start + clocks + 1);
                    CHECK(gba->irqf & IRQ_TIMER0);
                    CHECK(cupid_timer_read_counter(gba, 0) == (uint16_t)(0xFFFE + (clocks + 1) % 2));
                    CHECK(!memcmp(gba->memory.rom + 0xC4, raw, sizeof(raw)));
                    destroy(gba);
                    ++cases;
                }
    CHECK(cases == 576);
    printf("PASS %u byte-strobe/wait/timer/IRQ cases\n", cases);
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "values")) values();
    else if (!strcmp(argv[1], "strobes")) strobes();
    else CHECK(false);
    return 0;
}
