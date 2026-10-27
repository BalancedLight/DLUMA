#include "dlw_editor_model_animation_fix.h"

#include "core/dlw_editor_model_animation_guard.h"
#include "dlw_common.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_editor_host.h"
#include "dlw_log.h"
#include "dlw_path_security.h"

#define DLUMA_EDITOR_MODEL_ANIMATION_MAX_IMAGE_BYTES \
    (1024ULL * 1024ULL * 1024ULL)
#define DLUMA_EDITOR_MODEL_ANIMATION_HEADER_BYTES 4096U
#define DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE 16U
#define DLUMA_EDITOR_MODEL_ANIMATION_HELPER_RVA 0x0075C9E0UL
#define DLUMA_EDITOR_MODEL_ANIMATION_FAULT_RVA 0x0075CA16UL
#define DLUMA_EDITOR_MODEL_ANIMATION_GET_BIND_IAT_RVA 0x013CCC20UL
#define DLUMA_EDITOR_MODEL_ANIMATION_GET_BIND_ENGINE_RVA 0x0015B390UL
#define DLUMA_EDITOR_MODEL_ANIMATION_DRAIN_TIMEOUT_MS 2000ULL

typedef void (__fastcall *DlumaEditorModelAnimationOriginalFn)(
    void* model,
    float start_time);
typedef void* (__fastcall *DlumaEditorModelAnimationGetBindFn)(void* model);

typedef enum DlumaEditorModelAnimationHostProfile {
    DLUMA_EDITOR_MODEL_ANIMATION_HOST_NONE = 0,
    DLUMA_EDITOR_MODEL_ANIMATION_HOST_EDITOR = 1,
    DLUMA_EDITOR_MODEL_ANIMATION_HOST_PLAYER = 2
} DlumaEditorModelAnimationHostProfile;

typedef struct DlumaEditorModelAnimationFixState {
    HMODULE gamedll;
    HMODULE engine;
    BYTE* target;
    BYTE* trampoline;
    SIZE_T trampoline_size;
    DlumaEditorModelAnimationGetBindFn get_anim_bind;
    BYTE original[DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE];
    BYTE patch[DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE];
    volatile LONG installed;
    volatile LONG stopping;
    volatile LONG active_calls;
    volatile LONG suppression_count;
    volatile LONG teardown_count_logged;
    volatile LONG loader_pinned;
    DlumaEditorModelAnimationHostProfile host_profile;
} DlumaEditorModelAnimationFixState;

static const DlumaEditorModelAnimationIdentity g_dluma_editor_identity = {
    0x623396E1UL,
    0x01E4A000UL,
    "CACD9C96BA5D8C35A23170DC8BE8029DB9AA5E6223F5E6CD5DE3E61D8E72DA28"
};
static const DlumaEditorModelAnimationIdentity g_dluma_editor_gamedll_identity = {
    0x623397AAUL,
    0x02082000UL,
    "8E19C89C316E319E1E88D0C7A6F6A935C4B9C381905CAB48449CEE8FD6CCBA54"
};
static const DlumaEditorModelAnimationIdentity g_dluma_editor_engine_identity = {
    0x623395D0UL,
    0x00E8D000UL,
    "5B8090BFC3D3A25A24DA1846A3AAFD65C97E0D1E700B5DFEBFC7132737B6BFF6"
};
static const DlumaEditorModelAnimationIdentity g_dluma_player_identity = {
    0x623397B8UL,
    0x00103000UL,
    "61D9DFB20CFA179C6D097537DA46BA9DBE3D58797938762EF4EACFF5DFCC4B3A"
};
static const DlumaEditorModelAnimationIdentity g_dluma_player_gamedll_identity = {
    0x623397AAUL,
    0x02082000UL,
    "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95"
};
static const DlumaEditorModelAnimationIdentity g_dluma_player_engine_identity = {
    0x623395D0UL,
    0x00E8D000UL,
    "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C"
};
static const BYTE g_dluma_editor_model_animation_prologue[
    DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE] = {
        0x40U, 0x53U, 0x48U, 0x81U, 0xECU, 0x90U, 0x00U, 0x00U,
        0x00U, 0x83U, 0xB9U, 0xE8U, 0x00U, 0x00U, 0x00U, 0x00U
};
static const BYTE g_dluma_editor_model_animation_fault_bytes[] = {
    0x4CU, 0x8BU, 0x08U
};

static DlumaEditorModelAnimationFixState g_dluma_editor_model_animation_fix;
static DlumaEditorModelAnimationLifecycle
    g_dluma_editor_model_animation_lifecycle;
static volatile LONG g_dluma_editor_model_animation_lock;

static void dluma_editor_model_animation_acquire(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma_editor_model_animation_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_editor_model_animation_release(void)
{
    dluma_atomic_exchange(&g_dluma_editor_model_animation_lock, 0);
}

static const WCHAR* dluma_editor_model_animation_skip_extended(
    const WCHAR* path)
{
    if (path && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] == L'?' && path[3] == L'\\')
        return path + 4;
    return path;
}

static int dluma_editor_model_animation_file_info_equal(
    const BY_HANDLE_FILE_INFORMATION* first,
    const BY_HANDLE_FILE_INFORMATION* second)
{
    return first && second &&
        first->dwFileAttributes == second->dwFileAttributes &&
        first->dwVolumeSerialNumber == second->dwVolumeSerialNumber &&
        first->nNumberOfLinks == second->nNumberOfLinks &&
        first->nFileIndexHigh == second->nFileIndexHigh &&
        first->nFileIndexLow == second->nFileIndexLow &&
        first->nFileSizeHigh == second->nFileSizeHigh &&
        first->nFileSizeLow == second->nFileSizeLow &&
        first->ftLastWriteTime.dwHighDateTime ==
            second->ftLastWriteTime.dwHighDateTime &&
        first->ftLastWriteTime.dwLowDateTime ==
            second->ftLastWriteTime.dwLowDateTime;
}

static int dluma_editor_model_animation_validate_module(
    HMODULE module,
    const DlumaEditorModelAnimationIdentity* expected)
{
    BYTE header[DLUMA_EDITOR_MODEL_ANIMATION_HEADER_BYTES];
    BYTE* loaded = (BYTE*)module;
    WCHAR lexical[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION before;
    BY_HANDLE_FILE_INFORMATION after;
    LARGE_INTEGER zero;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD attributes;
    DWORD read = 0;
    DWORD loaded_timestamp = 0;
    DWORD loaded_image_size = 0;
    DWORD disk_timestamp = 0;
    DWORD disk_image_size = 0;
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    DlumaEditorModelAnimationIdentity actual;
    int result = 0;
    memset(lexical, 0, sizeof(lexical));
    memset(final_path, 0, sizeof(final_path));
    memset(&before, 0, sizeof(before));
    memset(&after, 0, sizeof(after));
    memset(sha256, 0, sizeof(sha256));
    zero.QuadPart = 0;
    if (!module || !expected || !loaded ||
        !dluma_is_readable(
            loaded, DLUMA_EDITOR_MODEL_ANIMATION_HEADER_BYTES) ||
        !dluma_build_identity_parse_pe_header(
            loaded, DLUMA_EDITOR_MODEL_ANIMATION_HEADER_BYTES,
            &loaded_timestamp, &loaded_image_size) ||
        !dluma_get_module_path_checked(
            module, lexical, DLUMA_ARRAY_COUNT(lexical)))
        return 0;
    attributes = GetFileAttributesW(lexical);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        return 0;
    file = CreateFileW(
        lexical, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!dluma_get_final_path_checked(
            file, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
        !GetFileInformationByHandle(file, &before) ||
        before.nNumberOfLinks != 1U ||
        (before.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
        dluma_wstricmp(
            dluma_editor_model_animation_skip_extended(lexical),
            dluma_editor_model_animation_skip_extended(final_path)) != 0 ||
        !dluma_sha256_handle_hex(
            file, DLUMA_EDITOR_MODEL_ANIMATION_MAX_IMAGE_BYTES, sha256) ||
        !SetFilePointerEx(file, zero, 0, FILE_BEGIN) ||
        !ReadFile(file, header, sizeof(header), &read, 0) ||
        !dluma_build_identity_parse_pe_header(
            header, read, &disk_timestamp, &disk_image_size) ||
        !GetFileInformationByHandle(file, &after) ||
        !dluma_editor_model_animation_file_info_equal(&before, &after) ||
        loaded_timestamp != disk_timestamp ||
        loaded_image_size != disk_image_size)
        goto done;
    actual.timestamp = loaded_timestamp;
    actual.size_of_image = loaded_image_size;
    actual.sha256 = sha256;
    result = dluma_editor_model_animation_identity_matches(
        &actual, expected);
done:
    CloseHandle(file);
    return result;
}

static DlumaEditorModelAnimationHostProfile
dluma_editor_model_animation_validate_host(
    const DlumaEditorModelAnimationIdentity** gamedll_identity,
    const DlumaEditorModelAnimationIdentity** engine_identity)
{
    const DlumaDualHostRuntimeSnapshot* runtime =
        dluma_dual_host_runtime_snapshot();
    DlumaEditorModelAnimationIdentity actual;
    if (gamedll_identity) *gamedll_identity = 0;
    if (engine_identity) *engine_identity = 0;
    if (!runtime || !runtime->initialized ||
        !runtime->classification.exact_profile ||
        runtime->classification.forwarding_only ||
        !runtime->evidence.canonical_image_verified ||
        !gamedll_identity || !engine_identity)
        return DLUMA_EDITOR_MODEL_ANIMATION_HOST_NONE;
    actual.timestamp = runtime->evidence.image.timestamp;
    actual.size_of_image = runtime->evidence.image.size_of_image;
    actual.sha256 = runtime->evidence.image.sha256;
    if (runtime->classification.kind == DLUMA_HOST_CHROME_EDITOR &&
        dluma_dual_host_is_editor_identity(
            runtime->evidence.canonical_image_utf8,
            &runtime->evidence.image) &&
        dluma_editor_model_animation_identity_matches(
            &actual, &g_dluma_editor_identity) &&
        dluma_editor_model_animation_validate_module(
            GetModuleHandleW(0), &g_dluma_editor_identity)) {
        *gamedll_identity = &g_dluma_editor_gamedll_identity;
        *engine_identity = &g_dluma_editor_engine_identity;
        return DLUMA_EDITOR_MODEL_ANIMATION_HOST_EDITOR;
    }
    if ((runtime->classification.kind == DLUMA_HOST_PLAYER_STANDALONE ||
         runtime->classification.kind ==
             DLUMA_HOST_PLAYER_EDITOR_PLAYTEST) &&
        dluma_editor_model_animation_identity_matches(
            &actual, &g_dluma_player_identity) &&
        dluma_editor_model_animation_validate_module(
            GetModuleHandleW(0), &g_dluma_player_identity)) {
        *gamedll_identity = &g_dluma_player_gamedll_identity;
        *engine_identity = &g_dluma_player_engine_identity;
        return DLUMA_EDITOR_MODEL_ANIMATION_HOST_PLAYER;
    }
    return DLUMA_EDITOR_MODEL_ANIMATION_HOST_NONE;
}

static int dluma_editor_model_animation_is_executable_range(
    const void* address,
    SIZE_T bytes)
{
    const BYTE* cursor = (const BYTE*)address;
    SIZE_T remaining = bytes;
    MEMORY_BASIC_INFORMATION information;
    if (!address || !bytes ||
        (ULONG_PTR)address > ~(ULONG_PTR)0 - (bytes - 1U))
        return 0;
    while (remaining) {
        SIZE_T offset;
        SIZE_T available;
        DWORD protection;
        if (VirtualQuery(
                cursor, &information, sizeof(information)) !=
            sizeof(information))
            return 0;
        protection = information.Protect & 0xFFUL;
        if (information.State != MEM_COMMIT ||
            (information.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
            !(protection == PAGE_EXECUTE ||
              protection == PAGE_EXECUTE_READ ||
              protection == PAGE_EXECUTE_READWRITE ||
              protection == PAGE_EXECUTE_WRITECOPY))
            return 0;
        offset = (SIZE_T)(cursor - (const BYTE*)information.BaseAddress);
        if (offset >= information.RegionSize) return 0;
        available = information.RegionSize - offset;
        if (available >= remaining) return 1;
        if ((ULONG_PTR)cursor > ~(ULONG_PTR)0 - available) return 0;
        cursor += available;
        remaining -= available;
    }
    return 1;
}

static int dluma_editor_model_animation_read_u32(
    void* context,
    uintptr_t address,
    uint32_t* output)
{
    (void)context;
    if (!output || !dluma_is_readable(
            (const void*)(ULONG_PTR)address, sizeof(*output)))
        return 0;
    __try {
        memcpy(output, (const void*)(ULONG_PTR)address, sizeof(*output));
    } __except(1) {
        return 0;
    }
    return 1;
}

static int dluma_editor_model_animation_read_pointer(
    void* context,
    uintptr_t address,
    uintptr_t* output)
{
    (void)context;
    if (!output || !dluma_is_readable(
            (const void*)(ULONG_PTR)address, sizeof(*output)))
        return 0;
    __try {
        memcpy(output, (const void*)(ULONG_PTR)address, sizeof(*output));
    } __except(1) {
        return 0;
    }
    return 1;
}

static uintptr_t dluma_editor_model_animation_get_bind(
    void* context,
    uintptr_t model)
{
    DlumaEditorModelAnimationFixState* state =
        (DlumaEditorModelAnimationFixState*)context;
    void* result = 0;
    if (!state || !state->get_anim_bind || !model) return 0;
    __try {
        result = state->get_anim_bind((void*)(ULONG_PTR)model);
    } __except(1) {
        result = 0;
    }
    return (uintptr_t)result;
}

static int dluma_editor_model_animation_is_executable(
    void* context,
    uintptr_t address)
{
    (void)context;
    return dluma_editor_model_animation_is_executable_range(
        (const void*)(ULONG_PTR)address, 1U);
}

static const char* dluma_editor_model_animation_refusal_key(
    DlumaEditorModelAnimationHostProfile profile)
{
    if (profile == DLUMA_EDITOR_MODEL_ANIMATION_HOST_PLAYER)
        return "player_animation_bind_fix_refused";
    if (profile == DLUMA_EDITOR_MODEL_ANIMATION_HOST_EDITOR)
        return "editor_animation_bind_fix_refused";
    return "model_animation_bind_fix_refused";
}

static void dluma_editor_model_animation_log_refusal(
    DlumaEditorModelAnimationHostProfile profile,
    const char* reason)
{
    dluma_log_pair(
        "WARN", dluma_editor_model_animation_refusal_key(profile), reason);
}

static int dluma_editor_model_animation_pin_loader(void)
{
    HMODULE pinned = 0;
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_model_animation_fix.loader_pinned, 0, 0))
        return 1;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_PIN |
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (LPCWSTR)(const void*)&g_dluma_editor_model_animation_fix,
            &pinned) || pinned != g_dluma.self_module) {
        dluma_log_hex(
            "ERROR", "animation_bind_guard_loader_pin_failed",
            GetLastError());
        return 0;
    }
    dluma_atomic_exchange(
        &g_dluma_editor_model_animation_fix.loader_pinned, 1);
    dluma_log_pair(
        "WARN", "animation_bind_guard_loader_pinned",
        "reachable detour state retained until process exit");
    return 1;
}

static void dluma_editor_model_animation_mark_retained(void)
{
    (void)dluma_editor_model_animation_pin_loader();
    dluma_editor_model_animation_lifecycle_mark_retained(
        &g_dluma_editor_model_animation_lifecycle);
}

static void dluma_editor_model_animation_log_final_count(void)
{
    LONG count;
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_model_animation_fix.teardown_count_logged,
            1, 0) != 0)
        return;
    count = dluma_atomic_compare_exchange(
        &g_dluma_editor_model_animation_fix.suppression_count, 0, 0);
    dluma_log_u64(
        "INFO",
        g_dluma_editor_model_animation_fix.host_profile ==
                DLUMA_EDITOR_MODEL_ANIMATION_HOST_PLAYER
            ? "player_animation_bind_suppression_count"
            : "editor_animation_bind_suppression_count",
        count > 0 ? (ULONGLONG)count : 0ULL);
}

static void __fastcall dluma_editor_model_animation_hook(
    void* model,
    float start_time)
{
    DlumaEditorModelAnimationAccess access;
    DlumaEditorModelAnimationDecision decision;
    DlumaEditorModelAnimationOriginalFn original;
    DlumaEditorModelAnimationHostProfile profile =
        g_dluma_editor_model_animation_fix.host_profile;
    LONG suppression_count;
    int tracked = 0;
    if (profile == DLUMA_EDITOR_MODEL_ANIMATION_HOST_EDITOR)
        tracked = dluma_editor_host_callback_enter();
    dluma_atomic_increment(
        &g_dluma_editor_model_animation_fix.active_calls);
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_model_animation_fix.stopping, 0, 0))
        goto done;
    access.context = &g_dluma_editor_model_animation_fix;
    access.read_u32 = dluma_editor_model_animation_read_u32;
    access.get_anim_bind = dluma_editor_model_animation_get_bind;
    access.read_pointer = dluma_editor_model_animation_read_pointer;
    access.is_executable = dluma_editor_model_animation_is_executable;
    decision = dluma_editor_model_animation_guard_probe(
        (uintptr_t)model, &access);
    original = (DlumaEditorModelAnimationOriginalFn)
        g_dluma_editor_model_animation_fix.trampoline;
    if (dluma_editor_model_animation_guard_calls_original(decision) &&
        original) {
        original(model, start_time);
    } else {
        suppression_count = dluma_atomic_increment(
            &g_dluma_editor_model_animation_fix.suppression_count);
        if (suppression_count > 0 &&
            dluma_editor_model_animation_guard_first_warning(
                (uint64_t)suppression_count))
            dluma_log_line(
                "WARN",
                profile == DLUMA_EDITOR_MODEL_ANIMATION_HOST_PLAYER
                    ? "Player animation-bind guard kept a ModelMenuScene3dDI custom model static because m_LoopedAnim has no readable, callable animation bind. Rebuild the model animation .ascr/bind to restore animation."
                    : "Editor animation-bind guard kept a ModelMenuScene3dDI custom model static because m_LoopedAnim has no readable, callable animation bind. Rebuild the model animation .ascr/bind to restore animation.");
    }
done:
    dluma_atomic_decrement(
        &g_dluma_editor_model_animation_fix.active_calls);
    if (profile == DLUMA_EDITOR_MODEL_ANIMATION_HOST_EDITOR)
        dluma_editor_host_callback_leave(tracked);
}

static int dluma_editor_model_animation_wait_active(void)
{
    ULONGLONG started = GetTickCount64();
    while (dluma_atomic_compare_exchange(
               &g_dluma_editor_model_animation_fix.active_calls, 0, 0) != 0 &&
           GetTickCount64() - started <
               DLUMA_EDITOR_MODEL_ANIMATION_DRAIN_TIMEOUT_MS)
        Sleep(1);
    return dluma_atomic_compare_exchange(
               &g_dluma_editor_model_animation_fix.active_calls, 0, 0) == 0;
}

static int dluma_editor_model_animation_free_trampoline(void)
{
    if (g_dluma_editor_model_animation_fix.trampoline &&
        !VirtualFree(
            g_dluma_editor_model_animation_fix.trampoline,
            0, MEM_RELEASE))
        return 0;
    g_dluma_editor_model_animation_fix.trampoline = 0;
    g_dluma_editor_model_animation_fix.trampoline_size = 0;
    return 1;
}

/* Returns 1 when installed, 0 after a clean refusal, and -1 when any
 * potentially reachable allocation or modified page must be retained. */
static int dluma_editor_model_animation_install_detour(
    BYTE* target,
    DlumaEditorModelAnimationGetBindFn get_anim_bind,
    DlumaEditorModelAnimationHostProfile host_profile)
{
    BYTE tail[DLUMA_EDITOR_ABSOLUTE_JUMP_SIZE];
    DWORD trampoline_old = 0;
    DWORD target_old = 0;
    DWORD ignored = 0;
    DWORD rollback_old = 0;
    int target_writable = 0;
    int flushed;
    int protection_restored;
    memset(&g_dluma_editor_model_animation_fix, 0,
           sizeof(g_dluma_editor_model_animation_fix));
    g_dluma_editor_model_animation_fix.target = target;
    g_dluma_editor_model_animation_fix.get_anim_bind = get_anim_bind;
    g_dluma_editor_model_animation_fix.host_profile = host_profile;
    memcpy(g_dluma_editor_model_animation_fix.original,
           g_dluma_editor_model_animation_prologue,
           sizeof(g_dluma_editor_model_animation_prologue));
    memset(g_dluma_editor_model_animation_fix.patch, 0x90,
           sizeof(g_dluma_editor_model_animation_fix.patch));
    dluma_editor_model_animation_build_absolute_jump(
        g_dluma_editor_model_animation_fix.patch,
        (uintptr_t)(const void*)dluma_editor_model_animation_hook);
    g_dluma_editor_model_animation_fix.trampoline_size =
        DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE +
        DLUMA_EDITOR_ABSOLUTE_JUMP_SIZE;
    g_dluma_editor_model_animation_fix.trampoline = (BYTE*)VirtualAlloc(
        0, g_dluma_editor_model_animation_fix.trampoline_size,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_dluma_editor_model_animation_fix.trampoline) return 0;
    memcpy(g_dluma_editor_model_animation_fix.trampoline,
           g_dluma_editor_model_animation_fix.original,
           DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE);
    dluma_editor_model_animation_build_absolute_jump(
        tail,
        (uintptr_t)(target + DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE));
    memcpy(g_dluma_editor_model_animation_fix.trampoline +
               DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE,
           tail, sizeof(tail));
    if (!VirtualProtect(
            g_dluma_editor_model_animation_fix.trampoline,
            g_dluma_editor_model_animation_fix.trampoline_size,
            PAGE_EXECUTE_READ, &trampoline_old) ||
        (trampoline_old & 0xFFUL) != PAGE_READWRITE ||
        !FlushInstructionCache(
            GetCurrentProcess(),
            g_dluma_editor_model_animation_fix.trampoline,
            g_dluma_editor_model_animation_fix.trampoline_size)) {
        return dluma_editor_model_animation_free_trampoline() ? 0 : -1;
    }

    if (!VirtualProtect(
            target, DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE,
            PAGE_EXECUTE_READWRITE, &target_old))
        return dluma_editor_model_animation_free_trampoline() ? 0 : -1;
    target_writable = 1;
    if (memcmp(
            target, g_dluma_editor_model_animation_fix.original,
            DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) != 0) {
        if (!VirtualProtect(
                target, DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE,
                target_old, &ignored))
            return -1;
        return dluma_editor_model_animation_free_trampoline() ? 0 : -1;
    }
    memcpy(target, g_dluma_editor_model_animation_fix.patch,
           DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE);
    flushed = FlushInstructionCache(
        GetCurrentProcess(), target,
        DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) != FALSE;
    protection_restored = VirtualProtect(
        target, DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE,
        target_old, &ignored) != FALSE;
    if (protection_restored) target_writable = 0;
    if (flushed && protection_restored &&
        memcmp(target, g_dluma_editor_model_animation_fix.patch,
               DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) == 0) {
        dluma_atomic_exchange(
            &g_dluma_editor_model_animation_fix.installed, 1);
        return 1;
    }

    if (!target_writable) {
        if (!VirtualProtect(
                target, DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE,
                PAGE_EXECUTE_READWRITE, &rollback_old))
            return -1;
        target_writable = 1;
    }
    if (memcmp(target, g_dluma_editor_model_animation_fix.patch,
               DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) != 0 &&
        memcmp(target, g_dluma_editor_model_animation_fix.original,
               DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) != 0) {
        (void)VirtualProtect(
            target, DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE,
            target_old, &ignored);
        return -1;
    }
    memcpy(target, g_dluma_editor_model_animation_fix.original,
           DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE);
    flushed = FlushInstructionCache(
        GetCurrentProcess(), target,
        DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) != FALSE;
    protection_restored = VirtualProtect(
        target, DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE,
        target_old, &ignored) != FALSE;
    if (protection_restored) target_writable = 0;
    if (flushed && protection_restored &&
        memcmp(target, g_dluma_editor_model_animation_fix.original,
               DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) == 0)
        return dluma_editor_model_animation_free_trampoline() ? 0 : -1;
    if (target_writable)
        (void)VirtualProtect(
            target, DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE,
            target_old, &ignored);
    if (memcmp(target, g_dluma_editor_model_animation_fix.patch,
               DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) == 0)
        dluma_atomic_exchange(
            &g_dluma_editor_model_animation_fix.installed, 1);
    return -1;
}

static int dluma_editor_model_animation_validate_targets(
    HMODULE gamedll,
    HMODULE engine,
    const DlumaEditorModelAnimationIdentity* gamedll_identity,
    const DlumaEditorModelAnimationIdentity* engine_identity,
    BYTE** target_out,
    DlumaEditorModelAnimationGetBindFn* get_bind_out)
{
    BYTE* game = (BYTE*)gamedll;
    BYTE* engine_base = (BYTE*)engine;
    BYTE* target;
    void* imported_get_bind = 0;
    void* expected_get_bind;
    if (!game || !engine_base || !gamedll_identity || !engine_identity ||
        !target_out || !get_bind_out ||
        DLUMA_EDITOR_MODEL_ANIMATION_HELPER_RVA +
                DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE >
            gamedll_identity->size_of_image ||
        DLUMA_EDITOR_MODEL_ANIMATION_FAULT_RVA +
                sizeof(g_dluma_editor_model_animation_fault_bytes) >
            gamedll_identity->size_of_image ||
        DLUMA_EDITOR_MODEL_ANIMATION_GET_BIND_IAT_RVA + sizeof(void*) >
            gamedll_identity->size_of_image ||
        DLUMA_EDITOR_MODEL_ANIMATION_GET_BIND_ENGINE_RVA >=
            engine_identity->size_of_image)
        return 0;
    target = game + DLUMA_EDITOR_MODEL_ANIMATION_HELPER_RVA;
    expected_get_bind =
        engine_base + DLUMA_EDITOR_MODEL_ANIMATION_GET_BIND_ENGINE_RVA;
    if (!dluma_is_readable(
            target, DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) ||
        !dluma_editor_model_animation_is_executable_range(
            target, DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) ||
        memcmp(target, g_dluma_editor_model_animation_prologue,
               DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) != 0 ||
        !dluma_is_readable(
            game + DLUMA_EDITOR_MODEL_ANIMATION_FAULT_RVA,
            sizeof(g_dluma_editor_model_animation_fault_bytes)) ||
        memcmp(game + DLUMA_EDITOR_MODEL_ANIMATION_FAULT_RVA,
               g_dluma_editor_model_animation_fault_bytes,
               sizeof(g_dluma_editor_model_animation_fault_bytes)) != 0 ||
        !dluma_is_readable(
            game + DLUMA_EDITOR_MODEL_ANIMATION_GET_BIND_IAT_RVA,
            sizeof(imported_get_bind)) ||
        !dluma_editor_model_animation_is_executable_range(
            expected_get_bind, 1U))
        return 0;
    memcpy(&imported_get_bind,
           game + DLUMA_EDITOR_MODEL_ANIMATION_GET_BIND_IAT_RVA,
           sizeof(imported_get_bind));
    if (imported_get_bind != expected_get_bind) return 0;
    *target_out = target;
    *get_bind_out =
        (DlumaEditorModelAnimationGetBindFn)imported_get_bind;
    return 1;
}

DlumaEditorModelAnimationFixInstallResult
dluma_editor_model_animation_fix_install(
    HMODULE gamedll,
    ULONGLONG generation)
{
    HMODULE engine;
    const DlumaEditorModelAnimationIdentity* gamedll_identity = 0;
    const DlumaEditorModelAnimationIdentity* engine_identity = 0;
    DlumaEditorModelAnimationHostProfile host_profile =
        DLUMA_EDITOR_MODEL_ANIMATION_HOST_NONE;
    BYTE* target = 0;
    DlumaEditorModelAnimationGetBindFn get_anim_bind = 0;
    int detour_result;
    DlumaEditorModelAnimationFixInstallResult result =
        DLUMA_EDITOR_MODEL_ANIMATION_FIX_REFUSED;
    dluma_editor_model_animation_acquire();
    if (g_dluma_editor_model_animation_lifecycle.retained) {
        result = DLUMA_EDITOR_MODEL_ANIMATION_FIX_RETAINED;
        goto done;
    }
    if (g_dluma_editor_model_animation_lifecycle.installed) {
        result = g_dluma_editor_model_animation_lifecycle.current_generation ==
                generation
            ? DLUMA_EDITOR_MODEL_ANIMATION_FIX_INSTALLED
            : DLUMA_EDITOR_MODEL_ANIMATION_FIX_RETAINED;
        goto done;
    }
    host_profile = dluma_editor_model_animation_validate_host(
        &gamedll_identity, &engine_identity);
    if (host_profile == DLUMA_EDITOR_MODEL_ANIMATION_HOST_NONE) {
        dluma_editor_model_animation_log_refusal(
            host_profile,
            "Editor or Player executable identity mismatch");
        goto done;
    }
    engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    if (!dluma_editor_model_animation_validate_module(
            gamedll, gamedll_identity) ||
        !dluma_editor_model_animation_validate_module(
            engine, engine_identity)) {
        dluma_editor_model_animation_log_refusal(
            host_profile,
            "gamedll or engine disk-loaded identity mismatch");
        goto done;
    }
    if (!dluma_editor_model_animation_validate_targets(
            gamedll, engine, gamedll_identity, engine_identity,
            &target, &get_anim_bind)) {
        dluma_editor_model_animation_log_refusal(
            host_profile,
            "helper prologue, fault bytes, IAT, or engine target mismatch");
        goto done;
    }
    if (!dluma_editor_model_animation_lifecycle_begin(
            &g_dluma_editor_model_animation_lifecycle,
            (uint64_t)generation)) {
        result = DLUMA_EDITOR_MODEL_ANIMATION_FIX_RETAINED;
        goto done;
    }
    detour_result = dluma_editor_model_animation_install_detour(
        target, get_anim_bind, host_profile);
    g_dluma_editor_model_animation_fix.gamedll = gamedll;
    g_dluma_editor_model_animation_fix.engine = engine;
    if (detour_result > 0) {
        dluma_editor_model_animation_lifecycle_mark_installed(
            &g_dluma_editor_model_animation_lifecycle);
        dluma_log_line(
            "INFO",
            host_profile == DLUMA_EDITOR_MODEL_ANIMATION_HOST_PLAYER
                ? "Exact Player ModelMenuScene3dDI animation-bind guard installed before original gamedll initialization."
                : "Exact Editor ModelMenuScene3dDI animation-bind guard installed before original gamedll initialization.");
        result = DLUMA_EDITOR_MODEL_ANIMATION_FIX_INSTALLED;
    } else if (detour_result < 0) {
        dluma_editor_model_animation_mark_retained();
        dluma_log_line(
            "ERROR",
            host_profile == DLUMA_EDITOR_MODEL_ANIMATION_HOST_PLAYER
                ? "Player animation-bind detour installation could not be rolled back completely; the gamedll generation and loader must remain loaded until process exit."
                : "Editor animation-bind detour installation could not be rolled back completely; the gamedll generation must remain loaded and reinitialization is disabled.");
        result = DLUMA_EDITOR_MODEL_ANIMATION_FIX_RETAINED;
    } else {
        dluma_editor_model_animation_lifecycle_abort(
            &g_dluma_editor_model_animation_lifecycle);
        dluma_editor_model_animation_log_refusal(
            host_profile,
            "validated detour installation failed cleanly");
    }
done:
    dluma_editor_model_animation_release();
    return result;
}

int dluma_editor_model_animation_fix_quiesce(void)
{
    int success = 0;
    dluma_editor_model_animation_acquire();
    if (g_dluma_editor_model_animation_lifecycle.retained) goto done;
    if (!g_dluma_editor_model_animation_lifecycle.installed) {
        success = 1;
        goto done;
    }
    dluma_atomic_exchange(
        &g_dluma_editor_model_animation_fix.stopping, 1);
    if (dluma_editor_model_animation_wait_active()) {
        success = 1;
        goto done;
    }
    dluma_editor_model_animation_log_final_count();
    dluma_editor_model_animation_mark_retained();
    dluma_log_line(
        "ERROR",
        "Animation-bind guard callbacks did not drain; the detour, loader, and complete gamedll generation are retained until process exit.");
done:
    dluma_editor_model_animation_release();
    return success;
}

int dluma_editor_model_animation_fix_restore(void)
{
    DWORD old_protect = 0;
    DWORD ignored = 0;
    int flushed;
    int protection_restored;
    int success = 0;
    dluma_editor_model_animation_acquire();
    if (g_dluma_editor_model_animation_lifecycle.retained) goto done;
    if (!g_dluma_editor_model_animation_lifecycle.installed) {
        success = 1;
        goto done;
    }
    dluma_atomic_exchange(
        &g_dluma_editor_model_animation_fix.stopping, 1);
    if (!dluma_editor_model_animation_wait_active() ||
        !g_dluma_editor_model_animation_fix.target ||
        !dluma_is_readable(
            g_dluma_editor_model_animation_fix.target,
            DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) ||
        memcmp(g_dluma_editor_model_animation_fix.target,
               g_dluma_editor_model_animation_fix.patch,
               DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) != 0 ||
        !VirtualProtect(
            g_dluma_editor_model_animation_fix.target,
            DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE,
            PAGE_EXECUTE_READWRITE, &old_protect))
        goto retain;
    if (memcmp(g_dluma_editor_model_animation_fix.target,
               g_dluma_editor_model_animation_fix.patch,
               DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) != 0) {
        (void)VirtualProtect(
            g_dluma_editor_model_animation_fix.target,
            DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE,
            old_protect, &ignored);
        goto retain;
    }
    memcpy(g_dluma_editor_model_animation_fix.target,
           g_dluma_editor_model_animation_fix.original,
           DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE);
    flushed = FlushInstructionCache(
        GetCurrentProcess(),
        g_dluma_editor_model_animation_fix.target,
        DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) != FALSE;
    protection_restored = VirtualProtect(
        g_dluma_editor_model_animation_fix.target,
        DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE,
        old_protect, &ignored) != FALSE;
    if (!flushed || !protection_restored ||
        memcmp(g_dluma_editor_model_animation_fix.target,
               g_dluma_editor_model_animation_fix.original,
               DLUMA_EDITOR_MODEL_ANIMATION_PATCH_SIZE) != 0)
        goto retain;
    dluma_atomic_exchange(
        &g_dluma_editor_model_animation_fix.installed, 0);
    if (!dluma_editor_model_animation_wait_active() ||
        !dluma_editor_model_animation_free_trampoline())
        goto retain;
    dluma_editor_model_animation_log_final_count();
    dluma_editor_model_animation_lifecycle_mark_restored(
        &g_dluma_editor_model_animation_lifecycle);
    memset(&g_dluma_editor_model_animation_fix, 0,
           sizeof(g_dluma_editor_model_animation_fix));
    success = 1;
    goto done;
retain:
    dluma_editor_model_animation_log_final_count();
    dluma_editor_model_animation_mark_retained();
    dluma_log_line(
        "ERROR",
        g_dluma_editor_model_animation_fix.host_profile ==
                DLUMA_EDITOR_MODEL_ANIMATION_HOST_PLAYER
            ? "Player animation-bind detour callbacks, ownership, restoration, or trampoline release failed; the detour, loader, and complete gamedll generation are retained until process exit."
            : "Editor animation-bind detour callbacks, ownership, restoration, or trampoline release failed; the complete gamedll generation was retained and reinitialization is disabled.");
done:
    dluma_editor_model_animation_release();
    return success;
}

void dluma_editor_model_animation_fix_retain(void)
{
    dluma_editor_model_animation_acquire();
    if (g_dluma_editor_model_animation_lifecycle.current_generation ||
        g_dluma_editor_model_animation_lifecycle.installed) {
        dluma_editor_model_animation_log_final_count();
        dluma_editor_model_animation_mark_retained();
    }
    dluma_editor_model_animation_release();
}
