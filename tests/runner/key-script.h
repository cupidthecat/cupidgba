/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file key-script.h
 * @brief Frame-indexed button changes loaded before headless guest execution.
 */
#ifndef CUPID_RUNNER_KEY_SCRIPT_H
#define CUPID_RUNNER_KEY_SCRIPT_H

#include <stdbool.h>
#include <stdint.h>

struct CupidKeyEvent {
    uint64_t frame;
    uint16_t keys;
};

/** Load a complete, ordered script. The caller frees events after success.
 * Failure leaves events NULL and count zero. Empty scripts are valid. */
bool cupid_runner_read_keys(const char *path, struct CupidKeyEvent **events, unsigned *count);

#endif
