/**
 * @file      multiply.h
 * @author    cupidthecat
 * @brief     Internal interface for ARM and Thumb multiply carry evaluation.
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
#ifndef CUPID_ARM_MULTIPLY_H
#define CUPID_ARM_MULTIPLY_H

#include <stdbool.h>
#include <stdint.h>

enum CupidMultiplyMode {
    CUPID_MULTIPLY_SHORT,
    CUPID_MULTIPLY_SIGNED_LONG,
    CUPID_MULTIPLY_UNSIGNED_LONG
};

/* Compute C from sampled operands without changing execution clocks or V.
 * The caller supplies zero for non-accumulating instructions. */
bool cupid_arm_multiply_carry(uint32_t multiplicand, uint32_t multiplier, uint64_t accumulator,
                              enum CupidMultiplyMode mode);

#endif
