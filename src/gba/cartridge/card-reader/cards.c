/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file cards.c
 * @brief Deterministic card strip conversion, error information and dot placement.
 *
 * - Convert raw blocks, full/reduced data fragments and packed dot images.
 * - Generate address bars and Reed-Solomon parity over the card's finite field.
 * - Validate monochrome BMP bounds before touching the output grid.
 */
#include "internal.h"

static uint8_t multiply(uint8_t a, uint8_t b) {
    unsigned result = 0, factor = a;
    for (unsigned bit = 0; bit < 8; bit++) {
        if (b & (1u << bit))
            result ^= factor;
        factor <<= 1;
        if (factor & 256)
            factor ^= 0x187;
    }
    return (uint8_t)result;
}

static uint8_t power(unsigned exponent) {
    uint8_t value = 1;
    for (unsigned i = 0; i < exponent; i++)
        value = multiply(value, 2);
    return value;
}

void cupid_card_parity(const uint8_t *data, size_t size, uint8_t parity[16]) {
    uint8_t generator[16] = {0}, remainder[16] = {0};
    generator[0] = power(120);
    for (unsigned degree = 1; degree < 16; degree++) {
        generator[degree] = 1;
        const uint8_t root = power(120 + degree);
        for (int term = (int)degree; term >= 0; term--)
            generator[term] = (term ? generator[term - 1] : 0) ^ multiply(generator[term], root);
    }
    for (size_t i = 0; i < size; i++) {
        const uint8_t feedback = data[i] ^ remainder[15];
        for (int term = 15; term >= 0; term--)
            remainder[term] =
                (term ? remainder[term - 1] : 0) ^ multiply(feedback, generator[term]);
    }
    for (unsigned i = 0; i < 16; i++)
        parity[i] = (uint8_t)~remainder[15 - i];
}

static uint16_t address_code(unsigned address) {
    uint16_t value = 0x3FF;
    for (unsigned i = 1; i <= address; i++) {
        value ^= (uint16_t)((i & (0u - i)) * 0x769);
        if (!(i & 7))
            value ^= 0x769;
        if (!(i & 15))
            value ^= 0xED2;
        if (!(i & 31))
            value ^= 0x1DA4 ^ 0x769;
    }
    return value;
}

static void dot(uint8_t *dots, unsigned x, unsigned y, unsigned value) {
    dots[y * CUPID_CARD_STRIDE + x] = (uint8_t)value;
}

static void anchors(uint8_t *dots, unsigned x, unsigned address) {
    const uint16_t code = address_code(address);
    for (unsigned y = 0; y < 5; y++)
        for (unsigned column = 0; column < 5; column++)
            if ((y != 0 && y != 4) || (column != 0 && column != 4)) {
                dot(dots, x + column, y, 1);
                dot(dots, x + column, y + 35, 1);
            }
    dot(dots, x + 2, 7, 1);
    for (unsigned bit = 0; bit < 16; bit++)
        dot(dots, x + 2, 16 + bit, (code >> (15 - bit)) & 1);
}

static void block_dots(uint8_t *dots, unsigned x, const uint8_t block[104]) {
    static const uint8_t codes[16] = {0, 1, 2, 18, 4, 5, 6, 22, 8, 9, 10, 20, 12, 13, 17, 16};
    for (unsigned y = 2; y <= 37; y += 35)
        for (unsigned column = 8; column <= 31; column++)
            if (column <= 18 ? !(column & 1) : (column > 20 && (column & 1)))
                dot(dots, x + column, y, 1);
    unsigned bit = 0;
    for (unsigned y = 4; y < 36; y++) {
        const bool shortRow = y < 7 || y >= 33;
        const unsigned left = shortRow ? 7 : 3, width = shortRow ? 26 : 34;
        for (unsigned column = 0; column < width; column++, bit++) {
            const unsigned nibble = (block[bit / 10] >> (bit % 10 < 5 ? 4 : 0)) & 15;
            dot(dots, x + left + column, y, (codes[nibble] >> (4 - bit % 5)) & 1);
        }
    }
}

static void reduced_header(const uint8_t *input, unsigned fragments, uint8_t data[44][64]) {
    uint8_t *header = data[0];
    header[1] = 0x30;
    header[2] = 1;
    header[3] = fragments == 28 ? 1 : 2;
    header[5] = 1;
    header[6] = fragments == 28 ? 5 : 8;
    header[7] = 0x10;
    header[10] = 0x10;
    header[11] = fragments == 28 ? 0x13 : 0x12;
    header[12] = input[1];
    header[13] = input[0];
    header[14] = fragments == 28 ? 2 : 1;
    memcpy(header + 16, input + 2, 2);
    header[18] = 0x10;
    header[21] = 0x19;
    header[25] = 8;
    memcpy(header + 26, "NINTENDO", 8);
    header[35] = 0x22;
    header[37] = 9;
    memcpy(header + 38, input + 4, 8);
    for (unsigned i = 0; i < 12; i++)
        header[46] ^= input[i];
    unsigned sum = 0, global = 0;
    for (unsigned fragment = 1; fragment < fragments; fragment++) {
        memcpy(data[fragment], input + 12 + (fragment - 1) * 48, 48);
        unsigned checksum = 0;
        for (unsigned byte = 0; byte < 48; byte += 2) {
            sum += ((unsigned)data[fragment][byte] << 8) | data[fragment][byte + 1];
            checksum ^= data[fragment][byte] ^ data[fragment][byte + 1];
        }
        global += checksum;
    }
    header[19] = (uint8_t)(~sum >> 8);
    header[20] = (uint8_t)~sum;
    for (unsigned byte = 0; byte < 47; byte++)
        global += header[byte];
    header[47] = (uint8_t)~global;
}

static unsigned little16(const uint8_t *data) {
    return data[0] | (unsigned)data[1] << 8;
}

static uint32_t little32(const uint8_t *data) {
    return little16(data) | (uint32_t)little16(data + 2) << 16;
}

static bool bitmap(const uint8_t *data, size_t size, uint8_t *dots) {
    if (size < 62 || little32(data + 14) != 40 || little16(data + 26) != 1 ||
        little16(data + 28) != 1 || little32(data + 30) != 0)
        return false;
    const unsigned width = little32(data + 18);
    const uint32_t height = little32(data + 22);
    if ((width != 639 && width != 989) || (height != 44 && height != UINT32_C(0xFFFFFFD4)))
        return false;
    const size_t stride = ((width + 31) / 32) * 4;
    const size_t offset = little32(data + 10);
    if (offset < 62 || offset > size || 44 * stride > size - offset || little32(data + 2) != size)
        return false;
    const unsigned first = data[54] + data[55] + data[56];
    const unsigned second = data[58] + data[59] + data[60];
    if (first == second)
        return false;
    const unsigned ink = first < second ? 0 : 1;
    memset(dots, 0, CUPID_CARD_DOTS);
    for (unsigned y = 0; y < 40; y++) {
        const unsigned row = height == 44 ? 43 - (y + 2) : y + 2;
        const uint8_t *line = data + offset + row * stride;
        for (unsigned x = 0; x < width - 4; x++)
            dot(dots, 200 + x, y, ((line[(x + 2) / 8] >> (7 - (x + 2) % 8)) & 1) == ink);
    }
    return true;
}

bool cupid_card_decode(const uint8_t *input, size_t size, uint8_t dots[CUPID_CARD_DOTS]) {
    if (!input || !dots)
        return false;
    const bool packed = size == 3520 || size == 5456;
    const bool raw = size == 1872 || size == 2912;
    const bool reduced = size == 1308 || size == 2076;
    if (!packed && !raw && !reduced && size != 1344 && size != 2112) {
        if (size >= 2 && input[0] == 'B' && input[1] == 'M')
            return bitmap(input, size, dots);
        return false;
    }
    memset(dots, 0, CUPID_CARD_DOTS);
    if (packed) {
        const unsigned stride = size == 3520 ? 80 : 124;
        const unsigned width = size == 3520 ? 640 : 987;
        for (unsigned y = 0; y < 40; y++)
            for (unsigned x = 0; x < width; x++)
                dot(dots, 200 + x, y, (input[(y + 2) * stride + x / 8] >> (7 - x % 8)) & 1);
        return true;
    }
    const unsigned blocks = (size == 1872 || size == 1344 || size == 1308) ? 18 : 28;
    const unsigned fragments = blocks == 18 ? 28 : 44;
    uint8_t data[44][64] = {{0}}, header[24] = {0};
    if (!raw) {
        if (reduced)
            reduced_header(input, fragments, data);
        else
            for (unsigned fragment = 0; fragment < fragments; fragment++)
                memcpy(data[fragment], input + fragment * 48, 48);
        for (unsigned fragment = 0; fragment < fragments; fragment++)
            cupid_card_parity(data[fragment], 48, data[fragment] + 48);
        header[1] = blocks == 18 ? 2 : 3;
        header[3] = blocks == 18 ? 1 : 25;
        header[4] = 64;
        header[5] = 16;
        header[7] = (uint8_t)fragments;
        cupid_card_parity(header, 8, header + 8);
    }
    for (unsigned block = 0; block <= blocks; block++)
        anchors(dots, 200 + block * 35, (blocks == 18 ? 1 : 25) + block);
    for (unsigned block = 0; block < blocks; block++) {
        uint8_t bytes[104] = {0};
        if (raw)
            memcpy(bytes, input + block * 104, sizeof(bytes));
        else {
            bytes[0] = header[(block * 2) % 24];
            bytes[1] = header[(block * 2) % 24 + 1];
            for (unsigned byte = 2; byte < 104; byte++) {
                const unsigned interleaved = block * 102 + byte - 2;
                if (interleaved / fragments < 64)
                    bytes[byte] = data[interleaved % fragments][interleaved / fragments];
            }
        }
        block_dots(dots, 200 + block * 35, bytes);
    }
    return true;
}
