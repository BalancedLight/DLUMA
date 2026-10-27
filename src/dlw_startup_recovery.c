#include "dlw_common.h"
#include "dlw_native_mods.h"

/* This marker is intentionally loader-owned, tiny, and written before any
 * third-party MOD code runs. It detects an interrupted startup, not every
 * possible process exit; recovery therefore describes the prior startup
 * conservatively and never claims that a recorded DLL MOD caused a crash. */
typedef struct DlumaStartupRecoveryState {
    WCHAR path[DLUMA_MAX_WPATH];
    DlumaStartupRecoveryPrompt prompt;
    volatile LONG lock;
    unsigned char begun;
} DlumaStartupRecoveryState;

static DlumaStartupRecoveryState g_dluma_startup_recovery;

static void dluma_startup_recovery_lock(void)
{
    while (dluma_atomic_compare_exchange(&g_dluma_startup_recovery.lock, 1, 0) != 0)
        Sleep(1);
}

static void dluma_startup_recovery_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_startup_recovery.lock, 0);
}

static int dluma_startup_recovery_write_utf8(const WCHAR* key, const char* value)
{
    WCHAR wide[256];
    if (!key || !value || !dluma_utf8_to_wide(value, wide, DLUMA_ARRAY_COUNT(wide)))
        return 0;
    return WritePrivateProfileStringW(
        L"startup", key, wide, g_dluma_startup_recovery.path) != 0;
}

static int dluma_startup_recovery_write_marker_locked(
    const char* state,
    const char* kind,
    const char* id,
    const char* registry_key,
    const char* fingerprint,
    const char* phase)
{
    int written;
    if (!g_dluma_startup_recovery.path[0]) return 0;
    written = dluma_startup_recovery_write_utf8(L"state", state ? state : "") &&
        dluma_startup_recovery_write_utf8(L"suspect_kind", kind ? kind : "") &&
        dluma_startup_recovery_write_utf8(L"suspect_id", id ? id : "") &&
        dluma_startup_recovery_write_utf8(
            L"suspect_registry_key", registry_key ? registry_key : "") &&
        dluma_startup_recovery_write_utf8(
            L"suspect_fingerprint", fingerprint ? fingerprint : "") &&
        dluma_startup_recovery_write_utf8(L"phase", phase ? phase : "");
    /* WritePrivateProfileString(NULL,NULL,NULL,...) requests a cache flush, but
     * Windows is permitted to return zero even after every actual key write
     * succeeded. The uploaded runtime proved this exact case: the marker file
     * contained state=armed while the loader logged marker_arm_failed. Treat
     * the key writes as authoritative and make the flush best-effort only. */
    if (written)
        (void)WritePrivateProfileStringW(
            0, 0, 0, g_dluma_startup_recovery.path);
    return written;
}

static void dluma_startup_recovery_clear_prompt_locked(void)
{
    g_dluma_startup_recovery.prompt.pending = 0;
    g_dluma_startup_recovery.prompt.has_specific_native_suspect = 0;
    g_dluma_startup_recovery.prompt.suspected_native_id[0] = 0;
    g_dluma_startup_recovery.prompt.suspected_native_registry_key[0] = 0;
    g_dluma_startup_recovery.prompt.suspected_native_fingerprint[0] = 0;
}

static void dluma_startup_recovery_read_utf8(
    const WCHAR* key, char* out, unsigned int capacity)
{
    WCHAR wide[256];
    if (!out || !capacity) return;
    out[0] = 0;
    if (!key || !g_dluma_startup_recovery.path[0]) return;
    wide[0] = 0;
    GetPrivateProfileStringW(
        L"startup", key, L"", wide, DLUMA_ARRAY_COUNT(wide),
        g_dluma_startup_recovery.path);
    if (!dluma_wide_to_utf8(wide, out, capacity)) out[0] = 0;
}

void dluma_startup_recovery_begin(void)
{
    char previous_state[32];
    char previous_kind[32];
    char previous_id[128];
    char previous_key[DLUMA_SHA256_HEX_CAPACITY];
    char previous_fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    int previous_interrupted;
    int armed;

    dluma_startup_recovery_lock();
    if (g_dluma_startup_recovery.begun) {
        dluma_startup_recovery_unlock();
        return;
    }
    memset(&g_dluma_startup_recovery.prompt, 0,
           sizeof(g_dluma_startup_recovery.prompt));
    dluma_path_join(g_dluma_startup_recovery.path,
                  DLUMA_ARRAY_COUNT(g_dluma_startup_recovery.path),
                  g_dluma.module_dir, L"dluma_runtime/startup_recovery.ini");
    previous_state[0] = previous_kind[0] = previous_id[0] = 0;
    previous_key[0] = previous_fingerprint[0] = 0;
    dluma_startup_recovery_read_utf8(
        L"state", previous_state, DLUMA_ARRAY_COUNT(previous_state));
    dluma_startup_recovery_read_utf8(
        L"suspect_kind", previous_kind, DLUMA_ARRAY_COUNT(previous_kind));
    dluma_startup_recovery_read_utf8(
        L"suspect_id", previous_id, DLUMA_ARRAY_COUNT(previous_id));
    dluma_startup_recovery_read_utf8(
        L"suspect_registry_key", previous_key, DLUMA_ARRAY_COUNT(previous_key));
    dluma_startup_recovery_read_utf8(
        L"suspect_fingerprint", previous_fingerprint,
        DLUMA_ARRAY_COUNT(previous_fingerprint));
    previous_interrupted = dluma_stricmp(previous_state, "armed") == 0;
    if (previous_interrupted) {
        g_dluma_startup_recovery.prompt.pending = 1;
        g_dluma_startup_recovery.prompt.suppressing_mods = 1;
        if (dluma_stricmp(previous_kind, "native") == 0 && previous_id[0] &&
            previous_key[0] && previous_fingerprint[0]) {
            g_dluma_startup_recovery.prompt.has_specific_native_suspect = 1;
            dluma_str_copy(g_dluma_startup_recovery.prompt.suspected_native_id,
                         DLUMA_ARRAY_COUNT(g_dluma_startup_recovery.prompt.
                                             suspected_native_id),
                         previous_id);
            dluma_str_copy(
                g_dluma_startup_recovery.prompt.suspected_native_registry_key,
                DLUMA_ARRAY_COUNT(g_dluma_startup_recovery.prompt.
                                    suspected_native_registry_key),
                previous_key);
            dluma_str_copy(
                g_dluma_startup_recovery.prompt.suspected_native_fingerprint,
                DLUMA_ARRAY_COUNT(g_dluma_startup_recovery.prompt.
                                    suspected_native_fingerprint),
                previous_fingerprint);
        }
    }
    g_dluma_startup_recovery.begun = 1;
    armed = dluma_startup_recovery_write_marker_locked(
        "armed", "none", "", "", "", "loader_initializing");
    if (!armed) {
        /* If this process cannot own a fresh marker, a stale prior file must
         * never suppress code or produce a repeating warning. Fail open and
         * leave a clear diagnostic instead. */
        dluma_startup_recovery_clear_prompt_locked();
        g_dluma_startup_recovery.prompt.suppressing_mods = 0;
    }
    dluma_startup_recovery_unlock();

    if (!armed) {
        dluma_log_line("ERROR", "startup_recovery_marker_arm_failed_recovery_disabled");
        return;
    }
    if (previous_interrupted) {
        dluma_log_line("WARN", "startup_recovery_previous_startup_incomplete");
        if (g_dluma_startup_recovery.prompt.has_specific_native_suspect)
            dluma_log_pair("WARN", "startup_recovery_native_suspect",
                         g_dluma_startup_recovery.prompt.suspected_native_id);
        else
            dluma_log_line("WARN", "startup_recovery_no_specific_mod_attribution");
    }
}

int dluma_startup_recovery_suppresses_mods(void)
{
    int suppressing;
    dluma_startup_recovery_lock();
    suppressing = g_dluma_startup_recovery.prompt.suppressing_mods != 0;
    dluma_startup_recovery_unlock();
    return suppressing;
}

void dluma_startup_recovery_note_native_mod(
    const char* id,
    const char* registry_key,
    const char* approval_fingerprint,
    const char* phase)
{
    dluma_startup_recovery_lock();
    if (g_dluma_startup_recovery.begun &&
        !g_dluma_startup_recovery.prompt.suppressing_mods &&
        !dluma_startup_recovery_write_marker_locked(
            "armed", "native", id, registry_key, approval_fingerprint, phase))
        dluma_log_line("ERROR", "startup_recovery_native_marker_write_failed");
    dluma_startup_recovery_unlock();
}

void dluma_startup_recovery_note_native_failure(
    const char* id,
    const char* registry_key,
    const char* approval_fingerprint)
{
    dluma_startup_recovery_lock();
    if (g_dluma_startup_recovery.begun) {
        g_dluma_startup_recovery.prompt.pending = 1;
        g_dluma_startup_recovery.prompt.has_specific_native_suspect = 1;
        dluma_str_copy(g_dluma_startup_recovery.prompt.suspected_native_id,
                     DLUMA_ARRAY_COUNT(g_dluma_startup_recovery.prompt.
                                         suspected_native_id),
                     id ? id : "");
        dluma_str_copy(
            g_dluma_startup_recovery.prompt.suspected_native_registry_key,
            DLUMA_ARRAY_COUNT(g_dluma_startup_recovery.prompt.
                                suspected_native_registry_key),
            registry_key ? registry_key : "");
        dluma_str_copy(g_dluma_startup_recovery.prompt.suspected_native_fingerprint,
                     DLUMA_ARRAY_COUNT(g_dluma_startup_recovery.prompt.
                                         suspected_native_fingerprint),
                     approval_fingerprint ? approval_fingerprint : "");
        if (!dluma_startup_recovery_write_marker_locked(
                "armed", "native", id, registry_key, approval_fingerprint,
                "native_loader_detected_failure"))
            dluma_log_line("ERROR", "startup_recovery_native_failure_marker_write_failed");
    }
    dluma_startup_recovery_unlock();
}

void dluma_startup_recovery_clear_native_mod(void)
{
    dluma_startup_recovery_lock();
    if (g_dluma_startup_recovery.begun &&
        !g_dluma_startup_recovery.prompt.suppressing_mods &&
        !dluma_startup_recovery_write_marker_locked(
            "armed", "none", "", "", "", "native_callback_complete"))
        dluma_log_line("ERROR", "startup_recovery_native_marker_clear_failed");
    dluma_startup_recovery_unlock();
}

int dluma_startup_recovery_snapshot_prompt(DlumaStartupRecoveryPrompt* out)
{
    int pending;
    if (!out) return 0;
    dluma_startup_recovery_lock();
    *out = g_dluma_startup_recovery.prompt;
    pending = out->pending != 0;
    dluma_startup_recovery_unlock();
    return pending;
}

/* This is deliberately independent of the frontend/game-thread UI. A prior
 * interrupted startup has already suppressed third-party code, so after the
 * caller completes raw DLL/Lua metadata discovery it is safe to ask before
 * the original game-script initializer can begin constructing the menu. */
int dluma_startup_recovery_prompt_before_game_init(void)
{
    DlumaStartupRecoveryPrompt prompt;
    WCHAR body[1024];
    WCHAR suspect[128];
    int response;
    int persisted = 0;
    int shown = 0;

    if (!dluma_startup_recovery_snapshot_prompt(&prompt)) return 0;
    if (!dluma_lua_initial_global_discovery_complete()) {
        dluma_log_line("WARN", "startup_recovery_early_prompt_deferred_global_discovery_incomplete");
        return 0;
    }

    body[0] = 0;
    suspect[0] = 0;
    if (prompt.has_specific_native_suspect &&
        dluma_utf8_to_wide(prompt.suspected_native_id, suspect,
                          DLUMA_ARRAY_COUNT(suspect))) {
        dluma_wstr_append(body, DLUMA_ARRAY_COUNT(body),
            L"The previous startup stopped before reaching the main menu while ");
        dluma_wstr_append(body, DLUMA_ARRAY_COUNT(body), suspect);
        dluma_wstr_append(body, DLUMA_ARRAY_COUNT(body),
            L" was inside a loader-controlled DLL MOD callback. This does not prove that the MOD caused the problem.\r\n\r\n");
        dluma_wstr_append(body, DLUMA_ARRAY_COUNT(body),
            L"All third-party MOD execution is suppressed for this recovery session. Disable only this DLL MOD for later launches?");
        response = MessageBoxW(
            0, body, L"DLUMA - Startup Recovery",
            MB_YESNO | MB_ICONWARNING | MB_SYSTEMMODAL | MB_DEFBUTTON2 |
                MB_SETFOREGROUND | MB_TOPMOST);
        shown = 1;
        if (response == IDYES) {
            persisted = dluma_native_mods_disable_exact_next_launch(
                prompt.suspected_native_registry_key,
                prompt.suspected_native_fingerprint);
            if (!persisted)
                dluma_log_line("ERROR", "startup_recovery_early_specific_native_disable_failed");
        } else if (response == IDNO) {
            persisted = 1;
            dluma_log_pair("INFO", "startup_recovery_early_specific_native_kept_enabled",
                         prompt.suspected_native_id);
        }
    } else {
        dluma_wstr_append(body, DLUMA_ARRAY_COUNT(body),
            L"The previous startup stopped before reaching the main menu. The loader could not safely attribute it to one MOD.\r\n\r\n");
        dluma_wstr_append(body, DLUMA_ARRAY_COUNT(body),
            L"All third-party MOD execution is suppressed for this recovery session. Disable every discovered DLL MOD and global Lua MOD for later launches?");
        response = MessageBoxW(
            0, body, L"DLUMA - Startup Recovery",
            MB_YESNO | MB_ICONWARNING | MB_SYSTEMMODAL | MB_DEFBUTTON2 |
                MB_SETFOREGROUND | MB_TOPMOST);
        shown = 1;
        if (response == IDYES) {
            int native_persisted = dluma_native_mods_disable_all_next_launch();
            int lua_persisted = dluma_lua_disable_all_globals_next_launch();
            persisted = native_persisted && lua_persisted;
            if (!persisted)
                dluma_log_line("ERROR", "startup_recovery_early_disable_all_persistence_failed");
        } else if (response == IDNO) {
            persisted = 1;
            dluma_log_line("INFO", "startup_recovery_early_mod_settings_kept_enabled");
        }
    }

    if (shown) {
        /* A modal recovery decision is one-shot for this process. Even when a
         * requested INI mutation fails, do not surprise the player with the
         * same warning again on the frontend timer a minute later. */
        (void)dluma_startup_recovery_complete_prompt();
        if (persisted)
            dluma_log_line("INFO", "startup_recovery_early_decision_persisted_restart_required");
        else
            dluma_log_line("WARN", "startup_recovery_early_decision_not_persisted_prompt_dismissed");
    }
    return shown;
}

int dluma_startup_recovery_complete_prompt(void)
{
    int begun;
    int had_pending;
    int runtime_started;
    int marker_written = 0;
    const char *state;
    const char *phase;
    dluma_startup_recovery_lock();
    begun = g_dluma_startup_recovery.begun != 0;
    had_pending = g_dluma_startup_recovery.prompt.pending != 0;
    runtime_started = dluma_atomic_compare_exchange(&g_dluma.started, 0, 0) != 0;
    /* Dismissing a prior-startup warning before the current runtime is ready
     * must not disarm detection for the current startup.  Keep this launch
     * armed until dluma_host_start succeeds.  A frontend fallback prompt runs
     * after the host is live and can resolve the current marker immediately. */
    state = had_pending && !runtime_started ? "armed" : "resolved";
    phase = had_pending
        ? (runtime_started
               ? "startup_recovery_prompt_complete_after_startup"
               : "startup_recovery_prompt_complete_startup_continues")
        : "loader_startup_complete";
    if (begun)
        marker_written = dluma_startup_recovery_write_marker_locked(
            state, "none", "", "", "", phase);
    /* In-memory completion is authoritative for one process. A failed disk
     * write is logged, but must never create a second modal from the fallback
     * UI path during the same session. */
    dluma_startup_recovery_clear_prompt_locked();
    dluma_startup_recovery_unlock();
    if (begun && !marker_written)
        dluma_log_line("ERROR", "startup_recovery_marker_transition_failed");
    return begun;
}
