/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CUPID_CABLE_H
#define CUPID_CABLE_H
#include "cupid/gba.h"

struct CupidCable;

/* A session borrows two to four initialized cores and owns their execution on
 * the calling thread. Each core's current clock becomes its session epoch.
 * Serial callbacks and clock coordinators must be unassigned, and a standalone
 * core must have finished its current step before joining. Do not run,
 * reset or destroy a borrowed core outside the session. Calls return with
 * CPUs suspended at clock requests; memory, video and input remain accessible.
 * Destroying the session abandons those in-flight instructions: reset a core
 * before reusing it. Save-file flushing can precede session destruction. */
struct CupidCable* cupid_cable_create(struct CupidGBA* const cores[], unsigned count);
void cupid_cable_destroy(struct CupidCable* cable);

/* Run exactly this many session clocks, dispatching all peripheral events at
 * the final clock before returning. STOP freezes only that console's clock.
 * No CPU phase crosses an earlier peer output or peripheral event. */
bool cupid_cable_run(struct CupidCable* cable, uint32_t clocks);
uint64_t cupid_cable_time(const struct CupidCable* cable);

/* Change connection at the current session clock. A pair cross-connects SO/SI
 * and shares SC/SD in normal/GPIO modes; UART connects peer SD to SC. Multiplayer
 * uses the core's connected-word model for up to four players. Disconnecting
 * releases input pins and cancels multiplayer frames. This is a logical local
 * connection, not the physical multiplayer cable's one-way normal wiring. */
void cupid_cable_connect(struct CupidCable* cable, bool connected);
bool cupid_cable_connected(const struct CupidCable* cable);

/* Observe a complete image at the core's existing VBlank handoff, even while
 * its CPU is halted or inside an instruction. Pixels are borrowed for this
 * callback; copy them to retain a frame. Timestamp is session time. Install
 * while paused, before running. The callback must not modify cores/session or
 * advance emulated time. STOP retains the last completed image. */
void cupid_cable_set_frame_callback(struct CupidCable* cable,
    void (*callback)(void* context, unsigned player, const uint32_t* pixels, uint64_t timestamp), void* context);
#endif
