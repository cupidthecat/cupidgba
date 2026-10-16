/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/cable.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"
#include "../../bios/caller.h"
#include "uart-driver.inc"
#include "cable-driver.inc"
#include <string.h>

struct Fixture {
    struct CupidGBA* cores[4];
    struct CupidCable* cable;
    unsigned count;
};
static const char* externalBios;
static bool guestDma;
static bool guestFault;

struct Terminal { uint32_t address; bool issued; };
static void observe_terminal(const struct CupidARM* cpu, void* context) {
    struct Terminal* terminal = context;
    if (cupid_arm_instruction_address(cpu) != terminal->address) return;
    CHECK(cpu->gprs[12] == 0);
    terminal->issued = true;
}

static void create(struct Fixture* fixture, unsigned count, uint64_t epoch) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->count = count;
    for (unsigned i = 0; i < count; ++i) {
        struct CupidGBA* gba = calloc(1, sizeof(*gba));
        CHECK(gba);
        fixture->cores[i] = gba;
        cupid_gba_create(gba);
        if (externalBios) CHECK(cupid_load_bios(gba, externalBios));
        cupid_gba_reset(gba);
        cupid_timing_clear(&gba->timing);
        gba->timing.globalCycles = epoch;
        gba->timing.masterCycles = (uint32_t)epoch;
        gba->cpu.halted = true;
    }
    fixture->cable = cupid_cable_create(fixture->cores, count);
    CHECK(fixture->cable && cupid_cable_connected(fixture->cable));
}

static void destroy(struct Fixture* fixture) {
    cupid_cable_destroy(fixture->cable);
    for (unsigned i = 0; i < fixture->count; ++i) {
        CHECK(!fixture->cores[i]->timing.synchronize);
        CHECK(!fixture->cores[i]->serial.outputChanged);
        cupid_gba_destroy(fixture->cores[i]);
        free(fixture->cores[i]);
    }
}

static void normal(void) {
    unsigned observations = 0;
    for (unsigned wrap = 0; wrap < 2; ++wrap)
    for (unsigned owner = 0; owner < 2; ++owner)
    for (unsigned width = 0; width < 2; ++width)
    for (unsigned fast = 0; fast < 2; ++fast) {
        struct Fixture f;
        const uint64_t epoch = wrap ? UINT64_C(0x1FFFFFFE0) : 0;
        create(&f, 2, epoch);
        const uint32_t words[] = {0xA5DE9C3B, 0x7C3A5816};
        for (unsigned i = 0; i < 2; ++i) {
            struct CupidSerial* serial = &f.cores[i]->serial;
            cupid_serial_write(serial, REG_RCNT, 0);
            cupid_serial_write(serial, REG_SIOCNT, width ? 0x1000 : 0);
            cupid_serial_write(serial, REG_SIODATA32_LO, (uint16_t)words[i]);
            cupid_serial_write(serial, REG_SIODATA32_HI, (uint16_t)(words[i] >> 16));
            cupid_serial_write(serial, REG_SIOMLT_SEND, (uint16_t)words[i]);
        }
        cupid_serial_write(&f.cores[owner ^ 1]->serial, REG_SIOCNT, (width ? 0x1000 : 0) | 0x4080);
        cupid_serial_write(&f.cores[owner]->serial, REG_SIOCNT, (width ? 0x1000 : 0) | 0x4081 | (fast ? 2 : 0));
        const unsigned length = (width ? 32 : 8) * (fast ? 8 : 64) + 6;
        CHECK(cupid_cable_run(f.cable, length - 1));
        for (unsigned i = 0; i < 2; ++i) CHECK(f.cores[i]->serial.active);
        CHECK(cupid_cable_run(f.cable, 1));
        for (unsigned i = 0; i < 2; ++i) {
            struct CupidGBA* gba = f.cores[i];
            CHECK(!gba->serial.active);
            CHECK(gba->irq.pendingIF & IRQ_SIO);
            uint32_t result = width ? gba->memory.io[REG_SIODATA32_LO >> 1] |
                ((uint32_t)gba->memory.io[REG_SIODATA32_HI >> 1] << 16) : gba->memory.io[REG_SIOMLT_SEND >> 1] & 0xFF;
            CHECK(result == (words[i ^ 1] & (width ? UINT32_MAX : 0xFF)));
            CHECK(cupid_timing_global_time(&gba->timing) == epoch + length);
            ++observations;
        }
        destroy(&f);
    }
    printf("normal: %u complete words, both owners, widths, clocks and epochs\n", observations);
}

static void uart(void) {
    unsigned observations = 0;
    for (unsigned wrap = 0; wrap < 2; ++wrap)
    for (unsigned baud = 0; baud < 4; ++baud)
    for (unsigned fifo = 0; fifo < 2; ++fifo) {
        struct Fixture f;
        create(&f, 2, wrap ? UINT64_C(0x2FFFFFFC0) : 0);
        for (unsigned i = 0; i < 2; ++i) {
            cupid_serial_write(&f.cores[i]->serial, REG_RCNT, 0);
            cupid_serial_write(&f.cores[i]->serial, REG_SIOCNT, (uint16_t)(0x7C84 | baud | (fifo ? 0x100 : 0)));
        }
        unsigned rounds = fifo ? 4 : 1;
        for (unsigned round = 0; round < rounds; ++round)
        for (unsigned i = 0; i < 2; ++i)
            cupid_serial_write(&f.cores[i]->serial, REG_SIOMLT_SEND, (uint16_t)(0x51 ^ (i * 0xA6) ^ (round * 0x38)));
        const unsigned rates[] = {9600, 38400, 57600, 115200};
        const unsigned clocks = (unsigned)((UINT64_C(16777216) * 10 + rates[baud] - 1) / rates[baud]);
        CHECK(cupid_cable_run(f.cable, clocks * rounds + 1));
        for (unsigned round = 0; round < rounds; ++round)
        for (unsigned i = 0; i < 2; ++i) {
            struct CupidSerial* serial = &f.cores[i]->serial;
            CHECK(!(cupid_serial_read(serial, REG_SIOCNT) & 0x60));
            CHECK(cupid_serial_read(serial, REG_SIOMLT_SEND) == (uint16_t)(0x51 ^ ((i ^ 1) * 0xA6) ^ (round * 0x38)));
            ++observations;
        }
        destroy(&f);
    }
    printf("uart: %u complete duplex bytes, all bauds, FIFO and clock wrap\n", observations);
}

static void multiplayer(void) {
    unsigned observations = 0;
    for (unsigned count = 2; count <= 4; ++count)
    for (unsigned baud = 0; baud < 4; ++baud) {
        struct Fixture f;
        create(&f, count, UINT64_C(0x3FFFFFFE0));
        for (unsigned i = 0; i < count; ++i) {
            cupid_serial_write(&f.cores[i]->serial, REG_RCNT, 0);
            cupid_serial_write(&f.cores[i]->serial, REG_SIOCNT, (uint16_t)(0x6000 | baud));
            cupid_serial_write(&f.cores[i]->serial, REG_SIOMLT_SEND, (uint16_t)(0x714B ^ (i * 0xC57)));
        }
        cupid_serial_write(&f.cores[0]->serial, REG_SIOCNT, (uint16_t)(0x6080 | baud));
        CHECK(cupid_cable_run(f.cable, 130000));
        for (unsigned i = 0; i < count; ++i) {
            struct CupidSerial* serial = &f.cores[i]->serial;
            CHECK(!serial->active && !serial->multiplayerError);
            CHECK(f.cores[i]->irq.pendingIF & IRQ_SIO);
            for (unsigned j = 0; j < 4; ++j) {
                CHECK(cupid_serial_read(serial, REG_SIODATA32_LO + j * 2) ==
                      (j < count ? (uint16_t)(0x714B ^ (j * 0xC57)) : 0xFFFF));
                ++observations;
            }
        }
        destroy(&f);
    }
    printf("multiplayer: %u complete receive slots, two through four players\n", observations);
}

static void dma_start(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing; CHECK(late == 0);
    struct CupidGBA* gba = context;
    cupid_io_write(gba, REG_DMA3SAD_LO, 0x8000);
    cupid_io_write(gba, REG_DMA3SAD_HI, 0x0200);
    cupid_io_write(gba, REG_DMA3DAD_LO, 0x4000);
    cupid_io_write(gba, REG_DMA3DAD_HI, 0x0300);
    cupid_io_write(gba, REG_DMA3CNT_LO, 1024);
    cupid_io_write(gba, REG_DMA3CNT_HI, 0xC400);
}

static void guest(unsigned mode, unsigned fifo, unsigned baud) {
    unsigned bytes = 0, halts = 0;
    CHECK(mode < 4 && fifo < 2 && baud < 4);
    {
        struct Fixture f;
        create(&f, 2, UINT64_C(0x1FFFFFF80));
        /* Configure the callers before starting their cooperative stacks. */
        uint8_t expected[2][128];
        struct Terminal terminals[2] = {0};
        for (unsigned i = 0; i < 2; ++i) {
            struct CupidGBA* gba = f.cores[i];
            const bool thumb = mode == 1 || (mode == 2 && i == 1) || (mode == 3 && i == 0);
            cupid_test_bios_caller(gba);
            memcpy(gba->memory.iwram + 0x1000, uartDriver, sizeof(uartDriver));
            memset(gba->memory.ewram, 0xA5, GBA_SIZE_EWRAM);
            for (unsigned j = 0; j < 128; ++j) expected[i][j] = (uint8_t)((j * 73) ^ (i ? 0x36 : 0xA5));
            memcpy(gba->memory.iwram + 0x2000, expected[i], 128);
            if (guestFault && i == 0) gba->memory.iwram[0x2000] ^= 1;
            gba->cpu.gprs[0] = GBA_BASE_IO + REG_SIOCNT;
            gba->cpu.gprs[1] = GBA_BASE_IO + REG_SIOMLT_SEND;
            gba->cpu.gprs[2] = GBA_BASE_IWRAM + 0x2000;
            gba->cpu.gprs[3] = GBA_BASE_EWRAM + 0x1000;
            gba->cpu.gprs[4] = gba->cpu.gprs[5] = 128;
            gba->cpu.gprs[6] = (int32_t)(0x7C84 | baud | (fifo ? 0x100 : 0));
            gba->cpu.gprs[12] = (int32_t)0xDEADBEEF;
            gba->cpu.cpsr.i = 1;
            gba->cpu.irqDisableLatch = true;
            gba->cpu.halted = false;
            _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
            gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000 + (thumb ? 0x100 : 0);
            /* Pipeline setup is untimed; the session owns execution afterwards. */
            if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
            gba->cpu.cycles = 0;
            terminals[i].address = GBA_BASE_IWRAM + 0x1000 + (thumb ? 0x1FC : 0xFC);
            gba->cpu.instructionObserver = observe_terminal;
            gba->cpu.instructionObserverContext = &terminals[i];
        }
        struct CupidTimingEvent dma = {.context = f.cores[mode & 1], .callback = dma_start,
            .name = "Owned guest cable DMA", .priority = 0x30};
        if (guestDma) {
            struct CupidGBA* gba = f.cores[mode & 1];
            for (unsigned i = 0; i < 4096; ++i) gba->memory.ewram[0x8000 + i] = (uint8_t)(i ^ (i >> 8) ^ 0xB6);
            cupid_timing_schedule(&gba->timing, &dma, 5000);
        }
        bool terminal[2] = {false, false};
        for (unsigned n = 0; n < 2400 && (!terminal[0] || !terminal[1]); ++n) {
            CHECK(cupid_cable_run(f.cable, 1000));
            for (unsigned i = 0; i < 2; ++i) {
                terminal[i] = terminals[i].issued;
                if (f.cores[i]->cpu.halted) ++halts;
            }
        }
        for (unsigned i = 0; i < 2; ++i) {
            if (!terminal[i]) fprintf(stderr, "guest mode=%u fifo=%u baud=%u player=%u pc=%08X tx=%d rx=%d halt=%d time=%" PRIu64 "\n",
                mode, fifo, baud, i, (unsigned)f.cores[i]->cpu.gprs[ARM_PC], f.cores[i]->cpu.gprs[4],
                f.cores[i]->cpu.gprs[5], f.cores[i]->cpu.halted, cupid_cable_time(f.cable));
            CHECK(terminal[i]);
            CHECK(f.cores[i]->cpu.gprs[4] == 0 && f.cores[i]->cpu.gprs[5] == 0);
            unsigned mismatches = 0;
            for (unsigned j = 0; j < 128; ++j) {
                uint8_t received = f.cores[i]->memory.ewram[0x1000 + j];
                mismatches += received != expected[i ^ 1][j];
                CHECK(received == (expected[i ^ 1][j] ^ (guestFault && i == 1 && j == 0 ? 1 : 0)));
            }
            CHECK(mismatches == (guestFault && i == 1 ? 1u : 0u));
            CHECK(f.cores[i]->memory.ewram[0xFFF] == 0xA5 && f.cores[i]->memory.ewram[0x1080] == 0xA5);
            bytes += 128;
        }
        if (guestDma) {
            struct CupidGBA* gba = f.cores[mode & 1];
            CHECK(memcmp(gba->memory.iwram + 0x4000, gba->memory.ewram + 0x8000, 4096) == 0);
            CHECK(gba->irq.pendingIF & IRQ_DMA3);
        }
        destroy(&f);
    }
    CHECK(halts);
    printf("guest: %u complete opposite-console bytes; %u HALT samples; mode=%u FIFO=%u baud=%u\n", bytes, halts, mode, fifo, baud);
}

static void word_guest(unsigned multi, unsigned selection) {
    unsigned words = 0;
    CHECK(multi < 2 && selection < (multi ? 12u : 4u));
    {
        const unsigned count = multi ? 2 + selection / 4 : 2;
        const unsigned baud = selection & 3;
        struct Fixture f; create(&f, count, UINT64_C(0x2FFFFFFE0));
        struct Terminal terminals[4] = {0};
        for (unsigned i = 0; i < count; ++i) {
            struct CupidGBA* gba = f.cores[i];
            cupid_test_bios_caller(gba);
            memcpy(gba->memory.iwram + 0x1000, cableDriver, sizeof(cableDriver));
            memset(gba->memory.ewram, 0xA5, GBA_SIZE_EWRAM);
            gba->cpu.gprs[0] = GBA_BASE_IO + REG_SIOCNT;
            gba->cpu.gprs[1] = GBA_BASE_IO + ((selection & 1) ? REG_SIODATA32_LO : REG_SIOMLT_SEND);
            gba->cpu.gprs[2] = GBA_BASE_EWRAM + 0x1000;
            gba->cpu.gprs[3] = (int32_t)(0xA19F56C3 ^ (i * 0x390B6721u));
            gba->cpu.gprs[4] = (int32_t)(multi ? 0x6000 | baud :
                0x4000 | ((selection & 1) ? 0x1000 : 0) | ((selection & 2) ? 2 : 0) | (i ? 0 : 1));
            gba->cpu.gprs[5] = (int32_t)i;
            gba->cpu.gprs[12] = (int32_t)0xDEADBEEF;
            gba->cpu.cpsr.i = 1; gba->cpu.irqDisableLatch = true; gba->cpu.halted = false;
            _CupidSetMode(&gba->cpu, MODE_ARM);
            gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + 0x1000 + (multi ? 0x100 : 0);
            CupidARMWritePC(&gba->cpu); gba->cpu.cycles = 0;
            terminals[i].address = GBA_BASE_IWRAM + 0x10FC + (multi ? 0x100 : 0);
            gba->cpu.instructionObserver = observe_terminal;
            gba->cpu.instructionObserverContext = &terminals[i];
        }
        bool complete = false;
        for (unsigned step = 0; step < 200 && !complete; ++step) {
            CHECK(cupid_cable_run(f.cable, 1000));
            complete = true;
            for (unsigned i = 0; i < count; ++i)
                complete &= terminals[i].issued;
        }
        CHECK(complete);
        for (unsigned i = 0; i < count; ++i) {
            struct CupidGBA* gba = f.cores[i];
            CHECK(gba->irq.pendingIF & IRQ_SIO);
            CHECK(gba->memory.ewram[0xFFF] == 0xA5);
            if (multi) {
                for (unsigned j = 0; j < 4; ++j) {
                    uint16_t received; memcpy(&received, gba->memory.ewram + 0x1000 + j * 2, 2);
                    CHECK(received == (j < count ? (uint16_t)(0xA19F56C3 ^ (j * 0x390B6721u)) : 0xFFFF));
                    ++words;
                }
                CHECK(gba->memory.ewram[0x1008] == 0xA5);
            } else {
                uint32_t received; memcpy(&received, gba->memory.ewram + 0x1000, 4);
                CHECK(received == ((0xA19F56C3 ^ ((i ^ 1) * 0x390B6721u)) & ((selection & 1) ? UINT32_MAX : 0xFF)));
                CHECK(gba->memory.ewram[0x1004] == 0xA5);
                ++words;
            }
        }
        destroy(&f);
    }
    printf("word guest: %u exact receive words, multiplayer=%u selection=%u\n", words, multi, selection);
}

struct ClockProbe {
    struct Fixture* fixture;
    struct CupidTimingEvent event;
    uint64_t epoch;
    unsigned count;
};

static void clock_probe(struct CupidTiming* timing, void* context, uint32_t late) {
    struct ClockProbe* probe = context;
    CHECK(late == 0);
    CHECK(cupid_timing_global_time(timing) == probe->epoch + 7 * (probe->count + 1));
    for (unsigned i = 0; i < probe->fixture->count; ++i)
        CHECK(cupid_timing_global_time(&probe->fixture->cores[i]->timing) == cupid_timing_global_time(timing));
    if (++probe->count < 128) cupid_timing_schedule(timing, &probe->event, 7);
}

static void chronology(void) {
    struct Fixture f; const uint64_t epoch = UINT64_C(0x1FFFFFFD0); create(&f, 4, epoch);
    struct ClockProbe probes[4] = {0};
    for (unsigned i = 0; i < 4; ++i) {
        probes[i].fixture = &f; probes[i].epoch = epoch;
        probes[i].event = (struct CupidTimingEvent){.context=&probes[i], .callback=clock_probe,
            .name="Owned cross-console clock", .priority=0x80 - i};
        cupid_timing_schedule(&f.cores[i]->timing, &probes[i].event, 7);
    }
    for (unsigned i = 0; i < 1000; ++i) CHECK(cupid_cable_run(f.cable, 1));
    for (unsigned i = 0; i < 4; ++i) CHECK(probes[i].count == 128);
    CHECK(cupid_cable_time(f.cable) == 1000);
    CHECK(!cupid_cable_run(f.cable, UINT32_MAX));
    destroy(&f);
}

static void lifecycle(void) {
    struct Fixture f; create(&f, 2, UINT64_C(0x2FFFFFFE0));
    for (unsigned i = 0; i < 2; ++i) {
        cupid_serial_write(&f.cores[i]->serial, REG_RCNT, 0);
        cupid_serial_write(&f.cores[i]->serial, REG_SIOCNT, 0x6003);
        cupid_serial_write(&f.cores[i]->serial, REG_SIOMLT_SEND, (uint16_t)(0xC135 + i));
    }
    CHECK(cupid_serial_read(&f.cores[0]->serial, REG_SIOCNT) & 8);
    for (unsigned cancellation = 0; cancellation < 3; ++cancellation) {
        cupid_serial_write(&f.cores[0]->serial, REG_SIOCNT, 0x6083);
        CHECK(cupid_cable_run(f.cable, 100));
        CHECK(f.cores[0]->serial.active && f.cores[1]->serial.active);
        if (cancellation == 0) cupid_cable_connect(f.cable, false);
        else if (cancellation == 1) cupid_serial_write(&f.cores[0]->serial, REG_SIOCNT, 0x6003);
        else cupid_serial_write(&f.cores[1]->serial, REG_RCNT, 0x8000);
        CHECK(!f.cores[0]->serial.active && !f.cores[1]->serial.active);
        CHECK(cupid_cable_run(f.cable, 10000));
        for (unsigned i = 0; i < 2; ++i) CHECK(!(f.cores[i]->irq.pendingIF & IRQ_SIO));
        cupid_cable_connect(f.cable, true);
        cupid_serial_write(&f.cores[1]->serial, REG_RCNT, 0);
        cupid_serial_write(&f.cores[1]->serial, REG_SIOCNT, 0x6003);
    }
    cupid_serial_write(&f.cores[1]->serial, REG_RCNT, 0x8100);
    CHECK(!(cupid_serial_read(&f.cores[0]->serial, REG_SIOCNT) & 8));
    cupid_serial_write(&f.cores[0]->serial, REG_RCNT, 0x8088);
    cupid_irq_write(f.cores[1], REG_IE, IRQ_SIO);
    CHECK(cupid_cable_run(f.cable, 2));
    f.cores[1]->stopped = true;
    uint64_t stopped = cupid_timing_global_time(&f.cores[1]->timing);
    CHECK(cupid_cable_run(f.cable, 1000));
    CHECK(cupid_timing_global_time(&f.cores[1]->timing) == stopped);
    cupid_serial_write(&f.cores[0]->serial, REG_RCNT, 0x8080);
    CHECK(!f.cores[1]->stopped);
    CHECK(cupid_cable_run(f.cable, 5));
    CHECK(cupid_timing_global_time(&f.cores[1]->timing) == stopped + 5);
    CHECK(f.cores[1]->irq.pendingIF & IRQ_SIO);
    destroy(&f);
}

struct NaiveWire { struct CupidGBA* peer; unsigned stale; };
static void naive_output(void* context, uint8_t mask, uint8_t levels, uint64_t timestamp) {
    struct NaiveWire* wire = context;
    if (cupid_timing_global_time(&wire->peer->timing) != timestamp) ++wire->stale;
    uint8_t inputs = ((mask & SERIAL_SO) >> 1) | (mask & (SERIAL_SC | SERIAL_SD));
    uint8_t values = ((levels & SERIAL_SO) >> 1) | (levels & (SERIAL_SC | SERIAL_SD));
    cupid_serial_set_input(&wire->peer->serial, inputs, values);
}

static void controls(void) {
    struct CupidGBA* cores[2]; struct NaiveWire wires[2];
    for (unsigned i = 0; i < 2; ++i) {
        cores[i] = calloc(1, sizeof(*cores[i])); CHECK(cores[i]);
        cupid_gba_create(cores[i]); cupid_gba_reset(cores[i]); cupid_timing_clear(&cores[i]->timing);
        cores[i]->cpu.halted = true;
    }
    for (unsigned i = 0; i < 2; ++i) {
        wires[i] = (struct NaiveWire){.peer=cores[i ^ 1]};
        cupid_serial_set_output_callback(&cores[i]->serial, naive_output, &wires[i]);
        cupid_serial_write(&cores[i]->serial, REG_RCNT, 0);
        cupid_serial_write(&cores[i]->serial, REG_SIOMLT_SEND, (uint16_t)(0xA5 ^ (i * 0x99)));
    }
    cupid_serial_write(&cores[1]->serial, REG_SIOCNT, 0x4080);
    cupid_serial_write(&cores[0]->serial, REG_SIOCNT, 0x4083);
    /* Whole-step alternation can leave a peer past incoming clock edges even
     * when all final data bits happen to match. The chronology control rejects it. */
    cupid_gba_step(cores[1]);
    for (unsigned i = 0; i < 17; ++i) cupid_gba_step(cores[0]);
    CHECK(wires[0].stale > 0);
    CHECK((cores[1]->memory.io[REG_SIOMLT_SEND >> 1] & 0xFF) == 0x3C); /* Not yet published. */
    for (unsigned i = 0; i < 2; ++i) { cupid_gba_destroy(cores[i]); free(cores[i]); }
}

int main(int argc, char** argv) {
    if (argc >= 4 && !strcmp(argv[argc - 2], "--external-bios")) { externalBios = argv[argc - 1]; argc -= 2; }
    CHECK(argc == 2 || argc == 4 || argc == 5);
    if (!strcmp(argv[1], "normal")) RUN(normal);
    else if (!strcmp(argv[1], "uart")) RUN(uart);
    else if (!strcmp(argv[1], "multiplayer")) RUN(multiplayer);
    else if (!strcmp(argv[1], "word-guest")) {
        CHECK(argc == 4);
        word_guest((unsigned)strtoul(argv[2], NULL, 10), (unsigned)strtoul(argv[3], NULL, 10));
    }
    else if (!strcmp(argv[1], "chronology")) RUN(chronology);
    else if (!strcmp(argv[1], "lifecycle")) RUN(lifecycle);
    else if (!strcmp(argv[1], "controls")) RUN(controls);
    else if (!strcmp(argv[1], "guest-dma")) { guestDma = true; guest(2, 1, 3); guest(3, 1, 3); }
    else if (!strcmp(argv[1], "guest-controls")) { guestFault = true; guest(2, 0, 3); guest(3, 1, 3); }
    else if (!strcmp(argv[1], "guest")) {
        CHECK(argc == 5);
        guest((unsigned)strtoul(argv[2], NULL, 10), (unsigned)strtoul(argv[3], NULL, 10),
              (unsigned)strtoul(argv[4], NULL, 10));
    }
    else CHECK(false);
    return 0;
}
