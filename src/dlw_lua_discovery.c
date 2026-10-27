#include "dlw_common.h"
#include "core/dlw_mod_package_runtime.h"
#include "generated/dluma_api_catalog.generated.h"
#include "dlw_lua_discovery_internal.h"
#include "dlw_lua_config.h"
#include "dlw_native_mod_config.h"
#include "dlw_path_security.h"

/* Metadata discovery normally remains worker-owned. Startup recovery performs
 * one raw pre-game metadata pass only; Lua execution is still owned by the
 * private runtime and reconciled on the game thread. */

#define DLUMA_LUA_DISCOVERY_CACHE_CAPACITY (DLUMA_MAX_LUA_FILES * 2U)

typedef struct DlumaLuaFileKey {
    WCHAR final_path[DLUMA_MAX_WPATH];
    WCHAR final_project_root[DLUMA_MAX_WPATH];
    DWORD volume_serial;
    DWORD file_index_high;
    DWORD file_index_low;
    DWORD size_high;
    DWORD size_low;
    DWORD last_write_high;
    DWORD last_write_low;
} DlumaLuaFileKey;

typedef struct DlumaLuaMetadataCacheEntry {
    DlumaLuaFileKey key;
    DlumaLuaMetadata metadata;
    ULONGLONG last_used;
    unsigned char occupied;
    unsigned char parse_succeeded;
    unsigned char saw_header;
    unsigned char reserved;
} DlumaLuaMetadataCacheEntry;

typedef struct DlumaLuaDiscoveryWorkspace {
    WCHAR (*projects)[DLUMA_MAX_WPATH];
    WCHAR (*paths)[DLUMA_MAX_WPATH];
    WCHAR (*owners)[DLUMA_MAX_WPATH];
    DlumaLuaMetadata* next;
    DlumaLuaMetadataCacheEntry* cache;
    volatile LONG initialization_state;
    volatile LONG scan_active;
    ULONGLONG cache_clock;
} DlumaLuaDiscoveryWorkspace;

static DlumaLuaDiscoveryWorkspace g_dluma_lua_workspace;
static DlumaLuaDiscoveryMetrics* g_dluma_lua_current_metrics;

static int dluma_lua_workspace_initialize(void)
{
    LONG state = g_dluma_lua_workspace.initialization_state;
    if (state == 2) return 1;
    if (dluma_atomic_compare_exchange(
            &g_dluma_lua_workspace.initialization_state, 1, 0) != 0) {
        while (g_dluma_lua_workspace.initialization_state == 1)
            Sleep(0);
        return g_dluma_lua_workspace.initialization_state == 2;
    }
    g_dluma_lua_workspace.projects =
        (WCHAR (*)[DLUMA_MAX_WPATH])dluma_alloc(
            sizeof(*g_dluma_lua_workspace.projects) *
            DLUMA_MAX_LUA_FILES);
    g_dluma_lua_workspace.paths =
        (WCHAR (*)[DLUMA_MAX_WPATH])dluma_alloc(
            sizeof(*g_dluma_lua_workspace.paths) *
            DLUMA_MAX_LUA_FILES);
    g_dluma_lua_workspace.owners =
        (WCHAR (*)[DLUMA_MAX_WPATH])dluma_alloc(
            sizeof(*g_dluma_lua_workspace.owners) *
            DLUMA_MAX_LUA_FILES);
    g_dluma_lua_workspace.next =
        (DlumaLuaMetadata*)dluma_alloc(
            sizeof(*g_dluma_lua_workspace.next) *
            DLUMA_MAX_LUA_FILES);
    g_dluma_lua_workspace.cache =
        (DlumaLuaMetadataCacheEntry*)dluma_alloc(
            sizeof(*g_dluma_lua_workspace.cache) *
            DLUMA_LUA_DISCOVERY_CACHE_CAPACITY);
    if (!g_dluma_lua_workspace.projects ||
        !g_dluma_lua_workspace.paths ||
        !g_dluma_lua_workspace.owners ||
        !g_dluma_lua_workspace.next ||
        !g_dluma_lua_workspace.cache) {
        dluma_free(g_dluma_lua_workspace.cache);
        dluma_free(g_dluma_lua_workspace.next);
        dluma_free(g_dluma_lua_workspace.owners);
        dluma_free(g_dluma_lua_workspace.paths);
        dluma_free(g_dluma_lua_workspace.projects);
        g_dluma_lua_workspace.cache = 0;
        g_dluma_lua_workspace.next = 0;
        g_dluma_lua_workspace.owners = 0;
        g_dluma_lua_workspace.paths = 0;
        g_dluma_lua_workspace.projects = 0;
        dluma_atomic_exchange(
            &g_dluma_lua_workspace.initialization_state, 0);
        return 0;
    }
    memset(
        g_dluma_lua_workspace.projects, 0,
        sizeof(*g_dluma_lua_workspace.projects) *
            DLUMA_MAX_LUA_FILES);
    memset(
        g_dluma_lua_workspace.paths, 0,
        sizeof(*g_dluma_lua_workspace.paths) *
            DLUMA_MAX_LUA_FILES);
    memset(
        g_dluma_lua_workspace.owners, 0,
        sizeof(*g_dluma_lua_workspace.owners) *
            DLUMA_MAX_LUA_FILES);
    memset(
        g_dluma_lua_workspace.next, 0,
        sizeof(*g_dluma_lua_workspace.next) *
            DLUMA_MAX_LUA_FILES);
    memset(
        g_dluma_lua_workspace.cache, 0,
        sizeof(*g_dluma_lua_workspace.cache) *
            DLUMA_LUA_DISCOVERY_CACHE_CAPACITY);
    g_dluma_lua_workspace.cache_clock = 0;
    dluma_atomic_exchange(
        &g_dluma_lua_workspace.initialization_state, 2);
    return 1;
}

void dluma_lua_discovery_shutdown(void)
{
    if (g_dluma_lua_workspace.initialization_state != 2 ||
        g_dluma_lua_workspace.scan_active)
        return;
    dluma_lua_config_shutdown();
    dluma_free(g_dluma_lua_workspace.cache);
    dluma_free(g_dluma_lua_workspace.next);
    dluma_free(g_dluma_lua_workspace.owners);
    dluma_free(g_dluma_lua_workspace.paths);
    dluma_free(g_dluma_lua_workspace.projects);
    memset(
        &g_dluma_lua_workspace, 0,
        sizeof(g_dluma_lua_workspace));
}


void dluma_lua_registry_lock(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma.lua_discovery.global_registry_lock, 1, 0) != 0) {
        Sleep(0);
    }
}

void dluma_lua_registry_unlock(void)
{
    dluma_atomic_exchange(&g_dluma.lua_discovery.global_registry_lock, 0);
}

void dluma_lua_get_global_registry_ini_path(WCHAR* out, unsigned int capacity)
{
    WCHAR runtime_dir[DLUMA_MAX_WPATH];
    if (!out || !capacity) return;
    dluma_path_join(runtime_dir, DLUMA_ARRAY_COUNT(runtime_dir),
                  g_dluma.module_dir, L"dluma_runtime");
    dluma_mkdirs(runtime_dir);
    dluma_path_join(out, capacity, runtime_dir, L"global_mods.ini");
}

static int dluma_lua_is_dot_dir(const WCHAR* name)
{
    return name && name[0] == L'.' &&
           (name[1] == 0 || (name[1] == L'.' && name[2] == 0));
}

static void dluma_lua_log_path(const char* label, const WCHAR* path)
{
    char utf8[DLUMA_MAX_WPATH * 2];
    utf8[0] = 0;
    if (dluma_wide_to_utf8(path, utf8, DLUMA_ARRAY_COUNT(utf8)))
        dluma_log_pair("INFO", label, utf8);
}

static int dluma_lua_final_path_within_root(
    const WCHAR* final_path,
    const WCHAR* final_root)
{
    unsigned int i = 0;
    if (!final_path || !final_root || !final_root[0]) return 0;
    while (final_root[i]) {
        WCHAR left = final_path[i];
        WCHAR right = final_root[i];
        if (!left || dluma_wascii_lower(left) != dluma_wascii_lower(right))
            return 0;
        ++i;
    }
    while (i && (final_root[i - 1] == L'\\' || final_root[i - 1] == L'/'))
        --i;
    return final_path[i] == L'\\' || final_path[i] == L'/';
}

static int dluma_lua_probe_file_key(
    const WCHAR* path, const WCHAR* project_root,
    DlumaLuaFileKey* key)
{
    HANDLE root_handle = INVALID_HANDLE_VALUE;
    HANDLE file_handle = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION root_info;
    BY_HANDLE_FILE_INFORMATION file_info;
    int result = 0;
    if (!path || !path[0] || !project_root || !project_root[0] ||
        !key)
        return 0;
    memset(key, 0, sizeof(*key));
    root_handle = CreateFileW(
        project_root, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (root_handle == INVALID_HANDLE_VALUE) goto done;
    file_handle = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ,
        0, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file_handle == INVALID_HANDLE_VALUE) goto done;
    memset(&root_info, 0, sizeof(root_info));
    memset(&file_info, 0, sizeof(file_info));
    if (!GetFileInformationByHandle(root_handle, &root_info) ||
        !GetFileInformationByHandle(file_handle, &file_info) ||
        !(root_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (root_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        (file_info.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !dluma_get_final_path_checked(
            root_handle, key->final_project_root,
            DLUMA_ARRAY_COUNT(key->final_project_root)) ||
        !dluma_get_final_path_checked(
            file_handle, key->final_path,
            DLUMA_ARRAY_COUNT(key->final_path)) ||
        !dluma_lua_final_path_within_root(
            key->final_path, key->final_project_root))
        goto done;
    key->volume_serial = file_info.dwVolumeSerialNumber;
    key->file_index_high = file_info.nFileIndexHigh;
    key->file_index_low = file_info.nFileIndexLow;
    key->size_high = file_info.nFileSizeHigh;
    key->size_low = file_info.nFileSizeLow;
    key->last_write_high =
        file_info.ftLastWriteTime.dwHighDateTime;
    key->last_write_low =
        file_info.ftLastWriteTime.dwLowDateTime;
    result = 1;
done:
    if (file_handle != INVALID_HANDLE_VALUE) CloseHandle(file_handle);
    if (root_handle != INVALID_HANDLE_VALUE) CloseHandle(root_handle);
    return result;
}

static int dluma_lua_file_key_equal(
    const DlumaLuaFileKey* left, const DlumaLuaFileKey* right)
{
    return left && right &&
        left->volume_serial == right->volume_serial &&
        left->file_index_high == right->file_index_high &&
        left->file_index_low == right->file_index_low &&
        left->size_high == right->size_high &&
        left->size_low == right->size_low &&
        left->last_write_high == right->last_write_high &&
        left->last_write_low == right->last_write_low &&
        dluma_wstricmp(left->final_path, right->final_path) == 0 &&
        dluma_wstricmp(
            left->final_project_root,
            right->final_project_root) == 0;
}

static DlumaLuaMetadataCacheEntry* dluma_lua_cache_find(
    const DlumaLuaFileKey* key)
{
    unsigned int index;
    if (!g_dluma_lua_workspace.cache || !key) return 0;
    for (index = 0;
         index < DLUMA_LUA_DISCOVERY_CACHE_CAPACITY;
         ++index) {
        DlumaLuaMetadataCacheEntry* entry =
            &g_dluma_lua_workspace.cache[index];
        if (entry->occupied &&
            dluma_lua_file_key_equal(&entry->key, key))
            return entry;
    }
    return 0;
}

static DlumaLuaMetadataCacheEntry* dluma_lua_cache_slot(void)
{
    unsigned int index;
    unsigned int oldest_index = 0;
    ULONGLONG oldest_use = ~0ULL;
    for (index = 0;
         index < DLUMA_LUA_DISCOVERY_CACHE_CAPACITY;
         ++index) {
        DlumaLuaMetadataCacheEntry* entry =
            &g_dluma_lua_workspace.cache[index];
        if (!entry->occupied) return entry;
        if (entry->last_used < oldest_use) {
            oldest_use = entry->last_used;
            oldest_index = index;
        }
    }
    return &g_dluma_lua_workspace.cache[oldest_index];
}

static int dluma_lua_read_validated_source(
    const WCHAR* path,
    const WCHAR* project_root,
    char** out_data,
    DWORD* out_size,
    DlumaLuaMetadata* metadata)
{
    HANDLE root_handle = INVALID_HANDLE_VALUE;
    HANDLE file_handle = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION info;
    LARGE_INTEGER size;
    WCHAR final_root[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    DWORD attributes;
    DWORD read = 0;
    char* data = 0;
    int ok = 0;
    if (!path || !project_root || !out_data || !out_size || !metadata)
        return 0;
    *out_data = 0;
    *out_size = 0;
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        return 0;
    root_handle = CreateFileW(
        project_root, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    if (root_handle == INVALID_HANDLE_VALUE) goto done;
    file_handle = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file_handle == INVALID_HANDLE_VALUE) goto done;
    memset(&info, 0, sizeof(info));
    if (!GetFileInformationByHandle(file_handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |
                                  FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !GetFileSizeEx(file_handle, &size) || size.QuadPart < 0 ||
        (ULONGLONG)size.QuadPart > DLUMA_MAX_SCRIPT_FILE_BYTES)
        goto done;
    if (!dluma_get_final_path_checked(
            root_handle, final_root, DLUMA_ARRAY_COUNT(final_root)) ||
        !dluma_get_final_path_checked(
            file_handle, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
        !dluma_lua_final_path_within_root(final_path, final_root))
        goto done;
    data = (char*)dluma_alloc((SIZE_T)size.QuadPart + 1U);
    if (!data) goto done;
    if (size.QuadPart &&
        (!ReadFile(file_handle, data, (DWORD)size.QuadPart, &read, 0) ||
         read != (DWORD)size.QuadPart))
        goto done;
    data[(DWORD)size.QuadPart] = 0;
    metadata->source_volume_serial = info.dwVolumeSerialNumber;
    metadata->source_file_index_high = info.nFileIndexHigh;
    metadata->source_file_index_low = info.nFileIndexLow;
    metadata->source_size = (DWORD)size.QuadPart;
    *out_data = data;
    *out_size = (DWORD)size.QuadPart;
    data = 0;
    ok = 1;
done:
    if (data) dluma_free(data);
    if (file_handle != INVALID_HANDLE_VALUE) CloseHandle(file_handle);
    if (root_handle != INVALID_HANDLE_VALUE) CloseHandle(root_handle);
    return ok;
}

static int dluma_lua_id_is_reserved_device(const char* id)
{
    char base[16];
    unsigned int i = 0;
    while (id && id[i] && id[i] != '.' && i + 1 < DLUMA_ARRAY_COUNT(base)) {
        base[i] = dluma_ascii_lower(id[i]);
        ++i;
    }
    base[i] = 0;
    if (dluma_stricmp(base, "con") == 0 ||
        dluma_stricmp(base, "prn") == 0 ||
        dluma_stricmp(base, "aux") == 0 ||
        dluma_stricmp(base, "nul") == 0 ||
        dluma_stricmp(base, "clock$") == 0)
        return 1;
    return i == 4 &&
        ((base[0] == 'c' && base[1] == 'o' && base[2] == 'm') ||
         (base[0] == 'l' && base[1] == 'p' && base[2] == 't')) &&
        base[3] >= '1' && base[3] <= '9';
}

static int dluma_lua_id_valid(const char* id)
{
    unsigned int i;
    unsigned int length;
    char first;
    char last;
    if (!id) return 0;
    length = (unsigned int)strlen(id);
    if (length < 3 || length > 127) return 0;
    first = id[0];
    last = id[length - 1];
    if (dluma_lua_id_is_reserved_device(id))
        return 0;
    if (!((first >= 'a' && first <= 'z') ||
          (first >= 'A' && first <= 'Z') ||
          (first >= '0' && first <= '9')))
        return 0;
    if (!((last >= 'a' && last <= 'z') ||
          (last >= 'A' && last <= 'Z') ||
          (last >= '0' && last <= '9')))
        return 0;
    for (i = 0; i < length; ++i) {
        char value = id[i];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') ||
              value == '.' || value == '_' || value == '-'))
            return 0;
    }
    return 1;
}

static int dluma_lua_path_already_collected(
    WCHAR paths[][DLUMA_MAX_WPATH],
    unsigned int count,
    const WCHAR* candidate)
{
    unsigned int i;
    for (i = 0; i < count; ++i) {
        if (dluma_wstricmp(paths[i], candidate) == 0) return 1;
    }
    return 0;
}

static unsigned int dluma_lua_collect_project_roots(
    WCHAR projects[][DLUMA_MAX_WPATH],
    unsigned int max_projects)
{
    unsigned int count = 0;
    unsigned int r;
    for (r = 0; r < g_dluma.workshop_root_count; ++r) {
        WCHAR data_dir[DLUMA_MAX_WPATH];
        WCHAR pattern[DLUMA_MAX_WPATH];
        WIN32_FIND_DATAW data;
        HANDLE find;
        const WCHAR* root = g_dluma.workshop_roots[r];

        dluma_path_join(data_dir, DLUMA_ARRAY_COUNT(data_dir), root, L"data");
        if (dluma_dir_exists(data_dir) &&
            !dluma_lua_path_already_collected(projects, count, root)) {
            if (g_dluma_lua_current_metrics)
                ++g_dluma_lua_current_metrics->projects_visited;
            if (count < max_projects)
                dluma_wstr_copy(projects[count++], DLUMA_MAX_WPATH, root);
            else if (g_dluma_lua_current_metrics)
                ++g_dluma_lua_current_metrics->projects_truncated;
        }

        dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), root, L"*");
        find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            WCHAR candidate[DLUMA_MAX_WPATH];
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                dluma_lua_is_dot_dir(data.cFileName)) continue;
            dluma_path_join(candidate, DLUMA_ARRAY_COUNT(candidate), root, data.cFileName);
            dluma_path_join(data_dir, DLUMA_ARRAY_COUNT(data_dir), candidate, L"data");
            if (!dluma_dir_exists(data_dir)) continue;
            if (dluma_lua_path_already_collected(projects, count, candidate)) continue;
            if (g_dluma_lua_current_metrics)
                ++g_dluma_lua_current_metrics->projects_visited;
            if (count < max_projects)
                dluma_wstr_copy(projects[count++], DLUMA_MAX_WPATH, candidate);
            else if (g_dluma_lua_current_metrics)
                ++g_dluma_lua_current_metrics->projects_truncated;
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    return count;
}

static unsigned int dluma_lua_collect_from_dir(
    const WCHAR* dir,
    const WCHAR* project_root,
    WCHAR paths[][DLUMA_MAX_WPATH],
    WCHAR owners[][DLUMA_MAX_WPATH],
    unsigned int count,
    unsigned int max_paths)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    if (!dluma_dir_exists(dir) ||
        (GetFileAttributesW(dir) & FILE_ATTRIBUTE_REPARSE_POINT)) return count;
    /*
     * DLUMA API 1.0 packages are one directory per mod.  Enumerate only the
     * immediate package directories so a dependency or nested module can
     * never become an independent entry point.
     */
    dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), dir, L"*");
    find = FindFirstFileW(pattern, &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            WCHAR package_root[DLUMA_MAX_WPATH];
            WCHAR manifest[DLUMA_MAX_WPATH];
            DWORD manifest_attributes;
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                dluma_lua_is_dot_dir(data.cFileName))
                continue;
            dluma_path_join(
                package_root, DLUMA_ARRAY_COUNT(package_root),
                dir, data.cFileName);
            dluma_path_join(
                manifest, DLUMA_ARRAY_COUNT(manifest),
                package_root, L"mod.dluma.json");
            manifest_attributes = GetFileAttributesW(manifest);
            if (manifest_attributes == INVALID_FILE_ATTRIBUTES ||
                (manifest_attributes &
                 (FILE_ATTRIBUTE_DIRECTORY |
                  FILE_ATTRIBUTE_REPARSE_POINT)))
                continue;
            if (g_dluma_lua_current_metrics)
                ++g_dluma_lua_current_metrics->files_visited;
            if (!dluma_lua_path_already_collected(
                    paths, count, manifest) &&
                count < max_paths) {
                dluma_wstr_copy(
                    paths[count], DLUMA_MAX_WPATH, manifest);
                dluma_wstr_copy(
                    owners[count], DLUMA_MAX_WPATH, project_root);
                ++count;
            } else if (count >= max_paths &&
                       g_dluma_lua_current_metrics) {
                ++g_dluma_lua_current_metrics->files_truncated;
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    /* Direct Lua files are legacy artifacts. They remain discoverable only
     * so the UI/report can explain that API 2.1 is unsupported. */
    dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), dir, L"*.lua");
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return count;
    do {
        WCHAR full[DLUMA_MAX_WPATH];
        if (data.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        if (g_dluma_lua_current_metrics)
            ++g_dluma_lua_current_metrics->files_visited;
        dluma_path_join(full, DLUMA_ARRAY_COUNT(full), dir, data.cFileName);
        if (!dluma_lua_path_already_collected(paths, count, full) && count < max_paths) {
            dluma_wstr_copy(paths[count], DLUMA_MAX_WPATH, full);
            dluma_wstr_copy(owners[count], DLUMA_MAX_WPATH, project_root);
            ++count;
        } else if (count >= max_paths && g_dluma_lua_current_metrics)
            ++g_dluma_lua_current_metrics->files_truncated;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return count;
}

static unsigned int dluma_lua_collect_project_files(
    const WCHAR* project_root,
    const char* map_name,
    WCHAR paths[][DLUMA_MAX_WPATH],
    WCHAR owners[][DLUMA_MAX_WPATH],
    unsigned int count,
    unsigned int max_paths)
{
    WCHAR dir[DLUMA_MAX_WPATH];
    WCHAR map_wide[DLUMA_MAX_MAP_NAME];

    /* Canonical 0.1.17 layout. The map directories below are added only for
     * the already selected owning project and active internal map. */
    dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"data\\scripts\\lua\\global");
    count = dluma_lua_collect_from_dir(dir, project_root, paths, owners, count, max_paths);
    dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"data\\scripts\\lua\\project");
    count = dluma_lua_collect_from_dir(dir, project_root, paths, owners, count, max_paths);

    /* 0.1.x compatibility locations. Metadata remains mandatory here because
     * the containing folder does not determine the script lifetime. */
    dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"data\\scripts\\dluma");
    count = dluma_lua_collect_from_dir(dir, project_root, paths, owners, count, max_paths);
    dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"data\\scripts");
    count = dluma_lua_collect_from_dir(dir, project_root, paths, owners, count, max_paths);
    dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"mods\\scripts\\dluma");
    count = dluma_lua_collect_from_dir(dir, project_root, paths, owners, count, max_paths);

    map_wide[0] = 0;
    if (map_name && map_name[0] &&
        dluma_utf8_to_wide(map_name, map_wide, DLUMA_ARRAY_COUNT(map_wide))) {
        dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"data\\scripts\\lua\\maps");
        dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), dir, map_wide);
        count = dluma_lua_collect_from_dir(dir, project_root, paths, owners, count, max_paths);

        /* Optional colocated form beside the map's ordinary project data. */
        dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"data\\maps");
        dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), dir, map_wide);
        dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), dir, L"scripts\\lua");
        count = dluma_lua_collect_from_dir(dir, project_root, paths, owners, count, max_paths);

        /* Compatibility with the earlier colocated prototype. */
        dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), project_root, L"data\\maps");
        dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), dir, map_wide);
        dluma_path_join(dir, DLUMA_ARRAY_COUNT(dir), dir, L"scripts\\dluma");
        count = dluma_lua_collect_from_dir(dir, project_root, paths, owners, count, max_paths);
    }
    return count;
}

static unsigned int dluma_lua_collect_editor_map_phase_files(
    const WCHAR* project_root,
    WCHAR paths[][DLUMA_MAX_WPATH],
    WCHAR owners[][DLUMA_MAX_WPATH],
    unsigned int count,
    unsigned int max_paths)
{
    static const WCHAR* roots[] = {
        L"data\\scripts\\lua\\maps",
        L"data\\maps"
    };
    unsigned int root_index;
    for (root_index = 0; root_index < DLUMA_ARRAY_COUNT(roots);
         ++root_index) {
        WCHAR root[DLUMA_MAX_WPATH];
        WCHAR pattern[DLUMA_MAX_WPATH];
        WIN32_FIND_DATAW data;
        HANDLE find;
        dluma_path_join(root, DLUMA_ARRAY_COUNT(root),
                      project_root, roots[root_index]);
        if (!dluma_dir_exists(root) ||
            (GetFileAttributesW(root) & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        dluma_path_join(pattern, DLUMA_ARRAY_COUNT(pattern), root, L"*");
        find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            WCHAR directory[DLUMA_MAX_WPATH];
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                dluma_lua_is_dot_dir(data.cFileName))
                continue;
            dluma_path_join(directory, DLUMA_ARRAY_COUNT(directory),
                          root, data.cFileName);
            if (root_index == 0U) {
                count = dluma_lua_collect_from_dir(
                    directory, project_root, paths, owners,
                    count, max_paths);
            } else {
                WCHAR scripts[DLUMA_MAX_WPATH];
                dluma_path_join(scripts, DLUMA_ARRAY_COUNT(scripts),
                              directory, L"scripts\\lua");
                count = dluma_lua_collect_from_dir(
                    scripts, project_root, paths, owners,
                    count, max_paths);
                dluma_path_join(scripts, DLUMA_ARRAY_COUNT(scripts),
                              directory, L"scripts\\dluma");
                count = dluma_lua_collect_from_dir(
                    scripts, project_root, paths, owners,
                    count, max_paths);
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    return count;
}

static void dluma_lua_sort_paths(
    WCHAR paths[][DLUMA_MAX_WPATH],
    WCHAR owners[][DLUMA_MAX_WPATH],
    unsigned int count)
{
    unsigned int i, j;
    for (i = 1; i < count; ++i) {
        WCHAR temp_path[DLUMA_MAX_WPATH];
        WCHAR temp_owner[DLUMA_MAX_WPATH];
        dluma_wstr_copy(temp_path, DLUMA_ARRAY_COUNT(temp_path), paths[i]);
        dluma_wstr_copy(temp_owner, DLUMA_ARRAY_COUNT(temp_owner), owners[i]);
        j = i;
        while (j > 0 && dluma_wstricmp(paths[j - 1], temp_path) > 0) {
            dluma_wstr_copy(paths[j], DLUMA_MAX_WPATH, paths[j - 1]);
            dluma_wstr_copy(owners[j], DLUMA_MAX_WPATH, owners[j - 1]);
            --j;
        }
        dluma_wstr_copy(paths[j], DLUMA_MAX_WPATH, temp_path);
        dluma_wstr_copy(owners[j], DLUMA_MAX_WPATH, temp_owner);
    }
}

static int dluma_lua_starts_with(const char* text, const char* prefix)
{
    unsigned int i = 0;
    if (!text || !prefix) return 0;
    while (prefix[i]) {
        if (text[i] != prefix[i]) return 0;
        ++i;
    }
    return 1;
}

static void dluma_lua_parse_value(char* value)
{
    unsigned int len;
    dluma_trim_in_place(value);
    len = (unsigned int)strlen(value);
    if (len >= 2 && ((value[0] == '"' && value[len - 1] == '"') ||
                     (value[0] == '\'' && value[len - 1] == '\''))) {
        unsigned int i;
        for (i = 1; i + 1 < len; ++i) value[i - 1] = value[i];
        value[len - 2] = 0;
    }
}

typedef struct DlumaLuaManifestParser {
    const char* cursor;
    const char* end;
} DlumaLuaManifestParser;

static void dluma_lua_manifest_skip_space(DlumaLuaManifestParser* parser)
{
    for (;;) {
        while (parser->cursor < parser->end &&
               (*parser->cursor == ' ' || *parser->cursor == '\t' ||
                *parser->cursor == '\r' || *parser->cursor == '\n'))
            ++parser->cursor;
        if (parser->cursor + 1 >= parser->end ||
            parser->cursor[0] != '-' || parser->cursor[1] != '-')
            return;
        parser->cursor += 2;
        if (parser->cursor + 1 < parser->end &&
            parser->cursor[0] == '[' && parser->cursor[1] == '[') {
            parser->cursor += 2;
            while (parser->cursor + 1 < parser->end &&
                   !(parser->cursor[0] == ']' && parser->cursor[1] == ']'))
                ++parser->cursor;
            if (parser->cursor + 1 < parser->end) parser->cursor += 2;
        } else {
            while (parser->cursor < parser->end &&
                   *parser->cursor != '\r' && *parser->cursor != '\n')
                ++parser->cursor;
        }
    }
}

static int dluma_lua_manifest_identifier(DlumaLuaManifestParser* parser,
                                       char* out, unsigned int capacity)
{
    unsigned int length = 0;
    dluma_lua_manifest_skip_space(parser);
    if (parser->cursor >= parser->end ||
        !((*parser->cursor >= 'A' && *parser->cursor <= 'Z') ||
          (*parser->cursor >= 'a' && *parser->cursor <= 'z') ||
          *parser->cursor == '_'))
        return 0;
    while (parser->cursor < parser->end &&
           ((*parser->cursor >= 'A' && *parser->cursor <= 'Z') ||
            (*parser->cursor >= 'a' && *parser->cursor <= 'z') ||
            (*parser->cursor >= '0' && *parser->cursor <= '9') ||
            *parser->cursor == '_')) {
        if (length + 1 < capacity) out[length++] = *parser->cursor;
        ++parser->cursor;
    }
    if (capacity) out[length] = 0;
    return 1;
}

static int dluma_lua_manifest_take(DlumaLuaManifestParser* parser, char value)
{
    dluma_lua_manifest_skip_space(parser);
    if (parser->cursor >= parser->end || *parser->cursor != value) return 0;
    ++parser->cursor;
    return 1;
}

static int dluma_lua_manifest_string(DlumaLuaManifestParser* parser,
                                   char* out, unsigned int capacity)
{
    char quote;
    unsigned int length = 0;
    dluma_lua_manifest_skip_space(parser);
    if (parser->cursor >= parser->end ||
        (*parser->cursor != '\'' && *parser->cursor != '"'))
        return 0;
    quote = *parser->cursor++;
    while (parser->cursor < parser->end && *parser->cursor != quote) {
        char value = *parser->cursor++;
        if (!value) return 0;
        if (value == '\\' && parser->cursor < parser->end) {
            char escaped = *parser->cursor++;
            if (!escaped) return 0;
            if (escaped == 'n') value = '\n';
            else if (escaped == 'r') value = '\r';
            else if (escaped == 't') value = '\t';
            else value = escaped;
        }
        if (length + 1 >= capacity) return 0;
        out[length++] = value;
    }
    if (parser->cursor >= parser->end || *parser->cursor != quote) return 0;
    ++parser->cursor;
    out[length] = 0;
    return 1;
}

static void dluma_lua_manifest_skip_string(DlumaLuaManifestParser* parser)
{
    char quote;
    if (parser->cursor >= parser->end ||
        (*parser->cursor != '\'' && *parser->cursor != '"')) return;
    quote = *parser->cursor++;
    while (parser->cursor < parser->end && *parser->cursor != quote) {
        if (*parser->cursor++ == '\\' && parser->cursor < parser->end)
            ++parser->cursor;
    }
    if (parser->cursor < parser->end) ++parser->cursor;
}

static int dluma_lua_manifest_skip_value(DlumaLuaManifestParser* parser)
{
    int depth = 0;
    char quote = 0;
    dluma_lua_manifest_skip_space(parser);
    while (parser->cursor < parser->end) {
        char value = *parser->cursor;
        if (quote) {
            ++parser->cursor;
            if (value == '\\' && parser->cursor < parser->end)
                ++parser->cursor;
            else if (value == quote)
                quote = 0;
            continue;
        }
        if (value == '\'' || value == '"') {
            quote = value;
            ++parser->cursor;
        } else if (value == '{' || value == '(' || value == '[') {
            ++depth;
            ++parser->cursor;
        } else if (value == '}' || value == ')' || value == ']') {
            if (!depth || (value == '}' && depth == 0)) return 1;
            --depth;
            ++parser->cursor;
        } else if (!depth && (value == ',' || value == ';')) {
            return 1;
        } else {
            ++parser->cursor;
        }
    }
    return 1;
}

static int dluma_lua_manifest_permissions(DlumaLuaManifestParser* parser,
                                        char* out, unsigned int capacity)
{
    char permission[96];
    out[0] = 0;
    dluma_lua_manifest_skip_space(parser);
    if (parser->cursor < parser->end &&
        (*parser->cursor == '\'' || *parser->cursor == '"'))
        return dluma_lua_manifest_string(parser, out, capacity);
    if (!dluma_lua_manifest_take(parser, '{')) return 0;
    for (;;) {
        dluma_lua_manifest_skip_space(parser);
        if (dluma_lua_manifest_take(parser, '}')) return 1;
        if (!dluma_lua_manifest_string(
                parser, permission, DLUMA_ARRAY_COUNT(permission))) return 0;
        if (out[0]) dluma_str_append(out, capacity, ", ");
        dluma_str_append(out, capacity, permission);
        dluma_lua_manifest_skip_space(parser);
        if (dluma_lua_manifest_take(parser, '}')) return 1;
        if (!dluma_lua_manifest_take(parser, ',') &&
            !dluma_lua_manifest_take(parser, ';')) return 0;
    }
}

static void dluma_lua_config_diagnostic(
    DlumaLuaMetadata* metadata, const char* code)
{
    if (!metadata || metadata->config_diagnostic[0]) return;
    dluma_str_copy(
        metadata->config_diagnostic,
        DLUMA_ARRAY_COUNT(metadata->config_diagnostic),
        code ? code : "dynamic_or_malformed");
}

static int dluma_lua_manifest_boolean(
    DlumaLuaManifestParser* parser, int* out)
{
    char value[16];
    if (!out ||
        !dluma_lua_manifest_identifier(
            parser, value, DLUMA_ARRAY_COUNT(value)))
        return 0;
    if (dluma_stricmp(value, "true") == 0) {
        *out = 1;
        return 1;
    }
    if (dluma_stricmp(value, "false") == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int dluma_lua_manifest_i32(
    DlumaLuaManifestParser* parser, int* out)
{
    const char* cursor;
    ULONGLONG value = 0;
    ULONGLONG limit;
    int negative = 0;
    dluma_lua_manifest_skip_space(parser);
    cursor = parser->cursor;
    if (cursor >= parser->end || !out) return 0;
    if (*cursor == '-') {
        negative = 1;
        ++cursor;
    } else if (*cursor == '+') {
        ++cursor;
    }
    if (cursor >= parser->end || *cursor < '0' || *cursor > '9')
        return 0;
    limit = negative ? 2147483648ULL : 2147483647ULL;
    while (cursor < parser->end && *cursor >= '0' && *cursor <= '9') {
        unsigned int digit = (unsigned int)(*cursor - '0');
        if (value > (limit - digit) / 10ULL) return 0;
        value = value * 10ULL + digit;
        ++cursor;
    }
    parser->cursor = cursor;
    if (negative) {
        if (value == 2147483648ULL)
            *out = (-2147483647 - 1);
        else
            *out = -(int)value;
    } else {
        *out = (int)value;
    }
    return 1;
}

static int dluma_lua_manifest_u32(
    DlumaLuaManifestParser* parser, unsigned int* out)
{
    const char* cursor;
    ULONGLONG value = 0;
    dluma_lua_manifest_skip_space(parser);
    cursor = parser->cursor;
    if (cursor >= parser->end || !out ||
        *cursor < '0' || *cursor > '9')
        return 0;
    while (cursor < parser->end && *cursor >= '0' && *cursor <= '9') {
        unsigned int digit = (unsigned int)(*cursor - '0');
        if (value > (0xFFFFFFFFULL - digit) / 10ULL) return 0;
        value = value * 10ULL + digit;
        ++cursor;
    }
    parser->cursor = cursor;
    *out = (unsigned int)value;
    return 1;
}

static int dluma_lua_config_text_equal(
    const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return left == right;
    while (left[index] && right[index]) {
        if (left[index] != right[index]) return 0;
        ++index;
    }
    return left[index] == right[index];
}

static int dluma_lua_manifest_config_option(
    DlumaLuaManifestParser* parser,
    DlumaLuaConfigEnumOption* option,
    DlumaLuaMetadata* metadata)
{
    int saw_value = 0;
    int saw_name = 0;
    if (!parser || !option ||
        !dluma_lua_manifest_take(parser, '{'))
        return 0;
    memset(option, 0, sizeof(*option));
    for (;;) {
        char key[32];
        dluma_lua_manifest_skip_space(parser);
        if (dluma_lua_manifest_take(parser, '}')) break;
        if (!dluma_lua_manifest_identifier(
                parser, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_lua_manifest_take(parser, '='))
            return 0;
        if (dluma_stricmp(key, "value") == 0) {
            if (saw_value ||
                !dluma_lua_manifest_string(
                    parser, option->value,
                    DLUMA_ARRAY_COUNT(option->value)))
                return 0;
            saw_value = 1;
        } else if (dluma_stricmp(key, "name") == 0) {
            if (saw_name ||
                !dluma_lua_manifest_string(
                    parser, option->name,
                    DLUMA_ARRAY_COUNT(option->name)))
                return 0;
            saw_name = 1;
        } else {
            dluma_lua_config_diagnostic(metadata, "invalid_setting");
            return 0;
        }
        dluma_lua_manifest_skip_space(parser);
        if (dluma_lua_manifest_take(parser, '}')) break;
        if (!dluma_lua_manifest_take(parser, ',') &&
            !dluma_lua_manifest_take(parser, ';'))
            return 0;
    }
    if (!saw_value || !saw_name ||
        !dluma_lua_config_identifier_valid(option->value) ||
        !dluma_lua_config_utf8_valid(
            option->name, DLUMA_LUA_CONFIG_NAME_CAPACITY - 1U,
            DLUMA_LUA_CONFIG_NAME_CAPACITY - 1U, 0)) {
        dluma_lua_config_diagnostic(metadata, "invalid_setting");
        return 0;
    }
    return 1;
}

static int dluma_lua_manifest_config_options(
    DlumaLuaManifestParser* parser,
    DlumaLuaConfigSetting* setting,
    DlumaLuaMetadata* metadata)
{
    if (!parser || !setting ||
        !dluma_lua_manifest_take(parser, '{'))
        return 0;
    setting->option_count = 0;
    for (;;) {
        unsigned int earlier;
        DlumaLuaConfigEnumOption* option;
        dluma_lua_manifest_skip_space(parser);
        if (dluma_lua_manifest_take(parser, '}')) break;
        if (setting->option_count >=
            DLUMA_LUA_CONFIG_MAX_ENUM_OPTIONS) {
            dluma_lua_config_diagnostic(metadata, "schema_limits");
            return 0;
        }
        option = &setting->options[setting->option_count];
        if (!dluma_lua_manifest_config_option(
                parser, option, metadata))
            return 0;
        for (earlier = 0;
             earlier < setting->option_count;
             ++earlier) {
            if (dluma_lua_config_text_equal(
                    setting->options[earlier].value,
                    option->value)) {
                dluma_lua_config_diagnostic(
                    metadata, "duplicate_identifier");
                return 0;
            }
        }
        ++setting->option_count;
        dluma_lua_manifest_skip_space(parser);
        if (dluma_lua_manifest_take(parser, '}')) break;
        if (!dluma_lua_manifest_take(parser, ',') &&
            !dluma_lua_manifest_take(parser, ';'))
            return 0;
    }
    return 1;
}

static int dluma_lua_manifest_config_default(
    DlumaLuaManifestParser* parser,
    int* literal_kind,
    int* integer_value,
    char* text_value,
    unsigned int text_capacity)
{
    int boolean_value;
    const char* start;
    if (!parser || !literal_kind || !integer_value ||
        !text_value || !text_capacity)
        return 0;
    dluma_lua_manifest_skip_space(parser);
    if (parser->cursor >= parser->end) return 0;
    if (*parser->cursor == '\'' || *parser->cursor == '"') {
        if (!dluma_lua_manifest_string(
                parser, text_value, text_capacity))
            return 0;
        *literal_kind = 3;
        return 1;
    }
    start = parser->cursor;
    if (dluma_lua_manifest_boolean(parser, &boolean_value)) {
        *literal_kind = 1;
        *integer_value = boolean_value;
        return 1;
    }
    parser->cursor = start;
    if (dluma_lua_manifest_i32(parser, integer_value)) {
        *literal_kind = 2;
        return 1;
    }
    return 0;
}

static int dluma_lua_manifest_config_setting(
    DlumaLuaManifestParser* parser,
    DlumaLuaConfigSetting* setting,
    DlumaLuaMetadata* metadata)
{
    char kind[24];
    char default_text[DLUMA_LUA_CONFIG_VALUE_CAPACITY];
    int default_integer = 0;
    int default_literal_kind = 0;
    int saw_key = 0;
    int saw_name = 0;
    int saw_description = 0;
    int saw_kind = 0;
    int saw_default = 0;
    int saw_minimum = 0;
    int saw_maximum = 0;
    int saw_step = 0;
    int saw_max_length = 0;
    int saw_options = 0;
    unsigned int option_index;
    if (!parser || !setting ||
        !dluma_lua_manifest_take(parser, '{'))
        return 0;
    memset(setting, 0, sizeof(*setting));
    kind[0] = 0;
    default_text[0] = 0;
    for (;;) {
        char key[32];
        dluma_lua_manifest_skip_space(parser);
        if (dluma_lua_manifest_take(parser, '}')) break;
        if (!dluma_lua_manifest_identifier(
                parser, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_lua_manifest_take(parser, '='))
            return 0;
        if (dluma_stricmp(key, "key") == 0) {
            if (saw_key ||
                !dluma_lua_manifest_string(
                    parser, setting->key,
                    DLUMA_ARRAY_COUNT(setting->key)))
                return 0;
            saw_key = 1;
        } else if (dluma_stricmp(key, "name") == 0) {
            if (saw_name ||
                !dluma_lua_manifest_string(
                    parser, setting->name,
                    DLUMA_ARRAY_COUNT(setting->name)))
                return 0;
            saw_name = 1;
        } else if (dluma_stricmp(key, "description") == 0) {
            if (saw_description ||
                !dluma_lua_manifest_string(
                    parser, setting->description,
                    DLUMA_ARRAY_COUNT(setting->description)))
                return 0;
            saw_description = 1;
        } else if (dluma_stricmp(key, "type") == 0) {
            if (saw_kind ||
                !dluma_lua_manifest_string(
                    parser, kind, DLUMA_ARRAY_COUNT(kind)))
                return 0;
            saw_kind = 1;
        } else if (dluma_stricmp(key, "default") == 0) {
            if (saw_default ||
                !dluma_lua_manifest_config_default(
                    parser, &default_literal_kind,
                    &default_integer, default_text,
                    DLUMA_ARRAY_COUNT(default_text)))
                return 0;
            saw_default = 1;
        } else if (dluma_stricmp(key, "min") == 0) {
            if (saw_minimum ||
                !dluma_lua_manifest_i32(parser, &setting->minimum))
                return 0;
            saw_minimum = 1;
        } else if (dluma_stricmp(key, "max") == 0) {
            if (saw_maximum ||
                !dluma_lua_manifest_i32(parser, &setting->maximum))
                return 0;
            saw_maximum = 1;
        } else if (dluma_stricmp(key, "step") == 0) {
            if (saw_step ||
                !dluma_lua_manifest_u32(parser, &setting->step))
                return 0;
            saw_step = 1;
        } else if (dluma_stricmp(key, "max_length") == 0) {
            if (saw_max_length ||
                !dluma_lua_manifest_u32(
                    parser, &setting->max_length))
                return 0;
            saw_max_length = 1;
        } else if (dluma_stricmp(key, "options") == 0) {
            if (saw_options ||
                !dluma_lua_manifest_config_options(
                    parser, setting, metadata))
                return 0;
            saw_options = 1;
        } else {
            dluma_lua_config_diagnostic(metadata, "invalid_setting");
            return 0;
        }
        dluma_lua_manifest_skip_space(parser);
        if (dluma_lua_manifest_take(parser, '}')) break;
        if (!dluma_lua_manifest_take(parser, ',') &&
            !dluma_lua_manifest_take(parser, ';'))
            return 0;
    }
    if (!saw_key || !saw_name || !saw_description ||
        !saw_kind || !saw_default ||
        !dluma_lua_config_identifier_valid(setting->key) ||
        !dluma_lua_config_utf8_valid(
            setting->name, DLUMA_LUA_CONFIG_NAME_CAPACITY - 1U,
            DLUMA_LUA_CONFIG_NAME_CAPACITY - 1U, 0) ||
        !dluma_lua_config_utf8_valid(
            setting->description,
            DLUMA_LUA_CONFIG_DESCRIPTION_CAPACITY - 1U,
            DLUMA_LUA_CONFIG_DESCRIPTION_CAPACITY - 1U, 0)) {
        dluma_lua_config_diagnostic(metadata, "invalid_setting");
        return 0;
    }
    if (dluma_stricmp(kind, "boolean") == 0) {
        if (default_literal_kind != 1 ||
            saw_minimum || saw_maximum || saw_step ||
            saw_max_length || saw_options) {
            dluma_lua_config_diagnostic(metadata, "invalid_default");
            return 0;
        }
        setting->kind = DLUMA_LUA_CONFIG_BOOLEAN;
        setting->default_integer = default_integer;
    } else if (dluma_stricmp(kind, "integer") == 0) {
        LONGLONG span;
        LONGLONG default_offset;
        if (default_literal_kind != 2 ||
            !saw_minimum || !saw_maximum || !saw_step ||
            saw_max_length || saw_options ||
            !setting->step ||
            setting->minimum > setting->maximum ||
            default_integer < setting->minimum ||
            default_integer > setting->maximum) {
            dluma_lua_config_diagnostic(metadata, "invalid_default");
            return 0;
        }
        span = (LONGLONG)setting->maximum -
               (LONGLONG)setting->minimum;
        default_offset = (LONGLONG)default_integer -
                         (LONGLONG)setting->minimum;
        if ((ULONGLONG)span % setting->step ||
            (ULONGLONG)default_offset % setting->step) {
            dluma_lua_config_diagnostic(metadata, "invalid_default");
            return 0;
        }
        setting->kind = DLUMA_LUA_CONFIG_INTEGER;
        setting->default_integer = default_integer;
    } else if (dluma_stricmp(kind, "keybind") == 0) {
        if (default_literal_kind != 2 ||
            default_integer < 1 || default_integer > 254 ||
            (saw_minimum && setting->minimum != 1) ||
            (saw_maximum && setting->maximum != 254) ||
            (saw_step && setting->step != 1U) ||
            saw_max_length || saw_options) {
            dluma_lua_config_diagnostic(metadata, "invalid_default");
            return 0;
        }
        setting->kind = DLUMA_LUA_CONFIG_INTEGER;
        setting->editor = DLUMA_LUA_CONFIG_EDITOR_KEYBIND;
        setting->default_integer = default_integer;
        setting->minimum = 1;
        setting->maximum = 254;
        setting->step = 1U;
    } else if (dluma_stricmp(kind, "enum") == 0) {
        int default_found = 0;
        if (default_literal_kind != 3 ||
            saw_minimum || saw_maximum || saw_step ||
            saw_max_length || !saw_options ||
            setting->option_count < 2U) {
            dluma_lua_config_diagnostic(metadata, "invalid_default");
            return 0;
        }
        for (option_index = 0;
             option_index < setting->option_count;
             ++option_index)
            if (dluma_lua_config_text_equal(
                    setting->options[option_index].value,
                    default_text))
                default_found = 1;
        if (!default_found) {
            dluma_lua_config_diagnostic(metadata, "invalid_default");
            return 0;
        }
        setting->kind = DLUMA_LUA_CONFIG_ENUM;
        dluma_str_copy(
            setting->default_text,
            DLUMA_ARRAY_COUNT(setting->default_text),
            default_text);
    } else if (dluma_stricmp(kind, "text") == 0) {
        if (default_literal_kind != 3 ||
            saw_minimum || saw_maximum || saw_step ||
            !saw_max_length || saw_options ||
            !setting->max_length ||
            setting->max_length >
                DLUMA_LUA_CONFIG_MAX_TEXT_CHARACTERS ||
            !dluma_lua_config_utf8_valid(
                default_text,
                DLUMA_LUA_CONFIG_MAX_TEXT_BYTES,
                setting->max_length, 1)) {
            dluma_lua_config_diagnostic(metadata, "invalid_default");
            return 0;
        }
        setting->kind = DLUMA_LUA_CONFIG_TEXT;
        dluma_str_copy(
            setting->default_text,
            DLUMA_ARRAY_COUNT(setting->default_text),
            default_text);
    } else {
        dluma_lua_config_diagnostic(metadata, "invalid_setting");
        return 0;
    }
    return 1;
}

static int dluma_lua_manifest_config_category(
    DlumaLuaManifestParser* parser,
    DlumaLuaConfigSchema* schema,
    DlumaLuaConfigCategory* category,
    DlumaLuaMetadata* metadata)
{
    int saw_id = 0;
    int saw_name = 0;
    int saw_settings = 0;
    if (!parser || !schema || !category ||
        !dluma_lua_manifest_take(parser, '{'))
        return 0;
    memset(category, 0, sizeof(*category));
    for (;;) {
        char key[32];
        dluma_lua_manifest_skip_space(parser);
        if (dluma_lua_manifest_take(parser, '}')) break;
        if (!dluma_lua_manifest_identifier(
                parser, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_lua_manifest_take(parser, '='))
            return 0;
        if (dluma_stricmp(key, "id") == 0) {
            if (saw_id ||
                !dluma_lua_manifest_string(
                    parser, category->id,
                    DLUMA_ARRAY_COUNT(category->id)))
                return 0;
            saw_id = 1;
        } else if (dluma_stricmp(key, "name") == 0) {
            if (saw_name ||
                !dluma_lua_manifest_string(
                    parser, category->name,
                    DLUMA_ARRAY_COUNT(category->name)))
                return 0;
            saw_name = 1;
        } else if (dluma_stricmp(key, "settings") == 0) {
            if (saw_settings ||
                !dluma_lua_manifest_take(parser, '{'))
                return 0;
            saw_settings = 1;
            category->first_setting = schema->setting_count;
            for (;;) {
                unsigned int earlier;
                DlumaLuaConfigSetting* setting;
                dluma_lua_manifest_skip_space(parser);
                if (dluma_lua_manifest_take(parser, '}')) break;
                if (category->setting_count >=
                        DLUMA_LUA_CONFIG_MAX_SETTINGS_PER_CATEGORY ||
                    schema->setting_count >=
                        DLUMA_LUA_CONFIG_MAX_SETTINGS) {
                    dluma_lua_config_diagnostic(
                        metadata, "schema_limits");
                    return 0;
                }
                setting = &schema->settings[schema->setting_count];
                if (!dluma_lua_manifest_config_setting(
                        parser, setting, metadata))
                    return 0;
                for (earlier = 0;
                     earlier < schema->setting_count;
                     ++earlier) {
                    if (dluma_lua_config_text_equal(
                            schema->settings[earlier].key,
                            setting->key)) {
                        dluma_lua_config_diagnostic(
                            metadata, "duplicate_identifier");
                        return 0;
                    }
                }
                ++schema->setting_count;
                ++category->setting_count;
                dluma_lua_manifest_skip_space(parser);
                if (dluma_lua_manifest_take(parser, '}')) break;
                if (!dluma_lua_manifest_take(parser, ',') &&
                    !dluma_lua_manifest_take(parser, ';'))
                    return 0;
            }
        } else {
            dluma_lua_config_diagnostic(metadata, "invalid_category");
            return 0;
        }
        dluma_lua_manifest_skip_space(parser);
        if (dluma_lua_manifest_take(parser, '}')) break;
        if (!dluma_lua_manifest_take(parser, ',') &&
            !dluma_lua_manifest_take(parser, ';'))
            return 0;
    }
    if (!saw_id || !saw_name || !saw_settings ||
        !category->setting_count ||
        !dluma_lua_config_identifier_valid(category->id) ||
        !dluma_lua_config_utf8_valid(
            category->name, DLUMA_LUA_CONFIG_NAME_CAPACITY - 1U,
            DLUMA_LUA_CONFIG_NAME_CAPACITY - 1U, 0)) {
        dluma_lua_config_diagnostic(metadata, "invalid_category");
        return 0;
    }
    return 1;
}

static int dluma_lua_manifest_config_schema(
    DlumaLuaManifestParser* parser,
    DlumaLuaConfigSchema* schema,
    DlumaLuaMetadata* metadata)
{
    int saw_version = 0;
    int saw_categories = 0;
    if (!parser || !schema || !metadata ||
        !dluma_lua_manifest_take(parser, '{'))
        return 0;
    memset(schema, 0, sizeof(*schema));
    for (;;) {
        char key[32];
        dluma_lua_manifest_skip_space(parser);
        if (dluma_lua_manifest_take(parser, '}')) break;
        if (!dluma_lua_manifest_identifier(
                parser, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_lua_manifest_take(parser, '='))
            return 0;
        if (dluma_stricmp(key, "schema_version") == 0) {
            if (saw_version ||
                !dluma_lua_manifest_u32(
                    parser, &schema->schema_version))
                return 0;
            saw_version = 1;
        } else if (dluma_stricmp(key, "categories") == 0) {
            if (saw_categories ||
                !dluma_lua_manifest_take(parser, '{'))
                return 0;
            saw_categories = 1;
            for (;;) {
                unsigned int earlier;
                DlumaLuaConfigCategory* category;
                dluma_lua_manifest_skip_space(parser);
                if (dluma_lua_manifest_take(parser, '}')) break;
                if (schema->category_count >=
                    DLUMA_LUA_CONFIG_MAX_CATEGORIES) {
                    dluma_lua_config_diagnostic(
                        metadata, "schema_limits");
                    return 0;
                }
                category =
                    &schema->categories[schema->category_count];
                if (!dluma_lua_manifest_config_category(
                        parser, schema, category, metadata))
                    return 0;
                for (earlier = 0;
                     earlier < schema->category_count;
                     ++earlier) {
                    if (dluma_lua_config_text_equal(
                            schema->categories[earlier].id,
                            category->id)) {
                        dluma_lua_config_diagnostic(
                            metadata, "duplicate_identifier");
                        return 0;
                    }
                }
                ++schema->category_count;
                dluma_lua_manifest_skip_space(parser);
                if (dluma_lua_manifest_take(parser, '}')) break;
                if (!dluma_lua_manifest_take(parser, ',') &&
                    !dluma_lua_manifest_take(parser, ';'))
                    return 0;
            }
        } else {
            dluma_lua_config_diagnostic(
                metadata, "dynamic_or_malformed");
            return 0;
        }
        dluma_lua_manifest_skip_space(parser);
        if (dluma_lua_manifest_take(parser, '}')) break;
        if (!dluma_lua_manifest_take(parser, ',') &&
            !dluma_lua_manifest_take(parser, ';'))
            return 0;
    }
    if (!saw_version || !saw_categories ||
        schema->schema_version !=
            DLUMA_LUA_CONFIG_SCHEMA_VERSION ||
        !schema->category_count || !schema->setting_count) {
        dluma_lua_config_diagnostic(
            metadata,
            saw_version &&
                    schema->schema_version !=
                        DLUMA_LUA_CONFIG_SCHEMA_VERSION
                ? "unsupported_schema_version"
                : "dynamic_or_malformed");
        return 0;
    }
    dluma_lua_config_compute_fingerprint(schema);
    return schema->fingerprint[0] != 0;
}

static int dluma_lua_find_literal_mod_manifest(const char* data, DWORD size,
                                             DlumaLuaManifestParser* manifest)
{
    DlumaLuaManifestParser parser;
    parser.cursor = data;
    parser.end = data + size;
    while (parser.cursor < parser.end) {
        const char* start;
        char token[32];
        dluma_lua_manifest_skip_space(&parser);
        if (parser.cursor >= parser.end) break;
        if (*parser.cursor == '\'' || *parser.cursor == '"') {
            dluma_lua_manifest_skip_string(&parser);
            continue;
        }
        start = parser.cursor;
        if (dluma_lua_manifest_identifier(&parser, token,
                                        DLUMA_ARRAY_COUNT(token)) &&
            (dluma_stricmp(token, "dlua") == 0 ||
             dluma_stricmp(token, "dluma") == 0) &&
            dluma_lua_manifest_take(&parser, '.') &&
            dluma_lua_manifest_identifier(&parser, token,
                                        DLUMA_ARRAY_COUNT(token)) &&
            dluma_stricmp(token, "mod") == 0 &&
            dluma_lua_manifest_take(&parser, '{')) {
            *manifest = parser;
            return 1;
        }
        parser.cursor = start + 1;
    }
    return 0;
}

static int dluma_lua_parse_literal_mod_manifest(const char* data, DWORD size,
                                              DlumaLuaMetadata* out,
                                              int* saw_scope,
                                              DlumaLuaConfigSchema* schema)
{
    DlumaLuaManifestParser parser;
    int saw_id = 0;
    int saw_name = 0;
    int saw_version = 0;
    int saw_config = 0;
    if (!dluma_lua_find_literal_mod_manifest(data, size, &parser)) return 0;
    for (;;) {
        char key[64];
        dluma_lua_manifest_skip_space(&parser);
        if (dluma_lua_manifest_take(&parser, '}')) break;
        if (!dluma_lua_manifest_identifier(&parser, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_lua_manifest_take(&parser, '=')) return -1;
        if (dluma_stricmp(key, "id") == 0) {
            if (!dluma_lua_manifest_string(&parser, out->id,
                                         DLUMA_ARRAY_COUNT(out->id))) return -1;
            saw_id = 1;
        } else if (dluma_stricmp(key, "name") == 0) {
            if (!dluma_lua_manifest_string(&parser, out->name,
                                         DLUMA_ARRAY_COUNT(out->name))) return -1;
            saw_name = 1;
        } else if (dluma_stricmp(key, "description") == 0) {
            if (!dluma_lua_manifest_string(
                    &parser, out->description,
                    DLUMA_ARRAY_COUNT(out->description))) return -1;
        } else if (dluma_stricmp(key, "version") == 0) {
            if (!dluma_lua_manifest_string(&parser, out->version,
                                         DLUMA_ARRAY_COUNT(out->version))) return -1;
            saw_version = 1;
        } else if (dluma_stricmp(key, "scope") == 0) {
            char scope[32];
            if (!dluma_lua_manifest_string(&parser, scope,
                                         DLUMA_ARRAY_COUNT(scope))) return -1;
            *saw_scope = 1;
            if (dluma_stricmp(scope, "global") == 0)
                out->scope = DLUMA_LUA_SCOPE_GLOBAL;
            else if (dluma_stricmp(scope, "project") == 0 ||
                     dluma_stricmp(scope, "local") == 0)
                out->scope = DLUMA_LUA_SCOPE_PROJECT;
            else if (dluma_stricmp(scope, "map") == 0)
                out->scope = DLUMA_LUA_SCOPE_MAP;
            else
                return -1;
        } else if (dluma_stricmp(key, "enabled_by_default") == 0) {
            char value[16];
            if (!dluma_lua_manifest_identifier(&parser, value,
                                              DLUMA_ARRAY_COUNT(value))) return -1;
            if (dluma_stricmp(value, "true") == 0) out->enabled_by_default = 1;
            else if (dluma_stricmp(value, "false") == 0) out->enabled_by_default = 0;
            else return -1;
        } else if (dluma_stricmp(key, "permissions") == 0) {
            if (!dluma_lua_manifest_permissions(
                    &parser, out->permissions,
                    DLUMA_ARRAY_COUNT(out->permissions))) return -1;
        } else if (dluma_stricmp(key, "config") == 0) {
            const char* value_start;
            if (saw_config || !schema) return -1;
            saw_config = 1;
            out->config_declared = 1;
            value_start = parser.cursor;
            if (!dluma_lua_manifest_config_schema(
                    &parser, schema, out)) {
                /*
                 * A config schema is optional metadata. A dynamic,
                 * unsupported, or semantically invalid schema disables only
                 * Configure when its value remains a syntactically bounded
                 * part of the otherwise-valid literal manifest.
                 */
                parser.cursor = value_start;
                if (!dluma_lua_manifest_skip_value(&parser) ||
                    parser.cursor >= parser.end)
                    return -1;
                dluma_lua_config_diagnostic(
                    out, "dynamic_or_malformed");
                memset(schema, 0, sizeof(*schema));
            }
        } else if (!dluma_lua_manifest_skip_value(&parser)) {
            return -1;
        }
        dluma_lua_manifest_skip_space(&parser);
        if (dluma_lua_manifest_take(&parser, '}')) break;
        if (!dluma_lua_manifest_take(&parser, ',') &&
            !dluma_lua_manifest_take(&parser, ';')) return -1;
    }
    return saw_id && saw_name && saw_version ? 1 : -1;
}

typedef struct DlumaLuaPermissionUse {
    const char* source_text;
    const char* permission;
} DlumaLuaPermissionUse;

static int dluma_lua_permission_declared(const char* list,
                                       const char* permission)
{
    unsigned int wanted;
    const char* cursor = list;
    if (!list || !permission) return 0;
    wanted = (unsigned int)strlen(permission);
    while (*cursor) {
        const char* start;
        unsigned int length;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') ++cursor;
        start = cursor;
        while (*cursor && *cursor != ',') ++cursor;
        length = (unsigned int)(cursor - start);
        while (length && (start[length - 1] == ' ' ||
                          start[length - 1] == '\t')) --length;
        if (length == wanted) {
            unsigned int i;
            int matches = 1;
            for (i = 0; i < wanted; ++i) {
                if (dluma_ascii_lower(start[i]) !=
                    dluma_ascii_lower(permission[i])) {
                    matches = 0;
                    break;
                }
            }
            if (matches) return 1;
        }
        if (*cursor == ',') ++cursor;
    }
    return 0;
}

static int dluma_lua_source_uses(const char* data, DWORD size,
                               const char* pattern)
{
    const char* cursor = data;
    const char* end = data + size;
    unsigned int length = (unsigned int)strlen(pattern);
    while (cursor < end) {
        if (cursor + 1 < end && cursor[0] == '-' && cursor[1] == '-') {
            cursor += 2;
            if (cursor + 1 < end && cursor[0] == '[' && cursor[1] == '[') {
                cursor += 2;
                while (cursor + 1 < end &&
                       !(cursor[0] == ']' && cursor[1] == ']')) ++cursor;
                if (cursor + 1 < end) cursor += 2;
            } else {
                while (cursor < end && *cursor != '\r' && *cursor != '\n')
                    ++cursor;
            }
            continue;
        }
        if (*cursor == '\'' || *cursor == '"') {
            char quote = *cursor++;
            while (cursor < end && *cursor != quote) {
                if (*cursor++ == '\\' && cursor < end) ++cursor;
            }
            if (cursor < end) ++cursor;
            continue;
        }
        if ((unsigned int)(end - cursor) >= length &&
            memcmp(cursor, pattern, length) == 0)
            return 1;
        ++cursor;
    }
    return 0;
}

static void dluma_lua_validate_permission_use(const char* data, DWORD size,
                                            DlumaLuaMetadata* out)
{
    static const DlumaLuaPermissionUse uses[] = {
        { "dlua.world.find", "world.query" },
        { "dlua.world.set_enabled_many", "world.write" },
        { "dlua.sensor.is_inside", "events.sensor" },
        { ":on_sensor_enter", "events.sensor" },
        { ":on_sensor_leave", "events.sensor" },
        { ":on_interact_begin", "events.interact" },
        { ":on_enemy_killed", "events.enemy_killed" },
        { ":handle_quest_phase", "quest.phase.handle" },
        { ":observe_quest_phase", "quest.phase.observe" },
        /* Quest APIs target authored engine state.  The compatibility names
         * also use this permission so every direct quest call is declared
         * consistently before its runtime capability check fails closed. */
        { "dlua.quest.", "quest.write" },
        { "dlua.storage.", "storage.mod" },
        { "dlua.debug.", "ui.debug" },
        { "dlua.ui.", "ui.overlay" },
        { "dlua.audio.", "audio.play" },
        { "dlua.player.", "player.read" },
        { "dlua.action.", "world.write" },
        { "dlua.modifier.", "world.write" },
        { "dlua.std.inside_sensor", "events.sensor" },
        { "dlua.std.object_enabled", "world.query" },
        { "dlua.std.require_object", "world.query" },
        { "dlua.std.player_health_below", "player.read" }
    };
    char missing[256];
    unsigned int i;
    missing[0] = 0;
    out->permission_diagnostic[0] = 0;
    for (i = 0; i < DLUMA_ARRAY_COUNT(uses); ++i) {
        if (!dluma_lua_source_uses(data, size, uses[i].source_text) ||
            dluma_lua_permission_declared(out->permissions,
                                        uses[i].permission) ||
            dluma_lua_permission_declared(missing, uses[i].permission))
            continue;
        if (missing[0]) dluma_str_append(missing, DLUMA_ARRAY_COUNT(missing), ", ");
        dluma_str_append(missing, DLUMA_ARRAY_COUNT(missing), uses[i].permission);
    }
    if (missing[0]) {
        dluma_str_append(out->permission_diagnostic,
                       DLUMA_ARRAY_COUNT(out->permission_diagnostic),
                       "Lua permission mismatch [");
        dluma_str_append(out->permission_diagnostic,
                       DLUMA_ARRAY_COUNT(out->permission_diagnostic),
                       out->id[0] ? out->id : "unknown script");
        dluma_str_append(out->permission_diagnostic,
                       DLUMA_ARRAY_COUNT(out->permission_diagnostic),
                       "]: API use requires undeclared permission(s): ");
        dluma_str_append(out->permission_diagnostic,
                       DLUMA_ARRAY_COUNT(out->permission_diagnostic), missing);
        dluma_str_append(out->permission_diagnostic,
                       DLUMA_ARRAY_COUNT(out->permission_diagnostic),
                       ". Add them to permissions = { ... } in dlua.mod.");
    }
}

static const WCHAR* dluma_lua_project_relative_path(
    const WCHAR* path,
    const WCHAR* project_root)
{
    unsigned int i = 0;
    if (!path || !project_root) return 0;
    while (project_root[i]) {
        WCHAR left = path[i];
        WCHAR right = project_root[i];
        if (left == L'/') left = L'\\';
        if (right == L'/') right = L'\\';
        if (!left || dluma_wascii_lower(left) != dluma_wascii_lower(right))
            return 0;
        ++i;
    }
    if (path[i] != L'\\' && path[i] != L'/') return 0;
    while (path[i] == L'\\' || path[i] == L'/') ++i;
    return path + i;
}

static unsigned char dluma_lua_infer_scope_from_path(
    const WCHAR* path,
    const WCHAR* project_root)
{
    const WCHAR* relative = dluma_lua_project_relative_path(path, project_root);
    const WCHAR* cursor;
    if (!relative) return DLUMA_LUA_SCOPE_INVALID;
    if (dluma_wstarts_with_icase(relative, L"data\\scripts\\lua\\global\\"))
        return DLUMA_LUA_SCOPE_GLOBAL;
    if (dluma_wstarts_with_icase(relative, L"data\\scripts\\lua\\project\\"))
        return DLUMA_LUA_SCOPE_PROJECT;
    if (dluma_wstarts_with_icase(relative, L"data\\scripts\\lua\\maps\\")) {
        cursor = relative + wcslen(L"data\\scripts\\lua\\maps\\");
        while (*cursor && *cursor != L'\\' && *cursor != L'/') ++cursor;
        if (cursor[0] && cursor[1]) return DLUMA_LUA_SCOPE_MAP;
    }
    if (dluma_wstarts_with_icase(relative, L"data\\maps\\")) {
        cursor = relative + wcslen(L"data\\maps\\");
        while (*cursor && *cursor != L'\\' && *cursor != L'/') ++cursor;
        if (cursor[0]) {
            while (*cursor == L'\\' || *cursor == L'/') ++cursor;
            if (dluma_wstarts_with_icase(cursor, L"scripts\\lua\\") ||
                dluma_wstarts_with_icase(cursor, L"scripts\\dluma\\"))
                return DLUMA_LUA_SCOPE_MAP;
        }
    }
    return DLUMA_LUA_SCOPE_INVALID;
}

static const char* dluma_lua_access_name(unsigned int id)
{
    unsigned int index;
    for (index = 0;
         index < DLUMA_GENERATED_ACCESS_NAME_COUNT;
         ++index)
        if (g_dluma_generated_access_names[index].id == id)
            return g_dluma_generated_access_names[index].name;
    return 0;
}

static int dluma_lua_parse_package_metadata(
    const WCHAR* manifest_path,
    const WCHAR* project_root,
    DlumaLuaMetadata* out,
    int* out_has_dluma_header)
{
    DlumaModPackageSnapshot package;
    DlumaModArtifactSnapshot config_artifact;
    DlumaLuaConfigSchema* config_schema = 0;
    WCHAR package_root[DLUMA_MAX_WPATH];
    unsigned int access_index;
    int result = 0;
    if (!manifest_path || !project_root || !out)
        return 0;
    if (out_has_dluma_header) *out_has_dluma_header = 1;
    memset(out, 0, sizeof(*out));
    memset(&package, 0, sizeof(package));
    memset(&config_artifact, 0, sizeof(config_artifact));
    package.struct_size = (unsigned int)sizeof(package);
    config_artifact.struct_size =
        (unsigned int)sizeof(config_artifact);
    dluma_wstr_copy(
        package_root, DLUMA_ARRAY_COUNT(package_root),
        manifest_path);
    dluma_path_parent(package_root);
    if (dluma_mod_package_discover(
            package_root, &package) != DLUMA_STATUS_OK ||
        !package.valid ||
        package.manifest.language != DLUMA_MOD_LANGUAGE_LUA ||
        !dluma_mod_manifest_expected_suffix(
            &package.manifest, ".lua"))
        return 0;
    out->enabled_by_default = 1;
    out->manifest_present = 1;
    out->scope = (unsigned char)package.manifest.scope;
    dluma_wstr_copy(
        out->file_path, DLUMA_ARRAY_COUNT(out->file_path),
        package.entry_path);
    dluma_wstr_copy(
        out->project_root, DLUMA_ARRAY_COUNT(out->project_root),
        project_root);
    dluma_wstr_copy(
        out->package_root, DLUMA_ARRAY_COUNT(out->package_root),
        package.package_root);
    dluma_str_copy(
        out->id, DLUMA_ARRAY_COUNT(out->id),
        package.manifest.id);
    dluma_str_copy(
        out->name, DLUMA_ARRAY_COUNT(out->name),
        package.manifest.name);
    dluma_str_copy(
        out->description, DLUMA_ARRAY_COUNT(out->description),
        package.manifest.description);
    dluma_str_copy(
        out->version, DLUMA_ARRAY_COUNT(out->version),
        package.manifest.version);
    dluma_str_copy(
        out->api_version, DLUMA_ARRAY_COUNT(out->api_version),
        DLUMA_API_PUBLIC_VERSION);
    dluma_str_copy(
        out->content_sha256,
        DLUMA_ARRAY_COUNT(out->content_sha256),
        package.entry_sha256);
    dluma_str_copy(
        out->package_entry,
        DLUMA_ARRAY_COUNT(out->package_entry),
        package.manifest.entry);
    dluma_str_copy(
        out->manifest_sha256,
        DLUMA_ARRAY_COUNT(out->manifest_sha256),
        package.manifest_sha256);
    dluma_str_copy(
        out->lock_sha256,
        DLUMA_ARRAY_COUNT(out->lock_sha256),
        package.lock_sha256);
    out->source_size = (DWORD)package.entry_size;
    out->source_volume_serial = package.entry_volume_serial;
    out->source_file_index_high =
        package.entry_file_index_high;
    out->source_file_index_low =
        package.entry_file_index_low;
    out->access_count = package.manifest.access_count;
    for (access_index = 0;
         access_index < package.manifest.access_count;
         ++access_index) {
        const char* access_name = dluma_lua_access_name(
            package.manifest.access_ids[access_index]);
        if (!access_name) return 0;
        out->access_ids[access_index] =
            package.manifest.access_ids[access_index];
        if (out->permissions[0])
            dluma_str_append(
                out->permissions,
                DLUMA_ARRAY_COUNT(out->permissions), ", ");
        dluma_str_append(
            out->permissions,
            DLUMA_ARRAY_COUNT(out->permissions), access_name);
    }
    out->declared_host_flags = package.manifest.host_flags;
    out->load_priority = package.manifest.load_priority;
    out->quest_phase_handler_count =
        package.manifest.quest_phase_handler_count;
    for (access_index = 0;
         access_index < package.manifest.quest_phase_handler_count;
         ++access_index) {
        dluma_str_copy(
            out->quest_phase_handlers[access_index].id,
            DLUMA_ARRAY_COUNT(out->quest_phase_handlers[access_index].id),
            package.manifest.quest_phase_handlers[access_index].id);
        dluma_str_copy(
            out->quest_phase_handlers[access_index].command_alias,
            DLUMA_ARRAY_COUNT(
                out->quest_phase_handlers[access_index].command_alias),
            package.manifest
                .quest_phase_handlers[access_index].command_alias);
    }
    dluma_describe_project_source(
        project_root,
        out->source_name, DLUMA_ARRAY_COUNT(out->source_name),
        out->workshop_id, DLUMA_ARRAY_COUNT(out->workshop_id),
        &out->source_is_workshop);
    out->valid = 1;
    if (out->scope == DLUMA_LUA_SCOPE_PROJECT ||
        out->scope == DLUMA_LUA_SCOPE_MAP) {
        out->enabled_requested = 1;
        out->enabled = 1;
    }
    if (out->scope == DLUMA_LUA_SCOPE_GLOBAL) {
        if (!dluma_lua_prepare_global_identity(out))
            return 0;
    } else if (out->scope == DLUMA_LUA_SCOPE_PROJECT ||
               out->scope == DLUMA_LUA_SCOPE_MAP) {
        if (!dluma_lua_prepare_local_identity(out))
            return 0;
    } else {
        return 0;
    }
    if (package.manifest.configuration_schema[0]) {
        out->config_declared = 1;
        config_schema = (DlumaLuaConfigSchema*)dluma_alloc(
            sizeof(*config_schema));
        if (!config_schema) {
            dluma_str_copy(
                out->config_diagnostic,
                DLUMA_ARRAY_COUNT(out->config_diagnostic),
                "config_schema_allocation_failed");
        } else {
            memset(config_schema, 0, sizeof(*config_schema));
            if (dluma_mod_package_read_artifact(
                    &package,
                    package.manifest.configuration_schema,
                    DLUMA_MOD_ARTIFACT_CONFIGURATION,
                    DLUMA_NATIVE_MOD_CONFIG_MAX_JSON_BYTES,
                    &config_artifact) != DLUMA_STATUS_OK) {
                dluma_str_copy(
                    out->config_diagnostic,
                    DLUMA_ARRAY_COUNT(out->config_diagnostic),
                    config_artifact.diagnostic[0]
                        ? config_artifact.diagnostic
                        : "locked_config_schema_unavailable");
            } else if (dluma_mod_config_parse_json(
                    (const char*)config_artifact.bytes,
                    config_artifact.size, config_schema,
                    out->config_diagnostic,
                    DLUMA_ARRAY_COUNT(out->config_diagnostic))) {
                out->config_available = 1;
                out->config_category_count =
                    (unsigned short)config_schema->category_count;
                out->config_setting_count =
                    (unsigned short)config_schema->setting_count;
                dluma_str_copy(
                    out->config_schema_fingerprint,
                    DLUMA_ARRAY_COUNT(
                        out->config_schema_fingerprint),
                    config_schema->fingerprint);
                if (!dluma_lua_config_publish(
                        out, config_schema)) {
                    out->config_available = 0;
                    dluma_str_copy(
                        out->config_diagnostic,
                        DLUMA_ARRAY_COUNT(out->config_diagnostic),
                        "config_registry_publish_failed");
                }
            }
            dluma_mod_artifact_release(&config_artifact);
        }
    }
    result = 1;
    dluma_free(config_schema);
    return result;
}

static int dluma_lua_parse_metadata(
    const WCHAR* path,
    const WCHAR* project_root,
    DlumaLuaMetadata* out,
    int* out_has_dluma_header)
{
    char* data = 0;
    DlumaLuaConfigSchema* config_schema = 0;
    DWORD size = 0;
    char* cursor;
    unsigned int lines = 0;
    int saw_metadata = 0;
    int saw_scope = 0;
    unsigned char declared_scope = DLUMA_LUA_SCOPE_INVALID;
    unsigned char inferred_scope;
    int manifest_result;
    int result = 0;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    out->enabled_by_default = 1;
    dluma_wstr_copy(out->file_path, DLUMA_MAX_WPATH, path);
    dluma_wstr_copy(out->project_root, DLUMA_MAX_WPATH, project_root);
    dluma_describe_project_source(project_root,
                                out->source_name, DLUMA_ARRAY_COUNT(out->source_name),
                                out->workshop_id, DLUMA_ARRAY_COUNT(out->workshop_id),
                                &out->source_is_workshop);
    if (!dluma_lua_read_validated_source(
            path, project_root, &data, &size, out)) return 0;
    config_schema = (DlumaLuaConfigSchema*)dluma_alloc(
        sizeof(*config_schema));
    if (!config_schema) {
        dluma_free(data);
        return 0;
    }
    memset(config_schema, 0, sizeof(*config_schema));

    dluma_sha256_bytes_hex(data, size, out->content_sha256);

    manifest_result = dluma_lua_parse_literal_mod_manifest(
        data, size, out, &saw_scope, config_schema);
    if (manifest_result) {
        saw_metadata = 1;
        if (manifest_result < 0) {
            if (out_has_dluma_header) *out_has_dluma_header = 1;
            goto cleanup;
        }
        declared_scope = out->scope;
    } else {
        /* Backward compatibility for 0.2.x scripts. New scripts use the one
         * literal dlua.mod table as their pre-execution manifest. */
        cursor = data;
        while (*cursor && lines < 96) {
            char line[1024];
            unsigned int n = 0;
            char* p;
            char* eq;
            while (*cursor && *cursor != '\r' && *cursor != '\n' &&
                   n + 1 < DLUMA_ARRAY_COUNT(line))
                line[n++] = *cursor++;
            line[n] = 0;
            while (*cursor == '\r' || *cursor == '\n') ++cursor;
            ++lines;
            dluma_trim_in_place(line);
            if (!line[0]) continue;
            if (!dluma_lua_starts_with(line, "--")) break;
            p = line + 2;
            while (*p == ' ' || *p == '\t') ++p;
            if (dluma_lua_starts_with(p, "dlua:"))
                p += 5;
            else if (dluma_lua_starts_with(p, "dluma:"))
                p += 4;
            else
                continue;
            saw_metadata = 1;
            eq = p;
            while (*eq && *eq != '=') ++eq;
            if (*eq != '=') continue;
            *eq++ = 0;
            dluma_trim_in_place(p);
            dluma_lua_parse_value(eq);
            if (dluma_stricmp(p, "id") == 0)
                dluma_str_copy(out->id, DLUMA_ARRAY_COUNT(out->id), eq);
            else if (dluma_stricmp(p, "name") == 0)
                dluma_str_copy(out->name, DLUMA_ARRAY_COUNT(out->name), eq);
            else if (dluma_stricmp(p, "description") == 0)
                dluma_str_copy(
                    out->description, DLUMA_ARRAY_COUNT(out->description), eq);
            else if (dluma_stricmp(p, "version") == 0)
                dluma_str_copy(out->version, DLUMA_ARRAY_COUNT(out->version), eq);
            else if (dluma_stricmp(p, "permissions") == 0)
                dluma_str_copy(out->permissions,
                             DLUMA_ARRAY_COUNT(out->permissions), eq);
            else if (dluma_stricmp(p, "scope") == 0) {
                saw_scope = 1;
                if (dluma_stricmp(eq, "global") == 0)
                    declared_scope = DLUMA_LUA_SCOPE_GLOBAL;
                else if (dluma_stricmp(eq, "project") == 0 ||
                         dluma_stricmp(eq, "local") == 0)
                    declared_scope = DLUMA_LUA_SCOPE_PROJECT;
                else if (dluma_stricmp(eq, "map") == 0)
                    declared_scope = DLUMA_LUA_SCOPE_MAP;
            } else if (dluma_stricmp(p, "enabled_by_default") == 0) {
                out->enabled_by_default = (unsigned char)(
                    dluma_stricmp(eq, "false") != 0 &&
                    dluma_stricmp(eq, "0") != 0 &&
                    dluma_stricmp(eq, "off") != 0);
            }
        }
    }
    if (saw_metadata)
        dluma_lua_validate_permission_use(data, size, out);
    dluma_free(data);
    data = 0;
    if (out_has_dluma_header) *out_has_dluma_header = saw_metadata;
    if (!saw_metadata) goto cleanup;
    inferred_scope = dluma_lua_infer_scope_from_path(path, project_root);
    if (saw_scope && declared_scope == DLUMA_LUA_SCOPE_INVALID)
        goto cleanup;
    if (declared_scope != DLUMA_LUA_SCOPE_INVALID &&
        inferred_scope != DLUMA_LUA_SCOPE_INVALID &&
        declared_scope != inferred_scope)
        goto cleanup;
    out->scope = declared_scope != DLUMA_LUA_SCOPE_INVALID
        ? declared_scope : inferred_scope;
    /* Map scripts must be physically owned by the active internal-map
     * directory. This prevents a map-scoped file in a shared project folder
     * from accidentally loading for every map in that project. */
    if (out->scope == DLUMA_LUA_SCOPE_MAP &&
        inferred_scope != DLUMA_LUA_SCOPE_MAP)
        goto cleanup;
    if (!dluma_lua_id_valid(out->id) ||
        out->scope == DLUMA_LUA_SCOPE_INVALID)
        goto cleanup;
    if (!out->name[0]) dluma_str_copy(out->name, DLUMA_ARRAY_COUNT(out->name), out->id);
    if (!out->version[0]) dluma_str_copy(out->version, DLUMA_ARRAY_COUNT(out->version), "0.0.0");
    out->valid = 1;
    if (out->scope == DLUMA_LUA_SCOPE_PROJECT ||
        out->scope == DLUMA_LUA_SCOPE_MAP) {
        /* Local project/map scripts do not use the global-mod consent
         * registry. Their explicit metadata default is their runtime state. */
        out->enabled_requested = out->enabled_by_default;
        out->enabled = out->enabled_by_default;
    }
    if (out->scope == DLUMA_LUA_SCOPE_GLOBAL &&
        !dluma_lua_prepare_global_identity(out))
        goto cleanup;
    if ((out->scope == DLUMA_LUA_SCOPE_PROJECT ||
         out->scope == DLUMA_LUA_SCOPE_MAP) &&
        !dluma_lua_prepare_local_identity(out))
        goto cleanup;
    if (out->config_declared) {
        if (out->scope != DLUMA_LUA_SCOPE_GLOBAL) {
            dluma_lua_config_diagnostic(
                out, "unsupported_scope");
        } else if (!out->config_diagnostic[0] &&
                   config_schema->fingerprint[0]) {
            dluma_str_copy(
                out->config_schema_fingerprint,
                DLUMA_ARRAY_COUNT(out->config_schema_fingerprint),
                config_schema->fingerprint);
            out->config_category_count =
                (unsigned short)config_schema->category_count;
            out->config_setting_count =
                (unsigned short)config_schema->setting_count;
            out->config_available = 1;
            if (!dluma_lua_config_publish(out, config_schema)) {
                out->config_available = 0;
                out->config_category_count = 0;
                out->config_setting_count = 0;
                out->config_schema_fingerprint[0] = 0;
                dluma_lua_config_diagnostic(
                    out, "registry_unavailable");
            }
        }
    }
    result = 1;

cleanup:
    dluma_free(data);
    dluma_free(config_schema);
    return result;
}

static int dluma_lua_parse_metadata_cached(
    const WCHAR* path, const WCHAR* project_root,
    DlumaLuaMetadata* out, int* out_has_dluma_header,
    int force_full, DlumaLuaDiscoveryMetrics* metrics)
{
    DlumaLuaFileKey before;
    DlumaLuaFileKey after;
    DlumaLuaMetadataCacheEntry* entry;
    int saw_header = 0;
    int parsed;
    if (path &&
        dluma_wends_with_icase(path, L"\\mod.dluma.json")) {
        if (metrics) ++metrics->cache_misses;
        return dluma_lua_parse_package_metadata(
            path, project_root, out, out_has_dluma_header);
    }
    if (!out || !dluma_lua_workspace_initialize() ||
        !dluma_lua_probe_file_key(path, project_root, &before))
        return 0;
    if (!force_full) {
        entry = dluma_lua_cache_find(&before);
        if (entry) {
            int reusable = 1;
            memcpy(out, &entry->metadata, sizeof(*out));
            if (entry->parse_succeeded &&
                out->config_available) {
                DlumaLuaConfigSchema* schema =
                    (DlumaLuaConfigSchema*)dluma_alloc(
                        sizeof(*schema));
                reusable =
                    schema &&
                    dluma_lua_config_snapshot(
                        out->registry_key,
                        out->metadata_fingerprint,
                        out->config_schema_fingerprint,
                        schema);
                dluma_free(schema);
            }
            if (!reusable)
                goto cache_miss;
            entry->last_used = ++g_dluma_lua_workspace.cache_clock;
            if (out_has_dluma_header)
                *out_has_dluma_header = entry->saw_header != 0;
            if (metrics) ++metrics->cache_hits;
            return entry->parse_succeeded != 0;
        }
    }
cache_miss:
    if (metrics) {
        ++metrics->cache_misses;
        metrics->bytes_read +=
            ((ULONGLONG)before.size_high << 32) |
            before.size_low;
    }
    parsed = dluma_lua_parse_metadata(
        path, project_root, out, &saw_header);
    if (parsed) {
        out->legacy_unsupported = 1;
        out->valid = 0;
        out->enabled = 0;
        out->enabled_requested = 0;
        out->approval_valid = 0;
        out->pending_review = 0;
        out->review_state = DLUMA_GLOBAL_REVIEW_INVALID;
        out->config_available = 0;
        dluma_str_copy(
            out->api_version, DLUMA_ARRAY_COUNT(out->api_version),
            "2.1");
        dluma_str_copy(
            out->runtime_error,
            DLUMA_ARRAY_COUNT(out->runtime_error),
            "Legacy Lua API 2.1 is unsupported; migrate this mod to "
            "mod.dluma.json and DLUMA API 1.0");
    }
    if (!dluma_lua_probe_file_key(path, project_root, &after) ||
        !dluma_lua_file_key_equal(&before, &after)) {
        memset(out, 0, sizeof(*out));
        if (out_has_dluma_header) *out_has_dluma_header = saw_header;
        return 0;
    }
    entry = dluma_lua_cache_find(&after);
    if (!entry) entry = dluma_lua_cache_slot();
    memset(entry, 0, sizeof(*entry));
    entry->key = after;
    entry->metadata = *out;
    entry->last_used = ++g_dluma_lua_workspace.cache_clock;
    entry->occupied = 1;
    entry->parse_succeeded = (unsigned char)(parsed ? 1 : 0);
    entry->saw_header = (unsigned char)(saw_header ? 1 : 0);
    if (out_has_dluma_header) *out_has_dluma_header = saw_header;
    return parsed;
}

static int dluma_lua_phase_provider_find(
    DlumaLuaQuestPhaseProviderMetadata* entries,
    unsigned int count,
    const char* id)
{
    unsigned int index;
    if (!entries || !id || !id[0]) return -1;
    for (index = 0; index < count; ++index)
        if (dluma_stricmp(entries[index].id, id) == 0)
            return (int)index;
    return -1;
}

void dluma_lua_discover_editor_quest_phase_metadata(void)
{
    WCHAR (*projects)[DLUMA_MAX_WPATH];
    WCHAR (*paths)[DLUMA_MAX_WPATH];
    WCHAR (*owners)[DLUMA_MAX_WPATH];
    DlumaLuaQuestPhaseProviderMetadata* providers = 0;
    unsigned int project_count;
    unsigned int path_count = 0;
    unsigned int provider_count = 0;
    unsigned int project_index;
    unsigned int path_index;
    if (!dluma_lua_workspace_initialize() ||
        dluma_atomic_compare_exchange(
            &g_dluma_lua_workspace.scan_active, 1, 0) != 0) {
        dluma_log_line(
            "WARN", "editor_lua_phase_metadata_scan_unavailable");
        return;
    }
    projects = g_dluma_lua_workspace.projects;
    paths = g_dluma_lua_workspace.paths;
    owners = g_dluma_lua_workspace.owners;
    providers = (DlumaLuaQuestPhaseProviderMetadata*)dluma_alloc(
        DLUMA_MAX_LUA_FILES * sizeof(*providers));
    if (!providers) goto done;
    memset(projects, 0, sizeof(*projects) * DLUMA_MAX_LUA_FILES);
    memset(paths, 0, sizeof(*paths) * DLUMA_MAX_LUA_FILES);
    memset(owners, 0, sizeof(*owners) * DLUMA_MAX_LUA_FILES);
    memset(providers, 0, DLUMA_MAX_LUA_FILES * sizeof(*providers));
    project_count = dluma_lua_collect_project_roots(
        projects, DLUMA_MAX_LUA_FILES);
    for (project_index = 0; project_index < project_count;
         ++project_index) {
        path_count = dluma_lua_collect_project_files(
            projects[project_index], 0, paths, owners,
            path_count, DLUMA_MAX_LUA_FILES);
        path_count = dluma_lua_collect_editor_map_phase_files(
            projects[project_index], paths, owners,
            path_count, DLUMA_MAX_LUA_FILES);
    }
    dluma_lua_sort_paths(paths, owners, path_count);
    for (path_index = 0; path_index < path_count;
         ++path_index) {
        DlumaLuaMetadata item;
        DlumaLuaQuestPhaseProviderMetadata* provider;
        int saw_header = 0;
        int duplicate;
        unsigned int handler_index;
        memset(&item, 0, sizeof(item));
        if (!dluma_lua_parse_metadata_cached(
                paths[path_index], owners[path_index], &item,
                &saw_header, 1, 0) || !item.valid ||
            !item.manifest_present || !item.quest_phase_handler_count ||
            !(item.declared_host_flags & DLUMA_MOD_HOST_PLAYER))
            continue;
        if (provider_count >= DLUMA_MAX_LUA_FILES) {
            dluma_log_line(
                "WARN", "editor_lua_phase_provider_limit_reached");
            break;
        }
        duplicate = dluma_lua_phase_provider_find(
            providers, provider_count, item.id);
        provider = &providers[provider_count++];
        memset(provider, 0, sizeof(*provider));
        dluma_str_copy(provider->id, DLUMA_ARRAY_COUNT(provider->id), item.id);
        dluma_str_copy(
            provider->manifest_sha256,
            DLUMA_ARRAY_COUNT(provider->manifest_sha256),
            item.manifest_sha256);
        dluma_str_copy(
            provider->content_sha256,
            DLUMA_ARRAY_COUNT(provider->content_sha256),
            item.content_sha256);
        provider->declared_host_flags = item.declared_host_flags;
        provider->load_priority = item.load_priority;
        provider->scope = item.scope;
        provider->quest_phase_handler_count =
            item.quest_phase_handler_count;
        for (handler_index = 0;
             handler_index < item.quest_phase_handler_count;
             ++handler_index)
            provider->quest_phase_handlers[handler_index] =
                item.quest_phase_handlers[handler_index];
        if (duplicate >= 0) {
            providers[duplicate].duplicate_id = 1U;
            provider->duplicate_id = 1U;
        }
    }
    dluma_lua_registry_lock();
    memset(g_dluma.lua_discovery.editor_phase_providers, 0,
           sizeof(g_dluma.lua_discovery.editor_phase_providers));
    if (provider_count)
        memcpy(g_dluma.lua_discovery.editor_phase_providers,
               providers, provider_count * sizeof(*providers));
    g_dluma.lua_discovery.editor_phase_provider_count = provider_count;
    dluma_lua_registry_unlock();
    dluma_log_u64(
        "INFO", "editor_lua_phase_providers", provider_count);
done:
    if (providers) dluma_free(providers);
    dluma_atomic_exchange(&g_dluma_lua_workspace.scan_active, 0);
}

unsigned int dluma_lua_snapshot_editor_quest_phase_providers(
    DlumaLuaQuestPhaseProviderMetadata* out,
    unsigned int capacity)
{
    unsigned int count;
    unsigned int copy_count;
    dluma_lua_registry_lock();
    count = g_dluma.lua_discovery.editor_phase_provider_count;
    copy_count = count < capacity ? count : capacity;
    if (out && copy_count)
        memcpy(out, g_dluma.lua_discovery.editor_phase_providers,
               copy_count * sizeof(*out));
    dluma_lua_registry_unlock();
    return count;
}

static int dluma_lua_find_id(DlumaLuaMetadata* entries, unsigned int count, const char* id)
{
    unsigned int i;
    for (i = 0; i < count; ++i) {
        if (entries[i].valid && dluma_stricmp(entries[i].id, id) == 0) return (int)i;
    }
    return -1;
}

static int dluma_lua_find_registry_key(DlumaLuaMetadata* entries,
                                     unsigned int count,
                                     const char* registry_key)
{
    unsigned int i;
    if (!registry_key || !registry_key[0]) return -1;
    for (i = 0; i < count; ++i) {
        if (entries[i].valid &&
            dluma_stricmp(entries[i].registry_key, registry_key) == 0)
            return (int)i;
    }
    return -1;
}

static ULONGLONG dluma_lua_hash_text(ULONGLONG hash, const char* text)
{
    unsigned int i;
    for (i = 0; text && text[i]; ++i) {
        hash ^= (ULONGLONG)(unsigned char)dluma_ascii_lower(text[i]);
        hash *= 1099511628211ULL;
    }
    hash ^= 0xFFu;
    return hash * 1099511628211ULL;
}

static ULONGLONG dluma_lua_hash_wtext(ULONGLONG hash, const WCHAR* text)
{
    unsigned int i;
    for (i = 0; text && text[i]; ++i) {
        hash ^= (ULONGLONG)dluma_wascii_lower(text[i]);
        hash *= 1099511628211ULL;
    }
    hash ^= 0xFEu;
    return hash * 1099511628211ULL;
}

static ULONGLONG dluma_lua_hash_global_registry(
    const DlumaLuaMetadata* entries,
    unsigned int count)
{
    ULONGLONG hash = 1469598103934665603ULL;
    unsigned int i;
    for (i = 0; i < count; ++i) {
        hash = dluma_lua_hash_text(hash, entries[i].id);
        hash = dluma_lua_hash_text(hash, entries[i].name);
        hash = dluma_lua_hash_text(hash, entries[i].version);
        hash = dluma_lua_hash_text(hash, entries[i].api_version);
        hash = dluma_lua_hash_text(hash, entries[i].permissions);
        hash = dluma_lua_hash_text(hash, entries[i].normalized_permissions);
        hash = dluma_lua_hash_text(hash, entries[i].source_name);
        hash = dluma_lua_hash_text(hash, entries[i].workshop_id);
        hash = dluma_lua_hash_text(hash, entries[i].source_identity);
        hash = dluma_lua_hash_text(hash, entries[i].canonical_relative_path);
        hash ^= (ULONGLONG)entries[i].declared_host_flags;
        hash *= 1099511628211ULL;
        hash ^= (ULONGLONG)(entries[i].load_priority + 1000);
        hash *= 1099511628211ULL;
        {
            unsigned int handler_index;
            for (handler_index = 0;
                 handler_index < entries[i].quest_phase_handler_count;
                 ++handler_index) {
                hash = dluma_lua_hash_text(
                    hash,
                    entries[i].quest_phase_handlers[handler_index].id);
                hash = dluma_lua_hash_text(
                    hash,
                    entries[i]
                        .quest_phase_handlers[handler_index]
                        .command_alias);
            }
        }
        hash = dluma_lua_hash_text(hash, entries[i].package_entry);
        hash = dluma_lua_hash_text(hash, entries[i].registry_key);
        hash = dluma_lua_hash_text(hash, entries[i].storage_key);
        hash = dluma_lua_hash_text(hash, entries[i].content_sha256);
        hash = dluma_lua_hash_text(hash, entries[i].manifest_sha256);
        hash = dluma_lua_hash_text(hash, entries[i].lock_sha256);
        hash = dluma_lua_hash_text(hash, entries[i].metadata_fingerprint);
        hash = dluma_lua_hash_text(hash, entries[i].permission_fingerprint);
        hash = dluma_lua_hash_text(hash, entries[i].seen_fingerprint);
        hash = dluma_lua_hash_text(hash, entries[i].approved_fingerprint);
        hash = dluma_lua_hash_text(hash, entries[i].prompted_fingerprint);
        hash = dluma_lua_hash_text(hash, entries[i].change_reason);
        hash = dluma_lua_hash_text(hash, entries[i].runtime_error);
        hash = dluma_lua_hash_text(
            hash, entries[i].config_schema_fingerprint);
        hash = dluma_lua_hash_text(
            hash, entries[i].config_diagnostic);
        hash = dluma_lua_hash_wtext(hash, entries[i].file_path);
        hash ^= entries[i].source_is_workshop;
        hash *= 1099511628211ULL;
        hash ^= entries[i].source_is_subscribed;
        hash *= 1099511628211ULL;
        hash ^= entries[i].source_selected;
        hash *= 1099511628211ULL;
        hash ^= entries[i].source_selection_missing;
        hash *= 1099511628211ULL;
        hash ^= entries[i].manifest_present;
        hash *= 1099511628211ULL;
        hash ^= entries[i].legacy_unsupported;
        hash *= 1099511628211ULL;
        hash ^= entries[i].enabled_by_default;
        hash *= 1099511628211ULL;
        hash ^= entries[i].enabled;
        hash *= 1099511628211ULL;
        hash ^= entries[i].has_enabled_override;
        hash *= 1099511628211ULL;
        hash ^= entries[i].review_state;
        hash *= 1099511628211ULL;
        hash ^= entries[i].is_new;
        hash *= 1099511628211ULL;
        hash ^= entries[i].approval_valid;
        hash *= 1099511628211ULL;
        hash ^= entries[i].enabled_requested;
        hash *= 1099511628211ULL;
        hash ^= entries[i].loaded;
        hash *= 1099511628211ULL;
        hash ^= entries[i].runtime_transition;
        hash *= 1099511628211ULL;
        hash ^= entries[i].scope;
        hash *= 1099511628211ULL;
        hash ^= entries[i].duplicate_id;
        hash *= 1099511628211ULL;
        hash ^= entries[i].config_declared;
        hash *= 1099511628211ULL;
        hash ^= entries[i].config_available;
        hash *= 1099511628211ULL;
        hash ^= entries[i].config_category_count;
        hash *= 1099511628211ULL;
        hash ^= entries[i].config_setting_count;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void dluma_lua_rehash_globals_locked(void)
{
    g_dluma.lua_discovery.global_registry_hash =
        dluma_lua_hash_global_registry(
            g_dluma.lua_discovery.global_files,
            g_dluma.lua_discovery.global_count);
}

static void dluma_lua_log_metadata(const char* label, const DlumaLuaMetadata* item)
{
    char line[1024];
    char path[DLUMA_MAX_WPATH * 2];
    path[0] = 0;
    dluma_wide_to_utf8(item->file_path, path, DLUMA_ARRAY_COUNT(path));
    line[0] = 0;
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), item->id);
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), " | ");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), item->name);
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), " | v");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), item->version);
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), " | ");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), item->enabled_by_default ? "default=on" : "default=off");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), item->enabled_requested ? " | requested=on" : " | requested=off");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), item->approval_valid ? " | approved=yes" : " | approved=no");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), item->is_new ? " | NEW" : " | seen");
    if (item->has_enabled_override)
        dluma_str_append(line, DLUMA_ARRAY_COUNT(line), " (user override)");
    if (item->permissions[0]) {
        dluma_str_append(line, DLUMA_ARRAY_COUNT(line), " | permissions=");
        dluma_str_append(line, DLUMA_ARRAY_COUNT(line), item->permissions);
    }
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), " | source=");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), item->source_name);
    if (item->source_is_workshop && item->workshop_id[0]) {
        dluma_str_append(line, DLUMA_ARRAY_COUNT(line), " | workshop_id=");
        dluma_str_append(line, DLUMA_ARRAY_COUNT(line), item->workshop_id);
    } else {
        dluma_str_append(line, DLUMA_ARRAY_COUNT(line), " | local_project");
    }
    dluma_log_pair("INFO", label, line);
    dluma_log_pair("INFO", "lua_file", path);
    dluma_log_pair("INFO", "lua_metadata_fingerprint", item->metadata_fingerprint);
    dluma_log_pair("INFO", "lua_registry_key", item->registry_key);
    dluma_log_pair(
        "INFO", "lua_permission_fingerprint",
        item->permission_fingerprint);
    dluma_log_pair("INFO", "lua_source_identity", item->source_identity);
    dluma_log_pair(
        "INFO", "lua_canonical_relative_path",
        item->canonical_relative_path);
    if (item->manifest_present) {
        dluma_log_pair(
            "INFO", "lua_manifest_sha256", item->manifest_sha256);
        dluma_log_pair("INFO", "lua_lock_sha256", item->lock_sha256);
    }
}

static void dluma_lua_append_report_entry(char* report, unsigned int cap, const DlumaLuaMetadata* item)
{
    char path[DLUMA_MAX_WPATH * 2];
    path[0] = 0;
    dluma_wide_to_utf8(item->file_path, path, DLUMA_ARRAY_COUNT(path));
    dluma_str_append(report, cap, item->duplicate_id ? "[CONFLICT] " : "[OK] ");
    if (item->scope == DLUMA_LUA_SCOPE_GLOBAL)
        dluma_str_append(report, cap, "GLOBAL ");
    else if (item->scope == DLUMA_LUA_SCOPE_PROJECT)
        dluma_str_append(report, cap, "PROJECT ");
    else if (item->scope == DLUMA_LUA_SCOPE_MAP)
        dluma_str_append(report, cap, "MAP ");
    else
        dluma_str_append(report, cap, "INVALID ");
    dluma_str_append(report, cap, item->id);
    dluma_str_append(report, cap, " v");
    dluma_str_append(report, cap, item->version);
    dluma_str_append(report, cap, item->enabled_by_default ? " default=on" : " default=off");
    dluma_str_append(report, cap, item->enabled_requested ? " requested=on" : " requested=off");
    dluma_str_append(report, cap, item->approval_valid ? " approved=yes" : " approved=no");
    if (item->is_new) dluma_str_append(report, cap, " NEW");
    if (item->has_enabled_override) dluma_str_append(report, cap, " override=yes");
    dluma_str_append(report, cap, "\r\n  ");
    dluma_str_append(report, cap, path);
    dluma_str_append(report, cap, "\r\n");
    dluma_str_append(report, cap, "  source: ");
    dluma_str_append(report, cap, item->source_name);
    if (item->source_is_workshop && item->workshop_id[0]) {
        dluma_str_append(report, cap, " (Workshop ");
        dluma_str_append(report, cap, item->workshop_id);
        dluma_str_append(report, cap, ")");
    } else {
        dluma_str_append(report, cap, " (local project)");
    }
    dluma_str_append(report, cap, "\r\n");
    if (item->permissions[0]) {
        dluma_str_append(report, cap, "  permissions: ");
        dluma_str_append(report, cap, item->permissions);
        dluma_str_append(report, cap, "\r\n");
    }
    dluma_str_append(report, cap, "  metadata fingerprint: ");
    dluma_str_append(report, cap, item->metadata_fingerprint);
    dluma_str_append(report, cap, "\r\n  content SHA-256: ");
    dluma_str_append(report, cap, item->content_sha256);
    if (item->config_declared) {
        dluma_str_append(report, cap, "\r\n  config: ");
        dluma_str_append(
            report, cap,
            item->config_available ? "available" : "unavailable");
        if (item->config_schema_fingerprint[0]) {
            dluma_str_append(
                report, cap, "\r\n  config schema fingerprint: ");
            dluma_str_append(
                report, cap, item->config_schema_fingerprint);
        }
        if (item->config_diagnostic[0]) {
            dluma_str_append(
                report, cap, "\r\n  config diagnostic: ");
            dluma_str_append(
                report, cap, item->config_diagnostic);
        }
    }
    dluma_str_append(report, cap, "\r\n");
}

static void dluma_lua_write_report(void)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR runtime_dir[DLUMA_MAX_WPATH];
    char* report;
    DlumaLuaMetadata* global_snapshot;
    char number[64];
    unsigned int global_count;
    ULONGLONG revision = 0;
    unsigned int i;
    report = (char*)dluma_alloc(128UL * 1024UL);
    if (!report) return;
    global_snapshot = (DlumaLuaMetadata*)dluma_alloc(
        sizeof(DlumaLuaMetadata) * DLUMA_MAX_LUA_FILES);
    if (!global_snapshot) {
        dluma_free(report);
        return;
    }
    memset(global_snapshot, 0,
           sizeof(DlumaLuaMetadata) * DLUMA_MAX_LUA_FILES);
    global_count = dluma_lua_snapshot_globals_with_revision(
        global_snapshot, DLUMA_MAX_LUA_FILES, 0, &revision);
    if (global_count > DLUMA_MAX_LUA_FILES)
        global_count = DLUMA_MAX_LUA_FILES;
    report[0] = 0;
    dluma_str_append(report, 128UL * 1024UL, "DLUMA Lua discovery probe\r\n");
    dluma_str_append(report, 128UL * 1024UL, "Version: " DLUMA_VERSION "\r\n\r\n");
    dluma_str_append(report, 128UL * 1024UL, "Global files: ");
    dluma_u64_to_text(global_count, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(report, 128UL * 1024UL, number);
    dluma_str_append(report, 128UL * 1024UL, "\r\nProject files for active project: ");
    dluma_u64_to_text(g_dluma.lua_discovery.project_count, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(report, 128UL * 1024UL, number);
    dluma_str_append(report, 128UL * 1024UL, "\r\nMap files for active map: ");
    dluma_u64_to_text(g_dluma.lua_discovery.map_count, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(report, 128UL * 1024UL, number);
    dluma_str_append(report, 128UL * 1024UL, "\r\nInvalid metadata files: ");
    dluma_u64_to_text(g_dluma.lua_discovery.invalid_count, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(report, 128UL * 1024UL, number);
    dluma_str_append(report, 128UL * 1024UL, "\r\nDuplicate IDs: ");
    dluma_u64_to_text(g_dluma.lua_discovery.duplicate_count, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(report, 128UL * 1024UL, number);
    dluma_str_append(report, 128UL * 1024UL, "\r\nGlobal registry revision: ");
    dluma_u64_to_text(revision, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(report, 128UL * 1024UL, number);
    dluma_str_append(report, 128UL * 1024UL, "\r\n\r\n");
    for (i = 0; i < global_count; ++i)
        dluma_lua_append_report_entry(report, 128UL * 1024UL,
                                    &global_snapshot[i]);
    for (i = 0; i < g_dluma.lua_discovery.local_count; ++i)
        dluma_lua_append_report_entry(report, 128UL * 1024UL, &g_dluma.lua_discovery.local_files[i]);
    dluma_path_join(runtime_dir, DLUMA_ARRAY_COUNT(runtime_dir), g_dluma.module_dir, L"dluma_runtime");
    dluma_path_join(path, DLUMA_ARRAY_COUNT(path), runtime_dir, L"lua_discovery_report.txt");
    dluma_write_utf8_file(path, report, 0);
    dluma_free(global_snapshot);
    dluma_free(report);
}


unsigned int dluma_lua_snapshot_globals(
    DlumaLuaMetadata* out,
    unsigned int capacity,
    ULONGLONG* hash_out)
{
    return dluma_lua_snapshot_globals_with_revision(
        out, capacity, hash_out, 0);
}

unsigned int dluma_lua_snapshot_globals_with_revision(
    DlumaLuaMetadata* out,
    unsigned int capacity,
    ULONGLONG* hash_out,
    ULONGLONG* revision_out)
{
    unsigned int total;
    unsigned int copy_count;
    unsigned int i;
    dluma_lua_registry_lock();
    total = g_dluma.lua_discovery.global_count;
    copy_count = total;
    if (copy_count > capacity) copy_count = capacity;
    if (out) {
        for (i = 0; i < copy_count; ++i)
            out[i] = g_dluma.lua_discovery.global_files[i];
    }
    if (hash_out) *hash_out = g_dluma.lua_discovery.global_registry_hash;
    if (revision_out)
        *revision_out = g_dluma.lua_discovery.global_registry_revision;
    dluma_lua_registry_unlock();
    return total;
}

int dluma_lua_set_global_enabled(const char* id, int enabled)
{
    DlumaGlobalMutationRequest request;
    unsigned int i;
    int found = -1;
    memset(&request, 0, sizeof(request));
    if (!id || !id[0]) return 0;
    dluma_lua_registry_lock();
    for (i = 0; i < g_dluma.lua_discovery.global_count; ++i) {
        DlumaLuaMetadata* item = &g_dluma.lua_discovery.global_files[i];
        if (!item->valid || dluma_stricmp(item->id, id) != 0) continue;
        if (item->duplicate_id || found >= 0) {
            found = -2;
            break;
        }
        found = (int)i;
    }
    if (found >= 0) {
        DlumaLuaMetadata* item = &g_dluma.lua_discovery.global_files[found];
        dluma_str_copy(request.registry_key, DLUMA_ARRAY_COUNT(request.registry_key),
                     item->registry_key);
        dluma_str_copy(request.metadata_fingerprint,
                     DLUMA_ARRAY_COUNT(request.metadata_fingerprint),
                     item->metadata_fingerprint);
        request.registry_revision =
            g_dluma.lua_discovery.global_registry_revision;
    }
    dluma_lua_registry_unlock();
    if (found < 0) return 0;
    return dluma_lua_request_global_enabled(&request, enabled, 0);
}

int dluma_lua_initial_global_discovery_complete(void)
{
    return dluma_atomic_compare_exchange(
        &g_dluma.lua_discovery.global_initial_discovery_complete, 0, 0) != 0;
}

int dluma_lua_disable_all_globals_next_launch(void)
{
    DlumaLuaMetadata* snapshot;
    unsigned int disabled = 0;
    unsigned int attempts = 0;
    int complete = 1;
    snapshot = (DlumaLuaMetadata*)dluma_alloc(
        sizeof(*snapshot) * DLUMA_MAX_LUA_FILES);
    if (!snapshot) return 0;
    for (;;) {
        unsigned int total;
        unsigned int count;
        unsigned int i;
        int found = 0;
        ULONGLONG revision = 0;
        total = dluma_lua_snapshot_globals_with_revision(
            snapshot, DLUMA_MAX_LUA_FILES, 0, &revision);
        count = total > DLUMA_MAX_LUA_FILES ? DLUMA_MAX_LUA_FILES : total;
        for (i = 0; i < count; ++i) {
            DlumaGlobalMutationRequest request;
            if (!snapshot[i].enabled_requested) continue;
            memset(&request, 0, sizeof(request));
            dluma_str_copy(request.registry_key,
                         DLUMA_ARRAY_COUNT(request.registry_key),
                         snapshot[i].registry_key);
            dluma_str_copy(request.metadata_fingerprint,
                         DLUMA_ARRAY_COUNT(request.metadata_fingerprint),
                         snapshot[i].metadata_fingerprint);
            request.registry_revision = revision;
            found = 1;
            if (!dluma_lua_request_global_enabled(&request, 0, 0))
                complete = 0;
            else
                ++disabled;
            break;
        }
        if (!found || !complete || ++attempts > DLUMA_MAX_LUA_FILES) break;
    }
    dluma_free(snapshot);
    dluma_log_u64("WARN", "startup_recovery_global_lua_disabled", disabled);
    return complete;
}

static const char* dluma_lua_discovery_mode_name(
    DlumaLuaDiscoveryMode mode)
{
    switch (mode) {
    case DLUMA_LUA_DISCOVERY_FULL_STARTUP:
        return "full_startup";
    case DLUMA_LUA_DISCOVERY_FULL_CONSISTENCY:
        return "full_consistency";
    case DLUMA_LUA_DISCOVERY_FALLBACK:
        return "watcher_fallback";
    default:
        return "incremental";
    }
}

void dluma_lua_discover_globals(void)
{
    dluma_lua_discover_globals_mode(
        DLUMA_LUA_DISCOVERY_FULL_STARTUP);
}

void dluma_lua_discover_globals_mode(DlumaLuaDiscoveryMode mode)
{
    WCHAR (*projects)[DLUMA_MAX_WPATH];
    WCHAR (*paths)[DLUMA_MAX_WPATH];
    WCHAR (*owners)[DLUMA_MAX_WPATH];
    DlumaLuaMetadata* next = 0;
    DlumaLuaDiscoveryMetrics metrics;
    ULONGLONG started_ms = GetTickCount64();
    unsigned int project_count;
    unsigned int path_count = 0;
    unsigned int next_count = 0;
    unsigned int invalid_count = 0;
    unsigned int duplicate_count = 0;
    unsigned char selection_processed[DLUMA_MAX_LUA_FILES];
    unsigned int p, i;
    ULONGLONG hash;
    ULONGLONG scan_start_revision = 0;
    ULONGLONG committed_revision = 0;
    int force_full =
        mode == DLUMA_LUA_DISCOVERY_FULL_STARTUP ||
        mode == DLUMA_LUA_DISCOVERY_FULL_CONSISTENCY;
    int scan_claimed = 0;

    /* A raw early-recovery scan runs from InitializeGameScriptDLL, where a
     * near-megabyte discovery scratch frame risks exhausting the game's
     * startup stack. Keep the normal worker path and this pre-game path on
     * the same bounded heap-backed storage. */
    memset(&metrics, 0, sizeof(metrics));
    memset(selection_processed, 0, sizeof(selection_processed));
    metrics.mode = (unsigned int)mode;
    if (!dluma_lua_workspace_initialize()) {
        dluma_log_line("ERROR", "global_lua_discovery_scratch_alloc_failed");
        goto cleanup;
    }
    if (dluma_atomic_compare_exchange(
            &g_dluma_lua_workspace.scan_active, 1, 0) != 0) {
        dluma_log_line("WARN", "global_lua_discovery_scan_already_active");
        goto cleanup;
    }
    scan_claimed = 1;
    projects = g_dluma_lua_workspace.projects;
    paths = g_dluma_lua_workspace.paths;
    owners = g_dluma_lua_workspace.owners;
    next = g_dluma_lua_workspace.next;
    g_dluma_lua_current_metrics = &metrics;

    dluma_lua_registry_lock();
    scan_start_revision = g_dluma.lua_discovery.global_registry_revision;
    dluma_lua_registry_unlock();

    memset(
        projects, 0,
        sizeof(*projects) * DLUMA_MAX_LUA_FILES);
    memset(
        paths, 0,
        sizeof(*paths) * DLUMA_MAX_LUA_FILES);
    memset(
        owners, 0,
        sizeof(*owners) * DLUMA_MAX_LUA_FILES);
    memset(
        next, 0,
        sizeof(*next) * DLUMA_MAX_LUA_FILES);
    project_count = dluma_lua_collect_project_roots(projects, DLUMA_MAX_LUA_FILES);
    for (p = 0; p < project_count; ++p)
        path_count = dluma_lua_collect_project_files(projects[p], 0, paths, owners, path_count, DLUMA_MAX_LUA_FILES);
    dluma_lua_sort_paths(paths, owners, path_count);

    for (i = 0; i < path_count && next_count < DLUMA_MAX_LUA_FILES; ++i) {
        DlumaLuaMetadata item;
        int saw_header = 0;
        int existing;
        if (!dluma_lua_parse_metadata_cached(
                paths[i], owners[i], &item, &saw_header,
                force_full, &metrics)) {
            if (saw_header) {
                ++invalid_count;
                dluma_lua_log_path("invalid_lua_metadata", paths[i]);
            }
            continue;
        }
        if (item.permission_diagnostic[0])
            dluma_log_line("ERROR", item.permission_diagnostic);
        if (item.scope != DLUMA_LUA_SCOPE_GLOBAL) continue;
        {
            int previous_index;
            dluma_lua_registry_lock();
            previous_index = dluma_lua_find_registry_key(
                g_dluma.lua_discovery.global_files,
                g_dluma.lua_discovery.global_count,
                item.registry_key);
            if (previous_index >= 0)
                dluma_lua_classify_global_review(
                    &item,
                    &g_dluma.lua_discovery.global_files[previous_index]);
            else
                dluma_lua_classify_global_review(&item, 0);
            dluma_lua_registry_unlock();
        }
        existing = -1;
        {
            unsigned int duplicate_index;
            for (duplicate_index = 0; duplicate_index < next_count;
                 ++duplicate_index) {
                if (dluma_stricmp(next[duplicate_index].id, item.id) == 0 &&
                    dluma_stricmp(next[duplicate_index].source_identity,
                                item.source_identity) == 0) {
                    existing = (int)duplicate_index;
                    break;
                }
            }
        }
        if (existing >= 0) {
            next[existing].duplicate_id = 1;
            next[existing].enabled = 0;
            next[existing].enabled_requested = 0;
            next[existing].approval_valid = 0;
            next[existing].pending_review = 1;
            next[existing].review_state = DLUMA_GLOBAL_REVIEW_CONFLICT;
            item.duplicate_id = 1;
            item.enabled = 0;
            item.enabled_requested = 0;
            item.approval_valid = 0;
            item.pending_review = 1;
            item.review_state = DLUMA_GLOBAL_REVIEW_CONFLICT;
            ++duplicate_count;
            dluma_log_pair("ERROR", "duplicate_global_lua_id", item.id);
        }
        next[next_count++] = item;
    }

    /* A declared ID is one logical mod. Distinct canonical sources are
     * alternatives, while physically ambiguous providers from the same source
     * remain fail-closed conflicts. A provider that failed validation is still
     * an alternative the player may resolve to: it is preferred last, never
     * loaded (every load gate checks `valid` separately), and keeping it
     * selectable is what stops the source picker from becoming a dead end for
     * a mod whose copies are all currently invalid. */
    for (i = 0; i < next_count; ++i) {
        char selected_source[512];
        int explicit_selection = 0;
        int selected_index = -1;
        int local_index = -1;
        int any_index = -1;
        int any_local_index = -1;
        int explicit_invalid_index = -1;
        unsigned int j;
        if (selection_processed[i]) continue;
        selected_source[0] = 0;
        dluma_mod_source_selection_read(
            "lua", next[i].id, selected_source,
            DLUMA_ARRAY_COUNT(selected_source), &explicit_selection);
        for (j = i; j < next_count; ++j) {
            if (dluma_stricmp(next[i].id, next[j].id) != 0) continue;
            if (next[j].duplicate_id) continue;
            if (explicit_selection &&
                dluma_stricmp(next[j].source_identity,
                            selected_source) == 0) {
                if (next[j].valid)
                    selected_index = (int)j;
                else if (explicit_invalid_index < 0)
                    explicit_invalid_index = (int)j;
            }
            if (!next[j].source_is_subscribed &&
                (any_local_index < 0 ||
                 dluma_wstricmp(next[j].project_root,
                              next[any_local_index].project_root) < 0))
                any_local_index = (int)j;
            if (any_index < 0 ||
                dluma_wstricmp(next[j].project_root,
                             next[any_index].project_root) < 0)
                any_index = (int)j;
            if (!next[j].valid) continue;
            if (!next[j].source_is_subscribed &&
                (local_index < 0 ||
                 dluma_wstricmp(next[j].project_root,
                              next[local_index].project_root) < 0))
                local_index = (int)j;
            if (!explicit_selection &&
                (selected_index < 0 ||
                 dluma_wstricmp(next[j].project_root,
                              next[selected_index].project_root) < 0))
                selected_index = (int)j;
        }
        if (!explicit_selection && local_index >= 0)
            selected_index = local_index;
        if (explicit_selection) {
            /* An explicit choice stays honoured while that exact copy is
             * present, even when it currently fails validation. */
            if (selected_index < 0)
                selected_index = explicit_invalid_index;
        } else if (selected_index < 0) {
            selected_index =
                any_local_index >= 0 ? any_local_index : any_index;
        }
        if (!explicit_selection && selected_index >= 0)
            dluma_mod_source_selection_write(
                "lua", next[i].id,
                next[selected_index].source_identity);
        for (j = i; j < next_count; ++j) {
            if (dluma_stricmp(next[i].id, next[j].id) != 0) continue;
            selection_processed[j] = 1;
            next[j].source_selected = (unsigned char)
                ((int)j == selected_index ? 1 : 0);
            next[j].source_selection_missing = (unsigned char)
                (explicit_selection && selected_index < 0 ? 1 : 0);
            if (!next[j].source_selected) next[j].enabled = 0;
        }
    }

    dluma_lua_registry_lock();
    if (scan_start_revision !=
        g_dluma.lua_discovery.global_registry_revision) {
        /* A UI review/approval completed during the scan. Merge only an exact
         * same-fingerprint state; stale consent never transfers by ID. */
        for (i = 0; i < next_count; ++i) {
            int old_index = dluma_lua_find_registry_key(
                g_dluma.lua_discovery.global_files,
                g_dluma.lua_discovery.global_count,
                next[i].registry_key);
            if (old_index >= 0 && !next[i].duplicate_id &&
                dluma_stricmp(next[i].metadata_fingerprint,
                            g_dluma.lua_discovery.global_files[old_index]
                                .metadata_fingerprint) == 0) {
                DlumaLuaMetadata refreshed = next[i];
                dluma_lua_classify_global_review(
                    &refreshed,
                    &g_dluma.lua_discovery.global_files[old_index]);
                next[i] = refreshed;
            }
        }
    }
    hash = dluma_lua_hash_global_registry(next, next_count);
    if (hash == g_dluma.lua_discovery.global_registry_hash &&
        next_count == g_dluma.lua_discovery.global_count &&
        invalid_count == g_dluma.lua_discovery.global_invalid_count &&
        duplicate_count == g_dluma.lua_discovery.global_duplicate_count) {
        dluma_atomic_exchange(
            &g_dluma.lua_discovery.global_initial_discovery_complete, 1);
        dluma_lua_registry_unlock();
        goto cleanup;
    }

    for (i = 0; i < g_dluma.lua_discovery.global_count; ++i) {
        unsigned int j;
        const DlumaLuaMetadata* old_item =
            &g_dluma.lua_discovery.global_files[i];
        int exact_still_present = 0;
        if (!old_item->loaded) continue;
        for (j = 0; j < next_count; ++j) {
            if (dluma_stricmp(old_item->registry_key,
                            next[j].registry_key) == 0 &&
                dluma_stricmp(old_item->metadata_fingerprint,
                            next[j].metadata_fingerprint) == 0) {
                exact_still_present = 1;
                break;
            }
        }
        if (!exact_still_present)
            dluma_log_pair("WARN", "global_state_unload_queued", old_item->id);
    }
    memset(g_dluma.lua_discovery.global_files, 0, sizeof(g_dluma.lua_discovery.global_files));
    for (i = 0; i < next_count; ++i) g_dluma.lua_discovery.global_files[i] = next[i];
    g_dluma.lua_discovery.global_count = next_count;
    g_dluma.lua_discovery.global_invalid_count = invalid_count;
    g_dluma.lua_discovery.global_duplicate_count = duplicate_count;
    g_dluma.lua_discovery.invalid_count =
        g_dluma.lua_discovery.global_invalid_count + g_dluma.lua_discovery.local_invalid_count;
    g_dluma.lua_discovery.duplicate_count =
        g_dluma.lua_discovery.global_duplicate_count + g_dluma.lua_discovery.local_duplicate_count;
    g_dluma.lua_discovery.global_registry_hash = hash;
    ++g_dluma.lua_discovery.global_registry_revision;
    dluma_lua_recompute_unseen_batch_locked();
    committed_revision = g_dluma.lua_discovery.global_registry_revision;
    dluma_atomic_exchange(
        &g_dluma.lua_discovery.global_initial_discovery_complete, 1);
    dluma_lua_registry_unlock();
    dluma_log_u64("INFO", "lua_projects_scanned", project_count);
    dluma_log_u64("INFO", "global_lua_files_found", next_count);
    dluma_log_u64("INFO", "global_lua_duplicate_ids", duplicate_count);
    dluma_log_u64("INFO", "global_mod_registry_revision", committed_revision);
    dluma_log_u64("INFO", "new_global_batch_computed",
                g_dluma.lua_discovery.current_unseen_count);
    for (i = 0; i < next_count; ++i) dluma_lua_log_metadata("global_lua", &next[i]);
    dluma_lua_write_report();

cleanup:
    if (scan_claimed)
        dluma_lua_config_prune(next, next_count);
    g_dluma_lua_current_metrics = 0;
    if (scan_claimed)
        dluma_atomic_exchange(
            &g_dluma_lua_workspace.scan_active, 0);
    metrics.duration_ms = GetTickCount64() - started_ms;
    dluma_log_pair(
        "INFO", "lua_discovery_mode",
        dluma_lua_discovery_mode_name(mode));
    dluma_log_u64(
        "INFO", "lua_discovery_duration_ms",
        metrics.duration_ms);
    dluma_log_u64(
        "INFO", "lua_discovery_projects_visited",
        metrics.projects_visited);
    dluma_log_u64(
        "INFO", "lua_discovery_files_visited",
        metrics.files_visited);
    dluma_log_u64(
        "INFO", "lua_discovery_source_bytes_read",
        metrics.bytes_read);
    dluma_log_u64(
        "INFO", "lua_discovery_cache_hits",
        metrics.cache_hits);
    dluma_log_u64(
        "INFO", "lua_discovery_cache_misses",
        metrics.cache_misses);
    dluma_log_u64(
        "WARN", "lua_discovery_projects_truncated",
        metrics.projects_truncated);
    dluma_log_u64(
        "WARN", "lua_discovery_files_truncated",
        metrics.files_truncated);
}

unsigned int dluma_lua_discover_locals(const WCHAR* project_root, const char* map_name)
{
    WCHAR paths[DLUMA_MAX_LUA_FILES][DLUMA_MAX_WPATH];
    WCHAR owners[DLUMA_MAX_LUA_FILES][DLUMA_MAX_WPATH];
    unsigned int path_count = 0;
    unsigned int count = 0;
    unsigned int local_invalid_count = 0;
    unsigned int local_duplicate_count = 0;
    unsigned int project_count = 0;
    unsigned int map_count = 0;
    unsigned int i;
    memset(paths, 0, sizeof(paths));
    memset(owners, 0, sizeof(owners));
    memset(g_dluma.lua_discovery.local_files, 0, sizeof(g_dluma.lua_discovery.local_files));
    g_dluma.lua_discovery.local_count = 0;
    g_dluma.lua_discovery.project_count = 0;
    g_dluma.lua_discovery.map_count = 0;
    g_dluma.lua_discovery.local_invalid_count = 0;
    g_dluma.lua_discovery.local_duplicate_count = 0;
    if (!project_root || !project_root[0]) return 0;
    path_count = dluma_lua_collect_project_files(project_root, map_name, paths, owners, 0, DLUMA_MAX_LUA_FILES);
    dluma_lua_sort_paths(paths, owners, path_count);
    dluma_log_u64("INFO", "active_project_lua_files_scanned", path_count);
    for (i = 0; i < path_count && count < DLUMA_MAX_LUA_FILES; ++i) {
        DlumaLuaMetadata item;
        int saw_header = 0;
        int existing_local;
        int existing_global;
        if (!dluma_lua_parse_metadata_cached(
                paths[i], owners[i], &item, &saw_header, 0, 0)) {
            if (saw_header) {
                ++local_invalid_count;
                dluma_lua_log_path("invalid_local_lua_metadata", paths[i]);
            }
            continue;
        }
        if (item.permission_diagnostic[0])
            dluma_log_line("ERROR", item.permission_diagnostic);
        if (item.scope != DLUMA_LUA_SCOPE_PROJECT &&
            item.scope != DLUMA_LUA_SCOPE_MAP) continue;
        existing_local = dluma_lua_find_id(g_dluma.lua_discovery.local_files, count, item.id);
        existing_global = dluma_lua_find_id(g_dluma.lua_discovery.global_files, g_dluma.lua_discovery.global_count, item.id);
        if (existing_local >= 0 || existing_global >= 0) {
            item.duplicate_id = 1;
            ++local_duplicate_count;
            dluma_log_pair("ERROR", "duplicate_local_lua_id", item.id);
        }
        g_dluma.lua_discovery.local_files[count++] = item;
        if (item.scope == DLUMA_LUA_SCOPE_PROJECT) {
            ++project_count;
            dluma_lua_log_metadata("project_lua", &item);
        } else {
            ++map_count;
            dluma_lua_log_metadata("map_lua", &item);
        }
    }
    g_dluma.lua_discovery.local_count = count;
    if (count != 0)
        dluma_custom_map_asset_isolation_note_local_lua_active();
    g_dluma.lua_discovery.project_count = project_count;
    g_dluma.lua_discovery.map_count = map_count;
    g_dluma.lua_discovery.local_invalid_count = local_invalid_count;
    g_dluma.lua_discovery.local_duplicate_count = local_duplicate_count;
    g_dluma.lua_discovery.invalid_count =
        g_dluma.lua_discovery.global_invalid_count + local_invalid_count;
    g_dluma.lua_discovery.duplicate_count =
        g_dluma.lua_discovery.global_duplicate_count + local_duplicate_count;
    dluma_log_u64("INFO", "local_lua_files_found", count);
    dluma_log_u64("INFO", "project_lua_files_found", project_count);
    dluma_log_u64("INFO", "map_lua_files_found", map_count);
    dluma_log_u64("INFO", "local_lua_invalid_metadata", local_invalid_count);
    dluma_log_u64("INFO", "local_lua_duplicate_ids", local_duplicate_count);
    dluma_log_line("INFO", "Local Lua metadata published for game-thread runtime reconciliation.");
    dluma_lua_write_report();
    return count;
}

void dluma_lua_clear_locals(void)
{
    memset(g_dluma.lua_discovery.local_files, 0, sizeof(g_dluma.lua_discovery.local_files));
    g_dluma.lua_discovery.local_count = 0;
    g_dluma.lua_discovery.project_count = 0;
    g_dluma.lua_discovery.map_count = 0;
    g_dluma.lua_discovery.local_invalid_count = 0;
    g_dluma.lua_discovery.local_duplicate_count = 0;
    g_dluma.lua_discovery.invalid_count = g_dluma.lua_discovery.global_invalid_count;
    g_dluma.lua_discovery.duplicate_count = g_dluma.lua_discovery.global_duplicate_count;
    dluma_lua_write_report();
}
