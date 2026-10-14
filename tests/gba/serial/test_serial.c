/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"

static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void tick(struct CupidGBA* gba, unsigned cycles) {
    CHECK(gba->cpu.cycles == 0);
    cupid_timing_tick(&gba->timing, (int32_t)cycles);
}

static void configure(struct CupidGBA* gba, bool wide, bool fast, bool owner, uint32_t data) {
    cupid_io_write(gba, REG_RCNT, 0);
    cupid_io_write(gba, REG_SIOCNT, (wide ? 0x1000 : 0) | (fast ? 2 : 0) | owner);
    if (wide) {
        cupid_io_write(gba, REG_SIODATA32_LO, (uint16_t)data);
        cupid_io_write(gba, REG_SIODATA32_HI, (uint16_t)(data >> 16));
    } else {
        cupid_io_write(gba, REG_SIOMLT_SEND, (uint16_t)data);
    }
}

static void start(struct CupidGBA* gba, bool irq) {
    cupid_io_write8(gba, REG_SIOCNT + 1, (gba->serial.control >> 8) | (irq ? 0x40 : 0));
    cupid_io_write8(gba, REG_SIOCNT, (uint8_t)gba->serial.control | 0x80);
}

static uint32_t received(struct CupidGBA* gba, bool wide) {
    return wide ? cupid_io_read(gba, REG_SIODATA32_LO) |
                  ((uint32_t)cupid_io_read(gba, REG_SIODATA32_HI) << 16) :
                  cupid_io_read(gba, REG_SIOMLT_SEND);
}

struct Wave {
    struct CupidGBA* gba;
    uint32_t input;
    uint32_t sent;
    unsigned bits;
    unsigned width;
    bool clockHigh;
    uint64_t edges[64];
    unsigned edgeCount;
};

static void drive_wave(void* context, uint8_t mask, uint8_t levels, uint64_t timestamp) {
    struct Wave* wave = context;
    bool high = (levels & SERIAL_SC) != 0;
    if (!(mask & SERIAL_SC) || high == wave->clockHigh) return;
    wave->clockHigh = high;
    CHECK(wave->edgeCount < 64);
    wave->edges[wave->edgeCount++] = timestamp;
    if (high) return;
    CHECK(wave->bits < wave->width);
    wave->sent = (wave->sent << 1) | ((levels & SERIAL_SO) != 0);
    bool input = ((wave->input >> (wave->width - 1 - wave->bits)) & 1) != 0;
    wave->bits++;
    cupid_serial_set_input(&wave->gba->serial, SERIAL_SI, input ? SERIAL_SI : 0);
}

static void internal_clocks_and_patterns(void) {
    const uint32_t words[] = {0, UINT32_MAX, 0x80000001, 0x13579BDF, 0xAAAAAAAA, 0x55555555};
    for (unsigned wide = 0; wide < 2; wide++) {
        for (unsigned fast = 0; fast < 2; fast++) {
            unsigned cases = wide ? sizeof(words) / sizeof(*words) : 256;
            for (unsigned i = 0; i < cases; i++) {
                struct CupidGBA* gba = machine(100);
                uint32_t data = wide ? words[i] : 0xA500 | i;
                uint32_t input = wide ? ~data : (i * 73u) & 255;
                configure(gba, wide, fast, true, data);
                struct Wave wave = {.gba = gba, .input = input, .width = wide ? 32 : 8, .clockHigh = true};
                cupid_serial_set_output_callback(&gba->serial, drive_wave, &wave);
                start(gba, true);
                unsigned period = fast ? 8 : 64;
                unsigned duration = period * wave.width;
                tick(gba, duration);
                CHECK(wave.sent == (wide ? data : data & 255));
                CHECK(wave.bits == wave.width && wave.edgeCount == 2 * wave.width);
                for (unsigned edge = 1; edge < wave.edgeCount; edge++) {
                    CHECK(wave.edges[edge] - wave.edges[edge - 1] == period / 2);
                }
                CHECK(received(gba, wide) == data);
                CHECK(cupid_io_read(gba, REG_SIOCNT) & 0x80);
                CHECK(!gba->irqf);
                tick(gba, 5);
                CHECK(cupid_io_read(gba, REG_SIOCNT) & 0x80);
                tick(gba, 1);
                CHECK(!(cupid_io_read(gba, REG_SIOCNT) & 0x80));
                CHECK(received(gba, wide) == (wide ? input : 0xA500 | input));
                CHECK(!gba->irqf);
                tick(gba, 1);
                CHECK(gba->irqf == IRQ_SIO);
                cupid_serial_set_output_callback(&gba->serial, NULL, NULL);
                destroy(gba);
            }
        }
    }
}

static void external_clock_and_input_edges(void) {
    for (unsigned wide = 0; wide < 2; wide++) {
        struct CupidGBA* gba = machine(0);
        uint32_t output = wide ? 0x9248F00D : 0x5A;
        uint32_t input = wide ? 0x71C39A02 : 0xC3;
        configure(gba, wide, true, false, output);
        start(gba, true);
        tick(gba, 4096);
        CHECK(cupid_io_read(gba, REG_SIOCNT) & 0x80);
        CHECK(received(gba, wide) == output && !gba->irqf);
        CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->serial.edgeEvent));
        unsigned bits = wide ? 32 : 8;
        uint32_t sent = 0;
        for (unsigned bit = 0; bit < bits; bit++) {
            unsigned sample = ((input >> (bits - 1 - bit)) & 1) ? SERIAL_SI : 0;
            cupid_serial_set_input(&gba->serial, SERIAL_SC | SERIAL_SI, (uint8_t)sample);
            sent = (sent << 1) | ((cupid_serial_pins(&gba->serial) & SERIAL_SO) != 0);
            /* Repeating a level is not another shift-clock edge. */
            cupid_serial_set_input(&gba->serial, SERIAL_SC | SERIAL_SI, (uint8_t)sample);
            tick(gba, 3);
            cupid_serial_set_input(&gba->serial, SERIAL_SC | SERIAL_SI, (uint8_t)(sample | SERIAL_SC));
            tick(gba, 1);
        }
        CHECK(sent == output);
        tick(gba, 5);
        CHECK(received(gba, wide) == input);
        CHECK(!(cupid_io_read(gba, REG_SIOCNT) & 0x80));
        tick(gba, 1);
        CHECK(gba->irqf == IRQ_SIO);
        destroy(gba);
    }
}

struct Connection { struct CupidGBA* other; };

static void connect_pins(void* context, uint8_t mask, uint8_t levels, uint64_t timestamp) {
    struct CupidGBA* other = ((struct Connection*)context)->other;
    uint64_t now = cupid_timing_global_time(&other->timing);
    CHECK(timestamp >= now && timestamp - now <= INT32_MAX);
    if (timestamp > now) tick(other, (unsigned)(timestamp - now));
    uint8_t inputMask = (mask & SERIAL_SC) | ((mask & SERIAL_SO) ? SERIAL_SI : 0);
    uint8_t inputLevels = (levels & SERIAL_SC) | ((levels & SERIAL_SO) ? SERIAL_SI : 0);
    cupid_serial_set_input(&other->serial, inputMask, inputLevels);
}

static void two_connected_ports(void) {
    for (unsigned wide = 0; wide < 2; wide++) {
        struct CupidGBA* owner = machine(0);
        struct CupidGBA* receiver = machine(0);
        uint32_t a = wide ? 0x8AB32917 : 0x69;
        uint32_t b = wide ? 0x61E728D3 : 0xB6;
        configure(owner, wide, true, true, a);
        configure(receiver, wide, false, false, b);
        struct Connection toReceiver = {.other = receiver}, toOwner = {.other = owner};
        cupid_serial_set_output_callback(&owner->serial, connect_pins, &toReceiver);
        cupid_serial_set_output_callback(&receiver->serial, connect_pins, &toOwner);
        start(receiver, true);
        start(owner, true);
        tick(owner, (wide ? 256 : 64) + 9);
        /* The receiving clock has advanced through the last observed output;
         * dispatch its remaining completion and IRQ publication stages. */
        uint64_t now = cupid_timing_global_time(&receiver->timing);
        if (now < cupid_timing_global_time(&owner->timing)) tick(receiver, (unsigned)(cupid_timing_global_time(&owner->timing) - now));
        CHECK(received(owner, wide) == b && received(receiver, wide) == a);
        CHECK(owner->irqf == IRQ_SIO && receiver->irqf == IRQ_SIO);
        CHECK(!owner->serial.active && !receiver->serial.active);
        cupid_serial_set_output_callback(&owner->serial, NULL, NULL);
        cupid_serial_set_output_callback(&receiver->serial, NULL, NULL);
        destroy(owner);
        destroy(receiver);
    }
}

static void byte_registers_and_mode_access(void) {
    struct CupidGBA* gba = machine(0);
    configure(gba, true, true, true, 0x12345678);
    cupid_io_write8(gba, REG_SIOCNT + 1, 0x5F);
    CHECK(gba->serial.control == 0x5F03);
    start(gba, true);
    tick(gba, 37);
    unsigned remaining = gba->serial.remaining;
    uint32_t deadline = gba->serial.edgeEvent.when;
    cupid_io_write8(gba, REG_SIOCNT + 1, 0x1F);
    CHECK(gba->serial.remaining == remaining && gba->serial.edgeEvent.when == deadline);
    tick(gba, 300);
    CHECK(!gba->serial.active && !gba->irqf);
    cupid_io_write8(gba, REG_SIODATA32_LO, 0xA5);
    cupid_io_write8(gba, REG_SIODATA32_HI + 1, 0x5A);
    CHECK(received(gba, true) == 0x5AFFFFA5);
    cupid_io_write(gba, REG_SIOCNT, 0x2000);
    cupid_io_write(gba, REG_SIODATA32_LO, 0x9999);
    CHECK(cupid_io_read(gba, REG_SIODATA32_LO) == 0xFFA5);
    cupid_io_write(gba, REG_SIOCNT, 0);
    CHECK(cupid_io_read(gba, REG_SIODATA32_LO) == 0);
    cupid_io_write(gba, REG_JOYCNT, 0x40);
    gba->memory.io[REG_JOYCNT >> 1] |= 7;
    cupid_io_write8(gba, REG_JOYCNT + 1, 0xFF);
    CHECK(cupid_io_read(gba, REG_JOYCNT) == 0x47);
    cupid_io_write8(gba, REG_JOYCNT, 0x42);
    CHECK(cupid_io_read(gba, REG_JOYCNT) == 0x45);
    cupid_io_write(gba, REG_RCNT, 0xC000);
    cupid_io_write(gba, REG_JOYSTAT, 0xFFFF);
    CHECK(cupid_io_read(gba, REG_JOYSTAT) == 0x30);
    cupid_io_write8(gba, REG_JOYSTAT + 1, 0xFF);
    CHECK(cupid_io_read(gba, REG_JOYSTAT) == 0x30);
    destroy(gba);
}

static void gpio_directions_and_interrupts(void) {
    struct CupidGBA* gba = machine(0);
    for (unsigned directions = 0; directions < 16; directions++) {
        for (unsigned outputs = 0; outputs < 16; outputs++) {
            cupid_io_write(gba, REG_RCNT, (uint16_t)(0x8000 | directions << 4 | outputs));
            cupid_serial_set_input(&gba->serial, 15, 5);
            CHECK((cupid_io_read(gba, REG_RCNT) & 15) == ((outputs & directions) | (5 & ~directions)));
            cupid_serial_set_input(&gba->serial, 0, 0);
            CHECK((cupid_io_read(gba, REG_RCNT) & 15) == ((outputs & directions) | (15 & ~directions)));
        }
    }
    cupid_io_write(gba, REG_RCNT, 0x8100);
    cupid_serial_set_input(&gba->serial, SERIAL_SI, 0);
    tick(gba, 1);
    CHECK(gba->irqf == IRQ_SIO);
    cupid_io_write(gba, REG_IF, IRQ_SIO);
    tick(gba, 1);
    cupid_serial_set_input(&gba->serial, SERIAL_SI, 0);
    tick(gba, 1);
    CHECK(!gba->irqf);
    cupid_serial_set_input(&gba->serial, SERIAL_SI, SERIAL_SI);
    tick(gba, 1);
    CHECK(!gba->irqf);
    cupid_io_write(gba, REG_RCNT, 0x8144); /* SI output suppresses the input IRQ. */
    cupid_serial_set_input(&gba->serial, SERIAL_SI, 0);
    tick(gba, 1);
    CHECK(!gba->irqf && (cupid_serial_pins(&gba->serial) & SERIAL_SI));
    destroy(gba);
}

static void arm_and_thumb_bus_access(void) {
    const uint32_t arm[] = {0xE5C10000, 0xE1C100B0, 0xE5810000};
    const uint16_t thumb[] = {0x7008, 0x8008, 0x6008};
    for (unsigned mode = 0; mode < 2; mode++) {
        for (unsigned width = 0; width < 3; width++) {
            struct CupidGBA* gba = machine(0);
            configure(gba, false, true, true, 0x3C);
            cupid_io_write8(gba, REG_SIOCNT + 1, 0x40);
            if (mode) {
                const uint16_t code[] = {thumb[width], 0x46C0, 0x46C0};
                memcpy(gba->memory.iwram, code, sizeof(code));
                _CupidSetMode(&gba->cpu, MODE_THUMB);
                gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
                CupidThumbWritePC(&gba->cpu);
            } else {
                const uint32_t code[] = {arm[width], 0xE1A00000, 0xE1A00000};
                memcpy(gba->memory.iwram, code, sizeof(code));
                gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
                CupidARMWritePC(&gba->cpu);
            }
            gba->cpu.gprs[0] = (int32_t)0xA5344083;
            gba->cpu.gprs[1] = GBA_BASE_IO + REG_SIOCNT;
            cupid_gba_step(gba);
            CHECK(cupid_timing_global_time(&gba->timing) == 2);
            CHECK(gba->serial.active && gba->serial.control == 0x4083);
            CHECK(gba->serial.transmit == 0x3C);
            CHECK(gba->serial.edgeEvent.when == 6);
            CHECK(cupid_io_read(gba, REG_SIOMLT_SEND) == (width == 2 ? 0xA534 : 0x3C));
            gba->cpu.nextEvent = gba->cpu.cycles;
            gba->cpu.irqh.processEvents(&gba->cpu);
            tick(gba, 71);
            CHECK(!gba->serial.active && gba->irqf == IRQ_SIO);
            CHECK(cupid_io_read(gba, REG_SIOMLT_SEND) == (width == 2 ? 0xA5FF : 0xFF));
            destroy(gba);
        }
    }
}

static void late_dispatch_wrap_cancel_and_reset(void) {
    const uint64_t clocks[] = {0, INT32_MAX - 12u, UINT32_MAX - 12u, UINT64_C(0x300000000)};
    for (unsigned i = 0; i < sizeof(clocks) / sizeof(*clocks); i++) {
        struct CupidGBA* late = machine(clocks[i]);
        struct CupidGBA* punctual = machine(clocks[i]);
        struct Wave waves[2] = {
            {.gba = late, .input = 0xABCD1234, .width = 32, .clockHigh = true},
            {.gba = punctual, .input = 0xABCD1234, .width = 32, .clockHigh = true}};
        for (unsigned j = 0; j < 2; j++) {
            struct CupidGBA* gba = waves[j].gba;
            configure(gba, true, true, true, 0x196387B4);
            cupid_serial_set_output_callback(&gba->serial, drive_wave, &waves[j]);
            start(gba, true);
        }
        tick(late, 280);
        for (unsigned cycle = 0; cycle < 280; cycle++) tick(punctual, 1);
        CHECK(received(late, true) == 0xABCD1234 && received(punctual, true) == 0xABCD1234);
        CHECK(late->irqf == IRQ_SIO && punctual->irqf == IRQ_SIO);
        CHECK(waves[0].sent == waves[1].sent);
        CHECK(waves[0].edgeCount == waves[1].edgeCount);
        CHECK(!memcmp(waves[0].edges, waves[1].edges, sizeof(waves[0].edges)));
        for (unsigned j = 0; j < 2; j++) {
            struct CupidGBA* gba = waves[j].gba;
            cupid_serial_set_output_callback(&gba->serial, NULL, NULL);
            configure(gba, false, false, true, 0x3C);
            start(gba, false);
            tick(gba, 35);
            cupid_io_write(gba, REG_RCNT, 0x8000);
            CHECK(!gba->serial.active && !cupid_timing_is_scheduled(&gba->timing, &gba->serial.edgeEvent));
            cupid_gba_reset(gba);
            CHECK(cupid_serial_mode(&gba->serial) == SERIAL_GPIO);
            CHECK(!gba->serial.active && !cupid_timing_is_scheduled(&gba->timing, &gba->serial.edgeEvent));
            destroy(gba);
        }
    }
}

static void halt_and_stop(void) {
    struct CupidGBA* gba = machine(0);
    const uint32_t code[] = {0xE1A00000, 0xE1A00000, 0xE1A00000};
    memcpy(gba->memory.iwram, code, sizeof(code));
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    CupidARMWritePC(&gba->cpu);
    cupid_io_write(gba, REG_IE, IRQ_SIO);
    tick(gba, 2);
    configure(gba, false, true, true, 0xF0);
    start(gba, true);
    cupid_gba_halt(gba);
    for (unsigned step = 0; gba->cpu.halted && step < 30; step++) cupid_gba_step(gba);
    CHECK(!gba->cpu.halted && gba->irqf == IRQ_SIO && !gba->irq.line);
    CHECK(gba->cpu.cpsr.i && gba->cpu.privilegeMode == MODE_SYS);
    cupid_io_write(gba, REG_RCNT, 0x8100);
    cupid_io_write(gba, REG_IF, IRQ_SIO);
    gba->cpu.nextEvent = gba->cpu.cycles;
    gba->cpu.irqh.processEvents(&gba->cpu);
    tick(gba, 3);
    cupid_gba_stop(gba);
    CHECK(gba->stopped);
    uint64_t frozen = cupid_timing_global_time(&gba->timing);
    cupid_gba_run_frame(gba);
    for (unsigned i = 0; i < 20; i++) cupid_gba_step(gba);
    CHECK(gba->stopped && cupid_timing_global_time(&gba->timing) == frozen);
    cupid_serial_set_input(&gba->serial, SERIAL_SI, 0);
    CHECK(!gba->stopped);
    for (unsigned i = 0; gba->cpu.halted && i < 8; i++) cupid_gba_step(gba);
    CHECK(!gba->cpu.halted && gba->irqf == IRQ_SIO);
    destroy(gba);

    gba = machine(0);
    configure(gba, false, true, false, 0xA6);
    start(gba, true);
    cupid_io_write(gba, REG_IE, IRQ_SIO);
    tick(gba, 2);
    cupid_gba_stop(gba);
    unsigned remaining = gba->serial.remaining;
    cupid_serial_set_input(&gba->serial, SERIAL_SC | SERIAL_SI, 0);
    cupid_serial_set_input(&gba->serial, SERIAL_SC | SERIAL_SI, SERIAL_SC);
    cupid_raise_irq(gba, IRQ_SIO, 0);
    cupid_gba_step(gba);
    CHECK(gba->stopped && gba->serial.remaining == remaining);
    destroy(gba);
}

static void stop_freezes_peripherals_and_filters_wake_sources(void) {
    struct CupidGBA* gba = machine(0);
    cupid_gba_reset(gba); /* Keep the real video and audio event queues. */
    cupid_io_write(gba, REG_TM0CNT_L, 0x1000);
    cupid_io_write(gba, REG_TM0CNT_H, 0x80);
    cupid_io_write(gba, REG_IE, IRQ_TIMER0 | IRQ_KEYPAD);
    tick(gba, 2048);
    gba->memory.ewram[100] = 0x5A;
    gba->memory.activeRegion = GBA_REGION_ROM0;
    cupid_io_write8(gba, REG_HALTCNT, 0x80);
    CHECK(!gba->stopped);
    /* Enter STOP through a real RAM caller and its timed BIOS strobe. */
    const uint32_t sleepCall[] = {0xEF030000, 0xEAFFFFFE};
    memcpy(gba->memory.iwram, sleepCall, sizeof(sleepCall));
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
    CupidARMWritePC(&gba->cpu);
    unsigned steps;
    for (steps = 0; steps < 2000 && !gba->stopped; steps++) cupid_gba_step(gba);
    CHECK(steps < 2000);
    CHECK(gba->stopped && gba->cpu.halted);
    uint64_t clock = cupid_timing_global_time(&gba->timing);
    uint16_t timer = cupid_io_read(gba, REG_TM0CNT_L);
    uint16_t line = cupid_io_read(gba, REG_VCOUNT);
    int samples = gba->audio.sampleWritePos;
    for (unsigned i = 0; i < 20; i++) cupid_gba_run_frame(gba);
    CHECK(cupid_timing_global_time(&gba->timing) == clock);
    CHECK(cupid_io_read(gba, REG_TM0CNT_L) == timer);
    CHECK(cupid_io_read(gba, REG_VCOUNT) == line && gba->audio.sampleWritePos == samples);
    CHECK(gba->memory.ewram[100] == 0x5A);
    cupid_raise_irq(gba, IRQ_TIMER0, 0);
    CHECK(gba->stopped); /* Enabled timers cannot wake STOP. */
    cupid_raise_irq(gba, IRQ_GAMEPAK, 0);
    CHECK(gba->stopped); /* This external source is disabled in IE. */
    cupid_raise_irq(gba, IRQ_KEYPAD, 0);
    CHECK(!gba->stopped && cupid_timing_global_time(&gba->timing) == clock);
    cupid_gba_reset(gba);
    CHECK(!gba->stopped && !gba->cpu.halted);
    destroy(gba);
}

int main(void) {
    RUN(internal_clocks_and_patterns);
    RUN(external_clock_and_input_edges);
    RUN(two_connected_ports);
    RUN(byte_registers_and_mode_access);
    RUN(gpio_directions_and_interrupts);
    RUN(arm_and_thumb_bus_access);
    RUN(late_dispatch_wrap_cancel_and_reset);
    RUN(halt_and_stop);
    RUN(stop_freezes_peripherals_and_filters_wake_sources);
    return 0;
}
