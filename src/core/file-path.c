/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file file-path.c
 * @brief Regular-file identity and normalized destination comparisons.
 *
 * - Detect existing hard links and followed symbolic links without opening streams.
 * - Resolve a missing destination through its existing parent directory.
 * - Reject unresolved links and nonregular leaves before callers create outputs.
 */
#ifndef _WIN32
#define _XOPEN_SOURCE 700
#endif
#include "cupid/file.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

struct PathIdentity {
    char *name;
    bool exists;
#ifdef _WIN32
    DWORD volume, high, low;
#else
    dev_t device;
    ino_t inode;
#endif
};

static char *copy_name(const char *name, size_t size) {
    if (size == SIZE_MAX) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    char *copy = malloc(size + 1);
    if (copy) {
        memcpy(copy, name, size);
        copy[size] = 0;
    }
    return copy;
}

static char *join_name(const char *parent, const char *leaf) {
    const size_t parentSize = strlen(parent), leafSize = strlen(leaf);
    if (!leafSize || parentSize > SIZE_MAX - 2 || leafSize > SIZE_MAX - parentSize - 2) {
        errno = EINVAL;
        return NULL;
    }
    char *name = malloc(parentSize + leafSize + 2);
    if (!name)
        return NULL;
    memcpy(name, parent, parentSize);
    size_t size = parentSize;
#ifdef _WIN32
    if (size && parent[size - 1] != '/' && parent[size - 1] != '\\')
        name[size++] = '\\';
#else
    if (size && parent[size - 1] != '/')
        name[size++] = '/';
#endif
    memcpy(name + size, leaf, leafSize + 1);
    return name;
}

#ifdef _WIN32
static int path_error(void) {
    switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return ENOENT;
        case ERROR_ACCESS_DENIED:
            return EACCES;
        case ERROR_NOT_ENOUGH_MEMORY:
            return ENOMEM;
        default:
            return EIO;
    }
}

static HANDLE path_handle(const char *path) {
    return CreateFileA(path, FILE_READ_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS, NULL);
}

static char *handle_name(HANDLE handle) {
    const DWORD needed = GetFinalPathNameByHandleA(handle, NULL, 0, VOLUME_NAME_DOS);
    if (!needed || needed == MAXDWORD) {
        errno = needed ? ENAMETOOLONG : path_error();
        return NULL;
    }
    char *name = malloc((size_t)needed + 1);
    if (!name)
        return NULL;
    const DWORD length = GetFinalPathNameByHandleA(handle, name, needed + 1, VOLUME_NAME_DOS);
    if (!length || length > needed) {
        const int error = length ? ENAMETOOLONG : path_error();
        free(name);
        errno = error;
        return NULL;
    }
    return name;
}

static WCHAR *wide_name(const char *name) {
    const int length = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, name, -1, NULL, 0);
    if (length <= 0 || (size_t)length > SIZE_MAX / sizeof(WCHAR)) {
        errno = EINVAL;
        return NULL;
    }
    WCHAR *wide = malloc((size_t)length * sizeof(*wide));
    if (!wide)
        return NULL;
    if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, name, -1, wide, length)) {
        free(wide);
        errno = EINVAL;
        return NULL;
    }
    return wide;
}

static int same_name(const char *first, const char *second) {
    WCHAR *a = wide_name(first), *b = a ? wide_name(second) : NULL;
    if (!a || !b) {
        const int error = errno;
        free(a);
        free(b);
        errno = error;
        return -1;
    }
    /* Use the operating system's ordinal case table, including non-ASCII
     * filenames. The C locale does not fold every Windows filename pair. */
    const int compared = CompareStringOrdinal(a, -1, b, -1, TRUE);
    const int error = compared ? 0 : path_error();
    free(a);
    free(b);
    errno = error;
    return compared ? compared == CSTR_EQUAL : -1;
}

static bool identify_path(const char *path, struct PathIdentity *identity) {
    HANDLE handle = path_handle(path);
    if (handle != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info;
        if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE))) {
            CloseHandle(handle);
            errno = EINVAL;
            return false;
        }
        identity->name = handle_name(handle);
        const int error = errno;
        CloseHandle(handle);
        errno = error;
        identity->exists = true;
        identity->volume = info.dwVolumeSerialNumber;
        identity->high = info.nFileIndexHigh;
        identity->low = info.nFileIndexLow;
        return identity->name != NULL;
    }
    errno = path_error();
    if (errno != ENOENT)
        return false;
    /* An existing reparse point whose target is missing cannot name a safe
     * destination by lexical normalization alone. */
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        errno = EINVAL;
        return false;
    }
    const DWORD needed = GetFullPathNameA(path, 0, NULL, NULL);
    if (!needed || needed == MAXDWORD) {
        errno = needed ? ENAMETOOLONG : path_error();
        return false;
    }
    char *absolute = malloc((size_t)needed + 1), *leaf = NULL;
    if (!absolute)
        return false;
    const DWORD length = GetFullPathNameA(path, needed + 1, absolute, &leaf);
    if (!length || length > needed || !leaf || !*leaf) {
        free(absolute);
        errno = EINVAL;
        return false;
    }
    char *basename = copy_name(leaf, strlen(leaf));
    if (!basename) {
        free(absolute);
        return false;
    }
    *leaf = 0;
    handle = path_handle(absolute);
    free(absolute);
    char *parent = handle == INVALID_HANDLE_VALUE ? NULL : handle_name(handle);
    int error = handle == INVALID_HANDLE_VALUE ? path_error() : errno;
    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
    if (parent)
        identity->name = join_name(parent, basename);
    else
        errno = error;
    error = errno;
    free(parent);
    free(basename);
    errno = error;
    return identity->name != NULL;
}
#else
static bool identify_path(const char *path, struct PathIdentity *identity) {
    struct stat info;
    if (!stat(path, &info)) {
        if (!S_ISREG(info.st_mode)) {
            errno = EINVAL;
            return false;
        }
        identity->exists = true;
        identity->device = info.st_dev;
        identity->inode = info.st_ino;
        identity->name = realpath(path, NULL);
        return identity->name != NULL;
    }
    if (errno != ENOENT)
        return false;
    if (!lstat(path, &info)) {
        errno = EINVAL;
        return false;
    }
    if (errno != ENOENT)
        return false;
    char *copy = copy_name(path, strlen(path));
    if (!copy)
        return false;
    char *slash = strrchr(copy, '/');
    const char *leaf = slash ? slash + 1 : copy;
    char *basename = copy_name(leaf, strlen(leaf));
    if (!basename) {
        free(copy);
        return false;
    }
    if (slash)
        slash[slash == copy ? 1 : 0] = 0;
    char *parent = realpath(slash ? copy : ".", NULL);
    if (parent)
        identity->name = join_name(parent, basename);
    const int error = errno;
    free(parent);
    free(basename);
    free(copy);
    errno = error;
    return identity->name != NULL;
}
#endif

int cupid_file_compare_paths(const char *first, const char *second) {
    if (!first || !second || !*first || !*second) {
        errno = EINVAL;
        return -1;
    }
    struct PathIdentity a = {0}, b = {0};
    int result = -1;
    if (identify_path(first, &a) && identify_path(second, &b)) {
        if (a.exists && b.exists) {
#ifdef _WIN32
            result = a.volume == b.volume && a.high == b.high && a.low == b.low;
#else
            result = a.device == b.device && a.inode == b.inode;
#endif
        } else {
#ifdef _WIN32
            result = same_name(a.name, b.name);
#else
            result = !strcmp(a.name, b.name);
#endif
        }
    }
    const int error = result < 0 ? errno : 0;
    free(a.name);
    free(b.name);
    errno = error;
    return result;
}
