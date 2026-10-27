#include "dlw_common.h"
#include "dlw_frontend_error.h"

/* Built-in Win32 controls run independently of the game's native frontend.
 * The loader's existing window-message owner creates/subclasses this panel. */
#define DLUMA_ERROR_COPY_ID 0x6E31U
#define DLUMA_ERROR_QUIT_ID 0x6E32U
#define DLUMA_ERROR_CHILD_VISIBLE 0x50000000UL
#define DLUMA_ERROR_TABSTOP 0x00010000UL
#define DLUMA_ERROR_NCDESTROY 0x0082U
#define DLUMA_ERROR_MOVEABLE 0x0002U
#define DLUMA_ERROR_UNICODETEXT 13U

__declspec(dllimport) HWND WINAPI CreateWindowExW(DWORD, LPCWSTR,
    LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
__declspec(dllimport) BOOL WINAPI DestroyWindow(HWND);
__declspec(dllimport) BOOL WINAPI ShowWindow(HWND, int);
__declspec(dllimport) BOOL WINAPI SetForegroundWindow(HWND);
__declspec(dllimport) BOOL WINAPI GetWindowRect(HWND, RECT *);
__declspec(dllimport) BOOL WINAPI SetWindowTextW(HWND, LPCWSTR);
__declspec(dllimport) BOOL WINAPI PostMessageW(HWND, UINT, WPARAM, LPARAM);
__declspec(dllimport) DWORD WINAPI GetWindowThreadProcessId(HWND, DWORD *);
__declspec(dllimport) DWORD WINAPI GetCurrentProcessId(void);
__declspec(dllimport) HANDLE WINAPI GlobalAlloc(UINT, SIZE_T);
__declspec(dllimport) LPVOID WINAPI GlobalLock(HANDLE);
__declspec(dllimport) BOOL WINAPI GlobalUnlock(HANDLE);
__declspec(dllimport) HANDLE WINAPI GlobalFree(HANDLE);
__declspec(dllimport) BOOL WINAPI OpenClipboard(HWND);
__declspec(dllimport) BOOL WINAPI EmptyClipboard(void);
__declspec(dllimport) HANDLE WINAPI SetClipboardData(UINT, HANDLE);
__declspec(dllimport) BOOL WINAPI CloseClipboard(void);

static DlumaFrontendErrorState g_error;
/* The first producer is the sole writer. Publish only after the bounded POD
 * copy is complete; snapshots never wait for a hook producer or replace it. */
static volatile LONG g_error_reason_ready;
static volatile LONG g_error_reason_logged;
static volatile LONG g_error_active;
static volatile LONG g_error_stopping;
static volatile LONG g_error_blocked;
static volatile LONG g_error_reason_capture_failures;
static volatile LONG g_error_queue_failures;
static volatile LONG g_error_copy_requests;
static volatile LONG g_error_quit_requested;
static volatile LONG g_error_quit_delivered;
static volatile LONG g_error_presentation_attempted;
static volatile LONG g_error_presented;
static volatile LONGLONG g_error_epoch;
static PVOID volatile g_error_owner;
static HWND g_error_panel;
static HWND g_error_status;
static WNDPROC g_error_original;

int dluma_frontend_error_snapshot(DlumaFrontendErrorState *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (dluma_atomic_compare_exchange(&g_error_reason_ready, 0, 0)) {
        *out = g_error;
    }
    out->blocked = dluma_atomic_compare_exchange(&g_error_blocked, 0, 0) != 0;
    out->epoch = (unsigned long long)_InterlockedCompareExchange64(&g_error_epoch,0,0);
    out->copy_requests = (unsigned int)dluma_atomic_compare_exchange(&g_error_copy_requests, 0, 0);
    out->quit_requested = dluma_atomic_compare_exchange(&g_error_quit_requested, 0, 0) != 0;
    out->quit_delivered = dluma_atomic_compare_exchange(&g_error_quit_delivered, 0, 0) != 0;
    out->reason_capture_failures = (unsigned int)dluma_atomic_compare_exchange(&g_error_reason_capture_failures, 0, 0);
    out->presentation_queue_failures = (unsigned int)dluma_atomic_compare_exchange(&g_error_queue_failures, 0, 0);
    out->presentation_attempted = dluma_atomic_compare_exchange(&g_error_presentation_attempted, 0, 0) != 0;
    out->presented = dluma_atomic_compare_exchange(&g_error_presented, 0, 0) != 0;
    if (out->blocked && !out->reason[0]) {
        out->reason_capture_failures = (unsigned int)dluma_atomic_increment(&g_error_reason_capture_failures);
        dluma_str_copy(out->reason, DLUMA_ARRAY_COUNT(out->reason),
            "The first blocker is still being captured. Player remains open and project launches remain blocked.");
    }
    return out->blocked != 0;
}

int dluma_frontend_error_blocks_project_launch(void)
{
    return dluma_atomic_compare_exchange(&g_error_blocked, 0, 0) != 0;
}

void dluma_frontend_error_bind_owner_window(HWND window)
{
    if (!g_error_stopping) dluma_atomic_pointer_exchange(&g_error_owner, window);
}

static int error_is_owner(HWND window)
{
    DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(window, &pid);
    return window && window == (HWND)dluma_atomic_pointer_compare_exchange(&g_error_owner, 0, 0) &&
        tid == GetCurrentThreadId() && pid == GetCurrentProcessId();
}

int dluma_frontend_error_present(const char *reason, unsigned long long epoch)
{
    HWND owner;
    int first;
    if (!reason || !reason[0]) return 0;
    first = dluma_atomic_compare_exchange(&g_error_blocked, 1, 0) == 0;
    if (first) {
        _InterlockedExchange64(&g_error_epoch,(LONGLONG)epoch);
        (void)dluma_frontend_error_latch(&g_error, reason, epoch);
        dluma_atomic_exchange(&g_error_reason_ready, 1);
    }
    owner = (HWND)dluma_atomic_pointer_compare_exchange(&g_error_owner, 0, 0);
    if (g_error_stopping || !owner ||
        !PostMessageW(owner, DLUMA_FRONTEND_ERROR_PRESENT_MESSAGE, 0, 0)) {
        dluma_atomic_increment(&g_error_queue_failures);
        return 0;
    }
    return 1;
}

static void error_diagnostics(WCHAR *out, unsigned int capacity)
{
    DlumaFrontendErrorState state;
    char text[2304];
    char number[32];
    char path[DLUMA_MAX_WPATH * 3];
    if (!out || !capacity) return;
    out[0] = 0;
    (void)dluma_frontend_error_snapshot(&state);
    text[0] = 0;
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text),
        "DLUMA loader\r\n"
        "Project exit is blocked. Player was not automatically closed.\r\n"
        "New projects are blocked until Player is restarted.\r\n\r\nBlocker: ");
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), state.reason);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "\r\nEpoch: ");
    dluma_u64_to_text(state.epoch, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), number);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "\r\nReason snapshot contention: ");
    dluma_u64_to_text(state.reason_capture_failures, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), number);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "\r\nPresentation queue failures: ");
    dluma_u64_to_text(state.presentation_queue_failures, number, DLUMA_ARRAY_COUNT(number));
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), number);
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), "\r\nLoader: " DLUMA_VERSION "\r\nLog: ");
    path[0] = 0;
    (void)dluma_wide_to_utf8(g_dluma.log_path, path, DLUMA_ARRAY_COUNT(path));
    dluma_str_append(text, DLUMA_ARRAY_COUNT(text), path);
    (void)dluma_utf8_to_wide(text, out, capacity);
}

static int error_copy(HWND panel)
{
    WCHAR text[2304];
    unsigned int length = 0;
    HANDLE storage;
    void *destination;
    int copied = 0;
    error_diagnostics(text, DLUMA_ARRAY_COUNT(text));
    while (text[length]) ++length;
    storage = GlobalAlloc(DLUMA_ERROR_MOVEABLE, (length + 1U) * sizeof(WCHAR));
    if (!storage) return 0;
    destination = GlobalLock(storage);
    if (!destination) { GlobalFree(storage); return 0; }
    memcpy(destination, text, (length + 1U) * sizeof(WCHAR));
    GlobalUnlock(storage);
    if (OpenClipboard(panel)) {
        if (EmptyClipboard() && SetClipboardData(DLUMA_ERROR_UNICODETEXT, storage)) copied = 1;
        CloseClipboard();
    }
    if (!copied) GlobalFree(storage);
    return copied;
}

static LRESULT CALLBACK error_window_proc(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam)
{
    LRESULT result = 0;
    WNDPROC original;
    dluma_atomic_increment(&g_error_active);
    original = g_error_original;
    if (message == WM_COMMAND && ((wparam >> 16) & 0xffffU) == 0U &&
        window == g_error_panel && !g_error_stopping) {
        unsigned int id = (unsigned int)(wparam & 0xffffU);
        if (id == DLUMA_ERROR_COPY_ID) {
            int copied;
            dluma_atomic_increment(&g_error_copy_requests);
            copied = error_copy(window);
            SetWindowTextW(g_error_status, copied ? L"Diagnostics copied." : L"Clipboard unavailable. The blocker is also recorded in the loader log.");
            goto done;
        }
        if (id == DLUMA_ERROR_QUIT_ID) {
            int requested;
            requested = dluma_atomic_compare_exchange(&g_error_quit_requested, 1, 0) == 0;
            if (requested && !PostMessageW((HWND)dluma_atomic_pointer_compare_exchange(&g_error_owner, 0, 0),
                DLUMA_FRONTEND_ERROR_QUIT_MESSAGE, 0, 0)) {
                dluma_atomic_exchange(&g_error_quit_requested, 0);
                SetWindowTextW(g_error_status, L"Quit could not be delivered. Player remains open; use its window close button.");
            }
            goto done;
        }
    }
    if (message == WM_CLOSE && window == g_error_panel) {
        /* X closes only this loader-owned panel, never the Player process. */
        DestroyWindow(window);
        goto done;
    }
    if (message == DLUMA_ERROR_NCDESTROY && window == g_error_panel) {
        g_error_panel = 0;
        g_error_status = 0;
        dluma_atomic_exchange(&g_error_presented, 0);
    }
    if (original) result = CallWindowProcW(original, window, message, wparam, lparam);
done:
    dluma_atomic_decrement(&g_error_active);
    return result;
}

void dluma_frontend_error_present_on_owner(HWND window)
{
    HWND panel = 0;
    HWND content;
    HWND copy;
    HWND quit;
    RECT rect;
    WCHAR text[2304];
    int x = 100;
    int y = 100;
    dluma_atomic_increment(&g_error_active);
    if (g_error_stopping || !error_is_owner(window) ||
        !dluma_frontend_error_blocks_project_launch()) goto done;
    /* A concurrent second producer can post before the first copy finishes.
     * That first producer posts again after publication; never wait here. */
    if (!dluma_atomic_compare_exchange(&g_error_reason_ready, 0, 0)) goto done;
    if (!dluma_atomic_compare_exchange(&g_error_reason_logged, 1, 0))
        dluma_log_pair("ERROR", "frontend_exit_blocker", g_error.reason);
    dluma_atomic_exchange(&g_error_presentation_attempted, 1);
    if (g_error_panel) { ShowWindow(g_error_panel, 5); goto done; }
    if (GetWindowRect(window, &rect)) {
        x = rect.left + ((rect.right-rect.left)-680)/2;
        y = rect.top + ((rect.bottom-rect.top)-340)/2;
    }
    error_diagnostics(text, DLUMA_ARRAY_COUNT(text));
    panel = CreateWindowExW(0x00000001UL, L"STATIC", L"Project exit needs attention",
        0x80C80000UL, x, y, 680, 340, window, 0, g_dluma.self_module, 0);
    if (!panel) goto failed;
    g_error_original = (WNDPROC)GetWindowLongPtrW(panel, GWLP_WNDPROC);
    if (!g_error_original || !SetWindowLongPtrW(panel, GWLP_WNDPROC, (LONG_PTR)error_window_proc)) goto failed;
    g_error_panel = panel;
    content = CreateWindowExW(0x200UL, L"EDIT", text,
        DLUMA_ERROR_CHILD_VISIBLE | 0x00200844UL, 16, 16, 634, 210,
        panel, 0, g_dluma.self_module, 0);
    copy = CreateWindowExW(0, L"BUTTON", L"Copy diagnostics",
        DLUMA_ERROR_CHILD_VISIBLE | DLUMA_ERROR_TABSTOP, 16, 240, 160, 32,
        panel, (HMENU)(UINT_PTR)DLUMA_ERROR_COPY_ID, g_dluma.self_module, 0);
    quit = CreateWindowExW(0, L"BUTTON", L"Quit Player",
        DLUMA_ERROR_CHILD_VISIBLE | DLUMA_ERROR_TABSTOP, 492, 240, 158, 32,
        panel, (HMENU)(UINT_PTR)DLUMA_ERROR_QUIT_ID, g_dluma.self_module, 0);
    g_error_status = CreateWindowExW(0, L"STATIC", L"Closing this panel keeps Player open and project launches blocked.",
        DLUMA_ERROR_CHILD_VISIBLE, 16, 281, 634, 20, panel, 0, g_dluma.self_module, 0);
    if (!content || !copy || !quit || !g_error_status || g_error_stopping) goto failed;
    ShowWindow(panel, 5);
    SetForegroundWindow(panel);
    dluma_atomic_exchange(&g_error_presented, 1);
    goto done;
failed:
    dluma_atomic_exchange(&g_error_presented, 0);
    if (panel) DestroyWindow(panel);
    dluma_log_line("ERROR", "Loader-owned failure panel creation failed; no automatic quit was requested.");
done:
    dluma_atomic_decrement(&g_error_active);
}

int dluma_frontend_error_consume_user_quit(HWND window)
{
    if (!error_is_owner(window)) return 0;
    if (!dluma_frontend_error_blocks_project_launch() ||
        !dluma_atomic_compare_exchange(&g_error_quit_requested, 0, 0)) return 0;
    return dluma_atomic_compare_exchange(&g_error_quit_delivered, 1, 0) == 0;
}

int dluma_frontend_error_stop(void)
{
    HWND panel;
    WNDPROC original;
    dluma_atomic_exchange(&g_error_stopping, 1);
    panel = g_error_panel;
    original = g_error_original;
    if (panel && original) {
        WNDPROC observed = (WNDPROC)GetWindowLongPtrW(panel, GWLP_WNDPROC);
        if (observed == error_window_proc) {
            if ((WNDPROC)SetWindowLongPtrW(panel, GWLP_WNDPROC, (LONG_PTR)original) != observed) return 0;
        } else if (observed && observed != original) return 0;
        /* The restored built-in class handles this message without calling
         * loader code, even when shutdown is on another owner thread. */
        PostMessageW(panel, WM_CLOSE, 0, 0);
        g_error_panel = 0;
        g_error_status = 0;
        dluma_atomic_exchange(&g_error_presented, 0);
    }
    return dluma_atomic_compare_exchange(&g_error_active, 0, 0) == 0;
}
