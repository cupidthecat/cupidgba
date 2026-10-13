/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "cupid/gba.h"
#include "../../test.h"
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

static char directory[1024];
static char input[1100], output[1100], sentinel[1160];

static uint8_t pattern(size_t i, unsigned revision) {
    return (uint8_t)(i * 13 + (i >> 9) * 7 + 31 + revision * 83);
}

static void seed(const char* path, size_t size, unsigned revision) {
    FILE* file = fopen(path, "wb");
    CHECK(file);
    for (size_t i = 0; i < size; i++) CHECK(fputc(pattern(i, revision), file) != EOF);
    CHECK(fclose(file) == 0);
}

static void verify(const char* path, size_t size, unsigned revision) {
    FILE* file = fopen(path, "rb");
    CHECK(file);
    for (size_t i = 0; i < size; i++) CHECK(fgetc(file) == pattern(i, revision));
    CHECK(fgetc(file) == EOF && !ferror(file));
    CHECK(fclose(file) == 0);
}

static void fill(struct CupidSavedata* save, unsigned revision) {
    for (size_t i = 0; i < save->size; i++) save->data[i] = pattern(i, revision);
    save->dirty = true;
}

static void exclusive_staging_preserves_an_existing_companion(void) {
#ifdef _WIN32
    unsigned long process = GetCurrentProcessId();
#else
    unsigned long process = (unsigned long)getpid();
#endif
    int length = snprintf(sentinel, sizeof(sentinel), "%s.tmp.%lu.0", output, process);
    CHECK(length > 0 && (size_t)length < sizeof(sentinel));
    seed(sentinel, 73, 4);
    struct CupidSavedata save;
    cupid_savedata_init(&save);
    cupid_savedata_force_type(&save, SAVE_EEPROM512);
    fill(&save, 1);
    CHECK(cupid_savedata_save_file(&save, output) && !save.dirty);
    verify(output, 512, 1);
    verify(sentinel, 73, 4);
    cupid_savedata_deinit(&save);
}

static void every_save_capacity_round_trips_complete_replacements(void) {
    const enum CupidSaveType types[] = {SAVE_EEPROM512, SAVE_EEPROM, SAVE_SRAM,
                                       SAVE_SRAM512, SAVE_FLASH512, SAVE_FLASH1M};
    const size_t sizes[] = {512, 8192, 32768, 65536, 65536, 131072};
    for (unsigned test = 0; test < 6; test++) {
        struct CupidSavedata save, reload;
        cupid_savedata_init(&save);
        cupid_savedata_force_type(&save, types[test]);
        CHECK(save.size == sizes[test]);
        for (unsigned revision = 0; revision < 2; revision++) {
            fill(&save, revision);
            CHECK(cupid_savedata_save_file(&save, output) && !save.dirty);
            verify(output, sizes[test], revision);
            cupid_savedata_init(&reload);
            cupid_savedata_force_type(&reload, types[test]);
            CHECK(cupid_savedata_load_file(&reload, output));
            CHECK(reload.size == sizes[test] && !memcmp(save.data, reload.data, sizes[test]));
            CHECK(!reload.dirty && reload.typeForced);
            cupid_savedata_deinit(&reload);
        }
        cupid_savedata_deinit(&save);
    }
}

static void inferred_capacities_expand_and_flash_bank_pointers_follow(void) {
    const size_t sizes[] = {8192, 65536, 131072};
    const enum CupidSaveType types[] = {SAVE_EEPROM, SAVE_SRAM512, SAVE_FLASH1M};
    for (unsigned test = 0; test < 3; test++) {
        struct CupidSavedata save;
        cupid_savedata_init(&save);
        if (test == 0) {
            uint8_t rom[512] = {0};
            memcpy(rom + 256, "EEPROM_V", 8);
            cupid_savedata_detect_type(&save, rom, sizeof(rom));
        } else if (test == 1) cupid_savedata_init_sram(&save);
        else { save.type = SAVE_FLASH512; cupid_savedata_init_flash(&save); }
        CHECK(!save.typeForced);
        seed(input, sizes[test], test);
        CHECK(cupid_savedata_load_file(&save, input));
        CHECK(save.type == types[test] && save.size == sizes[test]);
        for (size_t i = 0; i < sizes[test]; i++) CHECK(save.data[i] == pattern(i, test));
        CHECK(save.currentBank == save.data);
        if (test == 2) {
            save.flashBank = 1;
            save.currentBank = save.data + 65536;
            seed(input, 131072, 4);
            CHECK(cupid_savedata_load_file(&save, input));
            CHECK(save.currentBank == save.data + 65536 && save.flashBank == 1);
            CHECK(save.currentBank[19] == pattern(65536 + 19, 4));
        }
        cupid_savedata_deinit(&save);
    }
}

static void invalid_input_preserves_active_state_and_explicit_choices(void) {
    struct CupidSavedata save;
    cupid_savedata_init(&save);
    cupid_savedata_force_type(&save, SAVE_EEPROM512);
    fill(&save, 2);
    const size_t lengths[] = {0, 511, 513, 8192, 32768};
    for (unsigned test = 0; test < 5; test++) {
        seed(input, lengths[test], 0);
        struct CupidSavedata before;
        memcpy(&before, &save, sizeof(before));
        CHECK(!cupid_savedata_load_file(&save, input) && errno == EINVAL);
        CHECK(!memcmp(&before, &save, sizeof(save)));
        for (size_t i = 0; i < save.size; i++) CHECK(save.data[i] == pattern(i, 2));
    }
    char missing[1100];
    CHECK(snprintf(missing, sizeof(missing), "%s/missing.sav", directory) > 0);
    CHECK(!cupid_savedata_load_file(&save, missing) && errno == ENOENT);
    CHECK(save.dirty);
    struct CupidSavedata before;
    memcpy(&before, &save, sizeof(before));
    CHECK(!cupid_savedata_load_file(&save, directory));
    CHECK(!memcmp(&before, &save, sizeof(save)));
    cupid_savedata_force_type(&save, SAVE_NONE);
    CHECK(cupid_savedata_load_file(&save, input));
    cupid_savedata_deinit(&save);
}

static void smaller_related_files_fill_unused_capacity_with_erased_bytes(void) {
    struct CupidSavedata save;
    cupid_savedata_init(&save);
    cupid_savedata_force_type(&save, SAVE_EEPROM);
    fill(&save, 3);
    seed(input, 512, 1);
    CHECK(cupid_savedata_load_file(&save, input));
    CHECK(save.type == SAVE_EEPROM && save.size == 8192 && save.typeForced);
    for (size_t i = 0; i < 8192; i++) CHECK(save.data[i] == (i < 512 ? pattern(i, 1) : 0xFF));
    cupid_savedata_deinit(&save);
    cupid_savedata_init(&save);
    seed(input, 8192, 2);
    CHECK(cupid_savedata_load_file(&save, input));
    CHECK(save.type == SAVE_EEPROM && save.size == 8192 && !save.typeForced);
    cupid_savedata_deinit(&save);
}

static void protected_destinations_keep_the_previous_save_and_dirty_state(void) {
    struct CupidSavedata save;
    cupid_savedata_init(&save);
    cupid_savedata_force_type(&save, SAVE_EEPROM512);
    fill(&save, 1);
    seed(output, 512, 0);
#ifdef _WIN32
    CHECK(SetFileAttributesA(output, FILE_ATTRIBUTE_READONLY));
#else
    CHECK(chmod(output, 0444) == 0);
#endif
    CHECK(!cupid_savedata_save_file(&save, output) && save.dirty);
    verify(output, 512, 0);
#ifdef _WIN32
    CHECK(SetFileAttributesA(output, FILE_ATTRIBUTE_NORMAL));
    HANDLE held = CreateFileA(output, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(held != INVALID_HANDLE_VALUE);
    CHECK(!cupid_savedata_save_file(&save, output) && save.dirty);
    CHECK(errno == EBUSY || errno == EACCES);
    CHECK(CloseHandle(held));
    verify(output, 512, 0);
#else
    CHECK(chmod(output, 0640) == 0);
    struct stat previousInfo;
    CHECK(stat(output, &previousInfo) == 0);
    FILE* held = fopen(output, "rb");
    CHECK(held);
    CHECK(cupid_savedata_save_file(&save, output));
    for (size_t i = 0; i < 512; i++) CHECK(fgetc(held) == pattern(i, 0));
    CHECK(fclose(held) == 0);
    struct stat info;
    CHECK(stat(output, &info) == 0 && (info.st_mode & 0777) == (previousInfo.st_mode & 0777));
#endif
    CHECK(cupid_savedata_save_file(&save, output) && !save.dirty);
    verify(output, 512, 1);
    fill(&save, 2);
    CHECK(!cupid_savedata_save_file(&save, directory) && save.dirty);
    cupid_savedata_deinit(&save);
}

#ifndef _WIN32
static void real_delayed_write_failure_preserves_the_complete_previous_file(void) {
    struct CupidSavedata save;
    cupid_savedata_init(&save);
    cupid_savedata_force_type(&save, SAVE_EEPROM512);
    seed(output, 512, 0);
    fill(&save, 1);
    struct rlimit original, limited;
    CHECK(getrlimit(RLIMIT_FSIZE, &original) == 0);
    limited = original;
    limited.rlim_cur = 128;
    void (*prior)(int) = signal(SIGXFSZ, SIG_IGN);
    CHECK(prior != SIG_ERR && setrlimit(RLIMIT_FSIZE, &limited) == 0);
    bool result = cupid_savedata_save_file(&save, output);
    int error = errno;
    CHECK(setrlimit(RLIMIT_FSIZE, &original) == 0);
    CHECK(signal(SIGXFSZ, prior) != SIG_ERR);
    CHECK(!result && save.dirty && error == EFBIG);
    verify(output, 512, 0);
    CHECK(cupid_savedata_save_file(&save, output) && !save.dirty);
    verify(output, 512, 1);
    cupid_savedata_deinit(&save);
}

static void non_regular_input_is_rejected_without_waiting_for_a_writer(void) {
    /* Use the native filesystem: a Windows-mounted build directory does not
     * provide POSIX named pipes. */
    char fifoDirectory[] = "/tmp/cupidgba-save-input-XXXXXX";
    CHECK(mkdtemp(fifoDirectory));
    char fifo[128];
    CHECK(snprintf(fifo, sizeof(fifo), "%s/input.fifo", fifoDirectory) > 0);
    CHECK(mkfifo(fifo, 0600) == 0);
    struct CupidSavedata save;
    cupid_savedata_init(&save);
    cupid_savedata_force_type(&save, SAVE_EEPROM512);
    fill(&save, 3);
    CHECK(!cupid_savedata_load_file(&save, fifo) && errno == EINVAL);
    CHECK(save.dirty && save.size == 512);
    for (size_t i = 0; i < save.size; i++) CHECK(save.data[i] == pattern(i, 3));
    CHECK(remove(fifo) == 0);
    CHECK(rmdir(fifoDirectory) == 0);
    cupid_savedata_deinit(&save);
}
#endif

static void remove_owned_fixtures(void) {
    CHECK(remove(input) == 0 && remove(output) == 0 && remove(sentinel) == 0);
#ifdef _WIN32
    CHECK(_rmdir(directory) == 0);
#else
    CHECK(rmdir(directory) == 0);
#endif
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
#ifdef _WIN32
    unsigned long process = GetCurrentProcessId();
#else
    unsigned long process = (unsigned long)getpid();
#endif
    unsigned attempt;
    for (attempt = 0; attempt < 16; attempt++) {
        int length = snprintf(directory, sizeof(directory), "%s/save-file-test.%lu.%u", argv[1], process, attempt);
        CHECK(length > 0 && (size_t)length < sizeof(directory));
#ifdef _WIN32
        if (_mkdir(directory) == 0) break;
#else
        if (mkdir(directory, 0700) == 0) break;
#endif
        CHECK(errno == EEXIST);
    }
    CHECK(attempt < 16);
    CHECK(snprintf(input, sizeof(input), "%s/input.sav", directory) > 0);
    CHECK(snprintf(output, sizeof(output), "%s/output.sav", directory) > 0);
    RUN(exclusive_staging_preserves_an_existing_companion);
    RUN(every_save_capacity_round_trips_complete_replacements);
    RUN(inferred_capacities_expand_and_flash_bank_pointers_follow);
    RUN(invalid_input_preserves_active_state_and_explicit_choices);
    RUN(smaller_related_files_fill_unused_capacity_with_erased_bytes);
    RUN(protected_destinations_keep_the_previous_save_and_dirty_state);
#ifndef _WIN32
    RUN(real_delayed_write_failure_preserves_the_complete_previous_file);
    RUN(non_regular_input_is_rejected_without_waiting_for_a_writer);
#endif
    RUN(remove_owned_fixtures);
    return 0;
}
