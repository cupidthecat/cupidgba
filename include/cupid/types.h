/**
 * @file types.h
 * @brief Common types and macros used throughout the emulator.
 */

#ifndef CUPID_TYPES_H
#define CUPID_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/** @name Helper Macros */

#define LOAD_32(DEST, OFFSET, SRC) \
    memcpy(&(DEST), (const uint8_t*)(SRC) + (OFFSET), 4)

#define LOAD_16(DEST, OFFSET, SRC) \
    do { uint16_t _load16_tmp; memcpy(&_load16_tmp, (const uint8_t*)(SRC) + (OFFSET), 2); (DEST) = _load16_tmp; } while(0)

#define STORE_32(SRC, OFFSET, DST) \
    memcpy((uint8_t*)(DST) + (OFFSET), &(SRC), 4)

#define STORE_16(SRC, OFFSET, DST) \
    memcpy((uint8_t*)(DST) + (OFFSET), &(SRC), 2)

static inline uint32_t cupid_ror32(uint32_t val, unsigned shift) {
    shift &= 31;
    if (!shift) return val;
    return (val >> shift) | (val << (32 - shift));
}

#define ROR(VAL, SHIFT) cupid_ror32((VAL), (SHIFT))

/** GBA screen dimensions are in video.h */

/** GBA clock = 2^24 Hz (16.78 MHz) */
#define GBA_CLOCK_FREQ     0x1000000u

/** Color: GBA uses BGR555 */
typedef uint16_t Color16;

/** Convert BGR555 → RGBA8888 for SDL */
static inline uint32_t color16_to_rgba(Color16 c) {
    uint32_t r = (c & 0x001F) << 3;
    uint32_t g = ((c >> 5) & 0x1F) << 3;
    uint32_t b = ((c >> 10) & 0x1F) << 3;
    return (r) | (g << 8) | (b << 16) | 0xFF000000u;
}

#endif /* CUPID_TYPES_H */
