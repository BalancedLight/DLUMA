#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include "dlw_editor_ui.h"

/* Keep the SDK declaration surface local to this translation unit. */
extern int dluma_dual_host_runtime_is_editor(void);
extern int dluma_editor_is_main_window(HWND window, DWORD expected_process_id);

#define UI_THREAD_CAPACITY 256U
#define UI_QUEUE_CAPACITY 4096U
#define UI_OWNER_COUNT 5U
#define UI_SLOT_COUNT 4U
#define UI_SUBCLASS_ID ((UINT_PTR)0x444C5549U)

typedef struct UiSubscription {
    DlumaEditorUiListener listener;
    DlumaEditorUiObserver observer;
    void *context;
    unsigned int references;
    int active;
} UiSubscription;

typedef struct UiInvocation {
    struct UiInvocation *previous;
    UiSubscription subscriptions[UI_OWNER_COUNT];
    uint64_t values[UI_OWNER_COUNT][UI_SLOT_COUNT];
    DlumaEditorUiCleanup cleanup[UI_OWNER_COUNT][UI_SLOT_COUNT];
} UiInvocation;

typedef struct UiWindow {
    struct UiWindow *next;
    HWND window;
    DWORD thread_id;
    unsigned int depth;
    int detached;
    LONG64 identity;
    LONG refresh_generation;
    int placement_queued;
} UiWindow;

typedef struct UiThread {
    DWORD id;
    HANDLE handle;
    HWND anchor;
    HWND dispatcher;
    HHOOK getmessage_hook;
    HHOOK callwnd_hook;
    HWND queue[UI_QUEUE_CAPACITY];
    unsigned int head, count;
    LONG refresh_generation;
    int bootstrapping;
    int hooks_installing;
} UiThread;

typedef BOOL (WINAPI *UiSetSubclass)(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR);
typedef BOOL (WINAPI *UiRemoveSubclass)(HWND, SUBCLASSPROC, UINT_PTR);
typedef LRESULT (WINAPI *UiDefSubclass)(HWND, UINT, WPARAM, LPARAM);

static struct {
    SRWLOCK lock;
    volatile LONG state;
    volatile LONG callbacks;
    volatile LONG refresh_generation;
    volatile LONG64 next_identity;
    DWORD tls_index;
    UINT dispatch_message;
    UINT placement_message;
    HMODULE module, common_controls;
    HANDLE watcher, stop_event, watcher_ready, watcher_go;
    volatile LONG watcher_status;
    int class_registered;
    UiSetSubclass set_subclass;
    UiRemoveSubclass remove_subclass;
    UiDefSubclass def_subclass;
    UiWindow *windows;
    UiThread threads[UI_THREAD_CAPACITY];
    UiSubscription subscriptions[UI_OWNER_COUNT];
} ui;
/* A broker restart in the same Editor process must not move the frame again. */
static volatile LONG primary_placement_state;

static LRESULT CALLBACK ui_window_proc(HWND, UINT, WPARAM, LPARAM,
                                       UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK ui_dispatcher_proc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK ui_getmessage_hook(int, WPARAM, LPARAM);
static LRESULT CALLBACK ui_callwnd_hook(int, WPARAM, LPARAM);
static int ui_enroll_owner(HWND window);
static void ui_pump(UiThread *thread);
static void ui_cleanup_thread(UiThread *thread);

static void ui_placement_dpi(HWND window, HMONITOR monitor, UINT *source, UINT *target)
{
    typedef HANDLE (WINAPI *ThreadContextFn)(void);
    typedef HANDLE (WINAPI *WindowContextFn)(HWND);
    typedef BOOL (WINAPI *EqualContextFn)(HANDLE, HANDLE);
    typedef UINT (WINAPI *WindowDpiFn)(HWND);
    typedef HRESULT (WINAPI *MonitorDpiFn)(HMONITOR, int, UINT *, UINT *);
    HMODULE user=GetModuleHandleW(L"user32.dll"), shcore;
    ThreadContextFn thread_context; WindowContextFn window_context; EqualContextFn equal_context;
    WindowDpiFn window_dpi; MonitorDpiFn monitor_dpi; UINT x=0, y=0, current;
    *source=0; *target=0;
    if (!user) return;
    thread_context=(ThreadContextFn)(void *)GetProcAddress(user,"GetThreadDpiAwarenessContext");
    window_context=(WindowContextFn)(void *)GetProcAddress(user,"GetWindowDpiAwarenessContext");
    equal_context=(EqualContextFn)(void *)GetProcAddress(user,"AreDpiAwarenessContextsEqual");
    window_dpi=(WindowDpiFn)(void *)GetProcAddress(user,"GetDpiForWindow");
    if (!thread_context || !window_context || !equal_context || !window_dpi ||
        !equal_context(thread_context(),window_context(window))) return;
    shcore=LoadLibraryExW(L"shcore.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!shcore) return;
    monitor_dpi=(MonitorDpiFn)(void *)GetProcAddress(shcore,"GetDpiForMonitor");
    current=window_dpi(window);
    if (monitor_dpi && SUCCEEDED(monitor_dpi(monitor,0,&x,&y)) &&
        current>=48U && current<=768U && x>=48U && x<=768U && y==x) {
        *source=current; *target=x;
    }
    FreeLibrary(shcore);
}
static int ui_place_primary(HWND window)
{
    POINT origin = {0, 0}; HMONITOR monitor;
    MONITORINFO info; WINDOWPLACEMENT placement; RECT work, normal, workspace;
    int width, height, max_width, max_height, minimum_width, minimum_height;
    int64_t saved_width, saved_height, scaled_width, scaled_height;
    UINT source_dpi, target_dpi; HMODULE user; int (WINAPI *metrics_for_dpi)(int,UINT);
    if (!dluma_editor_is_main_window(window,GetCurrentProcessId())) return 0;
    monitor=MonitorFromPoint(origin,MONITOR_DEFAULTTOPRIMARY);
    ZeroMemory(&info,sizeof(info)); info.cbSize=sizeof(info);
    ZeroMemory(&placement,sizeof(placement)); placement.length=sizeof(placement);
    if (!monitor || !GetMonitorInfoW(monitor,&info) || !GetWindowPlacement(window,&placement)) return 0;
    work=info.rcWork; max_width=work.right-work.left; max_height=work.bottom-work.top;
    if (max_width<=0 || max_height<=0) return 0;
    normal=placement.rcNormalPosition;
    saved_width=(int64_t)normal.right-normal.left; saved_height=(int64_t)normal.bottom-normal.top;
    ui_placement_dpi(window,monitor,&source_dpi,&target_dpi);
    user=GetModuleHandleW(L"user32.dll");
    metrics_for_dpi=user ? (int (WINAPI *)(int,UINT))(void *)GetProcAddress(user,"GetSystemMetricsForDpi") : NULL;
    minimum_width=metrics_for_dpi && target_dpi ? metrics_for_dpi(SM_CXMINTRACK,target_dpi) : GetSystemMetrics(SM_CXMINTRACK);
    minimum_height=metrics_for_dpi && target_dpi ? metrics_for_dpi(SM_CYMINTRACK,target_dpi) : GetSystemMetrics(SM_CYMINTRACK);
    if (minimum_width<1) minimum_width=1;
    if (minimum_height<1) minimum_height=1;
    if (minimum_width>max_width) minimum_width=max_width;
    if (minimum_height>max_height) minimum_height=max_height;
    workspace.left=work.left-(work.left-info.rcMonitor.left);
    workspace.top=work.top-(work.top-info.rcMonitor.top);
    workspace.right=workspace.left+max_width; workspace.bottom=workspace.top+max_height;
    scaled_width=saved_width; scaled_height=saved_height;
    if (source_dpi && target_dpi && MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST)!=monitor) {
        scaled_width=(saved_width*(int64_t)target_dpi)/(int64_t)source_dpi;
        scaled_height=(saved_height*(int64_t)target_dpi)/(int64_t)source_dpi;
    }
    width=scaled_width<minimum_width ? minimum_width : scaled_width>max_width ? max_width : (int)scaled_width;
    height=scaled_height<minimum_height ? minimum_height : scaled_height>max_height ? max_height : (int)scaled_height;
    if (MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST)==monitor &&
        saved_width==width && saved_height==height &&
        normal.left>=workspace.left && normal.top>=workspace.top && normal.right<=workspace.right && normal.bottom<=workspace.bottom)
        return 1;
    /* WINDOWPLACEMENT normal coordinates are workspace-relative. */
    normal.left=workspace.left+(max_width-width)/2; normal.top=workspace.top+(max_height-height)/2;
    normal.right=normal.left+width; normal.bottom=normal.top+height;
    placement.rcNormalPosition=normal;
    placement.ptMaxPosition.x=-1; placement.ptMaxPosition.y=-1;
    if (IsZoomed(window) && MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST)!=monitor) {
        WINDOWPLACEMENT restored=placement;
        /* An already maximized frame keeps its current monitor unless its
         * normal placement is applied before maximizing on the new monitor. */
        restored.showCmd=SW_SHOWNOACTIVATE;
        if (!SetWindowPlacement(window,&restored)) return 0;
    }
    if (!SetWindowPlacement(window,&placement)) return 0;
    return MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST)==monitor;
}
static void ui_queue_primary_placement(UiWindow *record)
{
    if (!record || record->detached || !ui.placement_message ||
        InterlockedCompareExchange(&primary_placement_state,0,0)!=0 ||
        !dluma_editor_is_main_window(record->window,GetCurrentProcessId()) ||
        InterlockedCompareExchange(&primary_placement_state,1,0)!=0) return;
    record->placement_queued=1;
    if (!PostMessageW(record->window,ui.placement_message,0,0)) {
        record->placement_queued=0; InterlockedCompareExchange(&primary_placement_state,0,1);
    }
}

static int ui_running(void)
{
    return InterlockedCompareExchange(&ui.state, 0, 0) == 2;
}

static UiThread *ui_thread(DWORD id)
{
    unsigned int i;
    UiThread *found = NULL;
    AcquireSRWLockExclusive(&ui.lock);
    for (i = 0; i < UI_THREAD_CAPACITY; ++i) {
        if (ui.threads[i].id == id) { found = &ui.threads[i]; break; }
    }
    ReleaseSRWLockExclusive(&ui.lock);
    return found;
}

static void ui_invocation_begin(UiInvocation *invocation)
{
    unsigned int i;
    ZeroMemory(invocation, sizeof(*invocation));
    invocation->previous = (UiInvocation *)TlsGetValue(ui.tls_index);
    AcquireSRWLockExclusive(&ui.lock);
    for (i = 0; i < UI_OWNER_COUNT; ++i) {
        if (!ui.subscriptions[i].active) continue;
        invocation->subscriptions[i] = ui.subscriptions[i];
        ++ui.subscriptions[i].references;
    }
    ReleaseSRWLockExclusive(&ui.lock);
    (void)TlsSetValue(ui.tls_index, invocation);
}

static void ui_invocation_end(UiInvocation *invocation)
{
    unsigned int i, slot;
    for (i=0; i<UI_OWNER_COUNT; ++i)
        for (slot=0; slot<UI_SLOT_COUNT; ++slot)
            if (invocation->cleanup[i][slot])
                invocation->cleanup[i][slot](&invocation->values[i][slot]);
    (void)TlsSetValue(ui.tls_index, invocation->previous);
    AcquireSRWLockExclusive(&ui.lock);
    for (i = 0; i < UI_OWNER_COUNT; ++i)
        if (invocation->subscriptions[i].listener)
            --ui.subscriptions[i].references;
    ReleaseSRWLockExclusive(&ui.lock);
}

uint64_t *dluma_editor_ui_invocation_value(unsigned int owner)
{
    return dluma_editor_ui_invocation_slot(owner, DLUMA_EDITOR_UI_SLOT_FEATURE);
}

uint64_t *dluma_editor_ui_invocation_slot(unsigned int owner, unsigned int slot)
{
    UiInvocation *invocation;
    if (!owner || owner > UI_OWNER_COUNT || slot >= UI_SLOT_COUNT || !ui.dispatch_message) return NULL;
    invocation = (UiInvocation *)TlsGetValue(ui.tls_index);
    return invocation ? &invocation->values[owner - 1U][slot] : NULL;
}
int dluma_editor_ui_invocation_cleanup(unsigned int owner, unsigned int slot,
                                      DlumaEditorUiCleanup cleanup)
{
    UiInvocation *invocation;
    if (!owner || owner>UI_OWNER_COUNT || slot>=UI_SLOT_COUNT || !cleanup || !ui.dispatch_message) return 0;
    invocation=(UiInvocation *)TlsGetValue(ui.tls_index);
    if (!invocation || (invocation->cleanup[owner-1U][slot] &&
        invocation->cleanup[owner-1U][slot]!=cleanup)) return 0;
    invocation->cleanup[owner-1U][slot]=cleanup;
    return 1;
}

static int ui_notify(UiInvocation *invocation, HWND window, unsigned int stage,
                     UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    unsigned int i;
    int handled = 0;
    for (i = 0; i < UI_OWNER_COUNT; ++i) {
        UiSubscription *subscription = &invocation->subscriptions[i];
        if (subscription->listener &&
            subscription->listener(window, stage, message, wparam, lparam,
                                   result, subscription->context)) {
            handled = 1;
            /* A consumed feature message still receives AFTER/DESTROY. */
            if (stage == DLUMA_EDITOR_UI_BEFORE) break;
        }
    }
    return handled;
}

static void ui_forget(UiWindow *record)
{
    UiWindow **link;
    if (record->placement_queued) {
        record->placement_queued=0;
        InterlockedCompareExchange(&primary_placement_state,0,1);
    }
    AcquireSRWLockExclusive(&ui.lock);
    for (link = &ui.windows; *link; link = &(*link)->next) {
        if (*link == record) { *link = record->next; break; }
    }
    record->detached = 1;
    ReleaseSRWLockExclusive(&ui.lock);
}

static int ui_detach_owner(UiWindow *record)
{
    UiInvocation invocation;
    LRESULT result = 0;
    if (record->detached || record->depth || GetCurrentThreadId() != record->thread_id) return 0;
    if (!ui.remove_subclass(record->window, ui_window_proc, UI_SUBCLASS_ID))
        return 0;
    ++record->depth;
    ui_forget(record);
    ui_invocation_begin(&invocation);
    (void)ui_notify(&invocation, record->window, DLUMA_EDITOR_UI_DESTROY,
                    0, 0, 0, &result);
    ui_invocation_end(&invocation);
    if (!--record->depth) HeapFree(GetProcessHeap(), 0, record);
    return 1;
}

static LRESULT CALLBACK ui_window_proc(HWND window, UINT message, WPARAM wparam,
                                      LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    UiWindow *record = (UiWindow *)data;
    UiInvocation invocation;
    LRESULT result = 0;
    int handled;
    (void)id;
    InterlockedIncrement(&ui.callbacks);
    ++record->depth;
    if (message==ui.placement_message && record->placement_queued) {
        int placed;
        record->placement_queued=0;
        placed=ui_running() && ui_place_primary(window);
        InterlockedCompareExchange(&primary_placement_state,placed ? 2 : 0,1);
    }
    ui_invocation_begin(&invocation);
    __try {
        handled = ui_notify(&invocation, window, DLUMA_EDITOR_UI_BEFORE,
                         message, wparam, lparam, &result);
        /* Lifetime messages always reach the original window procedure. */
        if (!handled || message == WM_NCDESTROY || message == WM_DESTROY)
            result = ui.def_subclass(window, message, wparam, lparam);
        (void)ui_notify(&invocation, window, DLUMA_EDITOR_UI_AFTER,
                    message, wparam, lparam, &result);
        if (!record->detached && ui_running() &&
            (message==WM_SHOWWINDOW || message==WM_WINDOWPOSCHANGED))
            ui_queue_primary_placement(record);
        if (message == WM_NCDESTROY && !record->detached) {
            (void)ui.remove_subclass(window, ui_window_proc, UI_SUBCLASS_ID);
            ui_forget(record);
            (void)ui_notify(&invocation, window, DLUMA_EDITOR_UI_DESTROY,
                        message, wparam, lparam, &result);
        }
    } __finally {
        ui_invocation_end(&invocation);
        if (!--record->depth && record->detached)
            HeapFree(GetProcessHeap(), 0, record);
        InterlockedDecrement(&ui.callbacks);
    }
    return result;
}

static int ui_enroll_owner(HWND window)
{
    DWORD process_id = 0;
    DWORD thread_id = GetWindowThreadProcessId(window, &process_id);
    UiWindow *record;
    UiInvocation invocation;
    LRESULT result = 0;
    if (!ui_running() || !window || !thread_id ||
        thread_id != GetCurrentThreadId() || process_id != GetCurrentProcessId())
        return 0;
    AcquireSRWLockExclusive(&ui.lock);
    for (record = ui.windows; record; record = record->next)
        if (record->window == window && !record->detached) break;
    ReleaseSRWLockExclusive(&ui.lock);
    if (record) return 1;
    record = (UiWindow *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                  sizeof(*record));
    if (!record) return 0;
    record->window = window;
    record->thread_id = thread_id;
    record->identity = InterlockedIncrement64(&ui.next_identity);
    if (!ui.set_subclass(window, ui_window_proc, UI_SUBCLASS_ID,
                         (DWORD_PTR)record)) {
        HeapFree(GetProcessHeap(), 0, record);
        return 0;
    }
    AcquireSRWLockExclusive(&ui.lock);
    record->next = ui.windows;
    ui.windows = record;
    ReleaseSRWLockExclusive(&ui.lock);
    ++record->depth;
    ui_invocation_begin(&invocation);
    (void)ui_notify(&invocation, window, DLUMA_EDITOR_UI_BEFORE,
                    DLUMA_EDITOR_UI_ENROLLED, 0, 0, &result);
    ui_invocation_end(&invocation);
    if (!record->detached) ui_queue_primary_placement(record);
    if (!--record->depth && record->detached)
        HeapFree(GetProcessHeap(), 0, record);
    return 1;
}

static void ui_bootstrap(UiThread *thread)
{
    HWND dispatcher;
    if (!thread || thread->id != GetCurrentThreadId() ||
        thread->dispatcher || thread->bootstrapping || !ui_running()) return;
    thread->bootstrapping = 1;
    dispatcher = CreateWindowExW(0, L"DLUMA.EditorUi.Dispatcher.1", L"", 0,
                                  0, 0, 0, 0, HWND_MESSAGE, NULL,
                                  (HINSTANCE)ui.module, thread);
    thread->dispatcher = dispatcher;
    thread->bootstrapping = 0;
    if (dispatcher) (void)PostMessageW(dispatcher, ui.dispatch_message, 0, 0);
}

static LRESULT CALLBACK ui_getmessage_hook(int code, WPARAM wparam, LPARAM lparam)
{
    InterlockedIncrement(&ui.callbacks);
    if (code >= 0) {
        UiThread *thread = ui_thread(GetCurrentThreadId());
        if (ui_running()) ui_bootstrap(thread);
        else if (thread && InterlockedCompareExchange(&ui.state, 0, 0) == 3)
            ui_cleanup_thread(thread);
    }
    InterlockedDecrement(&ui.callbacks);
    return CallNextHookEx(NULL, code, wparam, lparam);
}

static LRESULT CALLBACK ui_callwnd_hook(int code, WPARAM wparam, LPARAM lparam)
{
    InterlockedIncrement(&ui.callbacks);
    if (code >= 0 && ui_running()) {
        const CWPSTRUCT *message = (const CWPSTRUCT *)lparam;
        UiThread *thread = ui_thread(GetCurrentThreadId());
        ui_bootstrap(thread);
        if (thread && thread->dispatcher && message && message->hwnd != thread->dispatcher &&
            (message->message == WM_CREATE || message->message == WM_SHOWWINDOW))
            (void)ui_enroll_owner(message->hwnd);
        if (message) {
            UiInvocation *invocation=(UiInvocation *)TlsGetValue(ui.tls_index);
            unsigned int i;
            if (invocation) for (i=0;i<UI_OWNER_COUNT;++i) {
                UiSubscription *subscription=&invocation->subscriptions[i];
                int active;
                if (!subscription->observer) continue;
                AcquireSRWLockShared(&ui.lock);
                active=ui.subscriptions[i].active &&
                    ui.subscriptions[i].observer==subscription->observer;
                ReleaseSRWLockShared(&ui.lock);
                if (active) subscription->observer(message->hwnd,message->message,
                    message->wParam,message->lParam,subscription->context);
            }
        }
    } else if (code >= 0 && InterlockedCompareExchange(&ui.state, 0, 0) == 3) {
        UiThread *thread = ui_thread(GetCurrentThreadId());
        if (thread) ui_cleanup_thread(thread);
    }
    InterlockedDecrement(&ui.callbacks);
    return CallNextHookEx(NULL, code, wparam, lparam);
}

int dluma_editor_ui_request_enroll(HWND window)
{
    DWORD process_id = 0;
    DWORD id = GetWindowThreadProcessId(window, &process_id);
    UiThread *thread = NULL;
    unsigned int i;
    int install = 0;
    int queued = 0;
    HWND wake;
    if (!ui_running() || !id || process_id != GetCurrentProcessId()) return 0;
    InterlockedIncrement(&ui.callbacks);
    AcquireSRWLockExclusive(&ui.lock);
    if (!ui_running()) { ReleaseSRWLockExclusive(&ui.lock); InterlockedDecrement(&ui.callbacks); return 0; }
    for (i = 0; i < UI_THREAD_CAPACITY; ++i)
        if (ui.threads[i].id == id) { thread = &ui.threads[i]; break; }
    if (thread && thread->handle && WaitForSingleObject(thread->handle, 0) == WAIT_OBJECT_0) {
        UiWindow *record;
        for (record = ui.windows; record; record = record->next)
            if (record->thread_id == id) break;
        if (record) { ReleaseSRWLockExclusive(&ui.lock); InterlockedDecrement(&ui.callbacks); return 0; }
        if (thread->getmessage_hook && !UnhookWindowsHookEx(thread->getmessage_hook)) {
            ReleaseSRWLockExclusive(&ui.lock); InterlockedDecrement(&ui.callbacks); return 0;
        }
        thread->getmessage_hook = NULL;
        if (thread->callwnd_hook && !UnhookWindowsHookEx(thread->callwnd_hook)) {
            ReleaseSRWLockExclusive(&ui.lock); InterlockedDecrement(&ui.callbacks); return 0;
        }
        thread->callwnd_hook = NULL;
        CloseHandle(thread->handle);
        ZeroMemory(thread, sizeof(*thread));
        thread = NULL;
    }
    if (!thread) {
        for (i = 0; i < UI_THREAD_CAPACITY; ++i)
            if (!ui.threads[i].id) {
                thread = &ui.threads[i];
                thread->id = id;
                thread->anchor = window;
                thread->refresh_generation = ui.refresh_generation;
                thread->hooks_installing = 1;
                install = 1;
                break;
            }
    }
    if (thread) {
        for (i = 0; i < thread->count; ++i)
            if (thread->queue[(thread->head + i) % UI_QUEUE_CAPACITY] == window) { queued = 1; break; }
    }
    if (!thread || (!queued && thread->count == UI_QUEUE_CAPACITY)) {
        ReleaseSRWLockExclusive(&ui.lock);
        InterlockedDecrement(&ui.callbacks);
        return 0;
    }
    if (!thread->anchor || !IsWindow(thread->anchor)) thread->anchor = window;
    if (!install && !thread->dispatcher && !thread->getmessage_hook &&
        !thread->callwnd_hook && !thread->hooks_installing) {
        thread->hooks_installing = 1;
        install = 1;
    }
    if (!queued) {
        thread->queue[(thread->head + thread->count) % UI_QUEUE_CAPACITY] = window;
        ++thread->count;
    }
    wake = thread->dispatcher ? thread->dispatcher : thread->anchor;
    ReleaseSRWLockExclusive(&ui.lock);
    if (install) {
        HANDLE handle = thread->handle ? thread->handle : OpenThread(SYNCHRONIZE, FALSE, id);
        HHOOK getmessage = NULL, callwnd = NULL;
        if (handle) {
            getmessage = SetWindowsHookExW(WH_GETMESSAGE, ui_getmessage_hook, ui.module, id);
            callwnd = SetWindowsHookExW(WH_CALLWNDPROC, ui_callwnd_hook, ui.module, id);
        }
        AcquireSRWLockExclusive(&ui.lock);
        thread->handle = handle;
        thread->getmessage_hook = getmessage;
        thread->callwnd_hook = callwnd;
        thread->hooks_installing = 0;
        ReleaseSRWLockExclusive(&ui.lock);
    }
    if (id == GetCurrentThreadId()) {
        ui_bootstrap(thread);
        if (thread->dispatcher) (void)ui_enroll_owner(window);
    }
    i = PostMessageW(wake, ui.dispatch_message, 0, 0) != 0;
    if (install && !thread->dispatcher && !thread->getmessage_hook && !thread->callwnd_hook) i = 0;
    InterlockedDecrement(&ui.callbacks);
    return (int)i;
}

static void ui_cleanup_thread(UiThread *thread)
{
    UiWindow *record;
    unsigned int count = 0;
    ULONGLONG started = GetTickCount64();
    if (thread->hooks_installing) {
        if (thread->anchor) (void)PostMessageW(thread->anchor, ui.dispatch_message, 0, 0);
        return;
    }
    for (;;) {
        AcquireSRWLockExclusive(&ui.lock);
        for (record = ui.windows; record; record = record->next)
            if (record->thread_id == thread->id) break;
        ReleaseSRWLockExclusive(&ui.lock);
        if (!record) break;
        if (!ui_detach_owner(record)) {
            if (thread->dispatcher || thread->anchor)
                (void)PostMessageW(thread->dispatcher ? thread->dispatcher : thread->anchor, ui.dispatch_message, 0, 0);
            return;
        }
        if (++count >= 128U || GetTickCount64() - started >= 2U) {
            if (thread->dispatcher || thread->anchor)
                (void)PostMessageW(thread->dispatcher ? thread->dispatcher : thread->anchor, ui.dispatch_message, 0, 0);
            return;
        }
    }
    if (thread->getmessage_hook && UnhookWindowsHookEx(thread->getmessage_hook))
        thread->getmessage_hook = NULL;
    if (thread->callwnd_hook && UnhookWindowsHookEx(thread->callwnd_hook))
        thread->callwnd_hook = NULL;
    if (!thread->getmessage_hook && !thread->callwnd_hook) {
        HWND dispatcher = thread->dispatcher;
        if (!dispatcher || DestroyWindow(dispatcher)) thread->dispatcher = NULL;
    }
}

static void ui_pump(UiThread *thread)
{
    ULONGLONG started = GetTickCount64();
    unsigned int count = 0;
    if (!ui_running()) { ui_cleanup_thread(thread); return; }
    while (count++ < 128U && GetTickCount64() - started < 2U) {
        HWND window = NULL;
        AcquireSRWLockExclusive(&ui.lock);
        if (thread->count) {
            window = thread->queue[thread->head];
            thread->head = (thread->head + 1U) % UI_QUEUE_CAPACITY;
            --thread->count;
        }
        ReleaseSRWLockExclusive(&ui.lock);
        if (!window) break;
        (void)ui_enroll_owner(window);
    }
    {
        LONG target = InterlockedCompareExchange(&thread->refresh_generation, 0, 0);
        int remaining = 0;
        UiWindow *record;
        while (count++ < 128U && GetTickCount64() - started < 2U) {
            HWND window = NULL;
            LONG64 identity = 0;
            UiInvocation invocation;
            LRESULT result = 0;
            if (!ui_running()) break;
            AcquireSRWLockExclusive(&ui.lock);
            for (record = ui.windows; record; record = record->next)
                if (record->thread_id == thread->id && record->refresh_generation != target) {
                    window = record->window; identity = record->identity; break;
                }
            ReleaseSRWLockExclusive(&ui.lock);
            if (!window) break;
            ui_invocation_begin(&invocation);
            (void)ui_notify(&invocation, window, DLUMA_EDITOR_UI_BEFORE,
                            DLUMA_EDITOR_UI_REFRESH, 0, 0, &result);
            ui_invocation_end(&invocation);
            AcquireSRWLockExclusive(&ui.lock);
            for (record = ui.windows; record; record = record->next)
                if (record->identity == identity) { record->refresh_generation = target; break; }
            ReleaseSRWLockExclusive(&ui.lock);
        }
        AcquireSRWLockExclusive(&ui.lock);
        if (thread->count || target != thread->refresh_generation) remaining = 1;
        for (record = ui.windows; record && !remaining; record = record->next)
            if (record->thread_id == thread->id && record->refresh_generation != target) remaining = 1;
        ReleaseSRWLockExclusive(&ui.lock);
        if (remaining && thread->dispatcher)
            (void)PostMessageW(thread->dispatcher, ui.dispatch_message, 0, 0);
    }
}

static LRESULT CALLBACK ui_dispatcher_proc(HWND window, UINT message,
                                          WPARAM wparam, LPARAM lparam)
{
    UiThread *thread = (UiThread *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW *create = (const CREATESTRUCTW *)lparam;
        thread = (UiThread *)create->lpCreateParams;
        (void)SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)thread);
    }
    if (message == ui.dispatch_message && thread) {
        InterlockedIncrement(&ui.callbacks);
        ui_pump(thread);
        InterlockedDecrement(&ui.callbacks);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void CALLBACK ui_event(HWINEVENTHOOK hook, DWORD event, HWND window,
                              LONG object, LONG child, DWORD thread, DWORD time)
{
    (void)hook; (void)thread; (void)time;
    InterlockedIncrement(&ui.callbacks);
    if (object == OBJID_WINDOW && child == CHILDID_SELF && ui_running() &&
        (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_SHOW))
        (void)dluma_editor_ui_request_enroll(window);
    /* The subclass owns WM_NCDESTROY; a delayed destroy event cannot act on
     * a reused HWND or release another generation's record. */
    InterlockedDecrement(&ui.callbacks);
}

static BOOL CALLBACK ui_enumerate(HWND window, LPARAM parameter)
{
    DWORD process_id = 0;
    (void)GetWindowThreadProcessId(window, &process_id);
    if (process_id == GetCurrentProcessId()) {
        (void)dluma_editor_ui_request_enroll(window);
        if (!parameter) (void)EnumChildWindows(window, ui_enumerate, 1);
    }
    return TRUE;
}

static DWORD WINAPI ui_watch(void *context)
{
    HWINEVENTHOOK hook;
    (void)context;
    hook = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, ui.module,
                           ui_event, GetCurrentProcessId(), 0, WINEVENT_INCONTEXT);
    InterlockedExchange(&ui.watcher_status, hook ? 1 : -1);
    (void)SetEvent(ui.watcher_ready);
    if (!hook) return 0;
    {
        HANDLE events[2];
        events[0] = ui.stop_event; events[1] = ui.watcher_go;
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0 + 1U) {
            (void)UnhookWinEvent(hook); return 0;
        }
    }
    (void)EnumWindows(ui_enumerate, 0);
    for (;;) {
        DWORD wait = MsgWaitForMultipleObjects(1, &ui.stop_event, FALSE,
                                               INFINITE, QS_ALLINPUT);
        MSG message;
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    (void)UnhookWinEvent(hook);
    return 0;
}

int dluma_editor_ui_request_refresh(void)
{
    unsigned int i;
    LONG generation;
    if (!ui_running()) return 0;
    InterlockedIncrement(&ui.callbacks);
    if (!ui_running()) { InterlockedDecrement(&ui.callbacks); return 0; }
    generation = InterlockedIncrement(&ui.refresh_generation);
    AcquireSRWLockExclusive(&ui.lock);
    for (i = 0; i < UI_THREAD_CAPACITY; ++i) {
        UiThread *thread = &ui.threads[i];
        if (!thread->id) continue;
        InterlockedExchange(&thread->refresh_generation, generation);
        if (thread->dispatcher)
            (void)PostMessageW(thread->dispatcher, ui.dispatch_message, 0, 0);
    }
    ReleaseSRWLockExclusive(&ui.lock);
    InterlockedDecrement(&ui.callbacks);
    return 1;
}

int dluma_editor_ui_subscribe(unsigned int owner,
                             DlumaEditorUiListener listener, void *context)
{
    return dluma_editor_ui_subscribe_observed(owner,listener,NULL,context);
}

int dluma_editor_ui_subscribe_observed(unsigned int owner,
    DlumaEditorUiListener listener, DlumaEditorUiObserver observer, void *context)
{
    UiSubscription *subscription;
    if (!owner || owner > UI_OWNER_COUNT || !listener || !ui_running()) return 0;
    AcquireSRWLockExclusive(&ui.lock);
    subscription = &ui.subscriptions[owner - 1U];
    if (subscription->references || subscription->active) {
        int same = subscription->active && subscription->listener == listener &&
                   subscription->observer == observer && subscription->context == context;
        ReleaseSRWLockExclusive(&ui.lock);
        return same;
    }
    subscription->listener = listener;
    subscription->observer = observer;
    subscription->context = context;
    subscription->active = 1;
    ReleaseSRWLockExclusive(&ui.lock);
    (void)dluma_editor_ui_request_refresh();
    return 1;
}

int dluma_editor_ui_unsubscribe(unsigned int owner)
{
    UiSubscription *subscription;
    int drained;
    if (!owner || owner > UI_OWNER_COUNT) return 0;
    AcquireSRWLockExclusive(&ui.lock);
    subscription = &ui.subscriptions[owner - 1U];
    subscription->active = 0;
    drained = subscription->references == 0;
    if (drained) ZeroMemory(subscription, sizeof(*subscription));
    ReleaseSRWLockExclusive(&ui.lock);
    return drained;
}

int dluma_editor_ui_start(void)
{
    WNDCLASSW window_class;
    if (!dluma_dual_host_runtime_is_editor()) return 0;
    AcquireSRWLockExclusive(&ui.lock);
    if (InterlockedCompareExchange(&ui.state, 1, 0) != 0) {
        int running = ui_running(); ReleaseSRWLockExclusive(&ui.lock); return running;
    }
    InterlockedIncrement(&ui.callbacks);
    ReleaseSRWLockExclusive(&ui.lock);
    ui.tls_index = TlsAlloc();
    if (ui.tls_index == TLS_OUT_OF_INDEXES) goto failed;
    ui.dispatch_message = RegisterWindowMessageW(L"DLUMA.EditorUi.Dispatch.1");
    ui.placement_message = RegisterWindowMessageW(L"DLUMA.EditorUi.PrimaryPlacement.1");
    if (!ui.dispatch_message || !ui.placement_message || !GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)(ULONG_PTR)ui_window_proc, &ui.module)) goto failed;
    ui.common_controls = LoadLibraryExW(L"comctl32.dll", NULL,
                                       LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ui.common_controls) goto failed;
    ui.set_subclass = (UiSetSubclass)(void *)GetProcAddress(
        ui.common_controls, "SetWindowSubclass");
    ui.remove_subclass = (UiRemoveSubclass)(void *)GetProcAddress(
        ui.common_controls, "RemoveWindowSubclass");
    ui.def_subclass = (UiDefSubclass)(void *)GetProcAddress(
        ui.common_controls, "DefSubclassProc");
    if (!ui.set_subclass || !ui.remove_subclass || !ui.def_subclass) goto failed;
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.lpfnWndProc = ui_dispatcher_proc;
    window_class.hInstance = (HINSTANCE)ui.module;
    window_class.lpszClassName = L"DLUMA.EditorUi.Dispatcher.1";
    if (!RegisterClassW(&window_class)) goto failed;
    ui.class_registered = 1;
    ui.stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ui.watcher_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    ui.watcher_go = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ui.stop_event || !ui.watcher_ready || !ui.watcher_go) goto failed;
    ui.watcher = CreateThread(NULL, 0, ui_watch, NULL, 0, NULL);
    if (ui.watcher) {
        if (WaitForSingleObject(ui.watcher_ready, 2000U) == WAIT_OBJECT_0 &&
            InterlockedCompareExchange(&ui.watcher_status, 0, 0) == 1 &&
            InterlockedCompareExchange(&ui.state, 2, 1) == 1) {
            (void)SetEvent(ui.watcher_go);
            InterlockedDecrement(&ui.callbacks);
            return 1;
        }
        InterlockedDecrement(&ui.callbacks);
        (void)dluma_editor_ui_stop();
        if (ui.watcher) (void)WaitForSingleObject(ui.watcher, 2000U);
        (void)dluma_editor_ui_stop();
        return 0;
    }
failed:
    /* A concurrent stop is nonblocking, but must finish reading the startup
     * handles before this path releases them. */
    AcquireSRWLockExclusive(&ui.lock);
    InterlockedExchange(&ui.state, -1);
    ReleaseSRWLockExclusive(&ui.lock);
    while (InterlockedCompareExchange(&ui.callbacks, 0, 0) > 1) Sleep(0);
    if (ui.stop_event) { CloseHandle(ui.stop_event); ui.stop_event = NULL; }
    if (ui.watcher_ready) { CloseHandle(ui.watcher_ready); ui.watcher_ready = NULL; }
    if (ui.watcher_go) { CloseHandle(ui.watcher_go); ui.watcher_go = NULL; }
    if (ui.class_registered) { (void)UnregisterClassW(L"DLUMA.EditorUi.Dispatcher.1", (HINSTANCE)ui.module); ui.class_registered = 0; }
    if (ui.common_controls) { FreeLibrary(ui.common_controls); ui.common_controls = NULL; }
    if (ui.tls_index != TLS_OUT_OF_INDEXES) TlsFree(ui.tls_index);
    ui.tls_index = TLS_OUT_OF_INDEXES;
    ui.dispatch_message = 0;
    ui.placement_message = 0;
    InterlockedDecrement(&ui.callbacks);
    return 0;
}

int dluma_editor_ui_callbacks_drained(void)
{
    unsigned int i;
    int empty;
    if (!ui.dispatch_message) return 1;
    if (InterlockedCompareExchange(&ui.callbacks, 0, 0)) return 0;
    AcquireSRWLockExclusive(&ui.lock);
    empty = ui.windows == NULL;
    for (i = 0; i < UI_OWNER_COUNT && empty; ++i)
        if (ui.subscriptions[i].references) empty = 0;
    ReleaseSRWLockExclusive(&ui.lock);
    if (!empty) return 0;
    for (i = 0; i < UI_THREAD_CAPACITY; ++i)
        if (ui.threads[i].dispatcher || ui.threads[i].getmessage_hook ||
            ui.threads[i].callwnd_hook) return 0;
    return !ui.watcher || WaitForSingleObject(ui.watcher, 0) == WAIT_OBJECT_0;
}

int dluma_editor_ui_stop(void)
{
    unsigned int i;
    AcquireSRWLockExclusive(&ui.lock);
    if (!ui.dispatch_message && InterlockedCompareExchange(&ui.state, 0, 0) != 1) { ReleaseSRWLockExclusive(&ui.lock); return 1; }
    if (InterlockedCompareExchange(&ui.state, 0, 0) == 4 || InterlockedCompareExchange(&ui.state, 0, 0) == -1) { ReleaseSRWLockExclusive(&ui.lock); return 0; }
    InterlockedIncrement(&ui.callbacks);
    InterlockedExchange(&ui.state, 3);
    ReleaseSRWLockExclusive(&ui.lock);
    if (ui.stop_event) (void)SetEvent(ui.stop_event);
    for (i = 0; i < UI_THREAD_CAPACITY; ++i) {
        UiThread *thread = &ui.threads[i];
        int dead;
        if (!thread->id) continue;
        dead = thread->handle && WaitForSingleObject(thread->handle, 0) == WAIT_OBJECT_0;
        if (dead) thread->dispatcher = NULL;
        if (!thread->dispatcher && InterlockedCompareExchange(&ui.callbacks, 0, 0) == 1) {
            UiWindow *record;
            int has_windows = 0;
            AcquireSRWLockExclusive(&ui.lock);
            for (record = ui.windows; record; record = record->next)
                if (record->thread_id == thread->id) { has_windows = 1; break; }
            ReleaseSRWLockExclusive(&ui.lock);
            if (!has_windows) {
                if (thread->getmessage_hook && UnhookWindowsHookEx(thread->getmessage_hook)) thread->getmessage_hook = NULL;
                if (thread->callwnd_hook && UnhookWindowsHookEx(thread->callwnd_hook)) thread->callwnd_hook = NULL;
            }
        }
        if (dead) continue;
        if (thread->id == GetCurrentThreadId()) ui_cleanup_thread(thread);
        else if (thread->dispatcher || thread->anchor)
            (void)PostMessageW(thread->dispatcher ? thread->dispatcher : thread->anchor, ui.dispatch_message, 0, 0);
    }
    InterlockedDecrement(&ui.callbacks);
    if (!dluma_editor_ui_callbacks_drained()) return 0;
    AcquireSRWLockExclusive(&ui.lock);
    if (InterlockedCompareExchange(&ui.callbacks, 0, 0) != 0 ||
        InterlockedCompareExchange(&ui.state, 4, 3) != 3) {
        ReleaseSRWLockExclusive(&ui.lock);
        return 0;
    }
    ReleaseSRWLockExclusive(&ui.lock);
    if (ui.class_registered && !UnregisterClassW(L"DLUMA.EditorUi.Dispatcher.1", (HINSTANCE)ui.module)) {
        InterlockedExchange(&ui.state, 3);
        return 0;
    }
    ui.class_registered = 0;
    for (i = 0; i < UI_THREAD_CAPACITY; ++i)
        if (ui.threads[i].handle) CloseHandle(ui.threads[i].handle);
    if (ui.watcher) CloseHandle(ui.watcher);
    if (ui.stop_event) CloseHandle(ui.stop_event);
    if (ui.watcher_ready) CloseHandle(ui.watcher_ready);
    if (ui.watcher_go) CloseHandle(ui.watcher_go);
    if (ui.common_controls) FreeLibrary(ui.common_controls);
    if (ui.tls_index != TLS_OUT_OF_INDEXES) TlsFree(ui.tls_index);
    ui.watcher = ui.stop_event = ui.watcher_ready = ui.watcher_go = NULL;
    ui.watcher_status = 0;
    ui.common_controls = NULL;
    ui.tls_index = TLS_OUT_OF_INDEXES;
    ui.dispatch_message = 0;
    ui.placement_message = 0;
    ZeroMemory(ui.threads, sizeof(ui.threads));
    ZeroMemory(ui.subscriptions, sizeof(ui.subscriptions));
    InterlockedExchange(&ui.state, 0);
    return 1;
}
