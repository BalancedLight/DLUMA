/* Actual broker coverage code against hidden in-process windows. */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
static HWND pretend_visible_main, pretend_visible_prompt, fail_subclass;
static int coverage_losses;
static HANDLE foreign_ready, foreign_stop;
static HWND foreign_window;
static DWORD WINAPI foreign_window_thread(void *unused)
{
    (void)unused;
    foreign_window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 1, 1,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    SetEvent(foreign_ready);
    WaitForSingleObject(foreign_stop, 10000);
    if (foreign_window) DestroyWindow(foreign_window);
    return 0;
}
static BOOL test_visible(HWND w) { return w == pretend_visible_main || w == pretend_visible_prompt || IsWindowVisible(w); }
#define IsWindowVisible test_visible
#include "../src/dlw_editor_ui.c"
#undef IsWindowVisible
#include "../src/dlw_editor_panel_host.c"
int dluma_editor_theme_copy_snapshot(DlumaEditorThemeResolved *out) { (void)out; return 0; }
int dluma_dual_host_runtime_is_editor(void) { return 1; }
int dluma_editor_is_main_window(HWND w, DWORD p) { (void)w; (void)p; return 0; }
static int guard(HWND w, unsigned int s, UINT m, WPARAM p, LPARAM l, LRESULT *r, void *c)
{ (void)w;(void)m;(void)p;(void)l;(void)r;(void)c;if (s == DLUMA_EDITOR_UI_COVERAGE_LOST) ++coverage_losses;return 0; }
static BOOL WINAPI set_subclass(HWND w, SUBCLASSPROC p, UINT_PTR id, DWORD_PTR data)
{ return w != fail_subclass && SetWindowSubclass(w, p, id, data); }
static BOOL WINAPI remove_subclass(HWND w, SUBCLASSPROC p, UINT_PTR id)
{ return RemoveWindowSubclass(w, p, id); }
static LRESULT WINAPI original(HWND w, UINT m, WPARAM p, LPARAM l)
{ (void)w;(void)m;(void)p;(void)l;return 0; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char **argv)
{
    HWND main_window, child, prompt, dispatcher;
    HANDLE foreign_thread;
    if (argc == 6 && !strcmp(argv[1], "--foreign-child")) {
        HWND parent = (HWND)(UINT_PTR)_strtoui64(argv[2], NULL, 16);
        HANDLE stop = (HANDLE)(UINT_PTR)_strtoui64(argv[3], NULL, 16);
        HWND notify = (HWND)(UINT_PTR)_strtoui64(argv[4], NULL, 16);
        HANDLE create_sibling = (HANDLE)(UINT_PTR)_strtoui64(argv[5], NULL, 16);
        HANDLE events[2] = {stop, create_sibling}; HWND sibling = NULL;
        HWND window = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 1, 1,
            parent, NULL, GetModuleHandleW(NULL), NULL);
        if (!window || !PostMessageW(notify, WM_APP + 61, (WPARAM)window, 0)) return 2;
        if (WaitForMultipleObjects(2, events, FALSE, 10000) == WAIT_OBJECT_0 + 1) {
            sibling = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 1, 1,
                parent, NULL, GetModuleHandleW(NULL), NULL);
            if (sibling) (void)PostMessageW(notify, WM_APP + 62, (WPARAM)sibling, 0);
            (void)WaitForSingleObject(stop, 10000);
        }
        if (sibling) DestroyWindow(sibling);
        DestroyWindow(window); CloseHandle(stop); return 0;
    }
    CHECK(argc == 1);
    WNDCLASSW dispatcher_class;
    uint64_t generation = 0, epoch;
    CWPSTRUCT event;
    UiWindow *record;
    ui.tls_index = TlsAlloc(); CHECK(ui.tls_index != TLS_OUT_OF_INDEXES);
    ui.state = 2; ui.activated = 1; ui.dispatch_message = RegisterWindowMessageW(L"DLUMA.TestCoverage.Dispatch"); CHECK(ui.dispatch_message);
    ui.set_subclass = set_subclass; ui.remove_subclass = remove_subclass;
    ui.get_subclass = GetWindowSubclass; ui.def_subclass = original;
    main_window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 1, 1, NULL, NULL, GetModuleHandleW(NULL), NULL); CHECK(main_window);
    child = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 1, 1, main_window, NULL, GetModuleHandleW(NULL), NULL); CHECK(child);
    pretend_visible_main = main_window;
    ui.threads[0].id = GetCurrentThreadId();
    ZeroMemory(&dispatcher_class, sizeof(dispatcher_class));
    dispatcher_class.lpfnWndProc = ui_dispatcher_proc; dispatcher_class.hInstance = GetModuleHandleW(NULL);
    dispatcher_class.lpszClassName = L"DLUMA.EditorUi.Dispatcher.1";
    CHECK(RegisterClassW(&dispatcher_class));
    dispatcher = CreateWindowExW(0, dispatcher_class.lpszClassName, L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, dispatcher_class.hInstance, &ui.threads[0]); CHECK(dispatcher);
    ui.threads[0].dispatcher = dispatcher;
    ui.threads[0].getmessage_hook = (HHOOK)(UINT_PTR)1; ui.threads[0].callwnd_hook = (HHOOK)(UINT_PTR)1;
    CHECK(dluma_editor_ui_guard_subscribe(guard, NULL));
    ui.threads[0].dispatcher = (HWND)(UINT_PTR)1;
    CHECK(dluma_editor_ui_guard_coverage_capture(guard, NULL, main_window, &generation) == DLUMA_EDITOR_UI_COVERAGE_FAILED);
    ui.threads[0].dispatcher = main_window;
    CHECK(dluma_editor_ui_guard_coverage_capture(guard, NULL, main_window, &generation) == DLUMA_EDITOR_UI_COVERAGE_FAILED);
    ui.threads[0].dispatcher = dispatcher;
    ui.threads[0].count = 1;
    CHECK(dluma_editor_ui_guard_coverage_capture(guard, NULL, main_window, &generation) == DLUMA_EDITOR_UI_COVERAGE_PENDING);
    ui.threads[0].count = 0;
    fail_subclass = child;
    CHECK(dluma_editor_ui_guard_coverage_capture(guard, NULL, main_window, &generation) == DLUMA_EDITOR_UI_COVERAGE_FAILED);
    fail_subclass = NULL;
    prompt = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 1, 1, NULL, NULL, GetModuleHandleW(NULL), NULL); CHECK(prompt);
    pretend_visible_prompt = prompt;
    CHECK(dluma_editor_ui_guard_coverage_capture(guard, NULL, main_window, &generation) == DLUMA_EDITOR_UI_COVERAGE_PENDING);
    pretend_visible_prompt = NULL; CHECK(DestroyWindow(prompt));
    foreign_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    foreign_stop = CreateEventW(NULL, TRUE, FALSE, NULL); CHECK(foreign_ready && foreign_stop);
    foreign_thread = CreateThread(NULL, 0, foreign_window_thread, NULL, 0, NULL); CHECK(foreign_thread);
    CHECK(WaitForSingleObject(foreign_ready, 5000) == WAIT_OBJECT_0 && foreign_window);
    pretend_visible_prompt = foreign_window;
    CHECK(dluma_editor_ui_guard_coverage_capture(guard, NULL, main_window, &generation) != DLUMA_EDITOR_UI_COVERAGE_READY);
    pretend_visible_prompt = NULL;
    CHECK(dluma_editor_ui_guard_coverage_capture(guard, NULL, main_window, &generation) == DLUMA_EDITOR_UI_COVERAGE_READY);
    CHECK(dluma_editor_ui_guard_coverage_current(guard, NULL, main_window, generation));
    pretend_visible_prompt = foreign_window;
    CHECK(!dluma_editor_ui_guard_coverage_current(guard, NULL, main_window, generation));
    pretend_visible_prompt = NULL; SetEvent(foreign_stop);
    CHECK(WaitForSingleObject(foreign_thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(foreign_thread); CloseHandle(foreign_ready); CloseHandle(foreign_stop);
    CHECK(dluma_editor_ui_guard_coverage_capture(guard, NULL, main_window, &generation) == DLUMA_EDITOR_UI_COVERAGE_READY);
    for (record = ui.windows; record && record->window != child; record = record->next) { }
    CHECK(record && RemoveWindowSubclass(child, ui_window_proc, UI_SUBCLASS_ID));
    CHECK(!dluma_editor_ui_guard_coverage_current(guard, NULL, main_window, generation));
    CHECK(SetWindowSubclass(child, ui_window_proc, UI_SUBCLASS_ID, (DWORD_PTR)record));
    CHECK(dluma_editor_ui_guard_coverage_capture(guard, NULL, main_window, &generation) == DLUMA_EDITOR_UI_COVERAGE_READY);
    epoch = dluma_editor_ui_guard_arm(guard, NULL); CHECK(epoch);
    ZeroMemory(&event, sizeof(event)); event.hwnd = child; event.message = WM_CREATE;
    (void)ui_callwnd_hook(HC_ACTION, 0, (LPARAM)&event);
    CHECK(!dluma_editor_ui_guard_coverage_current(guard, NULL, main_window, generation));
    CHECK(coverage_losses == 1);
    CHECK(dluma_editor_ui_guard_disarm(guard, NULL, epoch));
    {
        enum { bulk_count = 2400 };
        HWND *bulk = (HWND *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bulk_count * sizeof(HWND));
        ULONGLONG start; unsigned int i; UiCoverageScan limit; DlumaEditorUiCoverageDetail failure;
        CHECK(bulk);
        for (i = 0; i < bulk_count; ++i) {
            bulk[i] = CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"STATIC", L"", WS_CHILD, 0, 0, 1, 1,
                main_window, NULL, GetModuleHandleW(NULL), NULL);
            CHECK(bulk[i]);
        }
        start = GetTickCount64();
        CHECK(dluma_editor_ui_guard_coverage_capture(guard, NULL, main_window, &generation) == DLUMA_EDITOR_UI_COVERAGE_READY);
        CHECK(dluma_editor_ui_guard_coverage_current(guard, NULL, main_window, generation));
        printf("PASS: %u child windows captured and verified in %llu ms\n", bulk_count,
            (unsigned long long)(GetTickCount64() - start));
        ZeroMemory(&limit, sizeof(limit)); ZeroMemory(&failure, sizeof(failure));
        limit.main_window = main_window; limit.owner_thread = GetCurrentThreadId();
        limit.status = DLUMA_EDITOR_UI_COVERAGE_READY; limit.count = UI_COVERAGE_CAPACITY; limit.detail = &failure;
        CHECK(!ui_coverage_child(main_window, (LPARAM)&limit) && failure.reason == 11U);
        for (i = 0; i < bulk_count; ++i) CHECK(DestroyWindow(bulk[i]));
        HeapFree(GetProcessHeap(), 0, bulk);
    }
    {
        SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
        STARTUPINFOW startup; PROCESS_INFORMATION process;
        WCHAR executable[MAX_PATH], command[MAX_PATH + 120];
        HANDLE stop = CreateEventW(&security, TRUE, FALSE, NULL);
        HANDLE create_sibling = CreateEventW(&security, TRUE, FALSE, NULL);
        HWND remote_child = NULL, sibling = NULL, nested; MSG msg; DWORD pid = 0; unsigned int tries;
        FILETIME created, exited, kernel, user; ULARGE_INTEGER stamp;
        DlumaEditorUiCoverageForeign foreign;
        DlumaEditorUiCoverageDetail detail;
        CHECK(stop && create_sibling && GetModuleFileNameW(NULL, executable, MAX_PATH));
        ZeroMemory(&startup, sizeof(startup)); startup.cb = sizeof(startup); ZeroMemory(&process, sizeof(process));
        CHECK(swprintf_s(command, MAX_PATH + 120, L"\"%s\" --foreign-child %I64x %I64x %I64x %I64x", executable,
            (unsigned long long)(UINT_PTR)child, (unsigned long long)(UINT_PTR)stop,
            (unsigned long long)(UINT_PTR)main_window, (unsigned long long)(UINT_PTR)create_sibling) > 0);
        CHECK(CreateProcessW(executable, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process));
        for (tries = 0; tries < 500U && !remote_child; ++tries) {
            if (PeekMessageW(&msg, main_window, WM_APP + 61, WM_APP + 61, PM_REMOVE)) remote_child = (HWND)msg.wParam;
            else Sleep(10);
        }
        CHECK(remote_child && GetParent(remote_child) == child &&
              GetWindowThreadProcessId(remote_child, &pid) && pid == process.dwProcessId && pid != GetCurrentProcessId());
        {
            PanelState panel; PanelWait wait; HWND observed; uint64_t binding, creation;
            LONG_PTR previous = GetWindowLongPtrW(child, GWLP_USERDATA);
            ZeroMemory(&panel, sizeof(panel)); ZeroMemory(&wait, sizeof(wait));
            panel.ref.size = sizeof(panel.ref); panel.ref.version = 1; panel.ref.token = 42;
            panel.ref.panel_window = child; panel.ref.content_window = child;
            panel.owner = main_window; panel.child = remote_child; panel.child_pid = pid; panel.references = 1;
            panel.wait = &wait; panel.binding_generation = wait.generation = 17; wait.process = process.hProcess;
            CHECK(SetPropW(main_window, owner_property, &panel) && SetPropW(child, panel_property, &panel));
            SetWindowLongPtrW(child, GWLP_USERDATA, (LONG_PTR)&panel);
            CHECK(dluma_editor_panel_host_bound_child(&panel.ref, pid, process.hProcess, &observed, &binding, &creation));
            CHECK(observed == remote_child && binding == 17 && creation != 0);
            CHECK(!dluma_editor_panel_host_bound_child(&panel.ref, pid + 1U, process.hProcess, &observed, &binding, &creation));
            CHECK(!dluma_editor_panel_host_bound_child(&panel.ref, pid, GetCurrentProcess(), &observed, &binding, &creation));
            ++panel.binding_generation;
            CHECK(!dluma_editor_panel_host_bound_child(&panel.ref, pid, process.hProcess, &observed, &binding, &creation));
            --panel.binding_generation; panel.wait = NULL;
            CHECK(!dluma_editor_panel_host_bound_child(&panel.ref, pid, process.hProcess, &observed, &binding, &creation));
            SetWindowLongPtrW(child, GWLP_USERDATA, previous);
            RemovePropW(child, panel_property); RemovePropW(main_window, owner_property);
        }
        CHECK(dluma_editor_ui_guard_coverage_capture(guard, NULL, main_window, &generation) == DLUMA_EDITOR_UI_COVERAGE_FAILED);
        CHECK(GetProcessTimes(process.hProcess, &created, &exited, &kernel, &user));
        stamp.LowPart = created.dwLowDateTime; stamp.HighPart = created.dwHighDateTime;
        ZeroMemory(&foreign, sizeof(foreign)); foreign.child = remote_child; foreign.parent = child;
        foreign.process_id = pid; foreign.process = process.hProcess; foreign.binding_generation = 1; foreign.process_creation_time = stamp.QuadPart;
        CHECK(dluma_editor_ui_guard_coverage_capture_foreign(guard, NULL, main_window, &foreign, &generation, &detail) == DLUMA_EDITOR_UI_COVERAGE_READY);
        CHECK(dluma_editor_ui_guard_coverage_current_foreign(guard, NULL, main_window, &foreign, generation));
        {
            enum { large_count = 2400 };
            HWND *large = (HWND *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, large_count * sizeof(HWND));
            ULONGLONG started; unsigned int i;
            CHECK(large);
            for (i = 0; i < large_count; ++i) {
                large[i] = CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"STATIC", L"", WS_CHILD, 0, 0, 1, 1,
                    main_window, NULL, GetModuleHandleW(NULL), NULL);
                CHECK(large[i]);
            }
            started = GetTickCount64();
            CHECK(dluma_editor_ui_guard_coverage_capture_foreign(guard, NULL, main_window, &foreign, &generation, &detail) == DLUMA_EDITOR_UI_COVERAGE_READY);
            CHECK(dluma_editor_ui_guard_coverage_current_foreign(guard, NULL, main_window, &foreign, generation));
            printf("PASS: %u native windows plus foreign child captured and verified in %llu ms\n", large_count,
                (unsigned long long)(GetTickCount64() - started));
            for (i = 0; i < large_count; ++i) CHECK(DestroyWindow(large[i]));
            HeapFree(GetProcessHeap(), 0, large);
        }
        nested = CreateWindowExW(WS_EX_NOPARENTNOTIFY, L"STATIC", L"", WS_CHILD, 0, 0, 1, 1,
            remote_child, NULL, GetModuleHandleW(NULL), NULL); CHECK(nested);
        CHECK(dluma_editor_ui_guard_coverage_capture_foreign(guard, NULL, main_window, &foreign, &generation, &detail) == DLUMA_EDITOR_UI_COVERAGE_READY);
        for (record = ui.windows; record && record->window != nested; record = record->next) { }
        CHECK(record && dluma_editor_ui_guard_coverage_current_foreign(guard, NULL, main_window, &foreign, generation));
        ++foreign.process_creation_time;
        CHECK(!dluma_editor_ui_guard_coverage_current_foreign(guard, NULL, main_window, &foreign, generation));
        --foreign.process_creation_time; foreign.binding_generation = 0;
        CHECK(!dluma_editor_ui_guard_coverage_current_foreign(guard, NULL, main_window, &foreign, generation));
        foreign.binding_generation = 1;
        CHECK(SetEvent(create_sibling));
        for (tries = 0; tries < 500U && !sibling; ++tries) {
            if (PeekMessageW(&msg, main_window, WM_APP + 62, WM_APP + 62, PM_REMOVE)) sibling = (HWND)msg.wParam;
            else Sleep(10);
        }
        CHECK(sibling && GetParent(sibling) == child);
        CHECK(!dluma_editor_ui_guard_coverage_current_foreign(guard, NULL, main_window, &foreign, generation));
        CHECK(dluma_editor_ui_guard_coverage_capture_foreign(guard, NULL, main_window, &foreign, &generation, &detail) == DLUMA_EDITOR_UI_COVERAGE_FAILED);
        CHECK(detail.reason == 12U && detail.window == sibling && detail.process_id == pid);
        CHECK(DestroyWindow(nested));
        CHECK(SetEvent(stop));
        for (tries = 0; tries < 500U && WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0; ++tries) {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            Sleep(10);
        }
        CHECK(WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0);
        CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(stop); CloseHandle(create_sibling);
    }
    while (ui.windows) CHECK(ui_detach_owner(ui.windows));
    CHECK(dluma_editor_ui_guard_unsubscribe());
    CHECK(DestroyWindow(child)); CHECK(DestroyWindow(main_window)); CHECK(DestroyWindow(dispatcher));
    CHECK(UnregisterClassW(dispatcher_class.lpszClassName, dispatcher_class.hInstance)); TlsFree(ui.tls_index);
    puts("Editor UI coverage contract passed."); return 0;
}
