/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "cupid/gba.h"
#include "cupid/isa-inlines.h"
#include "../test.h"
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

static const char* directory;
static const char* group;

static void path(char* output, size_t capacity, const char* suffix) {
    int length = snprintf(output, capacity, "%s/bios-file-%s-%s.bin", directory, group, suffix);
    CHECK(length > 0 && (size_t)length < capacity);
}

static void seed(const char* filename, size_t length, unsigned revision) {
    FILE* file = fopen(filename, "wb");
    CHECK(file);
    for (size_t i = 0; i < length; i++) CHECK(fputc((int)((i * 17 + revision * 31) & 255), file) != EOF);
    CHECK(fclose(file) == 0);
}

static struct CupidGBA* machine(void) {
    struct CupidGBA* gba = calloc(1, sizeof(*gba));
    CHECK(gba);
    cupid_gba_create(gba);
    cupid_gba_reset(gba);
    gba->cpu.gprs[ARM_PC] = 0x100;
    cupid_memory_set_active_region(&gba->cpu, 0x100);
    gba->memory.biosPrefetch = 0x12345678;
    gba->cpu.prefetch[0] = 0xA55A5AA5;
    gba->cpu.prefetch[1] = 0x11223344;
    return gba;
}

static void destroy(struct CupidGBA* gba) {
    cupid_gba_destroy(gba);
    free(gba);
}

static void rejected_input_preserves_state(struct CupidGBA* gba, const char* filename) {
    struct CupidGBA* before = malloc(sizeof(*before));
    uint8_t firmware[GBA_SIZE_BIOS];
    CHECK(before);
    memcpy(before, gba, sizeof(*before));
    memcpy(firmware, gba->memory.bios, sizeof(firmware));
    CHECK(!cupid_load_bios(gba, filename));
    CHECK(!memcmp(before, gba, sizeof(*before)));
    CHECK(!memcmp(firmware, gba->memory.bios, sizeof(firmware)));
    free(before);
}

static void invalid_lengths_preserve_current_firmware(bool external) {
    char input[1024], valid[1024], missing[1024];
    path(input, sizeof(input), "candidate");
    path(valid, sizeof(valid), "existing");
    path(missing, sizeof(missing), "missing");
    struct CupidGBA* gba = machine();
    if (external) {
        seed(valid, GBA_SIZE_BIOS, 7);
        CHECK(cupid_load_bios(gba, valid));
        CHECK(remove(valid) == 0);
    }
    rejected_input_preserves_state(gba, missing);
    const size_t lengths[] = {0, 1, 15, 511, 4096, GBA_SIZE_BIOS - 1, GBA_SIZE_BIOS + 1, GBA_SIZE_BIOS * 2};
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(*lengths); i++) {
        seed(input, lengths[i], i);
        rejected_input_preserves_state(gba, input);
    }
    CHECK(remove(input) == 0);
    destroy(gba);
}

static void builtin_firmware_survives_invalid_input(void) { invalid_lengths_preserve_current_firmware(false); }
static void external_firmware_survives_invalid_input(void) { invalid_lengths_preserve_current_firmware(true); }

static void exact_images_install_and_execute_after_reset(void) {
    char input[1024];
    path(input, sizeof(input), "valid");
    struct CupidGBA* gba = machine();
    uint8_t* original = gba->memory.bios;
    for (unsigned revision = 1; revision < 4; revision++) {
        seed(input, GBA_SIZE_BIOS, revision);
        FILE* file = fopen(input, "r+b");
        CHECK(file);
        uint32_t code[] = {0xE3A00000u | revision, 0xEAFFFFFEu};
        CHECK(fwrite(code, sizeof(code), 1, file) == 1 && fclose(file) == 0);
        CHECK(cupid_load_bios(gba, input));
        CHECK(gba->memory.fullBios && gba->memory.biosLoaded && gba->hasBios && !gba->biosHLE);
        CHECK(gba->memory.bios == original);
        CHECK((const void*)gba->cpu.memory.activeRegion == gba->memory.bios);
        file = fopen(input, "rb");
        CHECK(file);
        for (unsigned i = 0; i < GBA_SIZE_BIOS; i++) CHECK(fgetc(file) == gba->memory.bios[i]);
        CHECK(fgetc(file) == EOF && !ferror(file) && fclose(file) == 0);
        cupid_gba_reset(gba);
        cupid_gba_step(gba);
        CHECK(gba->cpu.gprs[0] == (int32_t)revision);
        CHECK(gba->memory.fullBios && !gba->biosHLE);
    }
    destroy(gba);
    CHECK(remove(input) == 0);
}

static void nonregular_input_is_rejected_without_opening_a_pipe(void) {
    struct CupidGBA* gba = machine();
    rejected_input_preserves_state(gba, directory);
#ifndef _WIN32
    char temporary[] = "/tmp/cupidgba-bios-input-XXXXXX";
    CHECK(mkdtemp(temporary));
    char fifo[128];
    CHECK(snprintf(fifo, sizeof(fifo), "%s/image", temporary) > 0);
    CHECK(mkfifo(fifo, 0600) == 0);
    rejected_input_preserves_state(gba, fifo);
    CHECK(unlink(fifo) == 0 && rmdir(temporary) == 0);
#endif
    destroy(gba);
}

int main(int argc, char** argv) {
    CHECK(argc == 3);
    group = argv[1];
    directory = argv[2];
    if (!strcmp(group, "builtin")) RUN(builtin_firmware_survives_invalid_input);
    else if (!strcmp(group, "external")) RUN(external_firmware_survives_invalid_input);
    else if (!strcmp(group, "valid")) RUN(exact_images_install_and_execute_after_reset);
    else if (!strcmp(group, "nonregular")) RUN(nonregular_input_is_rejected_without_opening_a_pipe);
    else CHECK(false);
    return 0;
}
