/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"

enum { CTS = 4, ODD = 8, FULL = 16, EMPTY = 32, ERROR = 64, EIGHT = 128,
       FIFO = 256, PARITY = 512, SEND = 1024, RECEIVE = 2048, IRQ = 16384 };

/* Independent nominal half-bit deadlines, rounded up in system clocks. */
static const unsigned centers[4][23] = {
    {0,874,1748,2622,3496,4370,5243,6117,6991,7865,8739,9612,10486,11360,12234,13108,13982,14855,15729,16603,17477,18351,19224},
    {0,219,437,656,874,1093,1311,1530,1748,1967,2185,2403,2622,2840,3059,3277,3496,3714,3933,4151,4370,4588,4806},
    {0,146,292,437,583,729,874,1020,1166,1311,1457,1602,1748,1894,2039,2185,2331,2476,2622,2768,2913,3059,3204},
    {0,73,146,219,292,365,437,510,583,656,729,801,874,947,1020,1093,1166,1238,1311,1384,1457,1530,1602}
};

static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    gba->audio.lastSample = gba->audio.sampleOrigin = clock;
    gba->audio.nextSample = clock + 512;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    return gba;
}

static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }
static void tick(struct CupidGBA* gba, unsigned clocks) {
    CHECK(!gba->cpu.cycles);
    cupid_timing_tick(&gba->timing, (int32_t)clocks);
}
static uint16_t control(struct CupidGBA* gba) { return cupid_io_read(gba, REG_SIOCNT); }
static uint8_t data(struct CupidGBA* gba) { return (uint8_t)cupid_io_read(gba, REG_SIOMLT_SEND); }
static void configure(struct CupidGBA* gba, uint16_t flags) {
    cupid_io_write(gba, REG_RCNT, 0);
    cupid_io_write(gba, REG_SIOCNT, 0x3000);
    cupid_io_write(gba, REG_SIOCNT, (uint16_t)(0x3000 | flags));
}

static void loopback(void* context, uint8_t mask, uint8_t levels, uint64_t clock) {
    struct CupidGBA* gba = context;
    CHECK(mask == (SERIAL_SO | SERIAL_SD));
    CHECK(clock == cupid_timing_global_time(&gba->timing));
    uint8_t input = ((levels & SERIAL_SO) >> 1) | ((levels & SERIAL_SD) >> 1);
    cupid_serial_set_input(&gba->serial, SERIAL_SI | SERIAL_SC, input);
}

static void complete_format_matrix(void) {
    unsigned frames = 0, samples = 0;
    for (unsigned baud = 0; baud < 4; ++baud) for (unsigned bits = 7; bits <= 8; ++bits)
      for (unsigned parity = 0; parity < 3; ++parity) for (unsigned fifo = 0; fifo < 2; ++fifo) {
        struct CupidGBA* gba = machine(0);
        uint16_t flags = (uint16_t)(baud | SEND | RECEIVE | (bits == 8 ? EIGHT : 0)
            | (parity ? PARITY : 0) | (parity == 2 ? ODD : 0) | (fifo ? FIFO : 0));
        configure(gba, flags);
        cupid_serial_set_output_callback(&gba->serial, loopback, gba);
        for (unsigned value = 0; value < 256; ++value) {
            unsigned sent = value & ((1u << bits) - 1);
            unsigned ones = 0;
            for (unsigned bit = 0; bit < bits; ++bit) ones += (sent >> bit) & 1;
            unsigned cells = 1 + bits + (parity != 0) + 1;
            CHECK((control(gba) & (FULL | EMPTY | ERROR)) == EMPTY);
            cupid_io_write(gba, REG_SIOMLT_SEND, (uint16_t)(0xAB00 | value));
            CHECK(((control(gba) & FULL) != 0) == !fifo);
            unsigned elapsed = 0;
            for (unsigned cell = 0; cell < cells; ++cell) {
                unsigned deadline = centers[baud][cell * 2 + 1];
                tick(gba, deadline - elapsed);
                elapsed = deadline;
                unsigned expected;
                if (!cell) expected = 0;
                else if (cell <= bits) expected = (sent >> (cell - 1)) & 1;
                else if (parity && cell == bits + 1) expected = (ones + (parity == 2)) & 1;
                else expected = 1;
                CHECK(((cupid_serial_pins(&gba->serial) & SERIAL_SO) != 0) == (expected != 0));
                ++samples;
            }
            CHECK(!(control(gba) & (EMPTY | ERROR)));
            CHECK(data(gba) == sent);
            CHECK(control(gba) & EMPTY);
            tick(gba, centers[baud][cells * 2] - elapsed);
            CHECK(!gba->serial.uart.transmitting && !gba->serial.uart.receiving);
            CHECK((control(gba) & (FULL | EMPTY | ERROR)) == EMPTY);
            ++frames;
        }
        destroy(gba);
    }
    CHECK(frames == 12288 && samples == 124928);
    printf("PASS UART format matrix: %u complete transmitted/received frames, %u bit-center samples\n", frames, samples);
}

static void inject(struct CupidGBA* gba, unsigned baud, unsigned bits, unsigned value,
                   int parity, bool goodParity, bool stop) {
    cupid_serial_set_input(&gba->serial, SERIAL_SI, SERIAL_SI);
    cupid_serial_set_input(&gba->serial, SERIAL_SI, 0);
    unsigned elapsed = 0, ones = 0;
    for (unsigned bit = 0; bit < bits; ++bit) {
        unsigned deadline = centers[baud][(bit + 1) * 2];
        tick(gba, deadline - elapsed);
        elapsed = deadline;
        unsigned high = (value >> bit) & 1;
        ones += high;
        cupid_serial_set_input(&gba->serial, SERIAL_SI, high ? SERIAL_SI : 0);
    }
    unsigned cell = bits + 1;
    if (parity) {
        unsigned deadline = centers[baud][cell * 2];
        tick(gba, deadline - elapsed);
        elapsed = deadline;
        bool high = ((ones + (parity == 2)) & 1) != 0;
        if (!goodParity) high = !high;
        cupid_serial_set_input(&gba->serial, SERIAL_SI, high ? SERIAL_SI : 0);
        ++cell;
    }
    tick(gba, centers[baud][cell * 2] - elapsed);
    cupid_serial_set_input(&gba->serial, SERIAL_SI, stop ? SERIAL_SI : 0);
    tick(gba, centers[baud][(cell + 1) * 2] - centers[baud][cell * 2]);
    cupid_serial_set_input(&gba->serial, SERIAL_SI, SERIAL_SI);
}

static void fifo_capacity_and_flow_control(void) {
    struct CupidGBA* gba = machine(0);
    configure(gba, 3 | FIFO | EIGHT | RECEIVE | CTS);
    cupid_serial_set_input(&gba->serial, SERIAL_SI | SERIAL_SC, SERIAL_SI | SERIAL_SC);
    for (unsigned i = 0; i < 4; ++i) cupid_io_write(gba, REG_SIOMLT_SEND, (uint16_t)(0x50 + i));
    CHECK((control(gba) & FULL) && !gba->serial.uart.transmitting);
    CHECK(!(cupid_serial_pins(&gba->serial) & SERIAL_SD));
    cupid_io_write(gba, REG_SIOCNT, 0x3D87); /* Still blocked by high CTS. */
    CHECK(!gba->serial.uart.transmitting && (control(gba) & FULL));
    cupid_serial_set_input(&gba->serial, SERIAL_SI | SERIAL_SC, SERIAL_SI);
    CHECK(gba->serial.uart.transmitting && gba->serial.uart.transmitCount == 3);
    CHECK(!(control(gba) & FULL));
    cupid_io_write(gba, REG_SIOMLT_SEND, 0x54);
    CHECK(gba->serial.uart.transmitCount == 4 && (control(gba) & FULL));
    tick(gba, 300);
    cupid_serial_set_input(&gba->serial, SERIAL_SI | SERIAL_SC, SERIAL_SI | SERIAL_SC);
    tick(gba, 1457 - 300);
    CHECK(!gba->serial.uart.transmitting && gba->serial.uart.transmitCount == 4);
    CHECK(control(gba) & FULL);
    for (unsigned i = 0; i < 4; ++i) inject(gba, 3, 8, 0xA0 + i, 0, true, true);
    CHECK(gba->serial.uart.receiveCount == 4);
    CHECK(cupid_serial_pins(&gba->serial) & SERIAL_SD);
    CHECK(!(control(gba) & (EMPTY | ERROR)));
    CHECK(data(gba) == 0xA0);
    CHECK(!(cupid_serial_pins(&gba->serial) & SERIAL_SD));
    for (unsigned i = 1; i < 4; ++i) CHECK(data(gba) == 0xA0 + i);
    CHECK(control(gba) & EMPTY);
    destroy(gba);
}

static void errors_reject_data_and_acknowledge_on_status_read(void) {
    for (unsigned fifo = 0; fifo < 2; ++fifo) {
        struct CupidGBA* gba = machine(0);
        configure(gba, (uint16_t)(3 | EIGHT | RECEIVE | PARITY | IRQ | (fifo ? FIFO : 0)));
        inject(gba, 3, 8, 0x53, 1, false, true);
        CHECK(gba->irq.pendingIF & IRQ_SIO);
        CHECK((control(gba) & (ERROR | EMPTY)) == (ERROR | EMPTY));
        CHECK(!(control(gba) & ERROR));
        inject(gba, 3, 8, 0xA6, 1, true, false);
        CHECK((control(gba) & (ERROR | EMPTY)) == (ERROR | EMPTY));
        CHECK(!(control(gba) & ERROR));
        unsigned capacity = fifo ? 4 : 1;
        for (unsigned i = 0; i < capacity; ++i) inject(gba, 3, 8, 0x40 + i, 1, true, true);
        CHECK(!(control(gba) & (ERROR | EMPTY)));
        inject(gba, 3, 8, 0xFE, 1, true, true);
        CHECK(control(gba) & ERROR);
        CHECK(!(control(gba) & ERROR));
        for (unsigned i = 0; i < capacity; ++i) CHECK(data(gba) == 0x40 + i);
        CHECK(control(gba) & EMPTY);
        /* A false start must not publish a byte or a framing error. */
        cupid_serial_set_input(&gba->serial, SERIAL_SI, 0);
        tick(gba, 20);
        cupid_serial_set_input(&gba->serial, SERIAL_SI, SERIAL_SI);
        tick(gba, 2000);
        CHECK((control(gba) & (ERROR | EMPTY)) == EMPTY);
        destroy(gba);
    }
}

static void fifo_and_register_irq_conditions(void) {
    struct CupidGBA* gba = machine(0);
    configure(gba, 3 | FIFO | EIGHT | SEND | RECEIVE | IRQ);
    CHECK(!(gba->irq.pendingIF & IRQ_SIO));
    cupid_io_write(gba, REG_SIOMLT_SEND, 0x5A);
    CHECK(gba->serial.uart.transmitting && !gba->serial.uart.transmitCount);
    CHECK(gba->irq.pendingIF & IRQ_SIO); /* Empty FIFO, while the shifter is busy. */
    tick(gba, 2);
    CHECK(gba->memory.io[REG_IF >> 1] & IRQ_SIO);
    cupid_io_write(gba, REG_IF, IRQ_SIO);
    tick(gba, 1457);
    CHECK(!(gba->irq.pendingIF & IRQ_SIO)); /* No second FIFO IRQ at the stop bit. */
    for (unsigned i = 0; i < 3; ++i) {
        inject(gba, 3, 8, i, 0, true, true);
        CHECK(!(gba->irq.pendingIF & IRQ_SIO));
    }
    inject(gba, 3, 8, 3, 0, true, true);
    CHECK(gba->irq.pendingIF & IRQ_SIO);
    for (unsigned i = 0; i < 4; ++i) CHECK(data(gba) == i);
    cupid_io_write(gba, REG_IF, IRQ_SIO);
    configure(gba, 3 | EIGHT | SEND | RECEIVE | IRQ);
    cupid_io_write(gba, REG_SIOMLT_SEND, 0xA5);
    CHECK(!(gba->irq.pendingIF & IRQ_SIO));
    tick(gba, 1456);
    CHECK(!(gba->irq.pendingIF & IRQ_SIO));
    tick(gba, 1);
    CHECK(gba->irq.pendingIF & IRQ_SIO);
    destroy(gba);
}

struct Cable;
struct Endpoint { struct Cable* cable; unsigned id; };
struct Cable { struct CupidGBA* boards[2]; struct Endpoint ends[2]; };

static void advance_to(struct CupidGBA* gba, uint64_t target) {
    uint64_t now = cupid_timing_global_time(&gba->timing);
    CHECK(now <= target && target - now <= INT32_MAX);
    if (now != target) tick(gba, (unsigned)(target - now));
}

static void cable_output(void* context, uint8_t mask, uint8_t levels, uint64_t timestamp) {
    struct Endpoint* endpoint = context;
    struct CupidGBA* other = endpoint->cable->boards[endpoint->id ^ 1];
    CHECK(mask == (SERIAL_SO | SERIAL_SD));
    /* The cable owns inter-core synchronization. Never deliver a past edge. */
    advance_to(other, timestamp);
    cupid_serial_set_input(&other->serial, SERIAL_SI | SERIAL_SC,
                          ((levels & SERIAL_SO) >> 1) | ((levels & SERIAL_SD) >> 1));
}

static void connect(struct Cable* cable, bool fifo, unsigned baud) {
    for (unsigned i = 0; i < 2; ++i) {
        cable->boards[i] = machine(UINT32_MAX - 100u);
        cable->ends[i] = (struct Endpoint){cable, i};
        configure(cable->boards[i], (uint16_t)(baud | EIGHT | SEND | RECEIVE | CTS | IRQ | (fifo ? FIFO : 0)));
    }
    for (unsigned i = 0; i < 2; ++i)
        cupid_serial_set_output_callback(&cable->boards[i]->serial, cable_output, &cable->ends[i]);
}

static void run_cable(struct Cable* cable, unsigned clocks, unsigned first) {
    uint64_t start = cupid_timing_global_time(&cable->boards[0]->timing);
    CHECK(start == cupid_timing_global_time(&cable->boards[1]->timing));
    for (unsigned step = 1; step <= clocks; ++step) {
        advance_to(cable->boards[first], start + step);
        advance_to(cable->boards[first ^ 1], start + step);
    }
}

static void bidirectional_peers_exchange_complete_bytes(void) {
    static const uint8_t bytes[2][4] = {{0xA6,0x5B,0,0xFF}, {0x3C,0xC3,0x7E,0x81}};
    unsigned received = 0;
    for (unsigned baud = 0; baud < 4; ++baud) for (unsigned fifo = 0; fifo < 2; ++fifo)
      for (unsigned first = 0; first < 2; ++first) {
        struct Cable cable = {0};
        connect(&cable, fifo != 0, baud);
        for (unsigned byte = 0; byte < 4; ++byte) {
            for (unsigned board = 0; board < 2; ++board)
                cupid_io_write(cable.boards[board], REG_SIOMLT_SEND, bytes[board][byte]);
            if (!fifo) {
                run_cable(&cable, centers[baud][20] + 2, first);
                for (unsigned board = 0; board < 2; ++board) {
                    CHECK(!(control(cable.boards[board]) & (EMPTY | ERROR | FULL)));
                    CHECK(data(cable.boards[board]) == bytes[board ^ 1][byte]);
                    ++received;
                }
            }
        }
        if (fifo) {
            run_cable(&cable, centers[baud][20] * 4 + 2, first);
            for (unsigned board = 0; board < 2; ++board) {
                CHECK(!(control(cable.boards[board]) & (EMPTY | ERROR | FULL)));
                CHECK(cable.boards[board]->serial.uart.receiveCount == 4);
                for (unsigned byte = 0; byte < 4; ++byte) {
                    CHECK(data(cable.boards[board]) == bytes[board ^ 1][byte]);
                    ++received;
                }
            }
        }
        for (unsigned board = 0; board < 2; ++board) {
            CHECK(cable.boards[board]->irq.pendingIF & IRQ_SIO);
            CHECK(!cable.boards[board]->serial.uart.transmitting && !cable.boards[board]->serial.uart.receiving);
            destroy(cable.boards[board]);
        }
    }
    CHECK(received == 128);
    /* One active byte plus four queued bytes must respect the peer's RTS. */
    struct Cable cable = {0};
    connect(&cable, true, 3);
    for (unsigned byte = 0; byte < 5; ++byte)
        cupid_io_write(cable.boards[0], REG_SIOMLT_SEND, (uint16_t)(0x60 + byte));
    run_cable(&cable, centers[3][20] * 4 + 2, 0);
    CHECK(cable.boards[1]->serial.uart.receiveCount == 4);
    CHECK(!cable.boards[0]->serial.uart.transmitting && cable.boards[0]->serial.uart.transmitCount == 1);
    CHECK(data(cable.boards[1]) == 0x60);
    CHECK(cable.boards[0]->serial.uart.transmitting);
    run_cable(&cable, centers[3][20] + 2, 1);
    for (unsigned byte = 1; byte < 5; ++byte) CHECK(data(cable.boards[1]) == 0x60 + byte);
    CHECK(!(control(cable.boards[1]) & ERROR));
    destroy(cable.boards[0]); destroy(cable.boards[1]);
    printf("PASS UART peers: %u bidirectional bytes and a CTS-blocked fifth byte\n", received);
}

struct PinLog {
    struct CupidGBA* gba;
    unsigned count;
    struct { uint64_t timestamp; uint8_t mask, levels; } events[128];
};

static void record_loopback(void* context, uint8_t mask, uint8_t levels, uint64_t timestamp) {
    struct PinLog* log = context;
    CHECK(log->count < 128);
    log->events[log->count].timestamp = timestamp;
    log->events[log->count].mask = mask;
    log->events[log->count++].levels = levels;
    loopback(log->gba, mask, levels, timestamp);
}

static void timer_dma_and_late_dispatch_retain_waveforms(void) {
    const uint64_t origins[] = {0, UINT32_MAX - 100u, UINT64_C(0x300000000)};
    static const uint8_t bytes[] = {0x12,0x34,0x56,0x78};
    for (unsigned origin = 0; origin < 3; ++origin) {
        struct CupidGBA* boards[] = {machine(origins[origin]), machine(origins[origin])};
        struct PinLog logs[2] = {{.gba=boards[0]}, {.gba=boards[1]}};
        for (unsigned i = 0; i < 2; ++i) {
            struct CupidGBA* gba = boards[i];
            configure(gba, 3 | FIFO | EIGHT | SEND | RECEIVE | IRQ);
            cupid_serial_set_output_callback(&gba->serial, record_loopback, &logs[i]);
            for (unsigned byte = 0; byte < 4; ++byte) cupid_io_write(gba, REG_SIOMLT_SEND, bytes[byte]);
            for (unsigned byte = 0; byte < 256; ++byte) gba->memory.iwram[0x100 + byte] = (uint8_t)(byte ^ 0xA5);
            cupid_io_write(gba, REG_TM0CNT_L, 0xFF00);
            cupid_io_write(gba, REG_TM0CNT_H, 0x00C0);
            cupid_dma_write_sad(gba, 3, GBA_BASE_IWRAM + 0x100);
            cupid_dma_write_dad(gba, 3, GBA_BASE_EWRAM + 0x200);
            cupid_dma_write_cnt_lo(gba, 3, 128);
            cupid_dma_write_cnt(gba, 3, 0xC000);
        }
        tick(boards[0], 10000);
        for (unsigned clock = 0; clock < 10000; ++clock) tick(boards[1], 1);
        CHECK(logs[0].count == logs[1].count && logs[0].count > 20);
        for (unsigned edge = 0; edge < logs[0].count; ++edge) {
            CHECK(logs[0].events[edge].timestamp == logs[1].events[edge].timestamp);
            CHECK(logs[0].events[edge].mask == logs[1].events[edge].mask);
            CHECK(logs[0].events[edge].levels == logs[1].events[edge].levels);
        }
        for (unsigned i = 0; i < 2; ++i) {
            struct CupidGBA* gba = boards[i];
            CHECK((gba->irq.pendingIF & (IRQ_SIO | IRQ_TIMER0 | IRQ_DMA3)) == (IRQ_SIO | IRQ_TIMER0 | IRQ_DMA3));
            CHECK(!gba->memory.dmaRunning && !gba->memory.dma[3].enable);
            for (unsigned byte = 0; byte < 256; ++byte) CHECK(gba->memory.ewram[0x200 + byte] == (uint8_t)(byte ^ 0xA5));
            CHECK(!(control(gba) & (ERROR | EMPTY)));
            for (unsigned byte = 0; byte < 4; ++byte) CHECK(data(gba) == bytes[byte]);
        }
        CHECK(cupid_timer_read_counter(boards[0], 0) == cupid_timer_read_counter(boards[1], 0));
        CHECK(boards[0]->irqf == boards[1]->irqf);
        destroy(boards[0]); destroy(boards[1]);
    }
}

static void halt_stop_cancellation_and_access_widths(void) {
    for (unsigned thumb = 0; thumb < 2; ++thumb) {
        struct CupidGBA* gba = machine(0);
        const uint32_t instruction = thumb ? 0x46C0275A : 0xE3A0705A;
        memcpy(gba->memory.iwram + 0x1000, &instruction, sizeof(instruction));
        _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
        gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000;
        if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
        cupid_io_write(gba, REG_IE, IRQ_SIO);
        tick(gba, 1);
        configure(gba, 3 | EIGHT | SEND | IRQ);
        cupid_io_write8(gba, REG_SIOMLT_SEND + 1, 0xA5);
        CHECK(!(control(gba) & FULL) && !gba->serial.uart.transmitting);
        cupid_io_write8(gba, REG_SIOMLT_SEND, 0x5A);
        CHECK(control(gba) & FULL);
        cupid_gba_halt(gba);
        for (unsigned step = 0; step < 100 && gba->cpu.halted; ++step) cupid_gba_step(gba);
        CHECK(!gba->cpu.halted && (gba->irq.pendingIF & IRQ_SIO));
        if (gba->cpu.gprs[7] != 0x5A) cupid_gba_step(gba);
        CHECK(gba->cpu.gprs[7] == 0x5A && !gba->ime);
        CHECK(cupid_timing_global_time(&gba->timing) >= 1458);
        destroy(gba);
    }
    struct CupidGBA* gba = machine(0);
    cupid_io_write(gba, REG_IE, IRQ_KEYPAD);
    tick(gba, 1);
    configure(gba, 3 | EIGHT | SEND | CTS);
    cupid_serial_set_input(&gba->serial, SERIAL_SC, SERIAL_SC);
    cupid_io_write(gba, REG_SIOMLT_SEND, 0xA5);
    CHECK(!gba->serial.uart.transmitting && (control(gba) & FULL));
    cupid_gba_stop(gba);
    uint64_t frozen = cupid_timing_global_time(&gba->timing);
    cupid_serial_set_input(&gba->serial, SERIAL_SC, 0);
    for (unsigned i = 0; i < 10; ++i) cupid_gba_step(gba);
    CHECK(gba->stopped && !gba->serial.uart.transmitting);
    CHECK(cupid_timing_global_time(&gba->timing) == frozen);
    cupid_gba_wake_stop(gba, IRQ_KEYPAD);
    CHECK(!gba->stopped && gba->serial.uart.transmitting);
    tick(gba, 100);
    cupid_gba_stop(gba);
    frozen = cupid_timing_global_time(&gba->timing);
    for (unsigned i = 0; i < 10; ++i) cupid_gba_step(gba);
    CHECK(cupid_timing_global_time(&gba->timing) == frozen && gba->serial.uart.transmitting);
    cupid_gba_wake_stop(gba, IRQ_KEYPAD);
    tick(gba, 1457 - 100);
    CHECK(!gba->serial.uart.transmitting && !(control(gba) & FULL));
    destroy(gba);
    for (unsigned reset = 0; reset < 2; ++reset) {
        gba = machine(0);
        configure(gba, 3 | FIFO | EIGHT | SEND | RECEIVE);
        cupid_serial_set_output_callback(&gba->serial, loopback, gba);
        cupid_io_write(gba, REG_SIOMLT_SEND, 0xA5);
        tick(gba, 300);
        CHECK(gba->serial.uart.transmitting && gba->serial.uart.receiving);
        cupid_serial_set_output_callback(&gba->serial, NULL, NULL);
        if (reset) cupid_serial_reset(&gba->serial);
        else cupid_io_write(gba, REG_RCNT, 0x8000);
        tick(gba, 10000);
        CHECK(!gba->serial.uart.transmitting && !gba->serial.uart.receiving);
        CHECK(!(gba->irq.pendingIF & IRQ_SIO));
        configure(gba, 3 | FIFO | EIGHT | RECEIVE);
        CHECK(control(gba) & EMPTY);
        inject(gba, 3, 8, 0x53, 0, true, true);
        CHECK(!(control(gba) & EMPTY));
        cupid_io_write(gba, REG_SIOCNT, 0x3883);
        cupid_io_write(gba, REG_SIOCNT, 0x3983);
        CHECK(control(gba) & EMPTY); /* FIFO sequencers were reset while disabled. */
        destroy(gba);
    }
}

void test_uart_guest_driver(bool thumb, const char* bios, bool negative);

int main(int argc, char** argv) {
    CHECK(argc == 2 || (argc == 4 && !strcmp(argv[2], "--external-bios")));
    const char* bios = argc == 4 ? argv[3] : NULL;
    if (!strcmp(argv[1], "formats")) complete_format_matrix();
    else if (!strcmp(argv[1], "fifo")) RUN(fifo_capacity_and_flow_control);
    else if (!strcmp(argv[1], "errors")) RUN(errors_reject_data_and_acknowledge_on_status_read);
    else if (!strcmp(argv[1], "interrupts")) RUN(fifo_and_register_irq_conditions);
    else if (!strcmp(argv[1], "peers")) RUN(bidirectional_peers_exchange_complete_bytes);
    else if (!strcmp(argv[1], "chronology")) RUN(timer_dma_and_late_dispatch_retain_waveforms);
    else if (!strcmp(argv[1], "lifecycle")) RUN(halt_stop_cancellation_and_access_widths);
    else if (!strcmp(argv[1], "guest-arm")) test_uart_guest_driver(false, bios, false);
    else if (!strcmp(argv[1], "guest-thumb")) test_uart_guest_driver(true, bios, false);
    else if (!strcmp(argv[1], "guest-controls")) test_uart_guest_driver(false, bios, true);
    else CHECK(false);
    return 0;
}
