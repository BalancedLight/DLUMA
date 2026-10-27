#include "dlw_runtime_tree.h"

#include "dlw_json_reader.h"
#include "dlw_mod_manifest.h"
#include "../dlw_path_security.h"
#include "../dlw_sha256_internal.h"

#include <stdint.h>
#include <string.h>

#define DLUMA_RUNTIME_MANIFEST_MAX_BYTES (1024U * 1024U)
#define DLUMA_RUNTIME_FILE_MAX_BYTES (256ULL * 1024ULL * 1024ULL)
#define DLUMA_RUNTIME_TREE_MAX_DEPTH 12U

static const WCHAR* const g_dluma_runtime_mutable_root_files[] = {
    L"custom_content_ledger_report.txt",
    L"custom_main_menu_activation.ini",
    L"custom_map_asset_trace.log",
    L"custom_map_asset_trace_summary.txt",
    L"dluma.log",
    L"frontend_content_cold_reboot_report.txt",
    L"frontend_startup_baseline.txt",
    L"global_mods.ini",
    L"lua_discovery_report.txt",
    L"native_mods.ini",
    L"startup_recovery.ini"
};

static const WCHAR* const g_dluma_runtime_mutable_root_directories[] = {
    L"crashes",
    L"markers"
};

typedef struct DlumaRuntimeTreeRecord {
    char path[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    uint64_t size;
} DlumaRuntimeTreeRecord;

typedef struct DlumaRuntimeManifest {
    char runtime_version[32];
    DlumaRuntimeTreeRecord* records;
    unsigned int record_count;
    unsigned int required_mask;
} DlumaRuntimeManifest;

enum DlumaRuntimeRequiredFile {
    DLUMA_RUNTIME_REQUIRED_HOSTFXR = 1U << 0,
    DLUMA_RUNTIME_REQUIRED_CORECLR = 1U << 1,
    DLUMA_RUNTIME_REQUIRED_HOSTPOLICY = 1U << 2,
    DLUMA_RUNTIME_REQUIRED_CORELIB = 1U << 3,
    DLUMA_RUNTIME_REQUIRED_BOOTSTRAP = 1U << 4,
    DLUMA_RUNTIME_REQUIRED_DEPS = 1U << 5,
    DLUMA_RUNTIME_REQUIRED_CONFIG = 1U << 6,
    DLUMA_RUNTIME_REQUIRED_SDK = 1U << 7,
    DLUMA_RUNTIME_REQUIRED_LICENSE = 1U << 8,
    DLUMA_RUNTIME_REQUIRED_NOTICES = 1U << 9,
    DLUMA_RUNTIME_REQUIRED_DOTNET = 1U << 10,
    DLUMA_RUNTIME_REQUIRED_DESKTOP = 1U << 11,
    DLUMA_RUNTIME_REQUIRED_QG_EXE = 1U << 12,
    DLUMA_RUNTIME_REQUIRED_QG_DLL = 1U << 13,
    DLUMA_RUNTIME_REQUIRED_QG_DEPS = 1U << 14,
    DLUMA_RUNTIME_REQUIRED_QG_CONFIG = 1U << 15,
    DLUMA_RUNTIME_REQUIRED_QG_CORE = 1U << 16,
    DLUMA_RUNTIME_REQUIRED_QG_NODIFY = 1U << 17,
    DLUMA_RUNTIME_REQUIRED_QG_NOTICES = 1U << 18,
    DLUMA_RUNTIME_REQUIRED_QG_LICENSE = 1U << 19
};

#define DLUMA_RUNTIME_REQUIRED_ALL ((1U << 20) - 1U)

static void dluma_runtime_tree_error(
    DlumaRuntimeTreeLease* lease,
    const char* message)
{
    if (!lease) return;
    dluma_str_copy(
        lease->diagnostic,
        DLUMA_ARRAY_COUNT(lease->diagnostic),
        message ? message : "private runtime validation failed");
}

static int dluma_runtime_tree_path_under(
    const WCHAR* child,
    const WCHAR* parent)
{
    SIZE_T index;
    SIZE_T parent_length;
    if (!child || !parent) return 0;
    parent_length = wcslen(parent);
    if (!parent_length) return 0;
    for (index = 0; index < parent_length; ++index)
        if (!child[index] ||
            dluma_wascii_lower(child[index]) !=
                dluma_wascii_lower(parent[index]))
            return 0;
    return child[parent_length] == 0 ||
        child[parent_length] == L'\\' ||
        child[parent_length] == L'/';
}

static int dluma_runtime_tree_join(
    WCHAR* output,
    unsigned int capacity,
    const WCHAR* left,
    const WCHAR* right)
{
    SIZE_T left_length;
    SIZE_T right_length;
    if (!output || !capacity || !left || !right)
        return 0;
    left_length = wcslen(left);
    right_length = wcslen(right);
    if (!left_length || !right_length ||
        left_length + right_length + 2U > capacity)
        return 0;
    dluma_path_join(output, capacity, left, right);
    return output[0] &&
        wcslen(output) < (SIZE_T)capacity;
}

static int dluma_runtime_tree_open_root(
    const WCHAR* path,
    WCHAR* final_path,
    unsigned int final_capacity)
{
    HANDLE directory;
    BY_HANDLE_FILE_INFORMATION information;
    directory = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        0);
    if (directory == INVALID_HANDLE_VALUE)
        return 0;
    memset(&information, 0, sizeof(information));
    if (!GetFileInformationByHandle(directory, &information) ||
        !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_get_final_path_checked(
            directory, final_path, final_capacity)) {
        CloseHandle(directory);
        return 0;
    }
    CloseHandle(directory);
    return 1;
}

static HANDLE dluma_runtime_tree_open_file(
    const WCHAR* path,
    const WCHAR* final_root,
    uint64_t expected_size,
    uint64_t maximum_size,
    char sha256[DLUMA_SHA256_HEX_CAPACITY],
    WCHAR* final_path,
    unsigned int final_capacity)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION before;
    BY_HANDLE_FILE_INFORMATION after;
    LARGE_INTEGER size;
    file = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        0,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        0);
    if (file == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    if (!GetFileInformationByHandle(file, &before) ||
        (before.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        before.nNumberOfLinks != 1U ||
        !GetFileSizeEx(file, &size) ||
        size.QuadPart <= 0 ||
        (uint64_t)size.QuadPart > maximum_size ||
        (expected_size && (uint64_t)size.QuadPart != expected_size) ||
        !dluma_get_final_path_checked(file, final_path, final_capacity) ||
        !dluma_runtime_tree_path_under(final_path, final_root) ||
        !dluma_sha256_handle_hex(file, maximum_size, sha256) ||
        !GetFileInformationByHandle(file, &after) ||
        before.dwVolumeSerialNumber != after.dwVolumeSerialNumber ||
        before.nFileIndexHigh != after.nFileIndexHigh ||
        before.nFileIndexLow != after.nFileIndexLow ||
        before.nFileSizeHigh != after.nFileSizeHigh ||
        before.nFileSizeLow != after.nFileSizeLow ||
        before.ftLastWriteTime.dwHighDateTime !=
            after.ftLastWriteTime.dwHighDateTime ||
        before.ftLastWriteTime.dwLowDateTime !=
            after.ftLastWriteTime.dwLowDateTime ||
        before.nNumberOfLinks != after.nNumberOfLinks) {
        CloseHandle(file);
        return INVALID_HANDLE_VALUE;
    }
    return file;
}

static int dluma_runtime_sha256_text(const char* value)
{
    unsigned int index;
    if (!value || strlen(value) != 64U) return 0;
    for (index = 0; index < 64U; ++index)
        if (!((value[index] >= '0' && value[index] <= '9') ||
              (value[index] >= 'a' && value[index] <= 'f') ||
              (value[index] >= 'A' && value[index] <= 'F')))
            return 0;
    return 1;
}

static int dluma_runtime_sha256_equal(
    const char* left,
    const char* right)
{
    unsigned int index;
    if (!dluma_runtime_sha256_text(left) ||
        !dluma_runtime_sha256_text(right))
        return 0;
    for (index = 0; index < 64U; ++index) {
        char left_value = left[index];
        char right_value = right[index];
        if (left_value >= 'A' && left_value <= 'F')
            left_value = (char)(left_value + ('a' - 'A'));
        if (right_value >= 'A' && right_value <= 'F')
            right_value = (char)(right_value + ('a' - 'A'));
        if (left_value != right_value)
            return 0;
    }
    return 1;
}

static int dluma_runtime_string_equal(
    const char* left,
    const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return 0;
    while (left[index] && right[index]) {
        if (left[index] != right[index])
            return 0;
        ++index;
    }
    return left[index] == right[index];
}

static int dluma_runtime_mark(
    unsigned int* fields,
    unsigned int bit,
    DlumaJsonReader* reader)
{
    unsigned int mask = 1U << bit;
    if (!fields || (*fields & mask)) {
        if (reader) reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
        return 0;
    }
    *fields |= mask;
    return 1;
}

static int dluma_runtime_parse_runtime(
    DlumaJsonReader* reader,
    const char* expected_version,
    DlumaRuntimeManifest* output)
{
    unsigned int fields = 0;
    int first = 1;
    if (!dluma_json_object_begin(reader))
        return 0;
    for (;;) {
        char key[32];
        char value[64];
        int next = dluma_json_object_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_json_string(
                reader, key, (unsigned int)sizeof(key)) ||
            !dluma_json_take(reader, ':'))
            return 0;
        if (dluma_runtime_string_equal(key, "deployment")) {
            if (!dluma_runtime_mark(&fields, 0U, reader) ||
                !dluma_json_string(
                    reader, value, (unsigned int)sizeof(value)) ||
                !dluma_runtime_string_equal(
                    value, "private-framework-dependent"))
                return 0;
        } else if (dluma_runtime_string_equal(key, "framework")) {
            if (!dluma_runtime_mark(&fields, 1U, reader) ||
                !dluma_json_string(
                    reader, value, (unsigned int)sizeof(value)) ||
                !dluma_runtime_string_equal(
                    value, "Microsoft.NETCore.App"))
                return 0;
        } else if (dluma_runtime_string_equal(key, "rid")) {
            if (!dluma_runtime_mark(&fields, 2U, reader) ||
                !dluma_json_string(
                    reader, value, (unsigned int)sizeof(value)) ||
                !dluma_runtime_string_equal(value, "win-x64"))
                return 0;
        } else if (dluma_runtime_string_equal(key, "version")) {
            if (!dluma_runtime_mark(&fields, 3U, reader) ||
                !dluma_json_string(
                    reader,
                    output->runtime_version,
                    (unsigned int)sizeof(output->runtime_version)) ||
                !dluma_runtime_string_equal(
                    output->runtime_version, expected_version))
                return 0;
        } else {
            reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
            return 0;
        }
    }
    return dluma_json_object_end(reader) &&
        fields == 0x0FU;
}

static unsigned int dluma_runtime_required_bit(
    const char* path,
    const char* runtime_version)
{
    char expected[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    struct DlumaRequiredSuffix {
        const char* prefix;
        const char* suffix;
        unsigned int bit;
    };
    static const struct DlumaRequiredSuffix versioned[] = {
        {"dotnet/host/fxr/", "/hostfxr.dll",
            DLUMA_RUNTIME_REQUIRED_HOSTFXR},
        {"dotnet/shared/Microsoft.NETCore.App/", "/coreclr.dll",
            DLUMA_RUNTIME_REQUIRED_CORECLR},
        {"dotnet/shared/Microsoft.NETCore.App/", "/hostpolicy.dll",
            DLUMA_RUNTIME_REQUIRED_HOSTPOLICY},
        {"dotnet/shared/Microsoft.NETCore.App/", "/System.Private.CoreLib.dll",
            DLUMA_RUNTIME_REQUIRED_CORELIB},
        {"dotnet/shared/Microsoft.WindowsDesktop.App/",
            "/PresentationFramework.dll",
            DLUMA_RUNTIME_REQUIRED_DESKTOP}
    };
    static const struct {
        const char* path;
        unsigned int bit;
    } fixed[] = {
        {"managed/Dluma.Managed.Bootstrap.dll",
            DLUMA_RUNTIME_REQUIRED_BOOTSTRAP},
        {"managed/Dluma.Managed.Bootstrap.deps.json",
            DLUMA_RUNTIME_REQUIRED_DEPS},
        {"managed/Dluma.Managed.Bootstrap.runtimeconfig.json",
            DLUMA_RUNTIME_REQUIRED_CONFIG},
        {"managed/Dluma.Managed.Sdk.dll",
            DLUMA_RUNTIME_REQUIRED_SDK},
        {"licenses/dotnet/LICENSE.txt",
            DLUMA_RUNTIME_REQUIRED_LICENSE},
        {"licenses/dotnet/ThirdPartyNotices.txt",
            DLUMA_RUNTIME_REQUIRED_NOTICES},
        {"dotnet/dotnet.exe",
            DLUMA_RUNTIME_REQUIRED_DOTNET},
        {"quest-graph/Dluma.QuestGraph.exe",
            DLUMA_RUNTIME_REQUIRED_QG_EXE},
        {"quest-graph/Dluma.QuestGraph.dll",
            DLUMA_RUNTIME_REQUIRED_QG_DLL},
        {"quest-graph/Dluma.QuestGraph.deps.json",
            DLUMA_RUNTIME_REQUIRED_QG_DEPS},
        {"quest-graph/Dluma.QuestGraph.runtimeconfig.json",
            DLUMA_RUNTIME_REQUIRED_QG_CONFIG},
        {"quest-graph/Dluma.QuestGraph.Core.dll",
            DLUMA_RUNTIME_REQUIRED_QG_CORE},
        {"quest-graph/Nodify.dll",
            DLUMA_RUNTIME_REQUIRED_QG_NODIFY},
        {"quest-graph/THIRD_PARTY_NOTICES.md",
            DLUMA_RUNTIME_REQUIRED_QG_NOTICES},
        {"quest-graph/Licenses/Nodify-MIT.txt",
            DLUMA_RUNTIME_REQUIRED_QG_LICENSE}
    };
    unsigned int index;
    for (index = 0; index < DLUMA_ARRAY_COUNT(versioned); ++index) {
        expected[0] = 0;
        dluma_str_append(
            expected, DLUMA_ARRAY_COUNT(expected),
            versioned[index].prefix);
        dluma_str_append(
            expected, DLUMA_ARRAY_COUNT(expected),
            runtime_version);
        dluma_str_append(
            expected, DLUMA_ARRAY_COUNT(expected),
            versioned[index].suffix);
        if (dluma_runtime_string_equal(path, expected))
            return versioned[index].bit;
    }
    for (index = 0; index < DLUMA_ARRAY_COUNT(fixed); ++index)
        if (dluma_runtime_string_equal(
                path, fixed[index].path))
            return fixed[index].bit;
    return 0U;
}

static int dluma_runtime_parse_file(
    DlumaJsonReader* reader,
    DlumaRuntimeTreeRecord* output)
{
    unsigned int fields = 0;
    int first = 1;
    char normalized[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    if (!reader || !output ||
        !dluma_json_object_begin(reader))
        return 0;
    memset(output, 0, sizeof(*output));
    for (;;) {
        char key[32];
        int next = dluma_json_object_next(reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_json_string(
                reader, key, (unsigned int)sizeof(key)) ||
            !dluma_json_take(reader, ':'))
            return 0;
        if (dluma_runtime_string_equal(key, "path")) {
            if (!dluma_runtime_mark(&fields, 0U, reader) ||
                !dluma_json_string(
                    reader,
                    output->path,
                    (unsigned int)sizeof(output->path)))
                return 0;
        } else if (dluma_runtime_string_equal(key, "size")) {
            if (!dluma_runtime_mark(&fields, 1U, reader) ||
                !dluma_json_u64(reader, &output->size))
                return 0;
        } else if (dluma_runtime_string_equal(key, "sha256")) {
            if (!dluma_runtime_mark(&fields, 2U, reader) ||
                !dluma_json_string(
                    reader,
                    output->sha256,
                    (unsigned int)sizeof(output->sha256)))
                return 0;
        } else {
            reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
            return 0;
        }
    }
    if (!dluma_json_object_end(reader) ||
        fields != 0x07U ||
        !output->size ||
        output->size > DLUMA_RUNTIME_FILE_MAX_BYTES ||
        !dluma_runtime_sha256_text(output->sha256) ||
        !dluma_mod_relative_path_validate_and_normalize(
            output->path,
            normalized,
            (unsigned int)sizeof(normalized)) ||
        !dluma_runtime_string_equal(output->path, normalized) ||
        dluma_runtime_string_equal(
            output->path, "runtime-manifest.json"))
        return 0;
    return 1;
}

static int dluma_runtime_parse_files(
    DlumaJsonReader* reader,
    DlumaRuntimeManifest* output)
{
    int first = 1;
    if (!reader || !output ||
        !dluma_json_array_begin(reader))
        return 0;
    for (;;) {
        int next = dluma_json_array_next(reader, &first);
        DlumaRuntimeTreeRecord* record;
        if (next < 0) return 0;
        if (!next) break;
        if (output->record_count >=
            DLUMA_RUNTIME_TREE_MAX_FILES) {
            reader->status = DLUMA_STATUS_LIMIT_EXCEEDED;
            return 0;
        }
        record = &output->records[
            output->record_count];
        if (!dluma_runtime_parse_file(reader, record))
            return 0;
        if (output->record_count &&
            dluma_stricmp(
                output->records[
                    output->record_count - 1U].path,
                record->path) >= 0) {
            reader->status = DLUMA_STATUS_INVALID_ARGUMENT;
            return 0;
        }
        ++output->record_count;
    }
    return dluma_json_array_end(reader) &&
        output->record_count > 0U;
}

static int dluma_runtime_manifest_parse(
    const unsigned char* bytes,
    unsigned int size,
    const char* expected_version,
    const char* expected_catalog_sha256,
    DlumaRuntimeManifest* output)
{
    DlumaJsonReader reader;
    unsigned int fields = 0;
    int first = 1;
    if (!bytes || !size || !expected_version ||
        !expected_catalog_sha256 || !output ||
        !output->records)
        return 0;
    dluma_json_reader_initialize(&reader, bytes, size);
    if (!dluma_json_object_begin(&reader))
        return 0;
    for (;;) {
        char key[32];
        int next = dluma_json_object_next(&reader, &first);
        if (next < 0) return 0;
        if (!next) break;
        if (!dluma_json_string(
                &reader, key, (unsigned int)sizeof(key)) ||
            !dluma_json_take(&reader, ':'))
            return 0;
        if (dluma_runtime_string_equal(key, "schema")) {
            char value[32];
            if (!dluma_runtime_mark(&fields, 0U, &reader) ||
                !dluma_json_string(
                    &reader, value, (unsigned int)sizeof(value)) ||
                !dluma_runtime_string_equal(
                    value, "dluma.runtime-tree.v1"))
                return 0;
        } else if (dluma_runtime_string_equal(
                key, "api_version")) {
            char value[16];
            if (!dluma_runtime_mark(&fields, 1U, &reader) ||
                !dluma_json_string(
                    &reader, value, (unsigned int)sizeof(value)) ||
                !dluma_runtime_string_equal(value, "1.0"))
                return 0;
        } else if (dluma_runtime_string_equal(
                key, "catalog_sha256")) {
            char value[DLUMA_SHA256_HEX_CAPACITY];
            if (!dluma_runtime_mark(&fields, 2U, &reader) ||
                !dluma_json_string(
                    &reader, value, (unsigned int)sizeof(value)) ||
                !dluma_runtime_string_equal(
                    value, expected_catalog_sha256))
                return 0;
        } else if (dluma_runtime_string_equal(key, "runtime")) {
            if (!dluma_runtime_mark(&fields, 3U, &reader) ||
                !dluma_runtime_parse_runtime(
                    &reader, expected_version, output))
                return 0;
        } else if (dluma_runtime_string_equal(key, "files")) {
            if (!dluma_runtime_mark(&fields, 4U, &reader) ||
                !dluma_runtime_parse_files(&reader, output))
                return 0;
        } else {
            reader.status = DLUMA_STATUS_INVALID_ARGUMENT;
            return 0;
        }
    }
    return dluma_json_object_end(&reader) &&
        dluma_json_finished(&reader) &&
        fields == 0x1FU;
}

static int dluma_runtime_tree_mutable_root_entry(
    const WCHAR* name,
    DWORD attributes)
{
    const WCHAR* const* entries;
    unsigned int count;
    unsigned int index;
    int is_directory;
    if (!name || !name[0]) return 0;
    is_directory =
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (is_directory) {
        entries = g_dluma_runtime_mutable_root_directories;
        count = DLUMA_ARRAY_COUNT(
            g_dluma_runtime_mutable_root_directories);
    } else {
        entries = g_dluma_runtime_mutable_root_files;
        count = DLUMA_ARRAY_COUNT(
            g_dluma_runtime_mutable_root_files);
    }
    for (index = 0; index < count; ++index)
        if (dluma_wstricmp(name, entries[index]) == 0)
            return 1;
    return 0;
}

static int dluma_runtime_tree_count_files(
    const WCHAR* directory,
    unsigned int depth,
    unsigned int* count)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WCHAR child[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    if (!directory || !count ||
        depth > DLUMA_RUNTIME_TREE_MAX_DEPTH ||
        !dluma_runtime_tree_join(
            pattern, DLUMA_ARRAY_COUNT(pattern),
            directory, L"*"))
        return 0;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    do {
        if ((data.cFileName[0] == L'.' &&
             !data.cFileName[1]) ||
            (data.cFileName[0] == L'.' &&
             data.cFileName[1] == L'.' &&
             !data.cFileName[2]))
            continue;
        if (data.dwFileAttributes &
            FILE_ATTRIBUTE_REPARSE_POINT) {
            FindClose(find);
            return 0;
        }
        if (depth == 0U &&
            dluma_runtime_tree_mutable_root_entry(
                data.cFileName,
                data.dwFileAttributes))
            continue;
        if (!dluma_runtime_tree_join(
                child, DLUMA_ARRAY_COUNT(child),
                directory, data.cFileName)) {
            FindClose(find);
            return 0;
        }
        if (data.dwFileAttributes &
            FILE_ATTRIBUTE_DIRECTORY) {
            if (!dluma_runtime_tree_count_files(
                    child, depth + 1U, count)) {
                FindClose(find);
                return 0;
            }
        } else {
            ++*count;
            if (*count >
                DLUMA_RUNTIME_TREE_MAX_FILES + 1U) {
                FindClose(find);
                return 0;
            }
        }
    } while (FindNextFileW(find, &data));
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        FindClose(find);
        return 0;
    }
    FindClose(find);
    return 1;
}

void dluma_runtime_tree_release(DlumaRuntimeTreeLease* lease)
{
    unsigned int index;
    if (!lease) return;
    if (lease->files) {
        for (index = 0; index < lease->file_count; ++index)
            if (lease->files[index] &&
                lease->files[index] != INVALID_HANDLE_VALUE)
                CloseHandle(lease->files[index]);
        dluma_free(lease->files);
    }
    memset(lease, 0, sizeof(*lease));
    lease->struct_size =
        (unsigned int)sizeof(*lease);
}

int dluma_runtime_tree_acquire(
    const WCHAR* runtime_root,
    const char* expected_runtime_version,
    const char* expected_catalog_sha256,
    DlumaRuntimeTreeLease* lease)
{
    WCHAR final_root[DLUMA_MAX_WPATH];
    WCHAR manifest_path[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    HANDLE manifest_file = INVALID_HANDLE_VALUE;
    LARGE_INTEGER manifest_size;
    DWORD read_size = 0;
    LARGE_INTEGER zero;
    unsigned char* manifest_bytes = 0;
    DlumaRuntimeManifest manifest;
    unsigned int actual_file_count = 0;
    unsigned int index;
    if (!runtime_root || !expected_runtime_version ||
        !expected_catalog_sha256 || !lease ||
        lease->struct_size < sizeof(*lease))
        return 0;
    memset(lease, 0, sizeof(*lease));
    lease->struct_size =
        (unsigned int)sizeof(*lease);
    memset(&manifest, 0, sizeof(manifest));
    manifest.records = (DlumaRuntimeTreeRecord*)dluma_alloc(
        (SIZE_T)DLUMA_RUNTIME_TREE_MAX_FILES *
        sizeof(*manifest.records));
    lease->files = (HANDLE*)dluma_alloc(
        (SIZE_T)(DLUMA_RUNTIME_TREE_MAX_FILES + 1U) *
        sizeof(*lease->files));
    if (!manifest.records || !lease->files) {
        dluma_runtime_tree_error(
            lease, "private runtime validation allocation failed");
        goto fail;
    }
    memset(
        manifest.records, 0,
        (SIZE_T)DLUMA_RUNTIME_TREE_MAX_FILES *
        sizeof(*manifest.records));
    memset(
        lease->files, 0,
        (SIZE_T)(DLUMA_RUNTIME_TREE_MAX_FILES + 1U) *
        sizeof(*lease->files));
    if (!dluma_runtime_tree_open_root(
            runtime_root,
            final_root,
            DLUMA_ARRAY_COUNT(final_root)) ||
        !dluma_runtime_tree_join(
            manifest_path,
            DLUMA_ARRAY_COUNT(manifest_path),
            runtime_root,
            L"runtime-manifest.json")) {
        dluma_runtime_tree_error(
            lease, "private runtime root is missing or unsafe");
        goto fail;
    }
    manifest_file = dluma_runtime_tree_open_file(
        manifest_path,
        final_root,
        0U,
        DLUMA_RUNTIME_MANIFEST_MAX_BYTES,
        lease->manifest_sha256,
        final_path,
        DLUMA_ARRAY_COUNT(final_path));
    if (manifest_file == INVALID_HANDLE_VALUE ||
        !GetFileSizeEx(manifest_file, &manifest_size) ||
        manifest_size.QuadPart <= 0 ||
        manifest_size.QuadPart >
            DLUMA_RUNTIME_MANIFEST_MAX_BYTES) {
        dluma_runtime_tree_error(
            lease, "private runtime manifest is missing or unsafe");
        goto fail;
    }
    manifest_bytes = (unsigned char*)dluma_alloc(
        (SIZE_T)manifest_size.QuadPart);
    zero.QuadPart = 0;
    if (!manifest_bytes ||
        !SetFilePointerEx(
            manifest_file, zero, 0, FILE_BEGIN) ||
        !ReadFile(
            manifest_file,
            manifest_bytes,
            (DWORD)manifest_size.QuadPart,
            &read_size,
            0) ||
        read_size != (DWORD)manifest_size.QuadPart) {
        dluma_runtime_tree_error(
            lease, "private runtime manifest could not be read");
        goto fail;
    }
    if (!dluma_runtime_manifest_parse(
            manifest_bytes,
            (unsigned int)manifest_size.QuadPart,
            expected_runtime_version,
            expected_catalog_sha256,
            &manifest)) {
        dluma_runtime_tree_error(
            lease, "private runtime manifest is malformed or stale");
        goto fail;
    }
    lease->files[lease->file_count++] =
        manifest_file;
    manifest_file = INVALID_HANDLE_VALUE;
    for (index = 0;
         index < manifest.record_count;
         ++index) {
        DlumaRuntimeTreeRecord* record =
            &manifest.records[index];
        WCHAR relative[DLUMA_MAX_WPATH];
        WCHAR path[DLUMA_MAX_WPATH];
        char actual_sha256[DLUMA_SHA256_HEX_CAPACITY];
        HANDLE file;
        unsigned int character;
        if (!dluma_utf8_to_wide(
                record->path,
                relative,
                DLUMA_ARRAY_COUNT(relative)))
            goto file_invalid;
        for (character = 0; relative[character]; ++character)
            if (relative[character] == L'/')
                relative[character] = L'\\';
        if (!dluma_runtime_tree_join(
                path, DLUMA_ARRAY_COUNT(path),
                runtime_root, relative))
            goto file_invalid;
        file = dluma_runtime_tree_open_file(
            path,
            final_root,
            record->size,
            DLUMA_RUNTIME_FILE_MAX_BYTES,
            actual_sha256,
            final_path,
            DLUMA_ARRAY_COUNT(final_path));
        if (file == INVALID_HANDLE_VALUE ||
            !dluma_runtime_sha256_equal(
                actual_sha256, record->sha256)) {
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
            goto file_invalid;
        }
        lease->files[lease->file_count++] = file;
        manifest.required_mask |=
            dluma_runtime_required_bit(
                record->path,
                manifest.runtime_version);
        continue;
file_invalid:
        dluma_runtime_tree_error(
            lease, "private runtime file changed, escaped, or is unsafe");
        goto fail;
    }
    if (manifest.required_mask !=
            DLUMA_RUNTIME_REQUIRED_ALL ||
        !dluma_runtime_tree_count_files(
            runtime_root, 0U, &actual_file_count) ||
        actual_file_count !=
            manifest.record_count + 1U) {
        dluma_runtime_tree_error(
            lease, "private runtime tree is incomplete or has unlisted files");
        goto fail;
    }
    dluma_free(manifest_bytes);
    dluma_free(manifest.records);
    dluma_str_copy(
        lease->diagnostic,
        DLUMA_ARRAY_COUNT(lease->diagnostic),
        "private runtime tree verified");
    return 1;

fail:
    if (manifest_file != INVALID_HANDLE_VALUE)
        CloseHandle(manifest_file);
    dluma_free(manifest_bytes);
    dluma_free(manifest.records);
    {
        char diagnostic[DLUMA_RUNTIME_TREE_DIAGNOSTIC_CAPACITY];
        dluma_str_copy(
            diagnostic,
            DLUMA_ARRAY_COUNT(diagnostic),
            lease->diagnostic);
        dluma_runtime_tree_release(lease);
        dluma_str_copy(
            lease->diagnostic,
            DLUMA_ARRAY_COUNT(lease->diagnostic),
            diagnostic);
    }
    return 0;
}
