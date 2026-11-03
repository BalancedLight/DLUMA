#include "dlw_editor_exit_observer.h"
#include "dlw_common.h"
#include "dlw_log.h"
#include "dlw_editor_ui.h"
#include "dlw_editor_host.h"

typedef struct DlumaEditorWindowCandidate {
    DWORD process_id;
    HWND window;
    ULONGLONG area;
} DlumaEditorWindowCandidate;

typedef struct DlumaEditorExitObserver {
    volatile LONG start_state;
    volatile LONG snapshot_sequence;
    volatile LONG snapshot_active;
    HWND window;
    DlumaEditorExitIntentSnapshot state;
    DlumaEditorExitIntentSnapshot snapshots[2];
} DlumaEditorExitObserver;

static DlumaEditorExitObserver g_dluma_editor_exit_observer;

#define DLUMA_SMTO_BLOCK 0x0001U
#define DLUMA_SMTO_ABORTIFHUNG 0x0002U
#define DLUMA_SMTO_ERRORONEXIT 0x0020U

static void dluma_editor_exit_observer_publish(void)
{
    LONG active = dluma_atomic_compare_exchange(
        &g_dluma_editor_exit_observer.snapshot_active, 0, 0);
    LONG next = active ? 0 : 1;
    dluma_atomic_increment(&g_dluma_editor_exit_observer.snapshot_sequence);
    g_dluma_editor_exit_observer.snapshots[next] =
        g_dluma_editor_exit_observer.state;
    dluma_atomic_exchange(
        &g_dluma_editor_exit_observer.snapshot_active, next);
    dluma_atomic_increment(&g_dluma_editor_exit_observer.snapshot_sequence);
}

int dluma_editor_exit_observer_copy_snapshot(
    DlumaEditorExitIntentSnapshot* snapshot_out)
{
    unsigned int attempt;
    if (!snapshot_out) return 0;
    for (attempt = 0; attempt < 2U; ++attempt) {
        LONG before = dluma_atomic_compare_exchange(
            &g_dluma_editor_exit_observer.snapshot_sequence, 0, 0);
        LONG active;
        LONG after;
        if (before & 1) continue;
        active = dluma_atomic_compare_exchange(
            &g_dluma_editor_exit_observer.snapshot_active, 0, 0);
        *snapshot_out =
            g_dluma_editor_exit_observer.snapshots[active ? 1 : 0];
        after = dluma_atomic_compare_exchange(
            &g_dluma_editor_exit_observer.snapshot_sequence, 0, 0);
        if (before == after && !(after & 1)) return 1;
    }
    dluma_editor_exit_intent_initialize(snapshot_out, 0U);
    return 0;
}

int dluma_editor_is_main_window(HWND window, DWORD expected_process_id)
{
    DWORD process_id = 0;
    LONG_PTR style;
    WCHAR title[256];
    WCHAR class_name[128];
    if (!window || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) ||
        !GetWindowThreadProcessId(window, &process_id) ||
        process_id != expected_process_id)
        return 0;
    style = GetWindowLongPtrW(window, GWL_STYLE);
    if ((style & WS_CAPTION) != WS_CAPTION || !(style & WS_SYSMENU) ||
        !(style & WS_THICKFRAME) ||
        (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) ||
        GetClassNameW(window, class_name, DLUMA_ARRAY_COUNT(class_name)) < 4 ||
        class_name[0] != L'A' || class_name[1] != L'f' ||
        class_name[2] != L'x' || class_name[3] != L':' ||
        !GetWindowTextW(window, title, DLUMA_ARRAY_COUNT(title)))
        return 0;
    return dluma_wcontains_icase(title, L"Dying Light Developer Tools");
}

static BOOL CALLBACK dluma_editor_exit_find_window(
    HWND window, LPARAM parameter)
{
    DlumaEditorWindowCandidate* candidate =
        (DlumaEditorWindowCandidate*)parameter;
    DWORD process_id = 0;
    RECT rect;
    ULONGLONG width;
    ULONGLONG height;
    ULONGLONG area;
    if (!candidate ||
        !dluma_editor_is_main_window(window, candidate->process_id))
        return TRUE;
    if (!GetWindowThreadProcessId(window, &process_id) ||
        process_id != candidate->process_id ||
        !GetWindowRect(window, &rect) ||
        rect.right <= rect.left || rect.bottom <= rect.top)
        return TRUE;
    width = (ULONGLONG)(rect.right - rect.left);
    height = (ULONGLONG)(rect.bottom - rect.top);
    area = width * height;
    if (!candidate->window || area > candidate->area) {
        candidate->window = window;
        candidate->area = area;
    }
    return TRUE;
}

static int dluma_editor_exit_listener(
    HWND window, unsigned int stage, UINT message, WPARAM wparam,
    LPARAM lparam, LRESULT *result, void *context)
{
    uint64_t *generation;
    (void)lparam; (void)result; (void)context;
    if (!g_dluma_editor_exit_observer.window &&
        stage == DLUMA_EDITOR_UI_BEFORE &&
        (message == DLUMA_EDITOR_UI_ENROLLED || message == DLUMA_EDITOR_UI_REFRESH ||
         message == 0x0018U) &&
        dluma_editor_is_main_window(window, GetCurrentProcessId())) {
        g_dluma_editor_exit_observer.window = window;
        g_dluma_editor_exit_observer.state.observer_active = 1;
        dluma_editor_exit_observer_publish();
    }
    if (window != g_dluma_editor_exit_observer.window) return 0;
    generation = dluma_editor_ui_invocation_value(DLUMA_EDITOR_UI_OWNER_EXIT);
    if (stage == DLUMA_EDITOR_UI_DESTROY) {
        g_dluma_editor_exit_observer.window = 0;
        if (!message) g_dluma_editor_exit_observer.state.observer_active = 0;
        dluma_editor_exit_observer_publish();
        if (message == WM_NCDESTROY) dluma_editor_host_request_shutdown();
        return 0;
    }
    if (stage == DLUMA_EDITOR_UI_AFTER) {
        if (generation && *generation &&
            (message == WM_CLOSE || message == WM_SYSCOMMAND)) {
            dluma_editor_exit_intent_resolve(
                &g_dluma_editor_exit_observer.state, *generation,
                !IsWindow(window) || !IsWindowVisible(window));
            dluma_editor_exit_observer_publish();
        }
        return 0;
    }
    if (stage != DLUMA_EDITOR_UI_BEFORE) return 0;
    if (message == WM_SYSCOMMAND &&
        ((DWORD)wparam & 0xFFF0UL) == SC_CLOSE) {
        const int alt_f4 =
            (GetAsyncKeyState(VK_MENU) & 0x8000) != 0 &&
            (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
        ULONGLONG close_generation = dluma_editor_exit_intent_begin(
            &g_dluma_editor_exit_observer.state,
            alt_f4 ? DLUMA_EDITOR_EXIT_REASON_ALT_F4
                   : DLUMA_EDITOR_EXIT_REASON_SYSTEM_MENU_CLOSE,
            GetTickCount64());
        if (generation) *generation = close_generation;
        dluma_editor_exit_observer_publish();
    } else if (message == WM_CLOSE) {
        ULONGLONG close_generation = dluma_editor_exit_intent_begin(
            &g_dluma_editor_exit_observer.state,
            DLUMA_EDITOR_EXIT_REASON_WINDOW_CLOSE,
            GetTickCount64());
        if (generation) *generation = close_generation;
        dluma_editor_exit_observer_publish();
    } else if (message == WM_ENDSESSION && wparam) {
        dluma_editor_exit_intent_commit(
            &g_dluma_editor_exit_observer.state,
            DLUMA_EDITOR_EXIT_REASON_SYSTEM_SESSION_END,
            GetTickCount64());
        dluma_editor_exit_observer_publish();
    } else if (message == WM_DESTROY || message == WM_NCDESTROY) {
        /* Main-window destruction is accepted shutdown, including File/Exit
         * paths that bypass WM_CLOSE. Commit before stock teardown can fault. */
        dluma_editor_exit_intent_commit(
            &g_dluma_editor_exit_observer.state,
            DLUMA_EDITOR_EXIT_REASON_WINDOW_DESTROYED, GetTickCount64());
        dluma_editor_exit_observer_publish();
    }

    return 0;
}

int dluma_editor_exit_observer_dispatch_command(unsigned int command_id)
{
    HWND window = g_dluma_editor_exit_observer.window;
    DWORD process_id = 0;
    ULONG_PTR result = 0;
    if (dluma_atomic_compare_exchange(
            &g_dluma_editor_exit_observer.start_state, 0, 0) != 2 ||
        !window || command_id > 0xffffU ||
        !IsWindow(window) || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) ||
        !GetWindowThreadProcessId(window, &process_id) ||
        process_id != GetCurrentProcessId())
        return 0;
    return SendMessageTimeoutW(
               window, WM_COMMAND, (WPARAM)command_id, 0,
               DLUMA_SMTO_BLOCK | DLUMA_SMTO_ABORTIFHUNG |
                   DLUMA_SMTO_ERRORONEXIT,
               10000U, &result) != 0;
}
int dluma_editor_exit_observer_start(void)
{
    DlumaEditorWindowCandidate candidate;
    LONG state = dluma_atomic_compare_exchange(
        &g_dluma_editor_exit_observer.start_state, 1, 0);
    if (state != 0)
        return dluma_atomic_compare_exchange(
                   &g_dluma_editor_exit_observer.start_state, 0, 0) == 2;
    memset(&candidate, 0, sizeof(candidate));
    candidate.process_id = GetCurrentProcessId();
    dluma_editor_exit_intent_initialize(
        &g_dluma_editor_exit_observer.state, 0U);
    dluma_editor_exit_observer_publish();
    if (!dluma_editor_ui_subscribe(DLUMA_EDITOR_UI_OWNER_EXIT,
                                    dluma_editor_exit_listener, 0)) {
        dluma_atomic_exchange(&g_dluma_editor_exit_observer.start_state, 0);
        return 0;
    }
    (void)EnumWindows(dluma_editor_exit_find_window, (LPARAM)&candidate);
    if (candidate.window) (void)dluma_editor_ui_request_enroll(candidate.window);
    dluma_atomic_exchange(&g_dluma_editor_exit_observer.start_state, 2);
    dluma_log_line(
        "INFO",
        "Editor process-lifetime exit observer installed; shutdown crash dialogs will be suppressed while diagnostics remain active.");
    return 1;
}
