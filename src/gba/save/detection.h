/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file detection.h
 * @brief Internal backup detection and image-preparation interface.
 *
 * - Declare preparation of a fresh backup device for a candidate image.
 * - Keep image preparation separate from public save-file operations.
 */
#ifndef CUPID_SAVE_DETECTION_INTERNAL_H
#define CUPID_SAVE_DETECTION_INTERNAL_H

#include "cupid/memory.h"

/* Prepare a fresh, unscheduled backup device without modifying the attached one.
 * The caller relocates readyEvent.context before publishing the result. */
bool cupid_savedata_prepare_image(struct CupidSavedata *save, const uint8_t *rom, size_t size);

#endif
