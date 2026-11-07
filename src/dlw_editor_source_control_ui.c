#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "dlw_editor_source_control_ui.h"
#include "dlw_editor_git_session.h"
#include "dlw_editor_theme_profile.h"
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
    uint64_t generation;
    int session, closing;
} git_ui;
static UINT git_stop_message;

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

static DlumaEditorGitRequestResult request(const char *type, const char *payload,
    size_t size, char *output, size_t capacity, size_t *used, void *context)
{
    DlumaEditorGitParsedRequest *parsed;
    DlumaEditorGitJobSnapshotV1 job;
    HANDLE child = NULL;
    int current, ok = 0, count;
    unsigned int i;
    WCHAR *root;
    (void)context;
    *used = 0;
    if (capacity < 512U) return DLUMA_EDITOR_GIT_REQUEST_ERROR;
    parsed = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*parsed));
    if (!parsed) goto failed;
    if (!dluma_editor_source_control_parse_request(type, payload, size, parsed)) goto done;
    current = !git_ui.closing && !git_ui.stopping && project_current();
    if (parsed->kind == DLUMA_GIT_STATE) {
        root = HeapAlloc(GetProcessHeap(), 0, GIT_PATH_CAP * sizeof(WCHAR));
        if (!root) goto done;
        current = current && parsed->project_generation == git_ui.generation &&
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, parsed->project_root, -1, root, GIT_PATH_CAP) &&
            !lstrcmpiW(root, git_ui.root);
        HeapFree(GetProcessHeap(), 0, root);
        if (current && parsed->has_executable) current = pin_executable(parsed->git_executable);
        count = wsprintfA(output, "{\"projectRoot\":");
        if (count < 0) goto done;
        *used = (size_t)count;
        if (!json_root(output, capacity, used)) goto done;
        count = wsprintfA(output + *used,
            ",\"projectGeneration\":%I64u,\"enabled\":%s,\"mutationsAvailable\":false,\"worktreeAvailable\":false,\"message\":\"%s\"}",
            (unsigned long long)git_ui.generation, current ? "true" : "false",
            current ? "Git status and history are available. Write operations are not available yet." :
                "The project or Git executable is unavailable. Reopen Git to try again.");
        if (count >= 0) { *used += (size_t)count; ok = 1; }
    } else if (parsed->kind == DLUMA_GIT_CHILD_STARTED) {
        ZeroMemory(&job, sizeof(job)); job.size = sizeof(job); job.version = 1;
        reap_children();
        for (i = 0; i < 32U && git_ui.children[i]; ++i) {}
        if (current && !parsed->mutating && !parsed->has_lease && i < 32U &&
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
        count = wsprintfA(output,
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
    if (!dluma_editor_git_session_close() || !dluma_editor_git_session_drained()) return 0;
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
    git_stop_message = RegisterWindowMessageW(L"DLUMA.EditorGit.Stop.1");
    if (!git_stop_message || !dluma_editor_ui_subscribe(DLUMA_EDITOR_UI_OWNER_SOURCE_CONTROL, on_window, NULL)) return 0;
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
    InterlockedExchange(&git_ui.started, 0); return 1;
}

int dluma_editor_source_control_host_callbacks_drained(void)
{
    return !git_ui.started && !git_ui.callbacks && dluma_editor_git_session_drained();
}
