#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include "dlw_editor_panel_host.h"

typedef struct PanelState PanelState;
typedef struct PanelWait {
    HWND window;
    uint64_t token, generation;
    HANDLE process, registration, completion;
} PanelWait;
struct PanelState {
    DlumaEditorPanelRef ref;
    HWND owner, source, tabs, child;
    DWORD thread_id, child_pid;
    RECT stock, applied;
    HFONT font;
    DlumaEditorPanelNotify notify;
    void *context;
    PanelWait *wait;
    uint64_t binding_generation;
    LONG references;
    int visible, owns_hide, applied_valid, closing, layout_depth;
};
static const WCHAR panel_class[] = L"DLUMA.EditorPanel.Host.1";
static const WCHAR owner_property[] = L"DLUMA.EditorPanel.Owner.1";
static const WCHAR source_property[] = L"DLUMA.EditorPanel.Source.1";
static const WCHAR panel_property[] = L"DLUMA.EditorPanel.Window.1";
static const WCHAR exit_name[] = L"DLUMA.EditorPanel.ChildExit.1";
static const WCHAR focus_name[] = DLUMA_EDITOR_PANEL_FOCUS_MESSAGE_NAME;
static SRWLOCK class_lock;
static HINSTANCE module_instance;
static UINT exit_message, focus_message;
static volatile LONG64 next_token;
static volatile LONG pending_waits;
static LRESULT CALLBACK panel_proc(HWND, UINT, WPARAM, LPARAM);
static int set_visible(PanelState *, int);
static void focus_child(PanelState *);

static void panel_addref(PanelState *p) { ++p->references; }
static void panel_release(PanelState *p)
{
    if (!--p->references) HeapFree(GetProcessHeap(), 0, p);
}
static void wait_release(PanelWait *w)
{
    if (w->completion) CloseHandle(w->completion);
    if (w->process) CloseHandle(w->process);
    HeapFree(GetProcessHeap(), 0, w);
}
static DWORD WINAPI finish_wait(PVOID context)
{
    PanelWait *w = (PanelWait *)context;
    if (WaitForSingleObject(w->completion, INFINITE) == WAIT_OBJECT_0) {
        wait_release(w);
        InterlockedDecrement(&pending_waits);
    }
    return 0;
}
static VOID CALLBACK child_exit(PVOID context, BOOLEAN timed_out)
{
    PanelWait *w = (PanelWait *)context;
    if (!timed_out) {
        (void)PostMessageW(w->window, exit_message, (WPARAM)w->token, (LPARAM)w->generation);
    }
}
static void cancel_wait(PanelState *p)
{
    PanelWait *w = p->wait;
    p->wait = NULL; p->child = NULL; p->child_pid = 0;
    if (!w) return;
    w->completion = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (w->completion) {
        BOOL removed = UnregisterWaitEx(w->registration, w->completion);
        DWORD error = removed ? ERROR_SUCCESS : GetLastError();
        if (removed || error == ERROR_IO_PENDING) {
            if (!QueueUserWorkItem(finish_wait, w, WT_EXECUTELONGFUNCTION)) {
                HANDLE worker = CreateThread(NULL, 0, finish_wait, w, 0, NULL);
                if (worker) CloseHandle(worker);
            }
            return;
        }
    }
    /* On cancellation/worker failure retain the context; drain stays false. */
}
static uint64_t next_binding(PanelState *p)
{
    if (!++p->binding_generation) ++p->binding_generation;
    return p->binding_generation;
}
static void notify_panel(PanelState *p, unsigned int event, unsigned int argument)
{
    DlumaEditorPanelNotify callback = p->notify;
    DlumaEditorPanelRef snapshot = p->ref;
    void *context = p->context;
    if (!callback) return;
    panel_addref(p); callback(&snapshot, event, argument, context); panel_release(p);
}
static int owner_thread(HWND w)
{
    DWORD pid = 0;
    return IsWindow(w) && GetWindowThreadProcessId(w, &pid) == GetCurrentThreadId() && pid == GetCurrentProcessId();
}
static PanelState *find_panel(const DlumaEditorPanelRef *ref)
{
    PanelState *p;
    if (!ref || ref->size != sizeof(*ref) || ref->version != 1 || !ref->token || !owner_thread(ref->panel_window)) return NULL;
    p = (PanelState *)GetPropW(ref->panel_window, panel_property);
    if (!p || (PanelState *)GetWindowLongPtrW(ref->panel_window, GWLP_USERDATA) != p) return NULL;
    if (!p || p->closing || p->ref.token != ref->token || p->ref.panel_window != ref->panel_window ||
        p->ref.content_window != ref->content_window || GetPropW(p->owner, owner_property) != p) return NULL;
    panel_addref(p);
    return p;
}
static int panel_class_ready(void)
{
    WNDCLASSEXW wc;
    INITCOMMONCONTROLSEX controls;
    HMODULE common;
    typedef BOOL (WINAPI *InitCommonFn)(const INITCOMMONCONTROLSEX *);
    InitCommonFn initialize;
    int ok = 0;
    AcquireSRWLockExclusive(&class_lock);
    if (module_instance && exit_message && focus_message) { ok = 1; goto done; }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(ULONG_PTR)panel_proc, (HMODULE *)&module_instance)) goto done;
    common = GetModuleHandleW(L"comctl32.dll");
    initialize = common ? (InitCommonFn)(void *)GetProcAddress(common, "InitCommonControlsEx") : NULL;
    controls.dwSize = sizeof(controls); controls.dwICC = ICC_TAB_CLASSES;
    if (!initialize || !initialize(&controls)) goto done;
    ZeroMemory(&wc, sizeof(wc)); wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = panel_proc; wc.hInstance = module_instance; wc.lpszClassName = panel_class;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    if (!RegisterClassExW(&wc)) {
        WNDCLASSEXW existing; ZeroMemory(&existing, sizeof(existing)); existing.cbSize = sizeof(existing);
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS || !GetClassInfoExW(module_instance, panel_class, &existing) ||
            existing.lpfnWndProc != panel_proc || existing.hInstance != module_instance) goto done;
    }
    exit_message = RegisterWindowMessageW(exit_name);
    focus_message = RegisterWindowMessageW(focus_name);
    ok = exit_message && focus_message;
done:
    if (!ok) { exit_message = focus_message = 0; module_instance = NULL; }
    ReleaseSRWLockExclusive(&class_lock);
    return ok;
}
static int source_rect(PanelState *p, RECT *rect)
{
    if (!owner_thread(p->source) || GetPropW(p->owner, owner_property) != p ||
        GetPropW(p->source, source_property) != p || GetParent(p->source) != p->owner || !GetWindowRect(p->source, rect)) return 0;
    (void)MapWindowPoints(NULL, p->owner, (POINT *)rect, 2);
    return 1;
}
static int same_rect(const RECT *a, const RECT *b)
{
    return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
}
static int valid_child(PanelState *p)
{
    DWORD pid = 0;
    return p->child && IsWindow(p->child) && GetParent(p->child) == p->ref.content_window &&
        (GetWindowLongPtrW(p->child, GWL_STYLE) & WS_CHILD) &&
        GetWindowThreadProcessId(p->child, &pid) && pid == p->child_pid;
}
static int tab_height(PanelState *p)
{
    HDC dc = GetDC(p->tabs); TEXTMETRICW metrics; HFONT old = NULL; int height = 0;
    if (dc) {
        if (p->font) old = (HFONT)SelectObject(dc, p->font);
        if (GetTextMetricsW(dc, &metrics)) height = metrics.tmHeight;
        if (old) SelectObject(dc, old);
        ReleaseDC(p->tabs, dc);
    }
    if (height < 12) height = 12;
    {
        typedef UINT (WINAPI *GetDpiFn)(HWND);
        HMODULE user = GetModuleHandleW(L"user32.dll");
        GetDpiFn get_dpi = user ? (GetDpiFn)(void *)GetProcAddress(user, "GetDpiForWindow") : NULL;
        UINT dpi = get_dpi ? get_dpi(p->owner) : 96U;
        if (!dpi) dpi = 96;
        return height + MulDiv(12, (int)dpi, 96);
    }
}
static int layout_panel(PanelState *p, int observe_stock)
{
    RECT rect; int width, height, row, body;
    if (p->closing || p->layout_depth || !source_rect(p, &rect)) return 0;
    ++p->layout_depth;
    if (observe_stock && (!p->applied_valid || !same_rect(&rect, &p->applied))) {
        /* A host resize can leave our shifted top in place while changing the
         * other edges. Keep the native top instead of adding another tab row. */
        if (p->applied_valid && rect.top == p->applied.top)
            rect.top = p->stock.top;
        p->stock = rect;
    }
    width = p->stock.right - p->stock.left; height = p->stock.bottom - p->stock.top;
    if (width < 1 || height < 1) { --p->layout_depth; return 0; }
    {
        HFONT font = (HFONT)SendMessageW(p->owner, WM_GETFONT, 0, 0);
        if (!font) font = (HFONT)SendMessageW(p->source, WM_GETFONT, 0, 0);
        if (!font) font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        if (font != p->font) { p->font = font; (void)SendMessageW(p->tabs, WM_SETFONT, (WPARAM)font, FALSE); }
    }
    if (p->closing) { --p->layout_depth; return 0; }
    row = tab_height(p); if (row > height) row = height;
    body = height - row;
    if (!p->visible) {
        LONG_PTR source_style = GetWindowLongPtrW(p->source, GWL_STYLE);
        (void)ShowWindow(p->ref.panel_window, (source_style & WS_VISIBLE) ? SW_SHOWNA : SW_HIDE);
        if (p->closing) { --p->layout_depth; return 0; }
    }
    (void)SetWindowPos(p->ref.panel_window, HWND_TOP, p->stock.left, p->stock.top, width, p->visible ? height : row, SWP_NOACTIVATE);
    if (p->closing) { --p->layout_depth; return 0; }
    (void)SetWindowPos(p->tabs, NULL, 0, 0, width, row, SWP_NOZORDER | SWP_NOACTIVATE);
    if (p->closing) { --p->layout_depth; return 0; }
    (void)SetWindowPos(p->ref.content_window, NULL, 0, row, width, body, SWP_NOZORDER | SWP_NOACTIVATE);
    if (p->closing) { --p->layout_depth; return 0; }
    if (valid_child(p))
        (void)SetWindowPos(p->child, NULL, 0, 0, width, body, SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    if (!p->visible) {
        p->applied = p->stock; p->applied.top += row; p->applied_valid = 1;
        (void)SetWindowPos(p->source, NULL, p->applied.left, p->applied.top,
            p->applied.right - p->applied.left, p->applied.bottom - p->applied.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    --p->layout_depth;
    if (!p->closing) notify_panel(p, DLUMA_EDITOR_PANEL_RESIZED, 0);
    return !p->closing;
}

static void finish_panel(PanelState *p)
{
    RECT current;
    if (p->closing) return;
    p->closing = 1;
    (void)next_binding(p);
    cancel_wait(p);
    if (source_rect(p, &current)) {
        LONG_PTR style = GetWindowLongPtrW(p->source, GWL_STYLE);
        if (p->applied_valid && same_rect(&current, &p->applied))
            (void)SetWindowPos(p->source, NULL, p->stock.left, p->stock.top,
                p->stock.right - p->stock.left, p->stock.bottom - p->stock.top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (p->owns_hide && !(style & WS_VISIBLE)) (void)ShowWindow(p->source, SW_SHOWNA);
    }
    if (GetPropW(p->owner, owner_property) == p) (void)RemovePropW(p->owner, owner_property);
    if (GetPropW(p->source, source_property) == p) (void)RemovePropW(p->source, source_property);
    notify_panel(p, DLUMA_EDITOR_PANEL_CLOSING, 0);
}
static LRESULT CALLBACK panel_proc(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    PanelState *p = (PanelState *)GetWindowLongPtrW(window, GWLP_USERDATA);
    LRESULT result;
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lp;
        p = (PanelState *)create->lpCreateParams;
        if (!p) return FALSE;
        if (!SetPropW(window, panel_property, p)) return FALSE;
        panel_addref(p); /* window's reference */
        (void)SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)p);
    }
    if (!p) return DefWindowProcW(window, message, wp, lp);
    panel_addref(p);
    if (!p->closing && message == WM_NOTIFY && ((NMHDR *)lp) && ((NMHDR *)lp)->hwndFrom == p->tabs && ((NMHDR *)lp)->code == TCN_SELCHANGE) {
        int selected = (int)SendMessageW(p->tabs, TCM_GETCURSEL, 0, 0);
        (void)set_visible(p, selected == 1);
        result = 0;
    } else if (!p->closing && message == exit_message && (uint64_t)wp == p->ref.token &&
               (uint64_t)lp == p->binding_generation && p->wait) {
        (void)next_binding(p);
        cancel_wait(p);
        notify_panel(p, DLUMA_EDITOR_PANEL_CHILD_EXITED, 0);
        result = 0;
    } else if (message == WM_NCDESTROY) {
        finish_panel(p);
        (void)RemovePropW(window, panel_property);
        (void)SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        result = DefWindowProcW(window, message, wp, lp);
        panel_release(p); /* window's reference */
    } else result = DefWindowProcW(window, message, wp, lp);
    panel_release(p);
    return result;
}

int dluma_editor_panel_host_create(HWND owner, HWND source, const WCHAR *source_title,
    const WCHAR *panel_title, DlumaEditorPanelNotify notify, void *context, DlumaEditorPanelRef *out)
{
    PanelState *p; HWND window; TCITEMW item;
    if (!out || out->size != sizeof(*out) || out->version != 1 || !source_title || !*source_title ||
        !panel_title || !*panel_title || !notify || !owner_thread(owner) || !owner_thread(source) ||
        GetParent(source) != owner || !(GetWindowLongPtrW(source, GWL_STYLE) & WS_CHILD) ||
        GetPropW(owner, owner_property) || GetPropW(source, source_property) || !panel_class_ready()) return 0;
    p = (PanelState *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*p));
    if (!p) return 0;
    p->references = 1; p->owner = owner; p->source = source; p->thread_id = GetCurrentThreadId();
    p->context = context;
    p->ref.size = sizeof(p->ref); p->ref.version = 1; p->ref.token = (uint64_t)InterlockedIncrement64(&next_token);
    if (!p->ref.token || !SetPropW(owner, owner_property, p)) { panel_release(p); return 0; }
    if (!SetPropW(source, source_property, p) || !source_rect(p, &p->stock)) {
        if (GetPropW(source, source_property) == p) RemovePropW(source, source_property);
        if (GetPropW(owner, owner_property) == p) RemovePropW(owner, owner_property);
        panel_release(p); return 0;
    }
    window = CreateWindowExW(0, panel_class, L"", WS_CHILD | WS_CLIPCHILDREN |
        ((GetWindowLongPtrW(source, GWL_STYLE) & WS_VISIBLE) ? WS_VISIBLE : 0),
        p->stock.left, p->stock.top, p->stock.right-p->stock.left, p->stock.bottom-p->stock.top,
        owner, NULL, module_instance, p);
    if (!window) { if (GetPropW(source, source_property) == p) RemovePropW(source, source_property); if (GetPropW(owner, owner_property) == p) RemovePropW(owner, owner_property); panel_release(p); return 0; }
    p->ref.panel_window = window;
    p->tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
        0, 0, 1, 1, window, NULL, module_instance, NULL);
    p->ref.content_window = CreateWindowExW(0, L"Static", L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 1, 1, window, NULL, module_instance, NULL);
    if (!p->tabs || !p->ref.content_window || p->closing) { if (!p->closing) DestroyWindow(window); panel_release(p); return 0; }
    ZeroMemory(&item, sizeof(item)); item.mask = TCIF_TEXT; item.pszText = (WCHAR *)source_title;
    if (SendMessageW(p->tabs, TCM_INSERTITEMW, 0, (LPARAM)&item) < 0) { DestroyWindow(window); panel_release(p); return 0; }
    item.pszText = (WCHAR *)panel_title;
    if (SendMessageW(p->tabs, TCM_INSERTITEMW, 1, (LPARAM)&item) < 0) { DestroyWindow(window); panel_release(p); return 0; }
    (void)SendMessageW(p->tabs, TCM_SETCURSEL, 0, 0);
    if (!layout_panel(p, 0) || p->closing) { if (!p->closing) DestroyWindow(window); panel_release(p); return 0; }
    p->notify = notify;
    *out = p->ref;
    panel_release(p);
    return 1;
}

int dluma_editor_panel_host_layout(const DlumaEditorPanelRef *ref)
{
    PanelState *p = find_panel(ref); int result;
    if (!p) return 0;
    result = layout_panel(p, 1); panel_release(p); return result;
}
int dluma_editor_panel_host_show(const DlumaEditorPanelRef *ref, int visible)
{
    PanelState *p = find_panel(ref); int result;
    if (!p) return 0;
    result = set_visible(p, visible); panel_release(p); return result;
}
static void focus_child(PanelState *p)
{
    if (valid_child(p))
        (void)PostMessageW(p->child, focus_message, (WPARAM)p->ref.token, (LPARAM)p->binding_generation);
    if (!p->closing) notify_panel(p, DLUMA_EDITOR_PANEL_FOCUS, 0);
}
static int set_visible(PanelState *p, int visible)
{
    LONG_PTR style;
    visible = !!visible;
    if (p->closing || !owner_thread(p->source) || GetPropW(p->source, source_property) != p ||
        GetPropW(p->owner, owner_property) != p) return 0;
    if (p->visible == visible) return 1;
    style = GetWindowLongPtrW(p->source, GWL_STYLE);
    p->visible = visible;
    (void)SendMessageW(p->tabs, TCM_SETCURSEL, (WPARAM)(visible ? 1 : 0), 0);
    if (p->closing) return 0;
    if (visible) {
        if (style & WS_VISIBLE) { p->owns_hide = 1; (void)ShowWindow(p->source, SW_HIDE); }
        if (p->closing) return 0;
        (void)ShowWindow(p->ref.content_window, SW_SHOWNA);
    } else {
        (void)ShowWindow(p->ref.content_window, SW_HIDE);
        if (p->closing) return 0;
        if (p->owns_hide && !(style & WS_VISIBLE)) (void)ShowWindow(p->source, SW_SHOWNA);
        p->owns_hide = 0;
    }
    if (p->closing) return 0;
    if (!layout_panel(p, 0)) return 0;
    if (!p->closing) notify_panel(p, DLUMA_EDITOR_PANEL_VISIBILITY, (unsigned int)visible);
    if (!p->closing && visible) focus_child(p);
    else if (!p->closing && GetPropW(p->source, source_property) == p && IsWindowVisible(p->source))
        (void)SetFocus(p->source);
    return !p->closing;
}

int dluma_editor_panel_host_bind(const DlumaEditorPanelRef *ref, HWND child, DWORD expected_process_id, HANDLE process)
{
    PanelState *p = find_panel(ref); PanelWait *w; HANDLE duplicate = NULL; DWORD child_pid = 0;
    RECT bounds; int result = 0;
    if (!p) return 0;
    if (p->wait || !child || !expected_process_id || !process || !IsWindow(child) ||
        !(GetWindowLongPtrW(child, GWL_STYLE) & WS_CHILD) || GetParent(child) != p->ref.content_window ||
        !GetWindowThreadProcessId(child, &child_pid) || child_pid != expected_process_id ||
        GetProcessId(process) != expected_process_id || WaitForSingleObject(process, 0) != WAIT_TIMEOUT ||
        !DuplicateHandle(GetCurrentProcess(), process, GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS)) goto done;
    w = (PanelWait *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*w));
    if (!w) { CloseHandle(duplicate); goto done; }
    w->window = p->ref.panel_window; w->token = p->ref.token;
    w->generation = next_binding(p); w->process = duplicate;
    if (!RegisterWaitForSingleObject(&w->registration, duplicate, child_exit, w, INFINITE, WT_EXECUTEONLYONCE)) {
        wait_release(w); goto done;
    }
    InterlockedIncrement(&pending_waits);
    p->child = child; p->child_pid = expected_process_id; p->wait = w;
    if (GetClientRect(p->ref.content_window, &bounds))
        (void)SetWindowPos(child, NULL, 0, 0, bounds.right, bounds.bottom,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    if (!p->closing && p->visible) focus_child(p);
    result = !p->closing;
done:
    panel_release(p); return result;
}
int dluma_editor_panel_host_unbind(const DlumaEditorPanelRef *ref)
{
    PanelState *p = find_panel(ref);
    if (!p) return 0;
    (void)next_binding(p); cancel_wait(p);
    panel_release(p); return 1;
}
uint64_t dluma_editor_panel_host_binding_generation(const DlumaEditorPanelRef *ref)
{
    PanelState *p = find_panel(ref); uint64_t result;
    if (!p) return 0;
    result = p->wait ? p->binding_generation : 0;
    panel_release(p);
    return result;
}
int dluma_editor_panel_host_callbacks_drained(void)
{
    return InterlockedCompareExchange(&pending_waits, 0, 0) == 0;
}
int dluma_editor_panel_host_close(const DlumaEditorPanelRef *ref)
{
    PanelState *p = find_panel(ref); int result;
    if (!p) return 0;
    result = DestroyWindow(p->ref.panel_window) != 0;
    panel_release(p); return result;
}
