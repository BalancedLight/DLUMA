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
/* Stock Editor projects can expose more than a thousand native child windows.
 * Keep both enumeration and the heap snapshot bounded by one shared limit. */
#define UI_COVERAGE_CAPACITY 8192U
/* Owner IDs are contiguous and one-based. Keep storage aligned with the
 * highest declared subscriber instead of a separate hardcoded capacity. */
#define UI_OWNER_COUNT DLUMA_EDITOR_UI_OWNER_SOURCE_CONTROL
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
    UiSubscription guard;
    int unguarded;
    uint64_t start_guard_epoch;
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
    volatile LONG coverage_fault;
} UiThread;

typedef BOOL (WINAPI *UiSetSubclass)(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR);
typedef BOOL (WINAPI *UiRemoveSubclass)(HWND, SUBCLASSPROC, UINT_PTR);
typedef BOOL (WINAPI *UiGetSubclass)(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR *);
typedef LRESULT (WINAPI *UiDefSubclass)(HWND, UINT, WPARAM, LPARAM);

static struct {
    SRWLOCK lock;
    volatile LONG state;
    volatile LONG activated;
    volatile LONG callbacks;
    volatile LONG refresh_generation;
    volatile LONG64 next_identity;
    volatile LONG64 coverage_generation;
    DWORD tls_index;
    UINT dispatch_message;
    UINT placement_message;
    HMODULE module, common_controls;
    HANDLE watcher, stop_event, watcher_ready, watcher_go;
    volatile LONG watcher_status;
    int class_registered;
    UiSetSubclass set_subclass;
    UiRemoveSubclass remove_subclass;
    UiGetSubclass get_subclass;
    UiDefSubclass def_subclass;
    UiWindow *windows;
    HWND armed_queue[UI_QUEUE_CAPACITY];
    unsigned int armed_count;
    UiThread threads[UI_THREAD_CAPACITY];
    UiSubscription subscriptions[UI_OWNER_COUNT];
    UiSubscription guard;
    unsigned int unguarded_invocations, invocations, guard_arm_pending;
    uint64_t guard_epoch, next_guard_epoch;
} ui;
/* A broker restart in the same Editor process must not move the frame again. */
static volatile LONG primary_placement_state;

static LRESULT CALLBACK ui_window_proc(HWND, UINT, WPARAM, LPARAM,
                                       UINT_PTR, DWORD_PTR);
static LRESULT CALLBACK ui_dispatcher_proc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK ui_getmessage_hook(int, WPARAM, LPARAM);
static LRESULT CALLBACK ui_callwnd_hook(int, WPARAM, LPARAM);
static int ui_enroll_owner(HWND window);
static void ui_coverage_invalidate(void)
{
    /* Zero is reserved for an unavailable snapshot. A wrap is permanently
     * fail-closed rather than accepting a recycled generation. */
    if (InterlockedIncrement64(&ui.coverage_generation) <= 0)
        InterlockedExchange64(&ui.coverage_generation, -1);
}

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
    invocation->start_guard_epoch = ui.next_guard_epoch;
    ++ui.invocations;
    if (ui.guard.active) {
        invocation->guard = ui.guard;
        ++ui.guard.references;
    } else {
        invocation->unguarded = 1;
        ++ui.unguarded_invocations;
    }
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
    if (ui.guard_epoch && invocation->start_guard_epoch < ui.guard_epoch && ui.guard_arm_pending)
        --ui.guard_arm_pending;
    --ui.invocations;
    if (invocation->guard.listener) --ui.guard.references;
    if (invocation->unguarded) --ui.unguarded_invocations;
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

/* A synchronous native window change on a hooked UI thread invalidates an
 * armed coverage snapshot before the affected window procedure proceeds. */
static void ui_coverage_notify_loss(HWND window)
{
    UiInvocation invocation;
    LRESULT ignored = 0;
    ui_invocation_begin(&invocation);
    if (invocation.guard.listener &&
        dluma_editor_ui_guard_armed(invocation.guard.listener, invocation.guard.context))
        (void)invocation.guard.listener(window, DLUMA_EDITOR_UI_COVERAGE_LOST,
            0, 0, 0, &ignored, invocation.guard.context);
    ui_invocation_end(&invocation);
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
    ui_coverage_invalidate();
    ui_coverage_notify_loss(record->window);
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
        if (message == WM_DESTROY || message == WM_NCDESTROY) {
            ui_coverage_invalidate();
            ui_coverage_notify_loss(window);
        }
        int guarded = invocation.guard.listener && invocation.guard.listener(window,
            DLUMA_EDITOR_UI_BEFORE, message, wparam, lparam, &result, invocation.guard.context);
        /* Lifetime messages always reach normal listeners and the original procedure. */
        if (!guarded || message == WM_NCDESTROY || message == WM_DESTROY) {
            handled = ui_notify(&invocation, window, DLUMA_EDITOR_UI_BEFORE,
                             message, wparam, lparam, &result);
            if (!handled || message == WM_NCDESTROY || message == WM_DESTROY)
                result = ui.def_subclass(window, message, wparam, lparam);
            (void)ui_notify(&invocation, window, DLUMA_EDITOR_UI_AFTER,
                        message, wparam, lparam, &result);
        }
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
    if (!ui_running() || !InterlockedCompareExchange(&ui.activated,0,0) || !window || !thread_id ||
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
        thread->dispatcher || thread->bootstrapping || !ui_running() ||
        !InterlockedCompareExchange(&ui.activated,0,0)) return;
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
        if (ui_running() && InterlockedCompareExchange(&ui.activated,0,0)) ui_bootstrap(thread);
        else if (thread && InterlockedCompareExchange(&ui.state, 0, 0) == 3)
            ui_cleanup_thread(thread);
    }
    InterlockedDecrement(&ui.callbacks);
    return CallNextHookEx(NULL, code, wparam, lparam);
}

static LRESULT CALLBACK ui_callwnd_hook(int code, WPARAM wparam, LPARAM lparam)
{
    InterlockedIncrement(&ui.callbacks);
    if (code >= 0 && ui_running() && InterlockedCompareExchange(&ui.activated,0,0)) {
        const CWPSTRUCT *message = (const CWPSTRUCT *)lparam;
        UiThread *thread = ui_thread(GetCurrentThreadId());
        ui_bootstrap(thread);
        if (thread && thread->dispatcher && message && message->hwnd != thread->dispatcher &&
            (message->message == WM_CREATE || message->message == WM_SHOWWINDOW)) {
            ui_coverage_invalidate();
            ui_coverage_notify_loss(message->hwnd);
            if (!ui_enroll_owner(message->hwnd)) InterlockedExchange(&thread->coverage_fault, 1);
        }
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
    if (!ui.activated) {
        for (i=0; i<ui.armed_count; ++i) if (ui.armed_queue[i]==window) break;
        if (i==ui.armed_count) {
            if (ui.armed_count==UI_QUEUE_CAPACITY) {
                ReleaseSRWLockExclusive(&ui.lock); InterlockedDecrement(&ui.callbacks); return 0;
            }
            ui.armed_queue[ui.armed_count++]=window;
        }
        ReleaseSRWLockExclusive(&ui.lock); InterlockedDecrement(&ui.callbacks); return 1;
    }
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
    if (!InterlockedCompareExchange(&ui.activated,0,0)) return;
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
    if (object == OBJID_WINDOW && child == CHILDID_SELF && ui_running()) {
        if (event == EVENT_OBJECT_CREATE || event == EVENT_OBJECT_SHOW) {
            ui_coverage_invalidate();
            if (!dluma_editor_ui_request_enroll(window)) {
                UiThread *owner = ui_thread(thread);
                if (owner) InterlockedExchange(&owner->coverage_fault, 1);
            }
        } else if (event == EVENT_OBJECT_DESTROY) ui_coverage_invalidate();
    }
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
    for (;;) {
        HWND window = NULL;
        AcquireSRWLockExclusive(&ui.lock);
        if (ui.armed_count) window=ui.armed_queue[--ui.armed_count];
        ReleaseSRWLockExclusive(&ui.lock);
        if (!window || !ui_running()) break;
        (void)dluma_editor_ui_request_enroll(window);
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

int dluma_editor_ui_guard_subscribe(DlumaEditorUiListener listener, void *context)
{
    int same;
    if (!listener || !ui_running()) return 0;
    AcquireSRWLockExclusive(&ui.lock);
    if (!ui_running()) { ReleaseSRWLockExclusive(&ui.lock); return 0; }
    if (ui.guard.references || ui.guard.active) {
        same = ui.guard.active && ui.guard.listener == listener && ui.guard.context == context;
        ReleaseSRWLockExclusive(&ui.lock);
        return same;
    }
    ui.guard.listener = listener;
    ui.guard.context = context;
    ui.guard.active = 1;
    ReleaseSRWLockExclusive(&ui.lock);
    return 1;
}

int dluma_editor_ui_guard_unsubscribe(void)
{
    int drained;
    AcquireSRWLockExclusive(&ui.lock);
    if (ui.guard_epoch) { ReleaseSRWLockExclusive(&ui.lock); return 0; }
    ui.guard.active = 0;
    drained = ui.guard.references == 0;
    if (drained) ZeroMemory(&ui.guard, sizeof(ui.guard));
    ReleaseSRWLockExclusive(&ui.lock);
    return drained;
}

int dluma_editor_ui_guard_ready(DlumaEditorUiListener listener, void *context)
{
    int ready;
    AcquireSRWLockShared(&ui.lock);
    ready = ui_running() && ui.guard.active && ui.guard.listener == listener &&
        ui.guard.context == context && ui.unguarded_invocations == 0;
    ReleaseSRWLockShared(&ui.lock);
    return ready;
}

uint64_t dluma_editor_ui_guard_arm(DlumaEditorUiListener listener, void *context)
{
    uint64_t epoch = 0;
    AcquireSRWLockExclusive(&ui.lock);
    if (ui_running() && ui.guard.active && ui.guard.listener == listener && ui.guard.context == context) {
        if (ui.guard_epoch) epoch = ui.guard_epoch;
        else if (ui.next_guard_epoch != UINT64_MAX) {
            epoch = ++ui.next_guard_epoch;
            ui.guard_epoch = epoch;
            ui.guard_arm_pending = ui.invocations;
        }
    }
    ReleaseSRWLockExclusive(&ui.lock);
    return epoch;
}

int dluma_editor_ui_guard_armed(DlumaEditorUiListener listener, void *context)
{
    int armed;
    AcquireSRWLockShared(&ui.lock);
    armed = ui_running() && ui.guard.active && ui.guard.listener == listener &&
        ui.guard.context == context && ui.guard_epoch != 0;
    ReleaseSRWLockShared(&ui.lock);
    return armed;
}

int dluma_editor_ui_guard_arm_ready(DlumaEditorUiListener listener, void *context, uint64_t epoch)
{
    int ready;
    AcquireSRWLockShared(&ui.lock);
    ready = epoch && ui_running() && ui.guard.active && ui.guard.listener == listener &&
        ui.guard.context == context && ui.guard_epoch == epoch && ui.guard_arm_pending == 0;
    ReleaseSRWLockShared(&ui.lock);
    return ready;
}

int dluma_editor_ui_guard_disarm(DlumaEditorUiListener listener, void *context, uint64_t epoch)
{
    int result = 0;
    AcquireSRWLockExclusive(&ui.lock);
    if (epoch && ui.guard.active && ui.guard.listener == listener && ui.guard.context == context &&
        ui.guard_epoch == epoch) {
        ui.guard_epoch = 0;
        ui.guard_arm_pending = 0;
        result = 1;
    }
    ReleaseSRWLockExclusive(&ui.lock);
    return result;
}

typedef struct UiCoverageScan {
    HWND main_window;
    DWORD owner_thread;
    unsigned int count;
    int status, found_main;
    int verify_only;
    const DlumaEditorUiCoverageForeign *foreign;
    DlumaEditorUiCoverageDetail *detail;
} UiCoverageScan;

static void ui_coverage_detail(UiCoverageScan *scan, unsigned int reason, HWND window, DWORD pid, DWORD thread)
{
    if (!scan->detail || scan->detail->reason) return;
    scan->detail->reason = reason; scan->detail->window = window;
    scan->detail->process_id = pid; scan->detail->thread_id = thread; scan->detail->count = scan->count;
}

static int ui_coverage_foreign_valid(HWND main_window, const DlumaEditorUiCoverageForeign *foreign)
{
    DWORD pid = 0;
    FILETIME created, exited, kernel, user;
    ULARGE_INTEGER value;
    if (!foreign || !foreign->child || !foreign->parent || !foreign->process ||
        !foreign->process_id || !foreign->binding_generation || !foreign->process_creation_time ||
        !IsWindow(foreign->child) || !IsWindow(foreign->parent) ||
        !(GetWindowLongPtrW(foreign->child, GWL_STYLE) & WS_CHILD) ||
        GetParent(foreign->child) != foreign->parent || !IsChild(main_window, foreign->parent) ||
        !IsChild(main_window, foreign->child) || !GetWindowThreadProcessId(foreign->child, &pid) ||
        pid != foreign->process_id || GetProcessId(foreign->process) != pid ||
        WaitForSingleObject(foreign->process, 0) != WAIT_TIMEOUT ||
        !GetProcessTimes(foreign->process, &created, &exited, &kernel, &user)) return 0;
    value.LowPart = created.dwLowDateTime; value.HighPart = created.dwHighDateTime;
    return value.QuadPart == foreign->process_creation_time;
}

static BOOL CALLBACK ui_coverage_process_top(HWND window, LPARAM value)
{
    UiCoverageScan *scan = (UiCoverageScan *)value;
    DWORD pid = 0, owner = GetWindowThreadProcessId(window, &pid);
    if (!owner || !pid) {
        ui_coverage_detail(scan, 3U, window, pid, owner);
        scan->status = DLUMA_EDITOR_UI_COVERAGE_FAILED;
        return FALSE;
    }
    if (pid == GetCurrentProcessId() && owner != scan->owner_thread && IsWindowVisible(window)) {
        ui_coverage_detail(scan, 4U, window, pid, owner);
        scan->status = DLUMA_EDITOR_UI_COVERAGE_PENDING;
        return FALSE;
    }
    return TRUE;
}

static int ui_coverage_process_windows(DWORD owner, DlumaEditorUiCoverageDetail *detail)
{
    UiCoverageScan scan;
    ZeroMemory(&scan, sizeof(scan));
    scan.owner_thread = owner; scan.detail = detail;
    scan.status = DLUMA_EDITOR_UI_COVERAGE_READY;
    if (!EnumWindows(ui_coverage_process_top, (LPARAM)&scan) &&
        scan.status == DLUMA_EDITOR_UI_COVERAGE_READY) { ui_coverage_detail(&scan, 5U, NULL, 0, 0); return DLUMA_EDITOR_UI_COVERAGE_FAILED; }
    return scan.status;
}

static BOOL CALLBACK ui_coverage_child(HWND window, LPARAM value)
{
    UiCoverageScan *scan = (UiCoverageScan *)value;
    DWORD pid = 0, owner = GetWindowThreadProcessId(window, &pid);
    int valid = 0;
    if (++scan->count <= UI_COVERAGE_CAPACITY && owner && pid) {
        if (pid == GetCurrentProcessId() && owner == scan->owner_thread) {
            if (!scan->verify_only) valid = ui_enroll_owner(window);
            else {
                UiWindow *record; DWORD_PTR actual = 0;
                AcquireSRWLockShared(&ui.lock);
                for (record = ui.windows; record; record = record->next)
                    if (record->window == window && !record->detached && record->thread_id == owner) break;
                ReleaseSRWLockShared(&ui.lock);
                valid = record && ui.get_subclass && ui.get_subclass(window, ui_window_proc, UI_SUBCLASS_ID, &actual) &&
                    actual == (DWORD_PTR)record;
            }
        } else if (scan->foreign && pid == scan->foreign->process_id &&
                   (window == scan->foreign->child || IsChild(scan->foreign->child, window))) valid = 1;
    }
    if (!valid) {
        ui_coverage_detail(scan, scan->count > UI_COVERAGE_CAPACITY ? 11U :
            pid != GetCurrentProcessId() ? 12U : owner != scan->owner_thread ? 13U : 14U,
            window, pid, owner);
        scan->status = DLUMA_EDITOR_UI_COVERAGE_FAILED;
        return FALSE;
    }
    return TRUE;
}

static BOOL CALLBACK ui_coverage_top(HWND window, LPARAM value)
{
    UiCoverageScan *scan = (UiCoverageScan *)value;
    if (window == scan->main_window) scan->found_main = 1;
    else if (IsWindowVisible(window)) {
        ui_coverage_detail(scan, 15U, window, GetCurrentProcessId(), scan->owner_thread);
        /* A visible prompt/modal may later open Options without a new main
         * command. Refuse conservatively rather than closing user windows. */
        scan->status = DLUMA_EDITOR_UI_COVERAGE_PENDING;
        return FALSE;
    }
    if (!ui_coverage_child(window, value)) return FALSE;
    (void)EnumChildWindows(window, ui_coverage_child, value);
    return scan->status == DLUMA_EDITOR_UI_COVERAGE_READY;
}

static int ui_coverage_dispatcher_owned(UiThread *thread, HWND dispatcher, DWORD owner)
{
    WCHAR class_name[64];
    DWORD pid = 0;
    return dispatcher && IsWindow(dispatcher) &&
        GetWindowThreadProcessId(dispatcher, &pid) == owner && pid == GetCurrentProcessId() &&
        GetClassNameW(dispatcher, class_name, 64) > 0 &&
        !lstrcmpW(class_name, L"DLUMA.EditorUi.Dispatcher.1") &&
        (UiThread *)(ULONG_PTR)GetWindowLongPtrW(dispatcher, GWLP_USERDATA) == thread;
}

int dluma_editor_ui_guard_coverage_current_foreign(DlumaEditorUiListener listener, void *context,
    HWND main_window, const DlumaEditorUiCoverageForeign *foreign, uint64_t generation)
{
    typedef struct UiCoverageEntry { HWND window; DWORD_PTR record; } UiCoverageEntry;
    UiCoverageEntry *entries;
    UiWindow *record;
    UiThread *thread;
    HWND dispatcher;
    DWORD pid = 0, owner = GetWindowThreadProcessId(main_window, &pid);
    unsigned int count = 0, i;
    int valid;
    if (!generation || !owner || owner != GetCurrentThreadId() || pid != GetCurrentProcessId() ||
        !IsWindow(main_window) || !IsWindowVisible(main_window) || !IsWindowEnabled(main_window)) return 0;
    if (foreign && !ui_coverage_foreign_valid(main_window, foreign)) return 0;
    if (ui_coverage_process_windows(owner, NULL) != DLUMA_EDITOR_UI_COVERAGE_READY) return 0;
    thread = ui_thread(owner);
    if (!thread || !ui.get_subclass) return 0;
    entries = (UiCoverageEntry *)HeapAlloc(GetProcessHeap(), 0, UI_COVERAGE_CAPACITY * sizeof(*entries));
    if (!entries) return 0;
    AcquireSRWLockShared(&ui.lock);
    dispatcher = thread->dispatcher;
    valid = ui_running() && ui.activated && ui.guard.active && ui.guard.listener == listener &&
        ui.guard.context == context && dispatcher && thread->getmessage_hook &&
        thread->callwnd_hook && !thread->hooks_installing && !thread->count &&
        !thread->coverage_fault && (uint64_t)InterlockedCompareExchange64(&ui.coverage_generation, 0, 0) == generation;
    for (record = ui.windows; valid && record; record = record->next) {
        if (record->thread_id != owner || record->detached) continue;
        if (count == UI_COVERAGE_CAPACITY) { valid = 0; break; }
        entries[count].window = record->window;
        entries[count++].record = (DWORD_PTR)record;
    }
    ReleaseSRWLockShared(&ui.lock);
    if (!count || !ui_coverage_dispatcher_owned(thread, dispatcher, owner)) valid = 0;
    for (i = 0; valid && i < count; ++i) {
        DWORD_PTR actual = 0;
        if (!IsWindow(entries[i].window) || !ui.get_subclass(entries[i].window,
                ui_window_proc, UI_SUBCLASS_ID, &actual) || actual != entries[i].record) valid = 0;
    }
    HeapFree(GetProcessHeap(), 0, entries);
    if (valid && foreign) {
        UiCoverageScan scan;
        ZeroMemory(&scan, sizeof(scan)); scan.main_window = main_window; scan.owner_thread = owner;
        scan.status = DLUMA_EDITOR_UI_COVERAGE_READY; scan.verify_only = 1; scan.foreign = foreign;
        (void)EnumChildWindows(main_window, ui_coverage_child, (LPARAM)&scan);
        if (scan.status != DLUMA_EDITOR_UI_COVERAGE_READY || !ui_coverage_foreign_valid(main_window, foreign)) valid = 0;
    }
    AcquireSRWLockShared(&ui.lock);
    valid = valid && ui_running() && ui.activated && ui.guard.active && ui.guard.listener == listener &&
        ui.guard.context == context && thread->dispatcher == dispatcher && thread->getmessage_hook &&
        thread->callwnd_hook && !thread->hooks_installing && !thread->count && !thread->coverage_fault;
    ReleaseSRWLockShared(&ui.lock);
    if (!valid || (uint64_t)InterlockedCompareExchange64(&ui.coverage_generation, 0, 0) != generation) {
        ui_coverage_invalidate();
        return 0;
    }
    return 1;
}

int dluma_editor_ui_guard_coverage_current(DlumaEditorUiListener listener, void *context,
    HWND main_window, uint64_t generation)
{
    return dluma_editor_ui_guard_coverage_current_foreign(listener, context, main_window, NULL, generation);
}

int dluma_editor_ui_guard_coverage_capture_foreign(DlumaEditorUiListener listener, void *context,
    HWND main_window, const DlumaEditorUiCoverageForeign *foreign, uint64_t *generation,
    DlumaEditorUiCoverageDetail *detail)
{
    UiThread *thread;
    UiCoverageScan scan;
    HWND dispatcher;
    DWORD pid = 0, owner;
    LONG64 before;
    int available, pending;
    if (detail) ZeroMemory(detail, sizeof(*detail));
    if (!generation) { if (detail) detail->reason = 1U; return DLUMA_EDITOR_UI_COVERAGE_FAILED; }
    *generation = 0;
    owner = GetWindowThreadProcessId(main_window, &pid);
    if (!owner || owner != GetCurrentThreadId() || pid != GetCurrentProcessId() ||
        !IsWindow(main_window)) { if (detail) { detail->reason = 1U; detail->window = main_window; detail->process_id = pid; detail->thread_id = owner; } return DLUMA_EDITOR_UI_COVERAGE_FAILED; }
    if (foreign && !ui_coverage_foreign_valid(main_window, foreign)) { if (detail) detail->reason = 20U; return DLUMA_EDITOR_UI_COVERAGE_FAILED; }
    if (!IsWindowVisible(main_window) || !IsWindowEnabled(main_window)) { if (detail) detail->reason = 2U; return DLUMA_EDITOR_UI_COVERAGE_PENDING; }
    {
        int process_windows = ui_coverage_process_windows(owner, detail);
        if (process_windows != DLUMA_EDITOR_UI_COVERAGE_READY) return process_windows;
    }
    thread = ui_thread(owner);
    if (!thread) { if (detail) detail->reason = 6U; return DLUMA_EDITOR_UI_COVERAGE_FAILED; }
    AcquireSRWLockExclusive(&ui.lock);
    dispatcher = thread->dispatcher;
    available = ui_running() && ui.activated && ui.guard.active && ui.guard.listener == listener &&
        ui.guard.context == context && !ui.guard_epoch && dispatcher &&
        thread->getmessage_hook && thread->callwnd_hook && ui.get_subclass && !thread->hooks_installing;
    pending = thread->count != 0;
    if (available && !pending) InterlockedExchange(&thread->coverage_fault, 0);
    ReleaseSRWLockExclusive(&ui.lock);
    if (!available) { if (detail) detail->reason = 7U; return DLUMA_EDITOR_UI_COVERAGE_FAILED; }
    if (!ui_coverage_dispatcher_owned(thread, dispatcher, owner)) { if (detail) { detail->reason = 8U; detail->window = dispatcher; } return DLUMA_EDITOR_UI_COVERAGE_FAILED; }
    if (pending) { if (detail) detail->reason = 9U; return DLUMA_EDITOR_UI_COVERAGE_PENDING; }
    if (InterlockedCompareExchange64(&ui.coverage_generation, 1, 0) < 0)
        { if (detail) detail->reason = 10U; return DLUMA_EDITOR_UI_COVERAGE_FAILED; }
    before = InterlockedCompareExchange64(&ui.coverage_generation, 0, 0);
    ZeroMemory(&scan, sizeof(scan)); scan.main_window = main_window; scan.owner_thread = owner;
    scan.status = DLUMA_EDITOR_UI_COVERAGE_READY; scan.foreign = foreign; scan.detail = detail;
    if (!EnumThreadWindows(owner, ui_coverage_top, (LPARAM)&scan) &&
        scan.status == DLUMA_EDITOR_UI_COVERAGE_READY) { if (detail) detail->reason = 16U; return DLUMA_EDITOR_UI_COVERAGE_FAILED; }
    if (scan.status != DLUMA_EDITOR_UI_COVERAGE_READY) return scan.status;
    if (!scan.found_main || InterlockedCompareExchange(&thread->coverage_fault, 0, 0)) {
        if (detail) detail->reason = !scan.found_main ? 17U : 18U; return DLUMA_EDITOR_UI_COVERAGE_FAILED;
    }
    if (before != InterlockedCompareExchange64(&ui.coverage_generation, 0, 0))
        { if (detail) detail->reason = 19U; return DLUMA_EDITOR_UI_COVERAGE_PENDING; }
    if (!dluma_editor_ui_guard_coverage_current_foreign(listener, context, main_window, foreign, (uint64_t)before)) {
        if (detail) detail->reason = 21U; return DLUMA_EDITOR_UI_COVERAGE_PENDING;
    }
    *generation = (uint64_t)before;
    return DLUMA_EDITOR_UI_COVERAGE_READY;
}

int dluma_editor_ui_guard_coverage_capture(DlumaEditorUiListener listener, void *context,
    HWND main_window, uint64_t *generation)
{
    return dluma_editor_ui_guard_coverage_capture_foreign(listener, context, main_window, NULL, generation, NULL);
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
    ui.get_subclass = (UiGetSubclass)(void *)GetProcAddress(
        ui.common_controls, "GetWindowSubclass");
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

int dluma_editor_ui_activate(void)
{
    int activated = 0;
    AcquireSRWLockExclusive(&ui.lock);
    if (ui_running() && ui.watcher_go) {
        if (ui.activated) activated = 1;
        else {
            InterlockedExchange(&ui.activated,1);
            if (SetEvent(ui.watcher_go)) activated = 1;
            else InterlockedExchange(&ui.activated,0);
        }
    }
    ReleaseSRWLockExclusive(&ui.lock);
    return activated;
}

int dluma_editor_ui_callbacks_drained(void)
{
    unsigned int i;
    int empty;
    if (!ui.dispatch_message) return 1;
    if (InterlockedCompareExchange(&ui.callbacks, 0, 0)) return 0;
    AcquireSRWLockExclusive(&ui.lock);
    empty = ui.windows == NULL && ui.guard.references == 0 && ui.invocations == 0;
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
    InterlockedExchange(&ui.activated, 0);
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
    ui.get_subclass = NULL;
    ui.tls_index = TLS_OUT_OF_INDEXES;
    ui.dispatch_message = 0;
    ui.placement_message = 0;
    ZeroMemory(ui.threads, sizeof(ui.threads));
    ZeroMemory(ui.armed_queue, sizeof(ui.armed_queue)); ui.armed_count = 0;
    ZeroMemory(ui.subscriptions, sizeof(ui.subscriptions));
    AcquireSRWLockExclusive(&ui.lock);
    ZeroMemory(&ui.guard, sizeof(ui.guard));
    ui.unguarded_invocations = 0;
    ui.invocations = ui.guard_arm_pending = 0;
    ui.guard_epoch = 0;
    InterlockedExchange(&ui.state, 0);
    ReleaseSRWLockExclusive(&ui.lock);
    return 1;
}
