/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      detection.h
 * @author    cupidthecat
 * @brief     Internal backup detection and image-preparation interface.
 * @date      2026-11-16
 *
 * - Declare preparation of a fresh backup device for a candidate image.
 * - Keep image preparation separate from public save-file operations.
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
#ifndef CUPID_SAVE_DETECTION_INTERNAL_H
#define CUPID_SAVE_DETECTION_INTERNAL_H

#include "cupid/memory.h"

/* Prepare a fresh, unscheduled backup device without modifying the attached one.
 * The caller relocates readyEvent.context before publishing the result. */
bool cupid_savedata_prepare_image(struct CupidSavedata *save, const uint8_t *rom, size_t size);

#endif
