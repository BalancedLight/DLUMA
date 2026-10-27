#include "dlw_asset_mods.h"
#include "dlw_path_security.h"
#include "core/dlw_json_reader.h"

#define DLUMA_ASSET_MANIFEST_MAX_BYTES (64U * 1024U)
#define DLUMA_ASSET_MAX_PROVIDERS (DLUMA_ASSET_MOD_MAX_ITEMS * 2U)

typedef struct DlumaAssetProvider {
    DlumaAssetModSnapshot item;
} DlumaAssetProvider;

typedef struct DlumaAssetSetting {
    char id[DLUMA_ASSET_MOD_ID_CAPACITY];
    char source[DLUMA_ASSET_MOD_SOURCE_CAPACITY];
    unsigned char enabled;
} DlumaAssetSetting;

static DlumaAssetProvider g_dluma_asset_providers[DLUMA_ASSET_MAX_PROVIDERS];
static DlumaAssetSetting g_dluma_asset_settings[DLUMA_ASSET_MOD_MAX_ITEMS];
static DlumaAssetModSourceSnapshot g_dluma_asset_effective[DLUMA_ASSET_MOD_MAX_ITEMS];
static unsigned int g_dluma_asset_provider_count;
static unsigned int g_dluma_asset_setting_count;
static unsigned int g_dluma_asset_effective_count;
static ULONGLONG g_dluma_asset_desired_revision;
static ULONGLONG g_dluma_asset_effective_revision;
static volatile LONG g_dluma_asset_lock;
static unsigned char g_dluma_asset_initialized;

static void dluma_asset_lock(void)
{
    while (dluma_atomic_compare_exchange(&g_dluma_asset_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_asset_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_asset_lock, 0);
}

static int dluma_asset_result(
    int result, char *diagnostic, unsigned int capacity, const char *message)
{
    if (diagnostic && capacity)
        dluma_str_copy(diagnostic, capacity, message ? message : "");
    return result;
}

static int dluma_asset_join(
    WCHAR *output, unsigned int capacity,
    const WCHAR *root, const WCHAR *relative)
{
    if (!output || !capacity || !root || !root[0] || !relative ||
        !relative[0] || wcslen(root) + wcslen(relative) + 2U > capacity)
        return 0;
    dluma_path_join(output, capacity, root, relative);
    return output[0] != 0;
}

static int dluma_asset_under(const WCHAR *path, const WCHAR *root)
{
    SIZE_T index;
    SIZE_T length = root ? wcslen(root) : 0;
    if (!path || !length) return 0;
    for (index = 0; index < length; ++index)
        if (!path[index] || dluma_wascii_lower(path[index]) !=
                dluma_wascii_lower(root[index])) return 0;
    return path[length] == 0 || path[length] == L'\\' || path[length] == L'/';
}

static int dluma_asset_id_valid(char *id)
{
    unsigned int index;
    if (!id || !id[0]) return 0;
    for (index = 0; id[index]; ++index) {
        unsigned char value = (unsigned char)id[index];
        if (value >= 'A' && value <= 'Z') value += 'a' - 'A';
        if (!((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '-' || value == '_')) return 0;
        id[index] = (char)value;
    }
    return index < DLUMA_ASSET_MOD_ID_CAPACITY;
}

static int dluma_asset_device_segment(const char *segment, unsigned int length)
{
    static const char *const devices[] = {"con", "prn", "aux", "nul"};
    unsigned int stem = 0;
    unsigned int index;
    char folded[5];
    while (stem < length && segment[stem] != '.') ++stem;
    if (stem != 3U && stem != 4U) return 0;
    for (index = 0; index < stem; ++index) {
        char value = segment[index];
        folded[index] = value >= 'A' && value <= 'Z' ?
            (char)(value + ('a' - 'A')) : value;
    }
    folded[stem] = 0;
    for (index = 0; index < DLUMA_ARRAY_COUNT(devices); ++index)
        if (dluma_stricmp(folded, devices[index]) == 0) return 1;
    return stem == 4U && folded[3] >= '1' && folded[3] <= '9' &&
        ((folded[0] == 'c' && folded[1] == 'o' && folded[2] == 'm') ||
         (folded[0] == 'l' && folded[1] == 'p' && folded[2] == 't'));
}

static int dluma_asset_normalize_path(
    const char *value, char *output, unsigned int capacity)
{
    unsigned int source = 0;
    unsigned int target = 0;
    unsigned int segment = 0;
    if (!value || !value[0] || !output || capacity < 2U ||
        value[0] == '/' || value[0] == '\\') return 0;
    for (;;) {
        unsigned char character = (unsigned char)value[source++];
        if (!character || character == '/' || character == '\\') {
            unsigned int length = target - segment;
            if (!length ||
                (length == 1U && output[segment] == '.') ||
                (length == 2U && output[segment] == '.' && output[segment + 1U] == '.') ||
                output[target - 1U] == '.' || output[target - 1U] == ' ' ||
                dluma_asset_device_segment(output + segment, length)) return 0;
            if (!character) break;
            if (target + 1U >= capacity) return 0;
            output[target++] = '/';
            segment = target;
        } else {
            if (character < 0x20U || character == '<' || character == '>' ||
                character == ':' || character == '"' || character == '|' ||
                character == '?' || character == '*' ||
                target + 1U >= capacity) return 0;
            output[target++] = (char)character;
        }
    }
    output[target] = 0;
    return 1;
}

static HANDLE dluma_asset_open_directory(
    const WCHAR *path, WCHAR *canonical, unsigned int capacity)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION identity;
    file = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE) return file;
    if (!GetFileInformationByHandle(file, &identity) ||
        !(identity.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (identity.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_get_final_path_checked(file, canonical, capacity)) {
        CloseHandle(file);
        return INVALID_HANDLE_VALUE;
    }
    return file;
}

static int dluma_asset_manifest_parse(
    const void *bytes, unsigned int size, DlumaAssetModSnapshot *item)
{
    DlumaJsonReader reader;
    unsigned int fields = 0;
    int first = 1;
    if (!bytes || !size || size > DLUMA_ASSET_MANIFEST_MAX_BYTES || !item)
        return 0;
    dluma_json_reader_initialize(&reader, bytes, size);
    if (!dluma_json_object_begin(&reader)) return 0;
    for (;;) {
        char key[64];
        char *destination = 0;
        unsigned int capacity = 0;
        unsigned int field = 0;
        int next = dluma_json_object_next(&reader, &first);
        if (!next) break;
        if (next < 0 || !dluma_json_string(&reader, key, sizeof(key)) ||
            !dluma_json_take(&reader, ':')) return 0;
        if (dluma_stricmp(key, "schema_version") == 0) {
            int version;
            if ((fields & 1U) || !dluma_json_i32(&reader, &version) ||
                version != 1) return 0;
            fields |= 1U;
            continue;
        }
        if (dluma_stricmp(key, "id") == 0) {
            destination = item->id; capacity = sizeof(item->id); field = 2U;
        } else if (dluma_stricmp(key, "name") == 0) {
            destination = item->name; capacity = sizeof(item->name); field = 4U;
        } else if (dluma_stricmp(key, "author") == 0) {
            destination = item->author; capacity = sizeof(item->author); field = 8U;
        } else if (dluma_stricmp(key, "version") == 0) {
            destination = item->version; capacity = sizeof(item->version); field = 16U;
        } else if (dluma_stricmp(key, "description") == 0) {
            destination = item->description;
            capacity = sizeof(item->description); field = 32U;
        } else {
            if (!dluma_json_skip_value(&reader)) return 0;
            continue;
        }
        if ((fields & field) ||
            !dluma_json_string(&reader, destination, capacity)) return 0;
        fields |= field;
    }
    return dluma_json_object_end(&reader) && dluma_json_finished(&reader) &&
        (fields & 31U) == 31U && dluma_asset_id_valid(item->id) &&
        item->name[0] && item->author[0] && item->version[0];
}

static int dluma_asset_setting_index(const char *id)
{
    unsigned int index;
    for (index = 0; index < g_dluma_asset_setting_count; ++index)
        if (dluma_stricmp(g_dluma_asset_settings[index].id, id) == 0)
            return (int)index;
    return -1;
}

static int dluma_asset_state_path(WCHAR *path, unsigned int capacity)
{
    WCHAR runtime[DLUMA_MAX_WPATH];
    return dluma_asset_join(runtime, DLUMA_ARRAY_COUNT(runtime),
               g_dluma.module_dir, L"dluma_runtime") &&
        dluma_mkdirs(runtime) &&
        dluma_asset_join(path, capacity, runtime, L"asset_mods.ini");
}

static void dluma_asset_state_key(
    const WCHAR *name, unsigned int index, WCHAR *key, unsigned int capacity)
{
    char number[32];
    WCHAR wide_number[32];
    dluma_u64_to_text(index, number, DLUMA_ARRAY_COUNT(number));
    dluma_utf8_to_wide(number, wide_number, DLUMA_ARRAY_COUNT(wide_number));
    dluma_wstr_copy(key, capacity, name);
    dluma_wstr_append(key, capacity, L".");
    dluma_wstr_append(key, capacity, wide_number);
}

static void dluma_asset_state_load(void)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR key[48];
    WCHAR value[DLUMA_ASSET_MOD_SOURCE_CAPACITY];
    char text[DLUMA_ASSET_MOD_SOURCE_CAPACITY];
    unsigned int count;
    unsigned int index;
    g_dluma_asset_setting_count = 0;
    if (!dluma_asset_state_path(path, DLUMA_ARRAY_COUNT(path))) return;
    GetPrivateProfileStringW(L"asset_mods", L"count", L"0",
        value, DLUMA_ARRAY_COUNT(value), path);
    if (!dluma_wide_to_utf8(value, text, DLUMA_ARRAY_COUNT(text))) return;
    count = dluma_parse_u32(text, 0);
    if (count > DLUMA_ASSET_MOD_MAX_ITEMS) count = DLUMA_ASSET_MOD_MAX_ITEMS;
    for (index = 0; index < count; ++index) {
        DlumaAssetSetting setting;
        memset(&setting, 0, sizeof(setting));
        dluma_asset_state_key(L"id", index, key, DLUMA_ARRAY_COUNT(key));
        GetPrivateProfileStringW(L"asset_mods", key, L"",
            value, DLUMA_ARRAY_COUNT(value), path);
        if (!dluma_wide_to_utf8(value, setting.id, sizeof(setting.id)) ||
            !dluma_asset_id_valid(setting.id) ||
            dluma_asset_setting_index(setting.id) >= 0) continue;
        dluma_asset_state_key(L"enabled", index, key, DLUMA_ARRAY_COUNT(key));
        GetPrivateProfileStringW(L"asset_mods", key, L"0",
            value, DLUMA_ARRAY_COUNT(value), path);
        setting.enabled = value[0] == L'1' && !value[1];
        dluma_asset_state_key(L"source", index, key, DLUMA_ARRAY_COUNT(key));
        GetPrivateProfileStringW(L"asset_mods", key, L"",
            value, DLUMA_ARRAY_COUNT(value), path);
        if (!dluma_wide_to_utf8(value, setting.source, sizeof(setting.source)))
            setting.source[0] = 0;
        g_dluma_asset_settings[g_dluma_asset_setting_count++] = setting;
    }
}

static int dluma_asset_state_save(void)
{
    WCHAR path[DLUMA_MAX_WPATH];
    WCHAR temporary[DLUMA_MAX_WPATH];
    WCHAR value[DLUMA_ASSET_MOD_SOURCE_CAPACITY];
    WCHAR key[48];
    char number[32];
    WCHAR wide_number[32];
    HANDLE file;
    unsigned int index;
    int valid = 0;
    if (!dluma_asset_state_path(path, DLUMA_ARRAY_COUNT(path)) ||
        wcslen(path) + 48U >= DLUMA_ARRAY_COUNT(temporary)) return 0;
    dluma_wstr_copy(temporary, DLUMA_ARRAY_COUNT(temporary), path);
    dluma_wstr_append(temporary, DLUMA_ARRAY_COUNT(temporary), L".tmp.");
    dluma_u64_to_text(GetCurrentProcessId(), number, DLUMA_ARRAY_COUNT(number));
    dluma_utf8_to_wide(number, wide_number, DLUMA_ARRAY_COUNT(wide_number));
    dluma_wstr_append(temporary, DLUMA_ARRAY_COUNT(temporary), wide_number);
    /* The provider lock serializes writers in this process. */
    file = CreateFileW(temporary, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    {
        static const unsigned char marker[2] = {0xff, 0xfe};
        DWORD written = 0;
        valid = WriteFile(file, marker, sizeof(marker), &written, 0) &&
            written == sizeof(marker);
    }
    CloseHandle(file);
    if (!valid) goto done;
    dluma_u64_to_text(g_dluma_asset_setting_count, number, DLUMA_ARRAY_COUNT(number));
    if (!dluma_utf8_to_wide(number, value, DLUMA_ARRAY_COUNT(value)) ||
        !WritePrivateProfileStringW(L"asset_mods", L"count", value, temporary))
        goto failed;
    for (index = 0; index < g_dluma_asset_setting_count; ++index) {
        const DlumaAssetSetting *setting = &g_dluma_asset_settings[index];
        dluma_asset_state_key(L"id", index, key, DLUMA_ARRAY_COUNT(key));
        if (!dluma_utf8_to_wide(setting->id, value, DLUMA_ARRAY_COUNT(value)) ||
            !WritePrivateProfileStringW(L"asset_mods", key, value, temporary))
            goto failed;
        dluma_asset_state_key(L"enabled", index, key, DLUMA_ARRAY_COUNT(key));
        if (!WritePrivateProfileStringW(L"asset_mods", key,
                setting->enabled ? L"1" : L"0", temporary)) goto failed;
        dluma_asset_state_key(L"source", index, key, DLUMA_ARRAY_COUNT(key));
        if (!dluma_utf8_to_wide(setting->source, value, DLUMA_ARRAY_COUNT(value)) ||
            !WritePrivateProfileStringW(L"asset_mods", key, value, temporary))
            goto failed;
    }
    if (!WritePrivateProfileStringW(0, 0, 0, temporary)) goto failed;
    valid = MoveFileExW(temporary, path,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    goto done;
failed:
    valid = 0;
done:
    if (!valid) DeleteFileW(temporary);
    return valid;
}

static void dluma_asset_add_candidate(const WCHAR *root)
{
    WCHAR manifest[DLUMA_MAX_WPATH];
    WCHAR canonical[DLUMA_MAX_WPATH];
    WCHAR final_manifest[DLUMA_MAX_WPATH];
    HANDLE directory;
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION identity;
    DWORD read = 0;
    unsigned char *bytes;
    DlumaAssetProvider *provider;
    DlumaProjectSourceDescription source;
    unsigned int index;
    if (!dluma_asset_join(manifest, DLUMA_ARRAY_COUNT(manifest), root, L"assetmod.json") ||
        GetFileAttributesW(manifest) == INVALID_FILE_ATTRIBUTES ||
        g_dluma_asset_provider_count >= DLUMA_ASSET_MAX_PROVIDERS) return;
    directory = dluma_asset_open_directory(root, canonical, DLUMA_ARRAY_COUNT(canonical));
    if (directory == INVALID_HANDLE_VALUE) return;
    for (index = 0; index < g_dluma_asset_provider_count; ++index)
        if (dluma_wstricmp(g_dluma_asset_providers[index].item.project_root, canonical) == 0) {
            CloseHandle(directory); return;
        }
    provider = &g_dluma_asset_providers[g_dluma_asset_provider_count];
    memset(provider, 0, sizeof(*provider));
    dluma_wstr_copy(provider->item.project_root,
        DLUMA_ARRAY_COUNT(provider->item.project_root), canonical);
    file = CreateFileW(manifest, GENERIC_READ, FILE_SHARE_READ, 0,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE) { CloseHandle(directory); return; }
    if (!GetFileInformationByHandle(file, &identity) ||
        (identity.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        identity.nFileSizeHigh || !identity.nFileSizeLow ||
        identity.nFileSizeLow > DLUMA_ASSET_MANIFEST_MAX_BYTES ||
        !dluma_get_final_path_checked(file, final_manifest, DLUMA_ARRAY_COUNT(final_manifest)) ||
        !dluma_asset_under(final_manifest, canonical)) {
        CloseHandle(file); CloseHandle(directory); return;
    }
    bytes = (unsigned char *)dluma_alloc(identity.nFileSizeLow);
    if (!bytes) { CloseHandle(file); CloseHandle(directory); return; }
    if (ReadFile(file, bytes, identity.nFileSizeLow, &read, 0) &&
        read == identity.nFileSizeLow &&
        dluma_asset_manifest_parse(bytes, read, &provider->item)) {
        WCHAR payload[DLUMA_MAX_WPATH];
        DWORD data_attributes = INVALID_FILE_ATTRIBUTES;
        DWORD assets_attributes = INVALID_FILE_ATTRIBUTES;
        if (dluma_asset_join(payload, DLUMA_ARRAY_COUNT(payload), canonical, L"data"))
            data_attributes = GetFileAttributesW(payload);
        if (dluma_asset_join(payload, DLUMA_ARRAY_COUNT(payload), canonical, L"assets_pc"))
            assets_attributes = GetFileAttributesW(payload);
        provider->item.valid =
            (data_attributes != INVALID_FILE_ATTRIBUTES &&
             (data_attributes & FILE_ATTRIBUTE_DIRECTORY) &&
             !(data_attributes & FILE_ATTRIBUTE_REPARSE_POINT)) ||
            (assets_attributes != INVALID_FILE_ATTRIBUTES &&
             (assets_attributes & FILE_ATTRIBUTE_DIRECTORY) &&
             !(assets_attributes & FILE_ATTRIBUTE_REPARSE_POINT));
        if (!provider->item.valid)
            dluma_str_copy(provider->item.diagnostic, sizeof(provider->item.diagnostic),
                "No data or assets_pc directory was found.");
        dluma_sha256_bytes_hex(bytes, read, provider->item.fingerprint);
        memset(&source, 0, sizeof(source));
        if (dluma_classify_project_source(root, &source)) {
            dluma_str_copy(provider->item.source_identity,
                sizeof(provider->item.source_identity), source.source_identity);
            provider->item.source_is_subscribed = source.is_subscribed_workshop;
        } else if (!dluma_wide_to_utf8(canonical, provider->item.source_identity,
                       sizeof(provider->item.source_identity))) {
            provider->item.valid = 0;
            dluma_str_copy(provider->item.diagnostic, sizeof(provider->item.diagnostic),
                "The asset source path is too long.");
        }
        if (provider->item.source_identity[0]) ++g_dluma_asset_provider_count;
    } else {
        dluma_log_pair("WARN", "asset_mod_manifest", "Invalid assetmod.json.");
    }
    dluma_free(bytes);
    CloseHandle(file);
    CloseHandle(directory);
}

static void dluma_asset_scan_root(const WCHAR *root)
{
    WCHAR pattern[DLUMA_MAX_WPATH];
    WCHAR child[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW found;
    HANDLE search;
    dluma_asset_add_candidate(root);
    if (!dluma_asset_join(pattern, DLUMA_ARRAY_COUNT(pattern), root, L"*")) return;
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            (found.cFileName[0] == L'.' && (!found.cFileName[1] ||
             (found.cFileName[1] == L'.' && !found.cFileName[2])))) continue;
        if (dluma_asset_join(child, DLUMA_ARRAY_COUNT(child), root, found.cFileName) &&
            dluma_project_candidate_root_is_contained(root, child))
            dluma_asset_add_candidate(child);
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

static int dluma_asset_provider_compare(const DlumaAssetProvider *left,
    const DlumaAssetProvider *right)
{
    int result = dluma_stricmp(left->item.id, right->item.id);
    if (result) return result;
    if (left->item.source_is_subscribed != right->item.source_is_subscribed)
        return left->item.source_is_subscribed ? 1 : -1;
    return dluma_stricmp(left->item.source_identity, right->item.source_identity);
}

static const DlumaAssetModSnapshot *dluma_asset_selected(
    const DlumaAssetSetting *setting, int *missing)
{
    const DlumaAssetModSnapshot *first = 0;
    const DlumaAssetModSnapshot *valid = 0;
    unsigned int index;
    if (missing) *missing = setting->source[0] ? 1 : 0;
    for (index = 0; index < g_dluma_asset_provider_count; ++index) {
        const DlumaAssetModSnapshot *item = &g_dluma_asset_providers[index].item;
        if (dluma_stricmp(item->id, setting->id) != 0) continue;
        if (!first) first = item;
        if (item->valid && !valid) valid = item;
        if (setting->source[0] &&
            dluma_stricmp(item->source_identity, setting->source) == 0) {
            if (missing) *missing = 0;
            return item;
        }
    }
    return valid ? valid : first;
}

static unsigned int dluma_asset_desired_sources(
    DlumaAssetModSourceSnapshot *output, unsigned int capacity)
{
    unsigned int index;
    unsigned int count = 0;
    unsigned int order = 0;
    for (index = 0; index < g_dluma_asset_setting_count; ++index) {
        const DlumaAssetSetting *setting = &g_dluma_asset_settings[index];
        const DlumaAssetModSnapshot *item = dluma_asset_selected(setting, 0);
        if (!item) continue;
        if (setting->enabled && item->valid) {
            if (output && count < capacity) {
                DlumaAssetModSourceSnapshot *source = &output[count];
                memset(source, 0, sizeof(*source));
                dluma_str_copy(source->id, sizeof(source->id), item->id);
                dluma_str_copy(source->source_identity,
                    sizeof(source->source_identity), item->source_identity);
                dluma_str_copy(source->fingerprint, sizeof(source->fingerprint),
                    item->fingerprint);
                dluma_wstr_copy(source->project_root,
                    DLUMA_ARRAY_COUNT(source->project_root), item->project_root);
                source->order = order;
            }
            ++count;
        }
        ++order;
    }
    return count;
}

void dluma_asset_mods_discover(void)
{
    unsigned int index;
    dluma_asset_lock();
    /* Discovery owns the launch snapshot. Runtime edits alter desired state. */
    if (g_dluma_asset_initialized) { dluma_asset_unlock(); return; }
    dluma_asset_state_load();
    g_dluma_asset_provider_count = 0;
    for (index = 0; index < g_dluma.workshop_root_count; ++index)
        dluma_asset_scan_root(g_dluma.workshop_roots[index]);
    for (index = 1; index < g_dluma_asset_provider_count; ++index) {
        DlumaAssetProvider value = g_dluma_asset_providers[index];
        unsigned int position = index;
        while (position && dluma_asset_provider_compare(
                &value, &g_dluma_asset_providers[position - 1U]) < 0) {
            g_dluma_asset_providers[position] = g_dluma_asset_providers[position - 1U];
            --position;
        }
        g_dluma_asset_providers[position] = value;
    }
    for (index = 0; index < g_dluma_asset_provider_count; ++index) {
        const char *id = g_dluma_asset_providers[index].item.id;
        if (dluma_asset_setting_index(id) < 0 &&
            g_dluma_asset_setting_count < DLUMA_ASSET_MOD_MAX_ITEMS) {
            DlumaAssetSetting *setting = &g_dluma_asset_settings[g_dluma_asset_setting_count++];
            memset(setting, 0, sizeof(*setting));
            dluma_str_copy(setting->id, sizeof(setting->id), id);
        }
    }
    g_dluma_asset_desired_revision = 1;
    g_dluma_asset_initialized = 1;
    dluma_asset_unlock();
}

unsigned int dluma_asset_mods_snapshot(
    DlumaAssetModSnapshot *output, unsigned int capacity,
    unsigned int *total_out, ULONGLONG *revision_out)
{
    unsigned int index;
    unsigned int count = 0;
    unsigned int copied = 0;
    dluma_asset_lock();
    for (index = 0; index < g_dluma_asset_setting_count; ++index) {
        const DlumaAssetSetting *setting = &g_dluma_asset_settings[index];
        const DlumaAssetModSnapshot *item;
        int missing = 0;
        unsigned int other;
        item = dluma_asset_selected(setting, &missing);
        if (!item) continue;
        if (output && copied < capacity) {
            DlumaAssetModSnapshot *row = &output[copied++];
            *row = *item;
            row->desired_order = count;
            row->effective_order = 0xffffffffU;
            row->desired_enabled = setting->enabled;
            row->desired_revision = g_dluma_asset_desired_revision;
            row->effective_revision = g_dluma_asset_effective_revision;
            row->source_selection_missing = (unsigned char)missing;
            for (other = 0; other < g_dluma_asset_provider_count; ++other) {
                const DlumaAssetModSnapshot *provider = &g_dluma_asset_providers[other].item;
                if (dluma_stricmp(provider->id, row->id) != 0) continue;
                if (provider->source_is_subscribed) {
                    if (!row->has_steam || provider->valid) {
                        dluma_str_copy(row->steam_source_identity,
                            sizeof(row->steam_source_identity), provider->source_identity);
                    }
                    row->has_steam = 1;
                } else {
                    if (!row->has_local || provider->valid) {
                        dluma_str_copy(row->local_source_identity,
                            sizeof(row->local_source_identity), provider->source_identity);
                    }
                    row->has_local = 1;
                }
            }
            row->pending = row->desired_enabled != 0;
            for (other = 0; other < g_dluma_asset_effective_count; ++other) {
                const DlumaAssetModSourceSnapshot *effective = &g_dluma_asset_effective[other];
                if (dluma_stricmp(effective->id, row->id) != 0) continue;
                row->effective_enabled = 1;
                row->effective_order = effective->order;
                row->pending = !row->desired_enabled ||
                    row->desired_order != effective->order ||
                    dluma_stricmp(row->source_identity, effective->source_identity) != 0 ||
                    dluma_stricmp(row->fingerprint, effective->fingerprint) != 0;
                break;
            }
        }
        ++count;
    }
    if (total_out) *total_out = count;
    if (revision_out) *revision_out = g_dluma_asset_desired_revision;
    dluma_asset_unlock();
    return output ? copied : count;
}

unsigned int dluma_asset_mods_snapshot_sources(
    DlumaAssetModSourceSnapshot *output, unsigned int capacity,
    ULONGLONG *revision_out)
{
    unsigned int count;
    dluma_asset_lock();
    count = dluma_asset_desired_sources(output, capacity);
    if (revision_out) *revision_out = g_dluma_asset_desired_revision;
    dluma_asset_unlock();
    return output && count > capacity ? capacity : count;
}

static void dluma_asset_revision_advance(void)
{
    ++g_dluma_asset_desired_revision;
    if (!g_dluma_asset_desired_revision) ++g_dluma_asset_desired_revision;
}

int dluma_asset_mods_set_enabled(
    const char *id, int enabled, char *diagnostic, unsigned int capacity)
{
    int index;
    unsigned char previous;
    const DlumaAssetModSnapshot *item;
    if (!id || !id[0])
        return dluma_asset_result(0, diagnostic, capacity, "Select an asset mod.");
    dluma_asset_lock();
    index = dluma_asset_setting_index(id);
    item = index >= 0 ? dluma_asset_selected(&g_dluma_asset_settings[index], 0) : 0;
    if (!item || (enabled && !item->valid)) {
        dluma_asset_unlock();
        return dluma_asset_result(0, diagnostic, capacity, "This asset mod is unavailable.");
    }
    previous = g_dluma_asset_settings[index].enabled;
    g_dluma_asset_settings[index].enabled = enabled ? 1 : 0;
    if (previous != g_dluma_asset_settings[index].enabled) {
        if (!dluma_asset_state_save()) {
            g_dluma_asset_settings[index].enabled = previous;
            dluma_asset_unlock();
            return dluma_asset_result(0, diagnostic, capacity, "Could not save asset mod settings.");
        }
        dluma_asset_revision_advance();
    }
    dluma_asset_unlock();
    return dluma_asset_result(1, diagnostic, capacity, "Applies when the next level loads.");
}

int dluma_asset_mods_move(
    const char *id, int direction, char *diagnostic, unsigned int capacity)
{
    int index;
    int other;
    DlumaAssetSetting value;
    if (!id || (direction != -1 && direction != 1))
        return dluma_asset_result(0, diagnostic, capacity, "Select an asset mod to move.");
    dluma_asset_lock();
    index = dluma_asset_setting_index(id);
    if (index < 0 || !dluma_asset_selected(&g_dluma_asset_settings[index], 0)) {
        dluma_asset_unlock();
        return dluma_asset_result(0, diagnostic, capacity, "This asset mod is unavailable.");
    }
    other = index + direction;
    while (other >= 0 && other < (int)g_dluma_asset_setting_count &&
           !dluma_asset_selected(&g_dluma_asset_settings[other], 0)) other += direction;
    if (other < 0 || other >= (int)g_dluma_asset_setting_count) {
        dluma_asset_unlock();
        return dluma_asset_result(0, diagnostic, capacity,
            direction < 0 ? "Already at the top." : "Already at the bottom.");
    }
    value = g_dluma_asset_settings[index];
    g_dluma_asset_settings[index] = g_dluma_asset_settings[other];
    g_dluma_asset_settings[other] = value;
    if (!dluma_asset_state_save()) {
        g_dluma_asset_settings[other] = g_dluma_asset_settings[index];
        g_dluma_asset_settings[index] = value;
        dluma_asset_unlock();
        return dluma_asset_result(0, diagnostic, capacity, "Could not save asset mod order.");
    }
    dluma_asset_revision_advance();
    dluma_asset_unlock();
    return dluma_asset_result(1, diagnostic, capacity, "Applies when the next level loads.");
}

int dluma_asset_mods_select_source(
    const char *id, const char *source_identity,
    char *diagnostic, unsigned int capacity)
{
    int index;
    unsigned int provider;
    int found = 0;
    char previous[DLUMA_ASSET_MOD_SOURCE_CAPACITY];
    if (!id || !source_identity || !source_identity[0] ||
        strlen(source_identity) >= sizeof(previous))
        return dluma_asset_result(0, diagnostic, capacity, "Select an asset mod source.");
    dluma_asset_lock();
    index = dluma_asset_setting_index(id);
    for (provider = 0; index >= 0 && provider < g_dluma_asset_provider_count; ++provider) {
        const DlumaAssetModSnapshot *item = &g_dluma_asset_providers[provider].item;
        if (item->valid && dluma_stricmp(item->id, id) == 0 &&
            dluma_stricmp(item->source_identity, source_identity) == 0) {
            found = 1; break;
        }
    }
    if (!found) {
        dluma_asset_unlock();
        return dluma_asset_result(0, diagnostic, capacity, "This asset mod source is unavailable.");
    }
    dluma_str_copy(previous, sizeof(previous), g_dluma_asset_settings[index].source);
    if (dluma_stricmp(previous, source_identity) != 0) {
        dluma_str_copy(g_dluma_asset_settings[index].source,
            sizeof(g_dluma_asset_settings[index].source), source_identity);
        if (!dluma_asset_state_save()) {
            dluma_str_copy(g_dluma_asset_settings[index].source,
                sizeof(g_dluma_asset_settings[index].source), previous);
            dluma_asset_unlock();
            return dluma_asset_result(0, diagnostic, capacity, "Could not save the asset mod source.");
        }
        dluma_asset_revision_advance();
    }
    dluma_asset_unlock();
    return dluma_asset_result(1, diagnostic, capacity, "Applies when the next level loads.");
}

int dluma_asset_mods_commit_effective(ULONGLONG expected_revision)
{
    int valid = 0;
    dluma_asset_lock();
    if (expected_revision && expected_revision == g_dluma_asset_desired_revision) {
        memset(g_dluma_asset_effective, 0, sizeof(g_dluma_asset_effective));
        g_dluma_asset_effective_count = dluma_asset_desired_sources(
            g_dluma_asset_effective, DLUMA_ARRAY_COUNT(g_dluma_asset_effective));
        g_dluma_asset_effective_revision = expected_revision;
        valid = 1;
    }
    dluma_asset_unlock();
    return valid;
}

ULONGLONG dluma_asset_mods_desired_revision(void)
{
    ULONGLONG revision;
    dluma_asset_lock(); revision = g_dluma_asset_desired_revision; dluma_asset_unlock();
    return revision;
}

ULONGLONG dluma_asset_mods_effective_revision(void)
{
    ULONGLONG revision;
    dluma_asset_lock(); revision = g_dluma_asset_effective_revision; dluma_asset_unlock();
    return revision;
}

static int dluma_asset_probe_file(
    const WCHAR *root, const WCHAR *relative,
    WCHAR *physical, unsigned int capacity)
{
    WCHAR path[DLUMA_MAX_WPATH];
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION identity;
    DWORD attributes;
    int valid;
    if (!dluma_asset_join(path, DLUMA_ARRAY_COUNT(path), root, relative)) return -1;
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 0 : -1;
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return -1;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, 0);
    if (file == INVALID_HANDLE_VALUE) return -1;
    valid = GetFileInformationByHandle(file, &identity) &&
        !(identity.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        dluma_get_final_path_checked(file, physical, capacity) &&
        dluma_asset_under(physical, root);
    CloseHandle(file);
    if (!valid && physical && capacity) physical[0] = 0;
    return valid ? 1 : -1;
}

int dluma_asset_mods_resolve_file_root(
    const char *virtual_path, int use_desired,
    WCHAR *physical_path, unsigned int physical_capacity,
    WCHAR *project_root, unsigned int root_capacity,
    char *owner_id, unsigned int owner_capacity)
{
    char normalized[DLUMA_MAX_WPATH * 2U];
    WCHAR relative[DLUMA_MAX_WPATH];
    unsigned int index;
    int result = 0;
    if (physical_path && physical_capacity) physical_path[0] = 0;
    if (project_root && root_capacity) project_root[0] = 0;
    if (owner_id && owner_capacity) owner_id[0] = 0;
    if (!physical_path || !physical_capacity ||
        !dluma_asset_normalize_path(virtual_path,
            normalized, sizeof(normalized))) return -1;
    for (index = 0; normalized[index]; ++index) {
        if (normalized[index] == '\\') normalized[index] = '/';
        if (normalized[index] >= 'A' && normalized[index] <= 'Z')
            normalized[index] += 'a' - 'A';
    }
    if (!((strlen(normalized) > 5U && memcmp(normalized, "data/", 5U) == 0) ||
          (strlen(normalized) > 10U && memcmp(normalized, "assets_pc/", 10U) == 0)))
        return 0;
    if (!dluma_utf8_to_wide(normalized, relative, DLUMA_ARRAY_COUNT(relative))) return -1;
    for (index = 0; relative[index]; ++index)
        if (relative[index] == L'/') relative[index] = L'\\';
    dluma_asset_lock();
    for (index = 0; index < (use_desired ? g_dluma_asset_setting_count :
            g_dluma_asset_effective_count); ++index) {
        const WCHAR *root;
        const char *id;
        if (use_desired) {
            const DlumaAssetSetting *setting = &g_dluma_asset_settings[index];
            const DlumaAssetModSnapshot *item;
            if (!setting->enabled) continue;
            item = dluma_asset_selected(setting, 0);
            if (!item || !item->valid) continue;
            root = item->project_root; id = item->id;
        } else {
            root = g_dluma_asset_effective[index].project_root;
            id = g_dluma_asset_effective[index].id;
        }
        result = dluma_asset_probe_file(root, relative, physical_path, physical_capacity);
        if (result) {
            if (result > 0) {
                if ((project_root && wcslen(root) >= root_capacity) ||
                    (owner_id && strlen(id) >= owner_capacity)) {
                    physical_path[0] = 0; result = -1;
                } else {
                    if (project_root) dluma_wstr_copy(project_root, root_capacity, root);
                    if (owner_id) dluma_str_copy(owner_id, owner_capacity, id);
                }
            }
            break;
        }
    }
    dluma_asset_unlock();
    return result;
}

int dluma_asset_mods_resolve_file(
    const char *virtual_path, int use_desired,
    WCHAR *physical_path, unsigned int physical_capacity,
    char *owner_id, unsigned int owner_capacity)
{
    return dluma_asset_mods_resolve_file_root(virtual_path, use_desired,
        physical_path, physical_capacity, 0, 0, owner_id, owner_capacity);
}
