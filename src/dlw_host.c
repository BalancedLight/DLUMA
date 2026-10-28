#include "dlw_common.h"
#include "dlw_analysis_bridge.h"
#include "dlw_animation_refresh_runtime.h"
#include "dlw_frontend_level.h"
#include "dlw_discovery_watch.h"
#include "dlw_discord_presence.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_log.h"
#include "dlw_nightly.h"
#include "dlw_lua_discovery_internal.h"
#include "dlw_path_security.h"
#include "dlw_project_owner_identity.h"
#include "dlw_steam_presence.h"

DlumaRuntime g_dluma;

static void dluma_host_retain_for_process_exit(const char* reason)
{
    HMODULE pinned = 0;
    dluma_atomic_exchange(&g_dluma.retained_for_process_exit, 1);
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_PIN |
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (LPCWSTR)(const void*)&g_dluma, &pinned))
        dluma_log_hex("ERROR", "Failed to pin loader after unsafe shutdown drain",
                    GetLastError());
    dluma_log_pair("WARN", "loader_retained_until_process_exit",
                 reason ? reason : "owned callback state did not drain");
}

static unsigned int dluma_read_ini_u32(const WCHAR* section, const WCHAR* key, unsigned int fallback)
{
    WCHAR text[64];
    char utf8[64];
    text[0] = 0;
    GetPrivateProfileStringW(section, key, L"", text, DLUMA_ARRAY_COUNT(text), g_dluma.config_path);
    if (!text[0] || !dluma_wide_to_utf8(text, utf8, DLUMA_ARRAY_COUNT(utf8))) return fallback;
    return dluma_parse_u32(utf8, fallback);
}

static void dluma_read_ini_text(
    const WCHAR* section, const WCHAR* key, const WCHAR* fallback,
    char* out, unsigned int out_capacity)
{
    WCHAR text[64];
    text[0] = 0;
    GetPrivateProfileStringW(section, key, fallback, text,
                             DLUMA_ARRAY_COUNT(text), g_dluma.config_path);
    if (!dluma_wide_to_utf8(text, out, out_capacity)) out[0] = 0;
}

static void dluma_read_ini_bounded_text(
    const WCHAR* section, const WCHAR* key, const WCHAR* fallback,
    char* out, unsigned int out_capacity)
{
    WCHAR text[DLUMA_MAX_WPATH];
    text[0] = 0;
    GetPrivateProfileStringW(section, key, fallback, text,
                             DLUMA_ARRAY_COUNT(text), g_dluma.config_path);
    if (!dluma_wide_to_utf8(text, out, out_capacity)) out[0] = 0;
}

static DWORD dluma_read_local_lua_permission_policy(void)
{
    DWORD policy = 0;
#define DLUMA_READ_LOCAL_PERMISSION(key, fallback, bit) \
    do { \
        if (dluma_read_ini_u32(L"lua_local_permissions", L##key, fallback)) \
            policy |= (bit); \
    } while (0)
    DLUMA_READ_LOCAL_PERMISSION("game_read", 1, DLUMA_LOCAL_LUA_POLICY_GAME_READ);
    DLUMA_READ_LOCAL_PERMISSION("player_read", 1, DLUMA_LOCAL_LUA_POLICY_PLAYER_READ);
    DLUMA_READ_LOCAL_PERMISSION("world_query", 1, DLUMA_LOCAL_LUA_POLICY_WORLD_QUERY);
    DLUMA_READ_LOCAL_PERMISSION("events_sensor", 1, DLUMA_LOCAL_LUA_POLICY_EVENTS_SENSOR);
    DLUMA_READ_LOCAL_PERMISSION("events_interact", 1, DLUMA_LOCAL_LUA_POLICY_EVENTS_INTERACT);
    DLUMA_READ_LOCAL_PERMISSION("storage_mod", 1, DLUMA_LOCAL_LUA_POLICY_STORAGE_MOD);
    DLUMA_READ_LOCAL_PERMISSION("ui_debug", 1, DLUMA_LOCAL_LUA_POLICY_UI_DEBUG);
    DLUMA_READ_LOCAL_PERMISSION("ui_overlay", 1, DLUMA_LOCAL_LUA_POLICY_UI_OVERLAY);
    DLUMA_READ_LOCAL_PERMISSION("audio_play", 1, DLUMA_LOCAL_LUA_POLICY_AUDIO_PLAY);
    DLUMA_READ_LOCAL_PERMISSION("world_write", 1, DLUMA_LOCAL_LUA_POLICY_WORLD_WRITE);
    /* These remain disabled unless the local Workshop author/player opts in.
     * Approval alone never makes an unvalidated native quest binding live. */
    DLUMA_READ_LOCAL_PERMISSION("quest_write", 0, DLUMA_LOCAL_LUA_POLICY_QUEST_WRITE);
    DLUMA_READ_LOCAL_PERMISSION("events_enemy_killed", 0,
                              DLUMA_LOCAL_LUA_POLICY_EVENTS_ENEMY_KILLED);
    DLUMA_READ_LOCAL_PERMISSION("quest_phase_handle", 0,
                              DLUMA_LOCAL_LUA_POLICY_QUEST_PHASE_HANDLE);
    DLUMA_READ_LOCAL_PERMISSION("quest_phase_observe", 0,
                              DLUMA_LOCAL_LUA_POLICY_QUEST_PHASE_OBSERVE);
#undef DLUMA_READ_LOCAL_PERMISSION
    return policy;
}

int dluma_configure_runtime_paths(void)
{
    WCHAR self_path[DLUMA_MAX_WPATH];
    WCHAR runtime_dir[DLUMA_MAX_WPATH];
    if (g_dluma.log_path[0]) return dluma_log_start(g_dluma.log_path);
    self_path[0] = 0;
    if (!dluma_get_module_path_checked(
            g_dluma.self_module, self_path, DLUMA_ARRAY_COUNT(self_path)))
        return 0;
    dluma_wstr_copy(g_dluma.module_dir, DLUMA_MAX_WPATH, self_path);
    dluma_path_dirname(g_dluma.module_dir);
    dluma_path_join(runtime_dir, DLUMA_ARRAY_COUNT(runtime_dir), g_dluma.module_dir, L"dluma_runtime");
    if (!dluma_mkdirs(runtime_dir)) return 0;
    dluma_path_join(g_dluma.log_path, DLUMA_MAX_WPATH, runtime_dir, L"dluma.log");
    dluma_path_join(g_dluma.marker_root, DLUMA_MAX_WPATH, runtime_dir, L"markers");
    dluma_mkdirs(g_dluma.marker_root);
    dluma_path_join(g_dluma.config_path, DLUMA_MAX_WPATH, g_dluma.module_dir, L"dluma.ini");
    return dluma_log_start(g_dluma.log_path);
}

void dluma_load_config(void)
{
    static const WCHAR* root_keys[8] = { L"root1", L"root2", L"root3", L"root4", L"root5", L"root6", L"root7", L"root8" };
    static const WCHAR* watch_scr_keys[DLUMA_ASSET_TRACE_MAX_WATCH_PATHS] = {
        L"WatchScr1", L"WatchScr2", L"WatchScr3", L"WatchScr4",
        L"WatchScr5", L"WatchScr6", L"WatchScr7", L"WatchScr8" };
    static const WCHAR* watch_asset_keys[DLUMA_ASSET_TRACE_MAX_WATCH_PATHS] = {
        L"WatchAsset1", L"WatchAsset2", L"WatchAsset3", L"WatchAsset4",
        L"WatchAsset5", L"WatchAsset6", L"WatchAsset7", L"WatchAsset8" };
    unsigned int i;
    memset(&g_dluma.config, 0, sizeof(g_dluma.config));
    g_dluma.config.poll_interval_ms = dluma_read_ini_u32(L"loader", L"poll_interval_ms", 250);
    g_dluma.config.universal_screen_poll_interval_ms =
        dluma_read_ini_u32(
            L"loader", L"universal_screen_poll_interval_ms", 100);
    g_dluma.config.unmatched_level_grace_ms = dluma_read_ini_u32(L"loader", L"unmatched_level_grace_ms", 1500);
    g_dluma.config.enable_key_events = (unsigned char)(dluma_read_ini_u32(L"loader", L"enable_key_events", 1) ? 1 : 0);
    g_dluma.config.log_timer_events = (unsigned char)(dluma_read_ini_u32(L"loader", L"log_timer_events", 0) ? 1 : 0);
    g_dluma.config.discord_rich_presence_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"loader", L"discord_rich_presence_enabled", 1) ? 1 : 0);
    g_dluma.config.editor_discord_rich_presence_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"loader", L"editor_discord_rich_presence_enabled", 1) ? 1 : 0);
    g_dluma.config.steam_rich_presence_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"loader", L"steam_rich_presence_enabled", 1) ? 1 : 0);
    g_dluma.config.custom_map_workshop_loading_screen_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"loader", L"custom_map_workshop_loading_screen", 1) ? 1 : 0);
    g_dluma.config.developer_mode = (unsigned char)(
        dluma_read_ini_u32(L"loader", L"developer_mode", 0) ? 1 : 0);
    g_dluma.config.analysis_bridge_enabled = (unsigned char)(
        dluma_read_ini_u32(L"AnalysisBridge", L"Enabled", 0) ? 1 : 0);
    g_dluma.config.analysis_bridge_allow_actions = (unsigned char)(
        dluma_read_ini_u32(L"AnalysisBridge", L"AllowActions", 0) ? 1 : 0);
    dluma_read_ini_text(
        L"AnalysisBridge", L"StateId", L"",
        g_dluma.config.analysis_bridge_state_id,
        DLUMA_ARRAY_COUNT(g_dluma.config.analysis_bridge_state_id));
    g_dluma.config.crash_dialog_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"crash_dialog", L"enabled", 1) ? 1 : 0);
    g_dluma.config.crash_random_titles =
        (unsigned char)(dluma_read_ini_u32(
            L"crash_dialog", L"random_titles", 0) ? 1 : 0);
    g_dluma.config.custom_map_quest_level_name_crash_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"GameFixes", L"CustomMapQuestLevelNameCrash", 1) ? 1 : 0);
    g_dluma.config.custom_map_campaign_credits_exit_crash_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"GameFixes", L"CustomMapCampaignCreditsExitCrash", 1) ? 1 : 0);
    g_dluma.config.main_menu_credits_music_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"GameFixes", L"MainMenuCreditsMusic", 1) ? 1 : 0);
    g_dluma.config.custom_map_asset_isolation_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"GameFixes", L"CustomMapAssetIsolation", 0) ? 1 : 0);
    /* Off by default: the guard removes the engine+0x57E60E null read during a
     * custom menu background load, but detouring an accessor the engine calls
     * for every mesh has measurably stalled that load, so it stays opt-in
     * until the cost is resolved. */
    g_dluma.config.menu_mesh_name_guard_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"GameFixes", L"MenuMeshNameGuard", 0) ? 1 : 0);
    g_dluma.config.hide_invalid_workshop_projects_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"GameFixes", L"HideInvalidWorkshopProjects", 1) ? 1 : 0);
    g_dluma.config.custom_map_loading_screen_content_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"GameFixes", L"CustomMapLoadingScreenContent", 1) ? 1 : 0);
    g_dluma.config.mouse_window_lock_enabled =
        (unsigned char)(dluma_read_ini_u32(
            L"GameFixes", L"MouseWindowLock", 1) ? 1 : 0);
    g_dluma.config.lua_memory_limit_bytes =
        dluma_read_ini_u32(L"lua", L"memory_limit_bytes", 8UL * 1024UL * 1024UL);
    g_dluma.config.lua_instruction_limit =
        dluma_read_ini_u32(L"lua", L"instruction_limit", 200000);
    g_dluma.config.lua_wall_clock_limit_ms =
        dluma_read_ini_u32(L"lua", L"wall_clock_limit_ms", 10);
    g_dluma.config.lua_max_callbacks =
        dluma_read_ini_u32(L"lua", L"max_callbacks", 64);
    g_dluma.config.lua_max_timers =
        dluma_read_ini_u32(L"lua", L"max_timers", 32);
    g_dluma.config.lua_max_rules =
        dluma_read_ini_u32(L"lua", L"max_rules", 16);
    g_dluma.config.lua_log_messages_per_second =
        dluma_read_ini_u32(L"lua", L"log_messages_per_second", 20);
    g_dluma.config.lua_local_permission_policy =
        dluma_read_local_lua_permission_policy();
    for (i = 0; i < DLUMA_ARRAY_COUNT(watch_scr_keys); ++i) {
        char watched_path[DLUMA_ASSET_TRACE_WATCH_PATH_CAPACITY];
        watched_path[0] = 0;
        dluma_read_ini_bounded_text(
            L"AssetIsolationTrace", watch_scr_keys[i],
            i == 0 ? L"data\\scripts\\dluma_asset_isolation_test.scr" : L"",
            watched_path, DLUMA_ARRAY_COUNT(watched_path));
        if (watched_path[0] &&
            g_dluma.config.custom_map_asset_isolation_watch_scr_count <
                DLUMA_ASSET_TRACE_MAX_WATCH_PATHS) {
            dluma_str_copy(
                g_dluma.config.custom_map_asset_isolation_watch_scr
                    [g_dluma.config.custom_map_asset_isolation_watch_scr_count],
                DLUMA_ASSET_TRACE_WATCH_PATH_CAPACITY, watched_path);
            ++g_dluma.config.custom_map_asset_isolation_watch_scr_count;
        }
    }
    for (i = 0; i < DLUMA_ARRAY_COUNT(watch_asset_keys); ++i) {
        char watched_path[DLUMA_ASSET_TRACE_WATCH_PATH_CAPACITY];
        watched_path[0] = 0;
        dluma_read_ini_bounded_text(
            L"AssetIsolationTrace", watch_asset_keys[i], L"",
            watched_path, DLUMA_ARRAY_COUNT(watched_path));
        if (watched_path[0] &&
            g_dluma.config.custom_map_asset_isolation_watch_asset_count <
                DLUMA_ASSET_TRACE_MAX_WATCH_PATHS) {
            dluma_str_copy(
                g_dluma.config.custom_map_asset_isolation_watch_asset
                    [g_dluma.config.custom_map_asset_isolation_watch_asset_count],
                DLUMA_ASSET_TRACE_WATCH_PATH_CAPACITY, watched_path);
            ++g_dluma.config.custom_map_asset_isolation_watch_asset_count;
        }
    }
    if (g_dluma.config.poll_interval_ms < 50) g_dluma.config.poll_interval_ms = 50;
    if (g_dluma.config.poll_interval_ms > 5000) g_dluma.config.poll_interval_ms = 5000;
    if (g_dluma.config.universal_screen_poll_interval_ms < 10)
        g_dluma.config.universal_screen_poll_interval_ms = 10;
    if (g_dluma.config.universal_screen_poll_interval_ms > 5000)
        g_dluma.config.universal_screen_poll_interval_ms = 5000;
    if (g_dluma.config.unmatched_level_grace_ms < 250) g_dluma.config.unmatched_level_grace_ms = 250;
    if (g_dluma.config.unmatched_level_grace_ms > 30000) g_dluma.config.unmatched_level_grace_ms = 30000;
    if (g_dluma.config.lua_memory_limit_bytes < 256UL * 1024UL)
        g_dluma.config.lua_memory_limit_bytes = 256UL * 1024UL;
    if (g_dluma.config.lua_memory_limit_bytes > 64UL * 1024UL * 1024UL)
        g_dluma.config.lua_memory_limit_bytes = 64UL * 1024UL * 1024UL;
    if (g_dluma.config.lua_instruction_limit < 10000)
        g_dluma.config.lua_instruction_limit = 10000;
    if (g_dluma.config.lua_instruction_limit > 10000000)
        g_dluma.config.lua_instruction_limit = 10000000;
    if (g_dluma.config.lua_wall_clock_limit_ms < 1) g_dluma.config.lua_wall_clock_limit_ms = 1;
    if (g_dluma.config.lua_wall_clock_limit_ms > 100) g_dluma.config.lua_wall_clock_limit_ms = 100;
    if (g_dluma.config.lua_max_callbacks < 8) g_dluma.config.lua_max_callbacks = 8;
    if (g_dluma.config.lua_max_callbacks > 96) g_dluma.config.lua_max_callbacks = 96;
    if (g_dluma.config.lua_max_timers < 1) g_dluma.config.lua_max_timers = 1;
    if (g_dluma.config.lua_max_timers > 64) g_dluma.config.lua_max_timers = 64;
    if (g_dluma.config.lua_max_rules < 1) g_dluma.config.lua_max_rules = 1;
    if (g_dluma.config.lua_max_rules > 32) g_dluma.config.lua_max_rules = 32;
    if (g_dluma.config.lua_log_messages_per_second < 1)
        g_dluma.config.lua_log_messages_per_second = 1;
    if (g_dluma.config.lua_log_messages_per_second > 100)
        g_dluma.config.lua_log_messages_per_second = 100;
    for (i = 0; i < 8; ++i) {
        WCHAR root[DLUMA_MAX_WPATH];
        root[0] = 0;
        GetPrivateProfileStringW(L"roots", root_keys[i], L"", root, DLUMA_ARRAY_COUNT(root), g_dluma.config_path);
        if (root[0]) dluma_wstr_copy(g_dluma.config.configured_roots[g_dluma.config.configured_root_count++], DLUMA_MAX_WPATH, root);
    }
    dluma_log_u64("INFO", "poll_interval_ms", g_dluma.config.poll_interval_ms);
    dluma_log_u64("INFO", "universal_screen_poll_interval_ms",
                g_dluma.config.universal_screen_poll_interval_ms);
    dluma_log_u64("INFO", "unmatched_level_grace_ms", g_dluma.config.unmatched_level_grace_ms);
    dluma_log_u64("INFO", "developer_mode", g_dluma.config.developer_mode);
    dluma_log_u64(
        "INFO", "discord_rich_presence_enabled",
        g_dluma.config.discord_rich_presence_enabled);
    dluma_log_u64(
        "INFO", "editor_discord_rich_presence_enabled",
        g_dluma.config.editor_discord_rich_presence_enabled);
    dluma_log_u64(
        "INFO", "steam_rich_presence_enabled",
        g_dluma.config.steam_rich_presence_enabled);
    dluma_log_u64(
        "INFO", "custom_map_workshop_loading_screen_enabled",
        g_dluma.config.custom_map_workshop_loading_screen_enabled);
    dluma_log_u64(
        "INFO", "mouse_window_lock_enabled",
        g_dluma.config.mouse_window_lock_enabled);
    dluma_log_u64("INFO", "crash_dialog_enabled",
                g_dluma.config.crash_dialog_enabled);
    dluma_log_u64("INFO", "crash_random_titles",
                g_dluma.config.crash_random_titles);
    dluma_log_u64("INFO", "custom_map_quest_level_name_crash_enabled",
                g_dluma.config.custom_map_quest_level_name_crash_enabled);
    dluma_log_u64("INFO", "custom_map_campaign_credits_exit_crash_enabled",
                g_dluma.config.custom_map_campaign_credits_exit_crash_enabled);
    dluma_log_u64("INFO", "main_menu_credits_music_enabled",
                g_dluma.config.main_menu_credits_music_enabled);
    dluma_log_u64("INFO", "custom_map_asset_isolation_enabled",
                g_dluma.config.custom_map_asset_isolation_enabled);
    dluma_log_u64("INFO", "menu_mesh_name_guard_enabled",
                g_dluma.config.menu_mesh_name_guard_enabled);
    dluma_log_u64("INFO", "hide_invalid_workshop_projects_enabled",
                g_dluma.config.hide_invalid_workshop_projects_enabled);
    dluma_log_u64("INFO", "custom_map_loading_screen_content_enabled",
                g_dluma.config.custom_map_loading_screen_content_enabled);
    dluma_log_u64("INFO", "custom_map_asset_isolation_evidence_enabled",
                g_dluma.config.custom_map_asset_isolation_enabled);
    dluma_log_u64(
        "INFO", "custom_map_asset_isolation_watch_scr_count",
        g_dluma.config.custom_map_asset_isolation_watch_scr_count);
    dluma_log_u64(
        "INFO", "custom_map_asset_isolation_watch_asset_count",
        g_dluma.config.custom_map_asset_isolation_watch_asset_count);
    dluma_log_u64("INFO", "lua_memory_limit_bytes", g_dluma.config.lua_memory_limit_bytes);
    dluma_log_u64("INFO", "lua_instruction_limit", g_dluma.config.lua_instruction_limit);
    dluma_log_u64("INFO", "lua_wall_clock_limit_ms", g_dluma.config.lua_wall_clock_limit_ms);
    dluma_log_u64("INFO", "lua_local_permission_policy",
                g_dluma.config.lua_local_permission_policy);
}

int dluma_wait_for_stop(DWORD timeout_ms)
{
    if (g_dluma.stop_requested) return 1;
    if (!g_dluma.stop_event) {
        Sleep(timeout_ms);
        return g_dluma.stop_requested != 0;
    }
    return WaitForSingleObject(g_dluma.stop_event, timeout_ms) == WAIT_OBJECT_0;
}

static int dluma_asset_trace_requested(void)
{
    return g_dluma.config.custom_map_asset_isolation_enabled;
}

static void dluma_asset_trace_annotate_active_level(
    const char* map_name, void* level, const char* project_root,
    ULONGLONG worker_timestamp_ms)
{
    char record[DLUMA_MAX_WPATH * 2 + DLUMA_MAX_MAP_NAME + 192];
    if (!dluma_asset_trace_requested() || !map_name || !map_name[0]) return;
    dluma_custom_map_asset_trace_active_level_observed(
        map_name, level,
        project_root && project_root[0] ? project_root : "<unknown>",
        worker_timestamp_ms);
    record[0] = 0;
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), "map=");
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), map_name);
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), " project=");
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record),
                   project_root && project_root[0] ? project_root : "<unknown>");
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record),
                   " thread=lifecycle_worker role=late_worker_annotation cleanup_authority=none");
    dluma_log_pair("INFO", "passive_destination_observed_after_load", record);
}

static void dluma_asset_trace_late_transition(const char* old_map,
                                            const char* new_map)
{
    char record[DLUMA_MAX_MAP_NAME * 2 + 256];
    if (!dluma_asset_trace_requested() || !old_map || !old_map[0] ||
        !new_map || !new_map[0]) return;
    record[0] = 0;
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), "old_map=");
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), old_map);
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), " new_map=");
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record), new_map);
    dluma_str_append(record, DLUMA_ARRAY_COUNT(record),
                   " thread=lifecycle_worker role=late_worker_annotation cleanup_authority=none");
    dluma_log_pair("INFO", "passive_destination_observed_after_load", record);
}

static void dluma_unload_active_map(const char* reason, int publish_empty_lua_context)
{
    char line[512];
    if (!g_dluma.active_map[0]) return;
    line[0] = 0;
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), "Unloading workshop script context for map '");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), g_dluma.active_map);
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), "' (");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), reason ? reason : "level transition");
    dluma_str_append(line, DLUMA_ARRAY_COUNT(line), ").");
    dluma_log_line("INFO", line);
    dluma_scripts_fire(&g_dluma.scripts, DLUMA_EVENT_MAP_UNLOAD, 0);
    dluma_scripts_clear(&g_dluma.scripts);
    if (publish_empty_lua_context)
        dluma_lua_runtime_publish_context(L"", "", "", "", 0, 0);
    dluma_lua_clear_locals();
    g_dluma.active_map[0] = 0;
    g_dluma.active_project_root[0] = 0;
    g_dluma.active_project_owner_epoch = 0;
    g_dluma.active_project_owner_transition_generation = 0;
    g_dluma.map_loaded_ms = 0;
    g_dluma.first_unmatched_ms = 0;
    dluma_crash_publish_runtime_context(
        "map_unloaded", L"", "", 0, 0);
    dluma_engine_clear_tracked_level(&g_dluma.engine);
}

static void dluma_activate_map(
    const char* map_name, const WCHAR* project_root,
    const DlumaProjectOwnerProofSnapshot* proof)
{
    char project[DLUMA_MAX_WPATH * 2];
    char project_name[128];
    char workshop_id[64];
    unsigned char is_workshop = 0;
    dluma_str_copy(g_dluma.active_map, DLUMA_MAX_MAP_NAME, map_name);
    dluma_wstr_copy(g_dluma.active_project_root, DLUMA_MAX_WPATH, project_root);
    g_dluma.active_project_owner_epoch =
        proof && proof->status == DLUMA_PROJECT_OWNER_PROOF_READY
            ? proof->epoch
            : 0;
    g_dluma.active_project_owner_transition_generation =
        proof && proof->status == DLUMA_PROJECT_OWNER_PROOF_READY
            ? proof->transition_generation
            : 0;
    g_dluma.map_loaded_ms = GetTickCount64();
    g_dluma.first_unmatched_ms = 0;
    dluma_crash_publish_runtime_context(
        "map_active", project_root, map_name,
        g_dluma.active_project_owner_epoch,
        g_dluma.active_project_owner_transition_generation);
    dluma_wide_to_utf8(project_root, project, DLUMA_ARRAY_COUNT(project));
    dluma_log_pair("INFO", "active_workshop_map", map_name);
    dluma_log_pair("INFO", "active_workshop_project", project);
    dluma_lua_discover_locals(project_root, map_name);
    project_name[0] = 0;
    workshop_id[0] = 0;
    dluma_describe_project_source(project_root,
                                project_name, DLUMA_ARRAY_COUNT(project_name),
                                workshop_id, DLUMA_ARRAY_COUNT(workshop_id),
                                &is_workshop);
    (void)is_workshop;
    dluma_lua_runtime_publish_context(
        project_root, project_name, workshop_id, map_name,
        g_dluma.lua_discovery.local_files,
        g_dluma.lua_discovery.local_count);
    dluma_scripts_load(&g_dluma.scripts, project_root, map_name);
    dluma_scripts_fire(&g_dluma.scripts, DLUMA_EVENT_MAP_LOAD, 0);
    dluma_engine_track_last_level(&g_dluma.engine);
}

typedef struct DlumaEditorPlaytestActivation {
    DlumaDirectoryIdentity project_identity;
    char map_name[DLUMA_MAX_MAP_NAME];
    ULONGLONG generation;
} DlumaEditorPlaytestActivation;

static int dluma_prepare_editor_playtest_handoff(
    DlumaEditorPlaytestActivation** activation_out)
{
    const DlumaDualHostRuntimeSnapshot* runtime;
    DlumaDirectoryIdentity project_identity;
    DlumaEditorPlaytestActivation* activation;
    WCHAR project_root[DLUMA_MAX_WPATH];
    if (!activation_out) return 0;
    *activation_out = 0;
    if (!dluma_dual_host_runtime_is_playtest()) return 1;
    runtime = dluma_dual_host_runtime_snapshot();
    if (!runtime || !runtime->classification.handoff_valid ||
        !runtime->handoff.project_root_utf8[0] ||
        !runtime->handoff.project_name[0] ||
        !runtime->handoff.map_name[0] ||
        !dluma_utf8_to_wide(
            runtime->handoff.project_root_utf8, project_root,
            DLUMA_ARRAY_COUNT(project_root)) ||
        !dluma_open_directory_identity(project_root, &project_identity) ||
        !dluma_validate_map_anchor_under_project(
            project_root, runtime->handoff.map_name,
            &project_identity)) {
        dluma_log_pair(
            "ERROR", "editor_playtest_context",
            "validated handoff project/map identity changed before runtime activation");
        return 0;
    }
    /* A direct-to-map playtest never presents menu_lvl_new, so the ordinary
     * frame-authority startup proof cannot complete.  Admit only the exact
     * normalized level named by the still-valid handoff, after the project
     * directory and map anchor above have been re-opened and revalidated.
     * The frame route still needs all exact PE/vtable/caller/serialization
     * proof plus two matching live ILevel observations before dispatch. */
    if (!dluma_engine_configure_playtest_frame_authority(
            &g_dluma.engine, runtime->handoff.map_name)) {
        dluma_log_pair(
            "ERROR", "editor_playtest_context",
            "exact game-frame authority could not be bound to the revalidated handoff level");
        return 0;
    }
    activation = (DlumaEditorPlaytestActivation*)dluma_alloc(
        sizeof(DlumaEditorPlaytestActivation));
    if (!activation) {
        dluma_log_pair(
            "ERROR", "editor_playtest_context",
            "validated handoff activation allocation failed");
        return 0;
    }
    memset(activation, 0, sizeof(*activation));
    activation->project_identity = project_identity;
    dluma_str_copy(
        activation->map_name, DLUMA_ARRAY_COUNT(activation->map_name),
        runtime->handoff.map_name);
    activation->generation = runtime->handoff.generation;
    *activation_out = activation;
    dluma_log_pair(
        "INFO", "editor_playtest_context",
        "validated handoff project/map queued for lifecycle-worker activation");
    return 1;
}

static void dluma_activate_editor_playtest_handoff_on_worker(
    DlumaEditorPlaytestActivation* activation)
{
    DlumaDirectoryIdentity current_identity;
    if (!activation) return;
    memset(&current_identity, 0, sizeof(current_identity));
    if (!dluma_open_directory_identity(
            activation->project_identity.final_path,
            &current_identity) ||
        !dluma_directory_identity_equal(
            &activation->project_identity, &current_identity) ||
        !dluma_validate_map_anchor_under_project(
            current_identity.final_path, activation->map_name,
            &current_identity)) {
        dluma_log_pair(
            "ERROR", "editor_playtest_context",
            "validated handoff project/map identity changed before lifecycle-worker activation");
        dluma_free(activation);
        return;
    }
    dluma_activate_map(
        activation->map_name, current_identity.final_path, 0);
    dluma_log_pair(
        "INFO", "editor_playtest_context",
        "authoritative handoff project/map published before lifecycle polling");
    dluma_log_u64(
        "INFO", "editor_playtest_context_generation",
        activation->generation);
    dluma_free(activation);
}

static int dluma_is_frontend_level(const char* map_name)
{
    return dluma_frontend_level_name_is_frontend(map_name);
}

DWORD WINAPI dluma_worker_thread(LPVOID parameter)
{
    DlumaEditorPlaytestActivation* playtest_activation =
        (DlumaEditorPlaytestActivation*)parameter;
    ULONGLONG last_root_probe = 0;
    ULONGLONG last_full_scan = GetTickCount64();
    ULONGLONG last_fallback_scan = 0;
    ULONGLONG discovery_dirty_since = 0;
    ULONGLONG last_project_probe_ms = 0;
    ULONGLONG query_failure_started_ms = 0;
    char last_project_probe_map[DLUMA_MAX_MAP_NAME];
    int bind_warning_logged = 0;
    int query_failure_warning_logged = 0;
    last_project_probe_map[0] = 0;
    g_dluma.last_observed_map[0] = 0;
    g_dluma.last_unowned_map[0] = 0;
    dluma_log_line("INFO", "Lifecycle worker started.");
    dluma_activate_editor_playtest_handoff_on_worker(
        playtest_activation);

    while (!g_dluma.stop_requested) {
        ULONGLONG now = GetTickCount64();
        ULONGLONG level_sample_ms = 0;
        char observed_map[DLUMA_MAX_MAP_NAME];
        void* observed_level = 0;
        WCHAR project[DLUMA_MAX_WPATH];
        int has_level;
        int positive_level_changed;

        if (dluma_engine_take_tracked_level_destroyed(&g_dluma.engine)) {
            dluma_log_line("INFO", "tracked_level_destroyed = authoritative ILevel destructor signal received");
            dluma_unload_active_map("tracked level was destroyed", 1);
            g_dluma.last_observed_map[0] = 0;
            query_failure_started_ms = 0;
            query_failure_warning_logged = 0;
        }

        /* Discovery is process-local metadata work and remains available even
         * when an editor/unsupported build cannot bind gameplay methods. */
        int run_discovery = 0;
        DlumaLuaDiscoveryMode discovery_mode =
            DLUMA_LUA_DISCOVERY_INCREMENTAL;
        if (!last_root_probe ||
            now - last_root_probe >= DLUMA_DISCOVERY_ROOT_PROBE_MS) {
            int roots_changed;
            dluma_discover_workshop_roots();
            roots_changed = dluma_discovery_watch_sync_roots();
            if (roots_changed && !discovery_dirty_since)
                discovery_dirty_since = now;
            last_root_probe = now;
        }
        if (dluma_discovery_watch_poll()) {
            discovery_dirty_since = now;
            (void)dluma_discovery_watch_sync_roots();
        }
        if (now - last_full_scan >=
            DLUMA_DISCOVERY_FULL_SWEEP_MS) {
            run_discovery = 1;
            discovery_mode =
                DLUMA_LUA_DISCOVERY_FULL_CONSISTENCY;
        } else if (dluma_discovery_watch_requires_fallback() &&
                   (!last_fallback_scan ||
                    now - last_fallback_scan >=
                        DLUMA_DISCOVERY_FALLBACK_SWEEP_MS)) {
            run_discovery = 1;
            discovery_mode = DLUMA_LUA_DISCOVERY_FALLBACK;
        } else if (discovery_dirty_since &&
                   now - discovery_dirty_since >=
                       DLUMA_DISCOVERY_DEBOUNCE_MS) {
            run_discovery = 1;
            discovery_mode = DLUMA_LUA_DISCOVERY_INCREMENTAL;
        }
        if (run_discovery) {
            DlumaLuaMetadata* snapshot;
            unsigned int count;
            ULONGLONG revision = 0;
            dluma_workshop_menu_refresh_metadata();
            dluma_lua_discover_globals_mode(discovery_mode);
            snapshot = (DlumaLuaMetadata*)dluma_alloc(
                sizeof(DlumaLuaMetadata) * DLUMA_MAX_LUA_FILES);
            if (snapshot) {
                count = dluma_lua_snapshot_globals_with_revision(
                    snapshot, DLUMA_MAX_LUA_FILES, 0, &revision);
                if (count > DLUMA_MAX_LUA_FILES) count = DLUMA_MAX_LUA_FILES;
                dluma_lua_runtime_publish_globals(snapshot, count, revision);
                dluma_free(snapshot);
            }
            if (discovery_mode ==
                DLUMA_LUA_DISCOVERY_FULL_CONSISTENCY)
                last_full_scan = now;
            if (discovery_mode ==
                DLUMA_LUA_DISCOVERY_FALLBACK)
                last_fallback_scan = now;
            discovery_dirty_since = 0;
        }

        /* Remote preview bodies are produced on the game thread, but cache
         * writes belong to this existing worker.  This pump performs only
         * bounded loader-owned filesystem work and never touches XUI. */
        dluma_workshop_menu_worker_tick();

        if (!g_dluma.engine.ready) {
            if (!dluma_engine_bind(&g_dluma.engine)) {
                if (!bind_warning_logged) {
                    dluma_log_line("WARN", "Waiting for engine_x64_rwdi.dll and game lifecycle exports.");
                    bind_warning_logged = 1;
                }
                if (dluma_wait_for_stop(1000)) break;
                continue;
            }
            bind_warning_logged = 0;
        }

        /* Keep timers and key edges alive while the confirmed map context is
         * active.  A transient failure in the level pointer chain must not
         * pause F8/key dispatch or heartbeat timers. */
        if (g_dluma.active_map[0]) dluma_scripts_tick(&g_dluma.scripts, now);

        observed_map[0] = 0;
        has_level = dluma_ui_read_level_snapshot(
            observed_map,
            DLUMA_ARRAY_COUNT(observed_map),
            &observed_level,
            &level_sample_ms);
        if (has_level && now - level_sample_ms > 3000) has_level = 0;

        if (!has_level || !observed_map[0]) {
            observed_map[0] = 0;
            if (!query_failure_started_ms) query_failure_started_ms = now;
            if (g_dluma.active_map[0] && !query_failure_warning_logged &&
                now - query_failure_started_ms >= g_dluma.config.unmatched_level_grace_ms) {
                char retained[DLUMA_MAX_MAP_NAME + 96];
                retained[0] = 0;
                dluma_str_append(retained, DLUMA_ARRAY_COUNT(retained),
                               "main-thread level snapshot unavailable; retaining confirmed map '");
                dluma_str_append(retained, DLUMA_ARRAY_COUNT(retained), g_dluma.active_map);
                dluma_str_append(retained, DLUMA_ARRAY_COUNT(retained), "'");
                dluma_log_pair("WARN", "transient_level_query_failure", retained);
                query_failure_warning_logged = 1;
            }
            /* Null is an unknown sample, not proof that the game entered the
             * frontend.  Only a positive frontend/different-level observation
             * or loader shutdown can end a local script context. */
            if (dluma_wait_for_stop(g_dluma.config.poll_interval_ms)) break;
            continue;
        }

        if (query_failure_warning_logged) {
            dluma_log_u64("INFO", "level_query_recovered_after_ms",
                        now - query_failure_started_ms);
        }
        query_failure_started_ms = 0;
        query_failure_warning_logged = 0;
        g_dluma.engine.last_level_interface = observed_level;

        /* Log confirmed positive transitions independently from project/script
         * discovery.  Failed samples never manufacture <none> transitions. */
        positive_level_changed =
            dluma_stricmp(g_dluma.last_observed_map, observed_map) != 0;
        if (positive_level_changed) {
            char transition[DLUMA_MAX_MAP_NAME * 2 + 64];
            transition[0] = 0;
            dluma_str_append(transition, DLUMA_ARRAY_COUNT(transition),
                           g_dluma.last_observed_map[0] ? g_dluma.last_observed_map : "<none>");
            dluma_str_append(transition, DLUMA_ARRAY_COUNT(transition), " -> ");
            dluma_str_append(transition, DLUMA_ARRAY_COUNT(transition),
                           observed_map[0] ? observed_map : "<none>");
            dluma_log_pair("INFO", "observed_level_transition", transition);
            dluma_str_copy(g_dluma.last_observed_map, DLUMA_ARRAY_COUNT(g_dluma.last_observed_map), observed_map);
            last_project_probe_map[0] = 0;
            last_project_probe_ms = 0;
        }

        if (dluma_is_frontend_level(observed_map)) {
            if (positive_level_changed)
                dluma_asset_trace_annotate_active_level(
                    observed_map, observed_level, "<frontend>", now);
            g_dluma.last_unowned_map[0] = 0;
            if (g_dluma.active_map[0]) {
                dluma_asset_trace_late_transition(g_dluma.active_map, observed_map);
                dluma_unload_active_map("confirmed frontend level became active", 1);
            }
        } else {
            int same_active_map =
                g_dluma.active_map[0] &&
                dluma_stricmp(
                    g_dluma.active_map, observed_map) == 0;
            int replaced_active_context =
                g_dluma.active_map[0] && !same_active_map;
            int should_probe;
            int found_project = 0;
            DlumaProjectOwnerProofSnapshot project_proof;
            memset(&project_proof, 0, sizeof(project_proof));
            if (same_active_map)
                g_dluma.first_unmatched_ms = 0;
            if (replaced_active_context) {
                dluma_asset_trace_late_transition(
                    g_dluma.active_map, observed_map);
                dluma_unload_active_map("different confirmed level became active", 0);
            }
            should_probe =
                dluma_stricmp(
                    last_project_probe_map, observed_map) != 0 ||
                now - last_project_probe_ms >= 3000;
            project[0] = 0;
            if (should_probe) {
                dluma_str_copy(
                    last_project_probe_map,
                    DLUMA_ARRAY_COUNT(last_project_probe_map),
                    observed_map);
                last_project_probe_ms = now;
                dluma_log_pair(
                    "INFO", "project_lookup_for_map",
                    observed_map);
                found_project = dluma_find_project_for_map(
                    observed_map, project,
                    DLUMA_ARRAY_COUNT(project), &project_proof,
                    same_active_map
                        ? g_dluma.active_project_owner_epoch
                        : 0,
                    same_active_map
                        ? g_dluma.active_project_owner_transition_generation
                        : 0);
            }
            if (found_project) {
                char project_utf8[DLUMA_MAX_WPATH * 2];
                g_dluma.last_unowned_map[0] = 0;
                project_utf8[0] = 0;
                dluma_wide_to_utf8(
                    project, project_utf8,
                    DLUMA_ARRAY_COUNT(project_utf8));
                if (same_active_map) {
                    if (dluma_project_roots_same_identity(
                            g_dluma.active_project_root,
                            project)) {
                        if (project_proof.status ==
                            DLUMA_PROJECT_OWNER_PROOF_READY) {
                            g_dluma.active_project_owner_epoch =
                                project_proof.epoch;
                            g_dluma.active_project_owner_transition_generation =
                                project_proof.transition_generation;
                        }
                    } else {
                        dluma_log_pair(
                            "INFO",
                            "same_map_exact_project_transition",
                            observed_map);
                        dluma_unload_active_map(
                            "exact provider changed while the internal map name remained active",
                            0);
                        dluma_asset_trace_annotate_active_level(
                            observed_map, observed_level,
                            project_utf8, now);
                        dluma_activate_map(
                            observed_map, project,
                            &project_proof);
                    }
                } else {
                    if (positive_level_changed)
                        dluma_asset_trace_annotate_active_level(
                            observed_map, observed_level,
                            project_utf8, now);
                    dluma_activate_map(
                        observed_map, project, &project_proof);
                }
            } else if (!same_active_map) {
                if (positive_level_changed)
                    dluma_asset_trace_annotate_active_level(
                        observed_map, observed_level,
                        "<unknown>", now);
                if (replaced_active_context)
                    dluma_lua_runtime_publish_context(
                        L"", "", "", "", 0, 0);
                if (dluma_stricmp(
                        g_dluma.last_unowned_map,
                        observed_map) != 0) {
                    dluma_str_copy(
                        g_dluma.last_unowned_map,
                        DLUMA_ARRAY_COUNT(g_dluma.last_unowned_map),
                        observed_map);
                    dluma_log_pair(
                        "WARN",
                        "map_detected_but_no_project_owner_found",
                        observed_map);
                }
            }
        }
        if (g_dluma.active_project_root[0])
            dluma_animation_refresh_runtime_poll_project(
                g_dluma.active_project_root);
        if (dluma_wait_for_stop(g_dluma.config.poll_interval_ms)) break;
    }

    dluma_unload_active_map("loader shutdown", 1);
    dluma_log_line("INFO", "Lifecycle worker stopped.");
    return 0;
}

int dluma_host_start(void)
{
    DWORD thread_id = 0;
    int ui_started;
    DlumaEditorPlaytestActivation* playtest_activation = 0;
    if (dluma_atomic_compare_exchange(&g_dluma.started, 1, 0) != 0) return 1;
    g_dluma.stop_requested = 0;
    g_dluma.retained_for_process_exit = 0;
    dluma_atomic_exchange(&g_dluma.lifecycle_state, DLUMA_LIFECYCLE_RUNNING);
    dluma_crash_publish_runtime_context(
        "host_start", g_dluma.active_project_root, g_dluma.active_map,
        g_dluma.active_project_owner_epoch,
        g_dluma.active_project_owner_transition_generation);
    if (!dluma_configure_runtime_paths()) {
        dluma_atomic_exchange(&g_dluma.started, 0);
        return 0;
    }
    dluma_log_line("INFO", "DLUMA " DLUMA_VERSION " starting.");
    dluma_log_line("INFO", "Capabilities: isolated Lua 5.4 states, main-thread lifecycle dispatch, Workshop discovery, legacy .dlscript lifecycle, and fingerprint-gated native bindings.");
    dluma_log_line("INFO", "The game-thread timer is the sole active-level query owner; the worker consumes stable snapshots.");
    g_dluma.stop_event = CreateEventW(0, TRUE, FALSE, 0);
    if (!g_dluma.stop_event) {
        dluma_log_hex("ERROR", "CreateEventW for lifecycle stop failed",
                    GetLastError());
        dluma_atomic_exchange(&g_dluma.lifecycle_state, DLUMA_LIFECYCLE_FINALIZED);
        dluma_atomic_exchange(&g_dluma.started, 0);
        return 0;
    }
    if (!dluma_lua_runtime_start())
        dluma_log_line("WARN", "Lua execution is unavailable for this session; loader forwarding and metadata discovery continue.");
    if (!dluma_shutdown_hooks_start())
        dluma_log_line("WARN", "Orderly menu-quit hook unavailable; the loader will still use the window-close path when possible.");
    dlw_nightly_initialize(dluma_read_ini_u32(L"loader", L"nightly_updates_enabled", 0) != 0);
    ui_started = dluma_ui_start();
    if (!ui_started)
        dluma_log_line("WARN", "Game-thread UI timer could not be installed; lifecycle discovery will continue.");
    if (!dluma_analysis_bridge_start(ui_started))
        dluma_log_line(
            "WARN", "Analysis bridge was enabled but refused startup; loader operation continues.");
    if (!dluma_discord_presence_start())
        dluma_log_line(
            "WARN",
            "Discord Rich Presence controller could not start; loader operation continues.");
    if (!dluma_steam_presence_start())
        dluma_log_line(
            "WARN",
            "Steam Rich Presence controller could not start; Discord and loader operation continue.");
    if (!dluma_prepare_editor_playtest_handoff(
            &playtest_activation))
        dluma_log_line(
            "ERROR",
            "The Editor playtest handoff context was rejected; project/map Lua and custom quest-phase sidecars remain disabled until an independently validated runtime owner is observed.");
    /* The discovery probe sorts bounded path/metadata snapshots on its worker stack.
     * Reserve a larger stack so a large subscribed Workshop library cannot overflow
     * the default 1 MiB thread stack while the production registry is still being
     * moved to heap-owned containers. */
    g_dluma.worker_thread = CreateThread(
        0, 8UL * 1024UL * 1024UL, dluma_worker_thread,
        playtest_activation, 0, &thread_id);
    if (!g_dluma.worker_thread) {
        if (playtest_activation) dluma_free(playtest_activation);
        dluma_log_hex("ERROR", "CreateThread failed", GetLastError());
        dluma_request_quiesce("worker creation failed");
        (void)dluma_host_stop();
        return 0;
    }
    return 1;
}

int dluma_host_stop(void)
{
    HANDLE worker;
    DWORD worker_result;
    if (dluma_atomic_compare_exchange(&g_dluma.started, 1, 1) == 0) return 1;
    if (dluma_atomic_compare_exchange(
            &g_dluma.retained_for_process_exit, 1, 1) != 0)
        return 0;
    dlw_nightly_shutdown();
    dluma_request_quiesce("ShutdownGameScriptDLL");
    dluma_analysis_bridge_quiesce();
    dluma_discord_presence_quiesce();
    dluma_steam_presence_quiesce();
    worker = g_dluma.worker_thread;
    if (worker) {
        worker_result = WaitForSingleObject(worker, 5000);
        if (worker_result != WAIT_OBJECT_0) {
            dluma_host_retain_for_process_exit(
                "lifecycle worker did not positively exit");
            return 0;
        }
        CloseHandle(worker);
        g_dluma.worker_thread = 0;
    }
    dluma_discovery_watch_shutdown();
    if (!dluma_analysis_bridge_stop()) {
        dluma_host_retain_for_process_exit(
            "analysis bridge worker did not positively exit");
        return 0;
    }
    dluma_lua_discovery_shutdown();
    dluma_discord_presence_stop();
    if (!dluma_discord_presence_stop_succeeded()) {
        dluma_host_retain_for_process_exit(
            "Discord callback controller did not drain");
        return 0;
    }
    dluma_steam_presence_stop();
    dluma_lua_runtime_stop();
    if (!dluma_lua_runtime_stop_succeeded()) {
        dluma_host_retain_for_process_exit(
            "Lua/native callback graph did not drain");
        return 0;
    }
    dluma_ui_stop();
    if (!dluma_ui_stop_succeeded()) {
        dluma_host_retain_for_process_exit("UI callback graph did not drain");
        return 0;
    }
    dluma_workshop_menu_shutdown();
    if (!dluma_engine_unbind(&g_dluma.engine)) {
        dluma_host_retain_for_process_exit("engine callback graph did not drain");
        return 0;
    }
    if (!dluma_shutdown_hooks_stop()) {
        dluma_host_retain_for_process_exit(
            "orderly-exit callback graph did not drain");
        return 0;
    }
    if (g_dluma.stop_event) {
        CloseHandle(g_dluma.stop_event);
        g_dluma.stop_event = 0;
    }
    dluma_atomic_exchange(&g_dluma.started, 0);
    dluma_atomic_exchange(&g_dluma.lifecycle_state, DLUMA_LIFECYCLE_FINALIZED);
    return 1;
}
