/* Exercises the actual broker dispatch and subscription lifetime without an Editor. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/dlw_editor_ui.c"

int dluma_dual_host_runtime_is_editor(void) { return 1; }
int dluma_editor_is_main_window(HWND window, DWORD pid) { (void)window; (void)pid; return 0; }
static int guard_count, feature_before, feature_after, feature_destroy, original_count, nested, drain_during, ready_during, armed_seen;
static UiWindow test_window;
static void require(int ok, const char *message) { if (!ok) { fprintf(stderr, "%s\n", message); exit(1); } }
static LRESULT WINAPI original(HWND w, UINT m, WPARAM p, LPARAM l)
{ (void)w; (void)m; (void)p; (void)l; ++original_count; return 77; }
static int guard(HWND w, unsigned int stage, UINT m, WPARAM p, LPARAM l, LRESULT *r, void *c);
static int feature(HWND w, unsigned int stage, UINT m, WPARAM p, LPARAM l, LRESULT *r, void *c)
{ (void)w; (void)p; (void)l; (void)r; (void)c; if (stage == DLUMA_EDITOR_UI_BEFORE) { ++feature_before; if (m == WM_USER + 4) { require(dluma_editor_ui_guard_subscribe(guard, NULL), "guard registration during dispatch failed"); ready_during = dluma_editor_ui_guard_ready(guard, NULL); } } if (stage == DLUMA_EDITOR_UI_AFTER) ++feature_after; if (stage == DLUMA_EDITOR_UI_DESTROY) ++feature_destroy; return 0; }
static int guard(HWND w, unsigned int stage, UINT m, WPARAM p, LPARAM l, LRESULT *r, void *c)
{
    (void)stage; (void)p; (void)l; (void)c; ++guard_count;
    if (m == WM_USER + 5 && dluma_editor_ui_guard_armed(guard, NULL)) { armed_seen = 1; *r = 55; return 1; }
    if (m == WM_COMMAND && !nested) {
        nested = 1;
        drain_during = dluma_editor_ui_guard_unsubscribe();
        (void)ui_window_proc(w, WM_USER + 3, 0, 0, UI_SUBCLASS_ID, (DWORD_PTR)&test_window);
        *r = 42; return 1;
    }
    return m == WM_DESTROY || m == WM_NCDESTROY;
}
static BOOL WINAPI remove_subclass(HWND w, SUBCLASSPROC p, UINT_PTR id)
{ (void)w; (void)p; (void)id; return TRUE; }
int main(void)
{
    LRESULT value;
    UiWindow *ending;
    UiInvocation held;
    uint64_t epoch, later;
    ui.tls_index = TlsAlloc(); require(ui.tls_index != TLS_OUT_OF_INDEXES, "TLS allocation failed");
    ui.state = 2; ui.dispatch_message = 1; ui.def_subclass = original;
    test_window.window = (HWND)(UINT_PTR)1; test_window.thread_id = GetCurrentThreadId();
    require(dluma_editor_ui_subscribe(DLUMA_EDITOR_UI_OWNER_GRAPH, feature, NULL), "feature subscription failed");
    value = ui_window_proc(test_window.window, WM_USER + 2, 0, 0, UI_SUBCLASS_ID, (DWORD_PTR)&test_window);
    require(value == 77 && original_count == 1 && feature_before == 1 && feature_after == 1, "No-guard dispatch changed");
    value = ui_window_proc(test_window.window, WM_USER + 4, 0, 0, UI_SUBCLASS_ID, (DWORD_PTR)&test_window);
    require(value == 77 && !ready_during && dluma_editor_ui_guard_ready(guard, NULL), "Guard admitted while a pre-guard dispatch was active");
    require(dluma_editor_ui_guard_unsubscribe(), "Barrier guard did not drain");
    feature_before = feature_after = original_count = 0;
    require(dluma_editor_ui_guard_subscribe(guard, NULL), "guard subscription failed");
    value = ui_window_proc(test_window.window, WM_COMMAND, 0, 0, UI_SUBCLASS_ID, (DWORD_PTR)&test_window);
    require(value == 42 && guard_count == 1 && drain_during == 0, "Guard ordering or lifetime failed");
    require(original_count == 1 && feature_before == 1 && feature_after == 1, "Consumed message reached normal dispatch");
    require(dluma_editor_ui_guard_unsubscribe(), "Guard did not drain after callback");
    nested = 0; require(dluma_editor_ui_guard_subscribe(guard, NULL), "guard resubscription failed");
    value = ui_window_proc(test_window.window, WM_DESTROY, 0, 0, UI_SUBCLASS_ID, (DWORD_PTR)&test_window);
    require(value == 77 && original_count == 2 && feature_before == 2 && feature_after == 2, "Lifetime message was blocked");
    ending = (UiWindow *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*ending));
    require(ending != NULL, "window allocation failed");
    ending->window = test_window.window; ending->thread_id = test_window.thread_id;
    ui.windows = ending; ui.remove_subclass = remove_subclass;
    value = ui_window_proc(ending->window, WM_NCDESTROY, 0, 0, UI_SUBCLASS_ID, (DWORD_PTR)ending);
    require(value == 77 && original_count == 3 && feature_before == 3 && feature_after == 3 && feature_destroy == 1 && ui.windows == NULL,
        "Nonclient destruction must reach original and retirement callbacks");
    /* A dispatch that already passed an installed observe-only guard must
     * finish after arming before a write may be admitted. */
    ui_invocation_begin(&held);
    require(held.guard.listener != NULL && !dluma_editor_ui_guard_armed(guard, NULL), "pre-arm snapshot missing");
    require(!held.guard.listener(test_window.window, DLUMA_EDITOR_UI_BEFORE, WM_USER + 6, 0, 0, &value, held.guard.context), "observe-only guard did not pass");
    epoch = dluma_editor_ui_guard_arm(guard, NULL);
    require(epoch && dluma_editor_ui_guard_armed(guard, NULL) &&
        !dluma_editor_ui_guard_arm_ready(guard, NULL, epoch), "in-flight dispatch did not hold barrier");
    value = ui_window_proc(test_window.window, WM_USER + 5, 0, 0, UI_SUBCLASS_ID, (DWORD_PTR)&test_window);
    require(value == 55 && armed_seen, "new dispatch bypassed armed guard");
    require(!dluma_editor_ui_guard_unsubscribe(), "armed guard unsubscribed early");
    ui_invocation_end(&held);
    require(dluma_editor_ui_guard_arm_ready(guard, NULL, epoch), "barrier did not drain");
    {
        int prior_original = original_count, prior_before = feature_before, prior_after = feature_after;
        value = ui_window_proc(test_window.window, WM_DESTROY, 0, 0, UI_SUBCLASS_ID, (DWORD_PTR)&test_window);
        require(value == 77 && original_count == prior_original + 1 && feature_before == prior_before + 1 &&
            feature_after == prior_after + 1, "Armed coverage loss changed lifetime/Graph dispatch");
    }
    require(!dluma_editor_ui_guard_disarm(guard, NULL, epoch + 1), "wrong epoch disarmed guard");
    ui_invocation_begin(&held);
    require(dluma_editor_ui_guard_disarm(guard, NULL, epoch), "guard did not disarm");
    later = dluma_editor_ui_guard_arm(guard, NULL);
    require(later && later != epoch && !dluma_editor_ui_guard_arm_ready(guard, NULL, epoch) &&
        !dluma_editor_ui_guard_arm_ready(guard, NULL, later), "old epoch or callback bypassed new barrier");
    ui_invocation_end(&held);
    require(dluma_editor_ui_guard_arm_ready(guard, NULL, later), "new epoch did not drain");
    require(dluma_editor_ui_guard_disarm(guard, NULL, later), "second guard did not disarm");
    require(dluma_editor_ui_guard_unsubscribe(), "Guard lifetime subscription did not drain");
    require(dluma_editor_ui_unsubscribe(DLUMA_EDITOR_UI_OWNER_GRAPH), "Feature did not drain");
    ui.state = 3;
    require(!dluma_editor_ui_guard_subscribe(guard, NULL), "Guard registration succeeded while stopping");
    TlsFree(ui.tls_index);
    puts("Editor UI guard dispatch contract passed.");
    return 0;
}
