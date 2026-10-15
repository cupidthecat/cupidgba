/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "cupid/gba.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static enum CupidSaveType file_type(size_t size) {
    switch (size) {
    case GBA_SIZE_EEPROM512: return SAVE_EEPROM512;
    case GBA_SIZE_EEPROM: return SAVE_EEPROM;
    case GBA_SIZE_SRAM: return SAVE_SRAM;
    case GBA_SIZE_FLASH512: return SAVE_FLASH512;
    case GBA_SIZE_FLASH1M: return SAVE_FLASH1M;
    default: return SAVE_NONE;
    }
}

static int family(enum CupidSaveType type) {
    if (type == SAVE_EEPROM512 || type == SAVE_EEPROM) return 1;
    if (type == SAVE_SRAM || type == SAVE_SRAM512) return 2;
    if (type == SAVE_FLASH512 || type == SAVE_FLASH1M) return 3;
    return 0;
}

static bool load_capacity(const struct CupidSavedata* save, size_t fileSize,
                          enum CupidSaveType* type, size_t* capacity) {
    enum CupidSaveType fromFile = file_type(fileSize);
    if (fromFile == SAVE_NONE) return false;
    *type = save->type;
    *capacity = save->size;
    if (*type == SAVE_AUTODETECT) {
        *type = fromFile;
        *capacity = fileSize;
        return true;
    }
    /* 64 KiB is shared by SRAM512 and Flash512; the current family resolves it. */
    if (family(*type) == 2 && fileSize == GBA_SIZE_FLASH512) fromFile = SAVE_SRAM512;
    if (!family(*type) || family(*type) != family(fromFile)) return false;
    if (fileSize > *capacity) {
        if (save->typeForced) return false;
        *type = fromFile;
        *capacity = fileSize;
    }
    return *capacity >= fileSize;
}

#ifdef _WIN32
static int windows_error(void);
#endif

static bool readable_regular_file(const char* path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) { errno = windows_error(); return false; }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) {
        errno = EINVAL;
        return false;
    }
#else
    struct stat info;
    if (stat(path, &info)) return false;
    if (!S_ISREG(info.st_mode)) { errno = EINVAL; return false; }
#endif
    return true;
}

bool cupid_savedata_load_file(struct CupidSavedata* save, const char* path) {
    if (save->type == SAVE_NONE) { errno = 0; return true; }
    if (!readable_regular_file(path)) return false;
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    errno = 0;
    int error = 0;
    long length = -1;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET))
        error = errno ? errno : EIO;
    enum CupidSaveType type = save->type;
    size_t capacity = 0;
    if (!error && (length <= 0 || !load_capacity(save, (size_t)length, &type, &capacity)))
        error = EINVAL;
    uint8_t* loaded = NULL;
    if (!error) {
        loaded = malloc(capacity);
        if (!loaded) error = ENOMEM;
    }
    if (!error) {
        memset(loaded, 0xFF, capacity);
        if (fread(loaded, 1, (size_t)length, file) != (size_t)length || ferror(file))
            error = errno ? errno : EIO;
        /* Reject growth during the read as well as a truncated input. */
        if (!error && (fgetc(file) != EOF || ferror(file))) error = EIO;
    }
    if (fclose(file) && !error) error = errno ? errno : EIO;
    if (error) { free(loaded); errno = error; return false; }
    cupid_savedata_cancel_programming(save);
    save->command = FLASH_COMMAND_NONE;
    save->flashState = FLASH_STATE_RAW;
    save->readBitsRemaining = 0;
    save->eepromWriteBits = 0;
    free(save->data);
    save->data = loaded;
    save->size = capacity;
    save->type = type;
    if (type != SAVE_FLASH1M) save->flashBank = 0;
    save->currentBank = loaded + (family(type) == 3 ? (unsigned)save->flashBank * 0x10000u : 0);
    save->dirty = false;
    errno = 0;
    printf("CUPID: Loaded save: %s (%ld bytes, %zu-byte capacity)\n", path, length, capacity);
    return true;
}

#ifdef _WIN32
static int windows_error(void) {
    switch (GetLastError()) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return ENOENT;
    case ERROR_ACCESS_DENIED: return EACCES;
    case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION: return EBUSY;
    case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return ENOSPC;
    default: return EIO;
    }
}
#endif

static bool destination_mode(const char* path, unsigned* mode) {
#ifdef _WIN32
    (void)mode;
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        int error = windows_error();
        if (error == ENOENT) return true;
        errno = error;
        return false;
    }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_READONLY)) {
        errno = EACCES;
        return false;
    }
#else
    struct stat info;
    if (lstat(path, &info)) return errno == ENOENT;
    if (!S_ISREG(info.st_mode)) { errno = EINVAL; return false; }
    if (!(info.st_mode & 0222)) { errno = EACCES; return false; }
    *mode = (unsigned)info.st_mode & 0777;
#endif
    /* Check write access without truncating the destination. */
    FILE* existing = fopen(path, "r+b");
    if (!existing) return false;
    if (fclose(existing)) return false;
    return true;
}

static FILE* create_stage(const char* path) {
#ifdef _WIN32
    int fd = _open(path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
#endif
    if (fd < 0) return NULL;
#ifdef _WIN32
    FILE* file = _fdopen(fd, "wb");
#else
    FILE* file = fdopen(fd, "wb");
#endif
    if (!file) {
        int error = errno;
#ifdef _WIN32
        _close(fd);
#else
        close(fd);
#endif
        remove(path); /* This invocation created the stage exclusively. */
        errno = error;
    }
    return file;
}

bool cupid_savedata_save_file(struct CupidSavedata* save, const char* path) {
    if (!save->dirty || !save->data || !save->size) return true;
    unsigned mode = 0600;
    if (!destination_mode(path, &mode)) return false;
    size_t length = strlen(path) + 64;
    char* stage = malloc(length);
    if (!stage) { errno = ENOMEM; return false; }
    static atomic_uint sequence;
#ifdef _WIN32
    unsigned long process = GetCurrentProcessId();
#else
    unsigned long process = (unsigned long)getpid();
#endif
    FILE* file = NULL;
    for (unsigned attempt = 0; attempt < 16; attempt++) {
        unsigned serial = atomic_fetch_add_explicit(&sequence, 1, memory_order_relaxed);
        snprintf(stage, length, "%s.tmp.%lu.%u", path, process, serial);
        file = create_stage(stage);
        if (file || errno != EEXIST) break;
    }
    if (!file) { int error = errno; free(stage); errno = error; return false; }
    errno = 0;
    int error = 0;
    if (fwrite(save->data, 1, save->size, file) != save->size || fflush(file))
        error = errno ? errno : EIO;
#ifdef _WIN32
    if (!error && _commit(_fileno(file))) error = errno ? errno : EIO;
#else
    if (!error && (fchmod(fileno(file), (mode_t)mode) || fsync(fileno(file))))
        error = errno ? errno : EIO;
#endif
    if (fclose(file) && !error) error = errno ? errno : EIO;
    if (!error) {
#ifdef _WIN32
        if (!MoveFileExA(stage, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            error = windows_error();
#else
        if (rename(stage, path)) error = errno ? errno : EIO;
#endif
    }
    if (error) remove(stage); /* Remove only our exclusively created stage. */
    free(stage);
    if (error) { errno = error; return false; }
    save->dirty = false;
    errno = 0;
    printf("CUPID: Saved: %s (%zu bytes)\n", path, save->size);
    return true;
}
