#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../../test.h"
#include "../../bios/caller.h"
#include "joybus-driver.inc"

static const char* externalBios;
static const uint64_t origins[] = {0, INT32_MAX - UINT64_C(9),
    UINT32_MAX - UINT64_C(9), UINT64_C(0x300000000)};

static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba);
    if (externalBios) CHECK(cupid_load_bios(gba, externalBios));
    cupid_gba_reset(gba);
    cupid_test_bios_caller(gba);
    cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock;
    gba->timing.masterCycles = (uint32_t)clock;
    gba->cpu.cpsr.i = 1;
    gba->cpu.irqDisableLatch = true;
    cupid_io_write(gba, REG_RCNT, 0xC000);
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }
static void tick(struct CupidGBA* gba, unsigned clocks) {
    CHECK(!gba->cpu.cycles);
    cupid_timing_tick(&gba->timing, (int32_t)clocks);
}
static uint16_t reg(struct CupidGBA* gba, unsigned address) {
    return cupid_io_read(gba, address);
}
static uint32_t word(struct CupidGBA* gba, unsigned address) {
    return reg(gba, address) | (uint32_t)reg(gba, address + 2) << 16;
}
static size_t command(struct CupidGBA* gba, uint8_t code, uint8_t reply[5]) {
    return cupid_serial_joybus_command(&gba->serial, &code, 1, reply, 5);
}
static void receive(struct CupidGBA* gba, uint32_t value, uint8_t reply[5]) {
    const uint8_t request[] = {SERIAL_JOY_WRITE, (uint8_t)value, (uint8_t)(value >> 8),
        (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    CHECK(cupid_serial_joybus_command(&gba->serial, request, sizeof(request), reply, 5) == 1);
}
static uint32_t unpack(const uint8_t* bytes) {
    return bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}
static void transmit(struct CupidGBA* gba, uint32_t value) {
    cupid_io_write(gba, REG_JOY_TRANS_LO, (uint16_t)value);
    cupid_io_write(gba, REG_JOY_TRANS_HI, (uint16_t)(value >> 16));
}

static void commands_preserve_independent_flags(void) {
    for (unsigned flags = 0; flags < 4; flags++) for (unsigned irq = 0; irq < 2; irq++) {
        struct CupidGBA* gba = machine(0);
        uint8_t reply[5] = {0xA5, 0xA5, 0xA5, 0xA5, 0xA5};
        cupid_io_write(gba, REG_JOYSTAT, (uint16_t)(flags << 4));
        cupid_io_write(gba, REG_JOYCNT, irq ? 0x40 : 0);
        CHECK(command(gba, SERIAL_JOY_STATUS, reply) == 3);
        CHECK(reply[0] == 0 && reply[1] == 4 && reply[2] == flags << 4 && reply[3] == 0xA5);
        CHECK(!gba->irq.pendingIF);
        /* STATUS preserves IRQ enable as well as every event flag. */
        CHECK(reg(gba, REG_JOYCNT) == (irq ? 0x40 : 0));
        CHECK(command(gba, SERIAL_JOY_RESET, reply) == 3);
        CHECK(reg(gba, REG_JOYCNT) == (1 | (irq ? 0x40 : 0)));
        receive(gba, 0x78563412, reply);
        CHECK(reply[0] == (flags << 4 | 2));
        transmit(gba, 0x89ABCDEF);
        CHECK(command(gba, SERIAL_JOY_READ, reply) == 5);
        CHECK(unpack(reply) == 0x89ABCDEF && reply[4] == (flags << 4 | 10));
        CHECK(reg(gba, REG_JOYSTAT) == (flags << 4 | 2));
        CHECK(reg(gba, REG_JOYCNT) == (7 | (irq ? 0x40 : 0)));
        CHECK(command(gba, SERIAL_JOY_STATUS, reply) == 3 && reply[2] == (flags << 4 | 2));
        CHECK(word(gba, REG_JOY_RECV_LO) == 0x78563412);
        CHECK(reg(gba, REG_JOYSTAT) == flags << 4);
        for (unsigned bit = 0; bit < 3; bit++) {
            cupid_io_write8(gba, REG_JOYCNT, (uint8_t)((irq ? 0x40 : 0) | 1u << bit));
            CHECK((reg(gba, REG_JOYCNT) & 7) == (7u & ~((1u << (bit + 1)) - 1)));
        }
        CHECK(gba->irq.pendingIF == (irq ? IRQ_SIO : 0));
        destroy(gba);
    }
}

static void complete_payloads_and_buffer_ownership(void) {
    struct CupidGBA* gba = machine(0);
    unsigned observations = 0;
    for (unsigned lane = 0; lane < 4; lane++) for (unsigned byte = 0; byte < 256; byte++) {
        const uint32_t value = (0xA55A3CC3u & ~(0xFFu << (lane * 8))) | byte << (lane * 8);
        uint8_t packet[8] = {SERIAL_JOY_WRITE, (uint8_t)value, (uint8_t)(value >> 8),
            (uint8_t)(value >> 16), (uint8_t)(value >> 24), 0xA7, 0xB8, 0xC9};
        /* In-place receive consumes all payload bytes before replacing command. */
        CHECK(cupid_serial_joybus_command(&gba->serial, packet, 5, packet, sizeof(packet)) == 1);
        CHECK(packet[0] == 2 && packet[5] == 0xA7 && packet[6] == 0xB8 && packet[7] == 0xC9);
        CHECK(word(gba, REG_JOY_RECV_LO) == value);
        transmit(gba, ~value);
        packet[0] = SERIAL_JOY_READ;
        CHECK(cupid_serial_joybus_command(&gba->serial, packet, 1, packet, sizeof(packet)) == 5);
        CHECK(unpack(packet) == ~value && packet[4] == 8 && packet[5] == 0xA7);
        /* Reading an empty send buffer retains its last word, with ready clear. */
        uint8_t reply[5];
        CHECK(command(gba, SERIAL_JOY_READ, reply) == 5 && unpack(reply) == ~value && !reply[4]);
        receive(gba, value, reply); receive(gba, ~value, reply);
        CHECK(word(gba, REG_JOY_RECV_LO) == ~value);
        ++observations;
    }
    CHECK(observations == 1024);
    printf("Checked %u complete payload patterns and retained/overlapping buffers\n", observations);
    destroy(gba);
}

static void invalid_frames_are_atomic(void) {
    struct CupidGBA* gba = machine(0);
    uint8_t reply[8]; receive(gba, 0x01234567, reply); transmit(gba, 0x89ABCDEF);
    cupid_io_write(gba, REG_JOYCNT, 0x40);
    uint16_t saved[GBA_SIZE_IO / 2]; memcpy(saved, gba->memory.io, sizeof(saved));
    unsigned rejected = 0;
    for (unsigned code = 0; code < 256; code++) for (unsigned length = 0; length <= 6; length++)
      for (unsigned capacity = 0; capacity <= 5; capacity++) {
        uint8_t request[7] = {(uint8_t)code, 1, 2, 3, 4, 5, 6};
        const bool known = code == 0 || code == 0x14 || code == 0x15 || code == 0xFF;
        const unsigned requiredInput = code == 0x15 ? 5 : 1;
        const unsigned requiredOutput = code == 0x14 ? 5 : code == 0x15 ? 1 : 3;
        if (known && length == requiredInput && capacity >= requiredOutput) continue;
        memset(reply, 0xA5, sizeof(reply));
        CHECK(!cupid_serial_joybus_command(&gba->serial, request, length, reply, capacity));
        CHECK(!memcmp(saved, gba->memory.io, sizeof(saved)) && !gba->irq.pendingIF);
        for (unsigned i = 0; i < sizeof(reply); i++) CHECK(reply[i] == 0xA5);
        ++rejected;
    }
    const uint8_t request = SERIAL_JOY_RESET;
    CHECK(!cupid_serial_joybus_command(&gba->serial, NULL, 1, reply, 5));
    CHECK(!cupid_serial_joybus_command(&gba->serial, &request, 1, NULL, 5));
    CHECK(!memcmp(saved, gba->memory.io, sizeof(saved)) && !gba->irq.pendingIF);
    CHECK(rejected == 10740);
    printf("Rejected %u malformed frames without state or output changes\n", rejected);
    destroy(gba);
}

static void cpu_register_widths_and_acknowledgements(void) {
    /* Actual ARM/Thumb STR[B/H]/LDR[B/H] strobes exercise every transmit lane,
     * retained zero reads, receive acknowledgement and control/status merge. */
    for (unsigned thumb = 0; thumb < 2; thumb++) for (unsigned width = 0; width < 3; width++) {
        struct CupidGBA* gba = machine(0);
        const uint32_t armStore[] = {0xE5C10000, 0xE1C100B0, 0xE5810000};
        const uint32_t armLoad[] = {0xE5D10000, 0xE1D100B0, 0xE5910000};
        const uint16_t thumbStore[] = {0x7008, 0x8008, 0x6008};
        const uint16_t thumbLoad[] = {0x7808, 0x8808, 0x6808};
        uint32_t expectedWord = 0;
        for (unsigned lane = 0; lane < 4; lane += 1u << width) {
            memset(gba->memory.iwram, 0, 32);
            if (thumb) { memcpy(gba->memory.iwram, &thumbStore[width], 2); memcpy(gba->memory.iwram + 2, &thumbLoad[width], 2); }
            else { memcpy(gba->memory.iwram, &armStore[width], 4); memcpy(gba->memory.iwram + 4, &armLoad[width], 4); }
            gba->cpu.gprs[0] = (int32_t)0x89ABCDEF;
            gba->cpu.gprs[1] = GBA_BASE_IO + REG_JOY_TRANS_LO + lane;
            _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
            gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM;
            if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
            cupid_gba_step(gba); cupid_gba_step(gba);
            const uint32_t expected = width == 0 ? 0xEF : width == 1 ? 0xCDEF : 0x89ABCDEF;
            const uint32_t mask = width == 0 ? 0xFF : width == 1 ? 0xFFFF : UINT32_MAX;
            expectedWord = (expectedWord & ~(mask << (lane * 8))) | expected << (lane * 8);
            CHECK(!gba->cpu.gprs[0] && (reg(gba, REG_JOYSTAT) & 8));
            uint8_t outgoing[5];
            CHECK(command(gba, SERIAL_JOY_READ, outgoing) == 5 && unpack(outgoing) == expectedWord && outgoing[4] == 8);
            cupid_io_write(gba, REG_JOYCNT, 4);
        }
        uint8_t reply[5]; receive(gba, 0x13572468, reply);
        CHECK(word(gba, REG_JOY_RECV_LO) == 0x13572468 && !(reg(gba, REG_JOYSTAT) & 2));
        cupid_io_write8(gba, REG_JOYCNT + 1, 0xFF);
        CHECK(reg(gba, REG_JOYCNT) == 2);
        cupid_io_write8(gba, REG_JOYCNT, 0x42);
        CHECK(reg(gba, REG_JOYCNT) == 0x40);
        cupid_io_write8(gba, REG_JOYSTAT, 0xFF);
        CHECK(reg(gba, REG_JOYSTAT) == 0x30);
        transmit(gba, expectedWord);
        cupid_io_write8(gba, REG_JOYSTAT + 1, 0xFF);
        CHECK(reg(gba, REG_JOYSTAT) == 0x38);
        cupid_io_write8(gba, REG_JOYSTAT, 0);
        CHECK(reg(gba, REG_JOYSTAT) == 8);
        destroy(gba);
    }
}

static void irq_enable_and_halt_use_existing_pipeline(void) {
    const uint8_t commands[] = {SERIAL_JOY_STATUS, SERIAL_JOY_RESET, SERIAL_JOY_READ, SERIAL_JOY_WRITE};
    for (unsigned origin = 0; origin < 4; origin++) for (unsigned enable = 0; enable < 2; enable++)
      for (unsigned ime = 0; ime < 2; ime++) for (unsigned c = 0; c < 4; c++) {
        struct CupidGBA* gba = machine(origins[origin]);
        cupid_io_write(gba, REG_IE, IRQ_SIO); cupid_io_write(gba, REG_IME, (uint16_t)ime); tick(gba, 1);
        cupid_io_write(gba, REG_JOYCNT, enable ? 0x40 : 0);
        /* SIOCNT IRQ enable does not gate a JOY command. */
        cupid_io_write(gba, REG_SIOCNT, enable ? 0 : 0x4000);
        uint8_t reply[5];
        if (commands[c] == SERIAL_JOY_WRITE) receive(gba, 0xDEADBEEF, reply);
        else CHECK(command(gba, commands[c], reply));
        const bool raised = enable && commands[c] != SERIAL_JOY_STATUS;
        CHECK(gba->irq.pendingIF == (raised ? IRQ_SIO : 0));
        CHECK(!reg(gba, REG_IF) && !gba->irq.available && !gba->irq.line);
        tick(gba, 1); CHECK(reg(gba, REG_IF) == (raised ? IRQ_SIO : 0));
        tick(gba, 1); CHECK(gba->irq.available == raised && !gba->irq.line);
        tick(gba, 1); CHECK(gba->irq.line == (raised && ime));
        if (raised) {
            const uint32_t code[] = {0xE2855001, 0xEAFFFFFE};
            memcpy(gba->memory.iwram, code, sizeof(code));
            gba->cpu.gprs[5] = 0; gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM; CupidARMWritePC(&gba->cpu);
            cupid_gba_halt(gba);
            for (unsigned steps = 0; steps < 20 && gba->cpu.halted; steps++) cupid_gba_step(gba);
            CHECK(!gba->cpu.halted && gba->cpu.cpsr.i);
        }
        destroy(gba);
    }
}

struct Incoming {
    struct CupidGBA* gba;
    struct CupidTimingEvent event;
    uint8_t reply[5];
    uint64_t timestamp;
    unsigned calls;
};
static void incoming_callback(struct CupidTiming* timing, void* context, uint32_t late) {
    (void)late;
    struct Incoming* peer = context;
    CHECK(cupid_timing_global_time(timing) == peer->timestamp);
    receive(peer->gba, 0x78563412, peer->reply);
    ++peer->calls;
}
static void arrange_incoming(struct Incoming* peer, struct CupidGBA* gba, unsigned delay) {
    *peer = (struct Incoming){.gba = gba, .timestamp = cupid_timing_global_time(&gba->timing) + delay,
        .event = {.callback = incoming_callback, .name = "Owned JOY peer frame", .priority = 0x80}};
    peer->event.context = peer;
    cupid_timing_schedule(&gba->timing, &peer->event, (int32_t)delay);
}
static void timed_frames_keep_dma_timer_and_irq_chronology(void) {
    for (unsigned origin = 0; origin < 4; origin++) {
        struct CupidGBA* boards[2] = {machine(origins[origin]), machine(origins[origin])};
        struct Incoming peers[2];
        for (unsigned i = 0; i < 2; i++) {
            struct CupidGBA* gba = boards[i];
            cupid_io_write(gba, REG_JOYCNT, 0x40);
            cupid_io_write(gba, REG_TM0CNT_L, 0xFFF0); cupid_io_write(gba, REG_TM0CNT_H, 0xC0);
            cupid_dma_write_sad(gba, 0, GBA_BASE_IO + REG_JOY_RECV_LO);
            cupid_dma_write_dad(gba, 0, GBA_BASE_EWRAM);
            cupid_dma_write_cnt_lo(gba, 0, 1);
            cupid_dma_write_cnt(gba, 0, 0xC400);
            arrange_incoming(&peers[i], gba, 1);
        }
        for (unsigned t = 0; t < 80; t++) tick(boards[0], 1);
        tick(boards[1], 80);
        for (unsigned i = 0; i < 2; i++) {
            struct CupidGBA* gba = boards[i];
            CHECK(peers[i].calls == 1 && peers[i].reply[0] == 2);
            uint32_t stored; memcpy(&stored, gba->memory.ewram, 4);
            CHECK(stored == 0x78563412 && !(reg(gba, REG_JOYSTAT) & 2));
            CHECK((reg(gba, REG_IF) & (IRQ_DMA0 | IRQ_TIMER0 | IRQ_SIO)) == (IRQ_DMA0 | IRQ_TIMER0 | IRQ_SIO));
            CHECK(!gba->memory.dma[0].enable && !gba->memory.dmaRunning && gba->memory.activeDMA == -1);
        }
        CHECK(cupid_timer_read_counter(boards[0], 0) == cupid_timer_read_counter(boards[1], 0));
        CHECK(boards[0]->irq.pendingIF == boards[1]->irq.pendingIF);
        CHECK(boards[0]->memory.dma[0].nextSource == boards[1]->memory.dma[0].nextSource);
        destroy(boards[0]); destroy(boards[1]);
    }
}

static void mode_stop_reset_and_other_transfers_stay_isolated(void) {
    for (unsigned mode = 0; mode < 5; mode++) {
        struct CupidGBA* gba = machine(0);
        cupid_io_write(gba, REG_RCNT, mode == SERIAL_GPIO ? 0x8000 : 0);
        cupid_io_write(gba, REG_SIOCNT, (uint16_t)(mode < 4 ? mode << 12 : 0));
        if (mode == SERIAL_NORMAL8 || mode == SERIAL_NORMAL32) cupid_io_write(gba, REG_SIOCNT, (uint16_t)(mode << 12 | 0x81));
        const unsigned remaining = gba->serial.remaining;
        const uint32_t when = gba->serial.edgeEvent.when;
        uint16_t saved[GBA_SIZE_IO / 2]; memcpy(saved, gba->memory.io, sizeof(saved));
        uint8_t reply[5] = {0xA5, 0xA5, 0xA5, 0xA5, 0xA5};
        CHECK(!command(gba, SERIAL_JOY_RESET, reply));
        CHECK(!memcmp(saved, gba->memory.io, sizeof(saved)) && reply[0] == 0xA5);
        CHECK(gba->serial.remaining == remaining && gba->serial.edgeEvent.when == when);
        destroy(gba);
    }
    struct CupidGBA* gba = machine(0);
    cupid_io_write(gba, REG_IE, IRQ_KEYPAD); tick(gba, 1); cupid_gba_stop(gba);
    uint8_t reply[5] = {0xA5};
    const uint64_t frozen = cupid_timing_global_time(&gba->timing);
    CHECK(!command(gba, SERIAL_JOY_RESET, reply) && reply[0] == 0xA5 && !reg(gba, REG_JOYCNT));
    CHECK(gba->stopped && cupid_timing_global_time(&gba->timing) == frozen && !gba->irq.pendingIF);
    cupid_gba_wake_stop(gba, IRQ_KEYPAD); CHECK(!gba->stopped);
    receive(gba, 0x12345678, reply); transmit(gba, 0x89ABCDEF); CHECK(command(gba, SERIAL_JOY_RESET, reply) == 3);
    CHECK(command(gba, SERIAL_JOY_READ, reply) == 5 && unpack(reply) == 0x89ABCDEF && reply[4] == 10);
    CHECK(!word(gba, REG_JOY_TRANS_LO) && word(gba, REG_JOY_RECV_LO) == 0x12345678);
    cupid_serial_reset(&gba->serial);
    CHECK(!command(gba, SERIAL_JOY_STATUS, reply));
    cupid_io_write(gba, REG_RCNT, 0xC000);
    CHECK(!word(gba, REG_JOY_TRANS_LO) && !word(gba, REG_JOY_RECV_LO));
    CHECK(!reg(gba, REG_JOYCNT) && !reg(gba, REG_JOYSTAT));
    CHECK(command(gba, SERIAL_JOY_READ, reply) == 5 && !unpack(reply) && !reply[4]);
    destroy(gba);
}

static bool terminal(struct CupidGBA* gba, bool thumb) {
    /* The core keeps PC one instruction beyond its current pipeline slot. */
    return gba->cpu.executionMode == (thumb ? MODE_THUMB : MODE_ARM) &&
        (uint32_t)gba->cpu.gprs[ARM_PC] == GBA_BASE_IWRAM + (thumb ? 0x1FEu : 0x100u);
}
static void guest_start(struct CupidGBA* gba, bool thumb) {
    memcpy(gba->memory.iwram, joybusDriver, sizeof(joybusDriver));
    memset(gba->memory.ewram, 0xA5, 256);
    gba->cpu.gprs[2] = 0x10203040;
    gba->cpu.gprs[4] = GBA_BASE_IO + REG_JOY_RECV_LO;
    gba->cpu.gprs[5] = GBA_BASE_IO + REG_JOY_TRANS_LO;
    gba->cpu.gprs[6] = GBA_BASE_IO + REG_JOYCNT;
    gba->cpu.gprs[7] = GBA_BASE_EWRAM;
    gba->cpu.gprs[12] = (int32_t)0xDEADBEEF;
    _CupidSetMode(&gba->cpu, thumb ? MODE_THUMB : MODE_ARM);
    gba->cpu.gprs[ARM_PC] = GBA_BASE_IWRAM + (thumb ? 0x100 : 0);
    if (thumb) CupidThumbWritePC(&gba->cpu); else CupidARMWritePC(&gba->cpu);
    cupid_io_write(gba, REG_JOYCNT, 0x40); cupid_io_write(gba, REG_IE, IRQ_SIO);
}
static void complete_arm_thumb_guest_sessions(void) {
    unsigned rounds = 0;
    for (unsigned origin = 0; origin < 4; origin++) for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(origins[origin]); guest_start(gba, thumb != 0);
        for (unsigned round = 0; round < 32; round++) {
            const uint32_t value = 0x10203040u + round * 0x01010101u;
            uint8_t reply[5]; receive(gba, value, reply); CHECK(reply[0] == 2);
            unsigned steps;
            for (steps = 0; steps < 2000 && !gba->cpu.halted && !terminal(gba, thumb != 0); steps++) cupid_gba_step(gba);
            CHECK(steps < 2000 && gba->cpu.halted && !terminal(gba, thumb != 0));
            uint32_t stored, count;
            memcpy(&stored, gba->memory.ewram + round * 4, 4); memcpy(&count, gba->memory.ewram + 128 + round * 4, 4);
            CHECK(stored == value && count == round + 1 && reg(gba, REG_JOYCNT) == 0x40 && reg(gba, REG_JOYSTAT) == 8);
            CHECK(command(gba, SERIAL_JOY_READ, reply) == 5 && unpack(reply) == ~value && reply[4] == 8);
            for (steps = 0; steps < 2000 && gba->cpu.halted; steps++) cupid_gba_step(gba);
            CHECK(steps < 2000 && !gba->cpu.halted);
            ++rounds;
        }
        unsigned steps;
        for (steps = 0; steps < 2000 && !terminal(gba, thumb != 0); steps++) cupid_gba_step(gba);
        if (steps == 2000 || gba->cpu.gprs[12] || gba->cpu.gprs[3] != 32)
            fprintf(stderr, "JOY guest origin=%u thumb=%u steps=%u pc=%08X r3=%08X r12=%08X control=%04X status=%04X\n",
                origin, thumb, steps, (uint32_t)gba->cpu.gprs[ARM_PC], (uint32_t)gba->cpu.gprs[3],
                (uint32_t)gba->cpu.gprs[12], reg(gba, REG_JOYCNT), reg(gba, REG_JOYSTAT));
        CHECK(steps < 2000 && !gba->cpu.gprs[12] && gba->cpu.gprs[3] == 32);
        cupid_gba_step(gba); CHECK(terminal(gba, thumb != 0) && !gba->cpu.gprs[12]);
        for (unsigned round = 0; round < 32; round++) {
            uint32_t stored, count;
            memcpy(&stored, gba->memory.ewram + round * 4, 4);
            memcpy(&count, gba->memory.ewram + 128 + round * 4, 4);
            CHECK(stored == 0x10203040u + round * 0x01010101u && count == round + 1);
        }
        CHECK(reg(gba, REG_JOYCNT) == 0x40 && !reg(gba, REG_JOYSTAT));
        destroy(gba);
    }
    CHECK(rounds == 256);
    /* The same complete guest rejects a changed input word at its FAIL terminal. */
    for (unsigned thumb = 0; thumb < 2; thumb++) {
        struct CupidGBA* gba = machine(0); guest_start(gba, thumb != 0); uint8_t reply[5]; receive(gba, 0x10203041, reply);
        unsigned steps;
        for (steps = 0; steps < 2000 && !terminal(gba, thumb != 0); steps++) cupid_gba_step(gba);
        CHECK(steps < 2000 && gba->cpu.gprs[12] == 1 && !gba->cpu.gprs[3]);
        cupid_gba_step(gba); CHECK(terminal(gba, thumb != 0) && gba->cpu.gprs[12] == 1); destroy(gba);
    }
    printf("Checked %u complete guest receive/send/HALT rounds and two failing-input controls\n", rounds);
}

int main(int argc, char** argv) {
    CHECK(argc == 2 || argc == 3);
    if (argc == 3) externalBios = argv[2];
    if (!strcmp(argv[1], "commands")) RUN(commands_preserve_independent_flags);
    else if (!strcmp(argv[1], "payloads")) RUN(complete_payloads_and_buffer_ownership);
    else if (!strcmp(argv[1], "validation")) RUN(invalid_frames_are_atomic);
    else if (!strcmp(argv[1], "registers")) RUN(cpu_register_widths_and_acknowledgements);
    else if (!strcmp(argv[1], "interrupts")) RUN(irq_enable_and_halt_use_existing_pipeline);
    else if (!strcmp(argv[1], "chronology")) RUN(timed_frames_keep_dma_timer_and_irq_chronology);
    else if (!strcmp(argv[1], "lifecycle")) RUN(mode_stop_reset_and_other_transfers_stay_isolated);
    else if (!strcmp(argv[1], "guest")) RUN(complete_arm_thumb_guest_sessions);
    else CHECK(false);
    return 0;
}
