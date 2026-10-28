#ifndef DLUMA_ATOMIC_INI_H
#define DLUMA_ATOMIC_INI_H

#include "dlw_common.h"

/* Callers serialize writers to the same file in this process. */
static int dluma_atomic_ini_begin(const WCHAR* path, WCHAR* temporary,
                                  unsigned int capacity)
{
    char number[32];
    WCHAR wide_number[32];
    DWORD attributes;
    HANDLE file;
    if (!path || !path[0] || wcslen(path) + 48U >= capacity)
        return 0;
    dluma_wstr_copy(temporary, capacity, path);
    dluma_wstr_append(temporary, capacity, L".tmp.");
    if (!dluma_u64_to_text(GetCurrentProcessId(), number, DLUMA_ARRAY_COUNT(number)) ||
        !dluma_utf8_to_wide(number, wide_number, DLUMA_ARRAY_COUNT(wide_number)))
        return 0;
    dluma_wstr_append(temporary, capacity, wide_number);
    attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        /* The all-NULL profile call clears the cache and may return zero
         * even when the preceding profile writes succeeded. */
        (void)WritePrivateProfileStringW(0, 0, 0, path);
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            !CopyFileW(path, temporary, FALSE))
            return 0;
        return 1;
    }
    if (GetLastError() != ERROR_FILE_NOT_FOUND)
        return 0;
    file = CreateFileW(temporary, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    {
        static const unsigned char marker[2] = {0xff, 0xfe};
        DWORD written = 0;
        int ok = WriteFile(file, marker, sizeof(marker), &written, 0) &&
                 written == sizeof(marker) && FlushFileBuffers(file);
        CloseHandle(file);
        if (!ok) {
            DeleteFileW(temporary);
            return 0;
        }
    }
    return 1;
}

static int dluma_atomic_ini_commit(const WCHAR* path, const WCHAR* temporary)
{
    HANDLE file;
    int flushed;
    (void)WritePrivateProfileStringW(0, 0, 0, temporary);
    file = CreateFileW(temporary, GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        DeleteFileW(temporary);
        return 0;
    }
    flushed = FlushFileBuffers(file) != 0;
    CloseHandle(file);
    if (!flushed || !MoveFileExW(temporary, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary);
        return 0;
    }
    /* Discard the old profile cache after replacing the file. */
    WritePrivateProfileStringW(0, 0, 0, path);
    return 1;
}

static void dluma_atomic_ini_abort(const WCHAR* temporary)
{
    WritePrivateProfileStringW(0, 0, 0, temporary);
    DeleteFileW(temporary);
}

#endif
