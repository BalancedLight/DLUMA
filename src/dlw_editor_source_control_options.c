#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "dlw_editor_source_control_options.h"
#include "dlw_editor_source_control.h"

static struct {
    WCHAR config[32768];
    DWORD thread;
    void *editor;
    int started, enabled, init_returned, loaded_disabled;
} git_options;

int dluma_editor_source_control_options_start(const WCHAR *config_path)
{
    WCHAR selected[16];
    if (git_options.started || !config_path || !*config_path ||
        lstrlenW(config_path) >= (int)(sizeof(git_options.config) / sizeof(WCHAR))) return 0;
    lstrcpyW(git_options.config, config_path);
    git_options.thread = GetCurrentThreadId();
    GetPrivateProfileStringW(L"loader", L"editor_source_control", L"none",
        selected, (DWORD)(sizeof(selected) / sizeof(WCHAR)), config_path);
    git_options.enabled = lstrcmpiW(selected, L"git") == 0;
    git_options.started = 1;
    return 1;
}

void dluma_editor_source_control_options_stop(void)
{
    git_options.started = 0;
    git_options.init_returned = git_options.loaded_disabled = 0;
}

void dluma_editor_source_control_options_loaded(void *editor, DWORD owner_thread,
    unsigned int legacy_enabled, void *context)
{
    (void)context;
    if (!git_options.started || owner_thread != git_options.thread ||
        owner_thread != GetCurrentThreadId() || legacy_enabled > 1U) return;
    git_options.editor = editor;
    if (git_options.enabled && legacy_enabled)
        (void)WritePrivateProfileStringW(L"loader", L"editor_git_legacy_previous",
            L"enabled", git_options.config);
}

void dluma_editor_source_control_options_init_completed(void *editor,
    DWORD owner_thread, void *context)
{
    DlumaEditorSourceControlSnapshotV1 state;
    (void)context;
    if (!git_options.started || owner_thread != git_options.thread ||
        owner_thread != GetCurrentThreadId()) return;
    ZeroMemory(&state, sizeof(state)); state.size = sizeof(state);
    state.version = DLUMA_EDITOR_SOURCE_CONTROL_VERSION;
    if (!dluma_editor_source_control_native_snapshot(&state) || state.editor != editor ||
        state.owner_thread != owner_thread || !state.init_returned) return;
    git_options.editor = editor;
    git_options.init_returned = 1;
    git_options.loaded_disabled = state.legacy_enabled == 0U;
}

int dluma_editor_source_control_options_state(int *enabled, int *ready)
{
    DlumaEditorSourceControlSnapshotV1 state;
    int admitted = 0;
    if (enabled) *enabled = git_options.started && git_options.enabled;
    if (ready) *ready = 0;
    if (!git_options.started || GetCurrentThreadId() != git_options.thread) return 0;
    ZeroMemory(&state, sizeof(state)); state.size = sizeof(state);
    state.version = DLUMA_EDITOR_SOURCE_CONTROL_VERSION;
    if (git_options.init_returned && git_options.loaded_disabled &&
        dluma_editor_source_control_native_snapshot(&state) && state.init_returned &&
        state.editor == git_options.editor && state.owner_thread == git_options.thread &&
        !state.legacy_enabled) admitted = git_options.enabled;
    if (ready) *ready = admitted;
    return 1;
}

int dluma_editor_source_control_options_set_enabled(int enabled)
{
    if (!git_options.started || GetCurrentThreadId() != git_options.thread) return 0;
    if (!WritePrivateProfileStringW(L"loader", L"editor_source_control",
            enabled ? L"git" : L"none", git_options.config)) return 0;
    git_options.enabled = enabled != 0;
    return 1;
}

const WCHAR *dluma_editor_source_control_options_message(void)
{
    int enabled = 0, ready = 0;
    if (!dluma_editor_source_control_options_state(&enabled, &ready))
        return L"Source control is unavailable.";
    if (!enabled) return L"Git source control is off.";
    if (!git_options.init_returned) return L"Waiting for the Editor to finish starting.";
    if (ready) return L"Git source control is ready.";
    return L"Turn off legacy source control in Misc, save Editor settings, and restart.";
}
