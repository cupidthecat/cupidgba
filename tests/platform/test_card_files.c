/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_card_files.c
 * @brief Atomic card-file staging, multi-strip limits and queue ownership transfer.
 */
#include "../test.h"
#include "cupid/file.h"
#include "cupid/gba.h"
#include "cupid/platform/peripherals.h"

static void path(char *output, size_t capacity, const char *directory, const char *name) {
    int count = snprintf(output, capacity, "%s/%s", directory, name);
    CHECK(count > 0 && (size_t)count < capacity);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    char raw[1024], binary[1024], bad[1024], missing[1024], large[1024];
    path(raw, sizeof(raw), argv[1], "owned multiple strips.raw");
    path(binary, sizeof(binary), argv[1], "owned data.bin");
    path(bad, sizeof(bad), argv[1], "unsupported.bin");
    path(missing, sizeof(missing), argv[1], "never-created.card");
    path(large, sizeof(large), argv[1], "oversized.bin");
    uint8_t *bytes = calloc(1, 16 * 2912 + 1);
    CHECK(bytes);
    for (unsigned strip = 0; strip < 16; strip++)
        bytes[strip * 2912 + 1] = 3;
    CHECK(cupid_file_replace(raw, bytes, 16 * 2912));
    CHECK(cupid_file_replace(large, bytes, 16 * 2912 + 1));
    CHECK(cupid_file_replace(bad, "bad", 3));
    struct CupidCardReader input;
    cupid_card_reader_init(&input);
    const char *paths[] = {raw};
    CHECK(cupid_platform_cards_read(&input, paths, 1));
    CHECK(input.queueCount == 16);
    struct CupidCardReader before = input;
    uint8_t first[CUPID_CARD_DOTS];
    memcpy(first, input.queued[0], sizeof(first));
    const char *invalid[] = {bad, missing, large, argv[1]};
    for (unsigned i = 0; i < 4; i++) {
        const char *failure[] = {raw, invalid[i]};
        CHECK(!cupid_platform_cards_read(&input, failure, 2));
        CHECK(!memcmp(&input, &before, sizeof(input)));
        CHECK(!memcmp(first, input.queued[0], sizeof(first)));
    }
    const char *overflow[] = {raw, raw};
    CHECK(!cupid_platform_cards_read(&input, overflow, 2));
    CHECK(!cupid_platform_cards_read(&input, NULL, 1));
    CHECK(!cupid_platform_cards_read(&input, paths, 17));
    CHECK(!memcmp(&input, &before, sizeof(input)));
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    CHECK(!cupid_platform_cards_attach(gba, &input));
    gba->memory.rom = calloc(1, 256);
    CHECK(gba->memory.rom);
    gba->memory.romSize = 256;
    gba->memory.romMask = 255;
    cupid_cartridge_enable_card_reader(gba, true);
    CHECK(cupid_platform_cards_attach(gba, &input));
    CHECK(input.queueCount == 0 && gba->cartridge.cardReader.queueCount == 16);
    CHECK(gba->cartridge.cardReader.queued[0] == before.queued[0]);
    CHECK(!cupid_load_rom(gba, missing));
    CHECK(gba->cartridge.cardReader.queueCount == 16 &&
          gba->cartridge.cardReader.queued[0] == before.queued[0]);
    memset(bytes, 0, 2688);
    for (unsigned strip = 0; strip < 2; strip++) {
        bytes[strip * 1344 + 1] = 0x30;
        bytes[strip * 1344 + 14] = 2;
    }
    CHECK(cupid_file_replace(binary, bytes, 2688));
    const char *dataPaths[] = {binary};
    CHECK(cupid_platform_cards_read(&input, dataPaths, 1));
    CHECK(input.queueCount == 2);
    before = input;
    CHECK(!cupid_platform_cards_attach(gba, &input));
    CHECK(!memcmp(&input, &before, sizeof(input)));
    cupid_cartridge_enable_card_reader(gba, false);
    CHECK(!cupid_platform_cards_attach(gba, &input));
    cupid_cartridge_enable_card_reader(gba, true);
    CHECK(cupid_platform_cards_attach(gba, &input));
    CHECK(input.queueCount == 0 && gba->cartridge.cardReader.queueCount == 2);
    /* The staged copy stays usable after its source file has changed. */
    CHECK(cupid_file_replace(binary, "changed", 7));
    CHECK(gba->cartridge.cardReader.queued[0][201] == 1);
    CHECK(cupid_load_rom(gba, raw));
    CHECK(!gba->cartridge.cardReaderPresent && !gba->cartridge.cardReader.queueCount &&
          !gba->cartridge.cardReader.dots);
    cupid_card_reader_deinit(&input);
    cupid_gba_destroy(gba);
    free(gba);
    free(bytes);
    puts("PASS multi-strip file limits, atomic failures, immutable copies and queue ownership");
    return 0;
}
