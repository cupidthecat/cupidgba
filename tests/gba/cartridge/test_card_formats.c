/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_card_formats.c
 * @brief Owned card block, fragment, bitmap and malformed-input conversion controls.
 */
#include "../../../src/gba/cartridge/card-reader/internal.h"
#include "../../test.h"

static uint8_t field_product(uint8_t a, uint8_t b) {
    unsigned result = 0;
    while (b) {
        if (b & 1)
            result ^= a;
        const bool carry = (a & 128) != 0;
        a <<= 1;
        if (carry)
            a ^= 0x87;
        b >>= 1;
    }
    return (uint8_t)result;
}

static void syndromes(const uint8_t fragment[64]) {
    uint8_t root = 1;
    for (unsigned i = 0; i < 120; i++)
        root = field_product(root, 2);
    for (unsigned check = 0; check < 16; check++) {
        uint8_t sum = 0, term = 1;
        for (unsigned byte = 0; byte < 64; byte++) {
            const uint8_t coefficient = fragment[63 - byte] ^ (byte < 16 ? 255 : 0);
            sum ^= field_product(coefficient, term);
            term = field_product(term, root);
        }
        CHECK(sum == 0);
        root = field_product(root, 2);
    }
}

static unsigned nibble(unsigned bits) {
    static const uint8_t codes[] = {0, 1, 2, 18, 4, 5, 6, 22, 8, 9, 10, 20, 12, 13, 17, 16};
    for (unsigned i = 0; i < 16; i++)
        if (codes[i] == bits)
            return i;
    CHECK(false);
    return 0;
}

static void extract(const uint8_t *dots, unsigned block, uint8_t bytes[104]) {
    unsigned bit = 0, shift = 0, byte = 0;
    for (unsigned y = 4; y < 36; y++) {
        const unsigned start = y < 7 || y > 32 ? 7 : 3;
        const unsigned width = y < 7 || y > 32 ? 26 : 34;
        for (unsigned x = 0; x < width; x++) {
            shift = (shift << 1) | dots[y * CUPID_CARD_STRIDE + 200 + block * 35 + start + x];
            if (++bit % 5 == 0) {
                byte = (byte << 4) | nibble(shift);
                shift = 0;
                if (bit % 10 == 0) {
                    bytes[bit / 10 - 1] = (uint8_t)byte;
                    byte = 0;
                }
            }
        }
    }
    CHECK(bit == 1040);
}

static uint16_t address_bar(const uint8_t *dots, unsigned column) {
    uint16_t value = 0;
    for (unsigned y = 16; y < 32; y++)
        value = (uint16_t)((value << 1) | dots[y * CUPID_CARD_STRIDE + column + 2]);
    return value;
}

static void raw(void) {
    uint8_t input[2912], dots[CUPID_CARD_DOTS], output[104];
    for (unsigned large = 0; large < 2; large++)
        for (unsigned seed = 0; seed < 4; seed++) {
            const unsigned count = large ? 28 : 18, size = count * 104;
            for (unsigned i = 0; i < size; i++)
                input[i] = (uint8_t)(i * 37 + seed);
            CHECK(cupid_card_decode(input, size, dots));
            for (unsigned block = 0; block < count; block++) {
                extract(dots, block, output);
                CHECK(!memcmp(output, input + block * 104, 104));
            }
            CHECK(address_bar(dots, 200) == (large ? 26137 : 1174));
            CHECK(address_bar(dots, 200 + count * 35) == (large ? 54679 : 20007));
            for (unsigned y = 0; y < 40; y++)
                for (unsigned x = 0; x < CUPID_CARD_STRIDE; x++)
                    if (x < 200 || x >= 200 + count * 35 + 5)
                        CHECK(dots[y * CUPID_CARD_STRIDE + x] == 0);
        }
}

static void fragments(void) {
    static const uint8_t headers[2][24] = {
        {0,    2,    0,    1,    64,   16,   0,    28,   0x10, 0x6F, 0x40, 0xDA,
         0x39, 0x25, 0x8E, 0xE0, 0x7B, 0xB5, 0x98, 0xB6, 0x5B, 0xCF, 0x7F, 0x72},
        {0,    3,    0,    25,   64,   16,   0,    44,   0x0E, 0x88, 0xED, 0x82,
         0x50, 0x67, 0xFB, 0xD1, 0x43, 0xEE, 0x03, 0xC6, 0xC6, 0x2B, 0x2C, 0x93}};
    uint8_t input[2112], dots[CUPID_CARD_DOTS], decoded[44][64], bytes[104];
    for (unsigned large = 0; large < 2; large++)
        for (unsigned reduced = 0; reduced < 2; reduced++) {
            const unsigned count = large ? 44 : 28, blocks = large ? 28 : 18;
            const unsigned size = count * 48 - (reduced ? 36 : 0);
            for (unsigned i = 0; i < size; i++)
                input[i] = (uint8_t)(i * 37 + 91);
            memset(dots, 0xA5, sizeof(dots));
            CHECK(cupid_card_decode(input, size, dots));
            memset(decoded, 0, sizeof(decoded));
            for (unsigned block = 0; block < blocks; block++) {
                extract(dots, block, bytes);
                CHECK(bytes[0] == headers[large][block * 2 % 24]);
                CHECK(bytes[1] == headers[large][block * 2 % 24 + 1]);
                for (unsigned byte = 2; byte < 104; byte++) {
                    const unsigned index = block * 102 + byte - 2;
                    if (index < count * 64)
                        decoded[index % count][index / count] = bytes[byte];
                    else
                        CHECK(bytes[byte] == 0);
                }
            }
            for (unsigned fragment = 0; fragment < count; fragment++) {
                if (!reduced)
                    CHECK(!memcmp(decoded[fragment], input + fragment * 48, 48));
                else if (fragment)
                    CHECK(!memcmp(decoded[fragment], input + 12 + (fragment - 1) * 48, 48));
                syndromes(decoded[fragment]);
            }
            if (reduced) {
                CHECK(decoded[0][12] == input[1] && decoded[0][13] == input[0]);
                CHECK(decoded[0][14] == (large ? 1 : 2));
                CHECK(!memcmp(decoded[0] + 26, "NINTENDO", 8));
                CHECK(!memcmp(decoded[0] + 38, input + 4, 8));
                unsigned sum = 0, checksum = 0, global = 0;
                for (unsigned fragment = 1; fragment < count; fragment++) {
                    unsigned byteXor = 0;
                    for (unsigned byte = 0; byte < 48; byte += 2) {
                        sum +=
                            ((unsigned)decoded[fragment][byte] << 8) | decoded[fragment][byte + 1];
                        byteXor ^= decoded[fragment][byte] ^ decoded[fragment][byte + 1];
                    }
                    global += byteXor;
                }
                for (unsigned byte = 0; byte < 12; byte++)
                    checksum ^= input[byte];
                CHECK(decoded[0][46] == checksum);
                CHECK((((unsigned)decoded[0][19] << 8) | decoded[0][20]) == ((~sum) & 0xFFFF));
                for (unsigned byte = 0; byte < 48; byte++)
                    global += decoded[0][byte];
                CHECK((global & 255) == 255);
            }
        }
}

static void word(uint8_t *data, unsigned offset, uint32_t value) {
    for (unsigned byte = 0; byte < 4; byte++)
        data[offset + byte] = (uint8_t)(value >> (byte * 8));
}

static void bitmap(void) {
    uint8_t rawInput[2912], expected[CUPID_CARD_DOTS], result[CUPID_CARD_DOTS], image[5518];
    for (unsigned i = 0; i < sizeof(rawInput); i++)
        rawInput[i] = (uint8_t)(i * 13);
    for (unsigned large = 0; large < 2; large++) {
        CHECK(cupid_card_decode(rawInput, large ? 2912 : 1872, expected));
        const unsigned width = large ? 989 : 639, stride = large ? 124 : 80;
        const unsigned size = 62 + 44 * stride;
        for (unsigned reverse = 0; reverse < 2; reverse++)
            for (unsigned palette = 0; palette < 2; palette++) {
                memset(image, 0, sizeof(image));
                image[0] = 'B';
                image[1] = 'M';
                word(image, 2, size);
                word(image, 10, 62);
                word(image, 14, 40);
                word(image, 18, width);
                word(image, 22, reverse ? UINT32_C(0xFFFFFFD4) : 44);
                image[26] = image[28] = 1;
                memset(image + (palette ? 54 : 58), 255, 3);
                for (unsigned y = 0; y < 44; y++)
                    for (unsigned x = 0; x < width; x++) {
                        const unsigned ink =
                            x >= 2 && x < width - 2 && y >= 2 && y < 42
                                ? expected[(y - 2) * CUPID_CARD_STRIDE + 200 + x - 2]
                                : 0;
                        const unsigned index = ink ? palette : !palette;
                        const unsigned row = reverse ? y : 43 - y;
                        image[62 + row * stride + x / 8] |= (uint8_t)(index << (7 - x % 8));
                    }
                CHECK(cupid_card_decode(image, size, result));
                CHECK(!memcmp(expected, result, sizeof(result)));
                const uint32_t offsets[] = {0, 61, size, UINT32_MAX};
                for (unsigned i = 0; i < 4; i++) {
                    word(image, 10, offsets[i]);
                    memset(result, 0xA5, sizeof(result));
                    CHECK(!cupid_card_decode(image, size, result));
                    for (unsigned byte = 0; byte < sizeof(result); byte++)
                        CHECK(result[byte] == 0xA5);
                }
            }
    }
}

static void malformed(void) {
    uint8_t input[6000] = {0}, output[CUPID_CARD_DOTS];
    memset(output, 0xA5, sizeof(output));
    for (unsigned size = 0; size < sizeof(input); size++) {
        if (size == 1308 || size == 1344 || size == 1872 || size == 2076 || size == 2112 ||
            size == 2912 || size == 3520 || size == 5456)
            continue;
        CHECK(!cupid_card_decode(input, size, output));
    }
    CHECK(!cupid_card_decode(NULL, 1872, output));
    CHECK(!cupid_card_decode(input, 1872, NULL));
    for (unsigned byte = 0; byte < sizeof(output); byte++)
        CHECK(output[byte] == 0xA5);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    if (!strcmp(argv[1], "raw"))
        RUN(raw);
    else if (!strcmp(argv[1], "fragments"))
        RUN(fragments);
    else if (!strcmp(argv[1], "bitmap"))
        RUN(bitmap);
    else if (!strcmp(argv[1], "malformed"))
        RUN(malformed);
    else
        CHECK(false);
    return 0;
}
