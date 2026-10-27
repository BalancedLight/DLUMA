#include "dlw_discord_presence.h"
#include "dlw_frontend_level.h"
#include "dlw_dual_host_runtime.h"

#include "dlw_discord_presence_api.h"
#include "dlw_discord_presence_core.h"
#include "dlw_presence_core.h"
#include "dlw_player_media_adapter.h"
#include "dlw_log.h"
#include "dlw_main_menu_presentation_core.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"
#include "dlw_quest_hud.h"
#include "dlw_steam_presence.h"

typedef char DlumaDiscordUiVisibilityBitsMustMatch[
    (DLUMA_UI_MENU_VISIBLE_SELECTING_MAP ==
             DLUMA_DISCORD_MENU_VISIBLE_SELECTING_MAP &&
     DLUMA_UI_MENU_VISIBLE_SETTINGS ==
             DLUMA_DISCORD_MENU_VISIBLE_SETTINGS &&
     DLUMA_UI_MENU_VISIBLE_MODS ==
             DLUMA_DISCORD_MENU_VISIBLE_MODS &&
     DLUMA_UI_MENU_VISIBLE_CREDITS ==
             DLUMA_DISCORD_MENU_VISIBLE_CREDITS &&
     DLUMA_UI_MENU_VISIBLE_STATISTICS ==
             DLUMA_DISCORD_MENU_VISIBLE_STATISTICS)
        ? 1
        : -1];

#define DLUMA_DISCORD_DYING_LIGHT_APPLICATION_ID 1531147761955766444ULL
#define DLUMA_DISCORD_DEVELOPER_TOOLS_APPLICATION_ID 1521173705772765376ULL
#define DLUMA_DISCORD_PUBLISH_INTERVAL_MS 500ULL
#define DLUMA_DISCORD_TRANSITION_DEBOUNCE_MS 250U
#define DLUMA_DISCORD_RETRY_INITIAL_MS 1000ULL
#define DLUMA_DISCORD_RETRY_MAX_MS 30000ULL
#define DLUMA_DISCORD_WORKER_WAIT_MS 25U
#define DLUMA_DISCORD_SHUTDOWN_TIMEOUT_MS 4000U
#define DLUMA_DISCORD_DEVTOOLS_GAMEDLL_TIMESTAMP 0x623397AAUL
#define DLUMA_DISCORD_DEVTOOLS_GAMEDLL_IMAGE_SIZE 0x02082000UL
#define DLUMA_DISCORD_GAMEDI_FRAME_SUBOBJECT_OFFSET 0x10U
#define DLUMA_DISCORD_GAMEDI_PAUSE_FLAGS_OFFSET 0x880U
#define DLUMA_DISCORD_GAMEDI_PAUSE_MASK 0x1U
#define DLUMA_DISCORD_IMAGE_FILE_MACHINE_AMD64 0x8664U
#define DLUMA_DISCORD_IMAGE_FILE_DLL 0x2000U

typedef struct DlumaDiscordTtlString {
    const char* data;
    unsigned int size;
    unsigned int capacity;
} DlumaDiscordTtlString;

typedef char DlumaDiscordTtlStringSizeMustBe16[
    sizeof(DlumaDiscordTtlString) == 16 ? 1 : -1];

typedef union DlumaDiscordProcedureAddress {
    const void* address;
    FARPROC win32;
    DlumaDiscordInitializeFn initialize;
    DlumaDiscordPublishFn publish;
    DlumaDiscordClearFn clear;
    DlumaDiscordPumpFn pump;
    DlumaDiscordStatusFn status;
    DlumaDiscordQuiesceFn quiesce;
} DlumaDiscordProcedureAddress;

typedef struct DlumaDiscordController {
    HANDLE stop_event;
    HANDLE wake_event;
    HANDLE worker_thread;
    HMODULE helper_module;
    const DlumaDiscordPresenceApiV1* api;
    ULONGLONG application_id;
    volatile LONG enabled;
    volatile LONG suspended;
    volatile LONG stopping;
    volatile LONG stop_succeeded;
    volatile LONG clear_requested;
    volatile LONG snapshot_sequence;
    volatile LONG snapshot_generation;
    DlumaDiscordActivityV1 latest_activity;
    DlumaDiscordActivityV1 committed_activity;
    DlumaDiscordActivityV1 candidate_activity;
    DlumaDiscordDebounceState debounce;
    DlumaDiscordTimerState timer;
    unsigned int committed_context;
    unsigned int candidate_context;
    unsigned char committed_valid;
    unsigned char candidate_valid;
    unsigned char helper_initialized;
    unsigned char unavailable_logged;
    unsigned char absence_clear_published;
    ULONGLONG last_publish_ms;
    ULONGLONG next_retry_ms;
    ULONGLONG retry_delay_ms;
    ULONGLONG observed_callback_sequence;
    unsigned int host_kind;
    DlumaDiscordActivityV1 last_published;
    unsigned char last_published_valid;
} DlumaDiscordController;

static DlumaDiscordController g_dluma_discord;

static ULONGLONG dluma_discord_unix_seconds(void)
{
    FILETIME file_time;
    ULONGLONG ticks;
    const ULONGLONG unix_epoch_ticks =
        116444736000000000ULL;

    GetSystemTimeAsFileTime(&file_time);
    ticks =
        ((ULONGLONG)file_time.dwHighDateTime << 32) |
        (ULONGLONG)file_time.dwLowDateTime;
    if (ticks <= unix_epoch_ticks) return 0;
    return (ticks - unix_epoch_ticks) /
           10000000ULL;
}

static unsigned int dluma_discord_text_length(
    const char* text, unsigned int capacity)
{
    unsigned int length = 0;
    if (!text) return 0;
    while (length < capacity && text[length]) ++length;
    return length;
}

static int dluma_discord_text_equal(const char* left, const char* right)
{
    unsigned int i = 0;
    if (!left || !right) return left == right;
    for (;;) {
        if (left[i] != right[i]) return 0;
        if (!left[i]) return 1;
        ++i;
    }
}

static void dluma_discord_text_append(
    char* output, unsigned int capacity, const char* text)
{
    unsigned int offset;
    unsigned int i = 0;
    if (!output || !capacity || !text) return;
    offset = dluma_discord_text_length(output, capacity);
    if (offset >= capacity) {
        output[capacity - 1] = 0;
        return;
    }
    while (text[i] && offset + 1 < capacity)
        output[offset++] = text[i++];
    output[offset] = 0;
}

static void dluma_discord_copy_sanitized(
    char* output, unsigned int output_capacity,
    const char* input, unsigned int input_capacity,
    unsigned int max_codepoints)
{
    unsigned int input_size;
    if (!output || !output_capacity) return;
    output[0] = 0;
    if (!input || !input_capacity) return;
    input_size = dluma_discord_text_length(input, input_capacity);
    if (input_size == input_capacity) return;
    (void)dluma_discord_utf8_sanitize(
        input, input_size, output, output_capacity, max_codepoints);
}

static void dluma_discord_localized(
    const char* key, const char* fallback,
    char* output, unsigned int capacity)
{
    char raw[DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY];
    raw[0] = 0;
    if (!dluma_text_get_utf8(key, fallback, raw, DLUMA_ARRAY_COUNT(raw)))
        dluma_str_copy(raw, DLUMA_ARRAY_COUNT(raw), fallback);
    dluma_discord_copy_sanitized(
        output, capacity, raw, DLUMA_ARRAY_COUNT(raw), 128U);
}

static void dluma_discord_format_name(
    const char* format_key, const char* format_fallback,
    const char* value,
    char* output, unsigned int capacity)
{
    char format[DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY];
    format[0] = 0;
    dluma_discord_localized(
        format_key, format_fallback, format, DLUMA_ARRAY_COUNT(format));
    if (!dluma_discord_apply_name_format(
            format, DLUMA_ARRAY_COUNT(format),
            value, DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY,
            output, capacity, 128U)) {
        dluma_str_copy(
            format, DLUMA_ARRAY_COUNT(format), format_fallback);
        (void)dluma_discord_apply_name_format(
            format, DLUMA_ARRAY_COUNT(format),
            value, DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY,
            output, capacity, 128U);
    }
}

static void dluma_discord_format_version(
    const char* format_key, const char* format_fallback,
    char* output, unsigned int capacity)
{
    DlumaMainMenuPresentationPlaceholder placeholder;
    char format[DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY];
    format[0] = 0;
    placeholder.token = "{version}";
    placeholder.value = DLUMA_VERSION;
    dluma_discord_localized(
        format_key, format_fallback,
        format, DLUMA_ARRAY_COUNT(format));
    if (!dluma_main_menu_presentation_expand(
            format, &placeholder, 1U, output, capacity)) {
        (void)dluma_main_menu_presentation_expand(
            format_fallback, &placeholder, 1U,
            output, capacity);
    }
}

static unsigned int dluma_discord_u16(const BYTE* value)
{
    return (unsigned int)value[0] | ((unsigned int)value[1] << 8);
}

static unsigned int dluma_discord_u32(const BYTE* value)
{
    return (unsigned int)value[0] |
           ((unsigned int)value[1] << 8) |
           ((unsigned int)value[2] << 16) |
           ((unsigned int)value[3] << 24);
}

static int dluma_discord_pe_is_x64_dll(const WCHAR* path)
{
    HANDLE file;
    BYTE header[4096];
    DWORD read = 0;
    unsigned int nt;
    unsigned int machine;
    unsigned int characteristics;
    file = CreateFileW(
        path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(file, header, sizeof(header), &read, 0)) read = 0;
    CloseHandle(file);
    if (read < 0x100U || header[0] != 'M' || header[1] != 'Z')
        return 0;
    nt = dluma_discord_u32(header + 0x3CU);
    if (nt > read - 24U ||
        dluma_discord_u32(header + nt) != 0x00004550U)
        return 0;
    machine = dluma_discord_u16(header + nt + 4U);
    characteristics = dluma_discord_u16(header + nt + 22U);
    return machine == DLUMA_DISCORD_IMAGE_FILE_MACHINE_AMD64 &&
           (characteristics & DLUMA_DISCORD_IMAGE_FILE_DLL) != 0;
}

static int dluma_discord_file_final_path(
    const WCHAR* path, int directory,
    WCHAR* final_path, unsigned int capacity)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION information;
    file = CreateFileW(
        path, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        0, OPEN_EXISTING,
        directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileInformationByHandle(file, &information) ||
        (directory
             ? !(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
             : (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ||
        !dluma_get_final_path_checked(file, final_path, capacity)) {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    return 1;
}

static int dluma_discord_module_matches_final_path(
    HMODULE module, const WCHAR* expected_final)
{
    WCHAR loaded[DLUMA_MAX_WPATH];
    WCHAR loaded_final[DLUMA_MAX_WPATH];
    if (!module ||
        !dluma_get_module_path_checked(
            module, loaded, DLUMA_ARRAY_COUNT(loaded)) ||
        !dluma_discord_file_final_path(
            loaded, 0, loaded_final, DLUMA_ARRAY_COUNT(loaded_final)))
        return 0;
    return dluma_wstricmp(loaded_final, expected_final) == 0;
}

static int dluma_discord_pointer_owned_by_module(
    const void* pointer, HMODULE module)
{
    MEMORY_BASIC_INFORMATION memory;
    memset(&memory, 0, sizeof(memory));
    return pointer &&
           VirtualQuery(pointer, &memory, sizeof(memory)) == sizeof(memory) &&
           memory.AllocationBase == module;
}

static int dluma_discord_prepare_candidate(
    const WCHAR* basename,
    WCHAR* path_out, WCHAR* final_out)
{
    WCHAR module_final[DLUMA_MAX_WPATH];
    if (!dluma_checked_path_join(
            path_out, DLUMA_MAX_WPATH, g_dluma.module_dir, basename) ||
        !dluma_discord_file_final_path(
            g_dluma.module_dir, 1, module_final,
            DLUMA_ARRAY_COUNT(module_final)) ||
        !dluma_discord_file_final_path(
            path_out, 0, final_out, DLUMA_MAX_WPATH) ||
        !dluma_final_path_equal_or_under(final_out, module_final) ||
        !dluma_discord_pe_is_x64_dll(final_out))
        return 0;
    return 1;
}

static HANDLE dluma_discord_open_locked_candidate(
    const WCHAR* path, const WCHAR* expected_final)
{
    HANDLE file;
    WCHAR actual_final[DLUMA_MAX_WPATH];
    file = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    if (!dluma_get_final_path_checked(
            file, actual_final, DLUMA_ARRAY_COUNT(actual_final)) ||
        dluma_wstricmp(actual_final, expected_final) != 0) {
        CloseHandle(file);
        return INVALID_HANDLE_VALUE;
    }
    return file;
}

static HMODULE dluma_discord_load_validated_module(
    const WCHAR* basename,
    const WCHAR* candidate_path,
    const WCHAR* candidate_final)
{
    HMODULE module = GetModuleHandleW(basename);
    if (module &&
        !dluma_discord_module_matches_final_path(module, candidate_final))
        return 0;
    if (!module)
        module = LoadLibraryExW(
            candidate_path, 0,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module ||
        !dluma_discord_module_matches_final_path(module, candidate_final))
        return 0;
    return module;
}

static int dluma_discord_validate_api(
    const DlumaDiscordPresenceApiV1* api, HMODULE helper)
{
    DlumaDiscordProcedureAddress procedure;
    if (!api || !dluma_is_readable(api, sizeof(*api)) ||
        api->struct_size != sizeof(*api) ||
        api->abi_version != DLUMA_DISCORD_PRESENCE_ABI_VERSION ||
        !api->helper_release || !api->transport_release ||
        !dluma_is_readable(
            api->helper_release,
            sizeof(DLUMA_DISCORD_PRESENCE_HELPER_RELEASE)) ||
        !dluma_is_readable(
            api->transport_release,
            sizeof(DLUMA_DISCORD_RPC_TRANSPORT_RELEASE)) ||
        !dluma_discord_text_equal(
            api->helper_release, DLUMA_DISCORD_PRESENCE_HELPER_RELEASE) ||
        !dluma_discord_text_equal(
            api->transport_release,
            DLUMA_DISCORD_RPC_TRANSPORT_RELEASE) ||
        api->rpc_protocol_version !=
            DLUMA_DISCORD_RPC_PROTOCOL_VERSION ||
        !api->initialize || !api->publish || !api->clear || !api->pump ||
        !api->status || !api->quiesce)
        return 0;
    procedure.initialize = api->initialize;
    if (!dluma_discord_pointer_owned_by_module(procedure.address, helper))
        return 0;
    procedure.publish = api->publish;
    if (!dluma_discord_pointer_owned_by_module(procedure.address, helper))
        return 0;
    procedure.clear = api->clear;
    if (!dluma_discord_pointer_owned_by_module(procedure.address, helper))
        return 0;
    procedure.pump = api->pump;
    if (!dluma_discord_pointer_owned_by_module(procedure.address, helper))
        return 0;
    procedure.status = api->status;
    if (!dluma_discord_pointer_owned_by_module(procedure.address, helper))
        return 0;
    procedure.quiesce = api->quiesce;
    return dluma_discord_pointer_owned_by_module(
        procedure.address, helper);
}

static int dluma_discord_load_helper(void)
{
    WCHAR helper_path[DLUMA_MAX_WPATH];
    WCHAR helper_final[DLUMA_MAX_WPATH];
    HANDLE helper_file = INVALID_HANDLE_VALUE;
    FARPROC query;
    DlumaDiscordPresenceGetApiFn get_api;
    DlumaDiscordProcedureAddress query_address;
    const DlumaDiscordPresenceApiV1* api = 0;
    int result = 0;

    if (g_dluma_discord.api) return 1;
    if (!dluma_discord_prepare_candidate(
            L"dluma_discord_presence.dll", helper_path, helper_final))
        return 0;

    helper_file = dluma_discord_open_locked_candidate(
        helper_path, helper_final);
    if (helper_file == INVALID_HANDLE_VALUE ||
        !dluma_discord_pe_is_x64_dll(helper_path))
        goto cleanup;

    if (!g_dluma_discord.helper_module)
        g_dluma_discord.helper_module = dluma_discord_load_validated_module(
            L"dluma_discord_presence.dll", helper_path, helper_final);
    if (!g_dluma_discord.helper_module) goto cleanup;

    query = GetProcAddress(
        g_dluma_discord.helper_module, "DLUMA_DiscordPresence_GetApi");
    query_address.win32 = query;
    if (!query ||
        !dluma_discord_pointer_owned_by_module(
            query_address.address, g_dluma_discord.helper_module))
        goto cleanup;
    get_api = (DlumaDiscordPresenceGetApiFn)query;
    __try {
        if (!get_api(
                DLUMA_DISCORD_PRESENCE_ABI_VERSION,
                sizeof(DlumaDiscordPresenceApiV1), &api))
            api = 0;
    } __except (1) {
        api = 0;
    }
    if (!api ||
        !dluma_discord_validate_api(api, g_dluma_discord.helper_module))
        goto cleanup;
    g_dluma_discord.api = api;
    dluma_log_pair(
        "INFO", "discord_rpc_transport_release",
        DLUMA_DISCORD_RPC_TRANSPORT_RELEASE);
    result = 1;

cleanup:
    if (helper_file != INVALID_HANDLE_VALUE) CloseHandle(helper_file);
    return result;
}

static void dluma_discord_schedule_retry(ULONGLONG now_ms)
{
    if (!g_dluma_discord.retry_delay_ms)
        g_dluma_discord.retry_delay_ms = DLUMA_DISCORD_RETRY_INITIAL_MS;
    g_dluma_discord.next_retry_ms = now_ms + g_dluma_discord.retry_delay_ms;
    g_dluma_discord.retry_delay_ms = dluma_discord_next_backoff(
        g_dluma_discord.retry_delay_ms,
        DLUMA_DISCORD_RETRY_INITIAL_MS,
        DLUMA_DISCORD_RETRY_MAX_MS);
}

static int dluma_discord_read_latest(DlumaDiscordActivityV1* output)
{
    unsigned int attempt;
    if (!output) return 0;
    for (attempt = 0; attempt < 4U; ++attempt) {
        LONG before = dluma_atomic_compare_exchange(
            &g_dluma_discord.snapshot_sequence, 0, 0);
        LONG after;
        if (before & 1) continue;
        memcpy(output, &g_dluma_discord.latest_activity, sizeof(*output));
        after = dluma_atomic_compare_exchange(
            &g_dluma_discord.snapshot_sequence, 0, 0);
        if (before == after && !(after & 1))
            return output->generation != 0;
    }
    memset(output, 0, sizeof(*output));
    return 0;
}

static void dluma_discord_publish_to_worker(
    const DlumaDiscordActivityV1* activity)
{
    LONG sequence;
    if (!activity) return;
    sequence = dluma_atomic_increment(&g_dluma_discord.snapshot_sequence);
    if (!(sequence & 1))
        sequence = dluma_atomic_increment(&g_dluma_discord.snapshot_sequence);
    memcpy(&g_dluma_discord.latest_activity, activity, sizeof(*activity));
    dluma_atomic_increment(&g_dluma_discord.snapshot_sequence);
    if (g_dluma_discord.wake_event) SetEvent(g_dluma_discord.wake_event);
}

static void dluma_discord_clear_worker_snapshot(void)
{
    LONG sequence =
        dluma_atomic_increment(&g_dluma_discord.snapshot_sequence);
    if (!(sequence & 1))
        (void)dluma_atomic_increment(
            &g_dluma_discord.snapshot_sequence);
    memset(
        &g_dluma_discord.latest_activity, 0,
        sizeof(g_dluma_discord.latest_activity));
    (void)dluma_atomic_increment(
        &g_dluma_discord.snapshot_sequence);
}

static int dluma_discord_worker_enable(ULONGLONG now_ms)
{
    if (g_dluma_discord.helper_initialized) return 1;
    if (now_ms < g_dluma_discord.next_retry_ms) return 0;
    if (!dluma_discord_load_helper()) {
        if (!g_dluma_discord.unavailable_logged) {
            dluma_log_line(
                "INFO",
                "Discord Rich Presence is waiting for the Discord desktop client.");
            g_dluma_discord.unavailable_logged = 1;
        }
        dluma_discord_schedule_retry(now_ms);
        return 0;
    }
    (void)g_dluma_discord.api->pump();
    if (!g_dluma_discord.api->initialize(
            g_dluma_discord.application_id)) {
        if (!g_dluma_discord.unavailable_logged) {
            dluma_log_line(
                "INFO",
                "Discord Rich Presence is waiting for the Discord desktop client.");
            g_dluma_discord.unavailable_logged = 1;
        }
        dluma_discord_schedule_retry(now_ms);
        return 0;
    }
    g_dluma_discord.helper_initialized = 1;
    g_dluma_discord.unavailable_logged = 0;
    g_dluma_discord.retry_delay_ms = DLUMA_DISCORD_RETRY_INITIAL_MS;
    g_dluma_discord.next_retry_ms = 0;
    g_dluma_discord.last_published_valid = 0;
    return 1;
}

static void dluma_discord_worker_disable(void)
{
    if (g_dluma_discord.api && g_dluma_discord.helper_initialized)
        (void)g_dluma_discord.api->clear();
    g_dluma_discord.helper_initialized = 0;
    g_dluma_discord.last_published_valid = 0;
    g_dluma_discord.next_retry_ms = 0;
    g_dluma_discord.retry_delay_ms = DLUMA_DISCORD_RETRY_INITIAL_MS;
}

static DWORD WINAPI dluma_discord_worker(LPVOID parameter)
{
    DlumaDiscordActivityV1 activity;
    ULONGLONG handled_generation = 0;
    (void)parameter;
    for (;;) {
        ULONGLONG now_ms;
        DWORD wait_result = WaitForSingleObject(
            g_dluma_discord.stop_event, 0);
        if (wait_result == WAIT_OBJECT_0) break;
        (void)WaitForSingleObject(
            g_dluma_discord.wake_event, DLUMA_DISCORD_WORKER_WAIT_MS);
        now_ms = GetTickCount64();
        if (dluma_atomic_compare_exchange(
                &g_dluma_discord.suspended, 0, 0) ||
            !dluma_atomic_compare_exchange(
                &g_dluma_discord.enabled, 0, 0)) {
            dluma_discord_worker_disable();
            if (g_dluma_discord.api)
                (void)g_dluma_discord.api->pump();
            handled_generation = 0;
            continue;
        }
        if (dluma_atomic_exchange(
                &g_dluma_discord.clear_requested, 0)) {
            dluma_discord_worker_disable();
            if (g_dluma_discord.api)
                (void)g_dluma_discord.api->pump();
            handled_generation = 0;
            continue;
        }
        if (!dluma_discord_worker_enable(now_ms)) continue;
        if (!g_dluma_discord.api->pump()) {
            dluma_discord_worker_disable();
            handled_generation = 0;
            dluma_discord_schedule_retry(now_ms);
            continue;
        }
        {
            DlumaDiscordPresenceStatusV1 status;
            memset(&status, 0, sizeof(status));
            status.struct_size = sizeof(status);
            if (!g_dluma_discord.api->status(&status) ||
                !status.connected || !status.ready ||
                (!status.responses_pending &&
                 !status.last_operation_succeeded)) {
                dluma_discord_worker_disable();
                handled_generation = 0;
                dluma_discord_schedule_retry(now_ms);
                continue;
            }
        }
        if (dluma_discord_read_latest(&activity) &&
            activity.generation != handled_generation) {
            if (g_dluma_discord.last_published_valid &&
                dluma_discord_activity_equal(
                    &activity, &g_dluma_discord.last_published)) {
                handled_generation = activity.generation;
            } else if (!g_dluma_discord.last_publish_ms ||
                       now_ms - g_dluma_discord.last_publish_ms >=
                           DLUMA_DISCORD_PUBLISH_INTERVAL_MS) {
                if (g_dluma_discord.api->publish(&activity)) {
                    g_dluma_discord.last_published = activity;
                    g_dluma_discord.last_published_valid = 1;
                    g_dluma_discord.last_publish_ms = now_ms;
                    handled_generation = activity.generation;
                } else {
                    dluma_discord_worker_disable();
                    handled_generation = 0;
                    dluma_discord_schedule_retry(now_ms);
                }
            }
        }
    }
    if (g_dluma_discord.api) {
        if (!g_dluma_discord.api->quiesce(
                DLUMA_DISCORD_SHUTDOWN_TIMEOUT_MS))
            dluma_atomic_exchange(&g_dluma_discord.stop_succeeded, 0);
        else
            g_dluma_discord.helper_initialized = 0;
    }
    return 0;
}

static void dluma_discord_ttl_init(
    DlumaDiscordTtlString* value, const char* text)
{
    unsigned int length = dluma_discord_text_length(text, 256U);
    value->data = text;
    value->size = length;
    value->capacity = length;
}

static int dluma_discord_screen_visible(
    void* level, const char* script_name)
{
    DlumaDiscordTtlString exact;
    DlumaDiscordTtlString base;
    DlumaDiscordTtlString canonical;
    char base_text[128];
    char canonical_text[192];
    static const char canonical_prefix[] = "data\\menu\\scr\\";
    unsigned int length;
    int screen_id = -1;
    if (!level || !script_name ||
        !g_dluma.engine.get_ui_screen_id_by_script_name ||
        !g_dluma.engine.is_ui_screen_visible)
        return 0;
    dluma_discord_ttl_init(&exact, script_name);
    length = dluma_discord_text_length(
        script_name, DLUMA_ARRAY_COUNT(base_text));
    if (length >= 4U &&
        dluma_stricmp(script_name + length - 4U, ".xui") == 0)
        length -= 4U;
    if (length >= DLUMA_ARRAY_COUNT(base_text))
        return 0;
    memcpy(base_text, script_name, length);
    base_text[length] = 0;
    dluma_discord_ttl_init(&base, base_text);
    canonical_text[0] = 0;
    dluma_str_copy(
        canonical_text, DLUMA_ARRAY_COUNT(canonical_text),
        canonical_prefix);
    dluma_discord_text_append(
        canonical_text, DLUMA_ARRAY_COUNT(canonical_text),
        script_name);
    dluma_discord_ttl_init(&canonical, canonical_text);
    __try {
        screen_id = g_dluma.engine.get_ui_screen_id_by_script_name(
            level, &exact);
        if (screen_id < 0)
            screen_id = g_dluma.engine.get_ui_screen_id_by_script_name(
                level, &base);
        if (screen_id < 0)
            screen_id = g_dluma.engine.get_ui_screen_id_by_script_name(
                level, &canonical);
        if (screen_id < 0 ||
            (g_dluma.engine.is_ui_screen_loaded &&
             !g_dluma.engine.is_ui_screen_loaded(level, screen_id)))
            return 0;
        return g_dluma.engine.is_ui_screen_visible(level, screen_id) != 0;
    } __except (1) {
        return 0;
    }
}

static int dluma_discord_any_screen_visible(
    void* level, const char* const* screens, unsigned int count)
{
    unsigned int i;
    for (i = 0; i < count; ++i)
        if (dluma_discord_screen_visible(level, screens[i])) return 1;
    return 0;
}

static DlumaDiscordMenuActivity dluma_discord_menu_activity(void* level)
{
    unsigned int visibility =
        dluma_ui_discord_menu_visibility_snapshot();
    static const char* const selecting_map[] = {
        "DLM_SteamWorkshop.xui",
        "MenuSteamWorkshop.xui",
        "MenuCustomMap.xui"
    };
    static const char* const configuring_settings[] = {
        "MenuDLUMALoaderSettings.xui",
        "DLUMA_MenuOptions.xui",
        "MenuOptions.xui",
        "MenuOptionsAudio.xui",
        "MenuOptionsControls.xui",
        "MenuOptionsControlsGamepad.xui",
        "MenuOptionsControlsGamepad_Orbis.xui",
        "MenuOptionsControlsKeyboard.xui",
        "MenuOptionsEyeTracking.xui",
        "MenuOptionsGame.xui",
        "MenuOptionsIngame.xui",
        "MenuOptionsOnline.xui",
        "MenuOptionsVideo.xui",
        "MenuOptionsVideoIngame.xui",
        "MenuOptionsVideo_CONSOLE.xui",
        "MenuGamma.xui",
        "MenuDisplayDevice.xui",
        "MenuScreenAdjustment.xui"
    };
    static const char* const adjusting_mods[] = {
        "MenuDLUMAMods.xui",
        "MenuDLUMAModDetails.xui",
        "MenuDLUMAModConfig.xui"
    };
    static const char* const credits[] = {
        "MenuCredits.xui",
        "MenuCredits_PC.xui"
    };
    static const char* const statistics[] = {
        "MenuPersonalStatistics.xui"
    };
    if (dluma_discord_any_screen_visible(
            level, selecting_map, DLUMA_ARRAY_COUNT(selecting_map)))
        visibility |= DLUMA_DISCORD_MENU_VISIBLE_SELECTING_MAP;
    if (dluma_discord_any_screen_visible(
            level, configuring_settings,
            DLUMA_ARRAY_COUNT(configuring_settings)))
        visibility |= DLUMA_DISCORD_MENU_VISIBLE_SETTINGS;
    if (dluma_discord_any_screen_visible(
            level, adjusting_mods, DLUMA_ARRAY_COUNT(adjusting_mods)))
        visibility |= DLUMA_DISCORD_MENU_VISIBLE_MODS;
    if (dluma_discord_any_screen_visible(
            level, credits, DLUMA_ARRAY_COUNT(credits)))
        visibility |= DLUMA_DISCORD_MENU_VISIBLE_CREDITS;
    if (dluma_discord_any_screen_visible(
            level, statistics, DLUMA_ARRAY_COUNT(statistics)))
        visibility |= DLUMA_DISCORD_MENU_VISIBLE_STATISTICS;
    return dluma_discord_menu_from_visibility(visibility);
}

static const char* dluma_discord_menu_key(
    DlumaDiscordMenuActivity activity)
{
    switch (activity) {
    case DLUMA_DISCORD_MENU_SELECTING_MAP:
        return "DLUMA_Discord_SelectingMap";
    case DLUMA_DISCORD_MENU_CONFIGURING_SETTINGS:
        return "DLUMA_Discord_ConfiguringSettings";
    case DLUMA_DISCORD_MENU_ADJUSTING_MODS:
        return "DLUMA_Discord_AdjustingMods";
    case DLUMA_DISCORD_MENU_READING_CREDITS:
        return "DLUMA_Discord_ReadingCredits";
    case DLUMA_DISCORD_MENU_VIEWING_STATISTICS:
        return "DLUMA_Discord_ViewingStatistics";
    default:
        return "DLUMA_Discord_InMainMenu";
    }
}

static const char* dluma_discord_menu_fallback(
    DlumaDiscordMenuActivity activity)
{
    switch (activity) {
    case DLUMA_DISCORD_MENU_SELECTING_MAP: return "Selecting Map";
    case DLUMA_DISCORD_MENU_CONFIGURING_SETTINGS:
        return "Configuring Settings";
    case DLUMA_DISCORD_MENU_ADJUSTING_MODS: return "Adjusting Mods";
    case DLUMA_DISCORD_MENU_READING_CREDITS:
        return "Reading the Credits";
    case DLUMA_DISCORD_MENU_VIEWING_STATISTICS:
        return "Viewing Statistics";
    default: return "In the Main Menu";
    }
}

static void dluma_discord_build_menu_activity(
    DlumaDiscordMenuActivity menu,
    DlumaDiscordActivityV1* activity,
    DlumaPresenceSnapshot* snapshot)
{
    memset(activity, 0, sizeof(*activity));
    memset(snapshot, 0, sizeof(*snapshot));
    activity->struct_size = sizeof(*activity);
    snapshot->struct_size = sizeof(*snapshot);
    snapshot->context = DLUMA_PRESENCE_CONTEXT_FRONTEND;
    dluma_discord_localized(
        dluma_discord_menu_key(menu), dluma_discord_menu_fallback(menu),
        snapshot->primary_text,
        DLUMA_ARRAY_COUNT(snapshot->primary_text));
    dluma_str_copy(
        activity->details, DLUMA_ARRAY_COUNT(activity->details),
        snapshot->primary_text);
    dluma_str_copy(
        activity->large_image,
        DLUMA_ARRAY_COUNT(activity->large_image), "dl_menu");
    dluma_discord_format_version(
        "DLUMA_Discord_LoaderVersionFormat",
        "DLUMA v{version}",
        activity->large_text, DLUMA_ARRAY_COUNT(activity->large_text));
}

static int dluma_discord_module_identity(
    HMODULE module, DWORD* timestamp_out, DWORD* size_out)
{
    const BYTE* base = (const BYTE*)module;
    DWORD nt;
    if (!module || !dluma_is_readable(base, 0x100U) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    nt = *(const DWORD*)(base + 0x3CU);
    if (nt > 0x10000000UL ||
        !dluma_is_readable(base + nt, 0x100U) ||
        *(const DWORD*)(base + nt) != 0x00004550UL)
        return 0;
    if (timestamp_out) *timestamp_out = *(const DWORD*)(base + nt + 8U);
    if (size_out) *size_out = *(const DWORD*)(base + nt + 24U + 56U);
    return 1;
}

static int dluma_discord_pause_state(
    void* game_subobject, int* paused_out)
{
    static const BYTE frame_prefix[16] = {
        0x48, 0x8B, 0xC4, 0x55, 0x57, 0x48, 0x8D, 0xA8,
        0x18, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xD8
    };
    DlumaEngineState* state = &g_dluma.engine;
    BYTE* primary;
    DWORD timestamp = 0;
    DWORD image_size = 0;
    DWORD flags;
    if (paused_out) *paused_out = 0;
    if (!paused_out || !game_subobject || !state->ready ||
        (ULONG_PTR)game_subobject <
            DLUMA_DISCORD_GAMEDI_FRAME_SUBOBJECT_OFFSET ||
        !state->game_frame_hooked || state->game_frame_stopping ||
        game_subobject != dluma_engine_game_frame_route_subobject(state) ||
        !dluma_engine_game_frame_dispatch_active_on_current_thread(state) ||
        !dluma_engine_game_frame_hook_owned(state) ||
        !dluma_discord_module_identity(
            state->game_frame_module, &timestamp, &image_size) ||
        timestamp != DLUMA_DISCORD_DEVTOOLS_GAMEDLL_TIMESTAMP ||
        image_size != DLUMA_DISCORD_DEVTOOLS_GAMEDLL_IMAGE_SIZE ||
        !state->game_frame_original ||
        !dluma_is_readable(
            state->game_frame_original, sizeof(frame_prefix)) ||
        memcmp(
            state->game_frame_original,
            frame_prefix, sizeof(frame_prefix)) != 0 ||
        !state->game_frame_vtable_slot ||
        !state->game_frame_expected_primary_vtable ||
        !state->game_frame_expected_subobject_vtable)
        return 0;
    primary = (BYTE*)game_subobject -
        DLUMA_DISCORD_GAMEDI_FRAME_SUBOBJECT_OFFSET;
    if (!dluma_is_readable(
            primary,
            DLUMA_DISCORD_GAMEDI_PAUSE_FLAGS_OFFSET + sizeof(DWORD)) ||
        *(void**)primary != state->game_frame_expected_primary_vtable ||
        *(void**)game_subobject !=
            state->game_frame_expected_subobject_vtable)
        return 0;
    __try {
        flags = *(const DWORD*)(
            primary + DLUMA_DISCORD_GAMEDI_PAUSE_FLAGS_OFFSET);
    } __except (1) {
        return 0;
    }
    *paused_out = (flags & DLUMA_DISCORD_GAMEDI_PAUSE_MASK) != 0;
    return 1;
}

static void dluma_discord_project_basename(
    const WCHAR* path, char* output, unsigned int capacity)
{
    const WCHAR* base = path;
    const WCHAR* cursor = path;
    if (!output || !capacity) return;
    output[0] = 0;
    if (!path) return;
    while (*cursor) {
        if ((*cursor == L'\\' || *cursor == L'/') && cursor[1])
            base = cursor + 1;
        ++cursor;
    }
    (void)dluma_wide_to_utf8(base, output, capacity);
}

static unsigned int dluma_discord_context_hash(
    unsigned int kind, const char* map_name,
    const WCHAR* project_root);

static int dluma_discord_session_level_snapshot(
    DlumaGameThreadLevelSnapshot* snapshot,
    ULONGLONG sample_ms)
{
    DlumaEngineState* state = &g_dluma.engine;
    void* level = 0;
    const char* level_name = 0;
    ULONG_PTR level_vtable = 0;
    char raw[DLUMA_MAX_MAP_NAME * 2];
    char normalized[DLUMA_MAX_MAP_NAME * 2];
    unsigned int i;
    int terminated = 0;
    int delivered = 1;
    if (!snapshot) return 0;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!state->ready || !state->get_level_name ||
        !dluma_player_media_adapter_get_session_level(&level) ||
        !level || !dluma_is_readable(level, sizeof(level_vtable)))
        return 0;
    memcpy(&level_vtable, level, sizeof(level_vtable));
    if (!level_vtable ||
        !dluma_is_readable((void*)level_vtable, sizeof(void*)))
        return 0;
    __try {
        level_name = state->get_level_name(level);
    } __except (1) {
        delivered = 0;
    }
    if (!delivered || !level_name ||
        !dluma_is_readable(level_name, 1))
        return 0;
    for (i = 0; i < DLUMA_ARRAY_COUNT(raw); ++i) {
        if (!dluma_is_readable(level_name + i, 1))
            return 0;
        raw[i] = level_name[i];
        if (!raw[i]) {
            terminated = 1;
            break;
        }
    }
    if (!terminated || !raw[0])
        return 0;
    dluma_normalize_map_name(
        raw, normalized, DLUMA_ARRAY_COUNT(normalized));
    if (!normalized[0] ||
        dluma_frontend_level_name_is_frontend(normalized))
        return 0;
    snapshot->publisher_thread = (LONG)GetCurrentThreadId();
    snapshot->sample_ms = sample_ms;
    snapshot->level_interface = level;
    snapshot->level_vtable = level_vtable;
    snapshot->query_succeeded = 1;
    snapshot->frontend = 0;
    dluma_str_copy(
        snapshot->normalized_level_name,
        DLUMA_ARRAY_COUNT(snapshot->normalized_level_name),
        normalized);
    return 1;
}

static unsigned int dluma_discord_build_gameplay_activity(
    const DlumaGameThreadLevelSnapshot* level,
    void* game_subobject,
    DlumaDiscordActivityV1* activity,
    DlumaPresenceSnapshot* snapshot)
{
    DlumaProjectOwnerProofSnapshot owner;
    DlumaWorkshopMenuItem metadata;
    DlumaWorkshopMenuRemoteItem remote;
    DlumaLua54QuestHudSnapshot quest;
    DlumaLua54Error quest_error;
    char map_title[DLUMA_WORKSHOP_MENU_TITLE_CAPACITY];
    char map_sanitized[DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY];
    char quest_sanitized[DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY];
    int has_owner = 0;
    int has_metadata = 0;
    int has_remote = 0;
    int paused = 0;
    int pause_known;

    memset(activity, 0, sizeof(*activity));
    memset(snapshot, 0, sizeof(*snapshot));
    memset(&owner, 0, sizeof(owner));
    memset(&metadata, 0, sizeof(metadata));
    memset(&remote, 0, sizeof(remote));
    map_title[0] = 0;
    map_sanitized[0] = 0;
    quest_sanitized[0] = 0;
    activity->struct_size = sizeof(*activity);
    snapshot->struct_size = sizeof(*snapshot);
    snapshot->context = DLUMA_PRESENCE_CONTEXT_GAMEPLAY;

    has_owner =
        dluma_custom_map_exact_project_owner_snapshot(
            level->normalized_level_name, &owner) ==
            DLUMA_PROJECT_OWNER_PROOF_READY &&
        owner.project_root[0] != 0;
    if (has_owner)
        has_metadata = dluma_workshop_menu_find_metadata_for_project(
            owner.project_root, &metadata);
    if (has_metadata && metadata.workshop_id[0]) {
        dluma_workshop_menu_request_remote(metadata.workshop_id);
        has_remote = dluma_workshop_menu_get_remote(
            metadata.workshop_id, &remote);
    }
    if (has_remote && remote.details_ready && remote.title[0])
        dluma_str_copy(
            map_title, DLUMA_ARRAY_COUNT(map_title), remote.title);
    else if (has_metadata && metadata.title[0])
        dluma_str_copy(
            map_title, DLUMA_ARRAY_COUNT(map_title), metadata.title);
    else if (has_owner)
        dluma_discord_project_basename(
            owner.project_root, map_title, DLUMA_ARRAY_COUNT(map_title));
    if (!map_title[0])
        dluma_str_copy(
            map_title, DLUMA_ARRAY_COUNT(map_title),
            level->normalized_level_name);
    dluma_discord_copy_sanitized(
        map_sanitized, DLUMA_ARRAY_COUNT(map_sanitized),
        map_title, DLUMA_ARRAY_COUNT(map_title), 128U);
    if (!map_sanitized[0])
        dluma_discord_localized(
            "DLUMA_Discord_UnknownMap", "Unknown Map",
            map_sanitized, DLUMA_ARRAY_COUNT(map_sanitized));

    dluma_discord_format_name(
        "DLUMA_Discord_MapFormat", "Map: {name}", map_sanitized,
        snapshot->primary_text,
        DLUMA_ARRAY_COUNT(snapshot->primary_text));
    dluma_str_copy(
        activity->details, DLUMA_ARRAY_COUNT(activity->details),
        snapshot->primary_text);
    dluma_str_copy(
        activity->large_image,
        DLUMA_ARRAY_COUNT(activity->large_image),
        has_remote &&
                dluma_discord_https_asset_url_valid(remote.preview_url)
            ? remote.preview_url
            : "dl_fallback");
    dluma_discord_copy_sanitized(
        activity->large_text, DLUMA_ARRAY_COUNT(activity->large_text),
        map_sanitized, DLUMA_ARRAY_COUNT(map_sanitized), 128U);
    if (has_metadata &&
        dluma_discord_build_workshop_item_url(
            metadata.workshop_id,
            DLUMA_ARRAY_COUNT(metadata.workshop_id),
            activity->button_url,
            DLUMA_ARRAY_COUNT(activity->button_url))) {
        char raw_label[DLUMA_DISCORD_ACTIVITY_TEXT_CAPACITY];
        raw_label[0] = 0;
        if (!dluma_text_get_utf8(
                "DLUMA_Discord_ViewWorkshop",
                "View on Steam Workshop",
                raw_label, DLUMA_ARRAY_COUNT(raw_label)))
            dluma_str_copy(
                raw_label, DLUMA_ARRAY_COUNT(raw_label),
                "View on Steam Workshop");
        dluma_discord_copy_sanitized(
            activity->button_label,
            DLUMA_ARRAY_COUNT(activity->button_label),
            raw_label, DLUMA_ARRAY_COUNT(raw_label), 32U);
        if (activity->button_label[0])
            activity->flags |= DLUMA_DISCORD_ACTIVITY_HAS_BUTTON;
        else
            activity->button_url[0] = 0;
    }

    memset(&quest, 0, sizeof(quest));
    memset(&quest_error, 0, sizeof(quest_error));
    quest.struct_size = sizeof(quest);
    quest_error.struct_size = sizeof(quest_error);
    if (dluma_quest_hud_snapshot(&quest, &quest_error) &&
        quest.has_header && quest.header.text[0]) {
        dluma_discord_copy_sanitized(
            quest_sanitized, DLUMA_ARRAY_COUNT(quest_sanitized),
            quest.header.text,
            DLUMA_ARRAY_COUNT(quest.header.text), 128U);
    }
    activity->flags |= DLUMA_DISCORD_ACTIVITY_HAS_STATE;
    if (quest_sanitized[0]) {
        snapshot->flags |= DLUMA_PRESENCE_HAS_QUEST;
        dluma_discord_format_name(
            "DLUMA_Discord_QuestFormat", "Quest: {name}",
            quest_sanitized,
            snapshot->secondary_text,
            DLUMA_ARRAY_COUNT(snapshot->secondary_text));
    } else {
        dluma_discord_format_name(
            "DLUMA_Discord_ExploringFormat", "Exploring {name}",
            map_sanitized,
            snapshot->secondary_text,
            DLUMA_ARRAY_COUNT(snapshot->secondary_text));
    }
    dluma_str_copy(
        activity->state, DLUMA_ARRAY_COUNT(activity->state),
        snapshot->secondary_text);

    pause_known = dluma_discord_pause_state(game_subobject, &paused);
    if (pause_known) {
        snapshot->flags |= DLUMA_PRESENCE_PAUSE_KNOWN;
        if (paused) snapshot->flags |= DLUMA_PRESENCE_PAUSED;
        activity->flags |= DLUMA_DISCORD_ACTIVITY_HAS_SMALL_IMAGE;
        dluma_str_copy(
            activity->small_image,
            DLUMA_ARRAY_COUNT(activity->small_image),
            paused ? "dl_paused" : "dl_playing");
        dluma_discord_localized(
            paused ? "DLUMA_Discord_GamePaused" :
                     "DLUMA_Discord_GameRunning",
            paused ? "Game is PAUSED" : "Game is RUNNING",
            snapshot->pause_text,
            DLUMA_ARRAY_COUNT(snapshot->pause_text));
        dluma_str_copy(
            activity->small_text,
            DLUMA_ARRAY_COUNT(activity->small_text),
            snapshot->pause_text);
    }
    return dluma_discord_context_hash(
        0x100U, level->normalized_level_name,
        has_owner ? owner.project_root : 0);
}

static unsigned int dluma_discord_context_hash(
    unsigned int kind, const char* map_name,
    const WCHAR* project_root)
{
    unsigned int hash = 2166136261U ^ kind;
    unsigned int i = 0;
    if (map_name) {
        while (map_name[i]) {
            hash ^= (unsigned char)dluma_ascii_lower(map_name[i++]);
            hash *= 16777619U;
        }
    }
    i = 0;
    if (project_root) {
        while (project_root[i]) {
            unsigned int value =
                (unsigned int)dluma_wascii_lower(project_root[i++]);
            hash ^= value & 0xFFU;
            hash *= 16777619U;
            hash ^= (value >> 8) & 0xFFU;
            hash *= 16777619U;
        }
    }
    return hash ? hash : 1U;
}

int dluma_discord_presence_start(void)
{
    DWORD thread_id = 0;
    DlumaDiscordHostRoute route;
    if (g_dluma_discord.worker_thread) return 1;
    if (!dluma_discord_select_host_route(
            dluma_dual_host_runtime_kind(),
            dluma_dual_host_runtime_forwarding_only(),
            DLUMA_DISCORD_DYING_LIGHT_APPLICATION_ID,
            DLUMA_DISCORD_DEVELOPER_TOOLS_APPLICATION_ID, &route))
        return 0;
    memset(&g_dluma_discord, 0, sizeof(g_dluma_discord));
    g_dluma_discord.stop_event = CreateEventW(0, TRUE, FALSE, 0);
    g_dluma_discord.wake_event = CreateEventW(0, FALSE, FALSE, 0);
    if (!g_dluma_discord.stop_event || !g_dluma_discord.wake_event)
        goto failed;
    g_dluma_discord.application_id = route.application_id;
    g_dluma_discord.host_kind = dluma_dual_host_runtime_kind();
    g_dluma_discord.enabled = route.uses_editor_setting
        ? (g_dluma.config.editor_discord_rich_presence_enabled ? 1 : 0)
        : (g_dluma.config.discord_rich_presence_enabled ? 1 : 0);
    g_dluma_discord.stop_succeeded = 1;
    g_dluma_discord.retry_delay_ms = DLUMA_DISCORD_RETRY_INITIAL_MS;
    g_dluma_discord.worker_thread = CreateThread(
        0, 0, dluma_discord_worker, 0, 0, &thread_id);
    if (!g_dluma_discord.worker_thread) goto failed;
    dluma_log_line(
        "INFO",
        g_dluma_discord.enabled
            ? "Discord Rich Presence controller started (enabled)."
            : "Discord Rich Presence controller started (disabled).");
    return 1;
failed:
    if (g_dluma_discord.wake_event) {
        CloseHandle(g_dluma_discord.wake_event);
        g_dluma_discord.wake_event = 0;
    }
    if (g_dluma_discord.stop_event) {
        CloseHandle(g_dluma_discord.stop_event);
        g_dluma_discord.stop_event = 0;
    }
    return 0;
}

void dluma_discord_presence_set_enabled(int enabled)
{
    dluma_atomic_exchange(&g_dluma_discord.enabled, enabled ? 1 : 0);
    if (!enabled)
        dluma_atomic_exchange(&g_dluma_discord.clear_requested, 1);
    if (g_dluma_discord.wake_event) SetEvent(g_dluma_discord.wake_event);
}

int dluma_discord_presence_is_enabled(void)
{
    return dluma_atomic_compare_exchange(
               &g_dluma_discord.enabled, 0, 0) != 0 &&
           !dluma_atomic_compare_exchange(
               &g_dluma_discord.stopping, 0, 0);
}


void dluma_discord_presence_observe_editor(
    const DlumaPresenceSnapshot* snapshot,
    ULONGLONG document_identity)
{
    DlumaDiscordActivityV1 activity;
    ULONGLONG start_seconds;
    if (!snapshot ||
        snapshot->struct_size != sizeof(*snapshot) ||
        snapshot->context != DLUMA_PRESENCE_CONTEXT_EDITOR ||
        g_dluma_discord.host_kind != DLUMA_HOST_CHROME_EDITOR ||
        dluma_atomic_compare_exchange(
            &g_dluma_discord.stopping, 0, 0) ||
        !g_dluma_discord.worker_thread)
        return;
    /* Retain the latest document while disabled so enabling can publish it
     * without requiring the user to switch documents first. */
    memset(&activity, 0, sizeof(activity));
    activity.struct_size = sizeof(activity);
    dluma_discord_copy_sanitized(
        activity.details, DLUMA_ARRAY_COUNT(activity.details),
        snapshot->primary_text,
        DLUMA_ARRAY_COUNT(snapshot->primary_text), 128U);
    if (!activity.details[0]) return;
    if (snapshot->secondary_text[0]) {
        dluma_discord_copy_sanitized(
            activity.state, DLUMA_ARRAY_COUNT(activity.state),
            snapshot->secondary_text,
            DLUMA_ARRAY_COUNT(snapshot->secondary_text), 128U);
        if (activity.state[0])
            activity.flags |= DLUMA_DISCORD_ACTIVITY_HAS_STATE;
    }
    dluma_str_copy(
        activity.large_image,
        DLUMA_ARRAY_COUNT(activity.large_image), "dl_editor");
    dluma_str_copy(
        activity.large_text,
        DLUMA_ARRAY_COUNT(activity.large_text),
        "Dying Light Developer Tools");
    start_seconds = dluma_discord_timer_update(
        &g_dluma_discord.timer, DLUMA_DISCORD_TIMER_EDITOR,
        0, document_identity, dluma_discord_unix_seconds());
    if (start_seconds) {
        activity.flags |=
            DLUMA_DISCORD_ACTIVITY_HAS_START_TIMESTAMP;
        activity.start_timestamp_seconds = start_seconds;
    }
    activity.generation = (ULONGLONG)dluma_atomic_increment(
        &g_dluma_discord.snapshot_generation);
    g_dluma_discord.committed_activity = activity;
    g_dluma_discord.committed_context =
        DLUMA_PRESENCE_CONTEXT_EDITOR;
    g_dluma_discord.committed_valid = 1;
    dluma_discord_publish_to_worker(&activity);
}

void dluma_discord_presence_set_suspended(int suspended)
{
    LONG requested = suspended ? 1 : 0;
    if (dluma_atomic_exchange(
            &g_dluma_discord.suspended, requested) == requested)
        return;
    dluma_atomic_exchange(&g_dluma_discord.clear_requested, 1);
    if (g_dluma_discord.wake_event)
        SetEvent(g_dluma_discord.wake_event);
}
void dluma_discord_presence_game_thread_tick(
    void* game_subobject, ULONGLONG callback_sequence)
{
    DlumaGameThreadLevelSnapshot level;
    DlumaGameThreadLevelSnapshot session_level;
    DlumaDiscordActivityV1 observed;
    DlumaPresenceSnapshot observed_snapshot;
    DlumaDiscordLevelSource level_source;
    unsigned int observed_context = 0;
    unsigned int committed_context;
    DlumaDiscordMenuActivity menu = DLUMA_DISCORD_MENU_MAIN;
    ULONGLONG now_ms;
    int positive = 0;
    int changed = 0;
    int active_level_positive;
    int session_level_positive;

    if (dluma_atomic_compare_exchange(
            &g_dluma_discord.stopping, 0, 0) ||
        callback_sequence <= g_dluma_discord.observed_callback_sequence)
        return;
    g_dluma_discord.observed_callback_sequence = callback_sequence;
    dluma_steam_presence_game_thread_service();
    if (!dluma_discord_presence_is_enabled() &&
        !dluma_steam_presence_is_enabled())
        return;
    memset(&level, 0, sizeof(level));
    memset(&session_level, 0, sizeof(session_level));
    memset(&observed, 0, sizeof(observed));
    memset(&observed_snapshot, 0, sizeof(observed_snapshot));
    now_ms = GetTickCount64();
    active_level_positive =
        dluma_read_game_thread_level_snapshot(&level) &&
        level.query_succeeded && level.level_interface &&
        level.normalized_level_name[0];
    session_level_positive =
        dluma_discord_session_level_snapshot(
            &session_level, now_ms);
    level_source = dluma_discord_choose_level_source(
        active_level_positive, session_level_positive);
    if (level_source != DLUMA_DISCORD_LEVEL_SOURCE_NONE) {
        if (level_source == DLUMA_DISCORD_LEVEL_SOURCE_SESSION)
            level = session_level;
        positive = 1;
        if (level.frontend) {
            menu = dluma_discord_menu_activity(level.level_interface);
            dluma_discord_build_menu_activity(
                menu, &observed, &observed_snapshot);
            observed_context = (unsigned int)menu + 1U;
        } else {
            observed_context = dluma_discord_build_gameplay_activity(
                &level, game_subobject, &observed,
                &observed_snapshot);
        }
    }
    committed_context = dluma_discord_debounce_update(
        &g_dluma_discord.debounce, observed_context, positive,
        now_ms, DLUMA_DISCORD_TRANSITION_DEBOUNCE_MS,
        g_dluma.config.unmatched_level_grace_ms, &changed);
    if (!positive || committed_context != observed_context) {
        if (!g_dluma_discord.debounce.has_committed) {
            g_dluma_discord.committed_valid = 0;
            (void)dluma_discord_timer_update(
                &g_dluma_discord.timer,
                DLUMA_DISCORD_TIMER_NONE, 0, 0, 0);
            if (!g_dluma_discord.absence_clear_published) {
                g_dluma_discord.absence_clear_published = 1;
                dluma_steam_presence_game_thread_expire();
                dluma_discord_clear_worker_snapshot();
                dluma_atomic_exchange(
                    &g_dluma_discord.clear_requested, 1);
                if (g_dluma_discord.wake_event)
                    SetEvent(g_dluma_discord.wake_event);
            }
        }
        return;
    }
    {
        ULONGLONG start_seconds =
            dluma_discord_timer_update(
                &g_dluma_discord.timer,
                level.frontend
                    ? DLUMA_DISCORD_TIMER_MENU
                    : DLUMA_DISCORD_TIMER_GAMEPLAY,
                level.frontend
                    ? 0
                    : dluma_discord_context_hash(
                          0x200U,
                          level.normalized_level_name,
                          0),
                level.frontend
                    ? 0
                    : (ULONGLONG)(ULONG_PTR)
                          level.level_interface,
                dluma_discord_unix_seconds());
        if (start_seconds) {
            observed.flags |=
                DLUMA_DISCORD_ACTIVITY_HAS_START_TIMESTAMP;
            observed.start_timestamp_seconds =
                start_seconds;
        }
    }
    g_dluma_discord.absence_clear_published = 0;
    dluma_steam_presence_game_thread_observe(&observed_snapshot);
    if (!g_dluma_discord.committed_valid ||
        changed ||
        !dluma_discord_activity_equal(
            &observed, &g_dluma_discord.committed_activity)) {
        int context_changed =
            !g_dluma_discord.committed_valid || changed;
        observed.generation = (ULONGLONG)dluma_atomic_increment(
            &g_dluma_discord.snapshot_generation);
        g_dluma_discord.committed_activity = observed;
        g_dluma_discord.committed_context = observed_context;
        g_dluma_discord.committed_valid = 1;
        dluma_discord_publish_to_worker(&observed);
        if (context_changed)
            dluma_log_pair(
                "INFO", "discord_presence_context",
                level.frontend
                    ? dluma_discord_menu_fallback(menu)
                    : "Gameplay");
        if (context_changed &&
            level_source == DLUMA_DISCORD_LEVEL_SOURCE_SESSION)
            dluma_log_pair(
                "INFO", "discord_presence_level_source",
                "validated_game_global_session");
    }
}

void dluma_discord_presence_quiesce(void)
{
    dluma_atomic_exchange(&g_dluma_discord.stopping, 1);
    dluma_atomic_exchange(&g_dluma_discord.enabled, 0);
    dluma_atomic_exchange(&g_dluma_discord.clear_requested, 1);
    if (g_dluma_discord.wake_event) SetEvent(g_dluma_discord.wake_event);
}

void dluma_discord_presence_stop(void)
{
    DWORD result;
    if (!g_dluma_discord.worker_thread) return;
    dluma_discord_presence_quiesce();
    if (g_dluma_discord.stop_event) SetEvent(g_dluma_discord.stop_event);
    result = WaitForSingleObject(
        g_dluma_discord.worker_thread,
        DLUMA_DISCORD_SHUTDOWN_TIMEOUT_MS + 1000U);
    if (result != WAIT_OBJECT_0) {
        dluma_atomic_exchange(&g_dluma_discord.stop_succeeded, 0);
        return;
    }
    CloseHandle(g_dluma_discord.worker_thread);
    g_dluma_discord.worker_thread = 0;
    if (!dluma_discord_presence_stop_succeeded()) return;
    if (g_dluma_discord.wake_event) {
        CloseHandle(g_dluma_discord.wake_event);
        g_dluma_discord.wake_event = 0;
    }
    if (g_dluma_discord.stop_event) {
        CloseHandle(g_dluma_discord.stop_event);
        g_dluma_discord.stop_event = 0;
    }
    /* The two DLL handles intentionally remain loaded for process lifetime. */
}

int dluma_discord_presence_stop_succeeded(void)
{
    if (!g_dluma_discord.worker_thread &&
        !g_dluma_discord.stop_event &&
        !g_dluma_discord.wake_event)
        return 1;
    return dluma_atomic_compare_exchange(
               &g_dluma_discord.stop_succeeded, 0, 0) != 0;
}
