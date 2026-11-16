/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file      card-reader.h
 * @author    cupidthecat
 * @brief     Card-reader camera registers, scan buffers and queued optical input.
 * @date      2026-11-16
 *
 * - Retain cartridge-owned input independently of camera bus reset.
 * - Expose the serial camera and memory-mapped scanline interfaces.
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
#ifndef CUPID_CARD_READER_H
#define CUPID_CARD_READER_H

#include "cupid/timing.h"
#include "cupid/types.h"

enum {
    CUPID_CARD_STRIDE = 1420,
    CUPID_CARD_HEIGHT = 40,
    CUPID_CARD_DOTS = CUPID_CARD_STRIDE * CUPID_CARD_HEIGHT,
    CUPID_CARD_QUEUE = 16,
    CUPID_CARD_SENSOR_WORDS = 69
};

struct CupidGBA;

struct CupidCardReader {
    uint16_t sensor[CUPID_CARD_SENSOR_WORDS];
    uint16_t output, reset, led;
    uint8_t control[2], camera[128], intensity[48];
    uint8_t serialState, serialBits, serialByte, command, index;
    int scanX;
    unsigned scanY;
    uint8_t *dots, *queued[CUPID_CARD_QUEUE];
    unsigned queueHead, queueCount;
    bool calibrationChecked;
    struct CupidTimingEvent irqEvent;
    uint64_t irqClock;
    uint32_t irqRemaining; /**< Quarter-PHI exposure units, retained while clock-gated. */
    unsigned irqRate;
    bool irqActive;
};

void cupid_card_reader_init(struct CupidCardReader *reader);
void cupid_card_reader_deinit(struct CupidCardReader *reader);
void cupid_card_reader_reset(struct CupidCardReader *reader);
/* Queue owns a decoded copy. A failed conversion or full queue changes no state. */
bool cupid_card_reader_queue(struct CupidCardReader *reader, const void *data, size_t size);
bool cupid_card_reader_read(struct CupidCardReader *reader, uint32_t address, uint16_t *value);
bool cupid_card_reader_write(struct CupidCardReader *reader, uint32_t address, uint16_t value);
bool cupid_card_reader_read_port(struct CupidCardReader *reader, uint32_t address, uint8_t *value);
bool cupid_card_reader_write_port(struct CupidGBA *gba, uint32_t address, uint8_t value);
void cupid_card_reader_calibrate(struct CupidGBA *gba);
void cupid_card_reader_update_clock(struct CupidGBA *gba);

#endif
