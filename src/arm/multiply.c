/**
 * @file      multiply.c
 * @author    cupidthecat
 * @brief     ARM7TDMI multiply carry from the multiplier's finite-width adder stages.
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
#include "multiply.h"

bool cupid_arm_multiply_carry(uint32_t multiplicand, uint32_t multiplier, uint64_t accumulator,
                              enum CupidMultiplyMode mode) {
    const bool signedOperands = mode != CUPID_MULTIPLY_UNSIGNED_LONG;
    unsigned cycles = 1;
    uint32_t upper = multiplier >> 8;
    uint32_t remaining = UINT32_C(0xFFFFFF);
    /* The array consumes one byte per internal cycle. Short multiplication
     * uses the signed termination rule even though its result is truncated. */
    while (cycles < 4 && upper && (!signedOperands || upper != remaining)) {
        ++cycles;
        upper >>= 8;
        remaining >>= 8;
    }
    if (mode == CUPID_MULTIPLY_SHORT && cycles == 4) {
        /* The final low-word shifter bit is the last Booth negation input. */
        return (multiplier >> 30) == 2;
    }

    const uint64_t portMask = (UINT64_C(1) << 33) - 1;
    uint64_t operand = multiplicand;
    uint64_t digits = multiplier;
    if (signedOperands) {
        if (multiplicand >> 31)
            operand |= UINT64_C(3) << 32;
        if (multiplier >> 31)
            digits |= UINT64_C(3) << 32;
    }
    /* The initial multiplier bit occupies the carry input before the four
     * two-bit stages run. The result's first bit has already shifted out. */
    uint64_t sum = (accumulator >> 1) & portMask;
    uint64_t carry = multiplier & 1 ? (~operand >> 1) & portMask : 0;
    uint64_t accumulatorHigh = accumulator >> 34;
    for (unsigned stage = 0; stage < 4 * cycles; ++stage) {
        const unsigned group = (unsigned)(digits >> (2 * stage)) & 7;
        const int factor = (int)(group & 1) + (int)((group >> 1) & 1) - 2 * (int)(group >> 2);
        uint64_t addend = factor == 0 ? 0 : operand << (factor == 2 || factor == -2);
        if (factor < 0)
            addend = ~addend;
        const uint64_t input = addend & portMask;
        const uint64_t parity = sum ^ carry ^ input;
        const uint64_t majority = (sum & carry) | (sum & input) | (carry & input);
        /* Two result bits leave each stage. The upper accumulator bits enter
         * beside the complemented signs of the 33-bit carry and 34-bit addend.
         * Their compensating high bit enters the next carry port separately. */
        const uint64_t top =
            (accumulatorHigh & 1) + ((carry >> 32) ^ 1) + (((addend >> 33) & 1) ^ 1);
        sum = (parity >> 2) | (top << 31);
        /* Booth's +1 for a negative digit leaves with the discarded low bits. */
        carry = (majority >> 1) | (((accumulatorHigh >> 1 & 1) ^ 1) << 32);
        accumulatorHigh >>= 2;
    }
    /* Early termination exposes the low-result shifter bit. A complete long
     * multiply instead exposes bit 63 after the final high-result shift. */
    return (carry >> (cycles == 4 ? 30 : 30 - 8 * cycles)) & 1;
}
