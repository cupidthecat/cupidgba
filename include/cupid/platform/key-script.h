/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      key-script.h
 * @author    cupidthecat
 * @brief     Frame-indexed input scripts shared by the SDL2 frontend and runner.
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
#ifndef CUPID_PLATFORM_KEY_SCRIPT_H
#define CUPID_PLATFORM_KEY_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

struct CupidKeyEvent {
    uint64_t frame;
    uint16_t keys;
};

/** Load a complete, ordered script. The caller frees events after success.
 * Failure leaves events NULL and count zero. Empty scripts are valid. */
bool cupid_platform_read_key_script(const char *path, struct CupidKeyEvent **events,
                                    unsigned *count);

struct CupidGBA;

struct CupidKeyReplay {
    const struct CupidKeyEvent *events;
    unsigned count, next;
    uint64_t frame;
};

/* Apply every change at the current completed-video-frame index in file order. */
void cupid_platform_key_replay_apply(struct CupidGBA *gba, struct CupidKeyReplay *replay);
/* A completed video frame advances the replay; STOP produces no video frames. */
void cupid_platform_key_replay_advance(struct CupidGBA *gba, struct CupidKeyReplay *replay);

#endif
