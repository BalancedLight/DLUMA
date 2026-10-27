extern "C" {
#include "dlw_common.h"
#include "dlw_animation_refresh_runtime.h"
#include "dlw_asset_mods.h"
#include "dlw_asset_mod_runtime.h"
#include "dlw_asset_mod_native.h"
#include "dlw_custom_main_menu.h"
#include "dlw_custom_main_menu_runtime.h"
#include "dlw_log.h"
#include "dlw_lua_discovery_internal.h"
#include "dlw_new_mod_ticker.h"
#include "dlw_path_security.h"
#include "dlw_player_startup.h"
#include "dlw_isolation_startup.h"
}

#include "dlw_runtime_ui_fixes.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_editor_host.h"
#include "dlw_editor_model_animation_fix.h"
#include "hosts/dlw_managed_host.h"
#include "dlw_native_mods.h"
#include "dlw_quest_phase_player.h"
#include "game/dlw_media_service.h"

#include "fixes/FixRegistry.h"
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

typedef void* (__cdecl *DlumaInitializeScriptFn)(void);
typedef __int64 (__cdecl *DlumaShutdownScriptFn)(void);

#define DLUMA_PLAYER_MODEL_ANIMATION_GENERATION 1ULL

static HMODULE g_original_game_dll;
static DlumaInitializeScriptFn g_original_initialize;
static DlumaShutdownScriptFn g_original_shutdown;
static volatile LONG g_dluma_shutdown_entered;
static volatile LONG g_original_initialize_count;
static volatile LONG g_original_initialize_attempted;
static volatile LONG g_original_shutdown_count;
static void* g_original_script_manager;
static DlumaDualHostLifecycle g_dual_host_lifecycle;
static volatile LONG g_dual_host_export_state;

static const WCHAR* dluma_skip_extended_path_prefix(const WCHAR* path)
{
    if (path && path[0] == L'\\' && path[1] == L'\\' &&
        path[2] == L'?' && path[3] == L'\\')
        return path + 4;
    return path;
}

static int dluma_build_game_dll_candidate(
    WCHAR* out,
    unsigned int capacity,
    const WCHAR* directory,
    const WCHAR* relative_name)
{
    SIZE_T directory_length;
    SIZE_T name_length;
    const WCHAR* name = relative_name;
    int needs_separator;
    if (!out || !capacity || !directory || !directory[0] || !name)
        return 0;
    while (*name == L'\\' || *name == L'/') ++name;
    directory_length = wcslen(directory);
    name_length = wcslen(name);
    needs_separator = directory_length &&
        directory[directory_length - 1] != L'\\' &&
        directory[directory_length - 1] != L'/';
    if (directory_length + (needs_separator ? 1U : 0U) +
            name_length + 1U > capacity)
        return 0;
    dluma_path_join(out, capacity, directory, name);
    return 1;
}

static HMODULE dluma_load_game_dll_candidate(const WCHAR* candidate)
{
    WCHAR full_path[DLUMA_MAX_WPATH];
    WCHAR final_path[DLUMA_MAX_WPATH];
    WCHAR loaded_path[DLUMA_MAX_WPATH];
    WCHAR loaded_full_path[DLUMA_MAX_WPATH];
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE file = INVALID_HANDLE_VALUE;
    HMODULE module = 0;
    DWORD length;
    DWORD loaded_full_length;
    DWORD attributes;
    if (!candidate || !candidate[0]) return 0;
    length = GetFullPathNameW(candidate, DLUMA_ARRAY_COUNT(full_path),
                              full_path, 0);
    if (!length || length >= DLUMA_ARRAY_COUNT(full_path)) return 0;
    attributes = GetFileAttributesW(full_path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        return 0;
    file = CreateFileW(full_path, GENERIC_READ, FILE_SHARE_READ, 0,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) return 0;
    memset(&info, 0, sizeof(info));
    if (!dluma_get_final_path_checked(
            file, final_path, DLUMA_ARRAY_COUNT(final_path)) ||
        !GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        goto done;
    module = LoadLibraryExW(
        final_path, 0,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module || module == g_dluma.self_module) {
        module = 0;
        goto done;
    }
    loaded_path[0] = 0;
    loaded_full_path[0] = 0;
    if (!dluma_get_module_path_checked(
            module, loaded_path, DLUMA_ARRAY_COUNT(loaded_path))) {
        FreeLibrary(module);
        module = 0;
        goto done;
    }
    loaded_full_length = GetFullPathNameW(
        loaded_path, DLUMA_ARRAY_COUNT(loaded_full_path), loaded_full_path, 0);
    if (!loaded_full_length ||
        loaded_full_length >= DLUMA_ARRAY_COUNT(loaded_full_path) ||
        dluma_wstricmp(dluma_skip_extended_path_prefix(final_path),
                     dluma_skip_extended_path_prefix(loaded_full_path)) != 0 ||
        !GetProcAddress(module, "InitializeGameScriptDLL") ||
        !GetProcAddress(module, "ShutdownGameScriptDLL")) {
        FreeLibrary(module);
        module = 0;
    }
done:
    CloseHandle(file);
    return module;
}

static HMODULE dluma_find_original_game_dll(void)
{
    HMODULE module;
    WCHAR module_path[DLUMA_MAX_WPATH];
    WCHAR candidate[DLUMA_MAX_WPATH];

    module = GetModuleHandleW(L"engine_x64_rwdi.dll");
    if (module && dluma_get_module_path_checked(
            module, module_path, DLUMA_ARRAY_COUNT(module_path))) {
        dluma_path_dirname(module_path);
        if (dluma_build_game_dll_candidate(
                candidate, DLUMA_ARRAY_COUNT(candidate), module_path,
                L"gamedll_x64_rwdi.dll")) {
            module = dluma_load_game_dll_candidate(candidate);
            if (module) return module;
        }
    }

    module_path[0] = 0;
    if (dluma_get_module_path_checked(
            0, module_path, DLUMA_ARRAY_COUNT(module_path))) {
        dluma_path_dirname(module_path);

        if (dluma_build_game_dll_candidate(
                candidate, DLUMA_ARRAY_COUNT(candidate), module_path,
                L"gamedll_x64_rwdi.dll")) {
            module = dluma_load_game_dll_candidate(candidate);
            if (module) return module;
        }

        if (dluma_build_game_dll_candidate(
                candidate, DLUMA_ARRAY_COUNT(candidate), module_path,
                L"x64\\gamedll_x64_rwdi.dll")) {
            module = dluma_load_game_dll_candidate(candidate);
            if (module) return module;
        }
    }

    return 0;
}

static int dluma_resolve_original_game_script(void)
{
    if (g_original_initialize && g_original_shutdown) return 1;

    g_original_game_dll = dluma_find_original_game_dll();
    if (!g_original_game_dll || g_original_game_dll == g_dluma.self_module) return 0;

    g_original_initialize = (DlumaInitializeScriptFn)GetProcAddress(
        g_original_game_dll,
        "InitializeGameScriptDLL");
    g_original_shutdown = (DlumaShutdownScriptFn)GetProcAddress(
        g_original_game_dll,
        "ShutdownGameScriptDLL");
    if (g_original_initialize && g_original_shutdown)
        dluma_native_mods_set_original_gamedll(g_original_game_dll);
    return g_original_initialize && g_original_shutdown;
}

static int dluma_release_original_game_script(void)
{
    HMODULE module = g_original_game_dll;
    if (module && !FreeLibrary(module)) return 0;
    g_original_script_manager = 0;
    g_original_initialize = 0;
    g_original_shutdown = 0;
    g_original_game_dll = 0;
    dluma_native_mods_set_original_gamedll(0);
    return 1;
}

static int dluma_dual_host_exports_initialize(void)
{
    LONG state = dluma_atomic_compare_exchange(
        &g_dual_host_export_state, 1, 0);
    if (state != 0)
        return dluma_atomic_compare_exchange(
                   &g_dual_host_export_state, 0, 0) == 2;
    if (dluma_configure_runtime_paths()) {
        dluma_log_line("INFO", "InitializeGameScriptDLL host classification entered.");
        (void)dluma_build_identity_initialize(g_dluma.self_module);
        dluma_build_identity_log();
    }
    if (!dluma_dual_host_runtime_initialize()) {
        dluma_atomic_exchange(&g_dual_host_export_state, -1);
        return 0;
    }
    dluma_dual_host_lifecycle_initialize(
        &g_dual_host_lifecycle, dluma_dual_host_runtime_kind());
    dluma_atomic_exchange(&g_dual_host_export_state, 2);
    return 1;
}

static void* dluma_forwarding_only_initialize(void)
{
    void* manager;
    DlumaHostLifecycleAction action =
        dluma_dual_host_lifecycle_begin_initialize(&g_dual_host_lifecycle);
    if (action == DLUMA_HOST_ACTION_RETURN_ACTIVE_MANAGER)
        return g_original_script_manager;
    if (action != DLUMA_HOST_ACTION_START_GENERATION) return 0;
    if (!dluma_resolve_original_game_script()) {
        (void)dluma_dual_host_lifecycle_original_initialized(
            &g_dual_host_lifecycle, 0);
        dluma_log_line("ERROR", "Forwarding-only host could not resolve original gamedll.");
        return 0;
    }
    (void)dluma_dual_host_lifecycle_original_loaded(&g_dual_host_lifecycle);
    manager = g_original_initialize();
    g_original_script_manager = manager;
    (void)dluma_dual_host_lifecycle_original_initialized(
        &g_dual_host_lifecycle, manager != 0);
    dluma_log_pair(
        manager ? "INFO" : "ERROR", "forwarding_only_original_initialize",
        manager ? "succeeded" : "returned_null");
    return manager;
}

static __int64 dluma_forwarding_only_shutdown(void)
{
    __int64 result = 0;
    int called = 0;
    DlumaHostLifecycleAction action =
        dluma_dual_host_lifecycle_begin_shutdown(&g_dual_host_lifecycle);
    if (action != DLUMA_HOST_ACTION_SHUTDOWN_GENERATION) return 0;
    if (g_original_shutdown) {
        result = g_original_shutdown();
        called = 1;
    }
    (void)dluma_dual_host_lifecycle_finish_shutdown(
        &g_dual_host_lifecycle, 1, called, 0);
    g_original_script_manager = 0;
    dluma_log_pair("INFO", "forwarding_only_original_shutdown",
                 called ? "called_once" : "unavailable");
    (void)dluma_log_stop();
    return result;
}

static int dluma_editor_wait_callbacks_drained(ULONGLONG timeout_ms)
{
    ULONGLONG started = GetTickCount64();
    while (!dluma_editor_host_callbacks_drained() &&
           GetTickCount64() - started < timeout_ms)
        Sleep(1);
    return dluma_editor_host_callbacks_drained();
}

static void dluma_editor_mark_generation_retained(const char* message)
{
    dluma_editor_model_animation_fix_retain();
    g_dual_host_lifecycle.dependency_graph_retained = 1U;
    g_dual_host_lifecycle.module_pinned = 1U;
    g_dual_host_lifecycle.state =
        DLUMA_HOST_LIFECYCLE_RETAINED_REJECT_REINIT;
    if (message) dluma_log_line("ERROR", message);
}

static void* dluma_editor_initialize(void)
{
    void* manager;
    int editor_host_started;
    DlumaEditorModelAnimationFixInstallResult animation_fix_result =
        DLUMA_EDITOR_MODEL_ANIMATION_FIX_REFUSED;
    DlumaHostLifecycleAction action;
    editor_host_started = dluma_editor_host_start();
    action = dluma_dual_host_lifecycle_begin_initialize(&g_dual_host_lifecycle);
    if (action == DLUMA_HOST_ACTION_RETURN_ACTIVE_MANAGER)
        return g_original_script_manager;
    if (action != DLUMA_HOST_ACTION_START_GENERATION) {
        dluma_log_line("ERROR", "Editor reinitialization was rejected by the retained lifecycle state.");
        return 0;
    }
    if (!dluma_resolve_original_game_script()) {
        (void)dluma_dual_host_lifecycle_original_initialized(
            &g_dual_host_lifecycle, 0);
        dluma_editor_host_crash_context_update(
            "session_initialize_failed", 0,
            g_dual_host_lifecycle.generation);
        dluma_log_line("ERROR", "Editor generation could not load the original gamedll.");
        return 0;
    }
    (void)dluma_dual_host_lifecycle_original_loaded(&g_dual_host_lifecycle);
    if (editor_host_started) {
        animation_fix_result = dluma_editor_model_animation_fix_install(
            g_original_game_dll, g_dual_host_lifecycle.generation);
    } else {
        dluma_log_line(
            "WARN",
            "Editor animation-bind guard was not installed because the Editor host callback tracker was unavailable.");
    }
    if (animation_fix_result ==
            DLUMA_EDITOR_MODEL_ANIMATION_FIX_RETAINED) {
        dluma_editor_mark_generation_retained(
            "Editor animation-bind guard installation left reachable state; the original initializer was not called, the gamedll was retained, and reinitialization is disabled.");
        dluma_editor_host_crash_context_update(
            "session_initialize_failed", 0,
            g_dual_host_lifecycle.generation);
        return 0;
    }
    manager = g_original_initialize();
    g_original_script_manager = manager;
    (void)dluma_dual_host_lifecycle_original_initialized(
        &g_dual_host_lifecycle, manager != 0);
    if (editor_host_started)
        (void)dluma_crash_reporting_start_editor_filter();
    if (!manager) {
        dluma_log_line("ERROR", "Editor original InitializeGameScriptDLL returned null.");
        if (!dluma_editor_wait_callbacks_drained(2000ULL))
            dluma_editor_mark_generation_retained(
                "Editor callbacks did not drain after a failed original initializer; the complete gamedll generation was retained and reinitialization is disabled.");
        else if (!dluma_editor_model_animation_fix_restore())
            dluma_editor_mark_generation_retained(
                "Editor animation-bind guard could not be restored after a failed original initializer; the complete gamedll generation was retained and reinitialization is disabled.");
        else if (!dluma_release_original_game_script())
            dluma_editor_mark_generation_retained(
                "Editor original gamedll could not unload after a failed initializer; reinitialization will remain unavailable.");
        dluma_editor_host_crash_context_update(
            "session_initialize_failed", 0,
            g_dual_host_lifecycle.generation);
        return 0;
    }
    if (editor_host_started &&
        !dluma_editor_host_session_begin(g_dual_host_lifecycle.generation)) {
        dluma_log_line("ERROR", "Editor session callback graph could not begin; retaining forwarding only for this generation.");
    }
    dluma_log_u64("INFO", "editor_original_initialize_count",
                g_dual_host_lifecycle.original_initializes);
    return manager;
}

static __int64 dluma_editor_shutdown(void)
{
    __int64 result = 0;
    int callbacks_drained;
    int original_called = 0;
    int original_unloaded = 0;
    DlumaHostLifecycleAction action =
        dluma_dual_host_lifecycle_begin_shutdown(&g_dual_host_lifecycle);
    if (action != DLUMA_HOST_ACTION_SHUTDOWN_GENERATION) return 0;
    if (dluma_editor_host_current_thread_in_callback()) {
        dluma_editor_model_animation_fix_retain();
        dluma_editor_host_crash_context_update(
            "session_shutdown_rejected",
            g_dual_host_lifecycle.generation, 0);
        (void)dluma_dual_host_lifecycle_finish_shutdown(
            &g_dual_host_lifecycle, 0, 0, 0);
        dluma_log_line(
            "ERROR",
            "Editor shutdown was requested from inside a loader callback; the complete generation was retained without nested teardown and reinitialization is disabled.");
        return 0;
    }
    callbacks_drained = dluma_editor_host_session_end(
        g_dual_host_lifecycle.generation);
    if (!callbacks_drained) {
        dluma_editor_model_animation_fix_retain();
        (void)dluma_dual_host_lifecycle_finish_shutdown(
            &g_dual_host_lifecycle, 0, 0, 0);
        dluma_log_line("ERROR", "Editor callbacks did not drain; original session and complete dependency graph were retained and reinitialization is disabled.");
        return 0;
    }
    if (g_original_shutdown) {
        result = g_original_shutdown();
        original_called = 1;
    }
    if (!original_called) {
        dluma_editor_model_animation_fix_retain();
        dluma_log_line(
            "ERROR",
            "Editor original shutdown was unavailable; the animation-bind guard and complete gamedll generation were retained.");
    } else if (!dluma_editor_model_animation_fix_restore()) {
        dluma_editor_model_animation_fix_retain();
        dluma_log_line(
            "ERROR",
            "Editor animation-bind guard restoration failed after original shutdown; the complete gamedll generation was retained.");
    } else if (g_original_game_dll) {
        original_unloaded = dluma_release_original_game_script();
    }
    (void)dluma_dual_host_lifecycle_finish_shutdown(
        &g_dual_host_lifecycle, 1, original_called, original_unloaded);
    dluma_editor_host_crash_context_update(
        original_unloaded ? "session_unloaded" : "session_unload_failed",
        0, g_dual_host_lifecycle.generation);
    dluma_log_u64("INFO", "editor_original_shutdown_count",
                g_dual_host_lifecycle.original_shutdowns);
    dluma_log_u64("INFO", "editor_original_unload_count",
                g_dual_host_lifecycle.original_unloads);
    return result;
}

static void* dluma_player_initialize(void)
{
    void* manager = 0;
    DlumaEditorModelAnimationFixInstallResult animation_fix_result =
        DLUMA_EDITOR_MODEL_ANIMATION_FIX_REFUSED;
    int pack_mounted = 0;
    int host_started;
    int managed_started;
    int engine_bound;
    int services_stopped;
    int editor_playtest = dluma_dual_host_runtime_is_playtest();

    if (dluma_atomic_compare_exchange(
            &g_dluma_shutdown_entered, 0, 0) != 0 ||
        dluma_atomic_compare_exchange(
            &g_original_shutdown_count, 0, 0) != 0) {
        OutputDebugStringA("DLUMA: reinitialization after shutdown is unsupported.\n");
        return 0;
    }
    if (g_original_script_manager) {
        dluma_log_line("WARN", "InitializeGameScriptDLL re-entered; returning the sole original script manager.");
        return g_original_script_manager;
    }
    if (dluma_configure_runtime_paths()) {
        dluma_log_line("INFO", "InitializeGameScriptDLL entered.");
        (void)dluma_build_identity_initialize(g_dluma.self_module);
        dluma_build_identity_log();
        (void)dluma_chrome_log_notice();
    }
    (void)dluma_player_startup_begin();

    if (!dluma_resolve_original_game_script()) {
        dluma_log_hex("ERROR", "Could not load/resolve original gamedll; Win32 error", GetLastError());
        OutputDebugStringA("DLUMA: original gamedll resolution failed.\n");
        dluma_player_startup_abort("original_gamedll_resolution_failed");
        (void)dluma_log_stop();
        return 0;
    }

    dluma_load_config();
    if (!dluma_crash_reporting_start())
        dluma_log_line(
            "WARN",
            "Early crash diagnostics were unavailable; original game initialization will continue.");
    animation_fix_result = dluma_editor_model_animation_fix_install(
        g_original_game_dll, DLUMA_PLAYER_MODEL_ANIMATION_GENERATION);
    if (animation_fix_result ==
            DLUMA_EDITOR_MODEL_ANIMATION_FIX_RETAINED) {
        dluma_log_line(
            "ERROR",
            "Player animation-bind guard installation retained reachable detour state; the original initializer was not called and this process cannot reinitialize safely.");
        if (dluma_crash_reporting_stop()) {
            (void)dluma_log_stop();
        } else {
            dluma_log_emergency(
                "[CRASH] [ERROR] Player animation-bind guard retention also prevented clean crash-diagnostics teardown; state remains pinned until process exit\r\n");
        }
        dluma_player_startup_abort("animation_guard_retained");
        return 0;
    }
    /* The original gamedll parses debug-command arguments during its own
     * initializer. A mounted debugconf without -debugconf= is therefore not
     * sufficient. Apply the exact supported Player startup flags before that
     * initializer; an unknown profile fails closed without blocking forwarding. */
    if (!editor_playtest)
        (void)dluma_developer_mode_apply_startup_profile(g_original_game_dll);
    /* Arm recovery before discovery or any third-party DLL MOD can execute.
     * A prior marker suppresses MOD execution for this process until the
     * validated frontend can ask the player what to persist for next launch. */
    if (!editor_playtest) dluma_startup_recovery_begin();
    /* Content-isolation hooks need the immutable startup Workshop-root
     * snapshot before they can attribute an fs::add_source call to a project.
     * The lifecycle worker refreshes discovery later, but it starts only after
     * the original game-script initializer has returned and is therefore too
     * late for hook initialization. */
    dluma_player_startup_status(dluma_player_startup_core_milestone(
        DLUMA_PLAYER_STARTUP_MILESTONE_DISCOVERY));
    dluma_discover_workshop_roots();
    if (!editor_playtest)
        dluma_asset_mods_discover();
    if (!editor_playtest)
        dluma_custom_main_menu_discover_and_prepare();
    /* Recovery needs a complete raw global-Lua snapshot before it can honor
     * "disable all" immediately. This performs metadata/file parsing only;
     * Lua execution remains owned by the later host lifecycle. */
    dluma_lua_discover_globals();
    dluma_native_mods_discover();
    dluma_managed_mods_discover();
    if (!editor_playtest) {
        dluma_new_mod_ticker_capture_startup();
        (void)dluma_startup_recovery_prompt_before_game_init();
    } else {
        dluma_log_pair("INFO", "player_frontend_mode", "editor_playtest_skipped");
    }
    dluma_player_startup_status(dluma_player_startup_core_milestone(
        DLUMA_PLAYER_STARTUP_MILESTONE_CONTENT));
    {
        const dluma::fixes::RuntimeContext runtime_context(g_original_game_dll);
        dluma::fixes::InitializeAllFixes(runtime_context);
    }

    /* Mount loader-owned sources only after the exact fs::add_source path and
     * object-form hooks are installed. This preserves the PAK/cache source
     * pointer and successful-add ordinal for B0 while hook-side attribution
     * still classifies both mounts as process-lifetime loader sources. */
    pack_mounted = dluma_mount_content_pack();
    if (pack_mounted && !editor_playtest &&
        !g_dluma.config.custom_map_asset_isolation_enabled &&
        !dluma_asset_mod_native_start())
        dluma_log_pair("WARN", "asset_mods", "native_file_adapter_unavailable");
    if (pack_mounted && !editor_playtest &&
        (!dluma_asset_mod_runtime_prepare() ||
         !dluma_asset_mod_runtime_apply(1))) {
        dluma_log_pair("WARN", "asset_mods", "startup_stack_unavailable");
        dluma_custom_main_menu_disable_for_process("asset_stack_unavailable");
    }
    if (pack_mounted && !editor_playtest &&
        !dluma_custom_main_menu_runtime_start(g_original_game_dll))
        dluma_custom_main_menu_disable_for_process(
            "exact_runtime_bridge_unavailable");
    if (pack_mounted && !editor_playtest &&
        !dluma_custom_main_menu_prepare_selected_sources())
    {
        dluma_log_pair(
            "WARN", "custom_main_menu_status",
            "selected_source_identity_not_prepared_stock_frontend_retained");
        dluma_custom_main_menu_disable_for_process(
            "selected_source_identity_not_prepared");
    }

    /* The mounted Workshop pack owns same-length alternate XUI filenames.
     * Redirect both native registrations independently before the original
     * gamedll initializes its screens; no loose DevTools file is modified. */
    if (pack_mounted && !editor_playtest)
    {
        if (!dluma_ui_prepare_main_menu_redirect())
            dluma_log_line("WARN", "Main-menu XUI redirect was not applied; the loader will continue with the stock menu.");
        if (!dluma_ui_prepare_workshop_menu_redirect())
            dluma_log_line("WARN", "Workshop-menu XUI redirect was not applied; the loader will continue with the stock Workshop screen.");
    }
    else if (!editor_playtest)
    {
        dluma_log_pair("WARN", "workshop_menu_xui_redirect_status",
                     "content_pack_not_mounted");
    }

    /* Bind the exact-build Localized_FromFile bridge for future stock text
     * reloads. The initial stock load precedes this script-DLL lifecycle, so
     * initial loader catalogs are deliberately deferred until after the
     * original manager and engine interface are available. */
    if (pack_mounted && !editor_playtest) {
        if (!dluma_localization_bind() || !dluma_localization_load_bootstrap()) {
            dluma_log_line("WARN", "Loader localization bindings or future-reload bridge were unavailable; dynamic English fallbacks remain active.");
        }
    }

    if (dluma_atomic_compare_exchange(&g_original_initialize_attempted, 1, 0) != 0) {
        dluma_log_line("ERROR", "Original game-script initialization was already attempted; refusing a second manager owner.");
        if (!dluma_editor_model_animation_fix_restore())
            dluma_log_line(
                "ERROR",
                "Player animation-bind guard could not be restored after a rejected repeated initializer; its loader and gamedll remain pinned until process exit.");
        dluma_player_startup_abort("game_script_reinitialization_refused");
        return g_original_script_manager;
    }
    dluma_player_startup_status(dluma_player_startup_core_milestone(
        DLUMA_PLAYER_STARTUP_MILESTONE_SCRIPTS));
    if (!dluma_quest_phase_player_start(g_original_game_dll))
        dluma_log_line(
            "WARN",
            "Custom quest phase observation is unavailable for this gamedll profile; stock quest progression remains unchanged.");
    dluma_media_service_start();
    dluma_native_mods_pre_game_init(g_original_game_dll);
    dluma_log_pair("INFO", "native_mod_chain_phase", "pre_game_complete");
    manager = g_original_initialize();
    dluma_atomic_increment(&g_original_initialize_count);
    dluma_log_u64("INFO", "original_game_script_init_count",
                (ULONGLONG)g_original_initialize_count);
    if (!manager) {
        dluma_log_line("ERROR", "Original InitializeGameScriptDLL returned null.");
        dluma_player_startup_abort("game_script_initialization_failed");
        dluma_localization_shutdown();
        (void)dluma_custom_main_menu_runtime_stop();
        (void)dluma_asset_mod_native_stop();
        dluma_ui_restore_workshop_menu_redirect();
        dluma_ui_restore_main_menu_redirect();
        dluma::fixes::ShutdownAllFixes();
        (void)dluma_quest_phase_player_stop();
        services_stopped = dluma_media_service_quiesce_all();
        dluma_native_mods_shutdown_chain(g_original_game_dll, services_stopped);
        dluma_unmount_content_pack();
        dluma_lua_discovery_shutdown();
        if (!dluma_editor_model_animation_fix_restore())
            dluma_log_line(
                "ERROR",
                "Player animation-bind guard could not be restored after the original initializer failed; its loader and gamedll remain pinned until process exit.");
        OutputDebugStringA("DLUMA: original InitializeGameScriptDLL returned null.\n");
        if (dluma_crash_reporting_stop()) {
            (void)dluma_log_stop();
        } else {
            dluma_log_emergency(
                "[CRASH] [ERROR] initialization cleanup could not safely restore crash diagnostics; logger state is retained until process exit\r\n");
        }
        return 0;
    }

    g_original_script_manager = manager;
    /* Initial stock text loading has completed. Bind the engine interface,
     * apply English and then the selected loader catalog with replacement,
     * verify the wrapped sentinel, publish the locale generation, and cache
     * dynamic/crash text before the frontend is presented. */
    engine_bound = dluma_engine_bind(&g_dluma.engine);
    if (engine_bound)
        dluma_media_service_bind(g_original_game_dll, manager);
    if (!editor_playtest && engine_bound && dluma_localization_bind()) {
        if (!dluma_localization_load_initial())
            dluma_log_line("WARN", "Game-native loader localization did not verify; English emergency fallbacks remain active.");
    } else if (!editor_playtest) {
        dluma_log_line("WARN", "Game-native localization binding was unavailable; English emergency fallbacks remain active.");
    }
    dluma_native_mods_post_game_init(g_original_game_dll, manager);
    dluma_log_pair("INFO", "native_mod_chain_phase", "post_game_complete");

    dluma_player_startup_status(dluma_player_startup_core_milestone(
        DLUMA_PLAYER_STARTUP_MILESTONE_HOSTS));
    managed_started = dluma_managed_host_start();
    dluma_log_pair(
        managed_started ? "INFO" : "WARN",
        "managed_mod_chain_phase",
        managed_started
            ? "managed_host_ready"
            : "managed_host_unavailable_csharp_only");
    dluma_log_pair("INFO", "lua_start_after_native_chain", "true");
    host_started = dluma_host_start();
    if (!host_started) {
        dluma_log_line("ERROR", "Universal script host failed to start; returning original script manager.");
    } else {
        DlumaStartupRecoveryPrompt prompt;
        /* A normal startup must not leave state=armed until process shutdown.
         * Resolve the current marker once all loader-owned runtime owners are
         * running. Preserve a prior interrupted-startup prompt until the
         * player has answered it. */
        if (!editor_playtest && !dluma_startup_recovery_snapshot_prompt(&prompt))
            (void)dluma_startup_recovery_complete_prompt();
        if (!editor_playtest && !dluma_runtime_list_capacity_start())
            dluma_log_line("WARN", "Loader-owned list-capacity adapter was unavailable; native row limits remain active.");
    }
    dluma_crash_reporting_refresh();
    return manager;
}

static __int64 dluma_player_shutdown(void)
{
    __int64 result = 0;
    int loader_stopped;
    int services_stopped;
    int managed_stopped;
    int quest_phase_stopped;
    int callbacks_drained;

    if (dluma_atomic_compare_exchange(&g_dluma_shutdown_entered, 1, 0) != 0)
        return 0;

    dluma_player_startup_abort("shutdown_before_game_initialization");
    if (dluma_configure_runtime_paths())
        dluma_log_line("INFO", "ShutdownGameScriptDLL entered.");
    if (!dluma_editor_model_animation_fix_quiesce()) {
        dluma_editor_model_animation_fix_retain();
        dluma_log_line(
            "ERROR",
            "Player animation-bind callbacks did not drain; shutdown stopped before tearing down the original gamedll and all reachable detour state remains pinned until process exit.");
        return 0;
    }
    if (!dluma_animation_refresh_runtime_player_game_thread_shutdown())
        dluma_log_line(
            "WARN",
            "Retained Player animation runtime pack was not released because "
            "shutdown did not run on its established game-thread owner or an "
            "animation refresh was still active; retaining it until process "
            "exit.");
    dluma_atomic_exchange(&g_dluma.stop_requested, 1);
    dluma_runtime_list_capacity_stop();
    dluma_localization_quiesce();

    if (!dluma_custom_main_menu_runtime_stop()) {
        dluma_editor_model_animation_fix_retain();
        dluma_log_line(
            "WARN",
            "Custom-main-menu callbacks did not drain; retaining the complete dependent subsystem, staged source, and quiesced animation-bind detour until process exit.");
        return 0;
    }

    quest_phase_stopped = dluma_quest_phase_player_stop();
    if (!quest_phase_stopped) {
        dluma_editor_model_animation_fix_retain();
        dluma_log_line(
            "WARN",
            "Quest-phase dispatch is still active; retaining the complete "
            "Player dependency graph and refusing teardown from inside the "
            "callback.");
        return 0;
    }

    services_stopped = dluma_media_service_quiesce_all();

    loader_stopped = dluma_host_stop();
    callbacks_drained =
        loader_stopped && services_stopped && quest_phase_stopped;
    /* Reverse host shutdown order is Lua -> C# -> native. A managed drain
     * failure retains downstream native owners rather than tearing out a
     * service table that managed code could still reach. */
    managed_stopped =
        dluma_managed_host_shutdown(callbacks_drained);
    callbacks_drained =
        callbacks_drained && managed_stopped;
    dluma_native_mods_shutdown_chain(g_original_game_dll, callbacks_drained);
    loader_stopped = callbacks_drained;
    if (loader_stopped) {
        dluma_ui_restore_workshop_menu_redirect();
        dluma_ui_restore_main_menu_redirect();
        if (!dluma_asset_mod_native_stop()) {
            dluma_log_line("WARN", "Asset file callbacks are still active; shutdown is deferred.");
            return 0;
        }
        dluma::fixes::ShutdownAllFixes();
        dluma_log_line("INFO", "Loader callbacks, timers, menu hook, and engine hooks are stopped.");
    } else {
        dluma_log_line("WARN", "Loader-owned state remains pinned in pass-through mode because shutdown did not positively drain every owner.");
    }

    if (loader_stopped) {
        dluma_localization_shutdown();
    }

    if (dluma_resolve_original_game_script()) {
        if (dluma_atomic_compare_exchange(&g_original_shutdown_count, 1, 0) == 0) {
            dluma_log_line("INFO", "Calling original ShutdownGameScriptDLL.");
            result = g_original_shutdown();
            dluma_log_line("INFO", "Original ShutdownGameScriptDLL returned.");
        }
    } else {
        dluma_log_line("WARN", "Original ShutdownGameScriptDLL could not be resolved during shutdown.");
    }

    if (!dluma_editor_model_animation_fix_restore()) {
        loader_stopped = 0;
        dluma_editor_model_animation_fix_retain();
        dluma_log_line(
            "ERROR",
            "Player animation-bind guard could not be restored after original shutdown; its loader and gamedll remain pinned until process exit.");
    }

    /* This only releases loader bookkeeping. It intentionally does not call
     * fs::rem_source after C++ game/UI teardown. */
    if (loader_stopped) dluma_unmount_content_pack();
    {
        int crash_stopped = dluma_crash_reporting_stop();
        if (!crash_stopped) {
            loader_stopped = 0;
            dluma_log_emergency(
                "[CRASH] [ERROR] crash diagnostics did not drain; normal logger state is retained until process exit\r\n");
        }
        dluma_log_line("INFO", "DLUMA shutdown completed.");
        if (crash_stopped && !dluma_log_stop()) loader_stopped = 0;
    }
    return result;
}

extern "C" __declspec(dllexport) void* __cdecl InitializeGameScriptDLL(void)
{
    dluma_isolation_note_script_entry(_ReturnAddress(),
        reinterpret_cast<const void*>(&InitializeGameScriptDLL),
        GetCurrentThreadId());
    if (!dluma_dual_host_exports_initialize()) {
        OutputDebugStringA("DLUMA: host classification initialization failed.\n");
        return 0;
    }
    if (dluma_dual_host_runtime_is_editor())
        return dluma_editor_initialize();
    if (dluma_dual_host_runtime_forwarding_only())
        return dluma_forwarding_only_initialize();
    return dluma_player_initialize();
}

extern "C" __declspec(dllexport) __int64 __cdecl ShutdownGameScriptDLL(void)
{
    if (dluma_atomic_compare_exchange(
            &g_dual_host_export_state, 0, 0) != 2)
        return 0;
    if (dluma_dual_host_runtime_is_editor())
        return dluma_editor_shutdown();
    if (dluma_dual_host_runtime_forwarding_only())
        return dluma_forwarding_only_shutdown();
    return dluma_player_shutdown();
}

extern "C" __declspec(dllexport) const char* __cdecl DLUMA_GetVersion(void)
{
    return DLUMA_VERSION;
}
