/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "cupid/file.h"
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

bool cupid_file_read_exact(const char* path, void* data, size_t size) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) { errno = windows_error(); return false; }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) { errno = EINVAL; return false; }
#else
    struct stat info;
    if (stat(path, &info)) return false;
    if (!S_ISREG(info.st_mode)) { errno = EINVAL; return false; }
#endif
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    errno = 0;
    int error = 0;
    if (fread(data, 1, size, file) != size) error = ferror(file) ? (errno ? errno : EIO) : EINVAL;
    if (!error && fgetc(file) != EOF) error = EINVAL;
    if (!error && ferror(file)) error = errno ? errno : EIO;
    if (fclose(file) && !error) error = errno ? errno : EIO;
    errno = error;
    return !error;
}

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

bool cupid_file_replace(const char* path, const void* data, size_t size) {
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
    if (fwrite(data, 1, size, file) != size || fflush(file))
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
    errno = 0;
    return true;
}
