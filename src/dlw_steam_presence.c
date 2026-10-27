#include "dlw_steam_presence.h"

#include "dlw_dual_host_runtime.h"
#include "dlw_log.h"
#include "dlw_path_security.h"
#include "dlw_sha256_internal.h"
#include "dlw_steam_presence_core.h"

#define DLUMA_STEAM_API_MAX_BYTES (1024ULL * 1024ULL)
#define DLUMA_STEAM_FRIENDS_RVA 0x000088F0UL
#define DLUMA_STEAM_SET_RICH_PRESENCE_RVA 0x00004270UL

typedef void* (__cdecl *DlumaSteamFriendsFn)(void);
typedef unsigned char (__cdecl *DlumaSteamSetRichPresenceFn)(
    void* friends, const char* key, const char* value);

typedef union DlumaSteamProcedureAddress {
    FARPROC win32;
    const void* address;
    DlumaSteamFriendsFn friends;
    DlumaSteamSetRichPresenceFn set_rich_presence;
} DlumaSteamProcedureAddress;

typedef struct DlumaSteamPresenceController {
    volatile LONG started;
    volatile LONG enabled_requested;
    volatile LONG stopping;
    HMODULE module;
    void* friends;
    DlumaSteamSetRichPresenceFn set_rich_presence;
    DlumaSteamPresenceCore core;
    unsigned char resolved;
    unsigned char permanent_failure;
    unsigned char unavailable_logged;
    unsigned char rejected_logged;
    unsigned char ready_logged;
    unsigned int observed_context;
    unsigned int last_action_kind;
    unsigned int last_action_context;
    unsigned int last_action_result;
    char last_action_value[DLUMA_STEAM_STATUS_CAPACITY];
} DlumaSteamPresenceController;

static DlumaSteamPresenceController g_dluma_steam_presence;

static const char* dluma_steam_presence_context_name(unsigned int context)
{
    switch ((DlumaPresenceContextKind)context) {
    case DLUMA_PRESENCE_CONTEXT_FRONTEND:
        return "main_menu";
    case DLUMA_PRESENCE_CONTEXT_GAMEPLAY:
        return "gameplay";
    default:
        return "none";
    }
}

static void dluma_steam_presence_log_action_result(
    const DlumaSteamPresenceAction* action, int succeeded)
{
    unsigned int context;
    unsigned int result = succeeded ? 1U : 2U;
    const char* value;
    if (!action || action->kind == DLUMA_STEAM_ACTION_NONE)
        return;
    context = action->kind == DLUMA_STEAM_ACTION_SET_STATUS
        ? g_dluma_steam_presence.observed_context
        : DLUMA_PRESENCE_CONTEXT_NONE;
    value = action->kind == DLUMA_STEAM_ACTION_SET_STATUS
        ? action->value
        : "<cleared>";
    if (g_dluma_steam_presence.last_action_kind == action->kind &&
        g_dluma_steam_presence.last_action_context == context &&
        g_dluma_steam_presence.last_action_result == result &&
        dluma_stricmp(g_dluma_steam_presence.last_action_value, value) == 0)
        return;
    g_dluma_steam_presence.last_action_kind = action->kind;
    g_dluma_steam_presence.last_action_context = context;
    g_dluma_steam_presence.last_action_result = result;
    dluma_str_copy(
        g_dluma_steam_presence.last_action_value,
        DLUMA_ARRAY_COUNT(g_dluma_steam_presence.last_action_value), value);
    dluma_log_pair(
        "INFO", "steam_presence_context",
        dluma_steam_presence_context_name(context));
    dluma_log_pair("INFO", "steam_presence_status", value);
    dluma_log_pair(
        succeeded ? "INFO" : "WARN", "steam_presence_result",
        succeeded ? "accepted" : "rejected");
    if (action->kind == DLUMA_STEAM_ACTION_REMOVE_STATUS)
        dluma_log_pair(
            succeeded ? "INFO" : "WARN", "steam_presence_clear_result",
            succeeded ? "accepted" : "rejected");
}

static DlumaSteamPresenceHostProfile
dluma_steam_presence_retail_profile_seam(void)
{
    /* Reserved for a separately evidenced retail implementation. */
    return DLUMA_STEAM_HOST_RETAIL_UNSUPPORTED;
}

static DlumaSteamPresenceHostProfile dluma_steam_presence_host_profile(void)
{
    DlumaHostKind kind = dluma_dual_host_runtime_kind();
    if (kind == DLUMA_HOST_PLAYER_STANDALONE ||
        kind == DLUMA_HOST_PLAYER_EDITOR_PLAYTEST)
        return DLUMA_STEAM_HOST_PLAYER;
    if (kind == DLUMA_HOST_UNKNOWN)
        return dluma_steam_presence_retail_profile_seam();
    return DLUMA_STEAM_HOST_UNSUPPORTED;
}

static int dluma_steam_presence_host_supported(void)
{
    return dluma_steam_presence_host_profile() == DLUMA_STEAM_HOST_PLAYER;
}

static int dluma_steam_presence_pointer_owned_by_module(
    const void* pointer, HMODULE module)
{
    MEMORY_BASIC_INFORMATION memory;
    memset(&memory, 0, sizeof(memory));
    return pointer && module &&
           VirtualQuery(pointer, &memory, sizeof(memory)) == sizeof(memory) &&
           memory.AllocationBase == module;
}

static int dluma_steam_presence_interface_readable(void* friends)
{
    void* vtable = 0;
    if (!friends || !dluma_is_readable(friends, sizeof(vtable)))
        return 0;
    __try {
        memcpy(&vtable, friends, sizeof(vtable));
    } __except (1) {
        vtable = 0;
    }
    return vtable && dluma_is_readable(vtable, sizeof(void*));
}

static int dluma_steam_presence_module_identity(
    HMODULE module, DWORD* timestamp_out, DWORD* image_size_out)
{
    const BYTE* base = (const BYTE*)module;
    DWORD nt;
    if (!module || !dluma_is_readable(base, 0x100U) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    memcpy(&nt, base + 0x3CU, sizeof(nt));
    if (nt > 0x10000000UL ||
        !dluma_is_readable(base + nt, 0x100U) ||
        memcmp(base + nt, "PE\0\0", 4U) != 0)
        return 0;
    if (timestamp_out)
        memcpy(timestamp_out, base + nt + 8U, sizeof(*timestamp_out));
    if (image_size_out)
        memcpy(
            image_size_out, base + nt + 24U + 56U,
            sizeof(*image_size_out));
    return 1;
}

static int dluma_steam_presence_canonical_colocation(
    HMODULE module, WCHAR* module_final, unsigned int capacity,
    char sha256[DLUMA_SHA256_HEX_CAPACITY])
{
    HANDLE executable_file = INVALID_HANDLE_VALUE;
    HANDLE module_file = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION executable_information;
    BY_HANDLE_FILE_INFORMATION module_information;
    WCHAR executable_path[DLUMA_MAX_WPATH];
    WCHAR module_path[DLUMA_MAX_WPATH];
    WCHAR executable_final[DLUMA_MAX_WPATH];
    WCHAR executable_directory[DLUMA_MAX_WPATH];
    WCHAR module_directory[DLUMA_MAX_WPATH];
    int result = 0;
    if (module_final && capacity) module_final[0] = 0;
    if (sha256) sha256[0] = 0;
    if (!module || !module_final || capacity < DLUMA_MAX_WPATH ||
        !sha256 ||
        !dluma_get_module_path_checked(
            0, executable_path, DLUMA_ARRAY_COUNT(executable_path)) ||
        !dluma_get_module_path_checked(
            module, module_path, DLUMA_ARRAY_COUNT(module_path)))
        return 0;
    executable_file = CreateFileW(
        executable_path, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    module_file = CreateFileW(
        module_path, GENERIC_READ, FILE_SHARE_READ,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (executable_file == INVALID_HANDLE_VALUE ||
        module_file == INVALID_HANDLE_VALUE)
        goto cleanup;
    memset(&executable_information, 0, sizeof(executable_information));
    memset(&module_information, 0, sizeof(module_information));
    if (!GetFileInformationByHandle(
            executable_file, &executable_information) ||
        !GetFileInformationByHandle(module_file, &module_information) ||
        (executable_information.dwFileAttributes &
             FILE_ATTRIBUTE_DIRECTORY) ||
        (module_information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        !dluma_get_final_path_checked(
            executable_file, executable_final,
            DLUMA_ARRAY_COUNT(executable_final)) ||
        !dluma_get_final_path_checked(
            module_file, module_final, capacity) ||
        !dluma_sha256_handle_hex(
            module_file, DLUMA_STEAM_API_MAX_BYTES, sha256))
        goto cleanup;
    dluma_wstr_copy(
        executable_directory, DLUMA_ARRAY_COUNT(executable_directory),
        executable_final);
    dluma_wstr_copy(
        module_directory, DLUMA_ARRAY_COUNT(module_directory), module_final);
    dluma_path_dirname(executable_directory);
    dluma_path_dirname(module_directory);
    result = executable_directory[0] && module_directory[0] &&
             dluma_wstricmp(executable_directory, module_directory) == 0;
cleanup:
    if (module_file != INVALID_HANDLE_VALUE) CloseHandle(module_file);
    if (executable_file != INVALID_HANDLE_VALUE) CloseHandle(executable_file);
    if (!result) {
        module_final[0] = 0;
        sha256[0] = 0;
    }
    return result;
}

static void dluma_steam_presence_log_unavailable_once(void)
{
    if (g_dluma_steam_presence.unavailable_logged) return;
    g_dluma_steam_presence.unavailable_logged = 1;
    dluma_log_line(
        "INFO",
        "Steam Rich Presence is waiting for the already-loaded Player Steam API.");
}

static void dluma_steam_presence_log_rejected_once(void)
{
    if (g_dluma_steam_presence.rejected_logged) return;
    g_dluma_steam_presence.rejected_logged = 1;
    dluma_log_line(
        "WARN",
        "Steam Rich Presence rejected steam_api64.dll because its exact Player identity or exports did not match.");
}

static int dluma_steam_presence_resolve(void)
{
    static const BYTE friends_prefix[8] = {
        0x48, 0x8B, 0x05, 0xA1, 0xDD, 0x02, 0x00, 0xC3
    };
    static const BYTE setter_prefix[10] = {
        0x48, 0x8B, 0x01, 0x48, 0xFF,
        0xA0, 0x58, 0x01, 0x00, 0x00
    };
    DlumaSteamPresenceModuleEvidence evidence;
    DlumaSteamPresenceModuleAssessment assessment;
    DlumaSteamProcedureAddress friends_address;
    DlumaSteamProcedureAddress setter_address;
    DlumaSteamFriendsFn friends_function = 0;
    HMODULE module;
    WCHAR module_final[DLUMA_MAX_WPATH];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    DWORD timestamp = 0;
    DWORD image_size = 0;
    void* friends = 0;

    if (g_dluma_steam_presence.permanent_failure) return 0;
    if (g_dluma_steam_presence.resolved) {
        module = GetModuleHandleW(L"steam_api64.dll");
        if (module == g_dluma_steam_presence.module &&
            dluma_steam_presence_pointer_owned_by_module(
                (const void*)g_dluma_steam_presence.set_rich_presence,
                module))
            return 1;
        g_dluma_steam_presence.resolved = 0;
        g_dluma_steam_presence.module = 0;
        g_dluma_steam_presence.friends = 0;
        g_dluma_steam_presence.set_rich_presence = 0;
    }

    memset(&evidence, 0, sizeof(evidence));
    evidence.host_profile = dluma_steam_presence_host_profile();
    if (evidence.host_profile != DLUMA_STEAM_HOST_PLAYER) {
        assessment = dluma_steam_presence_assess_module(&evidence);
        if (assessment == DLUMA_STEAM_MODULE_UNSUPPORTED)
            g_dluma_steam_presence.permanent_failure = 1;
        return 0;
    }

    module = GetModuleHandleW(L"steam_api64.dll");
    evidence.module_present = module != 0;
    if (!module) {
        dluma_steam_presence_log_unavailable_once();
        return 0;
    }
    module_final[0] = 0;
    evidence.canonical_colocation =
        dluma_steam_presence_canonical_colocation(
            module, module_final, DLUMA_ARRAY_COUNT(module_final), sha256);
    evidence.hash_match = evidence.canonical_colocation &&
        dluma_stricmp(sha256, DLUMA_STEAM_PLAYER_API_SHA256) == 0;
    evidence.pe_identity_match =
        dluma_steam_presence_module_identity(
            module, &timestamp, &image_size) &&
        timestamp == DLUMA_STEAM_PLAYER_API_TIMESTAMP &&
        image_size == DLUMA_STEAM_PLAYER_API_IMAGE_SIZE;

    friends_address.win32 = GetProcAddress(module, "SteamFriends");
    setter_address.win32 = GetProcAddress(
        module, "SteamAPI_ISteamFriends_SetRichPresence");
    evidence.accessor_export_present =
        friends_address.win32 != 0 &&
        (const BYTE*)friends_address.address ==
            (const BYTE*)module + DLUMA_STEAM_FRIENDS_RVA;
    evidence.setter_export_present =
        setter_address.win32 != 0 &&
        (const BYTE*)setter_address.address ==
            (const BYTE*)module + DLUMA_STEAM_SET_RICH_PRESENCE_RVA;
    evidence.export_pointer_ownership =
        dluma_steam_presence_pointer_owned_by_module(
            friends_address.address, module) &&
        dluma_steam_presence_pointer_owned_by_module(
            setter_address.address, module);
    evidence.export_bytes_match =
        evidence.accessor_export_present &&
        evidence.setter_export_present &&
        dluma_is_readable(friends_address.address, sizeof(friends_prefix)) &&
        dluma_is_readable(setter_address.address, sizeof(setter_prefix)) &&
        memcmp(
            friends_address.address,
            friends_prefix, sizeof(friends_prefix)) == 0 &&
        memcmp(
            setter_address.address,
            setter_prefix, sizeof(setter_prefix)) == 0;

    friends_function = friends_address.friends;
    if (evidence.canonical_colocation && evidence.hash_match &&
        evidence.pe_identity_match && evidence.accessor_export_present &&
        evidence.setter_export_present && evidence.export_bytes_match &&
        evidence.export_pointer_ownership) {
        __try {
            friends = friends_function();
        } __except (1) {
            friends = 0;
        }
    }
    evidence.accessor_succeeded =
        dluma_steam_presence_interface_readable(friends);
    assessment = dluma_steam_presence_assess_module(&evidence);
    if (assessment == DLUMA_STEAM_MODULE_REJECTED) {
        g_dluma_steam_presence.permanent_failure = 1;
        dluma_steam_presence_log_rejected_once();
        return 0;
    }
    if (assessment != DLUMA_STEAM_MODULE_READY) {
        dluma_steam_presence_log_unavailable_once();
        return 0;
    }
    g_dluma_steam_presence.module = module;
    g_dluma_steam_presence.friends = friends;
    g_dluma_steam_presence.set_rich_presence =
        setter_address.set_rich_presence;
    g_dluma_steam_presence.resolved = 1;
    g_dluma_steam_presence.unavailable_logged = 0;
    if (!g_dluma_steam_presence.ready_logged) {
        g_dluma_steam_presence.ready_logged = 1;
        dluma_log_line(
            "INFO",
            "Steam Rich Presence validated the exact Player Steam API.");
    }
    return 1;
}

static int dluma_steam_presence_execute(
    const DlumaSteamPresenceAction* action)
{
    unsigned char succeeded = 0;
    const char* value;
    if (!action || action->kind == DLUMA_STEAM_ACTION_NONE ||
        !dluma_steam_presence_resolve())
        return 0;
    value = action->kind == DLUMA_STEAM_ACTION_SET_STATUS
        ? action->value
        : 0;
    __try {
        succeeded = g_dluma_steam_presence.set_rich_presence(
            g_dluma_steam_presence.friends, "status", value);
    } __except (1) {
        succeeded = 0;
    }
    if (!succeeded) {
        g_dluma_steam_presence.friends = 0;
        g_dluma_steam_presence.set_rich_presence = 0;
        g_dluma_steam_presence.module = 0;
        g_dluma_steam_presence.resolved = 0;
    }
    return succeeded != 0;
}

int dluma_steam_presence_start(void)
{
    int effective_enabled;
    memset(&g_dluma_steam_presence, 0, sizeof(g_dluma_steam_presence));
    effective_enabled =
        g_dluma.config.steam_rich_presence_enabled != 0 &&
        dluma_steam_presence_host_supported();
    dluma_steam_presence_core_initialize(
        &g_dluma_steam_presence.core,
        effective_enabled);
    g_dluma_steam_presence.enabled_requested =
        g_dluma.config.steam_rich_presence_enabled ? 1 : 0;
    g_dluma_steam_presence.started = 1;
    dluma_log_line(
        "INFO",
        g_dluma_steam_presence.enabled_requested
            ? "Steam Rich Presence controller started (enabled)."
            : "Steam Rich Presence controller started (disabled).");
    return 1;
}

void dluma_steam_presence_set_enabled(int enabled)
{
    dluma_atomic_exchange(
        &g_dluma_steam_presence.enabled_requested,
        enabled ? 1 : 0);
}

int dluma_steam_presence_is_enabled(void)
{
    return dluma_atomic_compare_exchange(
               &g_dluma_steam_presence.enabled_requested, 0, 0) != 0 &&
           !dluma_atomic_compare_exchange(
               &g_dluma_steam_presence.stopping, 0, 0) &&
           dluma_steam_presence_host_supported();
}

int dluma_steam_presence_needs_game_thread_service(void)
{
    return dluma_steam_presence_is_enabled() ||
           g_dluma_steam_presence.core.owns_status != 0 ||
           g_dluma_steam_presence.core.enabled !=
               (unsigned int)dluma_atomic_compare_exchange(
                   &g_dluma_steam_presence.enabled_requested, 0, 0);
}

void dluma_steam_presence_game_thread_service(void)
{
    DlumaSteamPresenceAction action;
    int enabled;
    int succeeded;
    ULONGLONG now_ms;
    if (!dluma_atomic_compare_exchange(
            &g_dluma_steam_presence.started, 0, 0) ||
        dluma_atomic_compare_exchange(
            &g_dluma_steam_presence.stopping, 0, 0))
        return;
    enabled = dluma_atomic_compare_exchange(
        &g_dluma_steam_presence.enabled_requested, 0, 0) != 0 &&
        dluma_steam_presence_host_supported();
    if (g_dluma_steam_presence.core.enabled != (unsigned int)enabled)
        dluma_steam_presence_core_set_enabled(
            &g_dluma_steam_presence.core, enabled);
    now_ms = GetTickCount64();
    action = dluma_steam_presence_core_next_action(
        &g_dluma_steam_presence.core, now_ms);
    if (action.kind == DLUMA_STEAM_ACTION_NONE) return;
    succeeded = dluma_steam_presence_execute(&action);
    dluma_steam_presence_core_complete(
        &g_dluma_steam_presence.core, &action, succeeded, now_ms);
    dluma_steam_presence_log_action_result(&action, succeeded);
    if (!succeeded &&
        g_dluma_steam_presence.core.next_retry_ms > now_ms)
        dluma_log_u64(
            "WARN", "steam_presence_retry_delay_ms",
            g_dluma_steam_presence.core.next_retry_ms - now_ms);
}

void dluma_steam_presence_game_thread_observe(
    const DlumaPresenceSnapshot* snapshot)
{
    if (snapshot &&
        snapshot->struct_size >= sizeof(DlumaPresenceSnapshot))
        g_dluma_steam_presence.observed_context = snapshot->context;
    else
        g_dluma_steam_presence.observed_context =
            DLUMA_PRESENCE_CONTEXT_NONE;
    dluma_steam_presence_core_observe(
        &g_dluma_steam_presence.core, snapshot);
    dluma_steam_presence_game_thread_service();
}

void dluma_steam_presence_game_thread_expire(void)
{
    g_dluma_steam_presence.observed_context =
        DLUMA_PRESENCE_CONTEXT_NONE;
    dluma_steam_presence_core_observe(
        &g_dluma_steam_presence.core, 0);
    dluma_steam_presence_game_thread_service();
}

void dluma_steam_presence_quiesce(void)
{
    dluma_atomic_exchange(&g_dluma_steam_presence.stopping, 1);
    dluma_atomic_exchange(
        &g_dluma_steam_presence.enabled_requested, 0);
}

void dluma_steam_presence_stop(void)
{
    dluma_steam_presence_quiesce();
    g_dluma_steam_presence.resolved = 0;
    g_dluma_steam_presence.module = 0;
    g_dluma_steam_presence.friends = 0;
    g_dluma_steam_presence.set_rich_presence = 0;
    dluma_atomic_exchange(&g_dluma_steam_presence.started, 0);
}
