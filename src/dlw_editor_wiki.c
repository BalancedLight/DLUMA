#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "dlw_editor_wiki.h"
#include "dlw_editor_wiki_session.h"
#include "dlw_editor_theme_profile.h"

extern int dluma_editor_is_main_window(HWND, DWORD);
extern void dluma_log_line(const char *, const char *);

#define WIKI_COMMAND 37484U
#define WIKI_PANE_ID 0xD1A0U
#define WIKI_DOCK_RIGHT 59422U
#define WIKI_PATH_CAPACITY 32768U

typedef struct NativeClass {
    const char *name;
    unsigned int object_size, schema;
    void *(__cdecl *create)(void);
    void *(__cdecl *base)(void);
    void *next, *initialization;
} NativeClass;
typedef void *(__fastcall *PaneCreateFn)(void *, const NativeClass *, const char *, const char *, int, unsigned int, int, BYTE);
typedef void *(__fastcall *TipsConstructorFn)(void *);
typedef void *(__cdecl *AllocateFn)(size_t);
typedef void (__cdecl *FreeFn)(void *);
typedef void *(__cdecl *ModuleStateFn)(void);
typedef void *(__fastcall *PermanentFn)(HWND);
typedef int (__fastcall *ShowWindowFn)(void *, int);
C_ASSERT(sizeof(NativeClass) == 48);
C_ASSERT(offsetof(NativeClass, create) == 16);

static struct {
    PaneCreateFn pane_create;
    TipsConstructorFn construct;
    AllocateFn allocate;
    FreeFn release;
    ModuleStateFn module_state;
    PermanentFn permanent;
    NativeClass content_class;
    void *main_tables[2], *pane_table, *tips_table;
    void **show_slot;
    ShowWindowFn original_show;
    HMODULE resources;
    int ready, patched, pinned;
} native;

static struct {
    volatile LONG started, stopping, callbacks;
    volatile LONG ui_cleanup_complete;
    DWORD creating_thread, ui_thread;
    HWND main, content, pane;
    HWND stock[32];
    unsigned int stock_count;
    int session_open, hidden_for_stop;
    DlumaEditorPanelRef panel;
    WCHAR config[WIKI_PATH_CAPACITY];
} wiki;
static UINT stop_message;
static DWORD owned_creating_thread;
static const WCHAR content_property[] = L"DLUMA.EditorWiki.Content.1";
static const WCHAR owned_content_property[] = L"DLUMA.EditorDock.OwnedContent.1";
static const BYTE factory_bytes[] = {0x48,0x8b,0xc4,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0x80};
static const BYTE constructor_bytes[] = {0x48,0x89,0x4c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x48,0xc7,0x44,0x24,0x20,0xfe};
static const BYTE show_bytes[] = {0x48,0x83,0xb9,0xc8,0x00,0x00,0x00,0x00,0x75,0x0b,0x48,0x8b,0x49,0x40};
static const BYTE permanent_bytes[] = {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xe8,0x46,0x61,0xeb,0xff};

static int accessible(const void *pointer, size_t length, int write)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t cursor = (uintptr_t)pointer, end;
    if (!cursor || length > UINTPTR_MAX - cursor) return 0;
    end = cursor + length;
    while (cursor < end) {
        DWORD protection;
        uintptr_t next;
        if (!VirtualQuery((void *)cursor, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
        protection = info.Protect & 0xffU;
        if (write && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return 0;
        next = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (next <= cursor) return 0;
        cursor = next;
    }
    return 1;
}

static int own_thread(HWND window)
{
    DWORD pid = 0;
    return IsWindow(window) && GetWindowThreadProcessId(window, &pid) == GetCurrentThreadId() &&
        pid == GetCurrentProcessId();
}

static int native_ready(void);
static int __fastcall show_window(void *object, int command)
{
    int suppress = 0, result;
    InterlockedIncrement(&wiki.callbacks);
    __try {
        if (!wiki.stopping && command != SW_HIDE && wiki.creating_thread != GetCurrentThreadId() &&
            owned_creating_thread != GetCurrentThreadId()) {
            __try {
                if (object && *(void **)object == native.tips_table) {
                    HWND window = *(HWND *)((BYTE *)object + 64);
                    DWORD pid = 0;
                    if (!*(void **)((BYTE *)object + 200) && window &&
                        GetWindowThreadProcessId(window, &pid) && pid == GetCurrentProcessId())
                    suppress = GetPropW(window, content_property) != &wiki &&
                        !GetPropW(window, owned_content_property);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) { suppress = 0; }
        }
        result = suppress ? 0 : native.original_show(object, command);
    } __finally { InterlockedDecrement(&wiki.callbacks); }
    return result;
}

static void * __cdecl create_content(void)
{
    void *object;
    int complete = 0;
    if (!native.ready) return NULL;
    object = native.allocate(native.content_class.object_size);
    if (!object) return NULL;
    __try {
        native.construct(object);
        complete = 1;
    } __finally {
        if (!complete) native.release(object);
    }
    return object;
}

static int native_ready(void)
{
    void *factory, *constructor, *permanent, *show;
    void **allocation, **release, **state;
    const NativeClass *content;
    HMODULE mfc, crt, pinned;
    if (native.ready) return 1;
    if (!dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR) ||
        !dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_MFC)) return 0;
    factory = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 0xADC00U, sizeof(factory_bytes));
    constructor = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 0x2BBA00U, sizeof(constructor_bytes));
    permanent = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC, 0x26B460U, sizeof(permanent_bytes));
    content = (const NativeClass *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 22427488U, sizeof(*content));
    allocation = (void **)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 18111632U, sizeof(void *));
    release = (void **)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 18113768U, sizeof(void *));
    state = (void **)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 18136024U, sizeof(void *));
    native.show_slot = (void **)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 18135352U, sizeof(void *));
    show = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC, 2663848U, sizeof(show_bytes));
    mfc = GetModuleHandleW(L"mfc100.dll"); crt = GetModuleHandleW(L"MSVCR100.dll");
    native.resources = GetModuleHandleW(NULL);
    if (!factory || !constructor || !permanent || !content || !allocation || !release || !state ||
        !native.show_slot || !show || !mfc || !crt ||
        memcmp(factory, factory_bytes, sizeof(factory_bytes)) ||
        memcmp(constructor, constructor_bytes, sizeof(constructor_bytes)) ||
        memcmp(permanent, permanent_bytes, sizeof(permanent_bytes)) ||
        memcmp(show, show_bytes, sizeof(show_bytes)) ||
        *native.show_slot != show || (void *)GetProcAddress(mfc, (LPCSTR)(ULONG_PTR)12597U) != show ||
        (void *)GetProcAddress(mfc, (LPCSTR)(ULONG_PTR)4194U) != permanent ||
        (void *)GetProcAddress(mfc, (LPCSTR)(ULONG_PTR)1895U) != *state ||
        (void *)GetProcAddress(crt, "malloc") != *allocation ||
        (void *)GetProcAddress(crt, "free") != *release ||
        content->object_size != 760U || content->schema != 65535U || content->create ||
        content->name != dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 22427472U, 9) ||
        (void *)content->base != dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 0x2BDD70U, 1) ||
        !FindResourceW(native.resources, MAKEINTRESOURCEW(832), MAKEINTRESOURCEW(5))) return 0;
    native.main_tables[0] = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 22113272U, sizeof(void *));
    native.main_tables[1] = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 22114312U, sizeof(void *));
    native.pane_table = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 21912200U, sizeof(void *));
    native.tips_table = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 22427816U, sizeof(void *));
    if (!native.main_tables[0] || !native.main_tables[1] || !native.pane_table || !native.tips_table) return 0;
    native.pane_create = (PaneCreateFn)factory; native.construct = (TipsConstructorFn)constructor;
    native.allocate = (AllocateFn)*allocation; native.release = (FreeFn)*release;
    native.module_state = (ModuleStateFn)*state; native.permanent = (PermanentFn)permanent;
    native.original_show = (ShowWindowFn)show;
    native.content_class = *content; native.content_class.create = create_content;
    native.content_class.next = native.content_class.initialization = NULL;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            (LPCWSTR)(ULONG_PTR)create_content, &pinned)) return 0;
    native.pinned = native.ready = 1;
    return 1;
}

int dluma_editor_wiki_admit_owned_content(HWND window, const void *tag)
{
    void *object;
    HANDLE previous;
    if (!tag || !own_thread(window) || !native_ready()) return 0;
    object = native.permanent(window);
    if (!accessible(object, 72U, 0) || *(void **)object != native.tips_table ||
        *(HWND *)((BYTE *)object + 64U) != window) return 0;
    previous = GetPropW(window, owned_content_property);
    if (previous) return previous == tag;
    return SetPropW(window, owned_content_property, (HANDLE)tag) != 0;
}

void dluma_editor_wiki_release_owned_content(HWND window, const void *tag)
{
    if (tag && own_thread(window) && GetPropW(window, owned_content_property) == tag)
        (void)RemovePropW(window, owned_content_property);
}

int dluma_editor_wiki_admit_show_slot(void **slot, const void *original)
{
    if (!native_ready() || slot != native.show_slot || original != (void *)native.original_show)
        return 0;
    return *slot == original || (native.patched && *slot == (void *)show_window);
}

static int set_hints_replacement(int enabled)
{
    DWORD old, ignored, writable;
    MEMORY_BASIC_INFORMATION info;
    void *expected = enabled ? (void *)native.original_show : (void *)show_window;
    void *desired = enabled ? (void *)show_window : (void *)native.original_show;
    void *previous;
    if (!native.ready || ((uintptr_t)native.show_slot & (sizeof(void *)-1U)) ||
        !VirtualQuery(native.show_slot, &info, sizeof(info))) return 0;
    writable = (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
        ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    if (!VirtualProtect(native.show_slot, sizeof(void *), writable, &old)) return 0;
    previous = InterlockedCompareExchangePointer(native.show_slot, desired, expected);
    if (!VirtualProtect(native.show_slot, sizeof(void *), old, &ignored)) {
        if (previous == expected) {
            (void)InterlockedCompareExchangePointer(native.show_slot, previous, desired);
        }
        (void)VirtualProtect(native.show_slot, sizeof(void *), old, &ignored);
        return 0;
    }
    if (previous != expected && previous != desired) return 0;
    native.patched = enabled;
    return 1;
}

static void *main_object(HWND window)
{
    void *object;
    if (!own_thread(window) || !dluma_editor_is_main_window(window, GetCurrentProcessId())) return NULL;
    object = native.permanent(window);
    if (!accessible(object, 6960, 0)) return NULL;
    __try {
        if ((*(void **)object != native.main_tables[0] && *(void **)object != native.main_tables[1]) ||
            *(HWND *)((BYTE *)object + 64) != window) return NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return NULL; }
    return object;
}

static int pane_class_available(void *frame, unsigned int id, const void *content_class, const void *expected)
{
    void **panes;
    unsigned int count, capacity, i;
    int found = 0;
    __try {
        panes = *(void ***)((BYTE *)frame + 6944);
        count = *(unsigned int *)((BYTE *)frame + 6952);
        capacity = *(unsigned int *)((BYTE *)frame + 6956);
        if (count > capacity || capacity > 4096U ||
            (count && !accessible(panes, count * sizeof(void *), 0))) return 0;
        for (i = 0; i < count; ++i) {
            BYTE *pane = (BYTE *)panes[i];
            if (!accessible(pane, 1264, 0)) return 0;
            if (*(unsigned int *)(pane + 528) == id) {
                if (*(void **)(pane + 1232) != content_class || (expected && expected != pane)) return 0;
                found = 1;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return !expected || found;
}

static int pane_id_available(void *frame, const void *expected)
{
    return pane_class_available(frame, WIKI_PANE_ID, &native.content_class, expected);
}

int dluma_editor_dock_show(DlumaEditorDockRef *dock, HWND main,
    unsigned int id, const char *title, const void *tag, int visible)
{
    void *frame, *content, *pane, **state, *old;
    HWND window, parent, child;
    NativeClass *content_class;
    if (!dock || !tag || !title || id != 0xD1A1U || !native_ready() || wiki.creating_thread || owned_creating_thread)
        return 0;
    frame = main_object(main);
    if (!frame || (dock->tag && (dock->tag != tag || dock->id != id || dock->main != main))) return 0;
    content_class = (NativeClass *)dock->native_class;
    if (!dock->tag) {
        *content_class = native.content_class;
        dock->id = id; dock->main = main; dock->tag = tag;
    }
    if (!pane_class_available(frame, id, content_class, NULL)) return 0;
    state = (void **)native.module_state();
    if (!accessible(state, 4U * sizeof(void *), 1)) return 0;
    old = state[3]; state[3] = native.resources;
    owned_creating_thread = GetCurrentThreadId();
    __try {
        content = native.pane_create(frame, content_class, title, "text_line_16",
            (int)id, WIKI_DOCK_RIGHT, 0, 0);
    } __finally { state[3] = old; owned_creating_thread = 0; }
    if (!accessible(content, 760U, 0)) return 0;
    __try {
        if (*(void **)content != native.tips_table) return 0;
        window = *(HWND *)((BYTE *)content + 64U);
        if (!own_thread(window) || native.permanent(window) != content ||
            !(GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD)) return 0;
        parent = GetParent(window);
        if (!own_thread(parent) || GetDlgCtrlID(parent) != (int)id) return 0;
        pane = native.permanent(parent);
        if (!accessible(pane, 1264U, 0) || *(void **)pane != native.pane_table ||
            *(void **)((BYTE *)pane + 1232U) != content_class ||
            *(void **)((BYTE *)pane + 1248U) != content ||
            (dock->content && dock->content != window)) return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!dluma_editor_wiki_admit_owned_content(window, tag)) return 0;
    if (!dock->content) {
        for (child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            EnableWindow(child, FALSE); ShowWindow(child, SW_HIDE);
        }
    }
    dock->content = window; dock->pane = parent;
    (void)dluma_editor_ui_request_enroll(window);
    (void)dluma_editor_ui_request_enroll(parent);
    state[3] = native.resources;
    __try {
        content = native.pane_create(frame, content_class, title, "text_line_16",
            (int)id, WIKI_DOCK_RIGHT, 0, (BYTE)!!visible);
    } __finally { state[3] = old; }
    return content != NULL;
}

static int tips_window(HWND window)
{
    WCHAR name[64], title[96];
    static const int ids[] = {1,1977,1883,1884,2328,3150,3181};
    unsigned int i;
    if (!own_thread(window) || !GetClassNameW(window, name, 64) || lstrcmpW(name, L"#32770") ||
        !GetWindowTextW(window, title, 96) || lstrcmpW(title, L"Dying Light Developer Tools Hints")) return 0;
    for (i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i) if (!GetDlgItem(window, ids[i])) return 0;
    return GetClassNameW(GetDlgItem(window, 3181), name, 64) && !lstrcmpiW(name, L"RichEdit20A");
}

static void hide_stock(HWND window)
{
    HWND child;
    unsigned int i;
    if (!wiki.stock_count) {
        for (child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            if (wiki.stock_count == sizeof(wiki.stock)/sizeof(wiki.stock[0])) break;
            wiki.stock[wiki.stock_count++] = child;
        }
    }
    for (i = 0; i < wiki.stock_count; ++i) {
        child = wiki.stock[i];
        if (IsWindow(child) && GetParent(child) == window) {
            EnableWindow(child, FALSE); ShowWindow(child, SW_HIDE);
        }
    }
}

static void panel_changed(const DlumaEditorPanelRef *panel, unsigned int event, unsigned int argument, void *context)
{
    (void)panel; (void)argument; (void)context;
    if (event == DLUMA_EDITOR_PANEL_CLOSING || event == DLUMA_EDITOR_PANEL_CHILD_EXITED) {
        dluma_editor_wiki_session_close(); wiki.session_open = 0;
    }
}

static void hide_native_pane(void)
{
    void *frame, *pane, **state, *old;
    if (!wiki.main || !own_thread(wiki.main) || !wiki.pane || !own_thread(wiki.pane)) return;
    frame = main_object(wiki.main);
    pane = native.permanent(wiki.pane);
    if (!frame || !pane || !pane_id_available(frame, pane)) return;
    state = (void **)native.module_state();
    if (!accessible(state, 4 * sizeof(void *), 1)) return;
    old = state[3]; state[3] = native.resources;
    __try {
        (void)native.pane_create(frame, &native.content_class, "Wiki", "text_line_16",
            WIKI_PANE_ID, WIKI_DOCK_RIGHT, 0, 0);
    } __finally { state[3] = old; }
}

static int show_pane(HWND main, int visible)
{
    void *frame = main_object(main), *content;
    void **module_state;
    void *old_resource;
    HWND content_window, pane_window;
    void *pane_object;
    if (!frame || !pane_id_available(frame, NULL) || wiki.creating_thread) return 0;
    module_state = (void **)native.module_state();
    if (!accessible(module_state, 4 * sizeof(void *), 1)) return 0;
    old_resource = module_state[3];
    wiki.creating_thread = GetCurrentThreadId();
    module_state[3] = native.resources;
    __try {
        content = native.pane_create(frame, &native.content_class, "Wiki", "text_line_16",
            WIKI_PANE_ID, WIKI_DOCK_RIGHT, 0, 0);
    } __finally {
        module_state[3] = old_resource; wiki.creating_thread = 0;
    }
    if (!accessible(content, 760, 0)) return 0;
    __try {
        if (*(void **)content != native.tips_table) return 0;
        content_window = *(HWND *)((BYTE *)content + 64);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (!own_thread(content_window) || !(GetWindowLongPtrW(content_window, GWL_STYLE) & WS_CHILD) ||
        native.permanent(content_window) != content) return 0;
    pane_window = GetParent(content_window);
    if (!own_thread(pane_window) || GetDlgCtrlID(pane_window) != WIKI_PANE_ID) return 0;
    pane_object = native.permanent(pane_window);
    if (!accessible(pane_object, 1264, 0)) return 0;
    __try {
        if (*(void **)pane_object != native.pane_table ||
            *(void **)((BYTE *)pane_object + 1232) != &native.content_class ||
            *(void **)((BYTE *)pane_object + 1248) != content) return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (wiki.content && wiki.content != content_window) return 0;
    wiki.main = main; wiki.content = content_window; wiki.pane = pane_window; wiki.ui_thread = GetCurrentThreadId();
    if (GetPropW(content_window, content_property) != &wiki && !SetPropW(content_window, content_property, &wiki)) return 0;
    hide_stock(content_window);
    (void)dluma_editor_ui_request_enroll(content_window);
    (void)dluma_editor_ui_request_enroll(pane_window);
    if (!wiki.panel.token) {
        wiki.panel.size = sizeof(wiki.panel); wiki.panel.version = 1;
        if (!dluma_editor_panel_host_create_view(content_window, panel_changed, NULL, &wiki.panel)) return 0;
    }
    if (visible && !wiki.session_open) {
        if (!dluma_editor_wiki_session_callbacks_drained()) return 0;
        (void)SetWindowTextW(wiki.panel.content_window, L"Loading wiki...");
        if (!dluma_editor_wiki_session_open(content_window, &wiki.panel, wiki.config)) return 0;
        wiki.session_open = 1;
    }
    module_state[3] = native.resources;
    __try {
        content = native.pane_create(frame, &native.content_class, "Wiki", "text_line_16",
            WIKI_PANE_ID, WIKI_DOCK_RIGHT, 0, (BYTE)!!visible);
    } __finally { module_state[3] = old_resource; }
    return content != NULL;
}

static int on_window(HWND window, unsigned int stage, UINT message, WPARAM wp, LPARAM lp, LRESULT *result, void *context)
{
    int handled = 0;
    (void)context;
    InterlockedIncrement(&wiki.callbacks);
    __try {
        if (stage == DLUMA_EDITOR_UI_BEFORE && message == WM_INITMENUPOPUP &&
            dluma_editor_is_main_window(window, GetCurrentProcessId())) {
            MENUITEMINFOW item;
            ZeroMemory(&item, sizeof(item)); item.cbSize = sizeof(item); item.fMask = MIIM_ID;
            if (GetMenuItemInfoW((HMENU)wp, WIKI_COMMAND, FALSE, &item)) {
                item.fMask = MIIM_STRING; item.dwTypeData = L"&Wiki";
                (void)SetMenuItemInfoW((HMENU)wp, WIKI_COMMAND, FALSE, &item);
            }
        }
        if (stage == DLUMA_EDITOR_UI_BEFORE && message == stop_message && wiki.stopping &&
            (window == wiki.content || window == wiki.main)) {
            (void)dluma_editor_wiki_stop(); *result = 0; handled = 1; __leave;
        }
        if (stage == DLUMA_EDITOR_UI_BEFORE && wiki.creating_thread == GetCurrentThreadId() && tips_window(window)) {
            if (message == WM_INITDIALOG) {
                if (!wiki.content || wiki.content == window) {
                    wiki.content = window;
                    (void)SetPropW(window, content_property, &wiki);
                }
            }
        }
        if (window == wiki.content && GetPropW(window, content_property) == &wiki) {
            if (stage == DLUMA_EDITOR_UI_BEFORE && dluma_editor_wiki_session_dispatch(window, message, wp, lp, result)) {
                if (*result == -1) {
                    dluma_editor_wiki_session_close(); wiki.session_open = 0;
                    if (wiki.panel.token)
                        (void)SetWindowTextW(wiki.panel.content_window, L"The wiki could not start. Reopen this view to try again.");
                } else if (wiki.panel.token && dluma_editor_panel_host_binding_generation(&wiki.panel)) {
                    (void)SetWindowTextW(wiki.panel.content_window, L"");
                }
                handled = 1;
            } else if (stage == DLUMA_EDITOR_UI_BEFORE &&
                (message == WM_SHOWWINDOW || message == WM_COMMAND || message == WM_NOTIFY || message == WM_CLOSE ||
                    (message == WM_SYSCOMMAND && (wp & 0xfff0U) == SC_CLOSE))) {
                if ((message == WM_CLOSE || (message == WM_SYSCOMMAND && (wp & 0xfff0U) == SC_CLOSE)) && wiki.main)
                    hide_native_pane();
                *result = 0; handled = 1;
            } else if (stage == DLUMA_EDITOR_UI_AFTER && message == WM_INITDIALOG) {
                hide_stock(window); *result = FALSE;
            } else if (stage == DLUMA_EDITOR_UI_AFTER &&
                (message == WM_SIZE || message == WM_WINDOWPOSCHANGED || message == WM_DPICHANGED || message == WM_STYLECHANGED)) {
                if (wiki.panel.token) (void)dluma_editor_panel_host_layout(&wiki.panel);
            } else if (stage == DLUMA_EDITOR_UI_BEFORE && message == WM_SETFOCUS && wiki.panel.token) {
                (void)SetFocus(wiki.panel.panel_window); *result = 0; handled = 1;
            } else if (stage == DLUMA_EDITOR_UI_DESTROY) {
                dluma_editor_wiki_session_close();
                wiki.session_open = 0;
                (void)RemovePropW(window, content_property);
                wiki.content = wiki.pane = NULL; wiki.stock_count = 0;
                ZeroMemory(&wiki.panel, sizeof(wiki.panel));
            }
        } else if (stage == DLUMA_EDITOR_UI_BEFORE && message == WM_COMMAND &&
            LOWORD(wp) == WIKI_COMMAND && HIWORD(wp) <= 1 && !lp && !wiki.stopping &&
            dluma_editor_is_main_window(window, GetCurrentProcessId())) {
            if (!show_pane(window, 1))
                MessageBoxW(window, L"The wiki could not be opened.", L"Wiki", MB_OK | MB_ICONINFORMATION);
            *result = 0; handled = 1;
        } else if (window == wiki.pane && stage == DLUMA_EDITOR_UI_AFTER && wiki.panel.token &&
            (message == WM_SIZE || message == WM_WINDOWPOSCHANGED)) {
            (void)dluma_editor_panel_host_layout(&wiki.panel);
        }
    } __finally { InterlockedDecrement(&wiki.callbacks); }
    return handled;
}

int dluma_editor_wiki_start(const WCHAR *theme_config)
{
    if (InterlockedCompareExchange(&wiki.started, 0, 0)) return !wiki.stopping;
    if (!theme_config || !*theme_config || lstrlenW(theme_config) >= WIKI_PATH_CAPACITY || !native_ready()) return 0;
    lstrcpynW(wiki.config, theme_config, WIKI_PATH_CAPACITY);
    wiki.stopping = 0; wiki.ui_cleanup_complete = 0; wiki.hidden_for_stop = 0;
    stop_message = RegisterWindowMessageW(L"DLUMA.EditorWiki.Stop.1");
    if (!stop_message) return 0;
    if (!dluma_editor_ui_subscribe(DLUMA_EDITOR_UI_OWNER_WIKI, on_window, NULL)) return 0;
    if (!set_hints_replacement(1)) {
        (void)dluma_editor_ui_unsubscribe(DLUMA_EDITOR_UI_OWNER_WIKI); return 0;
    }
    InterlockedExchange(&wiki.started, 1);
    dluma_log_line("INFO", "Editor Wiki command registered.");
    return 1;
}

int dluma_editor_wiki_stop(void)
{
    HWND owner;
    if (!InterlockedCompareExchange(&wiki.started, 0, 0)) return 1;
    InterlockedExchange(&wiki.stopping, 1);
    if (!InterlockedCompareExchange(&wiki.ui_cleanup_complete, 0, 0)) {
        owner = IsWindow(wiki.content) ? wiki.content : IsWindow(wiki.main) ? wiki.main : NULL;
        if (owner && !own_thread(owner)) { (void)PostMessageW(owner, stop_message, 0, 0); return 0; }
        dluma_editor_wiki_session_close(); wiki.session_open = 0;
        if (!wiki.hidden_for_stop) { hide_native_pane(); wiki.hidden_for_stop = 1; }
        if (!dluma_editor_wiki_session_callbacks_drained()) return 0;
        if (wiki.panel.token && IsWindow(wiki.panel.panel_window) &&
            !dluma_editor_panel_host_close(&wiki.panel)) return 0;
        ZeroMemory(&wiki.panel, sizeof(wiki.panel));
        InterlockedExchange(&wiki.ui_cleanup_complete, 1);
    }
    if (native.patched && !set_hints_replacement(0)) return 0;
    if (!dluma_editor_ui_unsubscribe(DLUMA_EDITOR_UI_OWNER_WIKI)) return 0;
    InterlockedExchange(&wiki.started, 0);
    return 1;
}

int dluma_editor_wiki_callbacks_drained(void)
{
    return !InterlockedCompareExchange(&wiki.callbacks, 0, 0) &&
        !InterlockedCompareExchange(&wiki.started, 0, 0) && dluma_editor_wiki_session_callbacks_drained();
}
