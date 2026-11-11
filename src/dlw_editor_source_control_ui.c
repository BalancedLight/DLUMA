#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "dlw_editor_source_control_ui.h"
#include "dlw_editor_git_session.h"
#include "dlw_editor_git_lease.inc"
#include "dlw_editor_git_guard.inc"
#include "dlw_editor_theme_profile.h"
#include "dlw_editor_git_policy.inc"
#include "dlw_editor_wiki.h"

extern int dluma_editor_is_main_window(HWND, DWORD);
extern void dluma_log_line(const char *, const char *);

#define GIT_COMMAND 0xD1A2U
#define GIT_TIMER 0xD1A3U
#define GIT_OPTIONS_BUTTON 0x6D30U
#define GIT_PATH_CAP 32768U

static struct {
    volatile LONG started, stopping, callbacks;
    volatile LONG thread;
    HWND main;
    HWND options_page, options_dialog, options_button;
    DlumaEditorDockRef dock;
    DlumaEditorPanelRef panel;
    WCHAR config[GIT_PATH_CAP], root[GIT_PATH_CAP];
    HANDLE project, executable, children[32];
#if defined(DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC) && DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC
    HANDLE diagnostic_project, diagnostic_git_dir;
    int diagnostic_requested;
#endif
    void *policy_editor;
    uint64_t generation;
    GitProjectLease lease;
    GitProjectArm arm;
    GitGuardState deferred;
    char last_arm_id[33];
    uint64_t last_arm_generation;
    ULONGLONG arm_started;
    uint64_t next_replay, guard_epoch, coverage_generation;
    DlumaEditorUiCoverageForeign coverage_foreign;
    int session, closing, guard_registered, guard_diagnostics, guard_events, readiness_events, busy_events, pending_events, coverage_events, aborting, replay_exhausted;
} git_ui;
static UINT git_stop_message, git_replay_message;

static int same_file(HANDLE, HANDLE);
static int git_policy_snapshot(void *expected_editor, void **observed_editor);
#if defined(DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC) && DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC
static int git_diagnostic_metadata_valid(int scan_contents);
#endif
/* Ordinary local writes require the exact supported Editor and an owner-thread
 * read of the effective native legacy-provider state. */
static int git_ordinary_local_supported(void)
{ return dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR); }
static int git_ordinary_local_qualified(void)
{ return git_ordinary_local_supported() && git_policy_snapshot(NULL, NULL); }
static int git_ordinary_mode_allowed(int qualified, int diagnostic_requested)
{ return qualified && !diagnostic_requested; }
static int git_ordinary_local_active(void)
{
#if defined(DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC) && DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC
    return git_ordinary_mode_allowed(git_ordinary_local_qualified(), git_ui.diagnostic_requested);
#else
    return git_ordinary_mode_allowed(git_ordinary_local_qualified(), 0);
#endif
}
static int git_ordinary_local_supported_mode(void)
{
#if defined(DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC) && DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC
    return git_ordinary_mode_allowed(git_ordinary_local_supported(), git_ui.diagnostic_requested);
#else
    return git_ordinary_mode_allowed(git_ordinary_local_supported(), 0);
#endif
}
static int git_mutations_qualified(void)
{
#if defined(DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC) && DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC
    if (git_ui.diagnostic_requested)
        return git_ui.diagnostic_project && same_file(git_ui.diagnostic_project, git_ui.project) &&
            git_diagnostic_metadata_valid(!git_ui.lease.active);
#endif
    return git_ordinary_local_active();
}
static int git_local_operation_permitted(const char *operation, int ordinary)
{
    return operation && (!lstrcmpA(operation, "stage") || !lstrcmpA(operation, "unstage") ||
                         !lstrcmpA(operation, "commit") ||
                         (ordinary && !lstrcmpA(operation, "createBranch")));
}
static int git_local_operation_allowed(const char *operation)
{ return git_local_operation_permitted(operation, git_ordinary_local_active()); }
#if defined(DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC) && DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC
static void git_diagnostic_close(void)
{
    if (git_ui.diagnostic_git_dir) CloseHandle(git_ui.diagnostic_git_dir);
    git_ui.diagnostic_git_dir = NULL;
    if (git_ui.diagnostic_project) CloseHandle(git_ui.diagnostic_project);
    git_ui.diagnostic_project = NULL;
    git_ui.diagnostic_requested = 0;
}
static HANDLE git_diagnostic_open_git_dir(const WCHAR *root)
{
    WCHAR *path;
    HANDLE directory;
    BY_HANDLE_FILE_INFORMATION info;
    size_t length;
    if (!root) return NULL;
    length = (size_t)lstrlenW(root);
    if (length + 6U >= GIT_PATH_CAP) return NULL;
    path = HeapAlloc(GetProcessHeap(), 0, GIT_PATH_CAP * sizeof(WCHAR));
    if (!path) return NULL;
    lstrcpyW(path, root); lstrcatW(path, L"\\.git");
    directory = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    HeapFree(GetProcessHeap(), 0, path);
    if (directory == INVALID_HANDLE_VALUE) return NULL;
    if (GetFileType(directory) != FILE_TYPE_DISK || !GetFileInformationByHandle(directory, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(directory); return NULL;
    }
    return directory;
}

/* A fresh diagnostic repository is deliberately bounded. Reject metadata
 * reparse points and Git's external metadata/alternate markers. */
static int git_diagnostic_scan_metadata(WCHAR *path, size_t length, unsigned int depth,
    unsigned int *count, unsigned int *required)
{
    WIN32_FIND_DATAW item;
    HANDLE search;
    DWORD error;
    int valid = 1;
    if (depth > 16U || length + 3U >= GIT_PATH_CAP) return 0;
    path[length] = L'\\'; path[length + 1U] = L'*'; path[length + 2U] = 0;
    search = FindFirstFileW(path, &item);
    path[length] = 0;
    if (search == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    do {
        size_t name_length;
        if (!lstrcmpW(item.cFileName, L".") || !lstrcmpW(item.cFileName, L"..")) continue;
        if (++*count > 4096U || (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !lstrcmpiW(item.cFileName, L"commondir") || !lstrcmpiW(item.cFileName, L"gitdir") ||
            !lstrcmpiW(item.cFileName, L"alternates") || !lstrcmpiW(item.cFileName, L"http-alternates")) {
            valid = 0; break;
        }
        if (depth == 0U) {
            if (!lstrcmpiW(item.cFileName, L"HEAD") && !(item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) *required |= 1U;
            if (!lstrcmpiW(item.cFileName, L"objects") && (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) *required |= 2U;
            if (!lstrcmpiW(item.cFileName, L"refs") && (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) *required |= 4U;
        }
        name_length = (size_t)lstrlenW(item.cFileName);
        if (length + 1U + name_length >= GIT_PATH_CAP) { valid = 0; break; }
        path[length] = L'\\'; memcpy(path + length + 1U, item.cFileName, (name_length + 1U) * sizeof(WCHAR));
        if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            valid = git_diagnostic_scan_metadata(path, length + 1U + name_length, depth + 1U, count, required);
        else {
            BY_HANDLE_FILE_INFORMATION info;
            HANDLE file = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
            valid = file != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file, &info) &&
                !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
                info.nNumberOfLinks == 1U;
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        }
        path[length] = 0;
        if (!valid) break;
    } while (FindNextFileW(search, &item));
    error = valid ? GetLastError() : ERROR_SUCCESS;
    FindClose(search);
    return valid && error == ERROR_NO_MORE_FILES;
}
static int git_diagnostic_metadata_valid(int scan_contents)
{
    HANDLE current;
    WCHAR *path;
    size_t length;
    unsigned int count = 0, required = 0;
    int valid;
    if (!git_ui.diagnostic_git_dir || !git_ui.root[0]) return 0;
    current = git_diagnostic_open_git_dir(git_ui.root);
    valid = same_file(current, git_ui.diagnostic_git_dir);
    if (current) CloseHandle(current);
    if (!valid) return 0;
    if (!scan_contents) return 1;
    path = HeapAlloc(GetProcessHeap(), 0, GIT_PATH_CAP * sizeof(WCHAR));
    if (!path) return 0;
    length = (size_t)lstrlenW(git_ui.root);
    if (length + 6U >= GIT_PATH_CAP) { HeapFree(GetProcessHeap(), 0, path); return 0; }
    lstrcpyW(path, git_ui.root); lstrcatW(path, L"\\.git");
    valid = git_diagnostic_scan_metadata(path, length + 5U, 0, &count, &required) && required == 7U;
    HeapFree(GetProcessHeap(), 0, path);
    return valid;
}
static void git_diagnostic_configure(const WCHAR *config)
{
    WCHAR *path;
    DWORD length;
    HANDLE directory;
    BY_HANDLE_FILE_INFORMATION info;
    git_diagnostic_close();
    if (!GetPrivateProfileIntW(L"loader", L"developer_mode", 0, config) ||
        GetPrivateProfileIntW(L"loader", L"editor_git_local_diagnostic", 0, config) != 1U) return;
    git_ui.diagnostic_requested = 1;
    path = HeapAlloc(GetProcessHeap(), 0, GIT_PATH_CAP * sizeof(WCHAR));
    if (!path) return;
    length = GetPrivateProfileStringW(L"loader", L"editor_git_diagnostic_project", L"", path, GIT_PATH_CAP, config);
    if (length < 3U || length >= GIT_PATH_CAP - 1U || path[1] != L':' ||
        (path[2] != L'\\' && path[2] != L'/')) { HeapFree(GetProcessHeap(), 0, path); return; }
    directory = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    git_ui.diagnostic_git_dir = git_diagnostic_open_git_dir(path);
    HeapFree(GetProcessHeap(), 0, path);
    if (directory == INVALID_HANDLE_VALUE || !git_ui.diagnostic_git_dir) {
        if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
        if (git_ui.diagnostic_git_dir) CloseHandle(git_ui.diagnostic_git_dir);
        git_ui.diagnostic_git_dir = NULL; return;
    }
    if (GetFileType(directory) != FILE_TYPE_DISK || !GetFileInformationByHandle(directory, &info) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        CloseHandle(directory); CloseHandle(git_ui.diagnostic_git_dir); git_ui.diagnostic_git_dir = NULL; return;
    }
    git_ui.diagnostic_project = directory;
    dluma_log_line("WARN", "Local Git diagnostic is enabled for its configured project.");
}
#endif
static int git_operation_guard(HWND, unsigned int, UINT, WPARAM, LPARAM, LRESULT *, void *);
static int git_guard_registered_valid(void)
{
    return git_ui.guard_registered && !git_ui.stopping && !git_ui.closing && !git_ui.aborting &&
        dluma_editor_ui_guard_ready(git_operation_guard, NULL);
}
static int git_policy_snapshot(void *expected_editor, void **observed_editor)
{
    void **global;
    void *table;
    if (!dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR)) return 0;
    global = (void **)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,
        GIT_POLICY_EDITOR_GLOBAL, sizeof(void *));
    table = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,
        GIT_POLICY_EDITOR_TABLE, sizeof(void *));
    return git_policy_read_off(global, table, (DWORD)git_ui.thread, expected_editor, observed_editor);
}
static int git_bound_foreign(DlumaEditorUiCoverageForeign *out)
{
    DlumaEditorGitJobSnapshotV1 job;
    if (!out) return 0;
    ZeroMemory(out, sizeof(*out)); ZeroMemory(&job, sizeof(job));
    job.size = sizeof(job); job.version = 1;
    if (!dluma_editor_git_session_job_snapshot(&job) || !job.companion_process || !job.companion_pid ||
        !dluma_editor_panel_host_bound_child(&git_ui.panel, job.companion_pid, job.companion_process,
            &out->child, &out->binding_generation, &out->process_creation_time)) return 0;
    out->parent = git_ui.panel.content_window; out->process_id = job.companion_pid; out->process = job.companion_process;
    return 1;
}
static int git_same_foreign(const DlumaEditorUiCoverageForeign *a, const DlumaEditorUiCoverageForeign *b)
{
    return a->child == b->child && a->parent == b->parent && a->process_id == b->process_id &&
        a->process == b->process && a->binding_generation == b->binding_generation &&
        a->process_creation_time == b->process_creation_time;
}
static int git_coverage_current(void)
{
    DlumaEditorUiCoverageForeign current;
    return git_bound_foreign(&current) && git_same_foreign(&current, &git_ui.coverage_foreign) &&
        dluma_editor_ui_guard_coverage_current_foreign(git_operation_guard, NULL, git_ui.main,
            &current, git_ui.coverage_generation);
}
static int git_coverage_capture(uint64_t *generation)
{
    DlumaEditorUiCoverageForeign before, after;
    DlumaEditorUiCoverageDetail detail;
    int result;
    ZeroMemory(&detail, sizeof(detail));
    if (!git_bound_foreign(&before)) { result = DLUMA_EDITOR_UI_COVERAGE_FAILED; detail.reason = 22U; }
    else {
        result = dluma_editor_ui_guard_coverage_capture_foreign(git_operation_guard, NULL, git_ui.main, &before, generation, &detail);
        if (result == DLUMA_EDITOR_UI_COVERAGE_READY &&
            (!git_bound_foreign(&after) || !git_same_foreign(&before, &after))) {
            result = DLUMA_EDITOR_UI_COVERAGE_FAILED; detail.reason = 23U;
        }
    }
    if (result != DLUMA_EDITOR_UI_COVERAGE_READY) {
        if (git_ui.guard_diagnostics && git_ui.coverage_events < 32) {
            char line[160];
            ++git_ui.coverage_events;
            wsprintfA(line, "Git coverage reason=%u window=%I64X pid=%u thread=%u count=%u",
                detail.reason, (unsigned long long)(UINT_PTR)detail.window,
                (unsigned int)detail.process_id, (unsigned int)detail.thread_id, detail.count);
            dluma_log_line("INFO", line);
        }
        return result;
    }
    git_ui.coverage_foreign = before;
    return result;
}
static int git_guard_authority_valid(void)
{
    return git_mutations_qualified() && git_guard_registered_valid() && git_ui.guard_epoch &&
        dluma_editor_ui_guard_armed(git_operation_guard, NULL) &&
        dluma_editor_ui_guard_arm_ready(git_operation_guard, NULL, git_ui.guard_epoch) &&
        git_coverage_current() &&
        (!git_ui.lease.active || git_ui.policy_editor) &&
        git_policy_snapshot(git_ui.lease.active ? git_ui.policy_editor : NULL, NULL);
}

/* Developer-only, bounded numeric reason bits: project=1, recovery=2,
 * diagnostic fixture/metadata=4, broker=8, native off/null policy=16. */
static void git_observe_readiness(int current)
{
    unsigned int mask = 0;
    char line[80];
    if (!git_ui.guard_diagnostics || git_ui.readiness_events >= 32) return;
    if (!current) mask |= 1U;
    if (git_ui.lease.recovery) mask |= 2U;
    if (!git_mutations_qualified()) mask |= 4U;
    if (!git_guard_registered_valid()) mask |= 8U;
    if (!git_policy_snapshot(NULL, NULL)) mask |= 16U;
    ++git_ui.readiness_events;
    wsprintfA(line, "Git readiness mask=%u", mask);
    dluma_log_line("INFO", line);
}

/* Developer-only sampled pre-arm busy bits: children=1, job count=2,
 * coverage pending=4, coverage failed=8, deferred replay=16, arm=32,
 * epoch=64, supplied arm=128. No project or command data is logged. */
static void git_observe_busy(int coverage, unsigned int children, unsigned int processes, int has_arm)
{
    unsigned int mask = 0;
    char line[80];
    if (!git_ui.guard_diagnostics || git_ui.busy_events >= 32) return;
    if (children) mask |= 1U;
    if (processes != 1U) mask |= 2U;
    if (coverage == DLUMA_EDITOR_UI_COVERAGE_PENDING) mask |= 4U;
    if (coverage == DLUMA_EDITOR_UI_COVERAGE_FAILED) mask |= 8U;
    if (git_ui.deferred.message) mask |= 16U;
    if (git_ui.arm.active) mask |= 32U;
    if (git_ui.guard_epoch) mask |= 64U;
    if (has_arm) mask |= 128U;
    ++git_ui.busy_events;
    wsprintfA(line, "Git admission busy mask=%u", mask);
    dluma_log_line("INFO", line);
}

/* Developer-only armed-state bits: broker=1, not armed=2, barrier=4,
 * coverage=8, native policy=16, children=32, job count=64, fixture=128. */
static void git_observe_pending(unsigned int children, unsigned int processes)
{
    unsigned int mask = 0;
    char line[80];
    if (!git_ui.guard_diagnostics || git_ui.pending_events >= 32) return;
    if (!git_guard_registered_valid()) mask |= 1U;
    if (!dluma_editor_ui_guard_armed(git_operation_guard, NULL)) mask |= 2U;
    if (!dluma_editor_ui_guard_arm_ready(git_operation_guard, NULL, git_ui.guard_epoch)) mask |= 4U;
    if (!git_coverage_current()) mask |= 8U;
    if (!git_policy_snapshot(NULL, NULL)) mask |= 16U;
    if (children) mask |= 32U;
    if (processes != 1U) mask |= 64U;
    if (!git_mutations_qualified()) mask |= 128U;
    ++git_ui.pending_events;
    wsprintfA(line, "Git admission armed mask=%u", mask);
    dluma_log_line("INFO", line);
}

/* Developer-only opt-in observation records message numbers, never payloads. */
static void git_guard_observe(const char *stage, HWND window, UINT message, WPARAM wparam)
{
    char line[128];
    if (!git_ui.guard_diagnostics || git_ui.guard_events >= 64 || window != git_ui.main ||
        (message != WM_COMMAND && message != WM_CLOSE && message != WM_SYSCOMMAND &&
         message != WM_QUERYENDSESSION && message != WM_ENDSESSION &&
         message != WM_DESTROY && message != WM_NCDESTROY)) return;
    ++git_ui.guard_events;
    wsprintfA(line, "Git guard %s message=%u command=%u", stage, (unsigned int)message,
        message == WM_COMMAND ? (unsigned int)LOWORD(wparam) : 0U);
    dluma_log_line("INFO", line);
}

static int git_operation_guard(HWND window, unsigned int stage, UINT message,
    WPARAM wparam, LPARAM lparam, LRESULT *result, void *context)
{
    int action;
    UINT previous;
    (void)context;
    if (stage == DLUMA_EDITOR_UI_COVERAGE_LOST) {
        if (GetCurrentThreadId() == (DWORD)git_ui.thread && !git_ui.aborting &&
            (git_ui.lease.active || dluma_editor_ui_guard_armed(git_operation_guard, NULL))) {
            git_ui.aborting = 1; git_ui.closing = 1;
            git_lease_retire(&git_ui.lease, 0);
            (void)dluma_editor_git_session_abort();
        }
        return 0;
    }
    git_guard_observe("before", window, message, wparam);
    previous = git_ui.deferred.message;
    action = git_guard_decide(&git_ui.deferred, git_ui.lease.active ||
        dluma_editor_ui_guard_armed(git_operation_guard, NULL),
        window == git_ui.main && GetCurrentThreadId() == (DWORD)git_ui.thread,
        window != git_ui.main && git_ui.main && GetCurrentThreadId() == (DWORD)git_ui.thread && IsChild(git_ui.main, window),
        message, wparam, lparam);
    if (!previous && git_ui.deferred.message) {
        if (git_ui.replay_exhausted || ++git_ui.next_replay == 0) {
            git_ui.replay_exhausted = 1;
            ZeroMemory(&git_ui.deferred, sizeof(git_ui.deferred));
        } else git_guard_capture(&git_ui.deferred, git_ui.generation,
            (uintptr_t)git_ui.main, git_ui.next_replay);
    }
    if ((action & GIT_GUARD_ABORT) && !git_ui.aborting) {
        git_ui.aborting = 1;
        git_ui.closing = 1;
        git_lease_retire(&git_ui.lease, 0);
        (void)dluma_editor_git_session_abort();
    }
    if (action & GIT_GUARD_BLOCK) { *result = 0; return 1; }
    return 0;
}

static int git_new_lease_token(char token[33])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char bytes[16];
    unsigned int i;
    if (BCryptGenRandom(NULL, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return 0;
    for (i = 0; i < sizeof(bytes); ++i) {
        token[2U*i] = hex[bytes[i] >> 4];
        token[2U*i+1U] = hex[bytes[i] & 15U];
    }
    token[32] = 0;
    return 1;
}

/* Called only after the broker's armed epoch has drained. */
static int git_promote_project_lease(int project_valid, const char *operation,
    const char *token, unsigned int pending, unsigned int processes)
{
    void *editor = NULL;
    if (!git_guard_authority_valid() || !git_policy_snapshot(NULL, &editor) ||
        !git_policy_snapshot(editor, NULL)) return 0;
    if (git_lease_acquire(&git_ui.lease, 1, project_valid, git_guard_authority_valid(),
            git_ui.generation, operation, token, pending, processes) != GIT_LEASE_OK) return 0;
    if (!git_policy_snapshot(editor, NULL) || !git_arm_promote(&git_ui.arm, git_ui.guard_epoch)) return 0;
    git_ui.policy_editor = editor;
    return 1;
}

static int git_request_project_matches(const DlumaEditorGitParsedRequest *request)
{
    WCHAR *root;
    int valid;
    if (request->project_generation != git_ui.generation) return 0;
    root = HeapAlloc(GetProcessHeap(), 0, GIT_PATH_CAP * sizeof(WCHAR));
    if (!root) return 0;
    valid = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, request->project_root, -1, root, GIT_PATH_CAP) &&
        !lstrcmpiW(root, git_ui.root);
    HeapFree(GetProcessHeap(), 0, root);
    return valid;
}

static int own_window(HWND window)
{
    DWORD pid = 0;
    return IsWindow(window) && GetWindowThreadProcessId(window, &pid) == GetCurrentThreadId() &&
        pid == GetCurrentProcessId();
}

static int same_file(HANDLE left, HANDLE right)
{
    BY_HANDLE_FILE_INFORMATION a, b;
    return left && left != INVALID_HANDLE_VALUE && right && right != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(left, &a) && GetFileInformationByHandle(right, &b) &&
        a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
        a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow;
}

/* A read-only snapshot, never a lease or a retained native string. Reject
 * non-ASCII paths until the native catalog's encoding contract is qualified. */
static HANDLE selected_project(WCHAR *path, DWORD capacity)
{
    BYTE *manager;
    BYTE *catalog, *again;
    const unsigned char *text;
    unsigned int count, count_again, i;
    int index, index_again;
    HANDLE directory;
    BY_HANDLE_FILE_INFORMATION info;
    WCHAR final_path[GIT_PATH_CAP];
    DWORD length;
    if (!git_ui.main || !own_window(git_ui.main) || !path || capacity < 4U ||
        !dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR)) return NULL;
    manager = (BYTE *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 0x1BA0400U, 188U);
    if (!manager) return NULL;
    __try {
        catalog = *(BYTE **)(manager + 32U);
        count = *(unsigned int *)(manager + 40U);
        index = *(int *)(manager + 184U);
        if (!catalog || !count || count > 65536U || index < 0 || (unsigned int)index >= count) return NULL;
        text = *(const unsigned char **)(catalog + 144U * (unsigned int)index + 64U);
        if (!text) return NULL;
        for (i = 0; i < capacity; ++i) {
            unsigned char c = text[i];
            if (c > 0x7fU || (c && c < 0x20U)) return NULL;
            path[i] = (WCHAR)c;
            if (!c) break;
        }
        if (i == capacity || i < 3U || path[1] != L':' || (path[2] != L'\\' && path[2] != L'/')) return NULL;
        again = *(BYTE **)(manager + 32U); count_again = *(unsigned int *)(manager + 40U);
        index_again = *(int *)(manager + 184U);
        if (again != catalog || count_again != count || index_again != index ||
            *(const unsigned char **)(catalog + 144U * (unsigned int)index + 64U) != text) return NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
    directory = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (directory == INVALID_HANDLE_VALUE) return NULL;
    length = GetFinalPathNameByHandleW(directory, final_path, GIT_PATH_CAP, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!GetFileInformationByHandle(directory, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        !length || length >= GIT_PATH_CAP || memcmp(final_path, L"\\\\?\\", 4U * sizeof(WCHAR)) ||
        final_path[5] != L':' || length - 4U >= capacity) {
        CloseHandle(directory); return NULL;
    }
    for (i = 4U; i < length; ++i) if (final_path[i] > 0x7fU) { CloseHandle(directory); return NULL; }
    lstrcpyW(path, final_path + 4U);
    return directory;
}

static int project_current(void)
{
    WCHAR *path = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, GIT_PATH_CAP * sizeof(WCHAR));
    HANDLE current;
    int valid;
    if (!path) return 0;
    current = selected_project(path, GIT_PATH_CAP);
    valid = same_file(current, git_ui.project) && !lstrcmpiW(path, git_ui.root);
    if (current) CloseHandle(current);
    HeapFree(GetProcessHeap(), 0, path);
    return valid;
}

static void reap_children(void)
{
    unsigned int i;
    for (i = 0; i < 32U; ++i) if (git_ui.children[i] &&
        WaitForSingleObject(git_ui.children[i], 0) == WAIT_OBJECT_0) {
        CloseHandle(git_ui.children[i]); git_ui.children[i] = NULL;
    }
}

static int pin_executable(const char *utf8)
{
    WCHAR *path;
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION info;
    unsigned int i;
    int success = 0;
    path = HeapAlloc(GetProcessHeap(), 0, GIT_PATH_CAP * sizeof(WCHAR));
    if (!path) return 0;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, path, GIT_PATH_CAP)) goto done;
    file = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { CloseHandle(file); goto done; }
    if (same_file(file, git_ui.executable)) { CloseHandle(file); success = 1; goto done; }
    reap_children();
    for (i = 0; i < 32U; ++i) if (git_ui.children[i]) { CloseHandle(file); goto done; }
    if (git_ui.executable) CloseHandle(git_ui.executable);
    git_ui.executable = file; success = 1;
done:
    HeapFree(GetProcessHeap(), 0, path); return success;
}

static int json_root(char *output, size_t capacity, size_t *used)
{
    unsigned int i;
    size_t n = *used;
    if (n + 1U >= capacity) return 0;
    output[n++] = '"';
    for (i = 0; git_ui.root[i]; ++i) {
        char c = (char)git_ui.root[i];
        if (n + 515U >= capacity) return 0;
        if (c == '\\' || c == '"') output[n++] = '\\';
        output[n++] = c;
    }
    if (n + 2U >= capacity) return 0;
    output[n++] = '"'; output[n] = 0; *used = n; return 1;
}

/* Called only on the owner thread after no Git mutation can be running. */
static void git_schedule_deferred(void)
{
    if (git_ui.deferred.message && (!git_replay_message || git_ui.stopping ||
        !IsWindow(git_ui.main) || !PostMessageW(git_ui.main, git_replay_message,
            (WPARAM)git_ui.deferred.token, 0)))
        ZeroMemory(&git_ui.deferred, sizeof(git_ui.deferred));
}

static int git_cancel_arm(void)
{
    if (!git_ui.arm.active || git_ui.lease.active || !git_ui.guard_epoch ||
        git_ui.arm.epoch != git_ui.guard_epoch ||
        !dluma_editor_ui_guard_disarm(git_operation_guard, NULL, git_ui.guard_epoch)) return 0;
    memcpy(git_ui.last_arm_id, git_ui.arm.token, sizeof(git_ui.last_arm_id));
    git_ui.last_arm_generation = git_ui.arm.generation;
    if (!git_arm_clear(&git_ui.arm, 1)) return 0;
    git_ui.guard_epoch = 0;
    git_ui.coverage_generation = 0;
    ZeroMemory(&git_ui.coverage_foreign, sizeof(git_ui.coverage_foreign));
    git_schedule_deferred();
    return 1;
}

static DlumaEditorGitRequestResult request(const char *type, const char *payload,
    size_t size, char *output, size_t capacity, size_t *used, void *context)
{
    DlumaEditorGitParsedRequest *parsed;
    DlumaEditorGitJobSnapshotV1 job;
    HANDLE child = NULL;
    int current, ok = 0, count;
    unsigned int i, pending;
    char token[33];
    GitLeaseResult lease_result;
    (void)context;
    *used = 0;
    if (capacity < 512U) return DLUMA_EDITOR_GIT_REQUEST_ERROR;
    parsed = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*parsed));
    if (!parsed) goto failed;
    if (!dluma_editor_source_control_parse_request(type, payload, size, parsed)) goto done;
    current = !git_ui.closing && !git_ui.stopping && project_current();
    if (parsed->kind == DLUMA_GIT_STATE) {
        int diagnostic, ordinary, qualified, policy;
        current = current && git_request_project_matches(parsed);
        if (current && parsed->has_executable) current = pin_executable(parsed->git_executable);
        qualified = current && git_mutations_qualified();
        policy = qualified && git_policy_snapshot(NULL, NULL);
        diagnostic = current && !git_ui.lease.recovery && qualified && git_guard_registered_valid() && policy;
        ordinary = diagnostic && git_ordinary_local_active();
        git_observe_readiness(current);
        count = wsprintfA(output, "{\"projectRoot\":");
        if (count < 0) goto done;
        *used = (size_t)count;
        if (!json_root(output, capacity, used)) goto done;
        count = wsprintfA(output + *used,
            ",\"projectGeneration\":%I64u,\"enabled\":%s,\"mutationsAvailable\":%s,\"worktreeAvailable\":false,\"localWritesOnly\":%s,\"branchCreateAvailable\":%s,\"diagnosticFixtureOnly\":%s,\"message\":\"%s\"}",
            (unsigned long long)git_ui.generation, current ? "true" : "false",
            diagnostic ? "true" : "false",
            diagnostic ? "true" : "false",
            ordinary ? "true" : "false",
            diagnostic && !ordinary ? "true" : "false",
            current && git_ui.lease.recovery ? "Git cleanup needs attention. Reopen Git after checking the operation." :
            ordinary ? "Local Git writes are available." :
            diagnostic ? "Local diagnostic Git writes are available." :
            current && git_ordinary_local_supported_mode() && !policy ? "Turn off Source control enabled in Editor Options to use Git writes." :
            current ? "Git status and history are available. Write operations are not available yet." :
                "The project or Git executable is unavailable. Reopen Git to try again.");
        if (count >= 0) { *used += (size_t)count; ok = 1; }
    } else if (parsed->kind == DLUMA_GIT_PROJECT_ACQUIRE) {
        int qualified = git_mutations_qualified();
        int project_valid = current && git_request_project_matches(parsed) && git_ui.executable != NULL;
        int job_valid = 0, coverage = DLUMA_EDITOR_UI_COVERAGE_FAILED;
        uint64_t coverage_generation = 0;
        pending = 0;
        ZeroMemory(&job, sizeof(job)); job.size = sizeof(job); job.version = 1;
        if (qualified && project_valid && git_guard_registered_valid()) {
            if (git_ui.arm.active)
                coverage = git_coverage_current() ? DLUMA_EDITOR_UI_COVERAGE_READY : DLUMA_EDITOR_UI_COVERAGE_FAILED;
            else coverage = git_coverage_capture(&coverage_generation);
            reap_children();
            for (i = 0; i < 32U; ++i) if (git_ui.children[i]) ++pending;
            job_valid = dluma_editor_git_session_job_snapshot(&job) &&
                WaitForSingleObject(job.companion_process, 0) == WAIT_TIMEOUT;
        }
        if (!qualified || !project_valid || !git_guard_registered_valid() || !job_valid ||
            git_ui.lease.recovery || git_ui.lease.active) {
            count = wsprintfA(output, "{\"granted\":false,\"leaseId\":null,\"code\":\"unavailable\",\"message\":\"Write operations are not available yet.\"}");
        } else if (!git_local_operation_allowed(parsed->operation)) {
            count = wsprintfA(output, "{\"granted\":false,\"leaseId\":null,\"code\":\"unavailable\",\"message\":\"This Git operation is not available.\"}");
        } else if (!git_ui.arm.active && coverage == DLUMA_EDITOR_UI_COVERAGE_FAILED) {
            count = wsprintfA(output, "{\"granted\":false,\"leaseId\":null,\"code\":\"unavailable\",\"message\":\"Git could not protect the Editor window. Close extra windows and reopen Git.\"}");
        } else if (!git_ui.arm.active && !parsed->has_arm && !git_ui.guard_epoch &&
                   coverage == DLUMA_EDITOR_UI_COVERAGE_READY && !git_ui.deferred.message && !pending && job.active_processes == 1U &&
            git_local_operation_allowed(parsed->operation) && git_new_lease_token(token)) {
            uint64_t epoch = dluma_editor_ui_guard_arm(git_operation_guard, NULL);
            git_ui.coverage_generation = coverage_generation;
            if (epoch && git_coverage_current() &&
                git_arm_begin(&git_ui.arm, token, epoch, git_ui.generation, parsed->operation)) {
                git_ui.guard_epoch = epoch;
                git_ui.arm_started = GetTickCount64();
                count = wsprintfA(output, "{\"granted\":false,\"leaseId\":null,\"code\":\"pending\",\"message\":\"Preparing Git.\",\"armId\":\"%s\"}", git_ui.arm.token);
            } else {
                if (epoch && !dluma_editor_ui_guard_disarm(git_operation_guard, NULL, epoch)) {
                    git_ui.lease.recovery = 1; git_ui.closing = 1;
                    (void)dluma_editor_git_session_abort();
                }
                git_ui.coverage_generation = 0;
                ZeroMemory(&git_ui.coverage_foreign, sizeof(git_ui.coverage_foreign));
                count = wsprintfA(output, "{\"granted\":false,\"leaseId\":null,\"code\":\"unavailable\",\"message\":\"Git could not be prepared.\"}");
            }
        } else if (git_ui.arm.active && parsed->has_arm &&
                   git_arm_matches(&git_ui.arm, parsed->arm_id, parsed->project_generation, parsed->operation)) {
            git_observe_pending(pending, job.active_processes);
            if (coverage == DLUMA_EDITOR_UI_COVERAGE_FAILED) {
                git_observe_busy(coverage, pending, job.active_processes, parsed->has_arm);
                count = wsprintfA(output, "{\"granted\":false,\"leaseId\":null,\"code\":\"unavailable\",\"message\":\"Git could not protect the Editor window. Close extra windows and reopen Git.\"}");
            } else if (coverage != DLUMA_EDITOR_UI_COVERAGE_READY || git_ui.deferred.message ||
                GetTickCount64() - git_ui.arm_started >= 10000ULL) {
                git_observe_busy(coverage, pending, job.active_processes, parsed->has_arm);
                count = wsprintfA(output, "{\"granted\":false,\"leaseId\":null,\"code\":\"busy\",\"message\":\"Git preparation was interrupted.\"}");
            } else if (!git_guard_authority_valid() || pending || job.active_processes != 1U) {
                count = wsprintfA(output, "{\"granted\":false,\"leaseId\":null,\"code\":\"pending\",\"message\":\"Preparing Git.\",\"armId\":\"%s\"}", git_ui.arm.token);
            } else if (git_new_lease_token(token) && git_promote_project_lease(project_valid,
                       parsed->operation, token, pending, job.active_processes)) {
                count = wsprintfA(output, "{\"granted\":true,\"leaseId\":\"%s\",\"code\":\"ok\",\"message\":\"\"}", git_ui.lease.token);
            } else {
                if (git_ui.lease.active) { git_ui.lease.recovery = 1; git_ui.closing = 1; (void)dluma_editor_git_session_abort(); }
                count = wsprintfA(output, "{\"granted\":false,\"leaseId\":null,\"code\":\"unavailable\",\"message\":\"Git could not be prepared.\"}");
            }
        } else {
            git_observe_busy(coverage, pending, job.active_processes, parsed->has_arm);
            count = wsprintfA(output, "{\"granted\":false,\"leaseId\":null,\"code\":\"busy\",\"message\":\"Another Git operation is finishing.\"}");
        }
        if (count >= 0) { *used = (size_t)count; ok = 1; }
    } else if (parsed->kind == DLUMA_GIT_PROJECT_CANCEL) {
        int same_project = git_request_project_matches(parsed);
        int matched = same_project && parsed->has_arm && git_ui.arm.active &&
            git_ui.arm.generation == parsed->project_generation &&
            !memcmp(git_ui.arm.token, parsed->arm_id, sizeof(git_ui.arm.token));
        int canceled = matched && git_cancel_arm();
        if (!canceled && same_project && parsed->has_arm && !matched &&
            git_ui.last_arm_generation == parsed->project_generation &&
            !memcmp(git_ui.last_arm_id, parsed->arm_id, sizeof(git_ui.last_arm_id))) canceled = 1;
        if (matched && !canceled) { git_ui.lease.recovery = 1; git_ui.closing = 1; (void)dluma_editor_git_session_abort(); }
        count = canceled ? wsprintfA(output, "{\"cancelled\":true,\"requiresRecovery\":false,\"code\":\"ok\",\"message\":\"\"}") :
            wsprintfA(output, "{\"cancelled\":false,\"requiresRecovery\":%s,\"code\":\"%s\",\"message\":\"Git preparation could not be canceled.\"}",
                matched ? "true" : "false", matched ? "recovery" : "invalid");
        if (count >= 0) { *used = (size_t)count; ok = 1; }
    } else if (parsed->kind == DLUMA_GIT_CHILD_STARTED) {
        ZeroMemory(&job, sizeof(job)); job.size = sizeof(job); job.version = 1;
        reap_children();
        for (i = 0; i < 32U && git_ui.children[i]; ++i) {}
        if (current && i < 32U &&
            ((!git_ui.lease.active && !git_ui.arm.active && !parsed->mutating && !parsed->has_lease) ||
             (git_mutations_qualified() && git_guard_authority_valid() && !git_ui.lease.recovery &&
              parsed->has_lease && git_lease_matches(&git_ui.lease, parsed->lease_id))) &&
            dluma_editor_git_session_job_snapshot(&job) &&
            dluma_editor_source_control_verify_child(job.companion_process, job.job, git_ui.executable,
                parsed->process_id, parsed->creation_time, parsed->process_handle, &child)) {
            git_ui.children[i] = child;
            count = wsprintfA(output,
                "{\"registered\":true,\"requiresRecovery\":false,\"code\":\"ok\",\"message\":\"\"}");
        } else count = wsprintfA(output,
            "{\"registered\":false,\"requiresRecovery\":false,\"code\":\"unavailable\",\"message\":\"Git process could not be registered.\"}");
        if (count >= 0) { *used = (size_t)count; ok = 1; }
    } else if (parsed->kind == DLUMA_GIT_LEASE_RELEASE) {
        ZeroMemory(&job, sizeof(job)); job.size = sizeof(job); job.version = 1;
        reap_children(); pending = 0;
        for (i = 0; i < 32U; ++i) if (git_ui.children[i]) ++pending;
        if (git_lease_matches(&git_ui.lease, parsed->lease_id)) {
            int authority = git_guard_authority_valid() && dluma_editor_git_session_job_snapshot(&job) &&
                WaitForSingleObject(job.companion_process, 0) == WAIT_TIMEOUT;
#if defined(DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC) && DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC
            if (authority && !git_ordinary_local_active() && !pending && job.active_processes == 1U && !git_diagnostic_metadata_valid(1)) authority = 0;
#endif
            lease_result = git_lease_release(&git_ui.lease, parsed->lease_id,
                current && git_ui.lease.generation == git_ui.generation, authority, pending, job.active_processes, 0);
            if (lease_result == GIT_LEASE_READY) {
                if (!git_ui.guard_epoch || !dluma_editor_ui_guard_disarm(git_operation_guard, NULL, git_ui.guard_epoch)) {
                    git_ui.lease.recovery = 1; lease_result = GIT_LEASE_RECOVERY;
                } else {
                    git_ui.guard_epoch = 0;
                    git_ui.coverage_generation = 0;
                    ZeroMemory(&git_ui.coverage_foreign, sizeof(git_ui.coverage_foreign));
                    lease_result = git_lease_release(&git_ui.lease, parsed->lease_id,
                        current && git_ui.lease.generation == git_ui.generation, authority, pending, job.active_processes, 1);
                }
            }
            if (lease_result == GIT_LEASE_OK) {
                git_ui.policy_editor = NULL;
                count = wsprintfA(output, "{\"released\":true,\"requiresRecovery\":false,\"code\":\"ok\",\"message\":\"\"}");
                git_schedule_deferred();
            }
            else if (lease_result == GIT_LEASE_PENDING)
                count = wsprintfA(output, "{\"released\":false,\"requiresRecovery\":false,\"code\":\"job_pending\",\"message\":\"Git is still finishing.\"}");
            else {
                /* Failed authority or disarm is not a completed release. Stop
                 * only this owned job and retain guard/recovery until drain. */
                git_ui.lease.recovery = 1;
                git_ui.closing = 1;
                if (!git_ui.aborting) {
                    git_ui.aborting = 1;
                    (void)dluma_editor_git_session_abort();
                }
                count = wsprintfA(output, "{\"released\":false,\"requiresRecovery\":true,\"code\":\"recovery\",\"message\":\"Git cleanup needs attention.\"}");
            }
        } else count = wsprintfA(output,
            "{\"released\":false,\"requiresRecovery\":false,\"code\":\"unavailable\",\"message\":\"No Git lease is active.\"}");
        if (count >= 0) { *used = (size_t)count; ok = 1; }
    } else {
        count = wsprintfA(output,
            "{\"granted\":false,\"leaseId\":null,\"code\":\"unavailable\",\"message\":\"Write operations are not available yet.\"}");
        if (count >= 0) { *used = (size_t)count; ok = 1; }
    }
done:
    HeapFree(GetProcessHeap(), 0, parsed);
    if (ok) return DLUMA_EDITOR_GIT_REQUEST_OK;
failed:
    count = wsprintfA(output,
        "{\"code\":\"invalid_request\",\"message\":\"Git request was not accepted.\"}");
    *used = count >= 0 ? (size_t)count : 0U;
    return DLUMA_EDITOR_GIT_REQUEST_ERROR;
}

static int close_session(void)
{
    unsigned int i;
    git_ui.closing = 1;
    if (!dluma_editor_git_session_close() || !dluma_editor_git_session_drained()) {
        git_lease_retire(&git_ui.lease, 0); return 0;
    }
    if (git_ui.guard_epoch && !dluma_editor_ui_guard_disarm(git_operation_guard, NULL, git_ui.guard_epoch)) return 0;
    git_ui.guard_epoch = 0;
    git_ui.coverage_generation = 0;
    ZeroMemory(&git_ui.coverage_foreign, sizeof(git_ui.coverage_foreign));
    if (git_ui.arm.active && !git_arm_clear(&git_ui.arm, 1)) return 0;
    ZeroMemory(git_ui.last_arm_id, sizeof(git_ui.last_arm_id));
    git_ui.last_arm_generation = 0;
    if (git_ui.guard_registered && !dluma_editor_ui_guard_unsubscribe()) return 0;
    git_ui.guard_registered = 0;
    git_lease_retire(&git_ui.lease, 1);
    git_ui.policy_editor = NULL;
    ZeroMemory(&git_ui.deferred, sizeof(git_ui.deferred));
    git_ui.aborting = 0;
    git_ui.session = 0;
    for (i = 0; i < 32U; ++i) if (git_ui.children[i]) {
        CloseHandle(git_ui.children[i]); git_ui.children[i] = NULL;
    }
    if (git_ui.executable) { CloseHandle(git_ui.executable); git_ui.executable = NULL; }
    if (git_ui.project) { CloseHandle(git_ui.project); git_ui.project = NULL; }
    git_ui.root[0] = 0; git_ui.closing = 0;
    return 1;
}

static void panel_changed(const DlumaEditorPanelRef *panel, unsigned int event, unsigned int argument, void *context)
{
    (void)panel; (void)argument; (void)context;
    if (git_ui.thread == (LONG)GetCurrentThreadId() &&
        (event == DLUMA_EDITOR_PANEL_CLOSING || event == DLUMA_EDITOR_PANEL_CHILD_EXITED))
        (void)close_session();
}

int dluma_editor_source_control_host_show(HWND main)
{
    DlumaEditorGitSessionSpecV1 spec;
    if (!git_ui.started || git_ui.stopping || !own_window(main) ||
        !dluma_editor_is_main_window(main, GetCurrentProcessId())) return 0;
    git_ui.thread = GetCurrentThreadId(); git_ui.main = main;
    if (!SetTimer(main, GIT_TIMER, 200U, NULL)) return 0;
    if (git_ui.closing && !close_session()) return 0;
    if (git_ui.session && !project_current() && !close_session()) return 0;
    if (!git_ui.session) {
        git_ui.project = selected_project(git_ui.root, GIT_PATH_CAP);
        if (!git_ui.project) return 0;
        if (++git_ui.generation == 0) { CloseHandle(git_ui.project); git_ui.project = NULL; return 0; }
    }
    if (!dluma_editor_dock_show(&git_ui.dock, main, 0xD1A1U, "Git", &git_ui, 1)) goto failed;
    if (!git_ui.panel.token) {
        git_ui.panel.size = sizeof(git_ui.panel); git_ui.panel.version = 1;
        if (!dluma_editor_panel_host_create_view(git_ui.dock.content, panel_changed, NULL, &git_ui.panel)) goto failed;
    }
    if (!git_ui.session) {
        if (!dluma_editor_git_session_drained()) goto failed;
        ZeroMemory(&spec, sizeof(spec)); spec.size = sizeof(spec); spec.version = 1;
        spec.owner = git_ui.dock.content; spec.panel = git_ui.panel;
        spec.theme_config = git_ui.config; spec.project_root = git_ui.root;
        spec.project_generation = git_ui.generation; spec.handle_request = request;
        if ((git_ordinary_local_supported_mode() || git_mutations_qualified() || git_ui.guard_diagnostics) && !git_ui.guard_registered) {
            git_ui.guard_registered = dluma_editor_ui_guard_subscribe(git_operation_guard, NULL);
            if (!git_ui.guard_registered && git_ui.guard_diagnostics)
                dluma_log_line("WARN", "Editor Git guard observation could not start.");
        }
        (void)SetWindowTextW(git_ui.panel.content_window, L"Loading Git...");
        if (!dluma_editor_git_session_open(&spec)) goto failed;
        git_ui.session = 1;
    }
    return 1;
failed:
    if (!git_ui.session) (void)close_session();
    return 0;
}

static int on_window(HWND window, unsigned int stage, UINT message, WPARAM wp, LPARAM lp, LRESULT *result, void *context)
{
    int handled = 0;
    (void)context;
    InterlockedIncrement(&git_ui.callbacks);
    __try {
        if (stage == DLUMA_EDITOR_UI_AFTER && git_ui.guard_registered) git_guard_observe("after", window, message, wp);
        if (stage == DLUMA_EDITOR_UI_BEFORE && window == git_ui.main && message == git_replay_message) {
            if (!lp && git_ui.deferred.message && (uint64_t)wp == git_ui.deferred.token) {
                GitGuardState deferred = git_ui.deferred;
                int valid = git_guard_replay_valid(&deferred, git_ui.generation, (uintptr_t)window,
                    (uint64_t)wp, git_ui.session && git_ui.guard_registered && !git_ui.closing,
                    project_current(), git_ui.lease.active, git_ui.stopping);
                ZeroMemory(&git_ui.deferred, sizeof(git_ui.deferred));
                if (valid) (void)SendMessageW(window, deferred.message, deferred.wparam, 0);
            }
            *result = 0; handled = 1; __leave;
        }
        if (stage == DLUMA_EDITOR_UI_BEFORE && window == git_ui.options_page && message == WM_COMMAND &&
            LOWORD(wp) == GIT_OPTIONS_BUTTON && HIWORD(wp) == BN_CLICKED &&
            (HWND)lp == git_ui.options_button && git_ui.options_button) {
            HWND main = GetAncestor(git_ui.options_dialog, GA_ROOTOWNER);
            if (!dluma_editor_source_control_host_show(main))
                MessageBoxW(git_ui.options_dialog, L"Git could not open for the selected project.", L"Git", MB_OK | MB_ICONINFORMATION);
            *result = 0; handled = 1; __leave;
        }
        if (stage == DLUMA_EDITOR_UI_DESTROY && window == git_ui.options_page) {
            git_ui.options_page = git_ui.options_dialog = git_ui.options_button = NULL;
        }
        if (stage == DLUMA_EDITOR_UI_BEFORE && message == WM_COMMAND && LOWORD(wp) == GIT_COMMAND &&
            HIWORD(wp) <= 1U && !lp && dluma_editor_is_main_window(window, GetCurrentProcessId())) {
            if (!dluma_editor_source_control_host_show(window))
                MessageBoxW(window, L"Git could not open for the selected project.", L"Git", MB_OK | MB_ICONINFORMATION);
            *result = 0; handled = 1;
        } else if (stage == DLUMA_EDITOR_UI_BEFORE && window == git_ui.main &&
            ((message == WM_TIMER && wp == GIT_TIMER) || message == git_stop_message)) {
            reap_children();
            if (!git_ui.closing && !git_ui.stopping && git_ui.guard_epoch &&
                (!git_coverage_current() ||
                 (git_ui.lease.active && !git_guard_authority_valid()))) {
                git_ui.closing = git_ui.aborting = 1;
                git_lease_retire(&git_ui.lease, 0);
                (void)dluma_editor_git_session_abort();
            }
            if (git_ui.arm.active && !git_ui.closing && GetTickCount64() - git_ui.arm_started >= 10000ULL && !git_cancel_arm()) {
                git_ui.lease.recovery = 1; git_ui.closing = 1; (void)dluma_editor_git_session_abort();
            }
            if (git_ui.stopping) (void)dluma_editor_source_control_host_stop();
            else if (git_ui.closing || (git_ui.session && !project_current())) (void)close_session();
            *result = 0; handled = 1;
        } else if (window == git_ui.dock.content) {
            if (stage == DLUMA_EDITOR_UI_BEFORE && dluma_editor_git_session_dispatch(window, message, wp, lp, result)) {
                if (*result == -1) {
                    (void)close_session();
                    if (git_ui.panel.token) (void)SetWindowTextW(git_ui.panel.content_window, L"Reopen Git to try again.");
                } else if (dluma_editor_panel_host_binding_generation(&git_ui.panel))
                    (void)SetWindowTextW(git_ui.panel.content_window, L"");
                handled = 1;
            } else if (stage == DLUMA_EDITOR_UI_AFTER &&
                (message == WM_SIZE || message == WM_WINDOWPOSCHANGED || message == WM_DPICHANGED)) {
                if (git_ui.panel.token) (void)dluma_editor_panel_host_layout(&git_ui.panel);
            } else if (stage == DLUMA_EDITOR_UI_BEFORE &&
                (message == WM_COMMAND || message == WM_NOTIFY || message == WM_CLOSE ||
                 (message == WM_SYSCOMMAND && (wp & 0xfff0U) == SC_CLOSE))) {
                if (message == WM_CLOSE || message == WM_SYSCOMMAND)
                    (void)dluma_editor_dock_show(&git_ui.dock, git_ui.main, 0xD1A1U, "Git", &git_ui, 0);
                *result = 0; handled = 1;
            } else if (stage == DLUMA_EDITOR_UI_BEFORE && message == WM_SETFOCUS && git_ui.panel.token) {
                (void)SetFocus(git_ui.panel.panel_window); *result = 0; handled = 1;
            } else if (stage == DLUMA_EDITOR_UI_DESTROY) {
                (void)close_session();
                ZeroMemory(&git_ui.panel, sizeof(git_ui.panel));
                git_ui.dock.content = git_ui.dock.pane = NULL;
            }
        } else if (window == git_ui.main && stage == DLUMA_EDITOR_UI_DESTROY) {
            /* The main window cannot carry retries after this callback. Complete
             * window-owned cleanup here, then hand session retirement off. */
            InterlockedExchange(&git_ui.stopping, 1);
            (void)dluma_editor_git_session_detach();
            if (git_ui.options_page) dluma_editor_source_control_host_options_detach(git_ui.options_page);
            if (git_ui.panel.token && IsWindow(git_ui.panel.panel_window) &&
                !dluma_editor_panel_host_close(&git_ui.panel)) __leave;
            ZeroMemory(&git_ui.panel, sizeof(git_ui.panel));
            git_ui.dock.content = git_ui.dock.pane = NULL;
            git_ui.main = NULL;
            InterlockedExchange(&git_ui.thread, 0);
        }
    } __finally { InterlockedDecrement(&git_ui.callbacks); }
    return handled;
}

int dluma_editor_source_control_host_start(const WCHAR *config_path)
{
    if (git_ui.started) return !git_ui.stopping;
    if (!config_path || !*config_path || lstrlenW(config_path) >= (int)GIT_PATH_CAP) return 0;
    lstrcpyW(git_ui.config, config_path);
#if defined(DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC) && DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC
    git_diagnostic_configure(config_path);
#endif
    git_ui.guard_diagnostics = GetPrivateProfileIntW(L"loader", L"developer_mode", 0, config_path) != 0 &&
        GetPrivateProfileIntW(L"loader", L"editor_git_guard_diagnostics", 0, config_path) != 0;
    git_ui.guard_events = 0;
    git_ui.readiness_events = 0;
    git_ui.busy_events = 0;
    git_ui.pending_events = 0;
    git_ui.coverage_events = 0;
    git_stop_message = RegisterWindowMessageW(L"DLUMA.EditorGit.Stop.1");
    git_replay_message = RegisterWindowMessageW(L"DLUMA.EditorGit.Replay.1");
    if (!git_stop_message || !git_replay_message || !dluma_editor_ui_subscribe(DLUMA_EDITOR_UI_OWNER_SOURCE_CONTROL, on_window, NULL)) {
#if defined(DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC) && DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC
        git_diagnostic_close();
#endif
        return 0;
    }
    git_ui.stopping = 0; InterlockedExchange(&git_ui.started, 1);
    dluma_log_line("INFO", "Editor Git view registered.");
    return 1;
}

void dluma_editor_source_control_host_options_layout(HWND page, int top_dip)
{
    typedef UINT (WINAPI *DpiFn)(HWND);
    DpiFn get_dpi;
    RECT rect;
    UINT dpi = 96U;
    int x, y, width, height;
    if (page != git_ui.options_page || !own_window(page) || !git_ui.options_button || !GetClientRect(page, &rect)) return;
    get_dpi = (DpiFn)(ULONG_PTR)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (get_dpi) dpi = get_dpi(page);
    if (!dpi || dpi > 768U) dpi = 96U;
    x = MulDiv(14, (int)dpi, 96); y = MulDiv(top_dip, (int)dpi, 96);
    width = MulDiv(132, (int)dpi, 96); height = MulDiv(28, (int)dpi, 96);
    if (width > rect.right - 2*x) width = rect.right - 2*x;
    if (width < 64 || y + height > rect.bottom) { ShowWindow(git_ui.options_button, SW_HIDE); return; }
    (void)SetWindowPos(git_ui.options_button, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(git_ui.options_button, SW_SHOWNA);
}

int dluma_editor_source_control_host_options_attach(HWND page, HWND dialog)
{
    HWND main;
    if (!git_ui.started || git_ui.stopping || git_ui.options_page || !own_window(page) ||
        !own_window(dialog) || GetParent(page) != dialog) return 0;
    main = GetAncestor(dialog, GA_ROOTOWNER);
    if (!dluma_editor_is_main_window(main, GetCurrentProcessId())) return 0;
    git_ui.options_button = CreateWindowExW(0, L"BUTTON", L"Open Git", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 1, 1, page, (HMENU)(UINT_PTR)GIT_OPTIONS_BUTTON, GetModuleHandleW(NULL), NULL);
    if (!git_ui.options_button) return 0;
    git_ui.options_page = page; git_ui.options_dialog = dialog;
    git_ui.main = main; git_ui.thread = GetCurrentThreadId();
    (void)SendMessageW(git_ui.options_button, WM_SETFONT, SendMessageW(page, WM_GETFONT, 0, 0), FALSE);
    (void)dluma_editor_ui_request_enroll(page);
    (void)dluma_editor_ui_request_enroll(git_ui.options_button);
    dluma_editor_source_control_host_options_layout(page, 146);
    return 1;
}

void dluma_editor_source_control_host_options_detach(HWND page)
{
    if (page != git_ui.options_page || !own_window(page)) return;
    if (git_ui.options_button && IsWindow(git_ui.options_button)) (void)DestroyWindow(git_ui.options_button);
    git_ui.options_page = git_ui.options_dialog = git_ui.options_button = NULL;
}

int dluma_editor_source_control_host_stop(void)
{
    LONG owner;
    if (!git_ui.started) return 1;
    InterlockedExchange(&git_ui.stopping, 1);
    owner = InterlockedCompareExchange(&git_ui.thread, 0, 0);
    if (owner && owner != (LONG)GetCurrentThreadId()) {
        if (git_ui.main) (void)PostMessageW(git_ui.main, git_stop_message, 0, 0);
        return 0;
    }
    /* Once the owner has handed off, wait for broker callbacks before touching
     * the remaining handle-only state from the shutdown thread. */
    if (!owner && !dluma_editor_ui_unsubscribe(DLUMA_EDITOR_UI_OWNER_SOURCE_CONTROL)) return 0;
    if (!close_session()) return 0;
    if (owner && git_ui.options_page) dluma_editor_source_control_host_options_detach(git_ui.options_page);
    if (owner && git_ui.panel.token && IsWindow(git_ui.panel.panel_window) && !dluma_editor_panel_host_close(&git_ui.panel)) return 0;
    ZeroMemory(&git_ui.panel, sizeof(git_ui.panel));
    if (owner && git_ui.main) (void)KillTimer(git_ui.main, GIT_TIMER);
    if (owner) {
        git_ui.main = NULL;
        git_ui.dock.content = git_ui.dock.pane = NULL;
        InterlockedExchange(&git_ui.thread, 0);
        if (!dluma_editor_ui_unsubscribe(DLUMA_EDITOR_UI_OWNER_SOURCE_CONTROL)) return 0;
    }
#if defined(DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC) && DLUMA_EDITOR_GIT_LOCAL_DIAGNOSTIC
    git_diagnostic_close();
#endif
    InterlockedExchange(&git_ui.started, 0); return 1;
}

int dluma_editor_source_control_host_callbacks_drained(void)
{
    return !git_ui.started && !git_ui.callbacks && dluma_editor_git_session_drained();
}
