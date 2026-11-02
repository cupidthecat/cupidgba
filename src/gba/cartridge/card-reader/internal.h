/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file internal.h
 * @brief Internal card format conversion and camera scan helpers.
 */
#ifndef CUPID_CARD_READER_INTERNAL_H
#define CUPID_CARD_READER_INTERNAL_H

#include "cupid/card-reader.h"

bool cupid_card_decode(const uint8_t *data, size_t size, uint8_t dots[CUPID_CARD_DOTS]);
void cupid_card_parity(const uint8_t *data, size_t size, uint8_t parity[16]);

#endif
