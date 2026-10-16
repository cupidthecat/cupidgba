/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"
#include "../../bios/caller.h"
#include "uart-driver.inc"

enum { SOURCE = 0x2000, OUTPUT = 0x1000, BYTES = 128, CODE = 0x1000 };

struct Wire {
    struct CupidGBA* gba;
    struct CupidTimingEvent corrupt, restore;
    bool fault, arranged;
};

static void drive(struct Wire* wire, uint8_t levels) {
    cupid_serial_set_input(&wire->gba->serial, SERIAL_SI | SERIAL_SC,
                          ((levels & SERIAL_SO) >> 1) | ((levels & SERIAL_SD) >> 1));
}

static void corrupt_sample(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing; (void)late;
    struct Wire* wire = context;
    struct CupidSerial* serial = &wire->gba->serial;
    cupid_serial_set_input(serial, serial->inputMask, serial->inputLevels ^ SERIAL_SI);
}

static void restore_sample(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)timing; (void)late;
    struct Wire* wire = context;
    drive(wire, wire->gba->serial.outputLevels);
}

static void wire_output(void* context, uint8_t mask, uint8_t levels, uint64_t timestamp) {
    struct Wire* wire = context;
    struct CupidSerial* serial = &wire->gba->serial;
    if (cupid_serial_mode(serial) != SERIAL_UART) return;
    CHECK(mask == (SERIAL_SO | SERIAL_SD));
    CHECK(timestamp == cupid_timing_global_time(&wire->gba->timing));
    drive(wire, levels);
    if (wire->fault && !wire->arranged && serial->uart.transmitting && serial->uart.transmitStep == 1) {
        wire->arranged = true;
        /* Corrupt only the first data-bit sample of one otherwise valid 8N1
         * frame. The receiver must still finish every byte and reach terminal. */
        cupid_timing_schedule(&wire->gba->timing, &wire->corrupt, 218);
        cupid_timing_schedule(&wire->gba->timing, &wire->restore, 292);
    }
}

struct GuestResult { bool terminal, complete, exact; };

static struct GuestResult run_guest(bool thumb, unsigned baud, bool fifo, const char* bios, bool fault) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    if (bios) CHECK(cupid_load_bios(gba, bios));
    cupid_gba_reset(gba);
    cupid_test_bios_caller(gba);
    cupid_timing_clear(&gba->timing);
    memcpy(gba->memory.iwram + CODE, uartDriver, sizeof(uartDriver));
    memset(gba->memory.ewram, 0xA5, GBA_SIZE_EWRAM);
    uint8_t expected[BYTES];
    for (unsigned i = 0; i < BYTES; ++i) expected[i] = (uint8_t)((i * 73) ^ 0xA5);
    memcpy(gba->memory.iwram + SOURCE, expected, BYTES);
    gba->cpu.gprs[0] = GBA_BASE_IO + REG_SIOCNT;
    gba->cpu.gprs[1] = GBA_BASE_IO + REG_SIOMLT_SEND;
    gba->cpu.gprs[2] = GBA_BASE_IWRAM + SOURCE;
    gba->cpu.gprs[3] = GBA_BASE_EWRAM + OUTPUT;
    gba->cpu.gprs[4] = gba->cpu.gprs[5] = BYTES;
    gba->cpu.gprs[6] = (int32_t)(0x7C84 | baud | (fifo ? 0x100 : 0));
    gba->cpu.gprs[12] = (int32_t)0xDEADBEEF;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + CODE + (thumb ? 0x100 : 0);
    if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
    struct Wire wire = {.gba = gba, .fault = fault,
        .corrupt = {.callback=corrupt_sample, .name="Owned UART sample fault", .priority=0x90},
        .restore = {.callback=restore_sample, .name="Owned UART sample restore", .priority=0x90}};
    wire.corrupt.context = wire.restore.context = &wire;
    cupid_serial_set_output_callback(&gba->serial, wire_output, &wire);
    const uint32_t terminalPC = GBA_BASE_IWRAM + CODE + (thumb ? 0x1FEu : 0x100u);
    struct GuestResult result = {0};
    bool sawInterrupt = false, sawHalt = false, resumed = false;
    for (unsigned steps = 0; steps < 1000000 && cupid_timing_global_time(&gba->timing) < 3000000; ++steps) {
        bool halted = gba->cpu.halted;
        cupid_gba_step(gba);
        sawInterrupt |= (gba->irq.pendingIF & IRQ_SIO) != 0;
        sawHalt |= gba->cpu.halted;
        resumed |= halted && !gba->cpu.halted;
        if ((uint32_t)gba->cpu.gprs[ARM_PC] == terminalPC && !gba->serial.uart.transmitting && !gba->serial.uart.receiving) {
            result.terminal = true;
            break;
        }
    }
    result.complete = result.terminal && gba->cpu.gprs[12] == 0 && gba->cpu.gprs[4] == 0 && gba->cpu.gprs[5] == 0
        && (uint32_t)gba->cpu.gprs[2] == GBA_BASE_IWRAM + SOURCE + BYTES
        && (uint32_t)gba->cpu.gprs[3] == GBA_BASE_EWRAM + OUTPUT + BYTES;
    result.exact = !memcmp(gba->memory.ewram + OUTPUT, expected, BYTES);
    if (!result.terminal)
        fprintf(stderr, "UART guest stopped at clock=%" PRIu64 " pc=%08" PRIx32 " tx-left=%d rx-left=%d\n",
            cupid_timing_global_time(&gba->timing), (uint32_t)gba->cpu.gprs[ARM_PC],
            gba->cpu.gprs[4], gba->cpu.gprs[5]);
    CHECK(!memcmp(gba->memory.iwram + SOURCE, expected, BYTES));
    for (unsigned i = 0; i < OUTPUT; ++i) CHECK(gba->memory.ewram[i] == 0xA5);
    for (unsigned i = OUTPUT + BYTES; i < GBA_SIZE_EWRAM; ++i) CHECK(gba->memory.ewram[i] == 0xA5);
    if (result.complete) CHECK(sawInterrupt && sawHalt && resumed);
    if (fault) CHECK(wire.arranged);
    cupid_gba_destroy(gba);
    free(gba);
    return result;
}

void test_uart_guest_driver(bool thumb, const char* bios, bool negative) {
    if (negative) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            struct GuestResult result = run_guest(mode != 0, 3, false, bios, true);
            CHECK(result.terminal && result.complete && !result.exact);
        }
        puts("PASS UART guest controls: both modes complete all 128 bytes and zero-result terminals; full output rejects corruption");
        return;
    }
    for (unsigned baud = 0; baud < 4; ++baud) for (unsigned fifo = 0; fifo < 2; ++fifo) {
        struct GuestResult result = run_guest(thumb, baud, fifo != 0, bios, false);
        if (!result.terminal || !result.complete || !result.exact)
            fprintf(stderr, "UART guest mode=%u baud=%u FIFO=%u terminal=%u complete=%u exact=%u\n",
                thumb, baud, fifo, result.terminal, result.complete, result.exact);
        CHECK(result.terminal && result.complete && result.exact);
    }
    printf("PASS UART %s guest: 1024 complete bytes, all baud rates and FIFO settings, full EWRAM guards and terminal\n",
        thumb ? "Thumb" : "ARM");
}
