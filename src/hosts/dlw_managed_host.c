#include "dlw_managed_host.h"
#include "../dlw_atomic_ini.h"

#include "../core/dlw_api_runtime.h"
#include "../core/dlw_runtime_tree.h"
#include "../game/dlw_game_backend.h"
#include "../generated/dluma_api_catalog.generated.h"
#include "../dlw_native_gameplay.h"
#include "../dlw_native_mod_config.h"
#include "../dlw_path_security.h"
#include "../dlw_sha256_internal.h"

#include <stdint.h>
#include <string.h>

#define DLUMA_MANAGED_ABI_VERSION 0x00010000U
#define DLUMA_MANAGED_STRUCT_VERSION 1U
#define DLUMA_MANAGED_MAX_PROJECTS (DLUMA_MAX_ROOTS * 32U)
#define DLUMA_MANAGED_MAX_SCAN_DIRECTORIES 4096U
#define DLUMA_MANAGED_MAX_SCAN_DEPTH 6U
#define DLUMA_MANAGED_MAX_ARTIFACTS 256U
#define DLUMA_MANAGED_MAX_ARTIFACT_BYTES (64U * 1024U * 1024U)
#define DLUMA_MANAGED_MAX_TOTAL_BYTES (256ULL * 1024ULL * 1024ULL)
#define DLUMA_MANAGED_MAX_EVENTS 256U
#define DLUMA_MANAGED_RUNTIME_VERSION L"10.0.13"
#define DLUMA_MANAGED_CRASH_CONTEXT_CAPACITY (32U * 1024U)

#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100UL
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800UL
#endif

typedef struct DlumaManagedStringView {
    const char* data;
    uint64_t size;
} DlumaManagedStringView;

typedef struct DlumaManagedBridgeV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t abi_version;
    uint32_t reserved;
    void* context;
    int (__cdecl *invoke)(
        void* context,
        uint32_t operation_id,
        void* request,
        uint32_t request_size,
        void* result,
        uint32_t result_size);
} DlumaManagedBridgeV1;

typedef struct DlumaManagedArtifactV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t kind;
    uint32_t reserved;
    DlumaManagedStringView relative_path;
    const unsigned char* bytes;
    uint64_t size;
} DlumaManagedArtifactV1;

typedef struct DlumaManagedModLoadV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    uint64_t owner_id;
    uint32_t scope;
    uint32_t map_generation;
    DlumaManagedStringView mod_id;
    DlumaManagedStringView entry_type;
    DlumaManagedStringView entry_path;
    DlumaManagedBridgeV1* bridge;
    DlumaManagedArtifactV1* artifacts;
    uint32_t artifact_count;
    uint32_t reserved;
} DlumaManagedModLoadV1;

typedef struct DlumaManagedActivationV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t scope;
    uint32_t map_generation;
    DlumaManagedStringView project;
    DlumaManagedStringView map;
} DlumaManagedActivationV1;

typedef struct DlumaManagedEventV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t kind;
    uint32_t map_generation;
    uint64_t callback_id;
    uint64_t token;
    double number;
    int32_t state;
    uint32_t reserved;
    DlumaManagedStringView name;
    DlumaManagedStringView project;
    DlumaManagedStringView map;
} DlumaManagedEventV1;

typedef struct DlumaManagedBootstrapApiV1 {
    uint32_t struct_size;
    uint32_t struct_version;
    uint32_t abi_version;
    uint32_t reserved;
    int (__cdecl *load)(
        DlumaManagedModLoadV1* load,
        uint64_t* output_handle,
        char* diagnostic,
        uint32_t diagnostic_capacity);
    int (__cdecl *activate)(
        uint64_t handle,
        DlumaManagedActivationV1* activation,
        char* diagnostic,
        uint32_t diagnostic_capacity);
    int (__cdecl *deactivate)(
        uint64_t handle,
        uint32_t reason,
        char* diagnostic,
        uint32_t diagnostic_capacity);
    int (__cdecl *dispatch)(
        uint64_t handle,
        DlumaManagedEventV1* event_value,
        char* diagnostic,
        uint32_t diagnostic_capacity);
    int (__cdecl *shutdown)(
        uint64_t handle,
        uint32_t reason,
        char* diagnostic,
        uint32_t diagnostic_capacity);
} DlumaManagedBootstrapApiV1;

typedef int (__cdecl *DlumaManagedGetBootstrapApiFn)(
    uint32_t requested_abi_version,
    void* output,
    uint32_t output_size,
    char* diagnostic,
    uint32_t diagnostic_capacity);

typedef void* DlumaHostFxrHandle;

typedef struct DlumaHostFxrInitializeParameters {
    size_t size;
    const WCHAR* host_path;
    const WCHAR* dotnet_root;
} DlumaHostFxrInitializeParameters;

typedef int32_t (__cdecl *DlumaHostFxrInitializeFn)(
    const WCHAR* runtime_config_path,
    const DlumaHostFxrInitializeParameters* parameters,
    DlumaHostFxrHandle* host_context_handle);
typedef int32_t (__cdecl *DlumaHostFxrGetDelegateFn)(
    DlumaHostFxrHandle host_context_handle,
    int delegate_type,
    void** delegate_out);
typedef int32_t (__cdecl *DlumaHostFxrCloseFn)(
    DlumaHostFxrHandle host_context_handle);
typedef int (__stdcall *DlumaLoadAssemblyAndGetFunctionPointerFn)(
    const WCHAR* assembly_path,
    const WCHAR* type_name,
    const WCHAR* method_name,
    const WCHAR* delegate_type_name,
    void* reserved,
    void** delegate_out);

typedef enum DlumaManagedLifecyclePhase {
    DLUMA_MANAGED_PHASE_IDLE = 0,
    DLUMA_MANAGED_PHASE_LOADING,
    DLUMA_MANAGED_PHASE_READY,
    DLUMA_MANAGED_PHASE_CALLBACK,
    DLUMA_MANAGED_PHASE_FAILED,
    DLUMA_MANAGED_PHASE_RETAINED
} DlumaManagedLifecyclePhase;

typedef struct DlumaManagedModEntry {
    DlumaManagedModSnapshot snapshot;
    DlumaModPackageSnapshot package;
    DlumaModOwner api_owner;
    DlumaCapabilityRegistry api_capabilities;
    DlumaApiBackendV1 api_backend;
    DlumaApiRuntime api_runtime;
    DlumaGameHostContext api_host;
    DlumaManagedBridgeV1 bridge;
    uint64_t managed_handle;
    uint32_t lifecycle_phase;
    uint32_t lifecycle_epoch;
    uint32_t cleanup_pending;
    uint32_t scope_exit_revoked;
    uint32_t global_map_exit_suspended;
} DlumaManagedModEntry;

typedef struct DlumaManagedQueuedEvent {
    uint64_t owner_id;
    uint64_t callback_id;
    uint64_t token;
    uint32_t kind;
    uint32_t map_generation;
    int32_t state;
    double number;
    char name[96];
} DlumaManagedQueuedEvent;

typedef struct DlumaManagedRuntime {
    HMODULE hostfxr_module;
    HANDLE hostfxr_file;
    HANDLE bootstrap_file;
    DlumaManagedBootstrapApiV1 api;
    WCHAR runtime_root[DLUMA_MAX_WPATH];
    WCHAR dotnet_root[DLUMA_MAX_WPATH];
    WCHAR hostfxr_path[DLUMA_MAX_WPATH];
    WCHAR bootstrap_path[DLUMA_MAX_WPATH];
    WCHAR runtime_config_path[DLUMA_MAX_WPATH];
    char diagnostic[DLUMA_MANAGED_DIAGNOSTIC_CAPACITY];
    unsigned int initialized;
    unsigned int attempted;
} DlumaManagedRuntime;

typedef struct DlumaManagedRegistry {
    DlumaManagedModEntry entries[DLUMA_MAX_MANAGED_MODS];
    DlumaManagedQueuedEvent events[DLUMA_MANAGED_MAX_EVENTS];
    unsigned int count;
    unsigned int total_found;
    unsigned int event_head;
    unsigned int event_count;
    unsigned int next_lifecycle_epoch;
    ULONGLONG revision;
    volatile LONG lock;
    volatile LONG event_lock;
    volatile LONG pump_active;
    WCHAR ini_path[DLUMA_MAX_WPATH];
    WCHAR current_project_root[DLUMA_MAX_WPATH];
    char current_project[128];
    char current_map[128];
    unsigned int current_map_generation;
    DlumaManagedRuntime runtime;
} DlumaManagedRegistry;

static DlumaManagedRegistry g_dluma_managed;
static volatile LONG g_dluma_managed_crash_context_sequence;
static volatile LONG g_dluma_managed_project_exit_gate;
static volatile LONG g_dluma_managed_map_exit_frozen;
static unsigned int g_dluma_managed_map_exit_generation;
static char g_dluma_managed_crash_context[
    DLUMA_MANAGED_CRASH_CONTEXT_CAPACITY];

static void dluma_managed_dispatch_event(
    DlumaManagedModEntry* entry,
    const DlumaManagedQueuedEvent* queued);
static void dluma_managed_host_event_sink(
    void* user,
    uint32_t kind,
    uint64_t callback_id,
    uint64_t token,
    double number,
    const char* name);

static void dluma_managed_lock_value(volatile LONG* lock)
{
    while (dluma_atomic_compare_exchange(lock, 1, 0) != 0)
        Sleep(1);
}

static void dluma_managed_unlock_value(volatile LONG* lock)
{
    dluma_atomic_exchange(lock, 0);
}

static void dluma_managed_lock(void)
{
    dluma_managed_lock_value(&g_dluma_managed.lock);
}

static void dluma_managed_publish_crash_context_locked(void)
{
    unsigned int index;
    char number[32];
    dluma_atomic_increment(&g_dluma_managed_crash_context_sequence);
    g_dluma_managed_crash_context[0] = 0;
    dluma_str_append(
        g_dluma_managed_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context),
        "[CRASH] [INFO] managed_registry count=");
    dluma_u64_to_text(
        g_dluma_managed.count, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(
        g_dluma_managed_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), number);
    dluma_str_append(
        g_dluma_managed_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), " revision=");
    dluma_u64_to_text(
        g_dluma_managed.revision, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(
        g_dluma_managed_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), number);
    dluma_str_append(
        g_dluma_managed_crash_context,
        DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), "\r\n");
    for (index = 0; index < g_dluma_managed.count; ++index) {
        const DlumaManagedModSnapshot* item =
            &g_dluma_managed.entries[index].snapshot;
        if (!item->loaded && !item->active &&
            !item->runtime_error[0])
            continue;
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context),
            "[CRASH] [INFO] managed_mod id=");
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), item->id);
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), " version=");
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), item->version);
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), " state=");
        dluma_u64_to_text(item->state, number, DLUMA_ARRAY_COUNT(number));
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), number);
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), " loaded=");
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context),
            item->loaded ? "1" : "0");
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), " active=");
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context),
            item->active ? "1" : "0");
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), " source=");
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context),
            item->source_identity);
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), " path=");
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context),
            item->canonical_relative_path);
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), " sha256=");
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context),
            item->entry_sha256);
        if (item->runtime_error[0]) {
            dluma_str_append(
                g_dluma_managed_crash_context,
                DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), " error=");
            dluma_str_append(
                g_dluma_managed_crash_context,
                DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context),
                item->runtime_error);
        }
        dluma_str_append(
            g_dluma_managed_crash_context,
            DLUMA_ARRAY_COUNT(g_dluma_managed_crash_context), "\r\n");
    }
    dluma_atomic_increment(&g_dluma_managed_crash_context_sequence);
}

static void dluma_managed_unlock(void)
{
    dluma_managed_publish_crash_context_locked();
    dluma_managed_unlock_value(&g_dluma_managed.lock);
}

static DlumaManagedStringView dluma_managed_view(const char* value)
{
    DlumaManagedStringView result;
    result.data = value;
    result.size = value ? (uint64_t)strlen(value) : 0U;
    return result;
}

static int dluma_managed_is_dot_directory(const WCHAR* name)
{
    return name &&
        ((name[0] == L'.' && !name[1]) ||
         (name[0] == L'.' && name[1] == L'.' && !name[2]));
}

static int dluma_managed_join_checked(
    WCHAR* output,
    unsigned int capacity,
    const WCHAR* left,
    const WCHAR* right)
{
    size_t left_length;
    size_t right_length;
    if (!output || !capacity || !left || !right)
        return 0;
    left_length = wcslen(left);
    right_length = wcslen(right);
    if (!left_length || !right_length ||
        left_length + right_length + 2U > capacity) {
        output[0] = 0;
        return 0;
    }
    dluma_path_join(output, capacity, left, right);
    return output[0] != 0 &&
        wcslen(output) < (size_t)capacity;
}

static int dluma_managed_path_under(
    const WCHAR* child,
    const WCHAR* parent)
{
    size_t parent_length;
    size_t index;
    if (!child || !parent) return 0;
    parent_length = wcslen(parent);
    if (!parent_length)
        return 0;
    for (index = 0; index < parent_length; ++index) {
        if (!child[index] ||
            dluma_wascii_lower(child[index]) !=
                dluma_wascii_lower(parent[index]))
            return 0;
    }
    return child[parent_length] == 0 ||
        child[parent_length] == L'\\' ||
        child[parent_length] == L'/';
}

static int dluma_managed_open_directory_final(
    const WCHAR* path,
    WCHAR* final_path,
    unsigned int final_capacity)
{
    HANDLE handle;
    BY_HANDLE_FILE_INFORMATION information;
    handle = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        0);
    if (handle == INVALID_HANDLE_VALUE)
        return 0;
    memset(&information, 0, sizeof(information));
    if (!GetFileInformationByHandle(handle, &information) ||
        !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !dluma_get_final_path_checked(
            handle, final_path, final_capacity)) {
        CloseHandle(handle);
        return 0;
    }
    CloseHandle(handle);
    return 1;
}

static HANDLE dluma_managed_open_regular_final(
    const WCHAR* path,
    WCHAR* final_path,
    unsigned int final_capacity)
{
    HANDLE handle;
    BY_HANDLE_FILE_INFORMATION information;
    handle = CreateFileW(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        0,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
        0);
    if (handle == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    memset(&information, 0, sizeof(information));
    if (!GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY |
             FILE_ATTRIBUTE_REPARSE_POINT)) ||
        !dluma_get_final_path_checked(
            handle, final_path, final_capacity)) {
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

static int dluma_managed_project_seen(
    WCHAR projects[][DLUMA_MAX_WPATH],
    unsigned int count,
    const WCHAR* candidate)
{
    unsigned int index;
    for (index = 0; index < count; ++index) {
        if (dluma_wstricmp(projects[index], candidate) == 0)
            return 1;
    }
    return 0;
}

static int dluma_managed_package_layout(
    const WCHAR* project_root,
    const DlumaModPackageSnapshot* package,
    char* canonical_relative_path,
    unsigned int canonical_capacity)
{
    WCHAR csharp_root[DLUMA_MAX_WPATH];
    WCHAR final_project[DLUMA_MAX_WPATH];
    WCHAR final_csharp[DLUMA_MAX_WPATH];
    const WCHAR* relative;
    char relative_utf8[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    char expected[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    unsigned int index;
    if (!project_root || !package || !package->valid ||
        !canonical_relative_path || !canonical_capacity ||
        !dluma_managed_open_directory_final(
            project_root, final_project,
            DLUMA_ARRAY_COUNT(final_project)) ||
        !dluma_managed_join_checked(
            csharp_root, DLUMA_ARRAY_COUNT(csharp_root),
            project_root, L"mods\\csharp") ||
        !dluma_managed_open_directory_final(
            csharp_root, final_csharp,
            DLUMA_ARRAY_COUNT(final_csharp)) ||
        !dluma_managed_path_under(final_csharp, final_project) ||
        !dluma_managed_path_under(
            package->package_root, final_csharp))
        return 0;

    relative = package->package_root + wcslen(final_csharp);
    while (*relative == L'\\' || *relative == L'/')
        ++relative;
    if (!relative[0] ||
        !dluma_wide_to_utf8(
            relative, relative_utf8,
            DLUMA_ARRAY_COUNT(relative_utf8)))
        return 0;
    for (index = 0; relative_utf8[index]; ++index) {
        if (relative_utf8[index] == '\\')
            relative_utf8[index] = '/';
    }

    expected[0] = 0;
    if (package->manifest.scope == DLUMA_SCOPE_GLOBAL) {
        dluma_str_append(expected, DLUMA_ARRAY_COUNT(expected), "global/");
    } else if (package->manifest.scope == DLUMA_SCOPE_PROJECT) {
        dluma_str_append(expected, DLUMA_ARRAY_COUNT(expected), "project/");
    } else if (package->manifest.scope == DLUMA_SCOPE_MAP) {
        dluma_str_append(expected, DLUMA_ARRAY_COUNT(expected), "maps/");
        dluma_str_append(
            expected, DLUMA_ARRAY_COUNT(expected),
            package->manifest.map);
        dluma_str_append(expected, DLUMA_ARRAY_COUNT(expected), "/");
    } else {
        return 0;
    }
    dluma_str_append(
        expected, DLUMA_ARRAY_COUNT(expected),
        package->manifest.id);
    if (dluma_stricmp(expected, relative_utf8) != 0)
        return 0;

    canonical_relative_path[0] = 0;
    dluma_str_append(
        canonical_relative_path, canonical_capacity,
        "mods/csharp/");
    dluma_str_append(
        canonical_relative_path, canonical_capacity,
        relative_utf8);
    return canonical_relative_path[0] != 0;
}

static void dluma_managed_ini_path(void)
{
    WCHAR runtime[DLUMA_MAX_WPATH];
    if (g_dluma_managed.ini_path[0])
        return;
    dluma_wstr_copy(
        runtime, DLUMA_ARRAY_COUNT(runtime),
        g_dluma.marker_root);
    dluma_path_parent(runtime);
    dluma_path_join(
        g_dluma_managed.ini_path,
        DLUMA_ARRAY_COUNT(g_dluma_managed.ini_path),
        runtime,
        L"managed_mods.ini");
}

static void dluma_managed_section(
    const DlumaManagedModSnapshot* item,
    WCHAR* output,
    unsigned int capacity)
{
    char section[96];
    section[0] = 0;
    dluma_str_append(
        section, DLUMA_ARRAY_COUNT(section), "managed:");
    dluma_str_append(
        section, DLUMA_ARRAY_COUNT(section),
        item->registry_key);
    if (!dluma_utf8_to_wide(section, output, capacity))
        output[0] = 0;
}

static void dluma_managed_fingerprint(
    DlumaManagedModSnapshot* item)
{
    const char* fields[10];
    fields[0] = item->id;
    fields[1] = item->source_identity;
    fields[2] = item->canonical_relative_path;
    fields[3] = item->manifest_sha256;
    fields[4] = item->lock_sha256;
    fields[5] = item->entry_sha256;
    fields[6] = DLUMA_API_CATALOG_SHA256;
    fields[7] = DLUMA_API_VERSION;
    fields[8] = "dotnet-10.0.13-x64";
    fields[9] = item->config_schema_fingerprint[0]
        ? item->config_schema_fingerprint
        : "<no configuration>";
    dluma_sha256_fields_hex(
        "DLW_MANAGED_MOD_APPROVAL_V1",
        fields,
        0,
        DLUMA_ARRAY_COUNT(fields),
        item->approval_fingerprint);
}

static void dluma_managed_config_identity(
    const DlumaManagedModSnapshot* item,
    DlumaModConfigIdentity* output)
{
    if (!output) return;
    memset(output, 0, sizeof(*output));
    if (!item) return;
    dluma_str_copy(
        output->registry_key,
        DLUMA_ARRAY_COUNT(output->registry_key),
        item->registry_key);
    dluma_str_copy(
        output->storage_key,
        DLUMA_ARRAY_COUNT(output->storage_key),
        item->registry_key);
    dluma_str_copy(
        output->metadata_fingerprint,
        DLUMA_ARRAY_COUNT(
            output->metadata_fingerprint),
        item->approval_fingerprint);
    dluma_str_copy(
        output->config_schema_fingerprint,
        DLUMA_ARRAY_COUNT(
            output->config_schema_fingerprint),
        item->config_schema_fingerprint);
    output->config_available =
        item->config_available ? 1U : 0U;
    output->owner_kind =
        DLUMA_MOD_CONFIG_OWNER_CSHARP;
}

static void dluma_managed_prune_config_registry(void)
{
    DlumaModConfigIdentity* identities;
    unsigned int index;
    identities = (DlumaModConfigIdentity*)dluma_alloc(
        (SIZE_T)g_dluma_managed.count *
        sizeof(*identities));
    if (!identities && g_dluma_managed.count)
        return;
    for (index = 0;
         index < g_dluma_managed.count;
         ++index)
        dluma_managed_config_identity(
            &g_dluma_managed.entries[index].snapshot,
            &identities[index]);
    dluma_mod_config_prune_owner(
        DLUMA_MOD_CONFIG_OWNER_CSHARP,
        identities,
        g_dluma_managed.count);
    dluma_free(identities);
}

static void dluma_managed_apply_persistence(
    DlumaManagedModSnapshot* item)
{
    WCHAR section[128];
    WCHAR fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR enabled[16];
    char fingerprint_utf8[DLUMA_SHA256_HEX_CAPACITY];
    char enabled_utf8[16];
    dluma_managed_ini_path();
    dluma_managed_section(
        item, section, DLUMA_ARRAY_COUNT(section));
    fingerprint[0] = 0;
    enabled[0] = 0;
    GetPrivateProfileStringW(
        section,
        L"approval_fingerprint",
        L"",
        fingerprint,
        DLUMA_ARRAY_COUNT(fingerprint),
        g_dluma_managed.ini_path);
    GetPrivateProfileStringW(
        section,
        L"enabled_next_launch",
        L"0",
        enabled,
        DLUMA_ARRAY_COUNT(enabled),
        g_dluma_managed.ini_path);
    if (!dluma_wide_to_utf8(
            fingerprint,
            fingerprint_utf8,
            DLUMA_ARRAY_COUNT(fingerprint_utf8)))
        fingerprint_utf8[0] = 0;
    if (!dluma_wide_to_utf8(
            enabled,
            enabled_utf8,
            DLUMA_ARRAY_COUNT(enabled_utf8)))
        enabled_utf8[0] = 0;
    item->approval_valid =
        fingerprint_utf8[0] &&
        dluma_stricmp(
            fingerprint_utf8,
            item->approval_fingerprint) == 0;
    item->enabled_requested =
        item->approval_valid &&
        dluma_parse_u32(enabled_utf8, 0) == 1U;
    if (item->duplicate_id)
        item->state = DLUMA_MANAGED_STATE_CONFLICT;
    else if (item->runtime_error[0])
        item->state = DLUMA_MANAGED_STATE_INCOMPATIBLE;
    else if (fingerprint_utf8[0] &&
             !item->approval_valid)
        item->state =
            DLUMA_MANAGED_STATE_CHANGED_REVIEW_REQUIRED;
    else if (item->enabled_requested)
        item->state =
            DLUMA_MANAGED_STATE_APPROVED_NEXT_LAUNCH;
    else
        item->state =
            DLUMA_MANAGED_STATE_DISCOVERED_DISABLED;
}

static int dluma_managed_compare_entry(
    const DlumaManagedModEntry* first,
    const DlumaManagedModEntry* second)
{
    int compare;
    if (first->snapshot.load_priority !=
        second->snapshot.load_priority)
        return first->snapshot.load_priority <
               second->snapshot.load_priority
            ? -1
            : 1;
    compare = dluma_stricmp(
        first->snapshot.source_identity,
        second->snapshot.source_identity);
    if (compare) return compare;
    compare = dluma_stricmp(
        first->snapshot.canonical_relative_path,
        second->snapshot.canonical_relative_path);
    if (compare) return compare;
    return dluma_stricmp(
        first->snapshot.registry_key,
        second->snapshot.registry_key);
}

static void dluma_managed_sort_entries(void)
{
    unsigned int index;
    for (index = 1; index < g_dluma_managed.count; ++index) {
        DlumaManagedModEntry value =
            g_dluma_managed.entries[index];
        unsigned int target = index;
        while (target &&
               dluma_managed_compare_entry(
                   &g_dluma_managed.entries[target - 1U],
                   &value) > 0) {
            g_dluma_managed.entries[target] =
                g_dluma_managed.entries[target - 1U];
            --target;
        }
        g_dluma_managed.entries[target] = value;
    }
    for (index = 0; index < g_dluma_managed.count; ++index)
        g_dluma_managed.entries[index]
            .snapshot.resolved_load_index = index;
}

static void dluma_managed_mark_conflicts(void)
{
    unsigned int first;
    unsigned int second;
    unsigned char processed[DLUMA_MAX_MANAGED_MODS];
    memset(processed, 0, sizeof(processed));
    for (first = 0; first < g_dluma_managed.count; ++first) {
        for (second = first + 1U;
             second < g_dluma_managed.count;
             ++second) {
            if (dluma_stricmp(
                    g_dluma_managed.entries[first].snapshot.id,
                    g_dluma_managed.entries[second].snapshot.id) == 0 &&
                dluma_stricmp(
                    g_dluma_managed.entries[first].snapshot.source_identity,
                    g_dluma_managed.entries[second].snapshot.source_identity) == 0) {
                g_dluma_managed.entries[first]
                    .snapshot.duplicate_id = 1U;
                g_dluma_managed.entries[second]
                    .snapshot.duplicate_id = 1U;
            }
        }
    }
    for (first = 0; first < g_dluma_managed.count; ++first)
        dluma_managed_apply_persistence(
            &g_dluma_managed.entries[first].snapshot);
    for (first = 0; first < g_dluma_managed.count; ++first) {
        DlumaManagedModSnapshot* first_item =
            &g_dluma_managed.entries[first].snapshot;
        char selected_source[512];
        int explicit_selection = 0;
        int selected_index = -1;
        int local_index = -1;
        int any_index = -1;
        int any_local_index = -1;
        int explicit_failed_index = -1;
        if (processed[first]) continue;
        selected_source[0] = 0;
        dluma_mod_source_selection_read(
            "csharp", first_item->id, selected_source,
            DLUMA_ARRAY_COUNT(selected_source), &explicit_selection);
        /* A copy that reported an error is preferred last but stays
         * resolvable, so a declared ID whose copies all failed still offers a
         * source the player can choose. Loading remains gated on approval. */
        for (second = first; second < g_dluma_managed.count; ++second) {
            DlumaManagedModSnapshot* item =
                &g_dluma_managed.entries[second].snapshot;
            if (dluma_stricmp(first_item->id, item->id) != 0) continue;
            processed[second] = 1;
            if (item->duplicate_id) continue;
            if (explicit_selection &&
                dluma_stricmp(item->source_identity,
                            selected_source) == 0) {
                if (!item->runtime_error[0])
                    selected_index = (int)second;
                else if (explicit_failed_index < 0)
                    explicit_failed_index = (int)second;
            }
            if (!item->source_is_subscribed &&
                (any_local_index < 0 ||
                 dluma_wstricmp(
                     item->project_root,
                     g_dluma_managed.entries[any_local_index]
                         .snapshot.project_root) < 0))
                any_local_index = (int)second;
            if (any_index < 0 ||
                dluma_wstricmp(
                    item->project_root,
                    g_dluma_managed.entries[any_index]
                        .snapshot.project_root) < 0)
                any_index = (int)second;
            if (item->runtime_error[0]) continue;
            if (!item->source_is_subscribed &&
                (local_index < 0 ||
                 dluma_wstricmp(
                     item->project_root,
                     g_dluma_managed.entries[local_index]
                         .snapshot.project_root) < 0))
                local_index = (int)second;
            if (!explicit_selection &&
                (selected_index < 0 ||
                 dluma_wstricmp(
                     item->project_root,
                     g_dluma_managed.entries[selected_index]
                         .snapshot.project_root) < 0))
                selected_index = (int)second;
        }
        if (!explicit_selection && local_index >= 0)
            selected_index = local_index;
        if (explicit_selection) {
            if (selected_index < 0)
                selected_index = explicit_failed_index;
        } else if (selected_index < 0) {
            selected_index =
                any_local_index >= 0 ? any_local_index : any_index;
        }
        if (!explicit_selection && selected_index >= 0)
            dluma_mod_source_selection_write(
                "csharp", first_item->id,
                g_dluma_managed.entries[selected_index]
                    .snapshot.source_identity);
        for (second = first; second < g_dluma_managed.count; ++second) {
            DlumaManagedModSnapshot* item =
                &g_dluma_managed.entries[second].snapshot;
            if (dluma_stricmp(first_item->id, item->id) != 0) continue;
            item->source_selected =
                (unsigned int)((int)second == selected_index ? 1 : 0);
            item->source_selection_missing =
                (unsigned int)(explicit_selection && selected_index < 0
                    ? 1 : 0);
        }
    }
}

static void dluma_managed_add_package(
    const WCHAR* project_root,
    const WCHAR* manifest_path)
{
    DlumaModPackageSnapshot package;
    DlumaModArtifactSnapshot config_artifact;
    DlumaLuaConfigSchema* config_schema = 0;
    DlumaModConfigIdentity config_identity;
    DlumaManagedModEntry* entry;
    DlumaManagedModSnapshot* item;
    WCHAR package_root[DLUMA_MAX_WPATH];
    DlumaProjectSourceDescription source;
    const char* key_fields[2];
    if (g_dluma_managed.count >= DLUMA_MAX_MANAGED_MODS) {
        dluma_log_line(
            "WARN",
            "managed_mod_registry_truncated");
        return;
    }
    memset(&package, 0, sizeof(package));
    memset(
        &config_artifact, 0,
        sizeof(config_artifact));
    package.struct_size = (unsigned int)sizeof(package);
    config_artifact.struct_size =
        (unsigned int)sizeof(config_artifact);
    dluma_wstr_copy(
        package_root,
        DLUMA_ARRAY_COUNT(package_root),
        manifest_path);
    dluma_path_parent(package_root);
    if (dluma_mod_package_discover(
            package_root, &package) != DLUMA_STATUS_OK ||
        !package.valid ||
        package.manifest.language !=
            DLUMA_MOD_LANGUAGE_CSHARP)
        return;

    entry = &g_dluma_managed.entries[
        g_dluma_managed.count];
    memset(entry, 0, sizeof(*entry));
    entry->package = package;
    entry->lifecycle_phase =
        DLUMA_MANAGED_PHASE_IDLE;
    item = &entry->snapshot;
    dluma_str_copy(
        item->id, DLUMA_ARRAY_COUNT(item->id),
        package.manifest.id);
    dluma_str_copy(
        item->name, DLUMA_ARRAY_COUNT(item->name),
        package.manifest.name);
    dluma_str_copy(
        item->description, DLUMA_ARRAY_COUNT(item->description),
        package.manifest.description);
    dluma_str_copy(
        item->version,
        DLUMA_ARRAY_COUNT(item->version),
        package.manifest.version);
    dluma_str_copy(
        item->author,
        DLUMA_ARRAY_COUNT(item->author),
        package.manifest.author);
    item->load_priority =
        package.manifest.load_priority;
    item->access_count =
        package.manifest.access_count;
    item->scope = package.manifest.scope;
    dluma_str_copy(
        item->api_version,
        DLUMA_ARRAY_COUNT(item->api_version),
        DLUMA_API_VERSION);
    dluma_str_copy(
        item->manifest_sha256,
        DLUMA_ARRAY_COUNT(item->manifest_sha256),
        package.manifest_sha256);
    dluma_str_copy(
        item->lock_sha256,
        DLUMA_ARRAY_COUNT(item->lock_sha256),
        package.lock_sha256);
    dluma_str_copy(
        item->entry_sha256,
        DLUMA_ARRAY_COUNT(item->entry_sha256),
        package.entry_sha256);
    dluma_wstr_copy(
        item->package_root,
        DLUMA_ARRAY_COUNT(item->package_root),
        package.package_root);
    dluma_wstr_copy(
        item->project_root,
        DLUMA_ARRAY_COUNT(item->project_root),
        project_root);
    if (!dluma_classify_project_source(project_root, &source)) return;
    dluma_str_copy(item->source_name, DLUMA_ARRAY_COUNT(item->source_name),
                 source.project_name);
    item->source_is_workshop = source.is_subscribed_workshop;
    item->source_is_subscribed = source.is_subscribed_workshop;
    dluma_str_copy(item->workshop_id, DLUMA_ARRAY_COUNT(item->workshop_id),
                 source.published_id);
    dluma_str_copy(item->source_identity,
                 DLUMA_ARRAY_COUNT(item->source_identity),
                 source.source_identity);
    if (!dluma_managed_package_layout(
            project_root,
            &package,
            item->canonical_relative_path,
            DLUMA_ARRAY_COUNT(
                item->canonical_relative_path))) {
        dluma_str_copy(
            item->runtime_error,
            DLUMA_ARRAY_COUNT(item->runtime_error),
            "manifest scope does not match the canonical C# package path");
    } else if (!package.manifest.entry_type[0]) {
        dluma_str_copy(
            item->runtime_error,
            DLUMA_ARRAY_COUNT(item->runtime_error),
            "C# manifest requires entry_type");
    } else if (!dluma_mod_manifest_expected_suffix(
                   &package.manifest, ".dll")) {
        dluma_str_copy(
            item->runtime_error,
            DLUMA_ARRAY_COUNT(item->runtime_error),
            "C# manifest entry must be a .dll");
    }
    key_fields[0] = item->source_identity;
    key_fields[1] = item->canonical_relative_path;
    dluma_sha256_fields_hex(
        "DLW_MANAGED_MOD_REGISTRY_KEY_V1",
        key_fields,
        0,
        DLUMA_ARRAY_COUNT(key_fields),
        item->registry_key);
    if (package.manifest.configuration_schema[0]) {
        item->config_declared = 1U;
        config_schema =
            (DlumaLuaConfigSchema*)dluma_alloc(
                sizeof(*config_schema));
        if (!config_schema) {
            dluma_str_copy(
                item->config_diagnostic,
                DLUMA_ARRAY_COUNT(
                    item->config_diagnostic),
                "config_schema_allocation_failed");
        } else {
            memset(
                config_schema, 0,
                sizeof(*config_schema));
            if (dluma_mod_package_read_artifact(
                    &package,
                    package.manifest
                        .configuration_schema,
                    DLUMA_MOD_ARTIFACT_CONFIGURATION,
                    DLUMA_NATIVE_MOD_CONFIG_MAX_JSON_BYTES,
                    &config_artifact) !=
                DLUMA_STATUS_OK) {
                dluma_str_copy(
                    item->config_diagnostic,
                    DLUMA_ARRAY_COUNT(
                        item->config_diagnostic),
                    config_artifact.diagnostic[0]
                        ? config_artifact.diagnostic
                        : "locked_config_schema_unavailable");
            } else if (dluma_mod_config_parse_json(
                    (const char*)
                        config_artifact.bytes,
                    config_artifact.size,
                    config_schema,
                    item->config_diagnostic,
                    DLUMA_ARRAY_COUNT(
                        item->config_diagnostic))) {
                item->config_available = 1U;
                item->config_category_count =
                    config_schema->category_count;
                item->config_setting_count =
                    config_schema->setting_count;
                dluma_str_copy(
                    item->config_schema_fingerprint,
                    DLUMA_ARRAY_COUNT(
                        item->
                            config_schema_fingerprint),
                    config_schema->fingerprint);
            }
            dluma_mod_artifact_release(
                &config_artifact);
        }
    }
    dluma_managed_fingerprint(item);
    if (item->config_available) {
        dluma_managed_config_identity(
            item, &config_identity);
        if (!config_schema ||
            !dluma_mod_config_publish(
                &config_identity,
                config_schema)) {
            item->config_available = 0U;
            dluma_str_copy(
                item->config_diagnostic,
                DLUMA_ARRAY_COUNT(
                    item->config_diagnostic),
                config_schema
                    ? "config_registry_publish_failed"
                    : "config_schema_allocation_failed");
        }
    }
    dluma_free(config_schema);
    ++g_dluma_managed.count;
}

static void dluma_managed_scan_directory(
    const WCHAR* project_root,
    const WCHAR* directory,
    unsigned int depth,
    unsigned int* directories_seen,
    unsigned int* truncated)
{
    WCHAR manifest[DLUMA_MAX_WPATH];
    WCHAR pattern[DLUMA_MAX_WPATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    DWORD attributes;
    if (!project_root || !directory ||
        !directories_seen || !truncated ||
        depth > DLUMA_MANAGED_MAX_SCAN_DEPTH ||
        *directories_seen >=
            DLUMA_MANAGED_MAX_SCAN_DIRECTORIES) {
        if (truncated) *truncated = 1U;
        return;
    }
    attributes = GetFileAttributesW(directory);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return;
    ++*directories_seen;
    if (!dluma_managed_join_checked(
            manifest, DLUMA_ARRAY_COUNT(manifest),
            directory, L"mod.dluma.json"))
        return;
    attributes = GetFileAttributesW(manifest);
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        !(attributes &
          (FILE_ATTRIBUTE_DIRECTORY |
           FILE_ATTRIBUTE_REPARSE_POINT))) {
        ++g_dluma_managed.total_found;
        dluma_managed_add_package(
            project_root, manifest);
        return;
    }
    if (!dluma_managed_join_checked(
            pattern, DLUMA_ARRAY_COUNT(pattern),
            directory, L"*")) {
        *truncated = 1U;
        return;
    }
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return;
    do {
        WCHAR child[DLUMA_MAX_WPATH];
        if (!(data.dwFileAttributes &
              FILE_ATTRIBUTE_DIRECTORY) ||
            (data.dwFileAttributes &
             FILE_ATTRIBUTE_REPARSE_POINT) ||
            dluma_managed_is_dot_directory(
                data.cFileName))
            continue;
        if (!dluma_managed_join_checked(
                child, DLUMA_ARRAY_COUNT(child),
                directory, data.cFileName)) {
            *truncated = 1U;
            continue;
        }
        dluma_managed_scan_directory(
            project_root,
            child,
            depth + 1U,
            directories_seen,
            truncated);
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

static void dluma_managed_scan_project(
    const WCHAR* project_root,
    unsigned int* directories_seen,
    unsigned int* truncated)
{
    WCHAR csharp_root[DLUMA_MAX_WPATH];
    if (!dluma_managed_join_checked(
            csharp_root,
            DLUMA_ARRAY_COUNT(csharp_root),
            project_root,
            L"mods\\csharp"))
        return;
    dluma_managed_scan_directory(
        project_root,
        csharp_root,
        0,
        directories_seen,
        truncated);
}

static void dluma_managed_auto_enable_startup(void);
static int g_dluma_managed_initial_discovery_complete;

void dluma_managed_mods_discover(void)
{
    WCHAR (*projects)[DLUMA_MAX_WPATH] = 0;
    unsigned int project_count = 0;
    unsigned int root_index;
    unsigned int index;
    unsigned int directories_seen = 0;
    unsigned int truncated = 0;
    dluma_managed_lock();
    for (index = 0;
         index < g_dluma_managed.count;
         ++index) {
        if (g_dluma_managed.entries[index]
                .lifecycle_phase !=
            DLUMA_MANAGED_PHASE_IDLE) {
            dluma_managed_unlock();
            return;
        }
    }
    memset(
        g_dluma_managed.entries,
        0,
        sizeof(g_dluma_managed.entries));
    g_dluma_managed.count = 0;
    g_dluma_managed.total_found = 0;
    projects = (WCHAR (*)[DLUMA_MAX_WPATH])
        dluma_alloc(
            (SIZE_T)DLUMA_MANAGED_MAX_PROJECTS *
            sizeof(*projects));
    if (!projects) {
        ++g_dluma_managed.revision;
        dluma_managed_unlock();
        dluma_log_line(
            "ERROR",
            "managed_mod_discovery_allocation_failed");
        return;
    }
    for (root_index = 0;
         root_index < g_dluma.workshop_root_count;
         ++root_index) {
        const WCHAR* root =
            g_dluma.workshop_roots[root_index];
        WCHAR pattern[DLUMA_MAX_WPATH];
        WIN32_FIND_DATAW data;
        HANDLE find;
        DWORD attributes;
        if (!root[0]) continue;
        attributes = GetFileAttributesW(root);
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            continue;
        if (project_count <
                DLUMA_MANAGED_MAX_PROJECTS &&
            !dluma_managed_project_seen(
                projects, project_count, root))
            dluma_wstr_copy(
                projects[project_count++],
                DLUMA_MAX_WPATH,
                root);
        if (!dluma_managed_join_checked(
                pattern,
                DLUMA_ARRAY_COUNT(pattern),
                root,
                L"*"))
            continue;
        find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE)
            continue;
        do {
            WCHAR project[DLUMA_MAX_WPATH];
            if (!(data.dwFileAttributes &
                  FILE_ATTRIBUTE_DIRECTORY) ||
                (data.dwFileAttributes &
                 FILE_ATTRIBUTE_REPARSE_POINT) ||
                dluma_managed_is_dot_directory(
                    data.cFileName))
                continue;
            if (!dluma_managed_join_checked(
                    project,
                    DLUMA_ARRAY_COUNT(project),
                    root,
                    data.cFileName))
                continue;
            if (project_count <
                    DLUMA_MANAGED_MAX_PROJECTS &&
                !dluma_managed_project_seen(
                    projects,
                    project_count,
                    project))
                dluma_wstr_copy(
                    projects[project_count++],
                    DLUMA_MAX_WPATH,
                    project);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    for (index = 0; index < project_count; ++index)
        dluma_managed_scan_project(
            projects[index],
            &directories_seen,
            &truncated);
    dluma_managed_sort_entries();
    dluma_managed_mark_conflicts();
    if (!g_dluma_managed_initial_discovery_complete) {
        g_dluma_managed_initial_discovery_complete = 1;
        dluma_managed_auto_enable_startup();
    }
    dluma_managed_prune_config_registry();
    ++g_dluma_managed.revision;
    dluma_managed_unlock();
    dluma_log_u64(
        "INFO",
        "managed_mod_roots_scanned",
        project_count);
    dluma_log_u64(
        "INFO",
        "managed_mod_directories_scanned",
        directories_seen);
    dluma_log_u64(
        truncated ? "WARN" : "INFO",
        "managed_mod_directory_traversal_truncated",
        truncated);
    dluma_log_u64(
        "INFO",
        "managed_mod_candidates_found",
        g_dluma_managed.total_found);
    dluma_free(projects);
}

unsigned int dluma_managed_mods_snapshot(
    DlumaManagedModSnapshot* output,
    unsigned int capacity,
    ULONGLONG* revision_out)
{
    unsigned int count;
    dluma_managed_lock();
    count = g_dluma_managed.count;
    if (revision_out)
        *revision_out = g_dluma_managed.revision;
    if (output && capacity) {
        unsigned int copy_count =
            count < capacity ? count : capacity;
        {
            unsigned int index;
            for (index = 0;
                 index < copy_count;
                 ++index)
                output[index] =
                    g_dluma_managed.entries[index]
                        .snapshot;
        }
    }
    dluma_managed_unlock();
    return count;
}

int dluma_managed_mods_copy_crash_context(
    char* output,
    unsigned int capacity)
{
    unsigned int attempt;
    if (!output || !capacity) return 0;
    output[0] = 0;
    for (attempt = 0; attempt < 2U; ++attempt) {
        LONG before = g_dluma_managed_crash_context_sequence;
        LONG after;
        if (before & 1) continue;
        dluma_str_copy(
            output, capacity, g_dluma_managed_crash_context);
        after = g_dluma_managed_crash_context_sequence;
        if (before == after && !(after & 1))
            return output[0] != 0;
        output[0] = 0;
    }
    return 0;
}

void dluma_managed_mods_make_mutation_request(
    DlumaManagedModMutationRequest* request,
    const DlumaManagedModSnapshot* item,
    ULONGLONG revision)
{
    if (!request) return;
    memset(request, 0, sizeof(*request));
    if (!item) return;
    dluma_str_copy(
        request->registry_key,
        DLUMA_ARRAY_COUNT(request->registry_key),
        item->registry_key);
    dluma_str_copy(
        request->approval_fingerprint,
        DLUMA_ARRAY_COUNT(
            request->approval_fingerprint),
        item->approval_fingerprint);
    request->registry_revision = revision;
}

static int dluma_managed_persist(
    const DlumaManagedModSnapshot* item,
    int enabled)
{
    WCHAR section[128];
    WCHAR fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR temporary[DLUMA_MAX_WPATH];
    dluma_managed_ini_path();
    dluma_managed_section(
        item, section, DLUMA_ARRAY_COUNT(section));
    if (!section[0] ||
        !dluma_utf8_to_wide(
            item->approval_fingerprint,
            fingerprint,
            DLUMA_ARRAY_COUNT(fingerprint)))
        return 0;
    if (!dluma_atomic_ini_begin(g_dluma_managed.ini_path,
                                temporary, DLUMA_ARRAY_COUNT(temporary)))
        return 0;
    if ((enabled && !WritePrivateProfileStringW(
             section, L"approval_fingerprint", fingerprint, temporary)) ||
        !WritePrivateProfileStringW(
            section, L"enabled_next_launch", enabled ? L"1" : L"0",
            temporary)) {
        dluma_atomic_ini_abort(temporary);
        return 0;
    }
    return dluma_atomic_ini_commit(g_dluma_managed.ini_path, temporary);
}

/* Called under the registry lock after source selection on the first scan. */
static void dluma_managed_auto_enable_startup(void)
{
    unsigned int index;
    if (!g_dluma.config.auto_enable_mods ||
        dluma_startup_recovery_suppresses_mods()) return;
    for (index = 0; index < g_dluma_managed.count; ++index) {
        DlumaManagedModSnapshot* item = &g_dluma_managed.entries[index].snapshot;
        WCHAR section[128];
        WCHAR saved[16];
        if (item->scope != DLUMA_SCOPE_GLOBAL || item->duplicate_id ||
            !item->source_selected || item->source_selection_missing ||
            item->runtime_error[0] || !item->approval_fingerprint[0])
            continue;
        dluma_managed_section(item, section, DLUMA_ARRAY_COUNT(section));
        saved[0] = 0;
        GetPrivateProfileStringW(section, L"enabled_next_launch", L"",
            saved, DLUMA_ARRAY_COUNT(saved), g_dluma_managed.ini_path);
        if (saved[0] && (saved[0] != L'1' || saved[1] != L'\0')) continue;
        if (item->enabled_requested && item->approval_valid) continue;
        if (!dluma_managed_persist(item, 1)) {
            item->enabled_requested = 0;
            item->approval_valid = 0;
            dluma_log_pair("WARN", "managed_mod_auto_enable_write_failed", item->id);
            continue;
        }
        item->approval_valid = 1U;
        item->enabled_requested = 1U;
        item->auto_approved = 1U;
        item->state = DLUMA_MANAGED_STATE_APPROVED_NEXT_LAUNCH;
        dluma_log_pair("INFO", "managed_mod_auto_enabled", item->id);
    }
}

int dluma_managed_mods_request_enabled(
    const DlumaManagedModMutationRequest* request,
    int enabled,
    int approve_current)
{
    unsigned int index;
    int result = 0;
    if (!request || !request->registry_key[0] ||
        !request->approval_fingerprint[0])
        return 0;
    dluma_managed_lock();
    if (request->registry_revision !=
        g_dluma_managed.revision) {
        dluma_managed_unlock();
        return 0;
    }
    for (index = 0;
         index < g_dluma_managed.count;
         ++index) {
        DlumaManagedModSnapshot* item =
            &g_dluma_managed.entries[index].snapshot;
        if (dluma_stricmp(
                item->registry_key,
                request->registry_key) != 0)
            continue;
        if (dluma_stricmp(
                item->approval_fingerprint,
                request->approval_fingerprint) != 0 ||
            item->duplicate_id ||
            !item->source_selected ||
            item->runtime_error[0] ||
            (enabled &&
             !item->approval_valid &&
             !approve_current))
            break;
        if (!dluma_managed_persist(item, enabled))
            break;
        if (enabled && approve_current)
            item->approval_valid = 1U;
        item->enabled_requested =
            enabled ? 1U : 0U;
        item->state = enabled
            ? DLUMA_MANAGED_STATE_APPROVED_NEXT_LAUNCH
            : (item->loaded
                ? DLUMA_MANAGED_STATE_DISABLE_NEXT_LAUNCH
                : DLUMA_MANAGED_STATE_DISCOVERED_DISABLED);
        ++g_dluma_managed.revision;
        result = 1;
        break;
    }
    dluma_managed_unlock();
    return result;
}

int dluma_managed_mods_request_source(
    const char* declared_id, const char* source_identity,
    ULONGLONG registry_revision)
{
    unsigned int index;
    int found = 0;
    if (!declared_id || !declared_id[0] ||
        !source_identity || !source_identity[0])
        return 0;
    dluma_managed_lock();
    if (registry_revision == g_dluma_managed.revision) {
        for (index = 0; index < g_dluma_managed.count; ++index) {
            DlumaManagedModSnapshot* item =
                &g_dluma_managed.entries[index].snapshot;
            /* Which copy provides the ID is independent of whether that copy
             * loaded. Enabling still requires approval and a usable host. */
            if (dluma_stricmp(item->id, declared_id) == 0 &&
                dluma_stricmp(item->source_identity, source_identity) == 0 &&
                !item->duplicate_id) {
                found = 1;
                break;
            }
        }
    }
    dluma_managed_unlock();
    if (!found || !dluma_mod_source_selection_write(
            "csharp", declared_id, source_identity))
        return 0;
    dluma_managed_lock();
    if (registry_revision != g_dluma_managed.revision) {
        dluma_managed_unlock();
        return 0;
    }
    for (index = 0; index < g_dluma_managed.count; ++index) {
        DlumaManagedModSnapshot* item =
            &g_dluma_managed.entries[index].snapshot;
        if (dluma_stricmp(item->id, declared_id) != 0) continue;
        item->source_selected =
            (unsigned int)(dluma_stricmp(item->source_identity,
                                      source_identity) == 0 ? 1 : 0);
        item->source_selection_missing = 0;
    }
    ++g_dluma_managed.revision;
    dluma_managed_unlock();
    return 1;
}

int dluma_managed_mods_disable_all_next_launch(void)
{
    unsigned int index;
    int result = 1;
    dluma_managed_lock();
    for (index = 0;
         index < g_dluma_managed.count;
         ++index) {
        DlumaManagedModSnapshot* item =
            &g_dluma_managed.entries[index].snapshot;
        if (!dluma_managed_persist(item, 0))
            result = 0;
        item->enabled_requested = 0;
        item->state = item->loaded
            ? DLUMA_MANAGED_STATE_DISABLE_NEXT_LAUNCH
            : DLUMA_MANAGED_STATE_DISCOVERED_DISABLED;
    }
    ++g_dluma_managed.revision;
    dluma_managed_unlock();
    return result;
}

const char* dluma_managed_mods_state_text(
    const DlumaManagedModSnapshot* item)
{
    if (!item) return "unknown";
    switch (item->state) {
    case DLUMA_MANAGED_STATE_DISCOVERED_DISABLED:
        return "disabled";
    case DLUMA_MANAGED_STATE_APPROVED_NEXT_LAUNCH:
        return "enabled next launch";
    case DLUMA_MANAGED_STATE_LOADED:
        return "loaded";
    case DLUMA_MANAGED_STATE_ACTIVE:
        return "active";
    case DLUMA_MANAGED_STATE_DISABLE_NEXT_LAUNCH:
        return "disable next launch";
    case DLUMA_MANAGED_STATE_CHANGED_REVIEW_REQUIRED:
        return "changed; review required";
    case DLUMA_MANAGED_STATE_CONFLICT:
        return "conflicting stable ID";
    case DLUMA_MANAGED_STATE_INCOMPATIBLE:
        return "incompatible";
    case DLUMA_MANAGED_STATE_HOST_UNAVAILABLE:
        return "managed host unavailable";
    case DLUMA_MANAGED_STATE_LOAD_ERROR:
        return "load error";
    case DLUMA_MANAGED_STATE_STARTUP_RECOVERY_SUPPRESSED:
        return "startup recovery suppressed";
    case DLUMA_MANAGED_STATE_RETAINED:
        return "retained until process exit";
    default:
        return "unknown";
    }
}

static unsigned int dluma_managed_next_epoch(void)
{
    ++g_dluma_managed.next_lifecycle_epoch;
    if (!g_dluma_managed.next_lifecycle_epoch)
        ++g_dluma_managed.next_lifecycle_epoch;
    return g_dluma_managed.next_lifecycle_epoch;
}

static void dluma_managed_set_error(
    DlumaManagedModSnapshot* item,
    unsigned int state,
    const char* error)
{
    if (!item) return;
    item->state = state;
    dluma_str_copy(
        item->runtime_error,
        DLUMA_ARRAY_COUNT(item->runtime_error),
        error && error[0]
            ? error
            : "managed host operation failed");
}

static int dluma_managed_runtime_paths(
    DlumaManagedRuntime* runtime)
{
    WCHAR host_fxr_relative[256];
    WCHAR managed_root[DLUMA_MAX_WPATH];
    WCHAR runtime_root[DLUMA_MAX_WPATH];
    if (!runtime) return 0;
    host_fxr_relative[0] = 0;
    dluma_wstr_copy(
        host_fxr_relative,
        DLUMA_ARRAY_COUNT(host_fxr_relative),
        L"host\\fxr\\");
    dluma_wstr_append(
        host_fxr_relative,
        DLUMA_ARRAY_COUNT(host_fxr_relative),
        DLUMA_MANAGED_RUNTIME_VERSION);
    dluma_wstr_append(
        host_fxr_relative,
        DLUMA_ARRAY_COUNT(host_fxr_relative),
        L"\\hostfxr.dll");
    if (!dluma_managed_join_checked(
            runtime_root,
            DLUMA_ARRAY_COUNT(runtime_root),
            g_dluma.module_dir,
            L"dluma_runtime\\dotnet") ||
        !dluma_managed_join_checked(
            managed_root,
            DLUMA_ARRAY_COUNT(managed_root),
            g_dluma.module_dir,
            L"dluma_runtime\\managed") ||
        !dluma_managed_join_checked(
            runtime->hostfxr_path,
            DLUMA_ARRAY_COUNT(runtime->hostfxr_path),
            runtime_root,
            host_fxr_relative) ||
        !dluma_managed_join_checked(
            runtime->bootstrap_path,
            DLUMA_ARRAY_COUNT(runtime->bootstrap_path),
            managed_root,
            L"Dluma.Managed.Bootstrap.dll") ||
        !dluma_managed_join_checked(
            runtime->runtime_config_path,
            DLUMA_ARRAY_COUNT(runtime->runtime_config_path),
            managed_root,
            L"Dluma.Managed.Bootstrap.runtimeconfig.json"))
        return 0;
    dluma_wstr_copy(
        runtime->runtime_root,
        DLUMA_ARRAY_COUNT(runtime->runtime_root),
        runtime_root);
    dluma_wstr_copy(
        runtime->dotnet_root,
        DLUMA_ARRAY_COUNT(runtime->dotnet_root),
        runtime_root);
    return 1;
}

static int dluma_managed_runtime_fail(
    DlumaManagedRuntime* runtime,
    DlumaRuntimeTreeLease* lease,
    const char* diagnostic)
{
    if (runtime && diagnostic && diagnostic[0])
        dluma_str_copy(
            runtime->diagnostic,
            DLUMA_ARRAY_COUNT(runtime->diagnostic),
            diagnostic);
    dluma_runtime_tree_release(lease);
    return 0;
}

static int dluma_managed_runtime_initialize(void)
{
    DlumaManagedRuntime* runtime =
        &g_dluma_managed.runtime;
    WCHAR hostfxr_final[DLUMA_MAX_WPATH];
    WCHAR bootstrap_final[DLUMA_MAX_WPATH];
    WCHAR loaded_path[DLUMA_MAX_WPATH];
    WCHAR host_path[DLUMA_MAX_WPATH];
    DlumaHostFxrInitializeFn initialize;
    DlumaHostFxrGetDelegateFn get_delegate;
    DlumaHostFxrCloseFn close_context;
    DlumaLoadAssemblyAndGetFunctionPointerFn load_assembly;
    DlumaManagedGetBootstrapApiFn get_api;
    DlumaHostFxrInitializeParameters parameters;
    DlumaRuntimeTreeLease runtime_lease;
    DlumaHostFxrHandle context = 0;
    void* raw_delegate = 0;
    void* raw_get_api = 0;
    int32_t result;
    static const WCHAR bootstrap_type[] =
        L"Dluma.Managed.Bootstrap.BootstrapEntryPoints, "
        L"Dluma.Managed.Bootstrap";
    if (runtime->initialized)
        return 1;
    if (runtime->attempted)
        return 0;
    runtime->attempted = 1U;
    runtime->hostfxr_file = INVALID_HANDLE_VALUE;
    runtime->bootstrap_file = INVALID_HANDLE_VALUE;
    memset(&runtime_lease, 0, sizeof(runtime_lease));
    runtime_lease.struct_size =
        (unsigned int)sizeof(runtime_lease);
    if (!dluma_managed_runtime_paths(runtime)) {
        dluma_str_copy(
            runtime->diagnostic,
            DLUMA_ARRAY_COUNT(runtime->diagnostic),
            "managed runtime paths are too long");
        return 0;
    }
    if (!dluma_runtime_tree_acquire(
            runtime->runtime_root,
            "10.0.13",
            DLUMA_API_CATALOG_SHA256,
            &runtime_lease)) {
        dluma_str_copy(
            runtime->diagnostic,
            DLUMA_ARRAY_COUNT(runtime->diagnostic),
            runtime_lease.diagnostic[0]
                ? runtime_lease.diagnostic
                : "private managed runtime tree validation failed");
        return 0;
    }
    runtime->hostfxr_file =
        dluma_managed_open_regular_final(
            runtime->hostfxr_path,
            hostfxr_final,
            DLUMA_ARRAY_COUNT(hostfxr_final));
    runtime->bootstrap_file =
        dluma_managed_open_regular_final(
            runtime->bootstrap_path,
            bootstrap_final,
            DLUMA_ARRAY_COUNT(bootstrap_final));
    if (runtime->hostfxr_file ==
            INVALID_HANDLE_VALUE ||
        runtime->bootstrap_file ==
            INVALID_HANDLE_VALUE) {
        return dluma_managed_runtime_fail(
            runtime,
            &runtime_lease,
            "private .NET 10.0.13 x64 runtime or managed bootstrap is missing");
    }
    runtime->hostfxr_module = LoadLibraryExW(
        hostfxr_final,
        0,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
            LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!runtime->hostfxr_module ||
        !dluma_get_module_path_checked(
            runtime->hostfxr_module,
            loaded_path,
            DLUMA_ARRAY_COUNT(loaded_path)) ||
        dluma_wstricmp(
            loaded_path, hostfxr_final) != 0) {
        return dluma_managed_runtime_fail(
            runtime,
            &runtime_lease,
            "private hostfxr failed final-path validation");
    }
    initialize = (DlumaHostFxrInitializeFn)
        GetProcAddress(
            runtime->hostfxr_module,
            "hostfxr_initialize_for_runtime_config");
    get_delegate = (DlumaHostFxrGetDelegateFn)
        GetProcAddress(
            runtime->hostfxr_module,
            "hostfxr_get_runtime_delegate");
    close_context = (DlumaHostFxrCloseFn)
        GetProcAddress(
            runtime->hostfxr_module,
            "hostfxr_close");
    if (!initialize || !get_delegate ||
        !close_context) {
        return dluma_managed_runtime_fail(
            runtime,
            &runtime_lease,
            "private hostfxr exports are incomplete");
    }
    memset(&parameters, 0, sizeof(parameters));
    parameters.size = sizeof(parameters);
    parameters.dotnet_root = runtime->dotnet_root;
    host_path[0] = 0;
    if (dluma_get_module_path_checked(
            g_dluma.self_module,
            host_path,
            DLUMA_ARRAY_COUNT(host_path)))
        parameters.host_path = host_path;
    result = initialize(
        runtime->runtime_config_path,
        &parameters,
        &context);
    if (result < 0 || !context) {
        return dluma_managed_runtime_fail(
            runtime,
            &runtime_lease,
            "hostfxr rejected the pinned managed runtime configuration");
    }
    result = get_delegate(
        context,
        5,
        &raw_delegate);
    (void)close_context(context);
    context = 0;
    if (result < 0 || !raw_delegate) {
        return dluma_managed_runtime_fail(
            runtime,
            &runtime_lease,
            "hostfxr did not provide the component load delegate");
    }
    load_assembly =
        (DlumaLoadAssemblyAndGetFunctionPointerFn)
            raw_delegate;
    result = load_assembly(
        bootstrap_final,
        bootstrap_type,
        L"GetApi",
        (const WCHAR*)(intptr_t)-1,
        0,
        &raw_get_api);
    if (result < 0 || !raw_get_api) {
        return dluma_managed_runtime_fail(
            runtime,
            &runtime_lease,
            "managed bootstrap entry point could not be loaded");
    }
    get_api = (DlumaManagedGetBootstrapApiFn)
        raw_get_api;
    memset(&runtime->api, 0, sizeof(runtime->api));
    runtime->diagnostic[0] = 0;
    if (!get_api(
            DLUMA_MANAGED_ABI_VERSION,
            &runtime->api,
            (uint32_t)sizeof(runtime->api),
            runtime->diagnostic,
            DLUMA_ARRAY_COUNT(runtime->diagnostic)) ||
        runtime->api.struct_size <
            (uint32_t)sizeof(runtime->api) ||
        runtime->api.struct_version !=
            DLUMA_MANAGED_STRUCT_VERSION ||
        runtime->api.abi_version !=
            DLUMA_MANAGED_ABI_VERSION ||
        !runtime->api.load ||
        !runtime->api.activate ||
        !runtime->api.deactivate ||
        !runtime->api.dispatch ||
        !runtime->api.shutdown) {
        if (!runtime->diagnostic[0])
            dluma_str_copy(
                runtime->diagnostic,
                DLUMA_ARRAY_COUNT(runtime->diagnostic),
                "managed bootstrap returned a malformed API table");
        memset(&runtime->api, 0, sizeof(runtime->api));
        return dluma_managed_runtime_fail(
            runtime, &runtime_lease, 0);
    }
    runtime->initialized = 1U;
    dluma_runtime_tree_release(&runtime_lease);
    dluma_log_pair(
        "INFO",
        "managed_runtime_version",
        "10.0.13 x64 private");
    return 1;
}

static int __cdecl dluma_managed_bridge_invoke(
    void* context,
    uint32_t operation_id,
    void* request,
    uint32_t request_size,
    void* result,
    uint32_t result_size)
{
    DlumaManagedModEntry* entry =
        (DlumaManagedModEntry*)context;
    if (!entry ||
        entry->lifecycle_phase ==
            DLUMA_MANAGED_PHASE_FAILED ||
        entry->lifecycle_phase ==
            DLUMA_MANAGED_PHASE_RETAINED)
        return (int)DLUMA_STATUS_OWNER_REVOKED;
    return (int)dluma_api_runtime_invoke(
        &entry->api_runtime,
        operation_id,
        request,
        request_size,
        result,
        result_size);
}

static int dluma_managed_api_initialize_entry(
    DlumaManagedModEntry* entry,
    unsigned int index,
    unsigned int epoch)
{
    DlumaModOwnerDefinition definition;
    DlumaGameHostContextDefinition host_definition;
    DlumaModConfigIdentity config_identity;
    if (!entry || !entry->package.valid ||
        entry->package.manifest.language !=
            DLUMA_MOD_LANGUAGE_CSHARP)
        return 0;
    memset(&definition, 0, sizeof(definition));
    definition.struct_size =
        (uint32_t)sizeof(definition);
    definition.struct_version = 1U;
    definition.owner_id =
        0x4353000000000000ULL |
        ((uint64_t)epoch << 16U) |
        ((uint64_t)index + 1U);
    definition.language =
        DLUMA_MOD_LANGUAGE_CSHARP;
    definition.scope =
        entry->package.manifest.scope;
    definition.map_generation =
        g_dluma_managed.current_map_generation
            ? g_dluma_managed.current_map_generation
            : 1U;
    definition.game_thread_id =
        GetCurrentThreadId();
    definition.stable_id.data =
        entry->snapshot.id;
    definition.stable_id.size =
        (uint64_t)strlen(entry->snapshot.id);
    definition.access_ids =
        entry->package.manifest.access_ids;
    definition.access_count =
        entry->package.manifest.access_count;
    definition.max_handles = 256U;
    definition.max_subscriptions = 128U;
    definition.max_timers = 128U;
    definition.max_companion_artifacts = 64U;
    if (dluma_mod_owner_initialize(
            &entry->api_owner,
            &definition) != DLUMA_STATUS_OK)
        return 0;
    dluma_managed_config_identity(
        &entry->snapshot,
        &config_identity);
    memset(
        &host_definition, 0,
        sizeof(host_definition));
    host_definition.struct_size =
        (uint32_t)sizeof(host_definition);
    host_definition.struct_version = 1U;
    host_definition.owner = &entry->api_owner;
    host_definition.package = &entry->package;
    host_definition.config_identity =
        &config_identity;
    host_definition.storage_key =
        entry->snapshot.registry_key;
    host_definition.event_sink =
        dluma_managed_host_event_sink;
    host_definition.event_user = entry;
    entry->api_host.struct_size =
        (uint32_t)sizeof(entry->api_host);
    if (dluma_game_host_context_initialize(
            &entry->api_host,
            &host_definition) != DLUMA_STATUS_OK) {
        dluma_mod_owner_begin_revoke(
            &entry->api_owner);
        dluma_mod_owner_finish_revoke(
            &entry->api_owner);
        return 0;
    }
    dluma_game_backend_prepare_capabilities(
        &entry->api_capabilities);
    dluma_game_host_context_prepare_capabilities(
        &entry->api_host,
        &entry->api_capabilities);
    dluma_game_backend_initialize_for_host(
        &entry->api_backend,
        &entry->api_host);
    if (dluma_api_runtime_initialize(
            &entry->api_runtime,
            &entry->api_owner,
            &entry->api_capabilities,
            &entry->api_backend) !=
        DLUMA_STATUS_OK) {
        dluma_game_host_context_revoke(
            &entry->api_host);
        dluma_mod_owner_begin_revoke(
            &entry->api_owner);
        dluma_mod_owner_finish_revoke(
            &entry->api_owner);
        return 0;
    }
    memset(&entry->bridge, 0, sizeof(entry->bridge));
    entry->bridge.struct_size =
        (uint32_t)sizeof(entry->bridge);
    entry->bridge.struct_version =
        DLUMA_MANAGED_STRUCT_VERSION;
    entry->bridge.abi_version =
        DLUMA_MANAGED_ABI_VERSION;
    entry->bridge.context = entry;
    entry->bridge.invoke =
        dluma_managed_bridge_invoke;
    return 1;
}

static int dluma_managed_collect_artifacts(
    DlumaManagedModEntry* entry,
    DlumaModArtifactSnapshot** snapshots_out,
    DlumaManagedArtifactV1** artifacts_out,
    unsigned int* artifact_count_out)
{
    DlumaModLockedArtifact* locked = 0;
    DlumaModArtifactSnapshot* snapshots = 0;
    DlumaManagedArtifactV1* artifacts = 0;
    unsigned int locked_count = 0;
    unsigned int selected_count = 0;
    unsigned int index;
    ULONGLONG total_bytes = 0;
    if (snapshots_out) *snapshots_out = 0;
    if (artifacts_out) *artifacts_out = 0;
    if (artifact_count_out) *artifact_count_out = 0;
    if (!entry || !snapshots_out || !artifacts_out ||
        !artifact_count_out)
        return 0;
    locked = (DlumaModLockedArtifact*)dluma_alloc(
        (SIZE_T)DLUMA_MANAGED_MAX_ARTIFACTS *
        sizeof(*locked));
    snapshots = (DlumaModArtifactSnapshot*)dluma_alloc(
        (SIZE_T)DLUMA_MANAGED_MAX_ARTIFACTS *
        sizeof(*snapshots));
    artifacts = (DlumaManagedArtifactV1*)dluma_alloc(
        (SIZE_T)DLUMA_MANAGED_MAX_ARTIFACTS *
        sizeof(*artifacts));
    if (!locked || !snapshots || !artifacts)
        goto fail;
    memset(
        snapshots, 0,
        (SIZE_T)DLUMA_MANAGED_MAX_ARTIFACTS *
        sizeof(*snapshots));
    memset(
        artifacts, 0,
        (SIZE_T)DLUMA_MANAGED_MAX_ARTIFACTS *
        sizeof(*artifacts));
    if (dluma_mod_package_list_artifacts(
            &entry->package,
            locked,
            DLUMA_MANAGED_MAX_ARTIFACTS,
            &locked_count) != DLUMA_STATUS_OK)
        goto fail;
    for (index = 0; index < locked_count; ++index) {
        DlumaModArtifactSnapshot* snapshot;
        DlumaManagedArtifactV1* artifact;
        if (locked[index].kind !=
                DLUMA_MOD_ARTIFACT_ENTRY &&
            locked[index].kind !=
                DLUMA_MOD_ARTIFACT_ASSEMBLY &&
            locked[index].kind !=
                DLUMA_MOD_ARTIFACT_DEPENDENCY)
            continue;
        if (selected_count >=
            DLUMA_MANAGED_MAX_ARTIFACTS)
            goto fail;
        snapshot = &snapshots[selected_count];
        snapshot->struct_size =
            (unsigned int)sizeof(*snapshot);
        if (dluma_mod_package_read_artifact(
                &entry->package,
                locked[index].path,
                locked[index].kind,
                DLUMA_MANAGED_MAX_ARTIFACT_BYTES,
                snapshot) != DLUMA_STATUS_OK)
            goto fail;
        total_bytes += snapshot->size;
        if (total_bytes >
            DLUMA_MANAGED_MAX_TOTAL_BYTES)
            goto fail;
        artifact = &artifacts[selected_count];
        artifact->struct_size =
            (uint32_t)sizeof(*artifact);
        artifact->struct_version =
            DLUMA_MANAGED_STRUCT_VERSION;
        artifact->kind = snapshot->kind;
        artifact->relative_path =
            dluma_managed_view(
                snapshot->relative_path);
        artifact->bytes = snapshot->bytes;
        artifact->size = snapshot->size;
        ++selected_count;
    }
    dluma_free(locked);
    if (!selected_count)
        goto fail_without_locked;
    *snapshots_out = snapshots;
    *artifacts_out = artifacts;
    *artifact_count_out = selected_count;
    return 1;

fail:
    dluma_free(locked);
fail_without_locked:
    if (snapshots) {
        for (index = 0;
             index < DLUMA_MANAGED_MAX_ARTIFACTS;
             ++index)
            dluma_mod_artifact_release(
                &snapshots[index]);
    }
    dluma_free(snapshots);
    dluma_free(artifacts);
    return 0;
}

static void dluma_managed_release_artifacts(
    DlumaModArtifactSnapshot* snapshots,
    DlumaManagedArtifactV1* artifacts,
    unsigned int artifact_count)
{
    unsigned int index;
    if (snapshots) {
        for (index = 0;
             index < artifact_count;
             ++index)
            dluma_mod_artifact_release(
                &snapshots[index]);
    }
    dluma_free(snapshots);
    dluma_free(artifacts);
}

static int dluma_managed_activate_entry(
    DlumaManagedModEntry* entry,
    const char* project,
    const char* map,
    unsigned int map_generation)
{
    DlumaManagedActivationV1 activation;
    char diagnostic[DLUMA_MANAGED_DIAGNOSTIC_CAPACITY];
    int succeeded;
    if (!entry || !entry->managed_handle ||
        !g_dluma_managed.runtime.initialized)
        return 0;
    memset(&activation, 0, sizeof(activation));
    memset(diagnostic, 0, sizeof(diagnostic));
    activation.struct_size =
        (uint32_t)sizeof(activation);
    activation.struct_version =
        DLUMA_MANAGED_STRUCT_VERSION;
    activation.scope =
        entry->package.manifest.scope;
    activation.map_generation = map_generation;
    activation.project =
        dluma_managed_view(project ? project : "");
    activation.map =
        dluma_managed_view(map ? map : "");
    entry->lifecycle_phase =
        DLUMA_MANAGED_PHASE_CALLBACK;
    succeeded =
        g_dluma_managed.runtime.api.activate(
            entry->managed_handle,
            &activation,
            diagnostic,
            DLUMA_ARRAY_COUNT(diagnostic));
    if (!succeeded) {
        dluma_managed_set_error(
            &entry->snapshot,
            DLUMA_MANAGED_STATE_LOAD_ERROR,
            diagnostic);
        entry->lifecycle_phase =
            DLUMA_MANAGED_PHASE_FAILED;
        dluma_mod_owner_begin_revoke(
            &entry->api_owner);
        dluma_game_host_context_revoke(
            &entry->api_host);
        dluma_mod_owner_finish_revoke(
            &entry->api_owner);
        return 0;
    }
    entry->snapshot.active = 1U;
    entry->snapshot.state =
        DLUMA_MANAGED_STATE_ACTIVE;
    entry->snapshot.runtime_error[0] = 0;
    entry->lifecycle_phase =
        DLUMA_MANAGED_PHASE_READY;
    return 1;
}

int dluma_managed_host_start(void)
{
    unsigned int index;
    unsigned int candidates = 0;
    int runtime_ready;
    for (index = 0;
         index < g_dluma_managed.count;
         ++index) {
        if (g_dluma_managed.entries[index]
                .snapshot.source_selected &&
            g_dluma_managed.entries[index]
                .snapshot.enabled_requested &&
            g_dluma_managed.entries[index]
                .snapshot.approval_valid &&
            !g_dluma_managed.entries[index]
                .snapshot.duplicate_id &&
            !g_dluma_managed.entries[index]
                .snapshot.runtime_error[0])
            ++candidates;
    }
    if (!candidates)
        return 1;
    if (dluma_startup_recovery_suppresses_mods()) {
        dluma_managed_lock();
        for (index = 0;
             index < g_dluma_managed.count;
             ++index) {
            DlumaManagedModSnapshot* item =
                &g_dluma_managed.entries[index]
                    .snapshot;
            if (!item->source_selected || !item->enabled_requested)
                continue;
            item->state =
                DLUMA_MANAGED_STATE_STARTUP_RECOVERY_SUPPRESSED;
            dluma_str_copy(
                item->runtime_error,
                DLUMA_ARRAY_COUNT(item->runtime_error),
                "suppressed for this startup-recovery session");
        }
        ++g_dluma_managed.revision;
        dluma_managed_unlock();
        return 1;
    }
    runtime_ready =
        dluma_managed_runtime_initialize();
    if (!runtime_ready) {
        dluma_managed_lock();
        for (index = 0;
             index < g_dluma_managed.count;
             ++index) {
            DlumaManagedModSnapshot* item =
                &g_dluma_managed.entries[index]
                    .snapshot;
            if (!item->source_selected || !item->enabled_requested ||
                !item->approval_valid)
                continue;
            dluma_managed_set_error(
                item,
                DLUMA_MANAGED_STATE_HOST_UNAVAILABLE,
                g_dluma_managed.runtime.diagnostic);
        }
        ++g_dluma_managed.revision;
        dluma_managed_unlock();
        dluma_log_pair(
            "ERROR",
            "managed_host_unavailable",
            g_dluma_managed.runtime.diagnostic);
        return 0;
    }

    for (index = 0;
         index < g_dluma_managed.count;
         ++index) {
        DlumaManagedModEntry* entry =
            &g_dluma_managed.entries[index];
        DlumaModPackageSnapshot current;
        DlumaModArtifactSnapshot* snapshots = 0;
        DlumaManagedArtifactV1* artifacts = 0;
        DlumaManagedModLoadV1 load;
        unsigned int artifact_count = 0;
        unsigned int epoch;
        char diagnostic[
            DLUMA_MANAGED_DIAGNOSTIC_CAPACITY];
        uint64_t managed_handle = 0;
        int loaded;
        if (!entry->snapshot.source_selected ||
            !entry->snapshot.enabled_requested ||
            !entry->snapshot.approval_valid ||
            entry->snapshot.duplicate_id ||
            entry->snapshot.runtime_error[0] ||
            entry->lifecycle_phase !=
                DLUMA_MANAGED_PHASE_IDLE)
            continue;
        memset(&current, 0, sizeof(current));
        current.struct_size =
            (unsigned int)sizeof(current);
        if (dluma_mod_package_revalidate(
                &entry->package,
                &current) != DLUMA_STATUS_OK) {
            entry->snapshot.approval_valid = 0;
            entry->snapshot.enabled_requested = 0;
            dluma_managed_set_error(
                &entry->snapshot,
                DLUMA_MANAGED_STATE_CHANGED_REVIEW_REQUIRED,
                current.diagnostic);
            continue;
        }
        epoch = dluma_managed_next_epoch();
        entry->lifecycle_epoch = epoch;
        entry->lifecycle_phase =
            DLUMA_MANAGED_PHASE_LOADING;
        if (!dluma_managed_api_initialize_entry(
                entry, index, epoch) ||
            !dluma_managed_collect_artifacts(
                entry,
                &snapshots,
                &artifacts,
                &artifact_count)) {
            dluma_managed_set_error(
                &entry->snapshot,
                DLUMA_MANAGED_STATE_LOAD_ERROR,
                "approved managed artifacts could not be snapshotted");
            entry->lifecycle_phase =
                DLUMA_MANAGED_PHASE_FAILED;
            dluma_game_host_context_revoke(
                &entry->api_host);
            dluma_mod_owner_begin_revoke(
                &entry->api_owner);
            dluma_mod_owner_finish_revoke(
                &entry->api_owner);
            dluma_managed_release_artifacts(
                snapshots,
                artifacts,
                artifact_count);
            continue;
        }
        memset(&load, 0, sizeof(load));
        memset(diagnostic, 0, sizeof(diagnostic));
        load.struct_size =
            (uint32_t)sizeof(load);
        load.struct_version =
            DLUMA_MANAGED_STRUCT_VERSION;
        load.owner_id =
            entry->api_owner.owner_id;
        load.scope =
            entry->package.manifest.scope;
        load.map_generation =
            entry->api_owner.map_generation;
        load.mod_id =
            dluma_managed_view(
                entry->snapshot.id);
        load.entry_type =
            dluma_managed_view(
                entry->package.manifest.entry_type);
        load.entry_path =
            dluma_managed_view(
                entry->package.manifest.entry);
        load.bridge = &entry->bridge;
        load.artifacts = artifacts;
        load.artifact_count = artifact_count;
        loaded = g_dluma_managed.runtime.api.load(
            &load,
            &managed_handle,
            diagnostic,
            DLUMA_ARRAY_COUNT(diagnostic));
        dluma_managed_release_artifacts(
            snapshots,
            artifacts,
            artifact_count);
        if (!loaded || !managed_handle) {
            dluma_managed_set_error(
                &entry->snapshot,
                DLUMA_MANAGED_STATE_LOAD_ERROR,
                diagnostic);
            entry->lifecycle_phase =
                DLUMA_MANAGED_PHASE_FAILED;
            dluma_mod_owner_begin_revoke(
                &entry->api_owner);
            dluma_game_host_context_revoke(
                &entry->api_host);
            dluma_mod_owner_finish_revoke(
                &entry->api_owner);
            continue;
        }
        entry->managed_handle = managed_handle;
        entry->snapshot.loaded = 1U;
        entry->snapshot.state =
            DLUMA_MANAGED_STATE_LOADED;
        entry->snapshot.runtime_error[0] = 0;
        entry->lifecycle_phase =
            DLUMA_MANAGED_PHASE_READY;
        dluma_log_pair(
            "INFO",
            "managed_mod_loaded",
            entry->snapshot.id);
        if (entry->package.manifest.scope ==
            DLUMA_SCOPE_GLOBAL)
            (void)dluma_managed_activate_entry(
                entry,
                "",
                "",
                entry->api_owner.map_generation);
    }
    ++g_dluma_managed.revision;
    return 1;
}

void dluma_managed_host_deactivate_scope(
    unsigned int next_map_generation)
{
    int reverse_index;
    for (reverse_index =
             (int)g_dluma_managed.count - 1;
         reverse_index >= 0;
         --reverse_index) {
        DlumaManagedModEntry* entry =
            &g_dluma_managed.entries[
                (unsigned int)reverse_index];
        char diagnostic[
            DLUMA_MANAGED_DIAGNOSTIC_CAPACITY];
        int stopped;
        if (!entry->snapshot.active ||
            entry->package.manifest.scope ==
                DLUMA_SCOPE_GLOBAL ||
            entry->lifecycle_phase !=
                DLUMA_MANAGED_PHASE_READY)
            continue;
        memset(diagnostic, 0, sizeof(diagnostic));
        entry->lifecycle_phase =
            DLUMA_MANAGED_PHASE_CALLBACK;
        stopped =
            g_dluma_managed.runtime.api.deactivate(
                entry->managed_handle,
                1U,
                diagnostic,
                DLUMA_ARRAY_COUNT(diagnostic));
        entry->snapshot.active = 0;
        dluma_game_host_context_advance_scope(
            &entry->api_host);
        dluma_mod_owner_advance_scope(
            &entry->api_owner,
            next_map_generation);
        if (!stopped) {
            dluma_managed_set_error(
                &entry->snapshot,
                DLUMA_MANAGED_STATE_LOAD_ERROR,
                diagnostic);
            entry->lifecycle_phase =
                DLUMA_MANAGED_PHASE_FAILED;
        } else {
            entry->snapshot.state =
                DLUMA_MANAGED_STATE_LOADED;
            entry->lifecycle_phase =
                DLUMA_MANAGED_PHASE_READY;
        }
    }
    ++g_dluma_managed.revision;
}

int dluma_managed_host_revoke_project_for_exit(
    unsigned int expected_generation, DlumaProjectHostExitDrain* out)
{
    return dluma_managed_host_revoke_project_for_exit_ex(expected_generation,0,out);
}

int dluma_managed_host_revoke_project_for_exit_ex(
    unsigned int expected_generation, const DlumaProjectExitDrainAuthority* authority,
    DlumaProjectHostExitDrain* out)
{
    DlumaProjectHostExitDrain result;
    unsigned int indices[DLUMA_MAX_MANAGED_MODS];
    uint64_t owners[DLUMA_MAX_MANAGED_MODS];
    unsigned int selected = 0;
    unsigned int i;
    unsigned int current_thread = GetCurrentThreadId();
    memset(&result,0,sizeof(result));
    result.expected_generation = expected_generation;
    result.sdk_contract_only = 1U;
    if (!out) return 0;
    if (!dluma_project_exit_validate_authority(expected_generation,authority)) {
        dluma_str_copy(result.blocker,DLUMA_ARRAY_COUNT(result.blocker),"managed_scope_owner_not_authoritative");
        *out=result; return 0;
    }
    if (dluma_atomic_compare_exchange(&g_dluma_managed.pump_active,1,0)) {
        dluma_str_copy(result.blocker,DLUMA_ARRAY_COUNT(result.blocker),"managed_scope_pump_active");
        *out=result; return 0;
    }
    dluma_atomic_exchange(&g_dluma_managed_project_exit_gate,1);
    if (dluma_atomic_compare_exchange(&g_dluma_managed.lock,1,0)) {
        dluma_str_copy(result.blocker,DLUMA_ARRAY_COUNT(result.blocker),"managed_scope_registry_busy");
        goto done;
    }
    for (i=0;i<g_dluma_managed.count;++i) {
        DlumaManagedModEntry* entry=&g_dluma_managed.entries[i];
        if (entry->package.manifest.scope==DLUMA_SCOPE_GLOBAL) {
            if (entry->snapshot.active) ++result.global_owners_preserved;
            continue;
        }
        if (entry->scope_exit_revoked==2U) {
            ++result.remaining_owners;
            dluma_str_copy(result.blocker,DLUMA_ARRAY_COUNT(result.blocker),"managed_scope_previous_deactivation_failed");
            dluma_str_copy(result.first_owner,DLUMA_ARRAY_COUNT(result.first_owner),entry->snapshot.id);
            break;
        }
        if (!entry->snapshot.active && !entry->cleanup_pending) {
            if (entry->scope_exit_revoked && entry->api_owner.map_generation==expected_generation) {
                indices[selected]=i;
                owners[selected++]=entry->api_owner.owner_id;
            }
            continue;
        }
        ++result.project_owners;
        if (entry->lifecycle_phase==DLUMA_MANAGED_PHASE_CALLBACK) ++result.active_callbacks;
        if (!entry->managed_handle || !g_dluma_managed.runtime.initialized ||
            !g_dluma_managed.runtime.api.deactivate) {
            ++result.unsupported_owners;
            dluma_str_copy(result.blocker,DLUMA_ARRAY_COUNT(result.blocker),"managed_scope_lifecycle_contract_unavailable");
        } else if (entry->api_owner.map_generation!=expected_generation) {
            dluma_str_copy(result.blocker,DLUMA_ARRAY_COUNT(result.blocker),"managed_scope_owner_generation_mismatch");
        } else if (entry->lifecycle_phase!=DLUMA_MANAGED_PHASE_READY || entry->cleanup_pending) {
            dluma_str_copy(result.blocker,DLUMA_ARRAY_COUNT(result.blocker),"managed_scope_callback_or_cleanup_pending");
        }
        if (result.blocker[0]) {
            dluma_str_copy(result.first_owner,DLUMA_ARRAY_COUNT(result.first_owner),entry->snapshot.id);
            break;
        }
        indices[selected]=i;
        owners[selected++]=entry->api_owner.owner_id;
    }
    if (result.blocker[0]) { dluma_managed_unlock_value(&g_dluma_managed.lock); goto done; }
    result.accepted=result.owner_thread_confirmed=1U;
    for (i=0;i<selected;++i)
        if (!g_dluma_managed.entries[indices[i]].scope_exit_revoked) {
            g_dluma_managed.entries[indices[i]].lifecycle_phase=DLUMA_MANAGED_PHASE_CALLBACK;
            g_dluma_managed.entries[indices[i]].api_owner.game_thread_id=current_thread;
        }
    dluma_managed_unlock_value(&g_dluma_managed.lock);
    for (i=selected;i>0;--i) {
        DlumaManagedModEntry* entry=&g_dluma_managed.entries[indices[i-1U]];
        char diagnostic[DLUMA_MANAGED_DIAGNOSTIC_CAPACITY];
        int stopped;
        if (entry->scope_exit_revoked) continue;
        memset(diagnostic,0,sizeof(diagnostic));
        /* Bootstrap Deactivate returns success only after StopState has
         * revoked Context, cleared Instance and marked Active=false under
         * its per-mod callback gate. The assembly/ALC remain mapped. */
        stopped=g_dluma_managed.runtime.api.deactivate(entry->managed_handle,1U,
            diagnostic,DLUMA_ARRAY_COUNT(diagnostic));
        dluma_mod_owner_begin_revoke(&entry->api_owner);
        dluma_game_host_context_advance_scope(&entry->api_host);
        dluma_mod_owner_finish_revoke(&entry->api_owner);
        entry->snapshot.active=0U;
        if (stopped) entry->snapshot.state=DLUMA_MANAGED_STATE_LOADED;
        entry->scope_exit_revoked=stopped ? 1U : 2U;
        entry->lifecycle_phase=stopped ? DLUMA_MANAGED_PHASE_READY : DLUMA_MANAGED_PHASE_FAILED;
        if (!stopped) {
            ++result.remaining_owners;
            dluma_str_copy(result.blocker,DLUMA_ARRAY_COUNT(result.blocker),"managed_scope_deactivation_failed");
            dluma_str_copy(result.first_owner,DLUMA_ARRAY_COUNT(result.first_owner),entry->snapshot.id);
            dluma_managed_set_error(&entry->snapshot,DLUMA_MANAGED_STATE_LOAD_ERROR,diagnostic);
        }
        if (entry->api_owner.state!=DLUMA_MOD_OWNER_REVOKED || entry->cleanup_pending)
            ++result.remaining_owners;
    }
    if (dluma_atomic_compare_exchange(&g_dluma_managed.event_lock,1,0)) {
        dluma_str_copy(result.blocker,DLUMA_ARRAY_COUNT(result.blocker),"managed_scope_event_queue_busy");
    } else {
        unsigned int write=0;
        for (i=0;i<g_dluma_managed.event_count;++i) {
            DlumaManagedQueuedEvent* event=&g_dluma_managed.events[(g_dluma_managed.event_head+i)%DLUMA_MANAGED_MAX_EVENTS];
            unsigned int owner;
            int retired=0;
            for (owner=0;owner<selected;++owner) if (event->owner_id==owners[owner]) retired=1;
            if (!retired) g_dluma_managed.events[(g_dluma_managed.event_head+write++)%DLUMA_MANAGED_MAX_EVENTS]=*event;
        }
        g_dluma_managed.event_count=write;
        dluma_atomic_exchange(&g_dluma_managed.event_lock,0);
    }
    if (!dluma_atomic_compare_exchange(&g_dluma_managed.lock,1,0)) {
        ++g_dluma_managed.revision;
        dluma_managed_unlock_value(&g_dluma_managed.lock);
    } else {
        dluma_str_copy(result.blocker,DLUMA_ARRAY_COUNT(result.blocker),"managed_scope_publication_busy");
    }
    result.complete=!result.blocker[0] && !result.remaining_owners;
done:
    dluma_atomic_exchange(&g_dluma_managed.pump_active,0);
    *out=result;
    return result.complete!=0;
}

int dluma_managed_host_freeze_map_exit(unsigned int generation,
    const DlumaProjectExitDrainAuthority* authority)
{
    unsigned int i;int ready=1;
    if(!dluma_project_exit_validate_authority(generation,authority))return 0;
    dluma_atomic_exchange(&g_dluma_managed_map_exit_frozen,1);
    dluma_atomic_exchange(&g_dluma_managed_project_exit_gate,1);
    g_dluma_managed_map_exit_generation=generation;
    if(dluma_atomic_compare_exchange(&g_dluma_managed.pump_active,0,0) ||
       dluma_atomic_compare_exchange(&g_dluma_managed.lock,1,0))return 0;
    for(i=0;i<g_dluma_managed.count;++i){
        DlumaManagedModEntry* e=&g_dluma_managed.entries[i];
        if(!e->snapshot.active)continue;
        if(e->lifecycle_phase!=DLUMA_MANAGED_PHASE_READY || e->cleanup_pending ||
           (g_dluma_managed.current_map[0] && e->api_owner.map_generation!=generation))ready=0;
    }
    dluma_managed_unlock_value(&g_dluma_managed.lock);return ready;
}

int dluma_managed_host_suspend_global_map(unsigned int generation,
    const DlumaProjectExitDrainAuthority* authority,unsigned int* count)
{
    unsigned int i;int complete=1;
    if(count)*count=0;
    if(!g_dluma_managed_map_exit_frozen || generation!=g_dluma_managed_map_exit_generation ||
       !dluma_project_exit_validate_authority(generation,authority) ||
       dluma_atomic_compare_exchange(&g_dluma_managed.pump_active,0,0))return 0;
    for(i=0;i<g_dluma_managed.count;++i){
        DlumaManagedModEntry* e=&g_dluma_managed.entries[i];
        if(e->package.manifest.scope!=DLUMA_SCOPE_GLOBAL || !e->snapshot.active)continue;
        if(e->lifecycle_phase!=DLUMA_MANAGED_PHASE_READY)return 0;
        e->api_owner.game_thread_id=GetCurrentThreadId();
        if(!dluma_game_host_context_suspend_map_for_exit(&e->api_host))complete=0;
        e->global_map_exit_suspended=1U;
        if(count)++*count;
    }
    return complete;
}

int dluma_managed_host_bind_frontend_after_exit(unsigned int old_generation,
    unsigned int new_generation,const DlumaProjectExitDrainAuthority* authority)
{
    unsigned int i;
    if(!g_dluma_managed_map_exit_frozen || old_generation!=g_dluma_managed_map_exit_generation ||
       !dluma_project_exit_validate_authority(old_generation,authority))return 0;
    for(i=0;i<g_dluma_managed.count;++i){
        DlumaManagedModEntry* e=&g_dluma_managed.entries[i];
        if(!e->global_map_exit_suspended || e->package.manifest.scope!=DLUMA_SCOPE_GLOBAL)continue;
        e->api_owner.game_thread_id=GetCurrentThreadId();e->api_owner.state=DLUMA_MOD_OWNER_ACTIVE;
        if(e->api_owner.map_generation!=new_generation)
            dluma_mod_owner_advance_scope(&e->api_owner,new_generation);
        e->api_owner.state=DLUMA_MOD_OWNER_REVOKING;
        dluma_game_host_context_bind_clean_frontend(&e->api_host);
    }
    g_dluma_managed.current_project_root[0]=0;
    g_dluma_managed.current_project[0]=g_dluma_managed.current_map[0]=0;
    g_dluma_managed.current_map_generation=new_generation;
    if(dluma_atomic_compare_exchange(&g_dluma_managed.event_lock,1,0))return 0;
    {
        unsigned int write=0;
        for(i=0;i<g_dluma_managed.event_count;++i){
            DlumaManagedQueuedEvent event=g_dluma_managed.events[(g_dluma_managed.event_head+i)%DLUMA_MANAGED_MAX_EVENTS];
            unsigned int owner;int keep=0;
            if(event.kind==10U)for(owner=0;owner<g_dluma_managed.count;++owner)
                if(g_dluma_managed.entries[owner].package.manifest.scope==DLUMA_SCOPE_GLOBAL &&
                   g_dluma_managed.entries[owner].api_owner.owner_id==event.owner_id)keep=1;
            if(keep){event.map_generation=new_generation;g_dluma_managed.events[(g_dluma_managed.event_head+write++)%DLUMA_MANAGED_MAX_EVENTS]=event;}
        }
        g_dluma_managed.event_count=write;
    }
    dluma_atomic_exchange(&g_dluma_managed.event_lock,0);return 1;
}

void dluma_managed_host_resume_frontend_after_exit(void)
{
    unsigned int i;
    for(i=0;i<g_dluma_managed.count;++i){
        DlumaManagedModEntry* e=&g_dluma_managed.entries[i];
        if(e->global_map_exit_suspended && e->package.manifest.scope==DLUMA_SCOPE_GLOBAL &&
           e->api_owner.state==DLUMA_MOD_OWNER_REVOKING)e->api_owner.state=DLUMA_MOD_OWNER_ACTIVE;
    }
    dluma_atomic_exchange(&g_dluma_managed_project_exit_gate,0);
    dluma_atomic_exchange(&g_dluma_managed_map_exit_frozen,0);
}

void dluma_managed_host_activate_scope(
    const WCHAR* project_root,
    const char* project_name,
    const char* map_name,
    unsigned int map_generation)
{
    unsigned int index;
    if(g_dluma_managed_map_exit_frozen)return;
    if (!project_root || !project_root[0] || !map_name || !map_name[0])
        dluma_atomic_exchange(&g_dluma_managed_project_exit_gate, 0);
    else if (g_dluma_managed_project_exit_gate)
        return;
    dluma_wstr_copy(
        g_dluma_managed.current_project_root,
        DLUMA_ARRAY_COUNT(
            g_dluma_managed.current_project_root),
        project_root ? project_root : L"");
    dluma_str_copy(
        g_dluma_managed.current_project,
        DLUMA_ARRAY_COUNT(g_dluma_managed.current_project),
        project_name ? project_name : "");
    dluma_str_copy(
        g_dluma_managed.current_map,
        DLUMA_ARRAY_COUNT(g_dluma_managed.current_map),
        map_name ? map_name : "");
    g_dluma_managed.current_map_generation =
        map_generation;
    for (index = 0;
         index < g_dluma_managed.count;
         ++index) {
        DlumaManagedModEntry* entry =
            &g_dluma_managed.entries[index];
        unsigned int scope =
            entry->package.manifest.scope;
        if (scope == DLUMA_SCOPE_GLOBAL) {
            if (entry->snapshot.active &&
                entry->lifecycle_phase ==
                    DLUMA_MANAGED_PHASE_READY) {
                dluma_mod_owner_advance_scope(
                    &entry->api_owner,
                    map_generation);
                dluma_game_host_context_rebind_map(
                    &entry->api_host);
                entry->global_map_exit_suspended=0U;
            }
            continue;
        }
        if (!project_root || !project_root[0] ||
            !map_name || !map_name[0] ||
            !entry->snapshot.loaded ||
            entry->snapshot.active ||
            entry->lifecycle_phase !=
                DLUMA_MANAGED_PHASE_READY ||
            dluma_wstricmp(
                entry->snapshot.project_root,
                project_root) != 0 ||
            (scope == DLUMA_SCOPE_MAP &&
             dluma_stricmp(
                 entry->package.manifest.map,
                 map_name) != 0))
            continue;
        dluma_game_host_context_advance_scope(
            &entry->api_host);
        if (entry->scope_exit_revoked == 1U &&
            entry->api_owner.state == DLUMA_MOD_OWNER_REVOKED) {
            entry->api_owner.state = DLUMA_MOD_OWNER_ACTIVE;
            entry->api_owner.game_thread_id = GetCurrentThreadId();
            entry->scope_exit_revoked = 0U;
        }
        dluma_mod_owner_advance_scope(
            &entry->api_owner,
            map_generation);
        (void)dluma_managed_activate_entry(
            entry,
            project_name,
            map_name,
            map_generation);
    }
    ++g_dluma_managed.revision;
}

int dluma_managed_host_enqueue_game_event(
    unsigned int kind,
    unsigned int map_generation,
    ULONGLONG native_index_epoch,
    ULONGLONG native_subscription_key_revision,
    const char* exact_name,
    int state,
    double number)
{
    DlumaNativeEventTarget targets[
        DLUMA_MAX_MANAGED_MODS];
    unsigned int target_count;
    unsigned int index;
    int queued = 0;
    if (!g_dluma_managed.runtime.initialized ||
        !exact_name || !exact_name[0] ||
        strlen(exact_name) >= 96U)
        return 0;
    target_count =
        dluma_native_gameplay_snapshot_event_targets(
            kind,
            map_generation,
            native_index_epoch,
            native_subscription_key_revision,
            exact_name,
            targets,
            DLUMA_ARRAY_COUNT(targets));
    dluma_managed_lock_value(
        &g_dluma_managed.event_lock);
    for (index = 0; index < target_count; ++index) {
        unsigned int queue_index;
        DlumaManagedQueuedEvent* event_value;
        if (g_dluma_managed.event_count >=
            DLUMA_MANAGED_MAX_EVENTS)
            break;
        queue_index =
            (g_dluma_managed.event_head +
             g_dluma_managed.event_count) %
            DLUMA_MANAGED_MAX_EVENTS;
        event_value =
            &g_dluma_managed.events[queue_index];
        memset(event_value, 0, sizeof(*event_value));
        event_value->owner_id =
            targets[index].owner_id;
        event_value->callback_id =
            targets[index].callback_id;
        event_value->token =
            targets[index].registration_token;
        event_value->kind = kind;
        event_value->map_generation =
            map_generation;
        event_value->state =
            state ? 1 : 0;
        event_value->number = number;
        dluma_str_copy(
            event_value->name,
            DLUMA_ARRAY_COUNT(event_value->name),
            exact_name);
        ++g_dluma_managed.event_count;
        queued = 1;
    }
    dluma_managed_unlock_value(
        &g_dluma_managed.event_lock);
    if (target_count && !queued)
        dluma_log_line(
            "ERROR",
            "managed event queue overflow");
    return queued;
}

static void dluma_managed_dispatch_event(
    DlumaManagedModEntry* entry,
    const DlumaManagedQueuedEvent* queued)
{
    DlumaManagedEventV1 event_value;
    char diagnostic[
        DLUMA_MANAGED_DIAGNOSTIC_CAPACITY];
    /* A global managed owner survives map reconciliation before queue drain.
     * Never deliver an old-map observation against its new active context. */
    if (!entry || !queued ||
        g_dluma_managed_map_exit_frozen ||
        (g_dluma_managed_project_exit_gate &&
         entry->package.manifest.scope != DLUMA_SCOPE_GLOBAL) ||
        !entry->snapshot.active ||
        entry->api_owner.state != DLUMA_MOD_OWNER_ACTIVE ||
        entry->api_owner.owner_id != queued->owner_id ||
        entry->api_owner.map_generation != queued->map_generation ||
        entry->lifecycle_phase !=
            DLUMA_MANAGED_PHASE_READY)
        return;
    memset(&event_value, 0, sizeof(event_value));
    memset(diagnostic, 0, sizeof(diagnostic));
    event_value.struct_size =
        (uint32_t)sizeof(event_value);
    event_value.struct_version =
        DLUMA_MANAGED_STRUCT_VERSION;
    event_value.kind = queued->kind;
    event_value.map_generation =
        queued->map_generation;
    event_value.callback_id =
        queued->callback_id;
    event_value.token = queued->token;
    event_value.number = queued->number;
    event_value.state = queued->state;
    event_value.name =
        dluma_managed_view(queued->name);
    event_value.project =
        dluma_managed_view(
            g_dluma_managed.current_project);
    event_value.map =
        dluma_managed_view(
            g_dluma_managed.current_map);
    entry->lifecycle_phase =
        DLUMA_MANAGED_PHASE_CALLBACK;
    if (!g_dluma_managed.runtime.api.dispatch(
            entry->managed_handle,
            &event_value,
            diagnostic,
            DLUMA_ARRAY_COUNT(diagnostic))) {
        char cleanup_diagnostic[
            DLUMA_MANAGED_DIAGNOSTIC_CAPACITY];
        memset(
            cleanup_diagnostic, 0,
            sizeof(cleanup_diagnostic));
        (void)g_dluma_managed.runtime.api.deactivate(
            entry->managed_handle,
            3U,
            cleanup_diagnostic,
            DLUMA_ARRAY_COUNT(cleanup_diagnostic));
        dluma_managed_set_error(
            &entry->snapshot,
            DLUMA_MANAGED_STATE_LOAD_ERROR,
            diagnostic);
        entry->snapshot.active = 0;
        entry->lifecycle_phase =
            DLUMA_MANAGED_PHASE_FAILED;
        dluma_mod_owner_begin_revoke(
            &entry->api_owner);
        entry->cleanup_pending = 1U;
    } else {
        entry->lifecycle_phase =
            DLUMA_MANAGED_PHASE_READY;
    }
}

static void dluma_managed_finish_pending_cleanup(
    DlumaManagedModEntry* entry)
{
    if (!entry || !entry->cleanup_pending)
        return;
    dluma_game_host_context_revoke(
        &entry->api_host);
    dluma_mod_owner_finish_revoke(
        &entry->api_owner);
    entry->cleanup_pending = 0U;
}

static void dluma_managed_host_event_sink(
    void* user,
    uint32_t kind,
    uint64_t callback_id,
    uint64_t token,
    double number,
    const char* name)
{
    DlumaManagedModEntry* entry =
        (DlumaManagedModEntry*)user;
    DlumaManagedQueuedEvent queued;
    if (!entry) return;
    memset(&queued, 0, sizeof(queued));
    queued.owner_id = entry->api_owner.owner_id;
    queued.callback_id = callback_id;
    queued.token = token;
    queued.kind = kind;
    queued.map_generation =
        entry->api_owner.map_generation;
    queued.number = number;
    dluma_str_copy(
        queued.name,
        DLUMA_ARRAY_COUNT(queued.name),
        name ? name : "");
    dluma_managed_dispatch_event(entry, &queued);
}

void dluma_managed_host_prepare_game_frame(void)
{
    if(g_dluma_managed_map_exit_frozen)return;
    unsigned int index;
    if (!dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine) ||
        dluma_engine_game_frame_authority_status(&g_dluma.engine, GetTickCount64()) !=
            DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE)
        return;
    dluma_managed_lock_value(&g_dluma_managed.lock);
    for (index = 0; index < g_dluma_managed.count; ++index) {
        DlumaManagedModEntry* entry = &g_dluma_managed.entries[index];
        if (entry->api_owner.state != DLUMA_MOD_OWNER_ACTIVE ||
            entry->lifecycle_phase != DLUMA_MANAGED_PHASE_READY)
            continue;
        /* Scope callbacks precede event dispatch. Rebind before either can
         * call the API, and refresh adapters that became ready after load. */
        entry->api_owner.game_thread_id = GetCurrentThreadId();
        dluma_game_backend_prepare_capabilities(&entry->api_capabilities);
        dluma_game_host_context_prepare_capabilities(
            &entry->api_host, &entry->api_capabilities);
    }
    /* Thread/readiness refresh does not change the public mod snapshot. */
    dluma_managed_unlock_value(&g_dluma_managed.lock);
}

void dluma_managed_host_game_thread_pump(
    double delta_seconds)
{
    if(g_dluma_managed_map_exit_frozen)return;
    unsigned int index;
    if (!g_dluma_managed.runtime.initialized ||
        dluma_atomic_compare_exchange(
            &g_dluma_managed.pump_active, 1, 0) != 0)
        return;
    for (;;) {
        DlumaManagedQueuedEvent queued;
        int have_event = 0;
        memset(&queued, 0, sizeof(queued));
        dluma_managed_lock_value(
            &g_dluma_managed.event_lock);
        if (g_dluma_managed.event_count) {
            queued = g_dluma_managed.events[
                g_dluma_managed.event_head];
            memset(
                &g_dluma_managed.events[
                    g_dluma_managed.event_head],
                0,
                sizeof(g_dluma_managed.events[0]));
            g_dluma_managed.event_head =
                (g_dluma_managed.event_head + 1U) %
                DLUMA_MANAGED_MAX_EVENTS;
            --g_dluma_managed.event_count;
            have_event = 1;
        }
        dluma_managed_unlock_value(
            &g_dluma_managed.event_lock);
        if (!have_event) break;
        for (index = 0;
             index < g_dluma_managed.count;
             ++index) {
            DlumaManagedModEntry* entry =
                &g_dluma_managed.entries[index];
            if (entry->api_owner.owner_id ==
                    queued.owner_id) {
                if (queued.kind == 10U &&
                    !queued.callback_id)
                    dluma_game_host_context_notify_config_changed(
                        &entry->api_host,
                        queued.name);
                else
                    dluma_managed_dispatch_event(
                        entry, &queued);
                dluma_managed_finish_pending_cleanup(
                    entry);
                break;
            }
        }
    }
    for (index = 0;
         index < g_dluma_managed.count;
         ++index) {
        DlumaManagedModEntry* entry =
            &g_dluma_managed.entries[index];
        if (!entry->snapshot.active ||
            entry->lifecycle_phase !=
                DLUMA_MANAGED_PHASE_READY)
            continue;
        dluma_game_host_context_pump(
            &entry->api_host,
            delta_seconds);
        dluma_managed_finish_pending_cleanup(entry);
    }
    dluma_atomic_exchange(
        &g_dluma_managed.pump_active, 0);
}

int dluma_managed_host_config_changed(
    const char* registry_key,
    const char* approval_fingerprint,
    const char* setting_key)
{
    unsigned int index;
    int notified = 0;
    if (!registry_key || !approval_fingerprint ||
        !setting_key || !setting_key[0])
        return 0;
    for (index = 0;
         index < g_dluma_managed.count;
         ++index) {
        DlumaManagedModEntry* entry =
            &g_dluma_managed.entries[index];
        if (!entry->snapshot.active ||
            entry->lifecycle_phase !=
                DLUMA_MANAGED_PHASE_READY ||
            dluma_stricmp(
                entry->snapshot.registry_key,
                registry_key) != 0 ||
            dluma_stricmp(
                entry->snapshot.approval_fingerprint,
                approval_fingerprint) != 0)
            continue;
        dluma_managed_lock_value(
            &g_dluma_managed.event_lock);
        if (g_dluma_managed.event_count <
            DLUMA_MANAGED_MAX_EVENTS) {
            unsigned int queue_index =
                (g_dluma_managed.event_head +
                 g_dluma_managed.event_count) %
                DLUMA_MANAGED_MAX_EVENTS;
            DlumaManagedQueuedEvent* queued =
                &g_dluma_managed.events[queue_index];
            memset(queued, 0, sizeof(*queued));
            queued->owner_id =
                entry->api_owner.owner_id;
            queued->kind = 10U;
            queued->map_generation =
                entry->api_owner.map_generation;
            dluma_str_copy(
                queued->name,
                DLUMA_ARRAY_COUNT(queued->name),
                setting_key);
            ++g_dluma_managed.event_count;
            notified = 1;
        }
        dluma_managed_unlock_value(
            &g_dluma_managed.event_lock);
    }
    return notified;
}

int dluma_managed_host_shutdown(
    int loader_callbacks_drained)
{
    int reverse_index;
    int drained = loader_callbacks_drained ? 1 : 0;
    uint64_t wait_started;
    if (!g_dluma_managed.runtime.initialized)
        return 1;
    wait_started = GetTickCount64();
    while (dluma_atomic_compare_exchange(
               &g_dluma_managed.pump_active,
               0,
               0) != 0 &&
           GetTickCount64() - wait_started < 2000U)
        Sleep(1);
    if (dluma_atomic_compare_exchange(
            &g_dluma_managed.pump_active,
            0,
            0) != 0)
        drained = 0;
    for (reverse_index =
             (int)g_dluma_managed.count - 1;
         reverse_index >= 0;
         --reverse_index) {
        DlumaManagedModEntry* entry =
            &g_dluma_managed.entries[
                (unsigned int)reverse_index];
        char diagnostic[
            DLUMA_MANAGED_DIAGNOSTIC_CAPACITY];
        int stopped = 0;
        if (!entry->snapshot.loaded ||
            !entry->managed_handle ||
            entry->lifecycle_phase ==
                DLUMA_MANAGED_PHASE_RETAINED)
            continue;
        memset(diagnostic, 0, sizeof(diagnostic));
        if (drained) {
            stopped =
                g_dluma_managed.runtime.api.shutdown(
                    entry->managed_handle,
                    2U,
                    diagnostic,
                    DLUMA_ARRAY_COUNT(diagnostic));
        }
        dluma_mod_owner_begin_revoke(
            &entry->api_owner);
        if (stopped) {
            dluma_game_host_context_revoke(
                &entry->api_host);
            dluma_mod_owner_finish_revoke(
                &entry->api_owner);
        } else {
            drained = 0;
        }
        entry->snapshot.active = 0;
        entry->snapshot.state =
            DLUMA_MANAGED_STATE_RETAINED;
        entry->lifecycle_phase =
            DLUMA_MANAGED_PHASE_RETAINED;
        if (!stopped)
            dluma_str_copy(
                entry->snapshot.runtime_error,
                DLUMA_ARRAY_COUNT(
                    entry->snapshot.runtime_error),
                diagnostic[0]
                    ? diagnostic
                    : "managed callbacks were retained because shutdown did not drain");
    }
    ++g_dluma_managed.revision;
    return drained;
}
