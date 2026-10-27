#include "dlw_editor_host.h"
#include "dlw_analysis_bridge.h"
#include "dlw_animation_refresh_runtime.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_editor_animation_bridge.h"
#include "dlw_editor_quest_graph_bridge.h"
#include "dlw_editor_exit_observer.h"
#include "dlw_editor_presence.h"
#include "dlw_discord_presence.h"
#include "dlw_editor_launch.h"
#include "dlw_editor_missing_asset_probe_fix.h"
#include "dlw_log.h"
#include "dlw_lua_discovery_internal.h"
#include "dlw_native_mods.h"
#include "dlw_quest_editor.h"

#define DLUMA_EDITOR_CALLBACK_THREAD_CAPACITY 256U

typedef struct DlumaEditorCallbackThread {
    DWORD thread_id;
    unsigned int depth;
} DlumaEditorCallbackThread;

typedef struct DlumaEditorHostState {
    volatile LONG start_state;
    volatile LONG session_active;
    volatile LONG callbacks_in_flight;
    volatile LONG untracked_callbacks_in_flight;
    volatile LONG callback_thread_lock;
    HANDLE animation_refresh_stop_event;
    HANDLE animation_refresh_worker_thread;
    ULONGLONG session_generation;
    ULONGLONG last_session_generation;
    DlumaEditorCallbackThread callback_threads[
        DLUMA_EDITOR_CALLBACK_THREAD_CAPACITY];
} DlumaEditorHostState;

static DlumaEditorHostState g_dluma_editor_host;

void dluma_editor_host_crash_context_update(
    const char* phase,
    ULONGLONG current_generation,
    ULONGLONG last_generation)
{
    const DlumaDualHostRuntimeSnapshot* runtime =
        dluma_dual_host_runtime_snapshot();
    const DlumaHostClassification* classification = runtime
        ? &runtime->classification : 0;
    dluma_crash_publish_host_context(
        "chrome_editor",
        classification ? classification->exact_profile != 0U : 0,
        classification ? classification->reason : "runtime_unavailable",
        phase,
        current_generation,
        last_generation);
    dluma_crash_reporting_refresh();
}

static void dluma_editor_host_callback_lock(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma_editor_host.callback_thread_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_editor_host_callback_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_editor_host.callback_thread_lock, 0);
}

int dluma_editor_host_callback_enter(void)
{
    DWORD thread_id = GetCurrentThreadId();
    unsigned int index;
    unsigned int free_index = DLUMA_EDITOR_CALLBACK_THREAD_CAPACITY;
    int tracked = 0;
    dluma_editor_host_callback_lock();
    for (index = 0; index < DLUMA_EDITOR_CALLBACK_THREAD_CAPACITY; ++index) {
        DlumaEditorCallbackThread* slot =
            &g_dluma_editor_host.callback_threads[index];
        if (slot->thread_id == thread_id && slot->depth) {
            if (slot->depth != 0xFFFFFFFFU) {
                ++slot->depth;
                tracked = 1;
            }
            break;
        }
        if (!slot->thread_id &&
            free_index == DLUMA_EDITOR_CALLBACK_THREAD_CAPACITY)
            free_index = index;
    }
    if (!tracked && index == DLUMA_EDITOR_CALLBACK_THREAD_CAPACITY &&
        free_index < DLUMA_EDITOR_CALLBACK_THREAD_CAPACITY) {
        g_dluma_editor_host.callback_threads[free_index].thread_id = thread_id;
        g_dluma_editor_host.callback_threads[free_index].depth = 1U;
        tracked = 1;
    }
    if (!tracked)
        dluma_atomic_increment(
            &g_dluma_editor_host.untracked_callbacks_in_flight);
    dluma_atomic_increment(&g_dluma_editor_host.callbacks_in_flight);
    dluma_editor_host_callback_unlock();
    return tracked;
}

void dluma_editor_host_callback_leave(int thread_tracked)
{
    DWORD thread_id = GetCurrentThreadId();
    unsigned int index;
    dluma_editor_host_callback_lock();
    if (thread_tracked) {
        for (index = 0; index < DLUMA_EDITOR_CALLBACK_THREAD_CAPACITY; ++index) {
            DlumaEditorCallbackThread* slot =
                &g_dluma_editor_host.callback_threads[index];
            if (slot->thread_id != thread_id || !slot->depth) continue;
            --slot->depth;
            if (!slot->depth) slot->thread_id = 0;
            break;
        }
    } else if (dluma_atomic_compare_exchange(
                   &g_dluma_editor_host.untracked_callbacks_in_flight,
                   0, 0) > 0) {
        dluma_atomic_decrement(
            &g_dluma_editor_host.untracked_callbacks_in_flight);
    }
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_host.callbacks_in_flight, 0, 0) > 0)
        dluma_atomic_decrement(&g_dluma_editor_host.callbacks_in_flight);
    dluma_editor_host_callback_unlock();
}

int dluma_editor_host_current_thread_in_callback(void)
{
    DWORD thread_id = GetCurrentThreadId();
    unsigned int index;
    int active = 0;
    dluma_editor_host_callback_lock();
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_host.untracked_callbacks_in_flight, 0, 0) > 0) {
        active = 1;
    } else {
        for (index = 0; index < DLUMA_EDITOR_CALLBACK_THREAD_CAPACITY; ++index)
            if (g_dluma_editor_host.callback_threads[index].thread_id ==
                    thread_id &&
                g_dluma_editor_host.callback_threads[index].depth) {
                active = 1;
                break;
            }
    }
    dluma_editor_host_callback_unlock();
    return active;
}

static DWORD WINAPI dluma_editor_animation_refresh_worker(LPVOID context)
{
    HANDLE stop_event = (HANDLE)context;
    for (;;) {
        DWORD wait_result = WaitForSingleObject(stop_event, 500U);
        WCHAR project_root[DLUMA_MAX_WPATH];
        char map_name[DLUMA_MAX_MAP_NAME];
        if (wait_result == WAIT_OBJECT_0) break;
        if (wait_result != WAIT_TIMEOUT) {
            dluma_log_hex(
                "WARN", "Editor animation refresh watcher wait failed",
                GetLastError());
            break;
        }
        (void)dluma_editor_animation_bridge_start();
        (void)dluma_editor_quest_graph_bridge_start();
        project_root[0] = 0;
        map_name[0] = 0;
        if (dluma_editor_launch_resolve_current_project(
                project_root, DLUMA_ARRAY_COUNT(project_root),
                map_name, DLUMA_ARRAY_COUNT(map_name))) {
            /* This watcher performs project-owned filesystem admission only.
             * Any future engine operation must marshal to an independently
             * proven Editor owner-thread dispatcher. */
            dluma_animation_refresh_runtime_poll_project(project_root);
        }
    }
    return 0;
}

static int dluma_editor_animation_refresh_watcher_start(void)
{
    DWORD thread_id = 0;
    if (g_dluma_editor_host.animation_refresh_worker_thread)
        return 1;
    g_dluma_editor_host.animation_refresh_stop_event =
        CreateEventW(0, TRUE, FALSE, 0);
    if (!g_dluma_editor_host.animation_refresh_stop_event) return 0;
    g_dluma_editor_host.animation_refresh_worker_thread = CreateThread(
        0, 0, dluma_editor_animation_refresh_worker,
        g_dluma_editor_host.animation_refresh_stop_event, 0, &thread_id);
    if (!g_dluma_editor_host.animation_refresh_worker_thread) {
        CloseHandle(g_dluma_editor_host.animation_refresh_stop_event);
        g_dluma_editor_host.animation_refresh_stop_event = 0;
        return 0;
    }
    return 1;
}

static int dluma_editor_animation_refresh_watcher_stop(void)
{
    DWORD wait_result;
    if (!g_dluma_editor_host.animation_refresh_worker_thread) return 1;
    if (!g_dluma_editor_host.animation_refresh_stop_event ||
        !SetEvent(g_dluma_editor_host.animation_refresh_stop_event))
        return 0;
    wait_result = WaitForSingleObject(
        g_dluma_editor_host.animation_refresh_worker_thread, 2000U);
    if (wait_result != WAIT_OBJECT_0) return 0;
    CloseHandle(g_dluma_editor_host.animation_refresh_worker_thread);
    CloseHandle(g_dluma_editor_host.animation_refresh_stop_event);
    g_dluma_editor_host.animation_refresh_worker_thread = 0;
    g_dluma_editor_host.animation_refresh_stop_event = 0;
    return 1;
}
int dluma_editor_host_start(void)
{
    LONG state = dluma_atomic_compare_exchange(
        &g_dluma_editor_host.start_state, 1, 0);
    if (state != 0)
        return dluma_atomic_compare_exchange(
                   &g_dluma_editor_host.start_state, 0, 0) == 2;
    if (!dluma_dual_host_runtime_is_editor() ||
        !dluma_dual_host_runtime_pin_editor_module()) {
        dluma_atomic_exchange(&g_dluma_editor_host.start_state, -1);
        return 0;
    }
    dluma_load_config();
    dluma_editor_host_crash_context_update("host_start", 0, 0);
    if (!dluma_crash_reporting_start() ||
        !dluma_crash_reporting_start_editor_filter()) {
        dluma_log_line(
            "WARN",
            "Editor crash diagnostics were unavailable; Editor initialization will continue.");
    }
    if (!dluma_editor_exit_observer_start())
        dluma_log_line(
            "WARN",
            "Editor exit observer was unavailable; crash dialogs will remain fail-open during shutdown.");
    if (!dluma_discord_presence_start())
        dluma_log_line("WARN", "Editor Discord Rich Presence controller could not start.");
    if (!dluma_editor_presence_start())
        dluma_log_line("WARN", "Editor document presence observer could not start.");
    (void)dluma_editor_missing_asset_probe_fix_start();
    if (!dluma_editor_animation_bridge_start())
        dluma_log_line(
            "WARN",
            "Selected-model animation reload refused this Editor build; stock commands remain chained except intercepted reload requests.");
    if (!dluma_editor_quest_graph_bridge_start())
        dluma_log_line(
            "WARN",
            "Visual PXSL quest bridge is unavailable; stock Quest Editor behavior remains unchanged.");
    if (!dluma_analysis_bridge_start(0))
        dluma_log_line(
            "WARN", "Editor analysis bridge was enabled but refused startup.");
    dluma_discover_workshop_roots();
    {
        WCHAR project_root[DLUMA_MAX_WPATH];
        char map_name[DLUMA_MAX_MAP_NAME];
        project_root[0] = 0;
        map_name[0] = 0;
        if (dluma_editor_launch_resolve_current_project(
                project_root, DLUMA_ARRAY_COUNT(project_root),
                map_name, DLUMA_ARRAY_COUNT(map_name)))
            dluma_animation_refresh_runtime_poll_project(project_root);
    }
    /* Editor discovery parses immutable Lua package metadata only. It does
     * not create a Lua state or start any Player runtime subsystem. */
    dluma_lua_discover_globals_mode(DLUMA_LUA_DISCOVERY_FULL_STARTUP);
    dluma_lua_discover_editor_quest_phase_metadata();
    dluma_native_mods_discover();
    if (!dluma_native_mods_editor_load())
        dluma_log_line(
            "WARN",
            "One or more approved Editor MODs were unavailable; other Editor features remain active.");
    if (!dluma_editor_launch_hook_start())
        dluma_log_line(
            "WARN",
            "Editor playtest handoff hook is unavailable; Player children will remain forwarding-only.");
    if (!dluma_quest_editor_start())
        dluma_log_line(
            "WARN",
            "QuestEditor integration is unavailable; stock quest compilation remains active.");
    /* Compiler/process hooks and editor-capable native MODs are installed by
     * their own exact-profile adapters.  This process host owns those adapters
     * for the rest of Chrome Editor's lifetime and deliberately starts none of
     * the Player UI, gameplay, audio, Lua, or managed subsystems. Editor
     * presence follows process lifetime, independently of map sessions. */
    dluma_log_pair("INFO", "editor_host_player_systems_started", "false");
    dluma_atomic_exchange(&g_dluma_editor_host.start_state, 2);
    return 1;
}

int dluma_editor_host_session_begin(ULONGLONG generation)
{
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_host.start_state, 0, 0) != 2 ||
        !generation ||
        dluma_atomic_compare_exchange(
            &g_dluma_editor_host.session_active, 1, 0) != 0)
        return 0;
    g_dluma_editor_host.session_generation = generation;
    dluma_editor_host_crash_context_update(
        "session_initializing", generation,
        g_dluma_editor_host.last_session_generation);
    dluma_log_u64("INFO", "editor_session_generation_begin", generation);
    {
        WCHAR project_root[DLUMA_MAX_WPATH];
        char map_name[DLUMA_MAX_MAP_NAME];
        project_root[0] = 0;
        map_name[0] = 0;
        if (dluma_editor_launch_resolve_current_project(
                project_root, DLUMA_ARRAY_COUNT(project_root),
                map_name, DLUMA_ARRAY_COUNT(map_name)))
            dluma_animation_refresh_runtime_poll_project(project_root);
    }
    if (!dluma_editor_animation_refresh_watcher_start())
        dluma_log_line(
            "WARN",
            "Editor animation refresh watcher was unavailable; requests will be checked at lifecycle boundaries only.");
    (void)dluma_editor_quest_graph_bridge_start();
    if (!dluma_native_mods_editor_session_begin(generation))
        dluma_log_line("WARN", "One or more Editor MOD session-begin callbacks failed.");
    dluma_editor_host_crash_context_update(
        "session_active", generation,
        g_dluma_editor_host.last_session_generation);
    return 1;
}

int dluma_editor_host_session_end(ULONGLONG generation)
{
    ULONGLONG started;
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_host.start_state, 0, 0) != 2)
        return 1;
    if (!generation) return 0;
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_host.session_active, 0, 0) == 0)
        return dluma_atomic_compare_exchange(
                   &g_dluma_editor_host.callbacks_in_flight, 0, 0) == 0;
    if (g_dluma_editor_host.session_generation != generation ||
        dluma_atomic_compare_exchange(
            &g_dluma_editor_host.session_active, 0, 1) != 1)
        return 0;
    dluma_editor_host_crash_context_update(
        "session_quiescing", generation,
        g_dluma_editor_host.last_session_generation);
    if (!dluma_editor_animation_refresh_watcher_stop()) {
        dluma_atomic_exchange(&g_dluma_editor_host.session_active, 1);
        dluma_editor_host_crash_context_update(
            "session_shutdown_failed", generation,
            g_dluma_editor_host.last_session_generation);
        dluma_log_pair(
            "ERROR", "editor_animation_refresh_watcher",
            "timed_out_retain_graph");
        return 0;
    }
    if (!dluma_editor_quest_graph_bridge_stop()) {
        dluma_atomic_exchange(&g_dluma_editor_host.session_active, 1);
        dluma_editor_host_crash_context_update(
            "session_shutdown_failed", generation,
            g_dluma_editor_host.last_session_generation);
        dluma_log_pair(
            "ERROR", "editor_quest_graph_bridge",
            "timed_out_retain_graph");
        return 0;
    }
    if (!dluma_native_mods_editor_session_end(generation))
        dluma_log_line("WARN", "One or more Editor MOD session-end callbacks failed.");
    started = GetTickCount64();
    while ((dluma_atomic_compare_exchange(
                &g_dluma_editor_host.callbacks_in_flight, 0, 0) != 0 ||
            !dluma_native_mods_editor_callbacks_drained()) &&
           GetTickCount64() - started < 2000ULL)
        Sleep(1);
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_host.callbacks_in_flight, 0, 0) != 0 ||
        !dluma_native_mods_editor_callbacks_drained()) {
        dluma_atomic_exchange(&g_dluma_editor_host.session_active, 1);
        dluma_editor_host_crash_context_update(
            "session_shutdown_failed", generation,
            g_dluma_editor_host.last_session_generation);
        dluma_log_pair("ERROR", "editor_session_drain", "timed_out_retain_graph");
        return 0;
    }
    g_dluma_editor_host.last_session_generation = generation;
    g_dluma_editor_host.session_generation = 0;
    dluma_editor_host_crash_context_update(
        "session_ended", 0, generation);
    dluma_log_u64("INFO", "editor_session_generation_end", generation);
    return 1;
}

int dluma_editor_host_callbacks_drained(void)
{
    return dluma_atomic_compare_exchange(
               &g_dluma_editor_host.callbacks_in_flight, 0, 0) == 0 &&
        (!g_dluma_editor_host.animation_refresh_worker_thread ||
         WaitForSingleObject(
             g_dluma_editor_host.animation_refresh_worker_thread, 0) ==
             WAIT_OBJECT_0) &&
        dluma_editor_launch_hook_callbacks_drained() &&
        dluma_editor_quest_graph_bridge_callbacks_drained() &&
        dluma_quest_editor_callbacks_drained() &&
        dluma_native_mods_editor_callbacks_drained();
}
