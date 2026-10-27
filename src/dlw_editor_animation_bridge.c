#include "dlw_editor_animation_bridge.h"

#include "dlw_animation_pack_runtime.h"
#include "dlw_animation_refresh_runtime.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_editor_launch.h"
#include "dlw_log.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"

#include <string.h>

#define DLUMA_EDITOR_ANIMATION_EXE_TIMESTAMP 0x623396E1U
#define DLUMA_EDITOR_ANIMATION_EXE_SIZE 0x01E4A000U
#define DLUMA_EDITOR_ANIMATION_ENGINE_SIZE 0x00E8D000U
#define DLUMA_EDITOR_ANIMATION_SINGLETON_RVA 0x01B70238U
#define DLUMA_EDITOR_ANIMATION_ANIMATOR_OFFSET 4552U
#define DLUMA_EDITOR_ANIMATION_SUBSCRIBERS_BEGIN 1032U
#define DLUMA_EDITOR_ANIMATION_SUBSCRIBERS_END 1040U
#define DLUMA_EDITOR_ANIMATION_CALLBACK_RVA 0x0022F410U
#define DLUMA_EDITOR_ANIMATION_RECONCILE_RVA 0x001CB350U
#define DLUMA_EDITOR_ANIMATION_CLEAR_RVA 0x001CB270U
#define DLUMA_EDITOR_ANIMATION_CACHE_BUILD_RVA 0x001CB4A0U
#define DLUMA_EDITOR_ANIMATION_CACHE_EVICT_RVA 0x001CB970U
#define DLUMA_EDITOR_ANIMATION_TAB_STATE_RVA 0x0022BB70U
#define DLUMA_EDITOR_ANIMATION_TAB_ROWS_RVA 0x0022C6F0U
#define DLUMA_EDITOR_ANIMATION_TAB_CONTROLS_RVA 0x0022D490U
#define DLUMA_EDITOR_ANIMATION_MODEL_INIT_RVA 0x0016FC10U
#define DLUMA_EDITOR_ANIMATION_MODEL_SHUTDOWN_RVA 0x0016FDB0U
#define DLUMA_EDITOR_ANIMATION_MIXER_POINTER_RVA 0x00ACCB98U
#define DLUMA_EDITOR_ANIMATION_CRITICAL_SECTION_RVA 0x00AD6AE8U
#define DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_HANDLER_RVA 0x000B1CB0U
#define DLUMA_EDITOR_ANIMATION_MAIN_FRAME_TITLE_RVA 0x000B23A0U
#define DLUMA_EDITOR_ANIMATION_REFRESH_FILESYSTEM_COMMAND 0x805AU
#define DLUMA_EDITOR_ANIMATION_RELOAD_SELECTED_COMMAND 0x807EU
#define DLUMA_EDITOR_ANIMATION_DISPATCH_MESSAGE (WM_APP + 0x2D1U)
#define DLUMA_EDITOR_ANIMATION_SEND_TIMEOUT_MS 30000U
#define DLUMA_EDITOR_ANIMATION_MAX_SUBSCRIBERS 4096U
#define DLUMA_EDITOR_ANIMATION_MAX_BANKS 65536U
#define DLUMA_EDITOR_ANIMATION_TAB_ANIMATOR_OFFSET 512U
#define DLUMA_EDITOR_ANIMATION_TAB_LIST_HWND_OFFSET 7464U
#define DLUMA_EDITOR_ANIMATION_CACHE_OFFSET 144U
#define DLUMA_EDITOR_ANIMATION_ACTIVE_CACHE_OFFSET 120U
#define DLUMA_EDITOR_ANIMATION_SCRIPT_OFFSET 88U
#define DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET 1040U
#define DLUMA_EDITOR_ANIMATION_MODEL_BIND_OFFSET 1048U
#define DLUMA_EDITOR_ANIMATION_LIST_GETITEMCOUNT 0x1004U
#define DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_PATCH_SIZE 14U
#define DLUMA_EDITOR_ANIMATION_WINDOW_TEXT_CAPACITY 2048U
#define DLUMA_SMTO_BLOCK 0x0001U
#define DLUMA_SMTO_ABORTIFHUNG 0x0002U
#define DLUMA_SMTO_ERRORONEXIT 0x0020U

typedef void (__fastcall *DlumaEditorAnimationReconcileFn)(void* animator);
typedef void (__fastcall *DlumaEditorAnimationClearFn)(
    void* animator, unsigned char preserve);
typedef void* (__fastcall *DlumaEditorAnimationCacheBuildFn)(
    void* cache, void* model);
typedef void (__fastcall *DlumaEditorAnimationCacheEvictFn)(
    void* cache, void* model);
typedef LRESULT (__fastcall *DlumaEditorAnimationTabRowsFn)(void* tab);
typedef void (__fastcall *DlumaEditorAnimationTabUpdateFn)(void* tab);
typedef void* (__fastcall *DlumaEditorAnimationSelectedModelFn)(void* owner);
typedef const char* (__fastcall *DlumaEditorAnimationModelNameFn)(void* model);
typedef unsigned char (__fastcall *DlumaEditorAnimationModelInitFn)(
    void* model, const char* script);
typedef void (__fastcall *DlumaEditorAnimationModelShutdownFn)(void* model);
typedef unsigned char (__cdecl *DlumaEditorAnimationFsExistsFn)(
    const char* path);

typedef struct DlumaEditorAnimationCriticalSection {
    PVOID DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} DlumaEditorAnimationCriticalSection;

__declspec(dllimport) VOID WINAPI EnterCriticalSection(
    DlumaEditorAnimationCriticalSection* critical_section);
__declspec(dllimport) VOID WINAPI LeaveCriticalSection(
    DlumaEditorAnimationCriticalSection* critical_section);

typedef struct DlumaEditorAnimationWindowCandidate {
    DWORD process_id;
    HWND window;
    ULONGLONG area;
} DlumaEditorAnimationWindowCandidate;

typedef struct DlumaEditorAnimationSelection {
    void* singleton;
    void* animator;
    void* tab;
    void* model;
    DlumaEditorAnimationModelInitFn init;
    DlumaEditorAnimationModelShutdownFn shutdown;
    DlumaEditorAnimationModelNameFn model_name;
    char script[DLUMA_ANIMATION_REFRESH_PATH_CAPACITY];
    char model_resource[DLUMA_ANIMATION_REFRESH_IDENTITY_CAPACITY];
} DlumaEditorAnimationSelection;

typedef struct DlumaEditorAnimationBridge {
    volatile LONG profile_state;
    volatile LONG attach_lock;
    volatile LONG transaction_lock;
    volatile LONG pending_state;
    volatile LONG breadcrumb_sequence;
    volatile LONG stock_reload_detour_state;
    volatile LONG discovery_log_state;
    HWND window;
    WNDPROC predecessor;
    DWORD owner_thread_id;
    BYTE* executable;
    BYTE* engine;
    DlumaEditorAnimationReconcileFn reconcile;
    DlumaEditorAnimationClearFn clear_slots;
    DlumaEditorAnimationCacheBuildFn cache_build;
    DlumaEditorAnimationCacheEvictFn cache_evict;
    DlumaEditorAnimationTabRowsFn rebuild_rows;
    DlumaEditorAnimationTabUpdateFn update_controls;
    DlumaEditorAnimationTabUpdateFn update_state;
    DlumaEditorAnimationReloadRequest pending_request;
    DlumaAnimationRefreshManifest pending_manifest;
    DlumaAnimationRefreshResult pending_result;
    DlumaEditorAnimationReloadRequest last_request;
    DlumaAnimationRefreshManifest last_manifest;
    unsigned int has_last_success;
    BYTE stock_reload_original[DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_PATCH_SIZE];
    DlumaEditorAnimationBreadcrumb breadcrumb;
} DlumaEditorAnimationBridge;

static DlumaEditorAnimationBridge g_dluma_editor_animation_bridge;

static const char g_dluma_editor_animation_exe_sha256[] =
    "CACD9C96BA5D8C35A23170DC8BE8029DB9AA5E6223F5E6CD5DE3E61D8E72DA28";
static const char g_dluma_editor_animation_engine_sha256[] =
    "5B8090BFC3D3A25A24DA1846A3AAFD65C97E0D1E700B5DFEBFC7132737B6BFF6";

static const BYTE g_dluma_editor_animation_reconcile_prefix[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0x49,
    0x18,0x48,0x85,0xC9};
static const BYTE g_dluma_editor_animation_clear_prefix[] = {
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x0F,0xB6,
    0xDA,0x48,0x8B,0xF9};
static const BYTE g_dluma_editor_animation_cache_build_prefix[] = {
    0x40,0x57,0x48,0x83,0xEC,0x40,0x48,0xC7,0x44,0x24,0x20,0xFE,
    0xFF,0xFF,0xFF,0x48,0x89,0x5C,0x24,0x58,0x48,0x89,0x6C,0x24};
static const BYTE g_dluma_editor_animation_cache_evict_prefix[] = {
    0x40,0x57,0x48,0x83,0xEC,0x40,0x48,0xC7,0x44,0x24,0x20,0xFE,
    0xFF,0xFF,0xFF,0x48,0x89,0x5C,0x24,0x58,0x48,0x89,0x74,0x24};
static const BYTE g_dluma_editor_animation_rows_prefix[] = {
    0x48,0x89,0x4C,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,0x55,
    0x41,0x56,0x41,0x57};
static const BYTE g_dluma_editor_animation_controls_prefix[] = {
    0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0xC7,0x44,0x24,0x20,0xFE,
    0xFF,0xFF,0xFF,0x48};
static const BYTE g_dluma_editor_animation_state_prefix[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,
    0x74,0x24,0x18,0x48};
static const BYTE g_dluma_editor_animation_callback_prefix[] = {
    0x48,0x83,0xEC,0x28,0x48,0x8B,0xC1,0x48,0x85,0xD2,0x74,0x2F,
    0x48,0x89,0x74,0x24};
static const BYTE g_dluma_editor_animation_init_prefix[] = {
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,
    0x83,0xEC,0x30,0x48};
static const BYTE g_dluma_editor_animation_shutdown_prefix[] = {
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,
    0xB9,0x18,0x04,0x00};
static const BYTE g_dluma_editor_animation_stock_reload_prefix[] = {
    0x48,0x83,0xEC,0x38,0x48,0xC7,0x44,0x24,0x20,0xFE,0xFF,0xFF,
    0xFF,0xFF};
static const BYTE g_dluma_editor_animation_main_frame_title_prefix[] = {
    0x48,0x8B,0xC4,0x55,0x56,0x57,0x48,0x83,0xEC,0x60,0x48,0xC7,
    0x44,0x24,0x50,0xFE};

static void dluma_editor_animation_reload_manual(
    const char* command_source);

static int dluma_editor_animation_prefix_matches(
    const BYTE* address,
    const BYTE* expected,
    SIZE_T size)
{
    return address && expected && size &&
        dluma_is_readable(address, size) &&
        memcmp(address, expected, size) == 0;
}

static int dluma_editor_animation_module_sha256(
    HMODULE module,
    char output[DLUMA_SHA256_HEX_CAPACITY])
{
    WCHAR path[DLUMA_MAX_WPATH];
    output[0] = 0;
    return module &&
        dluma_get_module_path_checked(
            module, path, DLUMA_ARRAY_COUNT(path)) &&
        dluma_sha256_file_hex(
            path, 512ULL * 1024ULL * 1024ULL, output);
}

static void dluma_editor_animation_publish_breadcrumb(
    const char* request_id,
    const char* stage)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    dluma_atomic_increment(&bridge->breadcrumb_sequence);
    bridge->breadcrumb.owner_thread_id = bridge->owner_thread_id;
    dluma_str_copy(
        bridge->breadcrumb.host_profile,
        DLUMA_ARRAY_COUNT(bridge->breadcrumb.host_profile),
        g_dluma_editor_animation_exe_sha256);
    dluma_str_copy(
        bridge->breadcrumb.request_id,
        DLUMA_ARRAY_COUNT(bridge->breadcrumb.request_id),
        request_id ? request_id : "manual-selected-model");
    dluma_str_copy(
        bridge->breadcrumb.last_stage,
        DLUMA_ARRAY_COUNT(bridge->breadcrumb.last_stage),
        stage ? stage : "unknown");
    bridge->breadcrumb.sequence =
        dluma_atomic_increment(&bridge->breadcrumb_sequence);
}

int dluma_editor_animation_bridge_copy_breadcrumb(
    DlumaEditorAnimationBreadcrumb* output)
{
    unsigned int attempt;
    if (!output) return 0;
    memset(output, 0, sizeof(*output));
    for (attempt = 0; attempt < 3U; ++attempt) {
        LONG before = dluma_atomic_compare_exchange(
            &g_dluma_editor_animation_bridge.breadcrumb_sequence, 0, 0);
        LONG after;
        if (before & 1) continue;
        *output = g_dluma_editor_animation_bridge.breadcrumb;
        after = dluma_atomic_compare_exchange(
            &g_dluma_editor_animation_bridge.breadcrumb_sequence, 0, 0);
        if (before == after && !(after & 1))
            return output->last_stage[0] != 0;
    }
    memset(output, 0, sizeof(*output));
    return 0;
}

int dluma_editor_animation_bridge_copy_crash_context(
    char* output,
    unsigned int capacity)
{
    DlumaEditorAnimationBreadcrumb breadcrumb;
    char owner_thread[32];
    if (!output || !capacity) return 0;
    output[0] = 0;
    if (!dluma_editor_animation_bridge_copy_breadcrumb(&breadcrumb))
        return 0;
    owner_thread[0] = 0;
    dluma_u64_to_text(
        breadcrumb.owner_thread_id,
        owner_thread,
        DLUMA_ARRAY_COUNT(owner_thread));
    dluma_str_append(output, capacity, "animation_reload_snapshot profile=");
    dluma_str_append(output, capacity, breadcrumb.host_profile);
    dluma_str_append(output, capacity, " request=");
    dluma_str_append(output, capacity, breadcrumb.request_id);
    dluma_str_append(output, capacity, " owner_thread=");
    dluma_str_append(output, capacity, owner_thread);
    dluma_str_append(output, capacity, " last_stage=");
    dluma_str_append(output, capacity, breadcrumb.last_stage);
    dluma_str_append(output, capacity, "\r\n");
    return output[0] != 0;
}

static int dluma_editor_animation_profile_bind(void)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    const DlumaDualHostRuntimeSnapshot* runtime;
    HMODULE executable;
    HMODULE engine;
    char engine_sha256[DLUMA_SHA256_HEX_CAPACITY];
    LONG state = dluma_atomic_compare_exchange(
        &bridge->profile_state, 1, 0);
    int accepted = 0;
    int retryable = 0;
    if (state == 2) return 1;
    if (state != 0) return 0;
    runtime = dluma_dual_host_runtime_snapshot();
    executable = GetModuleHandleW(0);
    engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    engine_sha256[0] = 0;
    if (!runtime || !runtime->initialized || !executable || !engine) {
        retryable = 1;
        goto done;
    }
    if (runtime->classification.kind != DLUMA_HOST_CHROME_EDITOR ||
        !runtime->classification.exact_profile ||
        runtime->classification.forwarding_only ||
        !runtime->evidence.canonical_image_verified ||
        runtime->evidence.image.timestamp !=
            DLUMA_EDITOR_ANIMATION_EXE_TIMESTAMP ||
        runtime->evidence.image.size_of_image !=
            DLUMA_EDITOR_ANIMATION_EXE_SIZE ||
        dluma_stricmp(
            runtime->evidence.image.sha256,
            g_dluma_editor_animation_exe_sha256) != 0 ||
        !dluma_editor_animation_module_sha256(engine, engine_sha256) ||
        dluma_stricmp(
            engine_sha256,
            g_dluma_editor_animation_engine_sha256) != 0)
        goto done;
    bridge->executable = (BYTE*)executable;
    bridge->engine = (BYTE*)engine;
    if (!dluma_editor_animation_prefix_matches(
            bridge->executable + DLUMA_EDITOR_ANIMATION_RECONCILE_RVA,
            g_dluma_editor_animation_reconcile_prefix,
            sizeof(g_dluma_editor_animation_reconcile_prefix)) ||
        !dluma_editor_animation_prefix_matches(
            bridge->executable + DLUMA_EDITOR_ANIMATION_CLEAR_RVA,
            g_dluma_editor_animation_clear_prefix,
            sizeof(g_dluma_editor_animation_clear_prefix)) ||
        !dluma_editor_animation_prefix_matches(
            bridge->executable + DLUMA_EDITOR_ANIMATION_CACHE_BUILD_RVA,
            g_dluma_editor_animation_cache_build_prefix,
            sizeof(g_dluma_editor_animation_cache_build_prefix)) ||
        !dluma_editor_animation_prefix_matches(
            bridge->executable + DLUMA_EDITOR_ANIMATION_CACHE_EVICT_RVA,
            g_dluma_editor_animation_cache_evict_prefix,
            sizeof(g_dluma_editor_animation_cache_evict_prefix)) ||
        !dluma_editor_animation_prefix_matches(
            bridge->executable + DLUMA_EDITOR_ANIMATION_TAB_ROWS_RVA,
            g_dluma_editor_animation_rows_prefix,
            sizeof(g_dluma_editor_animation_rows_prefix)) ||
        !dluma_editor_animation_prefix_matches(
            bridge->executable + DLUMA_EDITOR_ANIMATION_TAB_CONTROLS_RVA,
            g_dluma_editor_animation_controls_prefix,
            sizeof(g_dluma_editor_animation_controls_prefix)) ||
        !dluma_editor_animation_prefix_matches(
            bridge->executable + DLUMA_EDITOR_ANIMATION_TAB_STATE_RVA,
            g_dluma_editor_animation_state_prefix,
            sizeof(g_dluma_editor_animation_state_prefix)) ||
        !dluma_editor_animation_prefix_matches(
            bridge->executable + DLUMA_EDITOR_ANIMATION_CALLBACK_RVA,
            g_dluma_editor_animation_callback_prefix,
            sizeof(g_dluma_editor_animation_callback_prefix)) ||
        !dluma_editor_animation_prefix_matches(
            bridge->engine + DLUMA_EDITOR_ANIMATION_MODEL_INIT_RVA,
            g_dluma_editor_animation_init_prefix,
            sizeof(g_dluma_editor_animation_init_prefix)) ||
        !dluma_editor_animation_prefix_matches(
            bridge->engine + DLUMA_EDITOR_ANIMATION_MODEL_SHUTDOWN_RVA,
            g_dluma_editor_animation_shutdown_prefix,
            sizeof(g_dluma_editor_animation_shutdown_prefix)) ||
        !dluma_editor_animation_prefix_matches(
            bridge->executable +
                DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_HANDLER_RVA,
            g_dluma_editor_animation_stock_reload_prefix,
            sizeof(g_dluma_editor_animation_stock_reload_prefix)) ||
        !dluma_editor_animation_prefix_matches(
            bridge->executable +
                DLUMA_EDITOR_ANIMATION_MAIN_FRAME_TITLE_RVA,
            g_dluma_editor_animation_main_frame_title_prefix,
            sizeof(g_dluma_editor_animation_main_frame_title_prefix)))
        goto done;
    bridge->reconcile = (DlumaEditorAnimationReconcileFn)(
        bridge->executable + DLUMA_EDITOR_ANIMATION_RECONCILE_RVA);
    bridge->clear_slots = (DlumaEditorAnimationClearFn)(
        bridge->executable + DLUMA_EDITOR_ANIMATION_CLEAR_RVA);
    bridge->cache_build = (DlumaEditorAnimationCacheBuildFn)(
        bridge->executable + DLUMA_EDITOR_ANIMATION_CACHE_BUILD_RVA);
    bridge->cache_evict = (DlumaEditorAnimationCacheEvictFn)(
        bridge->executable + DLUMA_EDITOR_ANIMATION_CACHE_EVICT_RVA);
    bridge->rebuild_rows = (DlumaEditorAnimationTabRowsFn)(
        bridge->executable + DLUMA_EDITOR_ANIMATION_TAB_ROWS_RVA);
    bridge->update_controls = (DlumaEditorAnimationTabUpdateFn)(
        bridge->executable + DLUMA_EDITOR_ANIMATION_TAB_CONTROLS_RVA);
    bridge->update_state = (DlumaEditorAnimationTabUpdateFn)(
        bridge->executable + DLUMA_EDITOR_ANIMATION_TAB_STATE_RVA);
    accepted = 1;
done:
    dluma_atomic_exchange(
        &bridge->profile_state, accepted ? 2 : (retryable ? 0 : -1));
    if (!accepted && !retryable)
        dluma_log_line(
            "WARN",
            "Editor selected-model animation bridge refused an unsupported exact profile.");
    return accepted;
}

static void __fastcall dluma_editor_animation_stock_reload_hook(void)
{
    dluma_editor_animation_reload_manual("shortcut");
}

static int dluma_editor_animation_restore_stock_reload(
    BYTE* address,
    DWORD original_protect)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    DWORD writable_protect = 0;
    DWORD restored_protect = 0;
    if (!address ||
        !VirtualProtect(
            address,
            DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_PATCH_SIZE,
            PAGE_EXECUTE_READWRITE,
            &writable_protect))
        return 0;
    memcpy(
        address,
        bridge->stock_reload_original,
        DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_PATCH_SIZE);
    FlushInstructionCache(
        GetCurrentProcess(),
        address,
        DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_PATCH_SIZE);
    if (!VirtualProtect(
            address,
            DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_PATCH_SIZE,
            original_protect ? original_protect : writable_protect,
            &restored_protect))
        return 0;
    return memcmp(
        address,
        bridge->stock_reload_original,
        DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_PATCH_SIZE) == 0;
}

static int dluma_editor_animation_install_stock_reload_detour(void)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    BYTE patch[DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_PATCH_SIZE];
    BYTE* address;
    ULONGLONG target;
    DWORD old_protect = 0;
    DWORD restored_protect = 0;
    LONG state = dluma_atomic_compare_exchange(
        &bridge->stock_reload_detour_state, 1, 0);
    if (state == 2) return 1;
    if (state != 0) return 0;
    if (!bridge->executable) goto failed;
    address = bridge->executable +
        DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_HANDLER_RVA;
    if (!dluma_editor_animation_prefix_matches(
            address,
            g_dluma_editor_animation_stock_reload_prefix,
            sizeof(g_dluma_editor_animation_stock_reload_prefix)))
        goto failed;
    memcpy(
        bridge->stock_reload_original,
        address,
        DLUMA_EDITOR_ANIMATION_STOCK_RELOAD_PATCH_SIZE);
    memset(patch, 0, sizeof(patch));
    patch[0] = 0xFF;
    patch[1] = 0x25;
    target = (ULONGLONG)(ULONG_PTR)
        dluma_editor_animation_stock_reload_hook;
    memcpy(patch + 6U, &target, sizeof(target));
    if (!VirtualProtect(
            address,
            sizeof(patch),
            PAGE_EXECUTE_READWRITE,
            &old_protect))
        goto failed;
    memcpy(address, patch, sizeof(patch));
    FlushInstructionCache(GetCurrentProcess(), address, sizeof(patch));
    if (!VirtualProtect(
            address,
            sizeof(patch),
            old_protect,
            &restored_protect) ||
        memcmp(address, patch, sizeof(patch)) != 0) {
        (void)dluma_editor_animation_restore_stock_reload(
            address,
            old_protect);
        goto failed;
    }
    dluma_atomic_exchange(&bridge->stock_reload_detour_state, 2);
    dluma_log_line(
        "INFO",
        "Exact Editor Shift+Alt+A reload handler intercepted; stock global animation invalidation cannot run.");
    return 1;
failed:
    dluma_atomic_exchange(&bridge->stock_reload_detour_state, -1);
    dluma_log_pair(
        "WARN",
        "editor_selected_model_animation_command_interception",
        "exact_reload_handler_detour_failed");
    return 0;
}

static int dluma_editor_animation_copy_readable_string(
    const char* source,
    char* output,
    unsigned int capacity)
{
    unsigned int index;
    if (!source || !output || capacity < 2U) return 0;
    output[0] = 0;
    for (index = 0; index + 1U < capacity; ++index) {
        char value;
        if (!dluma_is_readable(source + index, 1U)) return 0;
        value = source[index];
        output[index] = value;
        if (!value) return index != 0U;
    }
    output[0] = 0;
    return 0;
}

static int dluma_editor_animation_stem(
    const char* value,
    char* output,
    unsigned int capacity)
{
    const char* start;
    const char* cursor;
    unsigned int length = 0;
    if (!value || !value[0] || !output || capacity < 2U) return 0;
    start = value;
    for (cursor = value; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\') start = cursor + 1;
    while (start[length] && start[length] != '.') {
        if (length + 1U >= capacity) return 0;
        output[length] = start[length];
        ++length;
    }
    output[length] = 0;
    return length != 0U;
}

static int dluma_editor_animation_stem_matches(
    const char* value,
    const char* expected)
{
    char left[DLUMA_ANIMATION_REFRESH_IDENTITY_CAPACITY];
    char right[DLUMA_ANIMATION_REFRESH_IDENTITY_CAPACITY];
    return dluma_editor_animation_stem(
               value, left, DLUMA_ARRAY_COUNT(left)) &&
        dluma_editor_animation_stem(
            expected, right, DLUMA_ARRAY_COUNT(right)) &&
        dluma_stricmp(left, right) == 0;
}

static int dluma_editor_animation_address_in_image(
    const void* address,
    const BYTE* base,
    SIZE_T size)
{
    const BYTE* value = (const BYTE*)address;
    return value && base && value >= base &&
        (SIZE_T)(value - base) < size;
}

static int dluma_editor_animation_objects(
    void** singleton_out,
    void** animator_out)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    void* singleton = 0;
    if (!singleton_out || !animator_out || !bridge->executable ||
        !dluma_is_readable(
            bridge->executable +
                DLUMA_EDITOR_ANIMATION_SINGLETON_RVA,
            sizeof(singleton)))
        return 0;
    memcpy(
        &singleton,
        bridge->executable + DLUMA_EDITOR_ANIMATION_SINGLETON_RVA,
        sizeof(singleton));
    if (!singleton ||
        !dluma_is_readable(
            singleton,
            DLUMA_EDITOR_ANIMATION_ANIMATOR_OFFSET + 256U))
        return 0;
    *singleton_out = singleton;
    *animator_out =
        (BYTE*)singleton + DLUMA_EDITOR_ANIMATION_ANIMATOR_OFFSET;
    return 1;
}

static void* dluma_editor_animation_selected_model(void* animator)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    BYTE* owner;
    void** vtable;
    DlumaEditorAnimationSelectedModelFn selected;
    if (!animator) return 0;
    owner = (BYTE*)animator + 16U;
    if (!dluma_is_readable(owner, sizeof(vtable))) return 0;
    vtable = *(void***)owner;
    if (!vtable || !dluma_is_readable(
            (BYTE*)vtable + 32U, sizeof(selected)))
        return 0;
    selected = *(DlumaEditorAnimationSelectedModelFn*)((BYTE*)vtable + 32U);
    if (!dluma_editor_animation_address_in_image(
            (const void*)selected,
            bridge->executable,
            DLUMA_EDITOR_ANIMATION_EXE_SIZE) &&
        !dluma_editor_animation_address_in_image(
            (const void*)selected,
            bridge->engine,
            DLUMA_EDITOR_ANIMATION_ENGINE_SIZE))
        return 0;
    return selected(owner);
}

static int dluma_editor_animation_model_contract(
    void* model,
    DlumaEditorAnimationModelInitFn* init_out,
    DlumaEditorAnimationModelShutdownFn* shutdown_out,
    DlumaEditorAnimationModelNameFn* name_out)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    void** vtable;
    DlumaEditorAnimationModelInitFn init;
    DlumaEditorAnimationModelShutdownFn shutdown;
    DlumaEditorAnimationModelNameFn name;
    if (!model || !init_out || !shutdown_out || !name_out ||
        !dluma_is_readable(model, sizeof(vtable)))
        return 0;
    vtable = *(void***)model;
    if (!vtable ||
        !dluma_is_readable((BYTE*)vtable + 2160U, sizeof(name)))
        return 0;
    init = *(DlumaEditorAnimationModelInitFn*)((BYTE*)vtable + 1880U);
    shutdown = *(DlumaEditorAnimationModelShutdownFn*)(
        (BYTE*)vtable + 1888U);
    name = *(DlumaEditorAnimationModelNameFn*)((BYTE*)vtable + 2160U);
    if ((const BYTE*)init !=
            bridge->engine + DLUMA_EDITOR_ANIMATION_MODEL_INIT_RVA ||
        (const BYTE*)shutdown !=
            bridge->engine + DLUMA_EDITOR_ANIMATION_MODEL_SHUTDOWN_RVA ||
        !dluma_editor_animation_address_in_image(
            (const void*)name,
            bridge->engine,
            DLUMA_EDITOR_ANIMATION_ENGINE_SIZE))
        return 0;
    *init_out = init;
    *shutdown_out = shutdown;
    *name_out = name;
    return 1;
}

static void* dluma_editor_animation_find_tab(
    void* singleton,
    void* animator)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    void** begin;
    void** end;
    SIZE_T count;
    SIZE_T index;
    if (!singleton || !animator ||
        !dluma_is_readable(
            (BYTE*)singleton + DLUMA_EDITOR_ANIMATION_SUBSCRIBERS_BEGIN,
            2U * sizeof(void*)))
        return 0;
    begin = *(void***)((BYTE*)singleton +
        DLUMA_EDITOR_ANIMATION_SUBSCRIBERS_BEGIN);
    end = *(void***)((BYTE*)singleton +
        DLUMA_EDITOR_ANIMATION_SUBSCRIBERS_END);
    if (!begin || !end || end < begin) return 0;
    count = (SIZE_T)(end - begin);
    if (!count || count > DLUMA_EDITOR_ANIMATION_MAX_SUBSCRIBERS ||
        !dluma_is_readable(begin, count * sizeof(void*)))
        return 0;
    for (index = 0; index < count; ++index) {
        void** record = (void**)begin[index];
        void* tab;
        HWND list;
        if (!record || !dluma_is_readable(record, 4U * sizeof(void*)) ||
            record[2] !=
                bridge->executable + DLUMA_EDITOR_ANIMATION_CALLBACK_RVA)
            continue;
        tab = record[1];
        if (!tab ||
            !dluma_is_readable(
                tab,
                DLUMA_EDITOR_ANIMATION_TAB_LIST_HWND_OFFSET +
                    sizeof(HWND)) ||
            *(void**)((BYTE*)tab +
                DLUMA_EDITOR_ANIMATION_TAB_ANIMATOR_OFFSET) != animator)
            continue;
        list = *(HWND*)((BYTE*)tab +
            DLUMA_EDITOR_ANIMATION_TAB_LIST_HWND_OFFSET);
        if (list && IsWindow(list)) return tab;
    }
    return 0;
}

static int dluma_editor_animation_resolve_selection(
    const DlumaAnimationRefreshManifest* manifest,
    const char* expected_model,
    const char* expected_script,
    DlumaEditorAnimationSelection* selection)
{
    const char* script_pointer = 0;
    const char* model_name_pointer;
    if (!selection) return 0;
    memset(selection, 0, sizeof(*selection));
    if (!dluma_editor_animation_objects(
            &selection->singleton, &selection->animator))
        return 0;
    selection->model =
        dluma_editor_animation_selected_model(selection->animator);
    if (!selection->model ||
        !dluma_editor_animation_model_contract(
            selection->model,
            &selection->init,
            &selection->shutdown,
            &selection->model_name))
        return 0;
    selection->tab = dluma_editor_animation_find_tab(
        selection->singleton, selection->animator);
    if (!selection->tab ||
        !dluma_is_readable(
            (BYTE*)selection->animator +
                DLUMA_EDITOR_ANIMATION_SCRIPT_OFFSET,
            sizeof(script_pointer)))
        return 0;
    script_pointer = *(const char**)((BYTE*)selection->animator +
        DLUMA_EDITOR_ANIMATION_SCRIPT_OFFSET);
    model_name_pointer = selection->model_name(selection->model);
    if (!dluma_editor_animation_copy_readable_string(
            model_name_pointer,
            selection->model_resource,
            DLUMA_ARRAY_COUNT(selection->model_resource)))
        return 0;
    if (!dluma_editor_animation_copy_readable_string(
            script_pointer,
            selection->script,
            DLUMA_ARRAY_COUNT(selection->script))) {
        if (!manifest || !manifest->animation_library_name[0])
            return 0;
        selection->script[0] = 0;
        dluma_str_append(
            selection->script,
            DLUMA_ARRAY_COUNT(selection->script),
            manifest->animation_library_name);
        dluma_str_append(
            selection->script,
            DLUMA_ARRAY_COUNT(selection->script),
            ".scr");
    }
    if (manifest &&
        (!dluma_editor_animation_stem_matches(
             selection->model_resource,
             manifest->model_resource_name) ||
         (!dluma_editor_animation_stem_matches(
              selection->script,
              manifest->animation_library_name) &&
          !dluma_editor_animation_stem_matches(
              selection->script,
              manifest->model_resource_name))))
        return 0;
    if (expected_model && expected_model[0] &&
        !dluma_editor_animation_stem_matches(
            selection->model_resource, expected_model))
        return 0;
    if (expected_script && expected_script[0] &&
        !dluma_editor_animation_stem_matches(
            selection->script, expected_script))
        return 0;
    return 1;
}

static int dluma_editor_animation_mixer_objects(
    void** mixer_out,
    DlumaEditorAnimationCriticalSection** critical_section_out)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    void* mixer = 0;
    DlumaEditorAnimationCriticalSection* critical_section;
    if (!mixer_out || !critical_section_out || !bridge->engine ||
        !dluma_is_readable(
            bridge->engine + DLUMA_EDITOR_ANIMATION_MIXER_POINTER_RVA,
            sizeof(mixer)))
        return 0;
    memcpy(
        &mixer,
        bridge->engine + DLUMA_EDITOR_ANIMATION_MIXER_POINTER_RVA,
        sizeof(mixer));
    critical_section = (DlumaEditorAnimationCriticalSection*)(
        bridge->engine + DLUMA_EDITOR_ANIMATION_CRITICAL_SECTION_RVA);
    if (!mixer || !dluma_is_readable(mixer, 32U) ||
        !dluma_is_readable(critical_section, sizeof(*critical_section)))
        return 0;
    *mixer_out = mixer;
    *critical_section_out = critical_section;
    return 1;
}

static int dluma_editor_animation_bank_entry_locked(
    void* mixer,
    void* bank,
    uint32_t* reference_count,
    uint32_t* occurrences)
{
    void* entries;
    uint32_t count;
    uint32_t index;
    if (!mixer || !bank || !reference_count || !occurrences)
        return 0;
    *reference_count = 0;
    *occurrences = 0;
    entries = *(void**)((BYTE*)mixer + 16U);
    count = *(uint32_t*)((BYTE*)mixer + 24U);
    if (count > DLUMA_EDITOR_ANIMATION_MAX_BANKS ||
        (count && (!entries ||
         !dluma_is_readable(entries, (SIZE_T)count * 16U))))
        return 0;
    for (index = 0; index < count; ++index) {
        BYTE* entry = (BYTE*)entries + (SIZE_T)index * 16U;
        if (*(void**)entry != bank) continue;
        *reference_count = *(uint32_t*)(entry + 8U);
        ++*occurrences;
    }
    return 1;
}

static int dluma_editor_animation_bank_is_exclusive(void* model)
{
    void* bank;
    void* mixer = 0;
    DlumaEditorAnimationCriticalSection* critical_section = 0;
    uint32_t reference_count = 0;
    uint32_t occurrences = 0;
    int exclusive = 0;
    if (!model ||
        !dluma_is_readable(
            (BYTE*)model + DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET,
            sizeof(bank)))
        return 0;
    bank = *(void**)((BYTE*)model +
        DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET);
    if (!bank) return 1;
    if (!dluma_editor_animation_mixer_objects(
            &mixer, &critical_section))
        return 0;
    EnterCriticalSection(critical_section);
    if (dluma_editor_animation_bank_entry_locked(
            mixer, bank, &reference_count, &occurrences))
        exclusive = occurrences == 1U && reference_count == 1U;
    LeaveCriticalSection(critical_section);
    return exclusive;
}

static int dluma_editor_animation_read_compiled_mode(
    unsigned char* compiled_mode)
{
    void* mixer = 0;
    DlumaEditorAnimationCriticalSection* critical_section = 0;
    if (!compiled_mode ||
        !dluma_editor_animation_mixer_objects(
            &mixer, &critical_section))
        return 0;
    EnterCriticalSection(critical_section);
    *compiled_mode = *((unsigned char*)mixer + 8U);
    LeaveCriticalSection(critical_section);
    return 1;
}

static int dluma_editor_animation_init_with_mode(
    void* model,
    const char* script,
    DlumaEditorAnimationModelInitFn init,
    unsigned char requested_mode)
{
    void* mixer = 0;
    DlumaEditorAnimationCriticalSection* critical_section = 0;
    unsigned char compiled_mode = 0;
    int entered = 0;
    int result = 0;
    if (!model || !script || !init ||
        !dluma_editor_animation_mixer_objects(
            &mixer, &critical_section))
        return 0;
    __try {
        EnterCriticalSection(critical_section);
        entered = 1;
        compiled_mode = *((unsigned char*)mixer + 8U);
        *((unsigned char*)mixer + 8U) = requested_mode;
        result = init(model, script) != 0;
    }
    __finally {
        if (entered) {
            *((unsigned char*)mixer + 8U) = compiled_mode;
            LeaveCriticalSection(critical_section);
        }
    }
    return result;
}

static int dluma_editor_animation_shutdown_selected_bank(
    void* model,
    void* bank,
    DlumaEditorAnimationModelShutdownFn shutdown)
{
    void* mixer = 0;
    DlumaEditorAnimationCriticalSection* critical_section = 0;
    uint32_t reference_count = 0;
    uint32_t occurrences = 0;
    unsigned char release_mode = 0;
    int entered = 0;
    int shutdown_called = 0;
    int result = 0;
    if (!model || !shutdown) return 0;
    if (!bank) {
        shutdown(model);
        return *(void**)((BYTE*)model +
            DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET) == 0;
    }
    if (!dluma_editor_animation_mixer_objects(
            &mixer, &critical_section))
        return 0;
    __try {
        EnterCriticalSection(critical_section);
        entered = 1;
        release_mode = *((unsigned char*)mixer + 9U);
        if (*(void**)((BYTE*)model +
                DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET) != bank ||
            !dluma_editor_animation_bank_entry_locked(
                mixer, bank, &reference_count, &occurrences) ||
            occurrences != 1U || reference_count != 1U)
            __leave;
        *((unsigned char*)mixer + 9U) = 1U;
        shutdown_called = 1;
        shutdown(model);
        if (*(void**)((BYTE*)model +
                DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET) != 0 ||
            !dluma_editor_animation_bank_entry_locked(
                mixer, bank, &reference_count, &occurrences) ||
            occurrences != 0U)
            __leave;
        result = 1;
    }
    __finally {
        if (entered) {
            *((unsigned char*)mixer + 9U) = release_mode;
            LeaveCriticalSection(critical_section);
        }
    }
    return result ? 1 : (shutdown_called ? -1 : 0);
}

static int dluma_editor_animation_bind_fs_exists(
    DlumaEditorAnimationFsExistsFn* output)
{
    static const char export_name[] = "?exists@fs@@YA_NPEBD@Z";
    static const BYTE expected_prefix[24] = {
        0x48,0x83,0xEC,0x78,0x48,0x8B,0xD1,0x48,
        0x8D,0x4C,0x24,0x20,0x41,0xB0,0x01,0xE8,
        0x4C,0xF3,0x00,0x00,0x44,0x8B,0x5C,0x24
    };
    HMODULE filesystem;
    BYTE* target;
    if (!output) return 0;
    *output = 0;
    filesystem = GetModuleHandleW(L"filesystem_x64_rwdi.dll");
    target = filesystem
        ? (BYTE*)GetProcAddress(filesystem, export_name) : 0;
    if (!target ||
        !dluma_editor_animation_prefix_matches(
            target, expected_prefix, sizeof(expected_prefix)))
        return 0;
    *output = (DlumaEditorAnimationFsExistsFn)target;
    return 1;
}

static int dluma_editor_animation_check_visibility(
    const DlumaAnimationRefreshManifest* manifest,
    DlumaAnimationRefreshResult* result)
{
    DlumaEditorAnimationFsExistsFn exists;
    unsigned int alias_seen = 0;
    unsigned int script_seen = 0;
    unsigned int animations_seen = 0;
    unsigned int alias_visible = 1;
    unsigned int script_visible = 1;
    unsigned int animations_visible = 1;
    unsigned int index;
    if (!manifest || !result ||
        !dluma_editor_animation_bind_fs_exists(&exists))
        return 0;
    for (index = 0; index < manifest->artifact_count; ++index) {
        const DlumaAnimationRefreshArtifact* artifact =
            &manifest->artifacts[index];
        switch (artifact->role) {
        case DLUMA_ANIMATION_REFRESH_ARTIFACT_ALIAS_SCRIPT:
            alias_seen = 1U;
            if (!exists(artifact->relative_path)) alias_visible = 0U;
            break;
        case DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_SCRIPT:
            script_seen = 1U;
            if (!exists(artifact->relative_path)) script_visible = 0U;
            break;
        case DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION:
            animations_seen = 1U;
            if (!exists(artifact->relative_path)) animations_visible = 0U;
            break;
        default:
            break;
        }
    }
    result->source_visible = alias_seen && alias_visible
        ? DLUMA_ANIMATION_REFRESH_STAGE_PASSED
        : DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
    result->script_visible = script_seen && script_visible
        ? DLUMA_ANIMATION_REFRESH_STAGE_PASSED
        : DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
    result->animation_files_visible =
        animations_seen && animations_visible
        ? DLUMA_ANIMATION_REFRESH_STAGE_PASSED
        : DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
    return alias_seen && alias_visible &&
        script_seen && script_visible &&
        animations_seen && animations_visible;
}

static int dluma_editor_animation_refresh_filesystem(void)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    if (!bridge->window || !bridge->predecessor ||
        GetCurrentThreadId() != bridge->owner_thread_id ||
        !IsWindow(bridge->window))
        return 0;
    (void)CallWindowProcW(
        bridge->predecessor,
        bridge->window,
        WM_COMMAND,
        (WPARAM)DLUMA_EDITOR_ANIMATION_REFRESH_FILESYSTEM_COMMAND,
        0);
    return IsWindow(bridge->window);
}

static int dluma_editor_animation_runtime_pack_matches(
    const DlumaEditorAnimationReloadRequest* request)
{
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    const DlumaAnimationRefreshArtifact* pack = 0;
    unsigned int index;
    if (!request ||
        request->route != DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK ||
        !request->manifest || !request->runtime_pack_path[0] ||
        !dluma_project_regular_file_is_contained(
            request->project_root, request->runtime_pack_path))
        return 0;
    for (index = 0; index < request->manifest->artifact_count; ++index) {
        const DlumaAnimationRefreshArtifact* artifact =
            &request->manifest->artifacts[index];
        if (artifact->role !=
            DLUMA_ANIMATION_REFRESH_ARTIFACT_ANIMATION_RUNTIME_PACK)
            continue;
        if (pack) return 0;
        pack = artifact;
    }
    sha256[0] = 0;
    return pack &&
        dluma_sha256_file_hex(
            request->runtime_pack_path,
            1024ULL * 1024ULL * 1024ULL,
            sha256) &&
        dluma_stricmp(sha256, pack->sha256) == 0;
}

static void dluma_editor_animation_remember_success(
    const DlumaEditorAnimationReloadRequest* request)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    if (!request || !request->manifest) return;
    bridge->last_request = *request;
    bridge->last_manifest = *request->manifest;
    bridge->last_request.manifest = &bridge->last_manifest;
    bridge->has_last_success = 1U;
}

static uint32_t dluma_editor_animation_rebuild_cache(
    void* animator,
    void* model)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    void* cache;
    uint32_t count;
    if (!animator || !model) return 0;
    bridge->cache_evict(
        (BYTE*)animator + DLUMA_EDITOR_ANIMATION_CACHE_OFFSET,
        model);
    cache = bridge->cache_build(
        (BYTE*)animator + DLUMA_EDITOR_ANIMATION_CACHE_OFFSET,
        model);
    *(void**)((BYTE*)animator +
        DLUMA_EDITOR_ANIMATION_ACTIVE_CACHE_OFFSET) = cache;
    if (!cache || !dluma_is_readable(cache, 20U)) return 0;
    count = *(uint32_t*)((BYTE*)cache + 16U);
    return count;
}

static int dluma_editor_animation_rebuild_inspector(
    void* tab,
    uint32_t* row_count)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    HWND list;
    LRESULT count;
    if (!tab || !row_count ||
        !dluma_is_readable(
            tab,
            DLUMA_EDITOR_ANIMATION_TAB_LIST_HWND_OFFSET + sizeof(HWND)))
        return 0;
    list = *(HWND*)((BYTE*)tab +
        DLUMA_EDITOR_ANIMATION_TAB_LIST_HWND_OFFSET);
    if (!list || !IsWindow(list)) return 0;
    bridge->rebuild_rows(tab);
    bridge->update_controls(tab);
    bridge->update_state(tab);
    count = SendMessageA(
        list, DLUMA_EDITOR_ANIMATION_LIST_GETITEMCOUNT, 0, 0);
    if (count < 0 || (ULONGLONG)count > UINT32_MAX) return 0;
    *row_count = (uint32_t)count;
    return 1;
}

static int dluma_editor_animation_refuse(
    DlumaAnimationRefreshResult* result,
    DlumaAnimationRefreshRefusal refusal)
{
    if (result) {
        result->primary_refusal = refusal;
        result->refusal = refusal;
        result->accepted = 0U;
    }
    return 0;
}

static int dluma_editor_animation_reload_ui(
    const DlumaEditorAnimationReloadRequest* request,
    DlumaAnimationRefreshResult* result)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    const DlumaAnimationRefreshManifest* manifest;
    const char* request_id;
    DlumaEditorAnimationSelection selection;
    DlumaEditorAnimationSelection refreshed;
    void* animator = 0;
    void* tab = 0;
    void* model = 0;
    void* bank = 0;
    void* bind = 0;
    char script[DLUMA_ANIMATION_REFRESH_PATH_CAPACITY];
    char model_name[DLUMA_ANIMATION_REFRESH_IDENTITY_CAPACITY];
    DlumaEditorAnimationModelInitFn init = 0;
    DlumaEditorAnimationModelShutdownFn shutdown = 0;
    DlumaAnimationPackSwap swap;
    DlumaAnimationRefreshRefusal failure =
        DLUMA_ANIMATION_REFRESH_REFUSAL_NONE;
    uint32_t cache_count = 0;
    uint32_t row_count = 0;
    unsigned char previous_compiled_mode = 1U;
    int had_bank = 0;
    int shutdown_done = 0;
    int shutdown_partial = 0;
    int shutdown_status = 0;
    int init_attempted = 0;
    int initialized = 0;
    int model_resolved_after_refresh = 0;
    int pack_committed = 0;
    int restore_ok = 1;
    int pack_rollback_ok = 1;
    if (!request || !result)
        return dluma_editor_animation_refuse(
            result,
            DLUMA_ANIMATION_REFRESH_REFUSAL_REQUEST_INVALID);
    if (dluma_atomic_compare_exchange(
            &bridge->transaction_lock, 1, 0) != 0)
        return dluma_editor_animation_refuse(
            result,
            DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED);
    memset(&swap, 0, sizeof(swap));
    memset(&selection, 0, sizeof(selection));
    memset(&refreshed, 0, sizeof(refreshed));
    memset(script, 0, sizeof(script));
    memset(model_name, 0, sizeof(model_name));
    manifest = request->manifest;
    request_id = request->request_id[0]
        ? request->request_id : "manual-selected-model";
    dluma_editor_animation_publish_breadcrumb(request_id, "resolve_selected_model");
    if (GetCurrentThreadId() != bridge->owner_thread_id ||
        !dluma_editor_animation_profile_bind()) {
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED;
        goto failed;
    }
    if (!dluma_editor_animation_resolve_selection(
            manifest, 0, 0, &selection)) {
        result->selected_model_match =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_SELECTED_MODEL_MISMATCH;
        goto failed;
    }
    animator = selection.animator;
    tab = selection.tab;
    model = selection.model;
    init = selection.init;
    shutdown = selection.shutdown;
    dluma_str_copy(script, DLUMA_ARRAY_COUNT(script), selection.script);
    dluma_str_copy(
        model_name, DLUMA_ARRAY_COUNT(model_name),
        selection.model_resource);
    result->selected_model_match =
        DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    if (!dluma_is_readable(
            (BYTE*)model + DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET,
            2U * sizeof(void*))) {
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_SELECTED_MODEL_MISMATCH;
        goto failed;
    }
    bank = *(void**)((BYTE*)model +
        DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET);
    had_bank = bank != 0;
    if (!dluma_editor_animation_read_compiled_mode(
            &previous_compiled_mode)) {
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_SCRIPT_BANK_RESOLUTION_FAILED;
        goto failed;
    }
    if (manifest && request->route ==
            DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK &&
        !dluma_editor_animation_runtime_pack_matches(request)) {
        result->pack_registration =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        result->fallback_mount =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_PACK_REGISTRATION_FAILED;
        goto failed;
    }
    if (!dluma_editor_animation_bank_is_exclusive(model)) {
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_BANK_STILL_REFERENCED;
        goto failed;
    }

    dluma_editor_animation_publish_breadcrumb(request_id, "selected_bank_shutdown");
    bridge->reconcile(animator);
    bridge->clear_slots(animator, 0);
    shutdown_status = dluma_editor_animation_shutdown_selected_bank(
        model, bank, shutdown);
    shutdown_partial = shutdown_status < 0;
    shutdown_done = shutdown_status != 0;
    if (shutdown_status != 1) {
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_BANK_STILL_REFERENCED;
        goto failed;
    }
    shutdown_done = 1;

    dluma_editor_animation_publish_breadcrumb(request_id, "filesystem_refresh");
    if (!dluma_editor_animation_refresh_filesystem()) {
        result->filesystem_refresh =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_FILESYSTEM_REFRESH_FAILED;
        goto failed;
    }
    result->filesystem_refresh =
        DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    if (manifest &&
        !dluma_editor_animation_check_visibility(manifest, result)) {
        if (result->source_visible != DLUMA_ANIMATION_REFRESH_STAGE_PASSED)
            failure = DLUMA_ANIMATION_REFRESH_REFUSAL_SOURCE_NOT_VISIBLE;
        else if (result->script_visible !=
                 DLUMA_ANIMATION_REFRESH_STAGE_PASSED)
            failure = DLUMA_ANIMATION_REFRESH_REFUSAL_SCRIPT_NOT_VISIBLE;
        else
            failure = DLUMA_ANIMATION_REFRESH_REFUSAL_ANIMATION_NOT_VISIBLE;
        goto failed;
    }

    if (request->route ==
            DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK &&
        manifest) {
        dluma_editor_animation_publish_breadcrumb(
            request_id, "animation_runtime_pack_registration");
        if (!request->runtime_pack_path[0] ||
            !dluma_animation_pack_runtime_swap(
                request->runtime_pack_path, &swap)) {
            result->pack_registration =
                DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
            result->fallback_mount =
                DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
            if (swap.rollback_attempted)
                result->rollback = swap.rollback_succeeded
                    ? DLUMA_ANIMATION_REFRESH_STAGE_PASSED
                    : DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
            failure = swap.rollback_attempted &&
                !swap.rollback_succeeded
                ? DLUMA_ANIMATION_REFRESH_REFUSAL_ROLLBACK_FAILED
                : DLUMA_ANIMATION_REFRESH_REFUSAL_PACK_REGISTRATION_FAILED;
            goto failed;
        }
        pack_committed = swap.changed != 0U;
        result->pack_registration =
            DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
        result->fallback_mount =
            DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    } else if (request->route !=
                   DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE &&
               request->route !=
                   DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK) {
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_ROUTE_NOT_ALLOWED;
        goto failed;
    }

    dluma_editor_animation_publish_breadcrumb(
        request_id, "selected_model_reresolve");
    if (!dluma_editor_animation_resolve_selection(
            manifest, model_name, script, &refreshed)) {
        result->selected_model_match =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_SELECTED_MODEL_MISMATCH;
        goto failed;
    }
    model_resolved_after_refresh = 1;
    animator = refreshed.animator;
    tab = refreshed.tab;
    model = refreshed.model;
    init = refreshed.init;
    shutdown = refreshed.shutdown;
    bridge->reconcile(animator);
    bridge->clear_slots(animator, 0);
    if (!dluma_is_readable(
            (BYTE*)model + DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET,
            2U * sizeof(void*)) ||
        *(void**)((BYTE*)model +
            DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET) != 0) {
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_BANK_STILL_REFERENCED;
        goto failed;
    }

    dluma_editor_animation_publish_breadcrumb(request_id, "model_anim_init");
    init_attempted = 1;
    initialized = dluma_editor_animation_init_with_mode(
        model, script, init,
        request->route == DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE
            ? 0U : 1U);
    if (!initialized) {
        result->script_bank_resolution =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_SCRIPT_BANK_RESOLUTION_FAILED;
        goto failed;
    }
    result->reload = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    bank = *(void**)((BYTE*)model +
        DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET);
    if (!bank) {
        result->script_bank_resolution =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_SCRIPT_BANK_RESOLUTION_FAILED;
        goto failed;
    }
    result->script_bank_resolution =
        DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    bind = *(void**)((BYTE*)model +
        DLUMA_EDITOR_ANIMATION_MODEL_BIND_OFFSET);
    if (!bind) {
        result->bind_creation =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_BIND_CREATION_FAILED;
        goto failed;
    }
    result->bind_creation = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    result->rebind = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;

    dluma_editor_animation_publish_breadcrumb(request_id, "animation_cache_rebuild");
    cache_count = dluma_editor_animation_rebuild_cache(animator, model);
    result->animation_name_count = cache_count;
    if (!cache_count) {
        result->clip_resolution =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_CLIP_RESOLUTION_FAILED;
        goto failed;
    }
    result->clip_resolution = DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    dluma_editor_animation_publish_breadcrumb(request_id, "inspector_rows_rebuild");
    if (!dluma_editor_animation_rebuild_inspector(tab, &row_count) ||
        !row_count) {
        result->inspector_rebuild =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        failure =
            DLUMA_ANIMATION_REFRESH_REFUSAL_INSPECTOR_REBUILD_FAILED;
        goto failed;
    }
    result->inspector_rebuild =
        DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    result->accepted = 1U;
    result->primary_refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_NONE;
    result->refusal = DLUMA_ANIMATION_REFRESH_REFUSAL_NONE;
    dluma_editor_animation_remember_success(request);
    dluma_editor_animation_publish_breadcrumb(request_id, "completed");
    dluma_atomic_exchange(&bridge->transaction_lock, 0);
    return 1;

failed:
    if (shutdown_done) {
        int current_cleanup_ok = 1;
        void* current_bank = 0;
        dluma_editor_animation_publish_breadcrumb(request_id, "rollback");
        if (shutdown_partial) {
            /* AnimShutdown crossed the mutation boundary but its exact
             * selected-bank postconditions did not hold. A second shutdown,
             * pack swap, or AnimInit would operate on uncertain ownership and
             * can turn a bounded refusal into heap corruption. */
            result->rollback = DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
            failure = DLUMA_ANIMATION_REFRESH_REFUSAL_ROLLBACK_FAILED;
            goto rollback_finished;
        }
        if (!model_resolved_after_refresh &&
            dluma_editor_animation_resolve_selection(
                manifest, model_name, script, &refreshed)) {
            model_resolved_after_refresh = 1;
            animator = refreshed.animator;
            tab = refreshed.tab;
            model = refreshed.model;
            init = refreshed.init;
            shutdown = refreshed.shutdown;
        }
        if (model_resolved_after_refresh && init_attempted) {
            if (!dluma_is_readable(
                    (BYTE*)model +
                        DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET,
                    sizeof(current_bank))) {
                current_cleanup_ok = 0;
            } else {
                current_bank = *(void**)((BYTE*)model +
                    DLUMA_EDITOR_ANIMATION_MODEL_BANK_OFFSET);
                current_cleanup_ok =
                    dluma_editor_animation_shutdown_selected_bank(
                        model, current_bank, shutdown) == 1;
            }
        }
        if (pack_committed && current_cleanup_ok)
            pack_rollback_ok =
                dluma_animation_pack_runtime_rollback(&swap);
        else if (pack_committed)
            pack_rollback_ok = 0;
        if (had_bank) {
            restore_ok = model_resolved_after_refresh &&
                current_cleanup_ok && pack_rollback_ok &&
                dluma_editor_animation_init_with_mode(
                    model, script, init, previous_compiled_mode);
            if (restore_ok) {
                (void)dluma_editor_animation_rebuild_cache(animator, model);
                (void)dluma_editor_animation_rebuild_inspector(
                    tab, &row_count);
            }
        }
        result->rollback = restore_ok && pack_rollback_ok
            ? DLUMA_ANIMATION_REFRESH_STAGE_PASSED
            : DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        if (!restore_ok || !pack_rollback_ok)
            failure = DLUMA_ANIMATION_REFRESH_REFUSAL_ROLLBACK_FAILED;
    }
rollback_finished:
    dluma_editor_animation_publish_breadcrumb(
        request_id,
        failure == DLUMA_ANIMATION_REFRESH_REFUSAL_ROLLBACK_FAILED
            ? "rollback_failed" : "refused");
    dluma_atomic_exchange(&bridge->transaction_lock, 0);
    return dluma_editor_animation_refuse(result, failure);
}

static DlumaAnimationRefreshRoute
dluma_editor_animation_configured_route(void)
{
    WCHAR value[64];
    value[0] = 0;
    GetPrivateProfileStringW(
        L"AnimationRefresh",
        L"Route",
        L"ProjectRPack",
        value,
        DLUMA_ARRAY_COUNT(value),
        g_dluma.config_path);
    if (dluma_wstricmp(value, L"ProjectRPack") == 0 ||
        dluma_wstricmp(value, L"ProjectRPackFallback") == 0)
        return DLUMA_ANIMATION_REFRESH_ROUTE_PROJECT_RPACK;
    if (dluma_wstricmp(value, L"RawLoose") == 0 ||
        dluma_wstricmp(value, L"LooseOnly") == 0)
        return DLUMA_ANIMATION_REFRESH_ROUTE_RAW_LOOSE;
    return (DlumaAnimationRefreshRoute)0;
}

static void dluma_editor_animation_reload_manual(
    const char* command_source)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    DlumaEditorAnimationReloadRequest request;
    DlumaAnimationRefreshResult result;
    DlumaAnimationRefreshRoute configured_route;
    WCHAR project_root[DLUMA_MAX_WPATH];
    char map_name[DLUMA_MAX_MAP_NAME];
    memset(project_root, 0, sizeof(project_root));
    memset(map_name, 0, sizeof(map_name));
    memset(&request, 0, sizeof(request));
    memset(&result, 0, sizeof(result));
    dluma_log_pair(
        "INFO",
        "editor_selected_model_animation_command_source",
        command_source ? command_source : "unknown");
    configured_route = dluma_editor_animation_configured_route();
    if (!configured_route ||
        !dluma_editor_launch_resolve_current_project(
            project_root, DLUMA_ARRAY_COUNT(project_root),
            map_name, DLUMA_ARRAY_COUNT(map_name))) {
        dluma_log_pair(
            "WARN",
            "editor_selected_model_animation_reload",
            "active_project_unavailable");
        return;
    }
    if (dluma_animation_refresh_runtime_poll_project_now(
            project_root,
            command_source ? command_source : "manual-command")) {
        dluma_log_pair(
            "INFO",
            "editor_selected_model_animation_reload",
            "latest_manifest_request_consumed");
        return;
    }
    if (bridge->has_last_success &&
        dluma_wstricmp(
            project_root, bridge->last_request.project_root) == 0 &&
        configured_route == bridge->last_request.route) {
        request = bridge->last_request;
        request.manifest = &bridge->last_manifest;
    } else {
        if (bridge->has_last_success &&
            dluma_wstricmp(
                project_root, bridge->last_request.project_root) != 0 &&
            dluma_animation_pack_runtime_retained()) {
            dluma_log_pair(
                "WARN",
                "editor_selected_model_animation_reload",
                "stale_runtime_pack_requires_project_refresh");
            return;
        }
        dluma_log_pair(
            "WARN",
            "editor_selected_model_animation_reload",
            "manifest_backed_request_required");
        return;
    }
    dluma_str_copy(
        request.request_id,
        DLUMA_ARRAY_COUNT(request.request_id),
        "manual-selected-model");
    (void)dluma_editor_animation_reload_ui(&request, &result);
    dluma_log_pair(
        result.accepted ? "INFO" : "WARN",
        "editor_selected_model_animation_reload",
        dluma_animation_refresh_refusal_name(result.refusal));
}

static void dluma_editor_animation_insert_menu_item(HMENU menu)
{
    int count;
    int position;
    int refresh_position = -1;
    if (!menu) return;
    count = GetMenuItemCount(menu);
    if (count <= 0 || count > 4096) return;
    for (position = 0; position < count; ++position) {
        UINT identifier = GetMenuItemID(menu, position);
        if (identifier == DLUMA_EDITOR_ANIMATION_RELOAD_SELECTED_COMMAND)
            return;
        if (identifier == DLUMA_EDITOR_ANIMATION_REFRESH_FILESYSTEM_COMMAND)
            refresh_position = position;
    }
    if (refresh_position < 0) return;
    if (InsertMenuW(
            menu,
            (UINT)(refresh_position + 1),
            MF_BYPOSITION | MF_STRING,
            DLUMA_EDITOR_ANIMATION_RELOAD_SELECTED_COMMAND,
            L"Reload Animations (Selected Model)"))
        DrawMenuBar(g_dluma_editor_animation_bridge.window);
}

static LRESULT CALLBACK dluma_editor_animation_window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    WNDPROC predecessor = bridge->predecessor;
    LRESULT result;
    if (message == WM_COMMAND &&
        LOWORD(wparam) ==
            DLUMA_EDITOR_ANIMATION_RELOAD_SELECTED_COMMAND) {
        dluma_editor_animation_reload_manual("refresh-menu");
        return 0;
    }
    if (message == DLUMA_EDITOR_ANIMATION_DISPATCH_MESSAGE) {
        int accepted;
        if (dluma_atomic_compare_exchange(
                &bridge->pending_state, 0, 0) != 1)
            return 0;
        accepted = dluma_editor_animation_reload_ui(
            &bridge->pending_request,
            &bridge->pending_result);
        dluma_atomic_exchange(&bridge->pending_state, 2);
        return accepted ? 1 : 0;
    }
    if (message == WM_INITMENUPOPUP) {
        result = predecessor
            ? CallWindowProcW(
                predecessor, window, message, wparam, lparam)
            : 0;
        dluma_editor_animation_insert_menu_item((HMENU)wparam);
        return result;
    }
    result = predecessor
        ? CallWindowProcW(
            predecessor, window, message, wparam, lparam)
        : 0;
    if (message == WM_NCDESTROY &&
        bridge->window == window) {
        bridge->window = 0;
        bridge->predecessor = 0;
        bridge->owner_thread_id = 0;
    }
    return result;
}

static WCHAR dluma_editor_animation_ascii_fold(WCHAR value)
{
    if (value >= L'A' && value <= L'Z')
        return value + (L'a' - L'A');
    return value;
}

static int dluma_editor_animation_wide_starts_icase(
    const WCHAR* value,
    const WCHAR* expected)
{
    unsigned int index = 0;
    if (!value || !expected) return 0;
    while (expected[index]) {
        if (!value[index] ||
            dluma_editor_animation_ascii_fold(value[index]) !=
                dluma_editor_animation_ascii_fold(expected[index]))
            return 0;
        ++index;
    }
    return 1;
}

static int dluma_editor_animation_wide_contains_icase(
    const WCHAR* value,
    const WCHAR* expected)
{
    unsigned int index;
    if (!value || !expected || !expected[0]) return 0;
    for (index = 0; value[index]; ++index)
        if (dluma_editor_animation_wide_starts_icase(
                value + index, expected))
            return 1;
    return 0;
}

static int dluma_editor_animation_main_window_matches(
    HWND window,
    DWORD expected_process_id,
    DWORD expected_thread_id)
{
    DWORD process_id = 0;
    DWORD thread_id;
    LONG_PTR style;
    LONG_PTR extended_style;
    WCHAR class_name[128];
    WCHAR title[DLUMA_EDITOR_ANIMATION_WINDOW_TEXT_CAPACITY];
    if (!window || !IsWindow(window) || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER))
        return 0;
    thread_id = GetWindowThreadProcessId(window, &process_id);
    if (!thread_id || process_id != expected_process_id ||
        (expected_thread_id && thread_id != expected_thread_id))
        return 0;
    style = GetWindowLongPtrW(window, GWL_STYLE);
    extended_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    class_name[0] = 0;
    title[0] = 0;
    if ((style & WS_CAPTION) != WS_CAPTION ||
        !(style & WS_SYSMENU) ||
        !(style & WS_THICKFRAME) ||
        (extended_style & WS_EX_TOOLWINDOW) ||
        GetClassNameW(
            window,
            class_name,
            (int)DLUMA_ARRAY_COUNT(class_name)) <= 0 ||
        !dluma_editor_animation_wide_starts_icase(
            class_name, L"Afx:") ||
        GetWindowTextW(
            window,
            title,
            (int)DLUMA_ARRAY_COUNT(title)) <= 0)
        return 0;
    return dluma_editor_animation_wide_contains_icase(
        title, L"Dying Light Developer Tools");
}

static BOOL CALLBACK dluma_editor_animation_find_window(
    HWND window,
    LPARAM parameter)
{
    DlumaEditorAnimationWindowCandidate* candidate =
        (DlumaEditorAnimationWindowCandidate*)parameter;
    DWORD process_id = 0;
    RECT rectangle;
    ULONGLONG width;
    ULONGLONG height;
    ULONGLONG area;
    if (!candidate ||
        !dluma_editor_animation_main_window_matches(
            window, candidate->process_id, 0) ||
        !GetWindowThreadProcessId(window, &process_id) ||
        !GetClientRect(window, &rectangle) ||
        rectangle.right <= rectangle.left ||
        rectangle.bottom <= rectangle.top)
        return TRUE;
    width = (ULONGLONG)(rectangle.right - rectangle.left);
    height = (ULONGLONG)(rectangle.bottom - rectangle.top);
    area = width * height;
    if (!candidate->window || area > candidate->area) {
        candidate->window = window;
        candidate->area = area;
    }
    return TRUE;
}

static int dluma_editor_animation_attach(void)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    DlumaEditorAnimationWindowCandidate candidate;
    DWORD process_id = 0;
    DWORD thread_id;
    WNDPROC observed;
    WNDPROC installed;
    int accepted = 0;
    if (bridge->window && bridge->predecessor &&
        dluma_editor_animation_main_window_matches(
            bridge->window,
            GetCurrentProcessId(),
            bridge->owner_thread_id) &&
        (WNDPROC)GetWindowLongPtrW(
            bridge->window, GWLP_WNDPROC) ==
            dluma_editor_animation_window_proc)
        return 1;
    if (bridge->window && IsWindow(bridge->window))
        return 0;
    bridge->window = 0;
    bridge->predecessor = 0;
    bridge->owner_thread_id = 0;
    if (dluma_atomic_compare_exchange(
            &bridge->attach_lock, 1, 0) != 0)
        return 0;
    memset(&candidate, 0, sizeof(candidate));
    candidate.process_id = GetCurrentProcessId();
    if (!EnumWindows(
            dluma_editor_animation_find_window,
            (LPARAM)&candidate) ||
        !candidate.window) {
        if (dluma_atomic_compare_exchange(
                &bridge->discovery_log_state, 1, 0) == 0)
            dluma_log_pair(
                "WARN",
                "editor_animation_ui_bridge_discovery",
                "mfc_main_frame_not_yet_available");
        goto done;
    }
    if (dluma_atomic_exchange(
            &bridge->discovery_log_state, 2) != 2)
        dluma_log_pair(
            "INFO",
            "editor_animation_ui_bridge_discovery",
            "validated_mfc_main_frame");
    observed = (WNDPROC)GetWindowLongPtrW(
        candidate.window, GWLP_WNDPROC);
    thread_id = GetWindowThreadProcessId(
        candidate.window, &process_id);
    if (!thread_id || process_id != candidate.process_id ||
        !observed ||
        observed == dluma_editor_animation_window_proc)
        goto done;
    bridge->window = candidate.window;
    bridge->predecessor = observed;
    bridge->owner_thread_id = thread_id;
    SetLastError(0);
    installed = (WNDPROC)SetWindowLongPtrW(
        candidate.window,
        GWLP_WNDPROC,
        (LONG_PTR)dluma_editor_animation_window_proc);
    if (!installed && GetLastError() != 0) {
        bridge->window = 0;
        bridge->predecessor = 0;
        bridge->owner_thread_id = 0;
        goto done;
    }
    if (installed &&
        installed != dluma_editor_animation_window_proc)
        bridge->predecessor = installed;
    accepted = bridge->predecessor != 0;
done:
    dluma_atomic_exchange(&bridge->attach_lock, 0);
    if (accepted) {
        dluma_log_pair(
            "INFO",
            "editor_animation_ui_bridge_subclass",
            "owner_thread_window_proc_installed");
        dluma_log_line(
            "INFO",
            "Editor selected-model animation UI bridge attached; Shift+Alt+A is scoped to the selected model.");
    } else if (candidate.window) {
        dluma_log_pair(
            "WARN",
            "editor_animation_ui_bridge_subclass",
            "installation_failed");
    }
    return accepted;
}

int dluma_editor_animation_bridge_start(void)
{
    if (!dluma_editor_animation_profile_bind()) return 0;
    if (!dluma_editor_animation_install_stock_reload_detour()) return 0;
    (void)dluma_editor_animation_attach();
    /* Main-window creation can lag loader startup. The Editor watcher retries
     * this function and no failed early discovery is made permanent. */
    return 1;
}

int dluma_editor_animation_bridge_copy_active_document_map(
    char* output,
    unsigned int capacity)
{
    static const WCHAR prefix[] = L"data/maps/";
    static const WCHAR product_marker[] =
        L" - Dying Light Developer Tools";
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    WCHAR title[DLUMA_EDITOR_ANIMATION_WINDOW_TEXT_CAPACITY];
    const WCHAR* map_start;
    const WCHAR* slash;
    const WCHAR* product = 0;
    const WCHAR* cursor;
    const WCHAR* extension;
    char file_stem[DLUMA_MAX_MAP_NAME];
    unsigned int map_length;
    unsigned int file_length;
    unsigned int index;
    if (!output || capacity < 2U) return 0;
    output[0] = 0;
    if (!dluma_editor_animation_bridge_start() ||
        !dluma_editor_animation_attach() ||
        !bridge->window)
        return 0;
    title[0] = 0;
    if (GetWindowTextW(
            bridge->window,
            title,
            (int)DLUMA_ARRAY_COUNT(title)) <= 0 ||
        !dluma_editor_animation_wide_starts_icase(title, prefix))
        return 0;
    for (cursor = title; *cursor; ++cursor) {
        if (dluma_editor_animation_wide_starts_icase(
                cursor, product_marker)) {
            product = cursor;
            break;
        }
    }
    if (!product) return 0;
    map_start = title + DLUMA_ARRAY_COUNT(prefix) - 1U;
    slash = map_start;
    while (slash < product && *slash != L'/' && *slash != L'\\')
        ++slash;
    map_length = (unsigned int)(slash - map_start);
    if (slash >= product || !map_length ||
        map_length + 1U > capacity ||
        map_length + 1U > DLUMA_ARRAY_COUNT(file_stem))
        return 0;
    for (index = 0; index < map_length; ++index) {
        if (map_start[index] < 0x20 || map_start[index] >= 0x7f)
            return 0;
        output[index] = (char)map_start[index];
    }
    output[map_length] = 0;
    if (!dluma_valid_map_segment(output)) {
        output[0] = 0;
        return 0;
    }
    ++slash;
    for (cursor = slash; cursor < product; ++cursor)
        if (*cursor == L'/' || *cursor == L'\\' ||
            *cursor < 0x20 || *cursor >= 0x7f) {
            output[0] = 0;
            return 0;
        }
    file_length = (unsigned int)(product - slash);
    if (file_length <= 4U ||
        file_length + 1U > DLUMA_ARRAY_COUNT(file_stem)) {
        output[0] = 0;
        return 0;
    }
    extension = product - 4U;
    if (!dluma_editor_animation_wide_starts_icase(extension, L".map")) {
        output[0] = 0;
        return 0;
    }
    for (index = 0; index < file_length - 4U; ++index)
        file_stem[index] = (char)slash[index];
    file_stem[file_length - 4U] = 0;
    if (dluma_stricmp(file_stem, output) != 0) {
        output[0] = 0;
        return 0;
    }
    return 1;
}

int dluma_editor_animation_bridge_dispatch(
    const DlumaEditorAnimationReloadRequest* request,
    DlumaAnimationRefreshResult* result)
{
    DlumaEditorAnimationBridge* bridge =
        &g_dluma_editor_animation_bridge;
    ULONG_PTR message_result = 0;
    LONG pending;
    int delivered;
    if (!request || !result)
        return 0;
    if (!dluma_editor_animation_bridge_start() ||
        !dluma_editor_animation_attach()) {
        result->ui_bridge_discovery =
            DLUMA_ANIMATION_REFRESH_STAGE_FAILED;
        return dluma_editor_animation_refuse(
            result,
            DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED);
    }
    result->ui_bridge_discovery =
        DLUMA_ANIMATION_REFRESH_STAGE_PASSED;
    if (GetCurrentThreadId() == bridge->owner_thread_id)
        return dluma_editor_animation_reload_ui(request, result);
    pending = dluma_atomic_compare_exchange(
        &bridge->pending_state, 0, 0);
    if (pending == 2)
        dluma_atomic_exchange(&bridge->pending_state, 0);
    if (dluma_atomic_compare_exchange(
            &bridge->pending_state, -1, 0) != 0)
        return dluma_editor_animation_refuse(
            result,
            DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED);
    bridge->pending_request = *request;
    if (request->manifest) {
        bridge->pending_manifest = *request->manifest;
        bridge->pending_request.manifest =
            &bridge->pending_manifest;
    } else {
        memset(
            &bridge->pending_manifest,
            0,
            sizeof(bridge->pending_manifest));
        bridge->pending_request.manifest = 0;
    }
    bridge->pending_result = *result;
    dluma_atomic_exchange(&bridge->pending_state, 1);
    delivered = SendMessageTimeoutW(
        bridge->window,
        DLUMA_EDITOR_ANIMATION_DISPATCH_MESSAGE,
        0,
        0,
        DLUMA_SMTO_BLOCK | DLUMA_SMTO_ABORTIFHUNG |
            DLUMA_SMTO_ERRORONEXIT,
        DLUMA_EDITOR_ANIMATION_SEND_TIMEOUT_MS,
        &message_result) != 0;
    if (dluma_atomic_compare_exchange(
            &bridge->pending_state, 0, 0) == 2) {
        *result = bridge->pending_result;
        dluma_atomic_exchange(&bridge->pending_state, 0);
        return delivered && message_result != 0 &&
            result->accepted != 0U;
    }
    /* The pending request owns a deep manifest copy. On timeout it remains
     * reserved so a late owner-thread dispatch cannot dereference caller
     * memory or overlap a newer request. */
    return dluma_editor_animation_refuse(
        result,
        DLUMA_ANIMATION_REFRESH_REFUSAL_OWNER_THREAD_DISPATCH_FAILED);
}
