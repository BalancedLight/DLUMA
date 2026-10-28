#include "dlw_new_mod_ticker.h"

#include "core/dlw_mod_owner.h"
#include "dlw_common.h"
#include "dlw_asset_mods.h"
#include "dlw_native_mods.h"
#include "hosts/dlw_managed_host.h"

#define DLUMA_NEW_MOD_TICKER_MAX_CANDIDATES \
    (DLUMA_MAX_LUA_FILES + DLUMA_MAX_NATIVE_MODS + DLUMA_MAX_MANAGED_MODS + \
     DLUMA_ASSET_MOD_MAX_ITEMS)
#define DLUMA_NEW_MOD_TICKER_SECTION L"new_mod_ticker_announced"

typedef struct DlumaNewModTickerCandidate {
    char marker_key[DLUMA_SHA256_HEX_CAPACITY];
    char fingerprint[DLUMA_SHA256_HEX_CAPACITY];
} DlumaNewModTickerCandidate;

typedef struct DlumaNewModTickerState {
    DlumaNewModTickerCandidate candidates[
        DLUMA_NEW_MOD_TICKER_MAX_CANDIDATES];
    unsigned int count;
    unsigned int lua_count;
    unsigned int native_count;
    unsigned int managed_count;
    unsigned int asset_count;
    unsigned char captured;
    unsigned char presented;
    unsigned char reserved[2];
} DlumaNewModTickerState;

static DlumaNewModTickerState g_dluma_new_mod_ticker;

static int dluma_new_mod_ticker_marker_key(
    const char* kind,
    const char* registry_key,
    char out[DLUMA_SHA256_HEX_CAPACITY])
{
    const char* fields[2];
    if (!kind || !kind[0] || !registry_key || !registry_key[0] || !out)
        return 0;
    fields[0] = kind;
    fields[1] = registry_key;
    dluma_sha256_fields_hex(
        "DLW_NEW_MOD_TICKER_KEY_V1", fields, 0, 2U, out);
    return out[0] != 0;
}

static int dluma_new_mod_ticker_read_marker(
    const WCHAR* path,
    const char* marker_key,
    char out[DLUMA_SHA256_HEX_CAPACITY])
{
    WCHAR key[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR value[DLUMA_SHA256_HEX_CAPACITY];
    DWORD length;
    if (!out)
        return 0;
    out[0] = 0;
    if (!path || !path[0] || !marker_key || !marker_key[0] ||
        !dluma_utf8_to_wide(marker_key, key, DLUMA_ARRAY_COUNT(key)))
        return 0;
    value[0] = 0;
    length = GetPrivateProfileStringW(
        DLUMA_NEW_MOD_TICKER_SECTION, key, L"", value,
        DLUMA_ARRAY_COUNT(value), path);
    if (!length || !value[0] ||
        !dluma_wide_to_utf8(value, out, DLUMA_SHA256_HEX_CAPACITY))
    {
        out[0] = 0;
        return 0;
    }
    return 1;
}

static int dluma_new_mod_ticker_add(
    const WCHAR* path,
    const char* kind,
    const char* registry_key,
    const char* fingerprint)
{
    DlumaNewModTickerCandidate* candidate;
    char marker_key[DLUMA_SHA256_HEX_CAPACITY];
    char announced[DLUMA_SHA256_HEX_CAPACITY];
    if (!fingerprint || !fingerprint[0] ||
        g_dluma_new_mod_ticker.count >=
            DLUMA_NEW_MOD_TICKER_MAX_CANDIDATES ||
        !dluma_new_mod_ticker_marker_key(kind, registry_key, marker_key))
        return 0;
    announced[0] = 0;
    (void)dluma_new_mod_ticker_read_marker(
        path, marker_key, announced);
    if (announced[0] &&
        dluma_stricmp(announced, fingerprint) == 0)
        return 0;
    candidate = &g_dluma_new_mod_ticker.candidates[
        g_dluma_new_mod_ticker.count++];
    memset(candidate, 0, sizeof(*candidate));
    dluma_str_copy(
        candidate->marker_key,
        DLUMA_ARRAY_COUNT(candidate->marker_key), marker_key);
    dluma_str_copy(
        candidate->fingerprint,
        DLUMA_ARRAY_COUNT(candidate->fingerprint), fingerprint);
    return 1;
}

static void dluma_new_mod_ticker_capture_lua(const WCHAR* path)
{
    DlumaLuaMetadata* entries;
    unsigned int total;
    unsigned int count;
    unsigned int index;
    entries = (DlumaLuaMetadata*)dluma_alloc(
        (SIZE_T)DLUMA_MAX_LUA_FILES * sizeof(*entries));
    if (!entries)
    {
        dluma_log_line(
            "ERROR", "new_mod_ticker_lua_snapshot_allocation_failed");
        return;
    }
    memset(entries, 0, (SIZE_T)DLUMA_MAX_LUA_FILES * sizeof(*entries));
    total = dluma_lua_snapshot_globals_with_revision(
        entries, DLUMA_MAX_LUA_FILES, 0, 0);
    count = total > DLUMA_MAX_LUA_FILES ? DLUMA_MAX_LUA_FILES : total;
    for (index = 0; index < count; ++index)
    {
        const DlumaLuaMetadata* item = &entries[index];
        if (!item->valid || item->duplicate_id ||
            item->scope != DLUMA_LUA_SCOPE_GLOBAL ||
            !item->is_new ||
            (!g_dluma.config.auto_enable_mods &&
             (!item->pending_review || item->approval_valid)) ||
            !item->registry_key[0] ||
            !item->metadata_fingerprint[0])
            continue;
        if (dluma_new_mod_ticker_add(
                path, "lua", item->registry_key,
                item->metadata_fingerprint))
            ++g_dluma_new_mod_ticker.lua_count;
    }
    dluma_free(entries);
}

static void dluma_new_mod_ticker_capture_native(const WCHAR* path)
{
    DlumaNativeModSnapshot* entries;
    unsigned int total;
    unsigned int count;
    unsigned int index;
    entries = (DlumaNativeModSnapshot*)dluma_alloc(
        (SIZE_T)DLUMA_MAX_NATIVE_MODS * sizeof(*entries));
    if (!entries)
    {
        dluma_log_line(
            "ERROR", "new_mod_ticker_native_snapshot_allocation_failed");
        return;
    }
    memset(entries, 0, (SIZE_T)DLUMA_MAX_NATIVE_MODS * sizeof(*entries));
    total = dluma_native_mods_snapshot(
        entries, DLUMA_MAX_NATIVE_MODS, 0);
    count = total > DLUMA_MAX_NATIVE_MODS ? DLUMA_MAX_NATIVE_MODS : total;
    for (index = 0; index < count; ++index)
    {
        const DlumaNativeModSnapshot* item = &entries[index];
        if (item->scope != DLUMA_SCOPE_GLOBAL ||
            item->duplicate_id ||
            item->abi_kind != DLUMA_NATIVE_ABI_V2 ||
            (item->approval_valid && !item->auto_approved) ||
            !item->registry_key[0] ||
            !item->approval_fingerprint[0] ||
            (item->companion_pack.declared &&
             !item->companion_pack.valid) ||
            (!item->auto_approved &&
             item->state != DLUMA_NATIVE_STATE_DISCOVERED_DISABLED &&
             item->state !=
                 DLUMA_NATIVE_STATE_CHANGED_REVIEW_REQUIRED))
            continue;
        if (dluma_new_mod_ticker_add(
                path, "native", item->registry_key,
                item->approval_fingerprint))
            ++g_dluma_new_mod_ticker.native_count;
    }
    dluma_free(entries);
}

static void dluma_new_mod_ticker_capture_managed(const WCHAR* path)
{
    DlumaManagedModSnapshot* entries;
    unsigned int total;
    unsigned int count;
    unsigned int index;
    entries = (DlumaManagedModSnapshot*)dluma_alloc(
        (SIZE_T)DLUMA_MAX_MANAGED_MODS * sizeof(*entries));
    if (!entries)
    {
        dluma_log_line(
            "ERROR", "new_mod_ticker_managed_snapshot_allocation_failed");
        return;
    }
    memset(entries, 0, (SIZE_T)DLUMA_MAX_MANAGED_MODS * sizeof(*entries));
    total = dluma_managed_mods_snapshot(
        entries, DLUMA_MAX_MANAGED_MODS, 0);
    count = total > DLUMA_MAX_MANAGED_MODS ? DLUMA_MAX_MANAGED_MODS : total;
    for (index = 0; index < count; ++index)
    {
        const DlumaManagedModSnapshot* item = &entries[index];
        if (item->scope != DLUMA_SCOPE_GLOBAL ||
            item->duplicate_id ||
            (item->approval_valid && !item->auto_approved) ||
            !item->registry_key[0] ||
            !item->approval_fingerprint[0] ||
            (!item->auto_approved &&
             item->state != DLUMA_MANAGED_STATE_DISCOVERED_DISABLED &&
             item->state !=
                 DLUMA_MANAGED_STATE_CHANGED_REVIEW_REQUIRED))
            continue;
        if (dluma_new_mod_ticker_add(
                path, "managed", item->registry_key,
                item->approval_fingerprint))
            ++g_dluma_new_mod_ticker.managed_count;
    }
    dluma_free(entries);
}

static void dluma_new_mod_ticker_capture_assets(const WCHAR* path)
{
    DlumaAssetModSnapshot* entries;
    unsigned int total = 0;
    unsigned int count;
    unsigned int index;
    entries = (DlumaAssetModSnapshot*)dluma_alloc(
        (SIZE_T)DLUMA_ASSET_MOD_MAX_ITEMS * sizeof(*entries));
    if (!entries) {
        dluma_log_line("ERROR", "new_mod_ticker_asset_snapshot_allocation_failed");
        return;
    }
    memset(entries, 0, (SIZE_T)DLUMA_ASSET_MOD_MAX_ITEMS * sizeof(*entries));
    (void)dluma_asset_mods_snapshot(entries, DLUMA_ASSET_MOD_MAX_ITEMS, &total, 0);
    count = total > DLUMA_ASSET_MOD_MAX_ITEMS ? DLUMA_ASSET_MOD_MAX_ITEMS : total;
    for (index = 0; index < count; ++index) {
        const DlumaAssetModSnapshot* item = &entries[index];
        const char* fields[2];
        char identity[DLUMA_SHA256_HEX_CAPACITY];
        if (!item->valid || item->source_selection_missing || !item->id[0] ||
            !item->source_identity[0] || !item->fingerprint[0])
            continue;
        fields[0] = item->id;
        fields[1] = item->source_identity;
        dluma_sha256_fields_hex("DLW_NEW_MOD_TICKER_ASSET_ID_V1", fields, 0, 2U, identity);
        if (dluma_new_mod_ticker_add(path, "asset", identity, item->fingerprint))
            ++g_dluma_new_mod_ticker.asset_count;
    }
    dluma_free(entries);
}

void dluma_new_mod_ticker_capture_startup(void)
{
    WCHAR path[DLUMA_MAX_WPATH];
    if (g_dluma_new_mod_ticker.captured)
        return;
    memset(&g_dluma_new_mod_ticker, 0, sizeof(g_dluma_new_mod_ticker));
    g_dluma_new_mod_ticker.captured = 1;
    path[0] = 0;
    dluma_lua_get_global_registry_ini_path(
        path, DLUMA_ARRAY_COUNT(path));
    if (!path[0])
    {
        dluma_log_line(
            "ERROR", "new_mod_ticker_registry_path_unavailable");
        return;
    }
    dluma_new_mod_ticker_capture_lua(path);
    dluma_new_mod_ticker_capture_native(path);
    dluma_new_mod_ticker_capture_managed(path);
    dluma_new_mod_ticker_capture_assets(path);
    dluma_log_u64(
        "INFO", "new_mod_ticker_lua_candidates",
        (ULONGLONG)g_dluma_new_mod_ticker.lua_count);
    dluma_log_u64(
        "INFO", "new_mod_ticker_native_candidates",
        (ULONGLONG)g_dluma_new_mod_ticker.native_count);
    dluma_log_u64(
        "INFO", "new_mod_ticker_managed_candidates",
        (ULONGLONG)g_dluma_new_mod_ticker.managed_count);
    dluma_log_u64(
        "INFO", "new_mod_ticker_asset_candidates",
        (ULONGLONG)g_dluma_new_mod_ticker.asset_count);
    dluma_log_u64(
        "INFO", "new_mod_ticker_candidates",
        (ULONGLONG)g_dluma_new_mod_ticker.count);
}

int dluma_new_mod_ticker_has_pending(void)
{
    return g_dluma_new_mod_ticker.captured &&
           !g_dluma_new_mod_ticker.presented &&
           g_dluma_new_mod_ticker.count != 0U;
}

int dluma_new_mod_ticker_mark_presented(void)
{
    WCHAR path[DLUMA_MAX_WPATH];
    unsigned int index;
    unsigned int persisted = 0;
    int flushed;
    if (!dluma_new_mod_ticker_has_pending())
        return 0;
    g_dluma_new_mod_ticker.presented = 1;
    path[0] = 0;
    dluma_lua_get_global_registry_ini_path(
        path, DLUMA_ARRAY_COUNT(path));
    if (!path[0])
    {
        dluma_log_line(
            "ERROR", "new_mod_ticker_presented_registry_path_unavailable");
        return 0;
    }
    for (index = 0; index < g_dluma_new_mod_ticker.count; ++index)
    {
        WCHAR key[DLUMA_SHA256_HEX_CAPACITY];
        WCHAR value[DLUMA_SHA256_HEX_CAPACITY];
        const DlumaNewModTickerCandidate* candidate =
            &g_dluma_new_mod_ticker.candidates[index];
        if (!dluma_utf8_to_wide(
                candidate->marker_key, key, DLUMA_ARRAY_COUNT(key)) ||
            !dluma_utf8_to_wide(
                candidate->fingerprint, value,
                DLUMA_ARRAY_COUNT(value)) ||
            !WritePrivateProfileStringW(
                DLUMA_NEW_MOD_TICKER_SECTION, key, value, path))
            continue;
        ++persisted;
    }
    flushed = WritePrivateProfileStringW(0, 0, 0, path) ? 1 : 0;
    dluma_log_u64(
        persisted == g_dluma_new_mod_ticker.count && flushed
            ? "INFO" : "WARN",
        "new_mod_ticker_markers_persisted",
        (ULONGLONG)persisted);
    return persisted == g_dluma_new_mod_ticker.count && flushed;
}
