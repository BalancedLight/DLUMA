#include "dlw_common.h"
#include "dlw_build_identity.h"
#include "dlw_build_provenance.h"
#include "dlw_path_security.h"

#define DLUMA_BUILD_IDENTITY_HEADER_CAPACITY 4096U
#define DLUMA_BUILD_IDENTITY_READ_CAPACITY 16384U
#define DLUMA_BUILD_IDENTITY_MAX_FILE_BYTES (1024ULL * 1024ULL * 1024ULL)

static DlumaBuildIdentity g_dluma_build_identity;
static char g_dluma_build_identity_text[DLUMA_BUILD_IDENTITY_TEXT_CAPACITY];
static WCHAR g_dluma_build_identity_path[DLUMA_MAX_WPATH];
static volatile LONG g_dluma_build_identity_initialized;

static int dluma_build_identity_file_info_equal(
    const BY_HANDLE_FILE_INFORMATION* first,
    const BY_HANDLE_FILE_INFORMATION* second)
{
    if (!first || !second) return 0;
    return first->dwVolumeSerialNumber == second->dwVolumeSerialNumber &&
           first->nFileIndexHigh == second->nFileIndexHigh &&
           first->nFileIndexLow == second->nFileIndexLow &&
           first->nFileSizeHigh == second->nFileSizeHigh &&
           first->nFileSizeLow == second->nFileSizeLow &&
           first->ftLastWriteTime.dwHighDateTime ==
               second->ftLastWriteTime.dwHighDateTime &&
           first->ftLastWriteTime.dwLowDateTime ==
               second->ftLastWriteTime.dwLowDateTime;
}

static int dluma_build_identity_hash_stable_file(
    const WCHAR* path,
    DWORD loaded_timestamp,
    DWORD loaded_size_of_image,
    ULONGLONG* file_size,
    char hash[DLUMA_BUILD_IDENTITY_SHA256_CAPACITY])
{
    HANDLE file;
    LARGE_INTEGER size;
    BY_HANDLE_FILE_INFORMATION before;
    BY_HANDLE_FILE_INFORMATION after;
    DlumaSha256Context context;
    BYTE digest[32];
    BYTE buffer[DLUMA_BUILD_IDENTITY_READ_CAPACITY];
    BYTE header[DLUMA_BUILD_IDENTITY_HEADER_CAPACITY];
    DWORD header_size = 0;
    ULONGLONG remaining;
    DWORD disk_timestamp = 0;
    DWORD disk_size_of_image = 0;
    int success = 0;
    if (!path || !path[0] || !file_size || !hash) return 0;
    *file_size = 0;
    hash[0] = 0;
    file = CreateFileW(path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_DELETE,
                       0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (ULONGLONG)size.QuadPart > DLUMA_BUILD_IDENTITY_MAX_FILE_BYTES ||
        !GetFileInformationByHandle(file, &before))
        goto done;

    remaining = (ULONGLONG)size.QuadPart;
    dluma_sha256_init(&context);
    while (remaining) {
        DWORD wanted = remaining > sizeof(buffer)
            ? (DWORD)sizeof(buffer) : (DWORD)remaining;
        DWORD read = 0;
        DWORD copy_count;
        if (!ReadFile(file, buffer, wanted, &read, 0) || read != wanted)
            goto done;
        copy_count = read;
        if (copy_count > sizeof(header) - header_size)
            copy_count = (DWORD)(sizeof(header) - header_size);
        if (copy_count) {
            memcpy(header + header_size, buffer, copy_count);
            header_size += copy_count;
        }
        dluma_sha256_update(&context, buffer, read);
        remaining -= read;
    }
    if (!GetFileInformationByHandle(file, &after) ||
        !dluma_build_identity_file_info_equal(&before, &after) ||
        !dluma_build_identity_parse_pe_header(
            header, header_size, &disk_timestamp, &disk_size_of_image) ||
        disk_timestamp != loaded_timestamp ||
        disk_size_of_image != loaded_size_of_image)
        goto done;

    dluma_sha256_final(&context, digest);
    dluma_sha256_hex(digest, hash);
    *file_size = (ULONGLONG)size.QuadPart;
    success = 1;
done:
    CloseHandle(file);
    return success;
}

int dluma_build_identity_initialize(HMODULE loader_module)
{
    BYTE* image = (BYTE*)loader_module;
    DlumaBuildIdentity identity;
    if (dluma_atomic_compare_exchange(
            &g_dluma_build_identity_initialized, 1, 0) != 0)
        return dluma_atomic_compare_exchange(
                   &g_dluma_build_identity_initialized, 0, 0) == 2 &&
               g_dluma_build_identity.complete ? 1 : 0;

    memset(&identity, 0, sizeof(identity));
    memset(g_dluma_build_identity_path, 0, sizeof(g_dluma_build_identity_path));
    if (image && dluma_is_readable(image, DLUMA_BUILD_IDENTITY_HEADER_CAPACITY) &&
        dluma_build_identity_parse_pe_header(
            image, DLUMA_BUILD_IDENTITY_HEADER_CAPACITY,
            &identity.pe_timestamp, &identity.size_of_image))
        identity.pe_identity_valid = 1;

    if (identity.pe_identity_valid && loader_module &&
        dluma_get_module_path_checked(
            loader_module, g_dluma_build_identity_path,
            DLUMA_ARRAY_COUNT(g_dluma_build_identity_path)) &&
        dluma_build_identity_hash_stable_file(
            g_dluma_build_identity_path, identity.pe_timestamp,
            identity.size_of_image, &identity.file_size, identity.sha256)) {
        identity.file_size_valid = 1;
        identity.file_hash_valid = 1;
    }
    identity.complete = (unsigned char)(
        identity.pe_identity_valid && identity.file_size_valid &&
        identity.file_hash_valid);
    g_dluma_build_identity = identity;
    dluma_build_identity_format(
        &g_dluma_build_identity, DLUMA_VERSION, g_dluma_build_identity_text,
        DLUMA_ARRAY_COUNT(g_dluma_build_identity_text));
    dluma_atomic_exchange(&g_dluma_build_identity_initialized, 2);
    return identity.complete ? 1 : 0;
}

const DlumaBuildIdentity* dluma_build_identity_snapshot(void)
{
    return dluma_atomic_compare_exchange(
               &g_dluma_build_identity_initialized, 0, 0) == 2
        ? &g_dluma_build_identity
        : 0;
}

const char* dluma_build_identity_text(void)
{
    return dluma_atomic_compare_exchange(
               &g_dluma_build_identity_initialized, 0, 0) == 2 &&
           g_dluma_build_identity_text[0]
        ? g_dluma_build_identity_text
        : "schema=DLUMA_BUILD_IDENTITY_V1 complete=0";
}

const char* dluma_build_identity_git_head(void)
{
    return DLUMA_BUILD_GIT_HEAD;
}

const char* dluma_build_git_short(void)
{
    return DLUMA_BUILD_GIT_SHORT;
}

int dluma_build_worktree_dirty(void)
{
    return DLUMA_BUILD_WORKTREE_DIRTY ? 1 : 0;
}

void dluma_build_identity_log(void)
{
    char path[DLUMA_MAX_WPATH];
    dluma_log_pair(g_dluma_build_identity.complete ? "INFO" : "WARN",
                 "loader_build_identity", dluma_build_identity_text());
    dluma_log_pair("INFO", "loader_build_git_head",
                 dluma_build_identity_git_head());
    dluma_log_pair("INFO", "loader_build_git_short", dluma_build_git_short());
    dluma_log_u64("INFO", "loader_build_worktree_dirty",
                (ULONGLONG)dluma_build_worktree_dirty());
    dluma_log_pair("INFO", "loader_build_worktree_fingerprint",
                 DLUMA_BUILD_WORKTREE_FINGERPRINT);
    path[0] = 0;
    if (g_dluma_build_identity_path[0] &&
        dluma_wide_to_utf8(g_dluma_build_identity_path, path,
                         DLUMA_ARRAY_COUNT(path)))
        dluma_log_pair("INFO", "loader_module_path", path);
}
