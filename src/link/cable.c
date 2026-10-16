/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cupid/cable.h"
#include "../platform/context.h"
#include <stdlib.h>

struct CableNode {
    struct CupidCable* cable;
    struct CupidGBA* gba;
    struct CupidContext* context;
    uint64_t target;
    unsigned id;
    bool idle;
};

struct CupidCable {
    struct CupidContext* host;
    struct CableNode nodes[4];
    unsigned count;
    uint64_t now;
    bool connected, propagating, dirty, running, cancelling;
};

static void dispatch_events(struct CupidCable* cable);

static void run_core(void* opaque) {
    struct CableNode* node = opaque;
    for (;;) {
        if (node->gba->stopped) {
            node->idle = true;
            cupid_context_yield(node->context);
            node->idle = false;
        }
        cupid_gba_step(node->gba);
    }
}

static int32_t synchronize(void* opaque, struct CupidTiming* timing, int32_t clocks) {
    struct CableNode* node = opaque;
    if (clocks < 0) abort();
    /* No other CPU can run between equal-time boundaries of this CPU. Dispatch
     * due peripherals directly, including nested clock flushes from MMIO in
     * a peripheral callback, without switching an already suspended stack. */
    if (!clocks) {
        dispatch_events(node->cable);
        return cupid_timing_next_event(timing);
    }
    node->target = node->cable->now + (uint32_t)clocks;
    cupid_context_yield(node->context);
    return cupid_timing_next_event(timing);
}

static void propagate(struct CupidCable* cable) {
    cable->dirty = true;
    if (cable->propagating) return;
    cable->propagating = true;
    do {
        cable->dirty = false;
        for (unsigned i = 0; i < cable->count; ++i) {
            if (!cable->nodes[i].gba) continue;
            struct CupidSerial* serial = &cable->nodes[i].gba->serial;
            uint8_t mask = 0, levels = 0;
            if (cable->connected && cable->count == 2) {
                struct CupidSerial* peer = &cable->nodes[i ^ 1].gba->serial;
                if (peer->outputMask & SERIAL_SO) {
                    mask |= SERIAL_SI;
                    if (peer->outputLevels & SERIAL_SO) levels |= SERIAL_SI;
                }
                if (cupid_serial_mode(serial) == SERIAL_UART) {
                    if (peer->outputMask & SERIAL_SD) {
                        mask |= SERIAL_SC;
                        if (peer->outputLevels & SERIAL_SD) levels |= SERIAL_SC;
                    }
                } else {
                    mask |= peer->outputMask & (SERIAL_SC | SERIAL_SD);
                    levels |= peer->outputLevels & (SERIAL_SC | SERIAL_SD);
                }
            }
            cupid_serial_set_input(serial, mask, levels);
        }
    } while (cable->dirty);
    cable->propagating = false;
}

static void output_changed(void* opaque, uint8_t mask, uint8_t levels, uint64_t timestamp) {
    (void)mask; (void)levels; (void)timestamp;
    struct CableNode* node = opaque;
    propagate(node->cable);
}

static bool exchange(void* opaque, uint16_t sent, unsigned baud, uint16_t received[4], uint64_t timestamp) {
    (void)timestamp;
    struct CableNode* master = opaque;
    struct CupidCable* cable = master->cable;
    bool valid = cable->connected && master->id == 0;
    for (unsigned i = 0; i < 4; ++i) received[i] = 0xFFFF;
    received[0] = sent;
    for (unsigned i = 1; i < cable->count; ++i) {
        struct CupidGBA* gba = cable->nodes[i].gba;
        bool ready = !gba->stopped && !gba->serial.active && cupid_serial_mode(&gba->serial) == SERIAL_MULTI;
        if (ready) received[i] = gba->memory.io[REG_SIOMLT_SEND >> 1];
        valid &= ready;
    }
    for (unsigned i = 1; i < cable->count; ++i)
        cupid_serial_multiplayer_receive(&cable->nodes[i].gba->serial, baud, received, !valid);
    return valid;
}

static bool multiplayer_ready(void* opaque) {
    struct CableNode* node = opaque;
    struct CupidCable* cable = node->cable;
    if (!cable->connected) return false;
    for (unsigned i = 0; i < cable->count; ++i)
        if (cable->nodes[i].gba->stopped || cupid_serial_mode(&cable->nodes[i].gba->serial) != SERIAL_MULTI) return false;
    return true;
}

static void multiplayer_cancelled(void* opaque) {
    struct CableNode* node = opaque;
    struct CupidCable* cable = node->cable;
    if (cable->cancelling) return;
    cable->cancelling = true;
    for (unsigned i = 0; i < cable->count; ++i) {
        if (i == node->id || !cable->nodes[i].gba) continue;
        struct CupidSerial* serial = &cable->nodes[i].gba->serial;
        cupid_serial_set_multiplayer_peer(serial, serial->multiplayerId, serial->multiplayerPlayers,
                                          serial->multiplayerExchange, serial->multiplayerContext);
    }
    cable->cancelling = false;
}

struct CupidCable* cupid_cable_create(struct CupidGBA* const cores[], unsigned count) {
    if (!cores || count < 2 || count > 4) return NULL;
    for (unsigned i = 0; i < count; ++i) {
        if (!cores[i] || cores[i]->timing.synchronize || cores[i]->serial.outputChanged ||
            cores[i]->serial.multiplayerExchange || cores[i]->serial.multiplayerReady ||
            cores[i]->serial.multiplayerCancelled || cores[i]->timing.inCallback || cores[i]->cpu.cycles) return NULL;
        if (cores[i]->serial.active || cores[i]->serial.uart.transmitting || cores[i]->serial.uart.receiving) return NULL;
        for (unsigned j = 0; j < i; ++j) if (cores[i] == cores[j]) return NULL;
    }
    struct CupidCable* cable = calloc(1, sizeof(*cable));
    if (!cable) return NULL;
    cable->count = count;
    cable->host = cupid_context_host_create();
    if (!cable->host) { free(cable); return NULL; }
    for (unsigned i = 0; i < count; ++i) {
        struct CableNode* node = &cable->nodes[i];
        node->cable = cable; node->gba = cores[i]; node->id = i;
        node->context = cupid_context_create(cable->host, run_core, node);
        if (!node->context) {
            for (unsigned j = 0; j < i; ++j) cupid_context_destroy(cable->nodes[j].context);
            cupid_context_destroy(cable->host);
            free(cable);
            return NULL;
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        struct CableNode* node = &cable->nodes[i];
        cores[i]->timing.synchronize = synchronize;
        cores[i]->timing.synchronizeContext = node;
        cupid_serial_set_output_callback(&cores[i]->serial, output_changed, node);
        cores[i]->serial.multiplayerReady = multiplayer_ready;
        cores[i]->serial.multiplayerCancelled = multiplayer_cancelled;
    }
    cupid_cable_connect(cable, true);
    return cable;
}

void cupid_cable_connect(struct CupidCable* cable, bool connected) {
    if (!cable || cable->running) return;
    if (cable->connected != connected) {
        cable->connected = connected;
        for (unsigned i = 0; i < cable->count; ++i) {
            if (cable->nodes[i].gba)
                cupid_serial_set_multiplayer_peer(&cable->nodes[i].gba->serial, i,
                    connected ? (uint8_t)((1u << cable->count) - 1) : 0, connected ? exchange : NULL,
                    &cable->nodes[i]);
        }
    }
    propagate(cable);
}

void cupid_cable_destroy(struct CupidCable* cable) {
    if (!cable) return;
    cupid_cable_connect(cable, false);
    for (unsigned i = 0; i < cable->count; ++i) {
        struct CableNode* node = &cable->nodes[i];
        cupid_context_destroy(node->context);
        if (node->gba) {
            node->gba->timing.synchronize = NULL;
            node->gba->timing.synchronizeContext = NULL;
            node->gba->serial.multiplayerReady = NULL;
            node->gba->serial.multiplayerCancelled = NULL;
            node->gba->serial.multiplayerContext = NULL;
            cupid_serial_set_output_callback(&node->gba->serial, NULL, NULL);
        }
    }
    cupid_context_destroy(cable->host);
    free(cable);
}

static void dispatch_events(struct CupidCable* cable) {
    for (;;) {
        struct CupidTiming* selected = NULL;
        unsigned priority = UINT_MAX;
        for (unsigned i = 0; i < cable->count; ++i) {
            struct CupidGBA* gba = cable->nodes[i].gba;
            const struct CupidTimingEvent* event = cupid_timing_first_event(&gba->timing);
            if (!gba->stopped && event && cupid_timing_until(&gba->timing, event) <= 0 &&
                (!selected || event->priority < priority)) {
                selected = &gba->timing; priority = event->priority;
            }
        }
        if (!selected) return;
        /* A new peer event can precede the event a HALTed CPU or a DMA wait
         * originally requested. Fixed-duration bus phases remain suspended. */
        if (selected->waitingForEvent) {
            struct CableNode* node = selected->synchronizeContext;
            node->target = cable->now;
        }
        cupid_timing_dispatch_next(selected);
    }
}

bool cupid_cable_run(struct CupidCable* cable, uint32_t clocks) {
    if (!cable || cable->running || clocks > INT32_MAX || UINT64_MAX - cable->now < clocks) return false;
    const uint64_t end = cable->now + clocks;
    cable->running = true;
    for (;;) {
        dispatch_events(cable);
        /* A core may request several zero-clock boundaries at this instant.
         * Finish them in player order before choosing the next clock. */
        for (unsigned i = 0; i < cable->count; ++i) {
            struct CableNode* node = &cable->nodes[i];
            while (!node->gba->stopped && ((!node->idle && node->target <= cable->now) || node->idle)) {
                cupid_context_resume(node->context);
                dispatch_events(cable);
            }
        }
        if (cable->now == end) break;
        uint64_t next = end;
        for (unsigned i = 0; i < cable->count; ++i) {
            struct CableNode* node = &cable->nodes[i];
            if (node->gba->stopped) continue;
            if (node->target < next) next = node->target;
            int32_t until = cupid_timing_next_event(&node->gba->timing);
            if (until >= 0 && (uint32_t)until < next - cable->now) next = cable->now + (uint32_t)until;
        }
        uint32_t elapsed = (uint32_t)(next - cable->now);
        for (unsigned i = 0; i < cable->count; ++i) {
            struct CableNode* node = &cable->nodes[i];
            if (!node->gba->stopped) cupid_timing_advance_clock(&node->gba->timing, elapsed);
            else if (!node->idle) node->target += elapsed;
        }
        cable->now = next;
    }
    cable->running = false;
    return true;
}

uint64_t cupid_cable_time(const struct CupidCable* cable) { return cable->now; }
bool cupid_cable_connected(const struct CupidCable* cable) { return cable->connected; }
