/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file key-script.h
 * @brief Frame-indexed input scripts shared by the SDL2 frontend and runner.
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
