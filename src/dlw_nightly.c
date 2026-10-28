#include "dlw_common.h"
#include "dlw_build_identity.h"
#include "dlw_nightly.h"

typedef struct NightlyStartup {
    DWORD size; LPWSTR reserved, desktop, title; DWORD x, y, width, height;
    DWORD x_chars, y_chars, fill, flags; WORD show, reserved_size;
    BYTE* reserved_data; HANDLE input, output, error;
} NightlyStartup;
typedef struct NightlyProcess { HANDLE process, thread; DWORD pid, tid; } NightlyProcess;
__declspec(dllimport) BOOL WINAPI CreateProcessW(LPCWSTR, LPWSTR, LPVOID, LPVOID, BOOL, DWORD, LPVOID, LPCWSTR, NightlyStartup*, NightlyProcess*);
__declspec(dllimport) BOOL WINAPI GetExitCodeProcess(HANDLE, DWORD*);

static struct {
    WCHAR module[DLUMA_MAX_WPATH], root[DLUMA_MAX_WPATH], result[DLUMA_MAX_WPATH];
    WCHAR helper[DLUMA_MAX_WPATH], digest[65];
    HANDLE process; int initialized, enabled, pending_enabled, started, action;
    DlwNightlyOffer offer;
} state;

static void append_number(WCHAR* dst, unsigned int cap, ULONGLONG value)
{
    WCHAR reversed[32], number[32]; unsigned int n = 0, i;
    do { reversed[n++] = (WCHAR)(L'0' + value % 10); value /= 10; } while (value && n < 31);
    for (i = 0; i < n; ++i) number[i] = reversed[n - i - 1];
    number[n] = 0; dluma_wstr_append(dst, cap, number);
}
static void argument(WCHAR* cmd, unsigned int cap, const WCHAR* value)
{
    if (cmd[0]) dluma_wstr_append(cmd, cap, L" ");
    dluma_wstr_append(cmd, cap, L"\""); dluma_wstr_append(cmd, cap, value); dluma_wstr_append(cmd, cap, L"\"");
}
static HANDLE launch(const WCHAR* exe, WCHAR* cmd)
{
    NightlyStartup startup; NightlyProcess process;
    memset(&startup, 0, sizeof(startup)); memset(&process, 0, sizeof(process));
    startup.size = sizeof(startup);
    if (!CreateProcessW(exe, cmd, 0, 0, FALSE, 0x08000000, 0, 0, &startup, &process)) return 0;
    CloseHandle(process.thread); return process.process;
}
static void probe(void)
{
    WCHAR cmd[DLUMA_MAX_WPATH * 5], exe[DLUMA_MAX_WPATH], directory[DLUMA_MAX_WPATH]; unsigned int i;
    if (state.process || !state.initialized) return;
    dluma_wstr_copy(directory, DLUMA_ARRAY_COUNT(directory), state.module);
    i = 0; while (directory[i]) ++i;
    while (i && directory[i - 1] != L'\\' && directory[i - 1] != L'/') --i;
    if (!i) { state.offer.kind = DLW_NIGHTLY_ERROR; return; }
    directory[i - 1] = 0;
    dluma_path_join(exe, DLUMA_ARRAY_COUNT(exe), directory, L"dluma_nightly_updater.exe");
    cmd[0] = 0; argument(cmd, DLUMA_ARRAY_COUNT(cmd), exe);
    dluma_wstr_append(cmd, DLUMA_ARRAY_COUNT(cmd), L" probe");
    argument(cmd, DLUMA_ARRAY_COUNT(cmd), state.module); argument(cmd, DLUMA_ARRAY_COUNT(cmd), state.root);
    argument(cmd, DLUMA_ARRAY_COUNT(cmd), state.result);
    dluma_wstr_append(cmd, DLUMA_ARRAY_COUNT(cmd), state.enabled ? L" 1" : L" 0");
    DeleteFileW(state.result); state.pending_enabled = state.enabled;
    state.process = launch(exe, cmd); state.started = 1;
    if (state.process) dluma_log_line("INFO", "Nightly update check started.");
    if (!state.process && state.enabled) { dluma_log_hex("ERROR", "Nightly update check could not start", GetLastError()); state.offer.kind = DLW_NIGHTLY_ERROR; }
}
void dlw_nightly_initialize(int enabled)
{
    WCHAR local[DLUMA_MAX_WPATH]; char path_utf8[DLUMA_MAX_WPATH * 3]; DWORD n;
    if (state.initialized) return;
    memset(&state, 0, sizeof(state));
    state.initialized = 1; state.enabled = !!enabled;
    dluma_log_pair("INFO", "nightly_updates_enabled", state.enabled ? "1" : "0");
    n = GetModuleFileNameW(g_dluma.self_module, state.module, DLUMA_ARRAY_COUNT(state.module));
    if (!n || n >= DLUMA_ARRAY_COUNT(state.module)) { dluma_log_hex("ERROR", "Nightly module path unavailable", GetLastError()); state.offer.kind = DLW_NIGHTLY_ERROR; return; }
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, DLUMA_ARRAY_COUNT(local));
    if (!n || n >= DLUMA_ARRAY_COUNT(local)) { dluma_log_hex("ERROR", "Nightly data directory unavailable", GetLastError()); state.offer.kind = DLW_NIGHTLY_ERROR; return; }
    dluma_path_join(state.root, DLUMA_ARRAY_COUNT(state.root), local, L"DLUMA-nightly");
    if (dluma_wide_to_utf8(state.root, path_utf8, DLUMA_ARRAY_COUNT(path_utf8))) dluma_log_pair("INFO", "nightly_data_path", path_utf8);
    dluma_wstr_copy(state.result, DLUMA_ARRAY_COUNT(state.result), local);
    dluma_wstr_append(state.result, DLUMA_ARRAY_COUNT(state.result), L"\\DLUMA-nightly-offer-");
    append_number(state.result, DLUMA_ARRAY_COUNT(state.result), GetCurrentProcessId());
    dluma_wstr_append(state.result, DLUMA_ARRAY_COUNT(state.result), L".ini");
    probe();
}
void dlw_nightly_set_enabled(int enabled)
{
    enabled = !!enabled;
    if (!state.initialized) { dlw_nightly_initialize(enabled); return; }
    if (state.enabled == enabled) return;
    state.enabled = enabled; state.offer.kind = DLW_NIGHTLY_NONE; state.started = 0;
    if (!state.process) probe();
}
const DlwNightlyOffer* dlw_nightly_get_offer(void)
{
    DWORD code; WCHAR kind[16], commit[41]; unsigned int i;
    if (state.process && GetExitCodeProcess(state.process, &code) && code != 259) {
        CloseHandle(state.process); state.process = 0;
        dluma_log_hex("INFO", "Nightly helper exit code", code);
        if (state.action) {
            state.action = 0;
            if (code != 0) { dluma_log_hex("ERROR", "Nightly updater failed", code); state.offer.kind = DLW_NIGHTLY_ERROR; }
            if (state.pending_enabled != state.enabled) { state.started = 0; probe(); }
            return &state.offer;
        }
        if (state.pending_enabled != state.enabled) { state.started = 0; probe(); return &state.offer; }
        if (code != 0 && state.enabled) { dluma_log_hex("ERROR", "Nightly update check failed", code); state.offer.kind = DLW_NIGHTLY_ERROR; }
        else if (code == 0) {
            GetPrivateProfileStringW(L"offer", L"kind", L"", kind, DLUMA_ARRAY_COUNT(kind), state.result);
            GetPrivateProfileStringW(L"offer", L"helper", L"", state.helper, DLUMA_ARRAY_COUNT(state.helper), state.result);
            GetPrivateProfileStringW(L"offer", L"digest", L"", state.digest, DLUMA_ARRAY_COUNT(state.digest), state.result);
            GetPrivateProfileStringW(L"offer", L"commit", L"", commit, DLUMA_ARRAY_COUNT(commit), state.result);
            for (i = 0; i < 40 && commit[i]; ++i) state.offer.commit[i] = (char)commit[i];
            state.offer.commit[i] = 0;
            if (!dluma_wstricmp(kind, L"update") && dluma_stricmp(state.offer.commit, dluma_build_identity_git_head())) state.offer.kind = DLW_NIGHTLY_UPDATE;
            else if (!dluma_wstricmp(kind, L"update")) state.offer.kind = DLW_NIGHTLY_NONE;
            else if (!dluma_wstricmp(kind, L"restore")) state.offer.kind = DLW_NIGHTLY_RESTORE;
            else if (dluma_wstricmp(kind, L"none")) state.offer.kind = DLW_NIGHTLY_ERROR;
        }
        DeleteFileW(state.result);
    }
    return &state.offer;
}
void dlw_nightly_respond(int accept)
{
    WCHAR cmd[DLUMA_MAX_WPATH * 5]; FILETIME created, exited, kernel, user; ULONGLONG ticks; HANDLE process;
    DlwNightlyKind kind = state.offer.kind; state.offer.kind = DLW_NIGHTLY_NONE;
    if (!accept || (kind != DLW_NIGHTLY_UPDATE && kind != DLW_NIGHTLY_RESTORE)) return;
    if (!GetProcessTimes((HANDLE)(LONG_PTR)-1, &created, &exited, &kernel, &user)) { state.offer.kind = DLW_NIGHTLY_ERROR; return; }
    ticks = (((ULONGLONG)created.dwHighDateTime << 32) | created.dwLowDateTime) + 504911232000000000ULL;
    cmd[0] = 0; argument(cmd, DLUMA_ARRAY_COUNT(cmd), state.helper);
    dluma_wstr_append(cmd, DLUMA_ARRAY_COUNT(cmd), kind == DLW_NIGHTLY_UPDATE ? L" update" : L" restore-stable");
    argument(cmd, DLUMA_ARRAY_COUNT(cmd), state.module);
    if (kind == DLW_NIGHTLY_UPDATE) argument(cmd, DLUMA_ARRAY_COUNT(cmd), state.digest);
    argument(cmd, DLUMA_ARRAY_COUNT(cmd), state.root);
    dluma_wstr_append(cmd, DLUMA_ARRAY_COUNT(cmd), L" "); append_number(cmd, DLUMA_ARRAY_COUNT(cmd), GetCurrentProcessId());
    dluma_wstr_append(cmd, DLUMA_ARRAY_COUNT(cmd), L" "); append_number(cmd, DLUMA_ARRAY_COUNT(cmd), ticks);
    process = launch(state.helper, cmd);
    if (process) { state.process = process; state.action = 1; state.pending_enabled = state.enabled; }
    else { dluma_log_hex("ERROR", "Nightly updater could not start", GetLastError()); state.offer.kind = DLW_NIGHTLY_ERROR; }
}
void dlw_nightly_shutdown(void)
{
    if (state.process) { CloseHandle(state.process); state.process = 0; }
    state.initialized = 0;
}
