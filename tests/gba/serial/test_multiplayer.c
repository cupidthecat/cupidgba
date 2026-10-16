/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/gba.h"
#include "../../test.h"

static const unsigned completion[4][4] = {
    {31976, 63427, 94884, 125829}, {8378, 16241, 24104, 31457},
    {5750, 10998, 16241, 20972}, {3140, 5755, 8376, 10486}
};
/* Nominal eighteen-bit receive phases; inter-player electrical gaps remain
 * unqualified. These cases check the modeled publication and completion. */
static const unsigned slots[4][4] = {
    {31457, 62915, 94372, 125829}, {7864, 15729, 23593, 31457},
    {5243, 10486, 15729, 20972}, {2621, 5243, 7864, 10486}
};
static struct CupidGBA* machine(uint64_t clock) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba)); CHECK(gba);
    cupid_gba_create(gba); cupid_gba_reset(gba); cupid_timing_clear(&gba->timing);
    gba->timing.globalCycles = clock; gba->timing.masterCycles = (uint32_t)clock;
    gba->cpu.cpsr.i = 1; gba->cpu.irqDisableLatch = true;
    return gba;
}
static void destroy(struct CupidGBA* gba) { cupid_gba_destroy(gba); free(gba); }
static void tick(struct CupidGBA* gba, unsigned clocks) {
    CHECK(!gba->cpu.cycles); cupid_timing_tick(&gba->timing, (int32_t)clocks);
}
struct Peer { unsigned baud, calls; uint64_t start; bool error; };
static bool exchange(void* context, uint16_t transmitted, unsigned baud,
                     uint16_t received[4], uint64_t timestamp) {
    struct Peer* peer = context;
    CHECK(transmitted == 0x5AA5 && baud == peer->baud && timestamp == peer->start);
    received[0] = 0xDEAD; received[1] = 0x1357; received[2] = 0x2468; received[3] = 0xBEEF;
    ++peer->calls;
    return !peer->error;
}
static void configure(struct CupidGBA* gba, unsigned id, unsigned players, struct Peer* peer) {
    CHECK(cupid_serial_set_multiplayer_peer(&gba->serial, id, (uint8_t)players, exchange, peer));
    cupid_io_write(gba, REG_RCNT, 0);
    cupid_io_write(gba, REG_SIOCNT, 0x2000);
    cupid_io_write(gba, REG_SIOMLT_SEND, 0x5AA5);
}
static uint16_t receive(struct CupidGBA* gba, unsigned slot) {
    return cupid_io_read(gba, REG_SIODATA32_LO + slot * 2);
}
static void master_words_publish_in_order(void) {
    const uint64_t origins[] = {0, UINT32_MAX - 100u, UINT64_C(0x300000000)};
    const uint16_t expected[] = {0x5AA5, 0x1357, 0x2468, 0xBEEF};
    for (unsigned origin = 0; origin < 3; origin++) for (unsigned baud = 0; baud < 4; baud++)
      for (unsigned count = 1; count <= 4; count++) {
        struct CupidGBA* gba = machine(origins[origin]);
        struct Peer peer = {.baud = baud, .start = origins[origin]};
        configure(gba, 0, (1u << count) - 1, &peer);
        cupid_io_write(gba, REG_SIOCNT, (uint16_t)(0x6080 | baud));
        CHECK(peer.calls == 1 && gba->serial.active);
        for (unsigned slot = 0; slot < 4; slot++) CHECK(receive(gba, slot) == 0xFFFF);
        cupid_io_write(gba, REG_SIOMLT_SEND, 0xAA55);
        unsigned elapsed = 0;
        for (unsigned slot = 0; slot < count; slot++) {
            tick(gba, slots[baud][slot] - elapsed - 1);
            CHECK(receive(gba, slot) == 0xFFFF && gba->serial.active);
            tick(gba, 1);
            elapsed = slots[baud][slot];
            for (unsigned ready = 0; ready <= slot; ready++) CHECK(receive(gba, ready) == expected[ready]);
            for (unsigned pending = slot + 1; pending < 4; pending++) CHECK(receive(gba, pending) == 0xFFFF);
        }
        if (completion[baud][count - 1] > elapsed) {
            CHECK(gba->serial.active);
            tick(gba, completion[baud][count - 1] - elapsed);
        }
        CHECK(!gba->serial.active && !(cupid_io_read(gba, REG_SIOCNT) & 0x80));
        CHECK(gba->irq.pendingIF & IRQ_SIO);
        CHECK(!(gba->memory.io[REG_IF >> 1] & IRQ_SIO));
        tick(gba, 1);
        CHECK(gba->memory.io[REG_IF >> 1] & IRQ_SIO);
        CHECK(peer.calls == 1 && !(cupid_io_read(gba, REG_SIOCNT) & 0x40));
        destroy(gba);
    }
}
static void slave_frames_latch_local_words(void) {
    const uint16_t incoming[] = {0x1111, 0x2222, 0x3333, 0x4444};
    for (unsigned id = 1; id < 4; id++) for (unsigned baud = 0; baud < 4; baud++) {
        struct CupidGBA* gba = machine(UINT32_MAX - 10u);
        struct Peer peer = {0}; configure(gba, id, 15, &peer);
        cupid_io_write(gba, REG_SIOCNT, 0x6083);
        CHECK(!gba->serial.active && !(cupid_io_read(gba, REG_SIOCNT) & 0x80) && !peer.calls);
        CHECK(cupid_serial_multiplayer_receive(&gba->serial, baud, incoming, id == 2));
        CHECK(!cupid_serial_multiplayer_receive(&gba->serial, baud, incoming, false));
        CHECK((cupid_io_read(gba, REG_SIOCNT) >> 4 & 3) == id);
        cupid_io_write(gba, REG_SIOMLT_SEND, 0xAA55);
        tick(gba, completion[baud][3]);
        for (unsigned slot = 0; slot < 4; slot++) CHECK(receive(gba, slot) == (slot == id ? 0x5AA5 : incoming[slot]));
        CHECK(((cupid_io_read(gba, REG_SIOCNT) & 0x40) != 0) == (id == 2));
        CHECK(!gba->serial.active && (gba->irq.pendingIF & IRQ_SIO) && !peer.calls);
        destroy(gba);
    }
}
static void cancellation_reset_and_late_dispatch(void) {
    for (unsigned variant = 0; variant < 3; variant++) {
        struct CupidGBA* gba = machine(0);
        struct Peer peer = {.baud = 3}; configure(gba, 0, 15, &peer);
        cupid_io_write(gba, REG_SIOCNT, 0x6083);
        tick(gba, 3000); CHECK(receive(gba, 0) == 0x5AA5 && receive(gba, 1) == 0xFFFF);
        if (!variant) {
            cupid_io_write(gba, REG_SIOCNT, 0x1000);
            cupid_io_write(gba, REG_SIODATA32_LO, 0xCAFE); cupid_io_write(gba, REG_SIODATA32_HI, 0xBABE);
            tick(gba, 20000);
            CHECK(receive(gba, 0) == 0xCAFE && receive(gba, 1) == 0xBABE);
            CHECK(!gba->serial.active && !(gba->irq.pendingIF & IRQ_SIO));
        } else if (variant == 1) {
            cupid_serial_reset(&gba->serial); tick(gba, 20000);
            CHECK(!gba->serial.active && !gba->serial.multiplayerSlot && !(gba->irq.pendingIF & IRQ_SIO));
            CHECK(gba->serial.multiplayerPlayers == 15 && gba->serial.multiplayerContext == &peer);
            for (unsigned slot = 0; slot < 4; slot++) CHECK(!gba->memory.io[(REG_SIODATA32_LO >> 1) + slot]);
        } else {
            tick(gba, 20000);
            CHECK(!gba->serial.active && receive(gba, 1) == 0x1357 && receive(gba, 2) == 0x2468 && receive(gba, 3) == 0xBEEF);
            CHECK(gba->memory.io[REG_IF >> 1] & IRQ_SIO);
        }
        destroy(gba);
    }
}
static void stop_freezes_transfer_and_rejects_new_slave_frames(void) {
    struct CupidGBA* gba = machine(0);
    cupid_io_write(gba, REG_IE, IRQ_KEYPAD); tick(gba, 1);
    struct Peer peer = {.baud = 3, .start = 1}; configure(gba, 0, 3, &peer);
    cupid_io_write(gba, REG_SIOCNT, 0x6083); tick(gba, 1000);
    cupid_gba_stop(gba);
    const uint64_t frozen = cupid_timing_global_time(&gba->timing);
    for (unsigned step = 0; step < 10; step++) cupid_gba_step(gba);
    CHECK(gba->stopped && cupid_timing_global_time(&gba->timing) == frozen && gba->serial.active);
    cupid_gba_wake_stop(gba, IRQ_KEYPAD); CHECK(!gba->stopped);
    tick(gba, 5755 - 1000);
    CHECK(!gba->serial.active && receive(gba, 1) == 0x1357);
    destroy(gba);
    gba = machine(0); peer = (struct Peer){0}; configure(gba, 1, 3, &peer);
    cupid_gba_stop(gba);
    const uint16_t incoming[] = {1, 2, 3, 4};
    CHECK(!cupid_serial_multiplayer_receive(&gba->serial, 3, incoming, false) && !gba->serial.active);
    CHECK(!cupid_serial_set_multiplayer_peer(&gba->serial, 4, 15, NULL, NULL));
    CHECK(!cupid_serial_set_multiplayer_peer(&gba->serial, 1, 5, NULL, NULL));
    destroy(gba);
}
struct TwoBoards { struct CupidGBA* child; unsigned calls; };
static bool exchange_two_boards(void* context, uint16_t transmitted, unsigned baud,
                               uint16_t received[4], uint64_t timestamp) {
    struct TwoBoards* cable = context;
    CHECK(cupid_timing_global_time(&cable->child->timing) == timestamp);
    received[0] = transmitted;
    received[1] = cupid_io_read(cable->child, REG_SIOMLT_SEND);
    received[2] = received[3] = 0xFFFF;
    CHECK(cupid_serial_multiplayer_receive(&cable->child->serial, baud, received, false));
    ++cable->calls;
    return true;
}
static void two_synchronized_boards_exchange_latched_words(void) {
    for (unsigned order = 0; order < 2; order++) {
        struct CupidGBA* parent = machine(UINT32_MAX - 100u);
        struct CupidGBA* child = machine(UINT32_MAX - 100u);
        struct TwoBoards cable = {.child = child};
        CHECK(cupid_serial_set_multiplayer_peer(&parent->serial, 0, 3, exchange_two_boards, &cable));
        CHECK(cupid_serial_set_multiplayer_peer(&child->serial, 1, 3, NULL, NULL));
        cupid_io_write(parent, REG_RCNT, 0); cupid_io_write(child, REG_RCNT, 0);
        cupid_io_write(parent, REG_SIOCNT, 0x2000); cupid_io_write(child, REG_SIOCNT, 0x6000);
        cupid_io_write(parent, REG_SIOMLT_SEND, 0xCAFE); cupid_io_write(child, REG_SIOMLT_SEND, 0xBABE);
        cupid_io_write(parent, REG_SIOCNT, 0x6083);
        CHECK(cable.calls == 1 && parent->serial.active && child->serial.active);
        cupid_io_write(child, REG_SIOMLT_SEND, 0x1234);
        tick(order ? child : parent, 5755);
        tick(order ? parent : child, 5755);
        for (unsigned board = 0; board < 2; board++) {
            struct CupidGBA* gba = board ? child : parent;
            CHECK(!gba->serial.active && receive(gba, 0) == 0xCAFE && receive(gba, 1) == 0xBABE);
            CHECK(receive(gba, 2) == 0xFFFF && receive(gba, 3) == 0xFFFF);
            CHECK(gba->irq.pendingIF & IRQ_SIO);
        }
        CHECK(cupid_timing_global_time(&parent->timing) == cupid_timing_global_time(&child->timing));
        destroy(parent); destroy(child);
    }
}
static void master_errors_and_connection_changes(void) {
    for (unsigned cancel = 0; cancel < 2; cancel++) {
        struct CupidGBA* gba = machine(0);
        struct Peer peer = {.baud = 3, .error = true}; configure(gba, 0, 3, &peer);
        cupid_io_write(gba, REG_SIOCNT, 0x6083);
        if (cancel) CHECK(cupid_serial_set_multiplayer_peer(&gba->serial, 0, 0, NULL, NULL));
        tick(gba, 5755);
        CHECK(!gba->serial.active && peer.calls == 1);
        if (cancel) CHECK(!(cupid_io_read(gba, REG_SIOCNT) & 0xC0) && !(gba->irq.pendingIF & IRQ_SIO));
        else CHECK((cupid_io_read(gba, REG_SIOCNT) & 0x40) && (gba->irq.pendingIF & IRQ_SIO));
        destroy(gba);
    }
}
static void disconnected_terminal_does_not_generate_a_clock(void) {
    for (unsigned baud = 0; baud < 4; baud++) {
        struct CupidGBA* gba = machine(0);
        cupid_io_write(gba, REG_RCNT, 0);
        cupid_io_write(gba, REG_SIOCNT, (uint16_t)(0x6080 | baud));
        CHECK(cupid_io_read(gba, REG_SIOCNT) & 4);
        CHECK(gba->serial.active && (cupid_io_read(gba, REG_SIOCNT) & 0x80));
        CHECK(!cupid_timing_is_scheduled(&gba->timing, &gba->serial.edgeEvent));
        tick(gba, completion[baud][3] + 10);
        CHECK(!(gba->irq.pendingIF & IRQ_SIO));
        for (unsigned slot = 0; slot < 4; slot++) CHECK(!receive(gba, slot));
        cupid_io_write(gba, REG_SIOCNT, 0x2000);
        CHECK(!gba->serial.active && !(cupid_io_read(gba, REG_SIOCNT) & 0x80));
        destroy(gba);
    }
}
int main(void) {
    RUN(master_words_publish_in_order);
    RUN(slave_frames_latch_local_words);
    RUN(cancellation_reset_and_late_dispatch);
    RUN(stop_freezes_transfer_and_rejects_new_slave_frames);
    RUN(two_synchronized_boards_exchange_latched_words);
    RUN(master_errors_and_connection_changes);
    RUN(disconnected_terminal_does_not_generate_a_clock);
    return 0;
}
