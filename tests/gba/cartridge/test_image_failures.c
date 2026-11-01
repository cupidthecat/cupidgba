/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../../src/gba/save/detection.h"
#include "../../test.h"
#include "cupid/file.h"
#include "cupid/gba.h"
#include <errno.h>

enum Failure {
    NONE,
    ROM_ALLOCATION,
    SAVE_ALLOCATION,
    SHORT_READ,
    READ_ERROR,
    EXTRA_BYTE,
    CLOSE,
    PAGED_ALLOCATION,
    PAGED_HEADER
};
static enum Failure failure;

static void *candidate_allocation(size_t count, size_t size) {
    if (failure == ROM_ALLOCATION) {
        errno = ENOMEM;
        return NULL;
    }
    return calloc(count, size);
}

static void *backup_allocation(size_t size) {
    if (failure == SAVE_ALLOCATION) {
        errno = ENOMEM;
        return NULL;
    }
    return malloc(size);
}

static void *page_allocation(size_t size) {
    if (failure == PAGED_ALLOCATION) {
        errno = ENOMEM;
        return NULL;
    }
    return malloc(size);
}

static size_t candidate_read(void *data, size_t size, size_t count, FILE *file) {
    size_t read = fread(data, size, failure == SHORT_READ ? count - 1 : count, file);
    if (failure == PAGED_HEADER && count > 0xC0 && read > 0xAC)
        ((uint8_t *)data)[0xAC] = 'A';
    return read;
}

static int candidate_error(FILE *file) {
    return failure == READ_ERROR ? 1 : ferror(file);
}

static int candidate_tail(FILE *file) {
    return failure == EXTRA_BYTE ? 0 : fgetc(file);
}

static int candidate_close(FILE *file) {
    const int result = fclose(file);
    return failure == CLOSE ? EOF : result;
}

/* Compile the production transaction and detector with only their fallible
 * host operations replaced. No alternate loader or acceptance rule is tested. */
#define malloc backup_allocation
#define cupid_savedata_prepare_image checked_prepare_image
#define cupid_savedata_detect_type checked_detect_type
#include "../../../src/gba/save/detection.c"
#undef malloc
#define cupid_load_rom checked_load_rom
#define calloc candidate_allocation
#define malloc page_allocation
#define fread candidate_read
#define ferror candidate_error
#define fgetc candidate_tail
#define fclose candidate_close
#include "../../../src/gba/cartridge/image.c"
#undef cupid_load_rom
#undef calloc
#undef malloc
#undef fread
#undef ferror
#undef fgetc
#undef fclose
#undef cupid_savedata_prepare_image
#undef cupid_savedata_detect_type

int main(int argc, char **argv) {
    CHECK(argc == 2 || (argc == 3 && !strcmp(argv[2], "paged")));
    const bool paged = argc == 3;
    char path[1024], oldPath[1024];
    int n = snprintf(path, sizeof(path), "%s/cartridge-image-failures-%s.gba", argv[1],
                     paged ? "paged" : "ordinary");
    CHECK(n > 0 && (size_t)n < sizeof(path));
    uint8_t image[512] = {0};
    memcpy(image + 256, "FLASH_V", 7);
    n = snprintf(oldPath, sizeof(oldPath), "%s/cartridge-image-failures-old.gba", argv[1]);
    CHECK(n > 0 && (size_t)n < sizeof(oldPath));
    if (paged) {
        FILE *old = fopen(oldPath, "wb");
        CHECK(old && fwrite(image, 1, sizeof(image), old) == sizeof(image));
        CHECK(fclose(old) == 0);
        image[0xAC] = 'M';
    }
    FILE *file = fopen(path, "wb");
    CHECK(file && fwrite(image, 1, sizeof(image), file) == sizeof(image));
    if (paged) {
        CHECK(fseek(file, GBA_SIZE_ROM + 511, SEEK_SET) == 0);
        CHECK(fputc(0xA5, file) != EOF);
    }
    CHECK(fclose(file) == 0);
    for (unsigned attached = 0; attached < (paged ? 3u : 2u); ++attached) {
        for (failure = ROM_ALLOCATION; failure <= (paged ? PAGED_HEADER : CLOSE); ++failure) {
            struct CupidGBA *gba = calloc(1, sizeof(*gba));
            CHECK(gba);
            cupid_gba_create(gba);
            cupid_gba_reset(gba);
            cupid_timing_clear(&gba->timing);
            if (attached) {
                /* The linked core uses ordinary host operations. */
                CHECK(cupid_load_rom(gba, paged && attached == 1 ? oldPath : path));
                const uint8_t value = 0x37;
                cupid_savedata_begin_programming(&gba->memory.savedata, 7, 1, &value, 40);
                gba->cpu.gprs[15] = GBA_BASE_ROM0 + 8;
                cupid_memory_set_active_region(&gba->cpu, GBA_BASE_ROM0);
            }
            const struct CupidMemory before = gba->memory;
            const struct CupidCartridge board = gba->cartridge;
            const struct CupidTiming timing = gba->timing;
            const void *active = gba->cpu.memory.activeRegion;
            CHECK(!checked_load_rom(gba, path));
            CHECK(!memcmp(&before, &gba->memory, sizeof(before)));
            CHECK(!memcmp(&board, &gba->cartridge, sizeof(board)));
            CHECK(!memcmp(&timing, &gba->timing, sizeof(timing)));
            CHECK(active == gba->cpu.memory.activeRegion);
            if (attached) {
                CHECK(gba->memory.savedata.data[7] == 0xFF);
                cupid_timing_tick(&gba->timing, 40);
                CHECK(gba->memory.savedata.data[7] == 0x37);
            }
            cupid_gba_destroy(gba);
            free(gba);
        }
    }
    failure = NONE;
    struct CupidGBA *gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    CHECK(checked_load_rom(gba, path));
    CHECK(gba->memory.savedata.type == SAVE_FLASH512);
    CHECK(checked_load_rom(gba, path));
    CHECK(gba->memory.savedata.readyEvent.context == &gba->memory.savedata);
    cupid_gba_destroy(gba);
    free(gba);
    CHECK(remove(path) == 0);
    if (paged)
        CHECK(remove(oldPath) == 0);
    printf("PASS image failure atomicity: %u injected failures and two successful commits\n",
           paged ? 24u : 12u);
    return 0;
}
