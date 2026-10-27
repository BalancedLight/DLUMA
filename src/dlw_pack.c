#include "dlw_common.h"
#include "dlw_pack.h"
#include "dlw_path_security.h"
#include "dlw_process_lifetime_sources.h"
#include "dlw_sha256_internal.h"
#include "dlw_workshop_vfs_refresh_core.h"

/*
 * filesystem_x64_rwdi.dll exports the public C++ filesystem entry point used
 * by the engine's DLC pack mounting path. Resolve it dynamically so a missing
 * or incompatible export can never prevent the .mod image from loading.
 */
typedef unsigned char (__cdecl *DlumaFsAddSourceFn)(const char* path, unsigned int flags);
typedef void (__cdecl *DlumaFsRefreshPathFn)(const char* path);
typedef void* (__cdecl *DlumaFsFindSourceFn)(const char* path);

#define DLUMA_DEVTOOLS_FILESYSTEM_TIMESTAMP 0x623394CDUL
#define DLUMA_DEVTOOLS_FILESYSTEM_IMAGE_SIZE 0x00060000UL
#define DLUMA_CONTENT_PACK_MAX_BYTES (64ULL * 1024ULL * 1024ULL)

static char g_dluma_pack_path[DLUMA_MAX_WPATH * 2];
static unsigned char g_dluma_pack_mounted;
static unsigned char g_dluma_pack_source_registered;
static DlumaContentPackMountSnapshot g_dluma_pack_mount_snapshot;
static HANDLE g_dluma_pack_identity_handle = INVALID_HANDLE_VALUE;
static volatile LONG g_dluma_pack_source_bind_lock;
static ULONGLONG g_dluma_pack_mount_generation;
static char g_dluma_developer_mode_source_path[DLUMA_MAX_WPATH * 2];
static unsigned char g_dluma_developer_mode_source_mounted;
static unsigned char g_dluma_developer_mode_source_registered;
static HANDLE g_dluma_developer_mode_source_identity_handle =
    INVALID_HANDLE_VALUE;
static char g_dluma_workshop_preview_cache_source_path[DLUMA_MAX_WPATH * 2];
static unsigned char g_dluma_workshop_preview_cache_source_mounted;
static unsigned char g_dluma_workshop_preview_cache_source_registered;
static HANDLE g_dluma_workshop_preview_source_identity_handle =
    INVALID_HANDLE_VALUE;
static DlumaFsRefreshPathFn g_dluma_workshop_preview_refresh_path;
static DlumaWorkshopVfsRefreshCore g_dluma_workshop_preview_vfs_refresh;
static volatile LONG g_dluma_workshop_preview_refresh_unavailable_log_once;
static volatile LONG g_dluma_custom_main_menu_publication_scope_depth;
static volatile LONG g_dluma_custom_main_menu_publication_scope_thread_id;
static volatile LONG g_dluma_custom_main_menu_source_state_lock;
typedef struct DlumaCustomMainMenuDirectorySource
{
    char path[DLUMA_MAX_WPATH * 2];
    char fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    const char *owner_id;
    void *source_object;
    unsigned int source_flags;
    ULONGLONG frontend_generation;
    DlumaProcessLifetimeSourceGenerationToken live_generation;
    DlumaProcessLifetimeSourceGenerationToken retired_generation;
    unsigned char mounted;
    unsigned char registered;
    unsigned char conflicted;
    unsigned char live_generation_valid;
    unsigned char retired_generation_valid;
    HANDLE identity_handle;
} DlumaCustomMainMenuDirectorySource;

static DlumaCustomMainMenuDirectorySource g_dluma_custom_main_menu_project = {
    {0}, {0}, DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT,
    0, 0, 0, {0}, {0}, 0, 0, 0, 0, 0, INVALID_HANDLE_VALUE
};
static DlumaCustomMainMenuDirectorySource g_dluma_custom_main_menu_data = {
    {0}, {0}, DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA,
    0, 0, 0, {0}, {0}, 0, 0, 0, 0, 0, INVALID_HANDLE_VALUE
};
static DlumaCustomMainMenuDirectorySource g_dluma_custom_main_menu_assets_pc = {
    {0}, {0}, DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_ASSETS_PC,
    0, 0, 0, {0}, {0}, 0, 0, 0, 0, 0, INVALID_HANDLE_VALUE
};
static DlumaCustomMainMenuDirectorySource g_dluma_custom_main_menu_overlay = {
    {0}, {0}, DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_OVERLAY,
    0, 0, 0, {0}, {0}, 0, 0, 0, 0, 0, INVALID_HANDLE_VALUE
};
static volatile LONGLONG
    g_dluma_custom_main_menu_frontend_generation_sequence;
static volatile LONGLONG
    g_dluma_custom_main_menu_frontend_generation;

/*
 * Lock order is always pack source-state -> process-lifetime registry.  The
 * registry never calls back into this module.  Native filesystem entry points
 * must never be invoked while this lock is held because their add/remove
 * hooks re-enter the pack source-state layer.
 */
static void dluma_custom_main_menu_source_state_lock(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma_custom_main_menu_source_state_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_custom_main_menu_source_state_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_custom_main_menu_source_state_lock, 0);
}

#define DLUMA_CMM_STOCK_PROJECT_SOURCE_FLAGS 0x100U
#define DLUMA_CMM_STOCK_DATA_SOURCE_FLAGS 0x10DU
#define DLUMA_CMM_STOCK_ASSETS_PC_SOURCE_FLAGS 0x59U

static unsigned int dluma_custom_main_menu_expected_native_flags(
    const DlumaCustomMainMenuDirectorySource* state)
{
    if (state == &g_dluma_custom_main_menu_project)
        return DLUMA_CMM_STOCK_PROJECT_SOURCE_FLAGS;
    if (state == &g_dluma_custom_main_menu_data)
        return DLUMA_CMM_STOCK_DATA_SOURCE_FLAGS;
    if (state == &g_dluma_custom_main_menu_assets_pc)
        return DLUMA_CMM_STOCK_ASSETS_PC_SOURCE_FLAGS;
    return 0U;
}

static int dluma_pack_directory_paths_equal(
    const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right || !left[0] || !right[0])
        return 0;
    for (;; ++index) {
        unsigned char left_value = (unsigned char)left[index];
        unsigned char right_value = (unsigned char)right[index];
        if (left_value == '/') left_value = '\\';
        if (right_value == '/') right_value = '\\';
        if (left_value >= 'A' && left_value <= 'Z')
            left_value = (unsigned char)(left_value + ('a' - 'A'));
        if (right_value >= 'A' && right_value <= 'Z')
            right_value = (unsigned char)(right_value + ('a' - 'A'));
        if (left_value != right_value)
            return 0;
        if (!left_value)
            return 1;
    }
}

static DlumaCustomMainMenuDirectorySource*
dluma_custom_main_menu_state_for_owner(const char* owner_id)
{
    if (owner_id && dluma_stricmp(
            owner_id,
            DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_PROJECT) == 0)
        return &g_dluma_custom_main_menu_project;
    if (owner_id && dluma_stricmp(
            owner_id,
            DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_DATA) == 0)
        return &g_dluma_custom_main_menu_data;
    if (owner_id && dluma_stricmp(
            owner_id,
            DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_ASSETS_PC) == 0)
        return &g_dluma_custom_main_menu_assets_pc;
    if (owner_id && dluma_stricmp(
            owner_id,
            DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU_OVERLAY) == 0)
        return &g_dluma_custom_main_menu_overlay;
    return 0;
}

static DlumaCustomMainMenuDirectorySource*
dluma_custom_main_menu_state_for_path(const char* source_path)
{
    if (source_path && g_dluma_custom_main_menu_project.path[0] &&
        dluma_pack_directory_paths_equal(
            source_path, g_dluma_custom_main_menu_project.path))
        return &g_dluma_custom_main_menu_project;
    if (source_path && g_dluma_custom_main_menu_data.path[0] &&
        dluma_pack_directory_paths_equal(
            source_path, g_dluma_custom_main_menu_data.path))
        return &g_dluma_custom_main_menu_data;
    if (source_path && g_dluma_custom_main_menu_assets_pc.path[0] &&
        dluma_pack_directory_paths_equal(
            source_path, g_dluma_custom_main_menu_assets_pc.path))
        return &g_dluma_custom_main_menu_assets_pc;
    if (source_path && g_dluma_custom_main_menu_overlay.path[0] &&
        dluma_pack_directory_paths_equal(
            source_path, g_dluma_custom_main_menu_overlay.path))
        return &g_dluma_custom_main_menu_overlay;
    return 0;
}

static int dluma_custom_main_menu_generation_token_from_record(
    const DlumaProcessLifetimeSourceRecord* record,
    DlumaProcessLifetimeSourceGenerationToken* token_out)
{
    if (!record || !token_out ||
        record->owner_kind != DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU ||
        !record->owner_id[0] || !record->source_object ||
        !record->mount_generation || !record->registry_generation)
        return 0;
    memset(token_out, 0, sizeof(*token_out));
    token_out->owner_kind = record->owner_kind;
    dluma_str_copy(
        token_out->owner_id, DLUMA_ARRAY_COUNT(token_out->owner_id),
        record->owner_id);
    token_out->source_object = record->source_object;
    token_out->mount_generation = record->mount_generation;
    token_out->registry_generation = record->registry_generation;
    token_out->retirement_registry_generation =
        record->retirement_registry_generation;
    return token_out->owner_id[0] != 0;
}

static int dluma_custom_main_menu_generation_identity_equal(
    const DlumaProcessLifetimeSourceGenerationToken* left,
    const DlumaProcessLifetimeSourceGenerationToken* right)
{
    return left && right &&
        left->owner_kind == right->owner_kind &&
        left->owner_id[0] && right->owner_id[0] &&
        dluma_stricmp(left->owner_id, right->owner_id) == 0 &&
        left->source_object == right->source_object &&
        left->mount_generation == right->mount_generation &&
        left->registry_generation == right->registry_generation;
}

static int dluma_custom_main_menu_directory_identity_equal(
    const DlumaProcessLifetimeSourceRecord* left,
    const DlumaProcessLifetimeSourceRecord* right)
{
    return left && right &&
        left->owner_kind == DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU &&
        right->owner_kind == DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU &&
        left->owner_id[0] && right->owner_id[0] &&
        dluma_stricmp(left->owner_id, right->owner_id) == 0 &&
        left->runtime_source_type ==
            DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY &&
        right->runtime_source_type ==
            DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY &&
        left->source_flags == right->source_flags &&
        left->metadata_complete && right->metadata_complete &&
        left->volume_serial == right->volume_serial &&
        left->file_index_high == right->file_index_high &&
        left->file_index_low == right->file_index_low &&
        left->file_size == right->file_size &&
        dluma_pack_directory_paths_equal(
            left->requested_mount_path,
            right->requested_mount_path) &&
        left->content_sha256[0] && right->content_sha256[0] &&
        dluma_stricmp(
            left->content_sha256, right->content_sha256) == 0;
}

static int dluma_custom_main_menu_state_exact_live_presence(
    const DlumaCustomMainMenuDirectorySource* state,
    DlumaProcessLifetimeSourceGenerationToken* token_out)
{
    DlumaProcessLifetimeSourceGenerationToken token;
    if (token_out) memset(token_out, 0, sizeof(*token_out));
    if (!state || !state->live_generation_valid ||
        !state->owner_id || !state->live_generation.source_object)
        return 0;
    memset(&token, 0, sizeof(token));
    if (!dluma_process_lifetime_source_live_presence(
            DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU,
            state->owner_id,
            state->live_generation.source_object,
            state->live_generation.mount_generation,
            state->live_generation.registry_generation,
            &token))
        return 0;
    if (token_out) *token_out = token;
    return 1;
}

static int dluma_custom_main_menu_state_live_for_current_frontend(
    const DlumaCustomMainMenuDirectorySource* state)
{
    const ULONGLONG current_generation =
        (ULONGLONG)_InterlockedCompareExchange64(
            &g_dluma_custom_main_menu_frontend_generation, 0, 0);
    return current_generation && state &&
        state->frontend_generation == current_generation &&
        dluma_custom_main_menu_state_exact_live_presence(state, 0);
}

static int dluma_custom_main_menu_sync_state_from_registry(
    DlumaCustomMainMenuDirectorySource* state,
    const DlumaProcessLifetimeSourceSnapshot* snapshot,
    ULONGLONG frontend_generation)
{
    unsigned int index;
    if (!state || !snapshot || !snapshot->complete ||
        !state->owner_id || !state->path[0] ||
        !state->fingerprint[0] || !frontend_generation)
        return 0;
    state->frontend_generation = 0;
    state->live_generation_valid = 0;
    memset(&state->live_generation, 0, sizeof(state->live_generation));
    for (index = 0; index < snapshot->count; ++index) {
        const DlumaProcessLifetimeSourceRecord* record =
            &snapshot->records[index];
        DlumaProcessLifetimeSourceGenerationToken token;
        if (record->owner_kind != DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU ||
            dluma_stricmp(record->owner_id, state->owner_id) != 0)
            continue;
        if (record->lifecycle_state ==
                DLUMA_PROCESS_SOURCE_STATE_RETIRED) {
            if (!dluma_custom_main_menu_generation_token_from_record(
                    record, &state->retired_generation))
                return 0;
            state->retired_generation_valid = 1U;
            state->mounted = 0U;
            state->registered = 0U;
            state->source_object = record->source_object;
            state->source_flags = record->source_flags;
            return 0;
        }
        if (!dluma_pack_directory_paths_equal(
                record->requested_mount_path, state->path) ||
            dluma_stricmp(
                record->content_sha256, state->fingerprint) != 0)
            return 0;
        state->source_object = record->source_object;
        state->source_flags = record->source_flags;
        state->mounted = 1U;
        state->registered = 1U;
        state->retired_generation_valid = 0U;
        memset(
            &state->retired_generation, 0,
            sizeof(state->retired_generation));
        if (record->lifecycle_state !=
                DLUMA_PROCESS_SOURCE_STATE_LIVE ||
            !dluma_process_lifetime_source_live_presence(
                record->owner_kind, record->owner_id,
                record->source_object, record->mount_generation,
                record->registry_generation, &token))
            return 0;
        state->live_generation = token;
        state->live_generation_valid = 1U;
        state->frontend_generation = frontend_generation;
        state->conflicted = 0U;
        return 1;
    }
    return 0;
}

static int dluma_custom_main_menu_snapshot_has_exact_live_source(
    const DlumaCustomMainMenuDirectorySource* state,
    const DlumaProcessLifetimeSourceSnapshot* snapshot)
{
    unsigned int index;
    if (!state || !snapshot || !snapshot->complete ||
        !state->owner_id || !state->path[0] || !state->fingerprint[0])
        return 0;
    for (index = 0; index < snapshot->count; ++index) {
        const DlumaProcessLifetimeSourceRecord* record =
            &snapshot->records[index];
        DlumaProcessLifetimeSourceGenerationToken token;
        if (record->owner_kind != DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU ||
            dluma_stricmp(record->owner_id, state->owner_id) != 0 ||
            record->lifecycle_state != DLUMA_PROCESS_SOURCE_STATE_LIVE ||
            !dluma_pack_directory_paths_equal(
                record->requested_mount_path, state->path) ||
            dluma_stricmp(record->content_sha256, state->fingerprint) != 0)
            continue;
        memset(&token, 0, sizeof(token));
        if (dluma_process_lifetime_source_live_presence(
                record->owner_kind, record->owner_id,
                record->source_object, record->mount_generation,
                record->registry_generation, &token))
            return 1;
    }
    return 0;
}

static const WCHAR* dluma_pack_skip_extended_prefix(const WCHAR* path)
{
    if (path && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] == L'?' && path[3] == L'\\')
        return path + 4;
    return path;
}

static int dluma_capture_content_pack_mount_snapshot(
    const WCHAR* pack_path, const char* mounted_path,
    ULONGLONG maximum_bytes,
    DlumaContentPackMountSnapshot* output, HANDLE* identity_handle_out)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
    LARGE_INTEGER size;
    WCHAR final_path[DLUMA_MAX_WPATH];
    const WCHAR* canonical;
    if (identity_handle_out)
        *identity_handle_out = INVALID_HANDLE_VALUE;
    if (!pack_path || !pack_path[0] || !mounted_path ||
        !mounted_path[0] || !output || !identity_handle_out)
        return 0;
    memset(output, 0, sizeof(*output));
    file = CreateFileW(
        pack_path, GENERIC_READ, FILE_SHARE_READ,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        !GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        !maximum_bytes ||
        (ULONGLONG)size.QuadPart > maximum_bytes ||
        !dluma_get_final_path_checked(
            file, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
        !dluma_sha256_handle_hex(
            file, maximum_bytes, output->content_sha256)) {
        CloseHandle(file);
        memset(output, 0, sizeof(*output));
        return 0;
    }
    canonical = dluma_pack_skip_extended_prefix(final_path);
    if (!dluma_wide_to_utf8(
            canonical, output->canonical_path,
            DLUMA_ARRAY_COUNT(output->canonical_path))) {
        CloseHandle(file);
        memset(output, 0, sizeof(*output));
        return 0;
    }
    dluma_str_copy(
        output->mounted_path, DLUMA_ARRAY_COUNT(output->mounted_path),
        mounted_path);
    output->file_size = (ULONGLONG)size.QuadPart;
    output->source_flags = 9U;
    output->volume_serial = info.dwVolumeSerialNumber;
    output->file_index_high = info.nFileIndexHigh;
    output->file_index_low = info.nFileIndexLow;
    output->metadata_complete = 1;
    *identity_handle_out = file;
    return 1;
}

static DWORD dluma_pack_read_u32(const BYTE* address)
{
    DWORD value = 0;
    if (address && dluma_is_readable(address, sizeof(value)))
        memcpy(&value, address, sizeof(value));
    return value;
}

static int dluma_pack_get_pe_identity(
    HMODULE module, DWORD* timestamp, DWORD* image_size)
{
    BYTE* base = (BYTE*)module;
    DWORD pe_offset;
    if (timestamp) *timestamp = 0;
    if (image_size) *image_size = 0;
    if (!base || !dluma_is_readable(base, 0x100) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    pe_offset = dluma_pack_read_u32(base + 0x3c);
    if (!pe_offset || pe_offset > 0x100000UL ||
        !dluma_is_readable(base + pe_offset, 0x58) ||
        memcmp(base + pe_offset, "PE\0\0", 4) != 0)
        return 0;
    if (timestamp)
        *timestamp = dluma_pack_read_u32(base + pe_offset + 0x08);
    if (image_size)
        *image_size = dluma_pack_read_u32(base + pe_offset + 0x50);
    return timestamp && image_size && *timestamp && *image_size;
}

static void* dluma_pack_find_source_exact(
    HMODULE filesystem, const char* requested_path)
{
    static const char find_source_export[] =
        "?find_source@fs@@YAPEAVCFSSource@@PEBD@Z";
    static const BYTE expected_prefix[24] = {
        0x40, 0x55, 0x53, 0x57, 0x48, 0x8d, 0xac, 0x24,
        0x20, 0xfe, 0xff, 0xff, 0x48, 0x81, 0xec, 0xe0,
        0x02, 0x00, 0x00, 0x48, 0x8b, 0xd1, 0x48, 0x83
    };
    BYTE* base = (BYTE*)filesystem;
    BYTE* expected;
    DlumaFsFindSourceFn find_source;
    DWORD timestamp = 0;
    DWORD image_size = 0;
    if (!filesystem || !requested_path || !requested_path[0] ||
        !dluma_pack_get_pe_identity(
            filesystem, &timestamp, &image_size) ||
        timestamp != DLUMA_DEVTOOLS_FILESYSTEM_TIMESTAMP ||
        image_size != DLUMA_DEVTOOLS_FILESYSTEM_IMAGE_SIZE)
        return 0;
    expected = base + 0x00014430UL;
    find_source = (DlumaFsFindSourceFn)GetProcAddress(
        filesystem, find_source_export);
    if (!find_source ||
        (BYTE*)find_source != expected ||
        !dluma_is_readable(expected, sizeof(expected_prefix)) ||
        memcmp(expected, expected_prefix,
               sizeof(expected_prefix)) != 0)
        return 0;
    return find_source(requested_path);
}

static int dluma_pack_capture_directory_identity(
    const WCHAR* directory_path,
    DlumaProcessLifetimeSourceRecord* record,
    HANDLE* identity_handle_out)
{
    HANDLE directory;
    BY_HANDLE_FILE_INFORMATION info;
    WCHAR final_path[DLUMA_MAX_WPATH];
    const WCHAR* canonical;
    if (identity_handle_out)
        *identity_handle_out = INVALID_HANDLE_VALUE;
    if (!directory_path || !directory_path[0] || !record ||
        !identity_handle_out)
        return 0;
    directory = CreateFileW(
        directory_path, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        0, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_OPEN_REPARSE_POINT,
        0);
    if (directory == INVALID_HANDLE_VALUE)
        return 0;
    if (!GetFileInformationByHandle(directory, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_get_final_path_checked(
            directory, final_path,
            DLUMA_ARRAY_COUNT(final_path))) {
        CloseHandle(directory);
        return 0;
    }
    canonical = dluma_pack_skip_extended_prefix(final_path);
    if (!dluma_wide_to_utf8(
            canonical, record->requested_mount_path,
            DLUMA_ARRAY_COUNT(record->requested_mount_path))) {
        CloseHandle(directory);
        return 0;
    }
    record->volume_serial = info.dwVolumeSerialNumber;
    record->file_index_high = info.nFileIndexHigh;
    record->file_index_low = info.nFileIndexLow;
    record->metadata_complete = 1;
    /*
     * Do not share deletion and retain the identity handle for process
     * lifetime. Files inside these writable loader directories may change,
     * but the mounted directory itself cannot be replaced by a junction or a
     * different file identity after ownership review.
     */
    *identity_handle_out = directory;
    return 1;
}

static int dluma_pack_register_directory_source(
    HMODULE filesystem, const WCHAR* directory_path,
    const char* requested_path,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id,
    const char* content_fingerprint,
    unsigned int source_flags,
    HANDLE* identity_handle_out)
{
    DlumaProcessLifetimeSourceRecord record;
    void* source;
    if (!filesystem || !directory_path || !requested_path ||
        !owner_id || (source_flags != 8U && source_flags != 9U) ||
        !identity_handle_out ||
        (*identity_handle_out != INVALID_HANDLE_VALUE &&
         *identity_handle_out != 0))
        return 0;
    source = dluma_pack_find_source_exact(
        filesystem, requested_path);
    if (!source) return 0;
    memset(&record, 0, sizeof(record));
    record.owner_kind = owner_kind;
    dluma_str_copy(
        record.owner_id, DLUMA_ARRAY_COUNT(record.owner_id),
        owner_id);
    record.source_object = source;
    record.runtime_source_type =
        DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY;
    record.source_flags = source_flags;
    if (content_fingerprint && content_fingerprint[0])
        dluma_str_copy(
            record.content_sha256,
            DLUMA_ARRAY_COUNT(record.content_sha256),
            content_fingerprint);
    if (!dluma_pack_capture_directory_identity(
            directory_path, &record, identity_handle_out))
        return 0;
    return dluma_process_lifetime_source_register(
        &record, 0);
}

static int dluma_custom_main_menu_capture_directory_record(
    const WCHAR* directory_path,
    DlumaCustomMainMenuDirectorySource* state,
    void* exact_source,
    unsigned int source_flags,
    DlumaProcessLifetimeSourceRecord* record_out,
    HANDLE* identity_handle_out)
{
    if (!directory_path || !directory_path[0] || !state ||
        !state->owner_id || !state->path[0] ||
        !state->fingerprint[0] || !exact_source || !source_flags ||
        !record_out || !identity_handle_out)
        return 0;
    *identity_handle_out = INVALID_HANDLE_VALUE;
    memset(record_out, 0, sizeof(*record_out));
    record_out->owner_kind = DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU;
    dluma_str_copy(
        record_out->owner_id,
        DLUMA_ARRAY_COUNT(record_out->owner_id), state->owner_id);
    record_out->source_object = exact_source;
    record_out->runtime_source_type =
        DLUMA_PROCESS_SOURCE_RUNTIME_DIRECTORY;
    record_out->source_flags = source_flags;
    dluma_str_copy(
        record_out->content_sha256,
        DLUMA_ARRAY_COUNT(record_out->content_sha256),
        state->fingerprint);
    if (!dluma_pack_capture_directory_identity(
            directory_path, record_out, identity_handle_out) ||
        !dluma_pack_directory_paths_equal(
            record_out->requested_mount_path, state->path)) {
        if (*identity_handle_out != INVALID_HANDLE_VALUE &&
            *identity_handle_out != 0)
            CloseHandle(*identity_handle_out);
        *identity_handle_out = INVALID_HANDLE_VALUE;
        memset(record_out, 0, sizeof(*record_out));
        return 0;
    }
    return 1;
}

static int dluma_custom_main_menu_register_directory_generation(
    DlumaCustomMainMenuDirectorySource* state,
    const DlumaProcessLifetimeSourceRecord* record,
    int* replaced_out)
{
    DlumaProcessLifetimeSourceSnapshot snapshot;
    unsigned int index;
    if (replaced_out) *replaced_out = 0;
    if (!state || !record || !state->owner_id)
        return 0;
    if (!state->retired_generation_valid &&
        dluma_process_lifetime_sources_snapshot(&snapshot) &&
        snapshot.complete) {
        for (index = 0; index < snapshot.count; ++index) {
            const DlumaProcessLifetimeSourceRecord* existing =
                &snapshot.records[index];
            if (existing->owner_kind !=
                    DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU ||
                dluma_stricmp(existing->owner_id, state->owner_id) != 0 ||
                existing->lifecycle_state !=
                    DLUMA_PROCESS_SOURCE_STATE_RETIRED)
                continue;
            if (!dluma_custom_main_menu_generation_token_from_record(
                    existing, &state->retired_generation))
                return 0;
            state->retired_generation_valid = 1U;
            break;
        }
    }
    if (state->retired_generation_valid) {
        if (!dluma_process_lifetime_source_compare_replace(
                &state->retired_generation, record, 0))
            return 0;
        if (replaced_out) *replaced_out = 1;
        state->retired_generation_valid = 0U;
        memset(
            &state->retired_generation, 0,
            sizeof(state->retired_generation));
    } else if (!dluma_process_lifetime_source_register(record, 0)) {
        return 0;
    }
    state->live_generation_valid = 0U;
    state->frontend_generation = 0;
    memset(&state->live_generation, 0, sizeof(state->live_generation));
    return 1;
}

static int dluma_pack_register_pack_source(
    HMODULE filesystem,
    DlumaContentPackMountSnapshot* mount_snapshot,
    DlumaProcessLifetimeSourceOwnerKind owner_kind,
    const char* owner_id)
{
    DlumaProcessLifetimeSourceRecord record;
    void* source;
    if (!filesystem || !mount_snapshot || !owner_id || !owner_id[0] ||
        !mount_snapshot->metadata_complete)
        return 0;
    source = dluma_pack_find_source_exact(
        filesystem, mount_snapshot->mounted_path);
    if (!source) return 0;
    memset(&record, 0, sizeof(record));
    record.owner_kind = owner_kind;
    dluma_str_copy(
        record.owner_id, DLUMA_ARRAY_COUNT(record.owner_id),
        owner_id);
    record.source_object = source;
    dluma_str_copy(
        record.requested_mount_path,
        DLUMA_ARRAY_COUNT(record.requested_mount_path),
        mount_snapshot->mounted_path);
    record.runtime_source_type =
        DLUMA_PROCESS_SOURCE_RUNTIME_PACK;
    record.source_flags = mount_snapshot->source_flags;
    record.volume_serial = mount_snapshot->volume_serial;
    record.file_index_high = mount_snapshot->file_index_high;
    record.file_index_low = mount_snapshot->file_index_low;
    record.file_size = mount_snapshot->file_size;
    dluma_str_copy(
        record.content_sha256,
        DLUMA_ARRAY_COUNT(record.content_sha256),
        mount_snapshot->content_sha256);
    record.metadata_complete = 1;
    if (!dluma_process_lifetime_source_register(
            &record, 0))
        return 0;
    mount_snapshot->exact_source =
        (ULONGLONG)(ULONG_PTR)source;
    return 1;
}

static int dluma_bind_workshop_preview_refresh(HMODULE filesystem)
{
    static const char refresh_export[] =
        "?refresh@fs@@YAXPEBD@Z";
    static const BYTE expected_prefix[] = {
        0x48, 0x8b, 0xc4, 0x55, 0x48, 0x8d, 0xa8, 0x58,
        0xfe, 0xff, 0xff, 0x48, 0x81, 0xec, 0xa0, 0x02,
        0x00, 0x00, 0x48, 0x89, 0x58, 0x08, 0x48, 0x8b,
        0xd1, 0x48, 0x89, 0x78, 0x18, 0x48, 0x8d, 0x8d
    };
    BYTE* base = (BYTE*)filesystem;
    BYTE* target;
    DWORD timestamp = 0;
    DWORD image_size = 0;
    const char* failure = 0;
    if (g_dluma_workshop_preview_refresh_path)
        return 1;
    if (!dluma_pack_get_pe_identity(
            filesystem, &timestamp, &image_size))
        failure = "identity_unreadable";
    else if (timestamp != DLUMA_DEVTOOLS_FILESYSTEM_TIMESTAMP ||
             image_size != DLUMA_DEVTOOLS_FILESYSTEM_IMAGE_SIZE)
        failure = "unsupported_identity";
    target = failure ? 0 :
        (BYTE*)GetProcAddress(filesystem, refresh_export);
    if (!failure && !target)
        failure = "path_export_missing";
    if (!failure &&
        (target < base ||
         (SIZE_T)(target - base) >
             (SIZE_T)image_size - sizeof(expected_prefix) ||
         !dluma_is_readable(target, sizeof(expected_prefix)) ||
         memcmp(target, expected_prefix, sizeof(expected_prefix)) != 0))
        failure = "prefix_mismatch";
    if (failure)
    {
        if (dluma_atomic_compare_exchange(
                &g_dluma_workshop_preview_refresh_unavailable_log_once,
                1, 0) == 0)
            dluma_log_pair(
                "WARN", "workshop_preview_vfs_refresh_export",
                failure);
        return 0;
    }
    g_dluma_workshop_preview_refresh_path =
        (DlumaFsRefreshPathFn)target;
    dluma_log_pair(
        "INFO", "workshop_preview_vfs_refresh_export",
        "validated");
    return 1;
}

LONG dluma_workshop_preview_cache_note_dds_published(void)
{
    return dluma_workshop_vfs_refresh_note_published(
        &g_dluma_workshop_preview_vfs_refresh);
}

static int dluma_workshop_preview_cache_refresh_source(void)
{
    LONG generation = 0;
    if (g_dluma.stop_requested ||
        !g_dluma_workshop_preview_refresh_path ||
        !g_dluma_workshop_preview_cache_source_mounted ||
        !g_dluma_workshop_preview_cache_source_path[0] ||
        !dluma_workshop_vfs_refresh_begin(
            &g_dluma_workshop_preview_vfs_refresh,
            &generation))
        return 0;
    dluma_log_pair(
        "INFO", "workshop_preview_vfs_refresh",
        "requested");
    dluma_log_u64(
        "INFO", "workshop_preview_vfs_refresh_generation",
        (ULONGLONG)(unsigned long)generation);
    g_dluma_workshop_preview_refresh_path(
        g_dluma_workshop_preview_cache_source_path);
    dluma_workshop_vfs_refresh_complete(
        &g_dluma_workshop_preview_vfs_refresh,
        generation);
    dluma_log_pair(
        "INFO", "workshop_preview_vfs_refresh",
        "completed");
    return 1;
}

int dluma_workshop_preview_cache_refresh_pending_on_game_thread(void)
{
    return dluma_workshop_preview_cache_refresh_source();
}

int dluma_workshop_preview_cache_refresh_for_visibility_retry(void)
{
    return dluma_workshop_preview_cache_refresh_source();
}

static int dluma_mount_workshop_preview_cache_source(
    HMODULE filesystem, DlumaFsAddSourceFn add_source)
{
    WCHAR cache_path[DLUMA_MAX_WPATH];

    if (g_dluma_workshop_preview_cache_source_mounted)
        return g_dluma_workshop_preview_cache_source_registered != 0;
    if (!add_source)
        return 0;
    dluma_path_join(
        cache_path, DLUMA_ARRAY_COUNT(cache_path), g_dluma.module_dir,
        L"dluma_runtime\\workshop_thumbnails");
    if (!dluma_mkdirs(cache_path))
    {
        dluma_log_pair(
            "WARN", "workshop_preview_cache_source",
            "directory_create_failed");
        return 0;
    }
    if (!dluma_wide_to_utf8(
            cache_path, g_dluma_workshop_preview_cache_source_path,
            DLUMA_ARRAY_COUNT(g_dluma_workshop_preview_cache_source_path)))
    {
        dluma_log_pair(
            "WARN", "workshop_preview_cache_source",
            "path_conversion_failed");
        return 0;
    }

    /*
     * The Windows engine's CUITextureManager resolves texture filenames
     * through fs::exists before IL_LoadImage. A directory source makes the
     * worker-produced DDS files eligible without copying them into a game
     * directory or rebuilding the loader PAK. Files published after this
     * mount are exposed through the separately validated, source-specific
     * refresh path. Mount this process-lifetime loader-owned source before
     * content-isolation hooks are installed so it can never be attributed to
     * a custom-project epoch.
     */
    if (!add_source(g_dluma_workshop_preview_cache_source_path, 8u))
    {
        dluma_log_pair(
            "WARN", "workshop_preview_cache_source",
            "mount_failed_cache_only");
        g_dluma_workshop_preview_cache_source_path[0] = 0;
        return 0;
    }
    g_dluma_workshop_preview_cache_source_mounted = 1;
    g_dluma_workshop_preview_cache_source_registered =
        (unsigned char)(dluma_pack_register_directory_source(
            filesystem, cache_path,
            g_dluma_workshop_preview_cache_source_path,
            DLUMA_SOURCE_OWNER_LOADER_THUMBNAILS,
            DLUMA_SOURCE_OWNER_ID_THUMBNAILS,
            0, 8U,
            &g_dluma_workshop_preview_source_identity_handle) != 0);
    if (!g_dluma_workshop_preview_cache_source_registered) {
        dluma_log_pair(
            "ERROR", "workshop_preview_cache_source",
            "exact_source_ownership_registration_failed");
        return 0;
    }
    dluma_log_pair(
        "INFO", "workshop_preview_cache_source",
        "mounted_process_lifetime");
    dluma_log_pair(
        "INFO", "workshop_preview_cache_source_path",
        g_dluma_workshop_preview_cache_source_path);
    dluma_log_u64("INFO", "workshop_preview_cache_source_flags", 8u);
    return 1;
}

static int dluma_write_developer_mode_debugconf(WCHAR* source_root,
                                              unsigned int capacity)
{
    static const char enabled_script[] =
        "!include(\"DebugConf.def\")\r\n"
        "Cheats()                           // enable cheats\r\n"
        "UseDeveloperKeys()                 // enable cheats - the same as Cheats\r\n"
        "NoLogos()                          // disables logos at game start\r\n"
        "SkipWaitingForInputToStartLoadedLevel()  // force level start once loaded\r\n"
        "SetLogLevel(15)                    // errors + warnings + infos + tests\r\n";
    WCHAR data_dir[DLUMA_MAX_WPATH];
    WCHAR script_path[DLUMA_MAX_WPATH];
    if (!g_dluma.config.developer_mode || !source_root || !capacity)
        return 0;
    source_root[0] = 0;
    dluma_path_join(source_root, capacity, g_dluma.module_dir,
                  L"dluma_runtime\\developer_mode");
    dluma_path_join(data_dir, DLUMA_ARRAY_COUNT(data_dir), source_root, L"data");
    if (!dluma_mkdirs(data_dir)) {
        dluma_log_pair("WARN", "developer_mode_debugconf", "directory_create_failed");
        return 0;
    }
    dluma_path_join(script_path, DLUMA_ARRAY_COUNT(script_path), data_dir,
                  L"debugconf.scr");
    if (!dluma_write_utf8_file(script_path, enabled_script, 0)) {
        dluma_log_pair("WARN", "developer_mode_debugconf", "write_failed");
        return 0;
    }
    dluma_log_pair(
        "INFO", "developer_mode_debugconf",
        "generated_for_explicit_debugconf_launch");
    return 1;
}

static int dluma_mount_developer_mode_source(
    HMODULE filesystem, DlumaFsAddSourceFn add_source)
{
    WCHAR source_root[DLUMA_MAX_WPATH];
    if (!g_dluma.config.developer_mode) {
        dluma_log_pair("INFO", "developer_mode_debugconf", "disabled");
        return 1;
    }
    if (g_dluma_developer_mode_source_mounted)
        return g_dluma_developer_mode_source_registered != 0;
    if (!add_source || !dluma_write_developer_mode_debugconf(
                           source_root, DLUMA_ARRAY_COUNT(source_root)))
        return 0;
    if (!dluma_wide_to_utf8(source_root, g_dluma_developer_mode_source_path,
                          DLUMA_ARRAY_COUNT(g_dluma_developer_mode_source_path))) {
        dluma_log_pair("WARN", "developer_mode_debugconf", "path_conversion_failed");
        return 0;
    }
    /* Preserve a loader-owned source for callers that explicitly launch with
     * -debugconf=debugconf.scr. Mounting alone does not make the gamedll parse
     * the file when that command-line selection is absent; the separately
     * exact-build-gated startup profile owns the supported native flags. */
    if (!add_source(g_dluma_developer_mode_source_path, 8u)) {
        dluma_log_pair("WARN", "developer_mode_debugconf", "mount_failed");
        g_dluma_developer_mode_source_path[0] = 0;
        return 0;
    }
    g_dluma_developer_mode_source_mounted = 1;
    g_dluma_developer_mode_source_registered =
        (unsigned char)(dluma_pack_register_directory_source(
            filesystem, source_root,
            g_dluma_developer_mode_source_path,
            DLUMA_SOURCE_OWNER_LOADER_DEVELOPER_MODE,
            DLUMA_SOURCE_OWNER_ID_DEVELOPER_MODE,
            0, 8U,
            &g_dluma_developer_mode_source_identity_handle) != 0);
    if (!g_dluma_developer_mode_source_registered) {
        dluma_log_pair(
            "ERROR", "developer_mode_debugconf",
            "exact_source_ownership_registration_failed");
        return 0;
    }
    dluma_log_pair(
        "INFO", "developer_mode_debugconf",
        "mounted_process_lifetime_parse_not_implied");
    return 1;
}

int dluma_mount_content_pack(void)
{
    static const char* add_source_export =
        "?add_source@fs@@YA_NPEBDW4ENUM@FFSAddSourceFlags@@@Z";
    HMODULE filesystem;
    DlumaFsAddSourceFn add_source;
    WCHAR pack_path[DLUMA_MAX_WPATH];
    DlumaContentPackMountSnapshot mount_snapshot;
    HANDLE pack_identity_handle = INVALID_HANDLE_VALUE;
    int pack_available;

    if (g_dluma_pack_mounted)
        return g_dluma_pack_source_registered &&
               g_dluma_workshop_preview_cache_source_registered &&
               (!g_dluma.config.developer_mode ||
                g_dluma_developer_mode_source_registered);
    dluma_path_join(pack_path, DLUMA_ARRAY_COUNT(pack_path), g_dluma.module_dir,
                  L"dluma.pak");
    pack_available = dluma_path_exists(pack_path);
    memset(&mount_snapshot, 0, sizeof(mount_snapshot));
    if (pack_available && !dluma_wide_to_utf8(
                              pack_path, g_dluma_pack_path,
                              DLUMA_ARRAY_COUNT(g_dluma_pack_path))) {
        dluma_log_line("ERROR", "Could not convert the content-pack path to UTF-8.");
        return 0;
    }
    if (pack_available &&
        !dluma_capture_content_pack_mount_snapshot(
            pack_path, g_dluma_pack_path, DLUMA_CONTENT_PACK_MAX_BYTES,
            &mount_snapshot,
            &pack_identity_handle))
        dluma_log_pair(
            "WARN", "content_pack_mount_identity",
            "canonical_metadata_or_content_hash_unavailable");

    filesystem = GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    if (!filesystem) {
        if (pack_identity_handle != INVALID_HANDLE_VALUE)
            CloseHandle(pack_identity_handle);
        dluma_log_line("WARN", "filesystem_x64_rwdi.dll is not loaded; content pack mounting is unavailable.");
        return 0;
    }
    add_source = (DlumaFsAddSourceFn)GetProcAddress(filesystem, add_source_export);
    if (!add_source) {
        if (pack_identity_handle != INVALID_HANDLE_VALUE)
            CloseHandle(pack_identity_handle);
        dluma_log_line("WARN", "The current filesystem build does not expose the validated fs::add_source entry point.");
        return 0;
    }
    (void)dluma_bind_workshop_preview_refresh(filesystem);

    /* Developer Mode is deliberately independent of the optional UI pack.
     * It is mounted before the original game-script initializer in either
     * case, so a missing pack cannot silently turn the requested debug
     * configuration into a no-op. */
    if (!pack_available) {
        dluma_log_line("WARN", "Optional content pack was not found; the native loader will continue without XUI overrides.");
        (void)dluma_mount_developer_mode_source(
            filesystem, add_source);
        return 0;
    }

    /* 9 is the standard pack-source flag set used by the engine's
     * CLobbyStorageBase::MountDlcNamed pack branch. */
    if (!add_source(g_dluma_pack_path, 9u)) {
        if (pack_identity_handle != INVALID_HANDLE_VALUE)
            CloseHandle(pack_identity_handle);
        dluma_log_pair("ERROR", "content_pack_mount_failed", g_dluma_pack_path);
        return 0;
    }
    g_dluma_pack_mounted = 1;
    mount_snapshot.mount_generation = ++g_dluma_pack_mount_generation;
    if (!mount_snapshot.mount_generation)
        mount_snapshot.mount_generation = ++g_dluma_pack_mount_generation;
    mount_snapshot.mounted = 1;
    g_dluma_pack_source_registered =
        (unsigned char)(dluma_pack_register_pack_source(
            filesystem, &mount_snapshot,
            DLUMA_SOURCE_OWNER_LOADER_PAK,
            DLUMA_SOURCE_OWNER_ID_LOADER_PAK) != 0);
    if (!g_dluma_pack_source_registered)
        dluma_log_pair(
            "ERROR", "content_pack_mount_identity",
            "exact_source_ownership_registration_failed");
    g_dluma_pack_mount_snapshot = mount_snapshot;
    g_dluma_pack_identity_handle = pack_identity_handle;
    dluma_atomic_exchange(&g_dluma_pack_source_bind_lock, 0);
    dluma_log_pair("INFO", "content_pack_mounted", g_dluma_pack_path);
    dluma_log_u64("INFO", "content_pack_source_flags", 9u);
    const int preview_owned =
        dluma_mount_workshop_preview_cache_source(
            filesystem, add_source);
    const int developer_owned =
        dluma_mount_developer_mode_source(
            filesystem, add_source);
    return g_dluma_pack_source_registered &&
           mount_snapshot.exact_source != 0 &&
           preview_owned && developer_owned;
}

static int dluma_mount_custom_main_menu_directory(
    HMODULE filesystem,
    DlumaFsAddSourceFn add_source,
    const WCHAR* directory_path,
    const char* fingerprint,
    unsigned int source_flags,
    DlumaCustomMainMenuDirectorySource* state)
{
    void* exact_source;
    char source_path[DLUMA_MAX_WPATH * 2];
    int adopted_existing;
    int replaced_generation = 0;
    DlumaProcessLifetimeSourceRecord record;
    HANDLE identity_handle = INVALID_HANDLE_VALUE;
    if (!filesystem || !add_source || !directory_path ||
        !directory_path[0] || !fingerprint ||
        (source_flags != 8U && source_flags != 9U) ||
        strlen(fingerprint) != 64U || !state || !state->owner_id)
        return 0;
    if (!dluma_wide_to_utf8(
            dluma_pack_skip_extended_prefix(directory_path), source_path,
            DLUMA_ARRAY_COUNT(source_path)))
    {
        dluma_log_pair(
            "ERROR", "custom_main_menu_source_mount",
            "directory_path_conversion_failed");
        return 0;
    }
    exact_source = dluma_pack_find_source_exact(filesystem, source_path);
    adopted_existing = exact_source != 0;
    if (!exact_source)
    {
        if (!add_source(source_path, source_flags))
        {
            dluma_log_pair(
                "ERROR", "custom_main_menu_source_mount",
                "add_source_failed_and_exact_source_absent");
            return 0;
        }
        exact_source = dluma_pack_find_source_exact(
            filesystem, source_path);
        if (!exact_source)
        {
            dluma_log_pair(
                "ERROR", "custom_main_menu_source_mount",
                "exact_source_missing_after_successful_add");
            return 0;
        }
    }
    if (dluma_pack_find_source_exact(filesystem, source_path) !=
            exact_source)
        return 0;
    dluma_custom_main_menu_source_state_lock();
    if ((state->mounted || state->registered) &&
        (!dluma_pack_directory_paths_equal(state->path, source_path) ||
         dluma_stricmp(state->fingerprint, fingerprint) != 0)) {
        dluma_custom_main_menu_source_state_unlock();
        return 0;
    }
    dluma_str_copy(
        state->path, DLUMA_ARRAY_COUNT(state->path), source_path);
    state->source_object = exact_source;
    state->source_flags = source_flags;
    dluma_str_copy(
        state->fingerprint, DLUMA_ARRAY_COUNT(state->fingerprint),
        fingerprint);
    state->registered = (unsigned char)(
        dluma_custom_main_menu_capture_directory_record(
            directory_path, state, exact_source, source_flags,
            &record, &identity_handle) &&
        dluma_custom_main_menu_register_directory_generation(
            state, &record, &replaced_generation));
    if (!state->registered)
    {
        if (identity_handle != INVALID_HANDLE_VALUE &&
            identity_handle != 0)
            CloseHandle(identity_handle);
        state->source_object = 0;
        state->source_flags = 0;
        state->registered = 0;
        state->mounted = 0;
        dluma_log_pair(
            "ERROR", "custom_main_menu_source",
            "exact_directory_source_ownership_registration_failed");
        dluma_custom_main_menu_source_state_unlock();
        return 0;
    }
    /* Previous generation identity handles intentionally remain open. */
    state->identity_handle = identity_handle;
    state->mounted = 1U;
    dluma_log_pair(
        "INFO", "custom_main_menu_source_mount",
        adopted_existing
            ? (replaced_generation
                ? "replaced_retired_exact_engine_directory_source"
                : "adopted_existing_exact_engine_directory_source")
            : "added_new_exact_directory_source");
    dluma_log_pair(
        "INFO", "custom_main_menu_source_owner", state->owner_id);
    dluma_log_pair(
        "INFO", "custom_main_menu_source_path", state->path);
    dluma_log_pair(
        "INFO", "custom_main_menu_source_fingerprint", fingerprint);
    dluma_log_u64(
        "INFO", "custom_main_menu_source_flags", source_flags);
    dluma_custom_main_menu_source_state_unlock();
    return 1;
}

static int dluma_prepare_custom_main_menu_directory(
    const WCHAR* directory_path,
    const char* fingerprint,
    int required,
    DlumaCustomMainMenuDirectorySource* state)
{
    DWORD attributes;
    char path[DLUMA_MAX_WPATH * 2];
    if (!directory_path || !directory_path[0] || !fingerprint ||
        strlen(fingerprint) != 64U || !state || !state->owner_id)
        return 0;
    attributes = GetFileAttributesW(directory_path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (required)
            return 0;
        dluma_custom_main_menu_source_state_lock();
        state->path[0] = 0;
        state->fingerprint[0] = 0;
        state->conflicted = 0;
        dluma_custom_main_menu_source_state_unlock();
        return 1;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U ||
        !dluma_wide_to_utf8(
            dluma_pack_skip_extended_prefix(directory_path), path,
            DLUMA_ARRAY_COUNT(path)))
        return 0;
    dluma_custom_main_menu_source_state_lock();
    if ((state->mounted || state->registered) &&
        (!dluma_pack_directory_paths_equal(state->path, path) ||
         dluma_stricmp(state->fingerprint, fingerprint) != 0)) {
        dluma_custom_main_menu_source_state_unlock();
        return 0;
    }
    dluma_str_copy(state->path, DLUMA_ARRAY_COUNT(state->path), path);
    dluma_str_copy(
        state->fingerprint, DLUMA_ARRAY_COUNT(state->fingerprint),
        fingerprint);
    if (!state->mounted && !state->registered)
        state->conflicted = 0;
    attributes = state->path[0] && state->fingerprint[0];
    dluma_custom_main_menu_source_state_unlock();
    return attributes != 0U;
}

int dluma_custom_main_menu_can_replace_sources(void)
{
    DlumaCustomMainMenuDirectorySource* states[] = {
        &g_dluma_custom_main_menu_project, &g_dluma_custom_main_menu_data,
        &g_dluma_custom_main_menu_assets_pc, &g_dluma_custom_main_menu_overlay
    };
    char paths[4][DLUMA_MAX_WPATH * 2];
    HMODULE filesystem = GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    unsigned int index;
    int ready = 1;
    dluma_custom_main_menu_source_state_lock();
    for (index = 0; index < DLUMA_ARRAY_COUNT(states); ++index) {
        if (states[index]->mounted || states[index]->registered ||
            states[index]->live_generation_valid)
            ready = 0;
        dluma_str_copy(paths[index], DLUMA_ARRAY_COUNT(paths[index]),
                     states[index]->path);
    }
    dluma_custom_main_menu_source_state_unlock();
    if (!ready)
        return 0;
    /* An invalidated frontend token does not remove a native source. */
    for (index = 0; index < DLUMA_ARRAY_COUNT(states); ++index)
        if (paths[index][0] && (!filesystem ||
            dluma_pack_find_source_exact(filesystem, paths[index])))
            return 0;
    return 1;
}

int dluma_prepare_custom_main_menu_sources(
    const WCHAR* project_root,
    const WCHAR* overlay_root,
    const char* fingerprint)
{
    WCHAR data_root[DLUMA_MAX_WPATH];
    WCHAR assets_pc_root[DLUMA_MAX_WPATH];
    DlumaCustomMainMenuDirectorySource* states[] = {
        &g_dluma_custom_main_menu_project, &g_dluma_custom_main_menu_data,
        &g_dluma_custom_main_menu_assets_pc, &g_dluma_custom_main_menu_overlay
    };
    DlumaCustomMainMenuDirectorySource before[4];
    DlumaCustomMainMenuDirectorySource prepared[4];
    unsigned int index;
    if (!project_root || !project_root[0] ||
        !overlay_root || !overlay_root[0] ||
        !fingerprint || strlen(fingerprint) != 64U)
        return 0;
    if (wcslen(project_root) + 11U >=
        DLUMA_ARRAY_COUNT(assets_pc_root))
        return 0;
    dluma_path_join(
        data_root, DLUMA_ARRAY_COUNT(data_root), project_root, L"data");
    dluma_path_join(
        assets_pc_root, DLUMA_ARRAY_COUNT(assets_pc_root),
        project_root, L"assets_pc");
    dluma_custom_main_menu_source_state_lock();
    for (index = 0; index < DLUMA_ARRAY_COUNT(states); ++index)
        before[index] = prepared[index] = *states[index];
    dluma_custom_main_menu_source_state_unlock();
    if (!dluma_prepare_custom_main_menu_directory(
            project_root, fingerprint, 1, &prepared[0]) ||
        !dluma_prepare_custom_main_menu_directory(
            data_root, fingerprint, 1, &prepared[1]) ||
        !dluma_prepare_custom_main_menu_directory(
            assets_pc_root, fingerprint, 0, &prepared[2]) ||
        !dluma_prepare_custom_main_menu_directory(
            overlay_root, fingerprint, 1, &prepared[3]))
        return 0;
    dluma_custom_main_menu_source_state_lock();
    for (index = 0; index < DLUMA_ARRAY_COUNT(states); ++index)
        if (memcmp(states[index], &before[index], sizeof(before[index])) != 0) {
            dluma_custom_main_menu_source_state_unlock();
            return 0;
        }
    for (index = 0; index < DLUMA_ARRAY_COUNT(states); ++index)
        *states[index] = prepared[index];
    dluma_custom_main_menu_source_state_unlock();
    return 1;
}

int dluma_mount_custom_main_menu_overlay(
    const WCHAR* overlay_root,
    const char* fingerprint)
{
    static const char add_source_export[] =
        "?add_source@fs@@YA_NPEBDW4ENUM@FFSAddSourceFlags@@@Z";
    HMODULE filesystem;
    DlumaFsAddSourceFn add_source;
    if (!overlay_root || !overlay_root[0] || !fingerprint ||
        strlen(fingerprint) != 64U)
    {
        dluma_log_pair(
            "ERROR", "custom_main_menu_overlay_mount_phase",
            "arguments_invalid");
        return 0;
    }
    if (!dluma_prepare_custom_main_menu_directory(
            overlay_root, fingerprint, 1,
            &g_dluma_custom_main_menu_overlay))
    {
        dluma_log_pair(
            "ERROR", "custom_main_menu_overlay_mount_phase",
            "overlay_preparation_failed");
        return 0;
    }
    filesystem = GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    if (!filesystem)
    {
        dluma_log_pair(
            "ERROR", "custom_main_menu_overlay_mount_phase",
            "filesystem_module_unavailable");
        return 0;
    }
    add_source = (DlumaFsAddSourceFn)GetProcAddress(
        filesystem, add_source_export);
    if (!add_source)
    {
        dluma_log_pair(
            "ERROR", "custom_main_menu_overlay_mount_phase",
            "add_source_export_unavailable");
        return 0;
    }
    if (!dluma_mount_custom_main_menu_directory(
            filesystem, add_source, overlay_root, fingerprint,
            9U, &g_dluma_custom_main_menu_overlay))
    {
        dluma_log_pair(
            "ERROR", "custom_main_menu_overlay_mount_phase",
            "generated_overlay_failed");
        return 0;
    }
    dluma_log_pair(
        "INFO", "custom_main_menu_source_precedence",
        "loader_pak_then_generated_overlay_before_frontend_then_engine_published_project_data_assets_with_overlay_final_precedence");
    return 1;
}

static DlumaCustomMainMenuDirectorySource*
dluma_custom_main_menu_native_state_for_path(const char* source_path)
{
    DlumaCustomMainMenuDirectorySource* state =
        dluma_custom_main_menu_state_for_path(source_path);
    return dluma_custom_main_menu_expected_native_flags(state)
        ? state : 0;
}

ULONGLONG dluma_custom_main_menu_begin_frontend_generation(void)
{
    DlumaCustomMainMenuDirectorySource* states[] = {
        &g_dluma_custom_main_menu_project,
        &g_dluma_custom_main_menu_data,
        &g_dluma_custom_main_menu_assets_pc,
        &g_dluma_custom_main_menu_overlay
    };
    ULONGLONG generation = (ULONGLONG)_InterlockedIncrement64(
        &g_dluma_custom_main_menu_frontend_generation_sequence);
    unsigned int index;
    if (!generation)
        generation = (ULONGLONG)_InterlockedIncrement64(
            &g_dluma_custom_main_menu_frontend_generation_sequence);
    dluma_custom_main_menu_source_state_lock();
    _InterlockedExchange64(
        &g_dluma_custom_main_menu_frontend_generation,
        (LONGLONG)generation);
    for (index = 0; index < DLUMA_ARRAY_COUNT(states); ++index) {
        states[index]->frontend_generation = 0;
        states[index]->live_generation_valid = 0U;
        states[index]->conflicted = 0U;
        memset(
            &states[index]->live_generation, 0,
            sizeof(states[index]->live_generation));
    }
    dluma_custom_main_menu_source_state_unlock();
    dluma_log_u64(
        "INFO", "custom_main_menu_frontend_generation_begin",
        generation);
    return generation;
}

void dluma_custom_main_menu_invalidate_frontend_generation(
    ULONGLONG frontend_generation)
{
    DlumaCustomMainMenuDirectorySource* states[] = {
        &g_dluma_custom_main_menu_project,
        &g_dluma_custom_main_menu_data,
        &g_dluma_custom_main_menu_assets_pc,
        &g_dluma_custom_main_menu_overlay
    };
    unsigned int index;
    if (!frontend_generation)
        return;
    dluma_custom_main_menu_source_state_lock();
    if ((ULONGLONG)_InterlockedCompareExchange64(
            &g_dluma_custom_main_menu_frontend_generation, 0,
            (LONGLONG)frontend_generation) != frontend_generation) {
        dluma_custom_main_menu_source_state_unlock();
        return;
    }
    for (index = 0; index < DLUMA_ARRAY_COUNT(states); ++index) {
        states[index]->frontend_generation = 0;
        states[index]->live_generation_valid = 0U;
        memset(
            &states[index]->live_generation, 0,
            sizeof(states[index]->live_generation));
    }
    dluma_custom_main_menu_source_state_unlock();
    dluma_log_u64(
        "INFO", "custom_main_menu_frontend_generation_invalidated",
        frontend_generation);
}

int dluma_custom_main_menu_refresh_live_sources(
    ULONGLONG frontend_generation)
{
    DlumaProcessLifetimeSourceSnapshot snapshot;
    int project_live;
    int data_live;
    int assets_pc_live;
    if (!frontend_generation)
        return 0;
    dluma_custom_main_menu_source_state_lock();
    if (
        (ULONGLONG)_InterlockedCompareExchange64(
            &g_dluma_custom_main_menu_frontend_generation, 0, 0) !=
            frontend_generation ||
        !dluma_process_lifetime_sources_snapshot(&snapshot) ||
        !snapshot.complete) {
        dluma_custom_main_menu_source_state_unlock();
        return 0;
    }
    project_live = dluma_custom_main_menu_sync_state_from_registry(
        &g_dluma_custom_main_menu_project, &snapshot,
        frontend_generation);
    data_live = dluma_custom_main_menu_sync_state_from_registry(
        &g_dluma_custom_main_menu_data, &snapshot,
        frontend_generation);
    assets_pc_live = !g_dluma_custom_main_menu_assets_pc.path[0] ||
        dluma_custom_main_menu_sync_state_from_registry(
            &g_dluma_custom_main_menu_assets_pc, &snapshot,
            frontend_generation);
    if (g_dluma_custom_main_menu_overlay.path[0])
        (void)dluma_custom_main_menu_sync_state_from_registry(
            &g_dluma_custom_main_menu_overlay, &snapshot,
            frontend_generation);
    dluma_custom_main_menu_source_state_unlock();
    dluma_log_u64(
        "INFO", "custom_main_menu_live_source_registry_generation",
        snapshot.publication_generation);
    dluma_log_u64(
        "INFO", "custom_main_menu_live_source_frontend_generation",
        frontend_generation);
    return project_live && data_live && assets_pc_live;
}

int dluma_custom_main_menu_capture_native_source_generation(
    const char* source_path,
    void* exact_source,
    DlumaProcessLifetimeSourceGenerationToken* generation_out,
    ULONGLONG* frontend_generation_out)
{
    DlumaCustomMainMenuDirectorySource* state;
    DlumaProcessLifetimeSourceSnapshot snapshot;
    DlumaProcessLifetimeSourceGenerationToken captured;
    unsigned int index;
    unsigned int live_matches = 0;
    int result = 0;
    if (generation_out)
        memset(generation_out, 0, sizeof(*generation_out));
    if (frontend_generation_out)
        *frontend_generation_out = 0;
    if (!source_path || !source_path[0] || !generation_out)
        return 0;

    memset(&snapshot, 0, sizeof(snapshot));
    memset(&captured, 0, sizeof(captured));
    dluma_custom_main_menu_source_state_lock();
    state = dluma_custom_main_menu_state_for_path(source_path);
    if (!state || !state->owner_id || !state->path[0] ||
        !dluma_process_lifetime_sources_snapshot(&snapshot) ||
        !snapshot.complete)
        goto done;
    for (index = 0; index < snapshot.count; ++index) {
        const DlumaProcessLifetimeSourceRecord* record =
            &snapshot.records[index];
        DlumaProcessLifetimeSourceGenerationToken candidate;
        if (record->owner_kind !=
                DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU ||
            dluma_stricmp(record->owner_id, state->owner_id) != 0 ||
            record->lifecycle_state !=
                DLUMA_PROCESS_SOURCE_STATE_LIVE ||
            !record->exact_source_joined ||
            !record->source_object ||
            (exact_source && record->source_object != exact_source) ||
            !dluma_pack_directory_paths_equal(
                record->requested_mount_path, state->path))
            continue;
        memset(&candidate, 0, sizeof(candidate));
        if (!dluma_custom_main_menu_generation_token_from_record(
                record, &candidate))
            continue;
        ++live_matches;
        if (live_matches == 1U)
            captured = candidate;
    }
    if (live_matches != 1U || !captured.source_object)
        goto done;
    *generation_out = captured;
    if (frontend_generation_out)
        *frontend_generation_out = state->frontend_generation;
    result = 1;
done:
    dluma_custom_main_menu_source_state_unlock();
    return result;
}

int dluma_custom_main_menu_retire_native_source_generation(
    const DlumaProcessLifetimeSourceGenerationToken* generation,
    ULONGLONG retirement_registry_generation)
{
    DlumaCustomMainMenuDirectorySource* state;
    DlumaProcessLifetimeSourceGenerationToken retired;
    DlumaProcessLifetimeSourceSnapshot snapshot;
    unsigned int index;
    int result = DLUMA_CMM_SOURCE_RETIRE_NONE;
    if (!generation ||
        generation->owner_kind != DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU ||
        !generation->owner_id[0] || !generation->source_object ||
        !generation->mount_generation ||
        !generation->registry_generation ||
        retirement_registry_generation <=
            generation->registry_generation)
        return DLUMA_CMM_SOURCE_RETIRE_NONE;

    memset(&retired, 0, sizeof(retired));
    memset(&snapshot, 0, sizeof(snapshot));
    dluma_custom_main_menu_source_state_lock();
    state = dluma_custom_main_menu_state_for_owner(
        generation->owner_id);
    if (!state ||
        dluma_stricmp(state->owner_id, generation->owner_id) != 0)
        goto done;
    if (state->retired_generation_valid &&
        dluma_custom_main_menu_generation_identity_equal(
            &state->retired_generation, generation) &&
        retirement_registry_generation >=
            state->retired_generation.retirement_registry_generation) {
        result = DLUMA_CMM_SOURCE_RETIRE_ALREADY;
        goto done;
    }
    if (dluma_process_lifetime_source_retire(
            generation->owner_kind, generation->owner_id,
            generation->source_object, generation->mount_generation,
            generation->registry_generation,
            retirement_registry_generation, &retired)) {
        result = DLUMA_CMM_SOURCE_RETIRE_NEW;
    } else if (dluma_process_lifetime_sources_snapshot(&snapshot) &&
               snapshot.complete) {
        for (index = 0; index < snapshot.count; ++index) {
            const DlumaProcessLifetimeSourceRecord* record =
                &snapshot.records[index];
            DlumaProcessLifetimeSourceGenerationToken candidate;
            memset(&candidate, 0, sizeof(candidate));
            if (record->lifecycle_state !=
                    DLUMA_PROCESS_SOURCE_STATE_RETIRED ||
                !dluma_custom_main_menu_generation_token_from_record(
                    record, &candidate) ||
                !dluma_custom_main_menu_generation_identity_equal(
                    &candidate, generation) ||
                !candidate.retirement_registry_generation ||
                retirement_registry_generation <
                    candidate.retirement_registry_generation)
                continue;
            retired = candidate;
            result = DLUMA_CMM_SOURCE_RETIRE_ALREADY;
            break;
        }
    }
    if (result == DLUMA_CMM_SOURCE_RETIRE_NONE)
        goto done;

    state->retired_generation = retired;
    state->retired_generation_valid = 1U;
    state->live_generation_valid = 0U;
    state->frontend_generation = 0;
    state->mounted = 0U;
    state->registered = 0U;
    state->conflicted = 0U;
    state->source_object = generation->source_object;
    memset(&state->live_generation, 0, sizeof(state->live_generation));
done:
    dluma_custom_main_menu_source_state_unlock();
    if (result == DLUMA_CMM_SOURCE_RETIRE_NEW) {
        dluma_log_pair(
            "INFO", "custom_main_menu_native_source_retired",
            generation->owner_id);
        dluma_log_u64(
            "INFO",
            "custom_main_menu_native_source_retirement_generation",
            retirement_registry_generation);
    }
    return result;
}

int dluma_custom_main_menu_publication_scope_enter(void)
{
    LONG thread_id = (LONG)GetCurrentThreadId();
    if (!thread_id ||
        dluma_atomic_compare_exchange(
            &g_dluma_custom_main_menu_publication_scope_depth, 1, 0) != 0)
        return 0;
    dluma_atomic_exchange(
        &g_dluma_custom_main_menu_publication_scope_thread_id, thread_id);
    return 1;
}

void dluma_custom_main_menu_publication_scope_leave(void)
{
    LONG thread_id = (LONG)GetCurrentThreadId();
    if (thread_id &&
        dluma_atomic_compare_exchange(
            &g_dluma_custom_main_menu_publication_scope_depth, 0, 0) == 1 &&
        dluma_atomic_compare_exchange(
            &g_dluma_custom_main_menu_publication_scope_thread_id, 0, 0) ==
            thread_id)
    {
        dluma_atomic_exchange(
            &g_dluma_custom_main_menu_publication_scope_thread_id, 0);
        dluma_atomic_exchange(
            &g_dluma_custom_main_menu_publication_scope_depth, 0);
    }
}

int dluma_custom_main_menu_publication_scope_active(void)
{
    return dluma_atomic_compare_exchange(
               &g_dluma_custom_main_menu_publication_scope_depth, 0, 0) == 1 &&
           dluma_atomic_compare_exchange(
               &g_dluma_custom_main_menu_publication_scope_thread_id, 0, 0) ==
               (LONG)GetCurrentThreadId();
}

int dluma_custom_main_menu_bind_native_source(
    const char* source_path,
    void* exact_source,
    unsigned int source_flags)
{
    DlumaCustomMainMenuDirectorySource* state;
    unsigned int expected_flags;
    int replaced_generation = 0;
    DlumaProcessLifetimeSourceRecord record;
    WCHAR directory_path[DLUMA_MAX_WPATH];
    HANDLE identity_handle = INVALID_HANDLE_VALUE;
    if (!dluma_custom_main_menu_publication_scope_active())
        return 0;
    dluma_custom_main_menu_source_state_lock();
    state = dluma_custom_main_menu_native_state_for_path(source_path);
    expected_flags = dluma_custom_main_menu_expected_native_flags(state);
    if (!state) {
        dluma_custom_main_menu_source_state_unlock();
        return 0;
    }
    if (!exact_source || !expected_flags || source_flags != expected_flags ||
        !state->fingerprint[0]) {
        state->conflicted = 1U;
        dluma_log_pair(
            "ERROR", "custom_main_menu_native_source_conflict",
            "invalid_exact_source_flags_or_prepared_identity");
        dluma_custom_main_menu_source_state_unlock();
        return 0;
    }
    if (!dluma_utf8_to_wide(
            state->path, directory_path,
            DLUMA_ARRAY_COUNT(directory_path))) {
        dluma_custom_main_menu_source_state_unlock();
        return 0;
    }
    if (!dluma_custom_main_menu_capture_directory_record(
            directory_path, state, exact_source, source_flags,
            &record, &identity_handle)) {
        if (identity_handle != INVALID_HANDLE_VALUE)
            CloseHandle(identity_handle);
        dluma_custom_main_menu_source_state_unlock();
        return 0;
    }
    if (state->retired_generation_valid) {
        if (!dluma_process_lifetime_source_compare_replace(
                &state->retired_generation, &record, 0)) {
            CloseHandle(identity_handle);
            dluma_custom_main_menu_source_state_unlock();
            return 0;
        }
        replaced_generation = 1;
        state->retired_generation_valid = 0U;
        memset(
            &state->retired_generation, 0,
            sizeof(state->retired_generation));
        state->live_generation_valid = 0U;
        state->frontend_generation = 0;
        memset(
            &state->live_generation, 0,
            sizeof(state->live_generation));
    } else if (!dluma_custom_main_menu_register_directory_generation(
                   state, &record, &replaced_generation)) {
        CloseHandle(identity_handle);
        dluma_custom_main_menu_source_state_unlock();
        return 0;
    }
    state->source_object = exact_source;
    state->source_flags = source_flags;
    /* Previous generation identity handles intentionally remain open. */
    state->identity_handle = identity_handle;
    state->mounted = 1U;
    state->registered = 1U;
    state->conflicted = 0U;
    if (replaced_generation)
        dluma_log_pair(
            "INFO", "custom_main_menu_native_source_replaced",
            state->owner_id);
    dluma_log_pair(
        "INFO", "custom_main_menu_native_source_owner",
        state->owner_id);
    dluma_log_pair(
        "INFO", "custom_main_menu_native_source_path",
        state->path);
    dluma_log_u64(
        "INFO", "custom_main_menu_native_source_flags",
        source_flags);
    dluma_custom_main_menu_source_state_unlock();
    return 1;
}

int dluma_custom_main_menu_rebind_retired_native_source(
    const char* source_path,
    void* exact_source,
    unsigned int source_flags)
{
    DlumaCustomMainMenuDirectorySource* state;
    DlumaProcessLifetimeSourceSnapshot snapshot;
    const DlumaProcessLifetimeSourceRecord* retired_record = 0;
    DlumaProcessLifetimeSourceRecord replacement;
    DlumaProcessLifetimeSourceGenerationToken retired_generation;
    WCHAR directory_path[DLUMA_MAX_WPATH];
    HANDLE identity_handle = INVALID_HANDLE_VALUE;
    ULONGLONG frontend_generation;
    unsigned int expected_flags;
    unsigned int index;
    int result = DLUMA_CMM_SOURCE_REBIND_NOT_APPLICABLE;

    if (!source_path || !source_path[0] || !exact_source ||
        dluma_custom_main_menu_publication_scope_active())
        return DLUMA_CMM_SOURCE_REBIND_NOT_APPLICABLE;

    memset(&snapshot, 0, sizeof(snapshot));
    memset(&replacement, 0, sizeof(replacement));
    memset(&retired_generation, 0, sizeof(retired_generation));
    dluma_custom_main_menu_source_state_lock();
    state = dluma_custom_main_menu_native_state_for_path(source_path);
    frontend_generation = (ULONGLONG)_InterlockedCompareExchange64(
        &g_dluma_custom_main_menu_frontend_generation, 0, 0);
    expected_flags = dluma_custom_main_menu_expected_native_flags(state);
    if (!state || !frontend_generation ||
        !state->retired_generation_valid)
        goto done;

    result = DLUMA_CMM_SOURCE_REBIND_FAILED;
    if (!expected_flags || source_flags != expected_flags ||
        !state->path[0] || !state->fingerprint[0] ||
        exact_source == state->retired_generation.source_object ||
        !dluma_utf8_to_wide(
            state->path, directory_path,
            DLUMA_ARRAY_COUNT(directory_path)) ||
        !dluma_custom_main_menu_capture_directory_record(
            directory_path, state, exact_source, source_flags,
            &replacement, &identity_handle) ||
        !dluma_process_lifetime_sources_snapshot(&snapshot) ||
        !snapshot.complete)
        goto done;

    for (index = 0; index < snapshot.count; ++index) {
        const DlumaProcessLifetimeSourceRecord* candidate =
            &snapshot.records[index];
        DlumaProcessLifetimeSourceGenerationToken token;
        memset(&token, 0, sizeof(token));
        if (candidate->lifecycle_state !=
                DLUMA_PROCESS_SOURCE_STATE_RETIRED ||
            !dluma_custom_main_menu_generation_token_from_record(
                candidate, &token) ||
            !dluma_custom_main_menu_generation_identity_equal(
                &token, &state->retired_generation))
            continue;
        if (retired_record)
            goto done;
        retired_record = candidate;
        retired_generation = token;
    }
    if (!retired_record ||
        !dluma_custom_main_menu_directory_identity_equal(
            retired_record, &replacement) ||
        !dluma_process_lifetime_source_compare_replace(
            &retired_generation, &replacement, 0))
        goto done;

    state->source_object = exact_source;
    state->source_flags = source_flags;
    /* Previous generation identity handles intentionally remain open. */
    state->identity_handle = identity_handle;
    identity_handle = INVALID_HANDLE_VALUE;
    state->mounted = 1U;
    state->registered = 1U;
    state->conflicted = 0U;
    state->live_generation_valid = 0U;
    state->frontend_generation = 0;
    memset(&state->live_generation, 0, sizeof(state->live_generation));
    state->retired_generation_valid = 0U;
    memset(
        &state->retired_generation, 0,
        sizeof(state->retired_generation));
    result = DLUMA_CMM_SOURCE_REBIND_SUCCEEDED;
done:
    if (identity_handle != INVALID_HANDLE_VALUE && identity_handle != 0)
        CloseHandle(identity_handle);
    if (result == DLUMA_CMM_SOURCE_REBIND_FAILED && state)
        state->conflicted = 1U;
    dluma_custom_main_menu_source_state_unlock();
    if (result == DLUMA_CMM_SOURCE_REBIND_SUCCEEDED) {
        dluma_log_pair(
            "INFO", "custom_main_menu_native_source_rebound",
            replacement.owner_id);
        dluma_log_u64(
            "INFO", "custom_main_menu_native_source_rebind_generation",
            frontend_generation);
    }
    return result;
}

int dluma_custom_main_menu_refresh_active_frontend_sources(void)
{
    const ULONGLONG frontend_generation =
        (ULONGLONG)_InterlockedCompareExchange64(
            &g_dluma_custom_main_menu_frontend_generation, 0, 0);
    if (!frontend_generation)
        return 1;
    return dluma_custom_main_menu_refresh_live_sources(
        frontend_generation);
}

static int dluma_custom_main_menu_adopt_published_directory(
    HMODULE filesystem,
    DlumaCustomMainMenuDirectorySource* state,
    void* exact_source)
{
    unsigned int expected_flags;
    DlumaProcessLifetimeSourceRecord record;
    WCHAR directory_path[DLUMA_MAX_WPATH];
    char prepared_path[DLUMA_MAX_WPATH * 2];
    char prepared_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    HANDLE identity_handle = INVALID_HANDLE_VALUE;
    int replaced_generation = 0;
    if (!filesystem || !state || !exact_source)
        return 0;
    prepared_path[0] = 0;
    prepared_fingerprint[0] = 0;
    dluma_custom_main_menu_source_state_lock();
    expected_flags = dluma_custom_main_menu_expected_native_flags(state);
    if (expected_flags && state->path[0] && state->fingerprint[0] &&
        state->owner_id) {
        dluma_str_copy(
            prepared_path, DLUMA_ARRAY_COUNT(prepared_path), state->path);
        dluma_str_copy(
            prepared_fingerprint,
            DLUMA_ARRAY_COUNT(prepared_fingerprint), state->fingerprint);
    }
    dluma_custom_main_menu_source_state_unlock();
    if (!expected_flags || !prepared_path[0] ||
        !prepared_fingerprint[0])
        return 0;
    if (!dluma_utf8_to_wide(
            prepared_path, directory_path,
            DLUMA_ARRAY_COUNT(directory_path)))
        return 0;
    if (dluma_pack_find_source_exact(filesystem, prepared_path) !=
            exact_source)
        return 0;
    dluma_custom_main_menu_source_state_lock();
    if (!dluma_pack_directory_paths_equal(state->path, prepared_path) ||
        dluma_stricmp(state->fingerprint, prepared_fingerprint) != 0) {
        dluma_custom_main_menu_source_state_unlock();
        return 0;
    }
    if (!dluma_custom_main_menu_capture_directory_record(
            directory_path, state, exact_source, expected_flags,
            &record, &identity_handle) ||
        !dluma_custom_main_menu_register_directory_generation(
            state, &record, &replaced_generation))
    {
        if (identity_handle != INVALID_HANDLE_VALUE)
            CloseHandle(identity_handle);
        dluma_custom_main_menu_source_state_unlock();
        return 0;
    }
    state->source_object = exact_source;
    state->source_flags = expected_flags;
    /* Previous generation identity handles intentionally remain open. */
    state->identity_handle = identity_handle;
    state->mounted = 1U;
    state->registered = 1U;
    state->conflicted = 0U;
    if (replaced_generation)
        dluma_log_pair(
            "INFO", "custom_main_menu_native_source_replaced",
            state->owner_id);
    dluma_log_pair(
        "INFO", "custom_main_menu_native_source_owner",
        state->owner_id);
    dluma_log_pair(
        "INFO", "custom_main_menu_native_source_path",
        state->path);
    dluma_log_u64(
        "INFO", "custom_main_menu_native_source_flags", expected_flags);
    dluma_log_pair(
        "INFO", "custom_main_menu_native_source_capture",
        replaced_generation
            ? "replaced_retired_engine_published_directory_source"
            : "adopted_exact_engine_published_directory_source");
    dluma_custom_main_menu_source_state_unlock();
    return 1;
}

int dluma_custom_main_menu_capture_published_sources(void)
{
    HMODULE filesystem =
        GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    char project_path[DLUMA_MAX_WPATH * 2];
    char data_path[DLUMA_MAX_WPATH * 2];
    char assets_pc_path[DLUMA_MAX_WPATH * 2];
    void* project_source;
    void* data_source;
    void* assets_pc_source = 0;
    project_path[0] = 0;
    data_path[0] = 0;
    assets_pc_path[0] = 0;
    if (!filesystem)
        return 0;
    dluma_custom_main_menu_source_state_lock();
    dluma_str_copy(
        project_path, DLUMA_ARRAY_COUNT(project_path),
        g_dluma_custom_main_menu_project.path);
    dluma_str_copy(
        data_path, DLUMA_ARRAY_COUNT(data_path),
        g_dluma_custom_main_menu_data.path);
    dluma_str_copy(
        assets_pc_path, DLUMA_ARRAY_COUNT(assets_pc_path),
        g_dluma_custom_main_menu_assets_pc.path);
    dluma_custom_main_menu_source_state_unlock();
    if (!project_path[0] || !data_path[0])
        return 0;
    project_source = dluma_pack_find_source_exact(
        filesystem, project_path);
    data_source = dluma_pack_find_source_exact(
        filesystem, data_path);
    if (assets_pc_path[0])
        assets_pc_source = dluma_pack_find_source_exact(
            filesystem, assets_pc_path);
    if (!project_source || !data_source ||
        (assets_pc_path[0] && !assets_pc_source) ||
        project_source == data_source ||
        (assets_pc_source &&
         (assets_pc_source == project_source ||
          assets_pc_source == data_source)))
        return 0;
    if (!dluma_custom_main_menu_adopt_published_directory(
            filesystem, &g_dluma_custom_main_menu_project,
            project_source) ||
        !dluma_custom_main_menu_adopt_published_directory(
            filesystem, &g_dluma_custom_main_menu_data,
            data_source) ||
        (assets_pc_source &&
         !dluma_custom_main_menu_adopt_published_directory(
              filesystem, &g_dluma_custom_main_menu_assets_pc,
              assets_pc_source)))
        return 0;
    /* Adoption commits the process-lifetime records in REGISTERED state.
     * Their exact intrusive-list identity is joined by
     * dluma_custom_main_menu_bind_live_source_inventory(), after this helper
     * returns to the publication validator.  Requiring LIVE readiness here
     * rejects every correctly adopted stock source before that join can run
     * and forces an otherwise valid custom menu to the stock fallback. */
    return 1;
}

static int dluma_custom_main_menu_native_sources_ready_locked(void)
{
    const ULONGLONG frontend_generation =
        (ULONGLONG)_InterlockedCompareExchange64(
            &g_dluma_custom_main_menu_frontend_generation, 0, 0);
    DlumaProcessLifetimeSourceGenerationToken project_live;
    DlumaProcessLifetimeSourceGenerationToken data_live;
    DlumaProcessLifetimeSourceGenerationToken assets_pc_live;
    if (!frontend_generation ||
        !(!g_dluma_custom_main_menu_project.conflicted &&
          !g_dluma_custom_main_menu_data.conflicted &&
          !g_dluma_custom_main_menu_assets_pc.conflicted) ||
        g_dluma_custom_main_menu_project.frontend_generation !=
            frontend_generation ||
        g_dluma_custom_main_menu_data.frontend_generation !=
            frontend_generation ||
        !g_dluma_custom_main_menu_project.live_generation_valid ||
        !g_dluma_custom_main_menu_data.live_generation_valid ||
        g_dluma_custom_main_menu_project.source_flags !=
            DLUMA_CMM_STOCK_PROJECT_SOURCE_FLAGS ||
        g_dluma_custom_main_menu_data.source_flags !=
            DLUMA_CMM_STOCK_DATA_SOURCE_FLAGS)
        return 0;
    memset(&project_live, 0, sizeof(project_live));
    memset(&data_live, 0, sizeof(data_live));
    memset(&assets_pc_live, 0, sizeof(assets_pc_live));
    if (!dluma_process_lifetime_source_live_presence(
            DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU,
            g_dluma_custom_main_menu_project.owner_id,
            g_dluma_custom_main_menu_project.live_generation.source_object,
            g_dluma_custom_main_menu_project.live_generation.mount_generation,
            g_dluma_custom_main_menu_project.live_generation.registry_generation,
            &project_live) ||
        !dluma_process_lifetime_source_live_presence(
            DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU,
            g_dluma_custom_main_menu_data.owner_id,
            g_dluma_custom_main_menu_data.live_generation.source_object,
            g_dluma_custom_main_menu_data.live_generation.mount_generation,
            g_dluma_custom_main_menu_data.live_generation.registry_generation,
            &data_live))
        return 0;
    if (!g_dluma_custom_main_menu_assets_pc.path[0])
        return 1;
    return !g_dluma_custom_main_menu_assets_pc.conflicted &&
        g_dluma_custom_main_menu_assets_pc.frontend_generation ==
            frontend_generation &&
        g_dluma_custom_main_menu_assets_pc.live_generation_valid &&
        g_dluma_custom_main_menu_assets_pc.source_flags ==
            DLUMA_CMM_STOCK_ASSETS_PC_SOURCE_FLAGS &&
        dluma_process_lifetime_source_live_presence(
            DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU,
            g_dluma_custom_main_menu_assets_pc.owner_id,
            g_dluma_custom_main_menu_assets_pc.live_generation.source_object,
            g_dluma_custom_main_menu_assets_pc.live_generation.mount_generation,
            g_dluma_custom_main_menu_assets_pc.live_generation.registry_generation,
            &assets_pc_live);
}

int dluma_custom_main_menu_native_sources_ready(void)
{
    int ready;
    dluma_custom_main_menu_source_state_lock();
    ready = dluma_custom_main_menu_native_sources_ready_locked();
    dluma_custom_main_menu_source_state_unlock();
    return ready;
}

int dluma_custom_main_menu_sources_ready(void)
{
    int ready;
    dluma_custom_main_menu_source_state_lock();
    ready = dluma_custom_main_menu_native_sources_ready_locked() &&
        !g_dluma_custom_main_menu_overlay.conflicted &&
        g_dluma_custom_main_menu_overlay.source_flags == 9U &&
        dluma_custom_main_menu_state_live_for_current_frontend(
            &g_dluma_custom_main_menu_overlay);
    dluma_custom_main_menu_source_state_unlock();
    return ready;
}

unsigned int dluma_custom_main_menu_source_flags_for_owner(
    const char* owner_id)
{
    const DlumaCustomMainMenuDirectorySource* state;
    unsigned int flags;
    dluma_custom_main_menu_source_state_lock();
    state = dluma_custom_main_menu_state_for_owner(owner_id);
    flags = state && !state->conflicted &&
        dluma_custom_main_menu_state_live_for_current_frontend(state)
        ? state->source_flags : 0U;
    dluma_custom_main_menu_source_state_unlock();
    return flags;
}

void* dluma_custom_main_menu_source_object_for_owner(
    const char* owner_id)
{
    const DlumaCustomMainMenuDirectorySource* state;
    void* source;
    dluma_custom_main_menu_source_state_lock();
    state = dluma_custom_main_menu_state_for_owner(owner_id);
    source = state && !state->conflicted &&
        dluma_custom_main_menu_state_live_for_current_frontend(state)
        ? state->source_object : 0;
    dluma_custom_main_menu_source_state_unlock();
    return source;
}

#if 0
    if (
        !dluma_wide_to_utf8(
            pack_path, g_dluma_custom_main_menu_source_path,
            DLUMA_ARRAY_COUNT(g_dluma_custom_main_menu_source_path)) ||
        !dluma_capture_content_pack_mount_snapshot(
            pack_path, g_dluma_custom_main_menu_source_path,
            DLUMA_CUSTOM_MAIN_MENU_PACK_MAX_BYTES,
            &mount_snapshot, &identity_handle) ||
        !add_source(g_dluma_custom_main_menu_source_path, 9U))
    {
        if (identity_handle != INVALID_HANDLE_VALUE)
            CloseHandle(identity_handle);
        g_dluma_custom_main_menu_source_path[0] = 0;
        return 0;
    }
    g_dluma_custom_main_menu_source_mounted = 1U;
    dluma_str_copy(
        g_dluma_custom_main_menu_source_fingerprint,
        DLUMA_ARRAY_COUNT(g_dluma_custom_main_menu_source_fingerprint),
        mount_snapshot.content_sha256);
    dluma_str_copy(
        g_dluma_custom_main_menu_stage_fingerprint,
        DLUMA_ARRAY_COUNT(g_dluma_custom_main_menu_stage_fingerprint),
        stage_fingerprint);
    g_dluma_custom_main_menu_source_registered =
        (unsigned char)(dluma_pack_register_pack_source(
            filesystem, &mount_snapshot,
            DLUMA_SOURCE_OWNER_CUSTOM_MAIN_MENU,
            DLUMA_SOURCE_OWNER_ID_CUSTOM_MAIN_MENU) != 0);
    g_dluma_custom_main_menu_source_identity_handle = identity_handle;
    if (!g_dluma_custom_main_menu_source_registered)
    {
        dluma_log_pair(
            "ERROR", "custom_main_menu_source",
            "exact_source_ownership_registration_failed");
        return 0;
    }
    dluma_log_pair(
        "INFO", "custom_main_menu_source",
        "mounted_process_lifetime_pack_after_loader_pak_with_override_precedence");
    dluma_log_pair(
        "INFO", "custom_main_menu_source_path",
        g_dluma_custom_main_menu_source_path);
    dluma_log_pair(
        "INFO", "custom_main_menu_source_fingerprint",
        g_dluma_custom_main_menu_source_fingerprint);
    dluma_log_pair(
        "INFO", "custom_main_menu_stage_fingerprint",
        stage_fingerprint);
    dluma_log_u64(
        "INFO", "custom_main_menu_source_flags", 9U);
    return 1;
}
#endif

void dluma_unmount_content_pack(void)
{
    DlumaProcessLifetimeSourceSnapshot snapshot;
    int snapshot_complete = 0;
    int project_live = 0;
    int data_live = 0;
    int assets_pc_live = 0;
    int overlay_live = 0;
    dluma_custom_main_menu_source_state_lock();
    if (!g_dluma_pack_mounted &&
        !g_dluma_workshop_preview_cache_source_mounted &&
        !g_dluma_developer_mode_source_mounted &&
        !g_dluma_custom_main_menu_project.path[0] &&
        !g_dluma_custom_main_menu_data.path[0] &&
        !g_dluma_custom_main_menu_assets_pc.path[0] &&
        !g_dluma_custom_main_menu_overlay.path[0]) {
        dluma_custom_main_menu_source_state_unlock();
        return;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot_complete =
        dluma_process_lifetime_sources_snapshot(&snapshot) &&
        snapshot.complete;
    if (snapshot_complete) {
        project_live = dluma_custom_main_menu_snapshot_has_exact_live_source(
            &g_dluma_custom_main_menu_project, &snapshot);
        data_live = dluma_custom_main_menu_snapshot_has_exact_live_source(
            &g_dluma_custom_main_menu_data, &snapshot);
        assets_pc_live =
            dluma_custom_main_menu_snapshot_has_exact_live_source(
                &g_dluma_custom_main_menu_assets_pc, &snapshot);
        overlay_live = dluma_custom_main_menu_snapshot_has_exact_live_source(
            &g_dluma_custom_main_menu_overlay, &snapshot);
    }

    /*
     * Do not call fs::rem_source during process/game-script shutdown.
     * The retail/DevTools shutdown order tears down UI and filesystem-owned
     * virtual objects inside the original gamedll shutdown. Calling back into
     * fs::rem_source after that point can invoke a partially destroyed C++
     * object and is a plausible source of R6025 pure-virtual termination.
     *
     * The universal loader is process-lifetime in the current architecture,
     * so Windows will reclaim the mounted source when the process exits. A
     * future live-unload feature must add an earlier, explicit safe phase.
     */
    if (g_dluma_pack_mounted)
        dluma_log_pair("INFO", "content_pack_left_mounted_until_process_exit",
                     g_dluma_pack_path);
    if (g_dluma_workshop_preview_cache_source_mounted)
        dluma_log_pair(
            "INFO",
            "workshop_preview_cache_source_left_mounted_until_process_exit",
            g_dluma_workshop_preview_cache_source_path);
    if (g_dluma_developer_mode_source_mounted)
        dluma_log_pair(
            "INFO", "developer_mode_debugconf_left_mounted_until_process_exit",
            g_dluma_developer_mode_source_path);
    if (g_dluma_custom_main_menu_project.path[0] && !project_live)
        dluma_log_pair(
            "INFO", "custom_main_menu_source_retired_or_absent_at_shutdown",
            g_dluma_custom_main_menu_project.owner_id);
    if (g_dluma_custom_main_menu_data.path[0] && !data_live)
        dluma_log_pair(
            "INFO", "custom_main_menu_source_retired_or_absent_at_shutdown",
            g_dluma_custom_main_menu_data.owner_id);
    if (g_dluma_custom_main_menu_assets_pc.path[0] && !assets_pc_live)
        dluma_log_pair(
            "INFO", "custom_main_menu_source_retired_or_absent_at_shutdown",
            g_dluma_custom_main_menu_assets_pc.owner_id);
    if (g_dluma_custom_main_menu_overlay.path[0] && !overlay_live)
        dluma_log_pair(
            "INFO", "custom_main_menu_source_retired_or_absent_at_shutdown",
            g_dluma_custom_main_menu_overlay.owner_id);
    if (project_live)
        dluma_log_pair(
            "INFO",
            "custom_main_menu_project_left_mounted_until_process_exit",
            g_dluma_custom_main_menu_project.path);
    if (data_live)
        dluma_log_pair(
            "INFO",
            "custom_main_menu_data_left_mounted_until_process_exit",
            g_dluma_custom_main_menu_data.path);
    if (assets_pc_live)
        dluma_log_pair(
            "INFO",
            "custom_main_menu_assets_pc_left_mounted_until_process_exit",
            g_dluma_custom_main_menu_assets_pc.path);
    if (overlay_live)
        dluma_log_pair(
            "INFO",
            "custom_main_menu_overlay_left_mounted_until_process_exit",
            g_dluma_custom_main_menu_overlay.path);
    dluma_custom_main_menu_source_state_unlock();
    /*
     * These sources are deliberately process-lifetime. Preserve their exact
     * owner records, paths, source objects, and retained pack identity handle
     * so a same-process shutdown/reinitialization cannot remount a duplicate
     * source or turn an earlier registration failure into success.
     */
}

int dluma_content_pack_snapshot(char* path_out, unsigned int path_capacity)
{
    if (path_out && path_capacity)
        dluma_str_copy(path_out, path_capacity, g_dluma_pack_path);
    return g_dluma_pack_mounted && g_dluma_pack_path[0];
}

int dluma_content_pack_mount_snapshot(
    DlumaContentPackMountSnapshot* snapshot_out)
{
    DlumaContentPackMountSnapshot before;
    DlumaContentPackMountSnapshot after;
    if (!snapshot_out)
        return 0;
    memset(snapshot_out, 0, sizeof(*snapshot_out));
    before = g_dluma_pack_mount_snapshot;
    if (!before.mounted || !before.mount_generation)
        return 0;
    after = g_dluma_pack_mount_snapshot;
    if (!after.mounted ||
        before.mount_generation != after.mount_generation ||
        before.source_identity != after.source_identity ||
        before.exact_source != after.exact_source ||
        before.exact_link != after.exact_link ||
        before.source_list_ordinal != after.source_list_ordinal)
        return 0;
    *snapshot_out = after;
    return 1;
}

int dluma_content_pack_bind_source_identity(
    ULONGLONG exact_source, ULONGLONG exact_link,
    DWORD source_list_ordinal, ULONGLONG source_identity)
{
    int result = 0;
    if (!exact_source || !exact_link || !source_list_ordinal ||
        !source_identity ||
        dluma_atomic_compare_exchange(
            &g_dluma_pack_source_bind_lock, 1, 0) != 0)
        return 0;
    if (g_dluma_pack_mount_snapshot.mounted &&
        g_dluma_pack_mount_snapshot.metadata_complete) {
        if (!g_dluma_pack_mount_snapshot.exact_source_bound) {
            if (!g_dluma_pack_mount_snapshot.exact_source ||
                g_dluma_pack_mount_snapshot.exact_source ==
                    exact_source) {
                g_dluma_pack_mount_snapshot.exact_source = exact_source;
                g_dluma_pack_mount_snapshot.exact_link = exact_link;
                g_dluma_pack_mount_snapshot.source_list_ordinal =
                    source_list_ordinal;
                g_dluma_pack_mount_snapshot.source_identity =
                    source_identity;
                g_dluma_pack_mount_snapshot.exact_source_bound = 1;
                result = 1;
            }
        } else {
            result =
                g_dluma_pack_mount_snapshot.exact_source == exact_source &&
                g_dluma_pack_mount_snapshot.exact_link == exact_link;
            if (result) {
                /* Registry insertion can change the list ordinal while the
                 * same process-lifetime CFSSource and intrusive link remain
                 * mounted. Permit that exact object to refresh its joined
                 * ordinal/identity; never transfer the binding to a
                 * different object. */
                g_dluma_pack_mount_snapshot.source_list_ordinal =
                    source_list_ordinal;
                g_dluma_pack_mount_snapshot.source_identity =
                    source_identity;
            }
        }
    }
    dluma_atomic_exchange(&g_dluma_pack_source_bind_lock, 0);
    return result;
}

int dluma_workshop_preview_cache_source_snapshot(
    char* path_out, unsigned int path_capacity)
{
    if (path_out && path_capacity)
        dluma_str_copy(
            path_out, path_capacity,
            g_dluma_workshop_preview_cache_source_path);
    return g_dluma_workshop_preview_cache_source_mounted &&
           g_dluma_workshop_preview_cache_source_path[0];
}

int dluma_developer_mode_source_snapshot(
    char* path_out, unsigned int path_capacity)
{
    if (path_out && path_capacity)
        dluma_str_copy(
            path_out, path_capacity,
            g_dluma_developer_mode_source_path);
    return g_dluma_developer_mode_source_mounted &&
           g_dluma_developer_mode_source_path[0];
}

int dluma_custom_main_menu_source_snapshot_for_owner(
    const char* owner_id,
    char* path_out, unsigned int path_capacity,
    char* fingerprint_out, unsigned int fingerprint_capacity)
{
    const DlumaCustomMainMenuDirectorySource* state;
    int result;
    if (path_out && path_capacity)
        path_out[0] = 0;
    if (fingerprint_out && fingerprint_capacity)
        fingerprint_out[0] = 0;
    dluma_custom_main_menu_source_state_lock();
    state = dluma_custom_main_menu_state_for_owner(owner_id);
    if (!state) {
        dluma_custom_main_menu_source_state_unlock();
        return 0;
    }
    if (path_out && path_capacity)
        dluma_str_copy(
            path_out, path_capacity,
            state->path);
    if (fingerprint_out && fingerprint_capacity)
        dluma_str_copy(
            fingerprint_out, fingerprint_capacity,
            state->fingerprint);
    result = state->path[0] && state->fingerprint[0];
    dluma_custom_main_menu_source_state_unlock();
    return result;
}
