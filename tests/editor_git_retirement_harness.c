#include <stdio.h>
#include "../src/dlw_editor_git_session.c"
#include "../src/core/dlw_json_reader.c"
static LONG unbinds, binds, releases; static HANDLE gate;
void *dluma_editor_companion_runtime_acquire(const WCHAR *p, const char *v) { (void)p; (void)v; return NULL; }
int dluma_sha256_file_hex(const WCHAR *p, ULONGLONG n, char *h) { (void)p; (void)n; (void)h; return 0; }
int dluma_project_regular_file_is_contained(const WCHAR *p, const WCHAR *q) { (void)p; (void)q; return 0; }
int dluma_editor_panel_host_unbind(const DlumaEditorPanelRef *p) { (void)p; ++unbinds; return 1; }
int dluma_editor_panel_host_bind(const DlumaEditorPanelRef *p, HWND h, DWORD id, HANDLE c) { (void)p; (void)h; (void)id; (void)c; ++binds; return 1; }
uint64_t dluma_editor_panel_host_binding_generation(const DlumaEditorPanelRef *p) { (void)p; return 1; }
void dluma_editor_companion_runtime_release(void *p) { (void)p; ++releases; }
static DWORD WINAPI worker(void *p) { GitSession *s = p; WaitForSingleObject(gate, INFINITE); git_release(s); return 0; }
static DWORD WINAPI closer(void *p) { (void)p; return (DWORD)dluma_editor_git_session_close(); }
static DWORD close_other(void) { DWORD result = 99; HANDLE h = CreateThread(NULL, 0, closer, NULL, 0, NULL); if (!h) return 99; WaitForSingleObject(h, INFINITE); GetExitCodeThread(h, &result); CloseHandle(h); return result; }
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
    GitSession *s; LRESULT result; STARTUPINFOW si; PROCESS_INFORMATION pi, pi2; WCHAR exe[MAX_PATH], cmd[MAX_PATH + 16]; DWORD count, tries; JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s)); CHECK(s);
    s->references = 2; s->owner_thread = GetCurrentThreadId(); s->owner = (HWND)(ULONG_PTR)123; s->serial = 42;
    s->stop = CreateEventW(NULL, TRUE, FALSE, NULL); s->reply = CreateEventW(NULL, TRUE, FALSE, NULL); s->job = CreateJobObjectW(NULL, NULL);
    gate = CreateEventW(NULL, TRUE, FALSE, NULL); CHECK(gate); s->runtime_lease = (void *)1; InitializeSRWLock(&s->lock); CHECK(s->stop && s->reply && s->job);
    ZeroMemory(&limits, sizeof(limits)); limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    CHECK(SetInformationJobObject(s->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    InterlockedIncrement(&g_sessions); g_session = s; g_dispatch_message = WM_APP + 25;
    s->worker = CreateThread(NULL, 0, worker, s, 0, NULL); CHECK(s->worker);
    CHECK(close_other() == 0); CHECK(!unbinds && !releases);
    CHECK(dluma_editor_git_session_detach()); CHECK(dluma_editor_git_session_detach()); CHECK(unbinds == 1);
    CHECK(dluma_editor_git_session_dispatch(s->owner, g_dispatch_message, 0, 42, &result)); CHECK(result == -1 && !binds);
    s->pending = 1; s->pending_serial = 9;
    CHECK(dluma_editor_git_session_dispatch(s->owner, g_dispatch_message, 9, 42, &result));
    CHECK(s->pending == 0 && !binds && !result && WaitForSingleObject(s->reply, 0) == WAIT_OBJECT_0);
    ResetEvent(s->reply); s->pending = 3; s->pending_serial = 10;
    CHECK(dluma_editor_git_session_dispatch(s->owner, g_dispatch_message, 10, 42, &result));
    CHECK(s->pending == 3 && WaitForSingleObject(s->reply, 0) == WAIT_TIMEOUT && !binds);
    CHECK(close_other() == 0); CHECK(!releases);
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si); ZeroMemory(&pi, sizeof(pi));
    CHECK(GetModuleFileNameW(NULL, exe, MAX_PATH)); CHECK(swprintf_s(cmd, MAX_PATH + 16, L"\"%s\" child", exe) > 0);
    CHECK(CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &si, &pi));
    if (!AssignProcessToJobObject(s->job, pi.hProcess)) {
        (void)TerminateProcess(pi.hProcess, 1); (void)WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        CHECK(0 && "could not assign test child to job");
    }
    ZeroMemory(&pi2, sizeof(pi2)); CHECK(swprintf_s(cmd, MAX_PATH + 16, L"\"%s\" child", exe) > 0);
    CHECK(CreateProcessW(exe, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &si, &pi2));
    if (!AssignProcessToJobObject(s->job, pi2.hProcess)) {
        (void)TerminateProcess(pi2.hProcess, 1); (void)WaitForSingleObject(pi2.hProcess, 5000);
        CloseHandle(pi2.hThread); CloseHandle(pi2.hProcess);
        CHECK(0 && "could not assign second test child to job");
    }
    CHECK(git_active(s, &count) && count == 2);
    CHECK(TerminateProcess(pi2.hProcess, 0)); CHECK(WaitForSingleObject(pi2.hProcess, 5000) == WAIT_OBJECT_0);
    CloseHandle(pi2.hThread); CloseHandle(pi2.hProcess);
    for (tries = 0; tries < 100U; ++tries) { CHECK(git_active(s, &count)); if (count == 1U) break; Sleep(10); }
    CHECK(count == 1U);
    SetEvent(gate); CHECK(WaitForSingleObject(s->worker, 5000) == WAIT_OBJECT_0);
    CHECK(git_active(s, &count) && count == 1); CHECK(close_other() == 0); CHECK(!releases);
    CHECK(dluma_editor_git_session_abort()); CHECK(WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    CHECK(git_active(s, &count) && count == 0); CHECK(close_other() == 1);
    CHECK(releases == 1 && unbinds == 1 && dluma_editor_git_session_drained()); CHECK(close_other() == 1 && releases == 1);
    CloseHandle(gate); puts("PASS: detach, pending dispatch, worker and job retirement"); return 0;
}
