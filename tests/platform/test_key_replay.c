/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      test_key_replay.c
 * @author    cupidthecat
 * @brief     Shared replay ordering, keypad IRQ publication, STOP wake and frame bounds.
 * @date      2026-11-16
 *
 * @copyright Copyright (c) 2026 cupidthecat
 *
 * This file is part of CupidGBA.
 *
 * CupidGBA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * CupidGBA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CupidGBA.  If not, see <https://gnu.org>.
 */
#include "../test.h"
#include "cupid/gba.h"
#include "cupid/platform/key-script.h"

static struct CupidGBA *machine(void) {
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    return gba;
}

static void destroy(struct CupidGBA *gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void order(void) {
    struct CupidGBA *gba = machine();
    const struct CupidKeyEvent events[] = {{0, 4}, {0, 8}, {1, 1}, {1, 16}, {2, 2}};
    struct CupidKeyReplay replay = {.events = events, .count = 5};
    cupid_platform_key_replay_apply(gba, &replay);
    CHECK(gba->keysActive == 8 && replay.next == 2 && replay.frame == 0);
    cupid_platform_key_replay_apply(gba, &replay);
    CHECK(gba->keysActive == 8 && replay.next == 2);
    cupid_io_write(gba, REG_KEYCNT, 0x4001);
    cupid_platform_key_replay_advance(gba, &replay);
    CHECK(gba->keysActive == 16 && replay.frame == 1 && replay.next == 4);
    CHECK((gba->irq.pendingIF | gba->irqf) & IRQ_KEYPAD);
    CHECK(cupid_io_read(gba, REG_KEYINPUT) == 0x3EF);
    cupid_platform_key_replay_advance(gba, &replay);
    CHECK(gba->keysActive == 2 && replay.next == 5);
    cupid_platform_key_replay_advance(gba, &replay);
    CHECK(gba->keysActive == 2 && replay.frame == 3 && replay.next == 5);
    destroy(gba);
}

static void stop(void) {
    struct CupidGBA *gba = machine();
    const struct CupidKeyEvent events[] = {{0, 16}};
    struct CupidKeyReplay replay = {.events = events, .count = 1};
    cupid_io_write(gba, REG_KEYCNT, 0x4010);
    gba->ie = IRQ_KEYPAD;
    cupid_gba_stop(gba);
    CHECK(gba->stopped);
    const uint64_t clock = cupid_timing_global_time(&gba->timing);
    /* Applying an available host change uses the ordinary external wake path.
     * Advancing stopped time alone does not create a completed video frame. */
    CHECK(cupid_gba_advance_stopped_time(gba, VIDEO_TOTAL_LENGTH) == VIDEO_TOTAL_LENGTH);
    CHECK(replay.frame == 0 && replay.next == 0 && gba->stopped);
    cupid_platform_key_replay_apply(gba, &replay);
    CHECK(!gba->stopped && gba->keysActive == 16);
    CHECK(cupid_timing_global_time(&gba->timing) == clock);
    CHECK((gba->irq.pendingIF | gba->irqf) & IRQ_KEYPAD);
    destroy(gba);
}

static void bounds(void) {
    struct CupidGBA *gba = machine();
    struct CupidKeyReplay empty = {0};
    cupid_platform_key_replay_apply(gba, &empty);
    cupid_platform_key_replay_advance(gba, &empty);
    CHECK(gba->keysActive == 0 && empty.next == 0 && empty.frame == 1);
    const struct CupidKeyEvent events[] = {{UINT64_MAX, 0x3FF}, {UINT64_MAX, 0x155}};
    struct CupidKeyReplay replay = {.events = events, .count = 2, .frame = UINT64_MAX - 1};
    cupid_platform_key_replay_apply(gba, &replay);
    CHECK(gba->keysActive == 0 && replay.next == 0);
    cupid_platform_key_replay_advance(gba, &replay);
    CHECK(gba->keysActive == 0x155 && replay.next == 2 && replay.frame == UINT64_MAX);
    cupid_platform_key_replay_advance(gba, &replay);
    CHECK(gba->keysActive == 0x155 && replay.frame == UINT64_MAX && replay.next == 2);
    destroy(gba);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "order"))
        RUN(order);
    else if (!strcmp(argv[1], "stop"))
        RUN(stop);
    else if (!strcmp(argv[1], "bounds"))
        RUN(bounds);
    else
        CHECK(false);
    return 0;
}
