#include "dlw_asset_mod_runtime.h"

#include "dlw_path_security.h"
#include "dlw_sha256_internal.h"

#ifndef FILE_FLAG_SEQUENTIAL_SCAN
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000UL
#endif
#ifndef ERROR_ALREADY_EXISTS
#define ERROR_ALREADY_EXISTS 183UL
#endif

__declspec(dllimport) BOOL WINAPI CopyFileW(
    LPCWSTR existing_file, LPCWSTR new_file, BOOL fail_if_exists);
__declspec(dllimport) BOOL WINAPI CreateHardLinkW(
    LPCWSTR new_file, LPCWSTR existing_file, void *security_attributes);

#define DLUMA_ASSET_RUNTIME_SOURCE_COUNT 1U
#define DLUMA_ASSET_RUNTIME_MAX_FILES 250000U
#define DLUMA_ASSET_RUNTIME_MAX_BYTES (64ULL * 1024ULL * 1024ULL * 1024ULL)
#define DLUMA_ASSET_RUNTIME_MAX_DEPTH 64U

typedef struct DlumaAssetRuntimeSource {
    DlumaProcessLifetimeSourceRecord record;
    HANDLE directory;
    unsigned char registered;
} DlumaAssetRuntimeSource;

typedef struct DlumaAssetRuntimeStage {
    WCHAR root[DLUMA_MAX_WPATH];
    char alias[128];
    void *namespace_source;
    char fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    ULONGLONG revision;
    ULONGLONG bytes;
    unsigned int files;
    unsigned int enabled_count;
    unsigned char ready;
} DlumaAssetRuntimeStage;

static DlumaAssetModNativeAdapter g_dluma_asset_native;
static DlumaAssetRuntimeStage g_dluma_asset_prepared;
static DlumaAssetRuntimeStage g_dluma_asset_active;
static PVOID volatile g_dluma_asset_active_view;
static DlumaAssetRuntimeSource g_dluma_asset_sources[DLUMA_ASSET_RUNTIME_SOURCE_COUNT];
static volatile LONG g_dluma_asset_runtime_busy;
static ULONGLONG g_dluma_asset_stage_sequence;
static const char *g_dluma_asset_source_ids[DLUMA_ASSET_RUNTIME_SOURCE_COUNT] = {
    "dluma.asset.namespace"
};

static const WCHAR *dluma_asset_runtime_plain_path(const WCHAR *path)
{
    return path && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] == L'?' && path[3] == L'\\' ? path + 4U : path;
}

static int dluma_asset_runtime_write(HANDLE file, const void *data, DWORD size)
{
    DWORD written = 0;
    return WriteFile(file, data, size, &written, 0) && written == size;
}

static int dluma_asset_runtime_directory(const WCHAR *path, HANDLE *handle_out,
                                      WCHAR *canonical, unsigned int capacity)
{
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE directory = CreateFileW(path, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (directory == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileInformationByHandle(directory, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_get_final_path_checked(directory, canonical, capacity)) {
        CloseHandle(directory);
        return 0;
    }
    *handle_out = directory;
    return 1;
}

static int dluma_asset_runtime_under(const WCHAR *path, const WCHAR *root)
{
    SIZE_T length = wcslen(root);
    SIZE_T index;
    if (!length) return 0;
    for (index = 0; index < length; ++index) {
        WCHAR left = dluma_wascii_lower(path[index]);
        WCHAR right = dluma_wascii_lower(root[index]);
        if (!left || left != right) return 0;
    }
    return path[length] == L'\\' || path[length] == L'/';
}

static int dluma_asset_runtime_copy_file(const WCHAR *source,
    const WCHAR *destination, const WCHAR *provider_root,
    const WCHAR *relative, HANDLE manifest, DlumaAssetRuntimeStage *stage)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
    LARGE_INTEGER size;
    WCHAR final_path[DLUMA_MAX_WPATH];
    char digest[DLUMA_SHA256_HEX_CAPACITY];
    char path_utf8[DLUMA_MAX_WPATH * 2U];
    WCHAR alias_wide[128], alias_root[DLUMA_MAX_WPATH];
    WCHAR alias_path[DLUMA_MAX_WPATH], parent[DLUMA_MAX_WPATH];
    if (GetFileAttributesW(destination) != INVALID_FILE_ATTRIBUTES)
        return 1;
    file = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ, 0,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, 0);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        stage->files >= DLUMA_ASSET_RUNTIME_MAX_FILES ||
        (ULONGLONG)size.QuadPart > DLUMA_ASSET_RUNTIME_MAX_BYTES - stage->bytes ||
        !dluma_get_final_path_checked(file, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
        !dluma_asset_runtime_under(final_path, provider_root) ||
        !dluma_sha256_handle_hex(file, DLUMA_ASSET_RUNTIME_MAX_BYTES, digest) ||
        !dluma_wide_to_utf8(relative, path_utf8, DLUMA_ARRAY_COUNT(path_utf8)) ||
        !CopyFileW(source, destination, TRUE) ||
        !dluma_asset_runtime_write(manifest, path_utf8, (DWORD)strlen(path_utf8) + 1U) ||
        !dluma_asset_runtime_write(manifest, digest, sizeof(digest))) {
        CloseHandle(file);
        return 0;
    }
    {
        const char *private_alias = stage->alias;
        while (*private_alias && *private_alias != '/') ++private_alias;
        if (*private_alias == '/') ++private_alias;
        if (!dluma_utf8_to_wide(private_alias, alias_wide, DLUMA_ARRAY_COUNT(alias_wide))) {
            CloseHandle(file);
            return 0;
        }
    }
    dluma_path_join(alias_root, DLUMA_ARRAY_COUNT(alias_root), stage->root, alias_wide);
    dluma_path_join(alias_path, DLUMA_ARRAY_COUNT(alias_path), alias_root, relative);
    dluma_wstr_copy(parent, DLUMA_ARRAY_COUNT(parent), alias_path);
    dluma_path_dirname(parent);
    dluma_mkdirs(parent);
    if (!CreateHardLinkW(alias_path, destination, 0) &&
        !CopyFileW(destination, alias_path, TRUE)) {
        CloseHandle(file);
        return 0;
    }
    stage->bytes += (ULONGLONG)size.QuadPart;
    ++stage->files;
    CloseHandle(file);
    return 1;
}

static int dluma_asset_runtime_copy_tree(const WCHAR *source_root,
    const WCHAR *stage_root, const WCHAR *provider_root,
    const WCHAR *relative, unsigned int depth, HANDLE manifest,
    DlumaAssetRuntimeStage *stage)
{
    WCHAR source[DLUMA_MAX_WPATH], destination[DLUMA_MAX_WPATH];
    WCHAR pattern[DLUMA_MAX_WPATH], canonical[DLUMA_MAX_WPATH];
    HANDLE directory = INVALID_HANDLE_VALUE;
    HANDLE find = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW entry;
    int result = 0;
    if (depth > DLUMA_ASSET_RUNTIME_MAX_DEPTH ||
        wcslen(source_root) + wcslen(relative) + 4U >= DLUMA_MAX_WPATH ||
        wcslen(stage_root) + wcslen(relative) + 4U >= DLUMA_MAX_WPATH)
        return 0;
    dluma_path_join(source, DLUMA_ARRAY_COUNT(source), source_root, relative);
    dluma_path_join(destination, DLUMA_ARRAY_COUNT(destination), stage_root, relative);
    if (!dluma_asset_runtime_directory(source, &directory, canonical,
            DLUMA_ARRAY_COUNT(canonical)) ||
        !dluma_asset_runtime_under(canonical, provider_root))
        goto done;
    if (!CreateDirectoryW(destination, 0) && GetLastError() != ERROR_ALREADY_EXISTS)
        goto done;
    if ((GetFileAttributesW(destination) & FILE_ATTRIBUTE_REPARSE_POINT) != 0U)
        goto done;
    dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), source, L"*");
    find = FindFirstFileW(pattern, &entry);
    if (find == INVALID_HANDLE_VALUE) {
        result = GetLastError() == ERROR_FILE_NOT_FOUND;
        goto done;
    }
    do {
        WCHAR next_relative[DLUMA_MAX_WPATH];
        WCHAR next_source[DLUMA_MAX_WPATH], next_destination[DLUMA_MAX_WPATH];
        if (dluma_wstricmp(entry.cFileName, L".") == 0 || dluma_wstricmp(entry.cFileName, L"..") == 0)
            continue;
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            wcslen(relative) + wcslen(entry.cFileName) + 2U >= DLUMA_MAX_WPATH)
            goto done;
        dluma_path_join(next_relative, DLUMA_ARRAY_COUNT(next_relative), relative, entry.cFileName);
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!dluma_asset_runtime_copy_tree(source_root, stage_root,
                    provider_root, next_relative, depth + 1U, manifest, stage))
                goto done;
        } else {
            dluma_path_join(next_source, DLUMA_ARRAY_COUNT(next_source), source_root, next_relative);
            dluma_path_join(next_destination, DLUMA_ARRAY_COUNT(next_destination), stage_root, next_relative);
            if (!dluma_asset_runtime_copy_file(next_source, next_destination,
                    provider_root, next_relative, manifest, stage))
                goto done;
        }
    } while (FindNextFileW(find, &entry));
    result = GetLastError() == ERROR_NO_MORE_FILES;
done:
    if (find != INVALID_HANDLE_VALUE) FindClose(find);
    if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
    return result;
}

int dluma_asset_mod_runtime_prepare(void)
{
    DlumaAssetModSourceSnapshot *sources;
    DlumaAssetRuntimeStage stage;
    WCHAR cache[DLUMA_MAX_WPATH], name[96], manifest_path[DLUMA_MAX_WPATH];
    WCHAR module_canonical[DLUMA_MAX_WPATH], cache_canonical[DLUMA_MAX_WPATH];
    WCHAR stage_canonical[DLUMA_MAX_WPATH];
    HANDLE module_directory = INVALID_HANDLE_VALUE;
    HANDLE cache_directory = INVALID_HANDLE_VALUE;
    HANDLE stage_directory = INVALID_HANDLE_VALUE;
    HANDLE manifest = INVALID_HANDLE_VALUE;
    unsigned int count, index;
    ULONGLONG revision = 0;
    int result = 0;
    if (g_dluma_asset_prepared.ready && g_dluma_asset_prepared.revision ==
            dluma_asset_mods_desired_revision())
        return 1;
    sources = (DlumaAssetModSourceSnapshot*)dluma_alloc(DLUMA_ASSET_MOD_MAX_ITEMS * sizeof(*sources));
    if (!sources) return 0;
    count = dluma_asset_mods_snapshot_sources(sources, DLUMA_ASSET_MOD_MAX_ITEMS, &revision);
    memset(&stage, 0, sizeof(stage));
    stage.revision = revision;
    stage.enabled_count = count;
    dluma_path_join(cache, DLUMA_ARRAY_COUNT(cache), g_dluma.module_dir, L"dluma_runtime\\asset_mods");
    dluma_mkdirs(cache);
    if (!dluma_asset_runtime_directory(g_dluma.module_dir, &module_directory,
            module_canonical, DLUMA_ARRAY_COUNT(module_canonical)) ||
        !dluma_asset_runtime_directory(cache, &cache_directory,
            cache_canonical, DLUMA_ARRAY_COUNT(cache_canonical)) ||
        !dluma_asset_runtime_under(cache_canonical, module_canonical))
        goto done;
    {
        char suffix[96], number[32];
        dluma_str_copy(suffix, DLUMA_ARRAY_COUNT(suffix), "stack_");
        dluma_u64_to_text(GetCurrentProcessId(), number, DLUMA_ARRAY_COUNT(number));
        dluma_str_append(suffix, DLUMA_ARRAY_COUNT(suffix), number);
        dluma_str_append(suffix, DLUMA_ARRAY_COUNT(suffix), "_");
        dluma_u64_to_text(revision, number, DLUMA_ARRAY_COUNT(number));
        dluma_str_append(suffix, DLUMA_ARRAY_COUNT(suffix), number);
        dluma_str_append(suffix, DLUMA_ARRAY_COUNT(suffix), "_");
        dluma_u64_to_text(++g_dluma_asset_stage_sequence, number, DLUMA_ARRAY_COUNT(number));
        dluma_str_append(suffix, DLUMA_ARRAY_COUNT(suffix), number);
        if (!dluma_utf8_to_wide(suffix, name, DLUMA_ARRAY_COUNT(name))) goto done;
        dluma_str_copy(stage.alias, DLUMA_ARRAY_COUNT(stage.alias), suffix);
        dluma_str_append(stage.alias, DLUMA_ARRAY_COUNT(stage.alias), "/__dluma_assets_");
        dluma_str_append(stage.alias, DLUMA_ARRAY_COUNT(stage.alias), suffix);
    }
    dluma_path_join(stage.root, DLUMA_ARRAY_COUNT(stage.root), cache, name);
    if (!CreateDirectoryW(stage.root, 0) ||
        !dluma_asset_runtime_directory(stage.root, &stage_directory,
            stage_canonical, DLUMA_ARRAY_COUNT(stage_canonical)) ||
        !dluma_asset_runtime_under(stage_canonical, cache_canonical))
        goto done;
    dluma_path_join(manifest_path, DLUMA_ARRAY_COUNT(manifest_path), stage.root, L"stack.index");
    manifest = CreateFileW(manifest_path, GENERIC_READ | GENERIC_WRITE, 0, 0,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);
    if (manifest == INVALID_HANDLE_VALUE) goto done;
    for (index = 0; index < count; ++index) {
        WCHAR canonical[DLUMA_MAX_WPATH];
        HANDLE directory = INVALID_HANDLE_VALUE;
        unsigned int tree;
        static const WCHAR *trees[] = {L"data", L"assets_pc"};
        if (!dluma_asset_runtime_directory(sources[index].project_root,
                &directory, canonical, DLUMA_ARRAY_COUNT(canonical)))
            goto done;
        if (!dluma_asset_runtime_write(manifest, sources[index].id,
                (DWORD)strlen(sources[index].id) + 1U) ||
            !dluma_asset_runtime_write(manifest, sources[index].fingerprint,
                sizeof(sources[index].fingerprint))) {
            CloseHandle(directory);
            goto done;
        }
        for (tree = 0; tree < DLUMA_ARRAY_COUNT(trees); ++tree) {
            WCHAR path[DLUMA_MAX_WPATH];
            DWORD attributes;
            dluma_path_join(path, DLUMA_ARRAY_COUNT(path), sources[index].project_root, trees[tree]);
            attributes = GetFileAttributesW(path);
            if (attributes == INVALID_FILE_ATTRIBUTES) continue;
            if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                !dluma_asset_runtime_copy_tree(sources[index].project_root,
                    stage.root, canonical, trees[tree], 0, manifest, &stage)) {
                CloseHandle(directory);
                goto done;
            }
        }
        CloseHandle(directory);
    }
    if (!FlushFileBuffers(manifest) ||
        !dluma_sha256_handle_hex(manifest, 128ULL * 1024ULL * 1024ULL,
            stage.fingerprint) || revision != dluma_asset_mods_desired_revision())
        goto done;
    stage.ready = 1;
    g_dluma_asset_prepared = stage;
    result = 1;
done:
    if (manifest != INVALID_HANDLE_VALUE) CloseHandle(manifest);
    if (stage_directory != INVALID_HANDLE_VALUE) CloseHandle(stage_directory);
    if (cache_directory != INVALID_HANDLE_VALUE) CloseHandle(cache_directory);
    if (module_directory != INVALID_HANDLE_VALUE) CloseHandle(module_directory);
    dluma_free(sources);
    if (!result) dluma_log_pair("WARN", "asset_mod_stack_prepare", "failed");
    return result;
}

void dluma_asset_mod_runtime_set_adapter(const DlumaAssetModNativeAdapter *adapter)
{
    if (adapter) g_dluma_asset_native = *adapter;
}

static int dluma_asset_runtime_mount_namespace(void)
{
    DlumaAssetRuntimeSource *state = &g_dluma_asset_sources[0];
    DlumaProcessLifetimeSourceRecord record;
    WCHAR path[DLUMA_MAX_WPATH], canonical[DLUMA_MAX_WPATH];
    HANDLE directory = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info;
    if (state->registered) return 1;
    if (!g_dluma_asset_native.mount_directory) return 0;
    memset(&record, 0, sizeof(record));
    dluma_path_join(path, DLUMA_ARRAY_COUNT(path), g_dluma.module_dir, L"dluma_runtime\\asset_mods");
    dluma_mkdirs(path);
    if (!dluma_asset_runtime_directory(path, &directory, canonical, DLUMA_ARRAY_COUNT(canonical)) ||
        !GetFileInformationByHandle(directory, &info) ||
        !dluma_wide_to_utf8(dluma_asset_runtime_plain_path(canonical),
            record.requested_mount_path, DLUMA_ARRAY_COUNT(record.requested_mount_path))) {
        if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
        return 0;
    }
    record.owner_kind = DLUMA_SOURCE_OWNER_ASSET_MOD;
    dluma_str_copy(record.owner_id, DLUMA_ARRAY_COUNT(record.owner_id), g_dluma_asset_source_ids[0]);
    record.runtime_source_type = DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY;
    record.source_flags = 8U;
    record.volume_serial = info.dwVolumeSerialNumber;
    record.file_index_high = info.nFileIndexHigh;
    record.file_index_low = info.nFileIndexLow;
    record.metadata_complete = 1;
    dluma_sha256_bytes_hex(record.requested_mount_path,
        strlen(record.requested_mount_path), record.content_sha256);
    if (!g_dluma_asset_native.mount_directory(record.requested_mount_path, record.source_flags,
            &record.source_object) || !record.source_object ||
        !dluma_process_lifetime_source_register(&record, 0)) {
        /* A successfully published source remains pinned if registration
         * fails; project admission reports the failure before any load. */
        state->directory = directory;
        state->record = record;
        return 0;
    }
    state->directory = directory;
    state->record = record;
    state->registered = 1;
    return 1;
}

int dluma_asset_mod_runtime_apply(int consumers_released)
{
    int result = 0;
    DlumaAssetRuntimeStage *published = 0;
    ULONGLONG desired = dluma_asset_mods_desired_revision();
    if (!g_dluma_asset_native.mount_directory)
        return dluma_asset_mods_snapshot_sources(0, 0, &desired) == 0
            ? dluma_asset_mods_commit_effective(desired) : 0;
    if (!dluma_asset_runtime_mount_namespace()) return 0;
    if (g_dluma_asset_active.ready && desired == g_dluma_asset_active.revision)
        return 1;
    if (!consumers_released ||
        dluma_atomic_compare_exchange(&g_dluma_asset_runtime_busy, 1, 0) != 0)
        return 0;
    if (!dluma_asset_mod_runtime_prepare()) goto done;
    if (!g_dluma_asset_native.refresh_namespace ||
        !g_dluma_asset_native.refresh_namespace(g_dluma_asset_sources[0].record.requested_mount_path))
        goto done;
    published = (DlumaAssetRuntimeStage*)dluma_alloc(sizeof(*published));
    if (!published) goto done;
    *published = g_dluma_asset_prepared;
    published->namespace_source = g_dluma_asset_sources[0].record.source_object;
    if (!dluma_asset_mods_commit_effective(g_dluma_asset_prepared.revision)) goto done;
    /* All generations share the same registered namespace. Existing native
     * consumers retain their source identity and frozen files while the new
     * resolver starts selecting this generation for subsequent loads. */
    g_dluma_asset_active = g_dluma_asset_prepared;
    /* Readers capture one complete generation, including the actual source.
     * Older snapshots remain available with their generation's frozen files. */
    (void)dluma_atomic_pointer_exchange(&g_dluma_asset_active_view, published);
    published = 0;
    result = 1;
    dluma_log_u64("INFO", "asset_mod_stack_applied_revision", g_dluma_asset_active.revision);
    dluma_log_u64("INFO", "asset_mod_stack_file_count", g_dluma_asset_active.files);
done:
    if (published) dluma_free(published);
    dluma_atomic_exchange(&g_dluma_asset_runtime_busy, 0);
    return result;
}
int dluma_asset_mod_runtime_validate_source(const DlumaProcessLifetimeSourceRecord *record)
{
    unsigned int index;
    if (!record || record->owner_kind != DLUMA_SOURCE_OWNER_ASSET_MOD)
        return 0;
    for (index = 0; index < DLUMA_ASSET_RUNTIME_SOURCE_COUNT; ++index) {
        const DlumaAssetRuntimeSource *state = &g_dluma_asset_sources[index];
        if (state->registered && state->record.source_object == record->source_object &&
            state->record.source_flags == record->source_flags &&
            dluma_stricmp(state->record.owner_id, record->owner_id) == 0 &&
            dluma_stricmp(state->record.requested_mount_path, record->requested_mount_path) == 0 &&
            dluma_stricmp(state->record.content_sha256, record->content_sha256) == 0 &&
            state->record.volume_serial == record->volume_serial &&
            state->record.file_index_high == record->file_index_high &&
            state->record.file_index_low == record->file_index_low)
            return 1;
    }
    return 0;
}

int dluma_asset_mod_runtime_source_owned(void *source)
{
    unsigned int index;
    if (!source) return 0;
    for (index = 0; index < DLUMA_ASSET_RUNTIME_SOURCE_COUNT; ++index)
        if (g_dluma_asset_sources[index].registered &&
            g_dluma_asset_sources[index].record.source_object == source)
            return 1;
    return 0;
}

int dluma_asset_mod_runtime_pending(void)
{
    return dluma_asset_mods_desired_revision() != dluma_asset_mods_effective_revision();
}

int dluma_asset_mod_runtime_has_overrides(void)
{
    const DlumaAssetRuntimeStage *active =
        (const DlumaAssetRuntimeStage*)dluma_atomic_pointer_load(&g_dluma_asset_active_view);
    return active && active->ready && active->enabled_count && active->namespace_source;
}

static int dluma_asset_runtime_resolve_in_source(const char *path, const char *source_root,
    char *alias_out, unsigned int alias_capacity,
    char *physical_out, unsigned int physical_capacity, void **source_out)
{
    char normalized[DLUMA_MAX_WPATH * 2U], relative[DLUMA_MAX_WPATH * 2U];
    unsigned int length = 0, start = 0, index, candidate;
    int absolute = 0, payload_kind = 0;
    static const char *prefixes[] = {"", "assets_pc/", "data/"};
    const DlumaAssetRuntimeStage *active =
        (const DlumaAssetRuntimeStage*)dluma_atomic_pointer_load(&g_dluma_asset_active_view);
    if (alias_out && alias_capacity) alias_out[0] = 0;
    if (physical_out && physical_capacity) physical_out[0] = 0;
    if (source_out) *source_out = 0;
    if (!path || !path[0] || !active || !active->ready ||
        !active->enabled_count || !active->namespace_source)
        return 0;
    while (path[length] && length + 1U < DLUMA_ARRAY_COUNT(normalized)) {
        char value = path[length];
        if (value == ':') absolute = 1;
        normalized[length] = value == '\\' ? '/' : dluma_ascii_lower(value);
        ++length;
    }
    if (path[length]) return 0;
    normalized[length] = 0;
    if (normalized[0] == '/') absolute = 1;
    if (absolute) {
        char root[DLUMA_MAX_WPATH * 2U];
        unsigned int root_length = 0;
        if (!source_root || !source_root[0]) return 0;
        while (source_root[root_length] && root_length + 1U < DLUMA_ARRAY_COUNT(root)) {
            char value = source_root[root_length];
            root[root_length++] = value == '\\' ? '/' : dluma_ascii_lower(value);
        }
        if (source_root[root_length]) return 0;
        while (root_length && root[root_length - 1U] == '/') --root_length;
        if (!root_length || root_length >= length || normalized[root_length] != '/' ||
            memcmp(normalized, root, root_length) != 0) return 0;
        start = root_length + 1U;
        if (root_length >= 5U && memcmp(root + root_length - 5U, "/data", 5U) == 0)
            payload_kind = 1;
        else if (root_length >= 10U && memcmp(root + root_length - 10U, "/assets_pc", 10U) == 0)
            payload_kind = 2;
    }
    while (normalized[start] == '.' && normalized[start + 1U] == '/') start += 2U;
    for (index = start; index < length; ++index)
        if (normalized[index] == ':' ||
            ((index == start || normalized[index - 1U] == '/') && normalized[index] == '.' &&
             (normalized[index + 1U] == '/' || normalized[index + 1U] == 0 ||
              (normalized[index + 1U] == '.' &&
               (normalized[index + 2U] == '/' || normalized[index + 2U] == 0)))))
            return 0;
    for (candidate = 0; candidate < DLUMA_ARRAY_COUNT(prefixes); ++candidate) {
        WCHAR wide[DLUMA_MAX_WPATH], physical[DLUMA_MAX_WPATH];
        DWORD attributes;
        if ((payload_kind == 1 && candidate != 2U) ||
            (payload_kind == 2 && candidate != 1U)) continue;
        if (!payload_kind && candidate &&
            ((length - start >= 5U && memcmp(normalized + start, "data/", 5U) == 0) ||
             (length - start >= 10U && memcmp(normalized + start, "assets_pc/", 10U) == 0)))
            break;
        dluma_str_copy(relative, DLUMA_ARRAY_COUNT(relative), prefixes[candidate]);
        dluma_str_append(relative, DLUMA_ARRAY_COUNT(relative), normalized + start);
        if (!dluma_utf8_to_wide(relative, wide, DLUMA_ARRAY_COUNT(wide)) ||
            wcslen(active->root) + wcslen(wide) + 2U >= DLUMA_MAX_WPATH)
            return 0;
        dluma_path_join(physical, DLUMA_ARRAY_COUNT(physical), active->root, wide);
        attributes = GetFileAttributesW(physical);
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            continue;
        if (alias_out && alias_capacity) {
            if (strlen(active->alias) + strlen(relative) + 2U > alias_capacity)
                return 0;
            dluma_str_copy(alias_out, alias_capacity, active->alias);
            dluma_str_append(alias_out, alias_capacity, "/");
            dluma_str_append(alias_out, alias_capacity, relative);
        }
        if (physical_out && physical_capacity &&
            !dluma_wide_to_utf8(physical, physical_out, physical_capacity)) return 0;
        if (source_out) *source_out = active->namespace_source;
        return 1;
    }
    return 0;
}

int dluma_asset_mod_runtime_resolve_open(const char *path,
    char *alias_out, unsigned int alias_capacity,
    char *physical_out, unsigned int physical_capacity, void **source_out)
{
    return dluma_asset_runtime_resolve_in_source(path, 0, alias_out, alias_capacity,
        physical_out, physical_capacity, source_out);
}

int dluma_asset_mod_runtime_resolve_native(const char *path, const char *source_root,
    char *physical_out, unsigned int physical_capacity, void **source_out)
{
    return dluma_asset_runtime_resolve_in_source(path, source_root, 0, 0,
        physical_out, physical_capacity, source_out);
}

void *dluma_asset_mod_runtime_source_for_path(const char *physical_path)
{
    const DlumaAssetRuntimeSource *state = &g_dluma_asset_sources[0];
    SIZE_T index, length;
    if (!physical_path || !state->registered) return 0;
    length = strlen(state->record.requested_mount_path);
    for (index = 0; index < length; ++index) {
        char left = physical_path[index], right = state->record.requested_mount_path[index];
        if (!left) return 0;
        if (left == '\\') left = '/';
        if (right == '\\') right = '/';
        if (dluma_ascii_lower(left) != dluma_ascii_lower(right)) return 0;
    }
    if (physical_path[length] != '/' && physical_path[length] != '\\') return 0;
    return state->record.source_object;
}
