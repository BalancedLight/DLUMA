#include "dlw_editor_presence.h"

#include "dlw_discord_presence.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_editor_presence_core.h"
#include "dlw_editor_exit_observer.h"
#include "dlw_log.h"
#include "dlw_path_security.h"

#include <string.h>

#define DLUMA_EDITOR_PRESENCE_POLL_MS 250U
#define DLUMA_EDITOR_PRESENCE_STOP_TIMEOUT_MS 3000U
#define DLUMA_EDITOR_PRESENCE_DISCOVER_OPTIONS (WM_APP + 0x5D1U)
#define DLUMA_EDITOR_OPTIONS_CONTROL_PAGE 0x6D10U
#define DLUMA_EDITOR_OPTIONS_CONTROL_DISCORD 0x6D11U
#define DLUMA_EDITOR_OPTIONS_CONTROL_HINT 0x6D12U
#define DLUMA_EDITOR_OPTIONS_TAB_INDEX 4
#define DLUMA_EDITOR_OPTIONS_STOCK_TABS 4U
#define DLUMA_EDITOR_PLAYER_MAX_BYTES (128ULL * 1024ULL * 1024ULL)
#define DLUMA_EDITOR_PROCESS_QUERY_LIMITED_INFORMATION 0x1000UL
#define DLUMA_EDITOR_SYNCHRONIZE 0x00100000UL
#define DLUMA_EDITOR_TH32CS_SNAPPROCESS 0x00000002UL
#define DLUMA_EDITOR_TCM_FIRST 0x1300U
#define DLUMA_EDITOR_TCM_GETITEMCOUNT (DLUMA_EDITOR_TCM_FIRST + 4U)
#define DLUMA_EDITOR_TCM_DELETEITEM (DLUMA_EDITOR_TCM_FIRST + 8U)
#define DLUMA_EDITOR_TCM_GETCURSEL (DLUMA_EDITOR_TCM_FIRST + 11U)
#define DLUMA_EDITOR_TCM_ADJUSTRECT (DLUMA_EDITOR_TCM_FIRST + 40U)
#define DLUMA_EDITOR_TCM_GETITEMW (DLUMA_EDITOR_TCM_FIRST + 60U)
#define DLUMA_EDITOR_TCM_INSERTITEMW (DLUMA_EDITOR_TCM_FIRST + 62U)
#define DLUMA_EDITOR_TCIF_TEXT 0x0001U
#define DLUMA_EDITOR_TCN_SELCHANGE ((unsigned int)-551)
#define DLUMA_EDITOR_BM_GETCHECK 0x00F0U
#define DLUMA_EDITOR_BM_SETCHECK 0x00F1U
#define DLUMA_EDITOR_BST_CHECKED 1U
#define DLUMA_EDITOR_BN_CLICKED 0U
#define DLUMA_EDITOR_SW_HIDE 0
#define DLUMA_EDITOR_SW_SHOW 5
#define DLUMA_EDITOR_WM_NOTIFY 0x004EU
#define DLUMA_EDITOR_WS_CHILD 0x40000000UL
#define DLUMA_EDITOR_WS_VISIBLE 0x10000000UL
#define DLUMA_EDITOR_WS_TABSTOP 0x00010000UL
#define DLUMA_EDITOR_SWP_NOZORDER 0x0004U
#define DLUMA_EDITOR_SWP_NOACTIVATE 0x0010U
#define DLUMA_EDITOR_PLAYER_SHA256 \
    "61D9DFB20CFA179C6D097537DA46BA9DBE3D58797938762EF4EACFF5DFCC4B3A"

__declspec(dllimport) BOOL WINAPI EnumThreadWindows(
    DWORD thread_id, WNDENUMPROC callback, LPARAM parameter);
__declspec(dllimport) HWND WINAPI GetParent(HWND window);
__declspec(dllimport) BOOL WINAPI IsChild(HWND parent, HWND window);
__declspec(dllimport) int WINAPI MapWindowPoints(
    HWND from, HWND to, POINT* points, unsigned int count);
__declspec(dllimport) BOOL WINAPI SetWindowPos(
    HWND window, HWND after, int x, int y, int width, int height,
    unsigned int flags);
__declspec(dllimport) BOOL WINAPI ShowWindow(HWND window, int command);
__declspec(dllimport) BOOL WINAPI DestroyWindow(HWND window);
__declspec(dllimport) int WINAPI GetDlgCtrlID(HWND window);
__declspec(dllimport) LRESULT WINAPI SendMessageW(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam);
__declspec(dllimport) BOOL WINAPI DuplicateHandle(
    HANDLE source_process, HANDLE source, HANDLE target_process,
    HANDLE* target, DWORD access, BOOL inherit, DWORD options);
__declspec(dllimport) HANDLE WINAPI OpenProcess(
    DWORD access, BOOL inherit, DWORD process_id);
__declspec(dllimport) HWND WINAPI CreateWindowExW(
    DWORD extended_style, LPCWSTR class_name, LPCWSTR window_name,
    DWORD style, int x, int y, int width, int height, HWND parent,
    HMENU menu, HINSTANCE instance, LPVOID parameter);
__declspec(dllimport) HWND WINAPI GetDlgItem(HWND window, int identifier);
__declspec(dllimport) BOOL WINAPI EnumChildWindows(
    HWND parent, WNDENUMPROC callback, LPARAM parameter);
__declspec(dllimport) BOOL WINAPI PostMessageW(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam);


#ifndef DUPLICATE_SAME_ACCESS
#define DUPLICATE_SAME_ACCESS 0x00000002UL
#endif
#ifndef WS_EX_CONTROLPARENT
#define WS_EX_CONTROLPARENT 0x00010000L
#endif
#ifndef WS_CLIPCHILDREN
#define WS_CLIPCHILDREN 0x02000000L
#endif
#ifndef BS_AUTOCHECKBOX
#define BS_AUTOCHECKBOX 0x00000003L
#endif
#ifndef SS_LEFT
#define SS_LEFT 0x00000000L
#endif

typedef struct DlumaEditorTcItemW {
    unsigned int mask;
    DWORD state;
    DWORD state_mask;
    WCHAR* text;
    int text_max;
    int image;
    LPARAM parameter;
} DlumaEditorTcItemW;

typedef struct DlumaEditorNmHdr {
    HWND window_from;
    UINT_PTR id_from;
    unsigned int code;
} DlumaEditorNmHdr;

typedef struct DlumaEditorGuiThreadInfo {
    DWORD size;
    DWORD flags;
    HWND active;
    HWND focus;
    HWND capture;
    HWND menu_owner;
    HWND move_size;
    HWND caret;
    RECT caret_rect;
} DlumaEditorGuiThreadInfo;

typedef struct DlumaEditorProcessInformation {
    HANDLE process;
    HANDLE thread;
    DWORD process_id;
    DWORD thread_id;
} DlumaEditorProcessInformation;

typedef struct DlumaEditorProcessEntry32W {
    DWORD size;
    DWORD usage;
    DWORD process_id;
    ULONG_PTR default_heap;
    DWORD module_id;
    DWORD threads;
    DWORD parent_process_id;
    LONG priority;
    DWORD flags;
    WCHAR executable[260];
} DlumaEditorProcessEntry32W;

typedef BOOL (WINAPI* DlumaEditorGetGuiThreadInfoFn)(
    DWORD thread_id, DlumaEditorGuiThreadInfo* info);
typedef HANDLE (WINAPI* DlumaEditorCreateToolhelp32SnapshotFn)(
    DWORD flags, DWORD process_id);
typedef BOOL (WINAPI* DlumaEditorProcess32FirstWFn)(
    HANDLE snapshot, DlumaEditorProcessEntry32W* entry);
typedef BOOL (WINAPI* DlumaEditorProcess32NextWFn)(
    HANDLE snapshot, DlumaEditorProcessEntry32W* entry);
typedef BOOL (WINAPI* DlumaEditorQueryFullProcessImageNameWFn)(
    HANDLE process, DWORD flags, WCHAR* path, DWORD* capacity);

typedef struct DlumaEditorOptionsState {
    HWND dialog;
    HWND tab;
    HWND page;
    HWND checkbox;
    HWND hint;
    HWND reset;
    HWND stock_pages[DLUMA_EDITOR_OPTIONS_STOCK_TABS];
    WNDPROC predecessor;
    DlumaEditorOptionsModel model;
    int last_stock_index;
} DlumaEditorOptionsState;

typedef struct DlumaEditorPresenceState {
    HANDLE stop_event;
    HANDLE wake_event;
    HANDLE worker_thread;
    HWND main_window;
    WNDPROC main_predecessor;
    DWORD owner_thread_id;
    volatile LONG started;
    volatile LONG stopping;
    volatile LONG callbacks_in_flight;
    volatile LONG playtest_lock;
    HANDLE playtest_process;
    DWORD playtest_pid;
    ULONGLONG playtest_start_time;
    ULONGLONG playtest_generation;
    DlumaEditorPlaytestOwnershipState ownership;
    unsigned int playtest_suspended;
    unsigned int expectation_valid;
    ULONGLONG expectation_deadline_ms;
    DlumaEditorPresencePlaytestLaunch expectation;
    DlumaEditorOptionsState options;
    DlumaEditorPresenceObservation last_observation;
    unsigned int last_observation_valid;
} DlumaEditorPresenceState;

typedef struct DlumaEditorWindowCandidate {
    HWND window;
    DWORD process_id;
    DWORD thread_id;
    ULONGLONG area;
} DlumaEditorWindowCandidate;

typedef struct DlumaEditorDocumentCandidate {
    HWND foreground;
    HWND focus;
    DWORD process_id;
    unsigned int priority;
    DlumaEditorPresenceObservation observation;
} DlumaEditorDocumentCandidate;

typedef struct DlumaEditorStockPageSearch {
    HWND tab;
    HWND pages[DLUMA_EDITOR_OPTIONS_STOCK_TABS];
    unsigned int count;
    unsigned int refused;
} DlumaEditorStockPageSearch;

static DlumaEditorPresenceState g_dluma_editor_presence;

static void dluma_log_event(
    const char* category, const char* level,
    const char* event, const char* detail)
{
    (void)detail;
    dluma_log_pair(level, category, event);
}

static WCHAR dluma_editor_presence_wide_lower(WCHAR value)
{
    return value >= L'A' && value <= L'Z'
        ? (WCHAR)(value + (L'a' - L'A')) : value;
}

static int dluma_editor_presence_wide_equal_ci(
    const WCHAR* left, const WCHAR* right)
{
    unsigned int index = 0;
    if (!left || !right) return left == right;
    while (left[index] && right[index]) {
        if (dluma_editor_presence_wide_lower(left[index]) !=
            dluma_editor_presence_wide_lower(right[index]))
            return 0;
        ++index;
    }
    return left[index] == right[index];
}

static int dluma_editor_presence_wide_starts_ci(
    const WCHAR* value, const WCHAR* prefix)
{
    unsigned int index = 0;
    if (!value || !prefix) return 0;
    while (prefix[index]) {
        if (!value[index] ||
            dluma_editor_presence_wide_lower(value[index]) !=
                dluma_editor_presence_wide_lower(prefix[index]))
            return 0;
        ++index;
    }
    return 1;
}

static int dluma_editor_presence_wide_contains_ci(
    const WCHAR* value, const WCHAR* needle)
{
    unsigned int offset;
    unsigned int index;
    if (!value || !needle || !needle[0]) return 0;
    for (offset = 0; value[offset]; ++offset) {
        for (index = 0; needle[index] && value[offset + index]; ++index)
            if (dluma_editor_presence_wide_lower(value[offset + index]) !=
                dluma_editor_presence_wide_lower(needle[index]))
                break;
        if (!needle[index]) return 1;
    }
    return 0;
}

static const WCHAR* dluma_editor_presence_skip_extended(const WCHAR* value)
{
    if (value && value[0] == L'\\' && value[1] == L'\\' &&
        value[2] == L'?' && value[3] == L'\\')
        return value + 4;
    return value;
}

static ULONGLONG dluma_editor_presence_filetime(const FILETIME* value)
{
    return value ? ((ULONGLONG)value->dwHighDateTime << 32) |
        value->dwLowDateTime : 0ULL;
}

static void dluma_editor_presence_callback_enter(void)
{
    dluma_atomic_increment(&g_dluma_editor_presence.callbacks_in_flight);
}

static void dluma_editor_presence_callback_leave(void)
{
    dluma_atomic_decrement(&g_dluma_editor_presence.callbacks_in_flight);
}

static int dluma_editor_presence_window_text(
    HWND window, WCHAR* output, unsigned int capacity)
{
    if (!window || !output || capacity < 2U) return 0;
    output[0] = 0;
    return GetWindowTextW(window, output, (int)capacity) > 0;
}

static int dluma_editor_presence_parse_window(
    HWND window, DlumaEditorPresenceObservation* output)
{
    WCHAR wide[DLUMA_EDITOR_PRESENCE_TITLE_CAPACITY];
    char utf8[DLUMA_EDITOR_PRESENCE_TITLE_CAPACITY];
    if (!output || !dluma_editor_presence_window_text(
            window, wide, DLUMA_ARRAY_COUNT(wide)) ||
        !dluma_wide_to_utf8(wide, utf8, DLUMA_ARRAY_COUNT(utf8)))
        return 0;
    return dluma_editor_presence_parse_title_utf8(
        utf8, DLUMA_ARRAY_COUNT(utf8), output);
}

static unsigned int dluma_editor_presence_window_priority(
    HWND window, const DlumaEditorDocumentCandidate* candidate,
    const DlumaEditorPresenceObservation* observation)
{
    unsigned int priority = 1U;
    HWND cursor;
    if (observation->document_kind == DLUMA_EDITOR_DOCUMENT_MAP) priority = 2U;
    if (observation->document_kind == DLUMA_EDITOR_DOCUMENT_QUEST) priority = 3U;
    cursor = candidate->focus;
    while (cursor) {
        if (cursor == window) return priority + 16U;
        cursor = GetParent(cursor);
    }
    if (window == candidate->foreground ||
        IsChild(window, candidate->foreground) ||
        IsChild(candidate->foreground, window))
        priority += 8U;
    return priority;
}

static BOOL CALLBACK dluma_editor_presence_find_document_window(
    HWND window, LPARAM parameter)
{
    DlumaEditorDocumentCandidate* candidate =
        (DlumaEditorDocumentCandidate*)parameter;
    DlumaEditorPresenceObservation observed;
    DWORD process_id = 0;
    unsigned int priority;
    if (!candidate || !IsWindowVisible(window) ||
        !GetWindowThreadProcessId(window, &process_id) ||
        process_id != candidate->process_id ||
        !dluma_editor_presence_parse_window(window, &observed))
        return TRUE;
    priority = dluma_editor_presence_window_priority(
        window, candidate, &observed);
    if (priority > candidate->priority) {
        candidate->priority = priority;
        candidate->observation = observed;
    }
    return TRUE;
}

static void dluma_editor_presence_observe_document(
    DlumaEditorPresenceObservation* output)
{
    DlumaEditorDocumentCandidate candidate;
    DlumaEditorGuiThreadInfo gui;
    HMODULE user32;
    DlumaEditorGetGuiThreadInfoFn get_gui_thread_info;
    DWORD foreground_process = 0;
    DWORD foreground_thread = 0;
    memset(&candidate, 0, sizeof(candidate));
    memset(&gui, 0, sizeof(gui));
    candidate.process_id = GetCurrentProcessId();
    candidate.foreground = GetForegroundWindow();
    foreground_thread = candidate.foreground
        ? GetWindowThreadProcessId(
              candidate.foreground, &foreground_process) : 0;
    user32 = GetModuleHandleW(L"user32.dll");
    get_gui_thread_info = user32
        ? (DlumaEditorGetGuiThreadInfoFn)GetProcAddress(
              user32, "GetGUIThreadInfo") : 0;
    gui.size = sizeof(gui);
    if (foreground_thread && foreground_process == candidate.process_id &&
        get_gui_thread_info && get_gui_thread_info(foreground_thread, &gui))
        candidate.focus = gui.focus;
    if (candidate.focus) {
        HWND cursor = candidate.focus;
        unsigned int level;
        for (level = 0; cursor && level < 12U; ++level) {
            DlumaEditorPresenceObservation focused;
            if (dluma_editor_presence_parse_window(cursor, &focused)) {
                *output = focused;
                return;
            }
            cursor = GetParent(cursor);
        }
    }
    (void)EnumWindows(
        dluma_editor_presence_find_document_window, (LPARAM)&candidate);
    if (g_dluma_editor_presence.main_window)
        (void)EnumChildWindows(
            g_dluma_editor_presence.main_window,
            dluma_editor_presence_find_document_window,
            (LPARAM)&candidate);
    if (candidate.priority) *output = candidate.observation;
    else dluma_editor_presence_build_browsing(output);
}

static BOOL CALLBACK dluma_editor_presence_find_main_window(
    HWND window, LPARAM parameter)
{
    DlumaEditorWindowCandidate* candidate =
        (DlumaEditorWindowCandidate*)parameter;
    RECT rectangle;
    DWORD thread_id;
    ULONGLONG area;
    if (!candidate ||
        !dluma_editor_is_main_window(window, candidate->process_id) ||
        !GetWindowRect(window, &rectangle) ||
        rectangle.right <= rectangle.left || rectangle.bottom <= rectangle.top)
        return TRUE;
    thread_id = GetWindowThreadProcessId(window, 0);
    if (!thread_id) return TRUE;
    area = (ULONGLONG)(rectangle.right - rectangle.left) *
           (ULONGLONG)(rectangle.bottom - rectangle.top);
    if (area > candidate->area) {
        candidate->window = window;
        candidate->thread_id = thread_id;
        candidate->area = area;
    }
    return TRUE;
}

static BOOL CALLBACK dluma_editor_presence_find_tab(
    HWND window, LPARAM parameter)
{
    HWND* output = (HWND*)parameter;
    WCHAR class_name[64];
    if (!output || *output ||
        !GetClassNameW(window, class_name,
                       (int)DLUMA_ARRAY_COUNT(class_name)))
        return TRUE;
    if (dluma_editor_presence_wide_equal_ci(
            class_name, L"SysTabControl32")) {
        *output = window;
        return FALSE;
    }
    return TRUE;
}

static BOOL CALLBACK dluma_editor_presence_find_stock_page(
    HWND window, LPARAM parameter)
{
    DlumaEditorStockPageSearch* search =
        (DlumaEditorStockPageSearch*)parameter;
    WCHAR class_name[128];
    if (!search || GetParent(window) == search->tab ||
        !GetClassNameW(window, class_name,
                       (int)DLUMA_ARRAY_COUNT(class_name)))
        return TRUE;
    if (!dluma_editor_presence_wide_equal_ci(class_name, L"#32770") &&
        !dluma_editor_presence_wide_starts_ci(class_name, L"Afx:"))
        return TRUE;
    if (search->count >= DLUMA_EDITOR_OPTIONS_STOCK_TABS) {
        search->refused = 1U;
        return FALSE;
    }
    search->pages[search->count++] = window;
    return TRUE;
}

typedef struct DlumaEditorResetSearch {
    HWND reset;
} DlumaEditorResetSearch;

static BOOL CALLBACK dluma_editor_presence_find_reset(
    HWND window, LPARAM parameter)
{
    DlumaEditorResetSearch* search = (DlumaEditorResetSearch*)parameter;
    WCHAR class_name[32];
    WCHAR title[32];
    if (!search || search->reset ||
        !GetClassNameW(window, class_name,
                       (int)DLUMA_ARRAY_COUNT(class_name)) ||
        !GetWindowTextW(window, title, (int)DLUMA_ARRAY_COUNT(title)))
        return TRUE;
    if (dluma_editor_presence_wide_equal_ci(class_name, L"Button") &&
        dluma_editor_presence_wide_equal_ci(title, L"Reset")) {
        search->reset = window;
        return FALSE;
    }
    return TRUE;
}

static int dluma_editor_presence_stock_tabs_exact(HWND tab)
{
    static const WCHAR* expected[DLUMA_EDITOR_OPTIONS_STOCK_TABS] = {
        L"Misc", L"Actions", L"Tabs", L"Classes"
    };
    unsigned int index;
    if (!tab || SendMessageW(
            tab, DLUMA_EDITOR_TCM_GETITEMCOUNT, 0, 0) !=
            DLUMA_EDITOR_OPTIONS_STOCK_TABS)
        return 0;
    for (index = 0; index < DLUMA_EDITOR_OPTIONS_STOCK_TABS; ++index) {
        DlumaEditorTcItemW item;
        WCHAR text[64];
        memset(&item, 0, sizeof(item));
        memset(text, 0, sizeof(text));
        item.mask = DLUMA_EDITOR_TCIF_TEXT;
        item.text = text;
        item.text_max = (int)DLUMA_ARRAY_COUNT(text);
        if (!SendMessageW(
                tab, DLUMA_EDITOR_TCM_GETITEMW, index,
                (LPARAM)&item) ||
            !dluma_editor_presence_wide_equal_ci(text, expected[index]))
            return 0;
    }
    return 1;
}

static void dluma_editor_presence_position_options_page(void)
{
    DlumaEditorOptionsState* options = &g_dluma_editor_presence.options;
    RECT rectangle;
    POINT* points = (POINT*)&rectangle;
    int width;
    int height;
    if (!options->dialog || !options->tab || !options->page ||
        !GetClientRect(options->tab, &rectangle))
        return;
    (void)SendMessageW(
        options->tab, DLUMA_EDITOR_TCM_ADJUSTRECT, 0,
        (LPARAM)&rectangle);
    if (!MapWindowPoints(options->tab, options->dialog, points, 2U))
        return;
    width = rectangle.right - rectangle.left;
    height = rectangle.bottom - rectangle.top;
    if (width < 120 || height < 80) return;
    (void)SetWindowPos(
        options->page, 0, rectangle.left, rectangle.top, width, height,
        DLUMA_EDITOR_SWP_NOZORDER | DLUMA_EDITOR_SWP_NOACTIVATE);
    (void)SetWindowPos(
        options->checkbox, 0, 14, 14, width - 28, 24,
        DLUMA_EDITOR_SWP_NOZORDER | DLUMA_EDITOR_SWP_NOACTIVATE);
    (void)SetWindowPos(
        options->hint, 0, 14, 46, width - 28, height - 54,
        DLUMA_EDITOR_SWP_NOZORDER | DLUMA_EDITOR_SWP_NOACTIVATE);
}

static void dluma_editor_presence_show_custom_page(int show)
{
    DlumaEditorOptionsState* options = &g_dluma_editor_presence.options;
    unsigned int index;
    if (!options->page) return;
    if (show) {
        for (index = 0; index < DLUMA_EDITOR_OPTIONS_STOCK_TABS; ++index)
            if (options->stock_pages[index])
                (void)ShowWindow(
                    options->stock_pages[index], DLUMA_EDITOR_SW_HIDE);
        dluma_editor_presence_position_options_page();
        (void)ShowWindow(options->page, DLUMA_EDITOR_SW_SHOW);
    } else {
        (void)ShowWindow(options->page, DLUMA_EDITOR_SW_HIDE);
    }
}

static int dluma_editor_presence_persist_enabled(int enabled)
{
    int persisted;
    enabled = enabled ? 1 : 0;
    persisted = WritePrivateProfileStringW(
        L"loader", L"editor_discord_rich_presence_enabled",
        enabled ? L"1" : L"0", g_dluma.config_path) ? 1 : 0;
    if (persisted)
        persisted = WritePrivateProfileStringW(
            0, 0, 0, g_dluma.config_path) ? 1 : 0;
    if (!persisted) {
        dluma_log_event(
            "editor_presence", "WARN", "options_persist_failed", 0);
        return 0;
    }
    g_dluma.config.editor_discord_rich_presence_enabled =
        (unsigned char)enabled;
    dluma_discord_presence_set_enabled(enabled);
    return 1;
}

static LRESULT CALLBACK dluma_editor_presence_options_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    DlumaEditorOptionsState* options = &g_dluma_editor_presence.options;
    WNDPROC predecessor = options->predecessor;
    LRESULT result;
    dluma_editor_presence_callback_enter();
    if (message == DLUMA_EDITOR_WM_NOTIFY &&
        options->tab && lparam) {
        DlumaEditorNmHdr* notification = (DlumaEditorNmHdr*)lparam;
        if (notification->window_from == options->tab &&
            notification->code == DLUMA_EDITOR_TCN_SELCHANGE) {
            int selected = (int)SendMessageW(
                options->tab, DLUMA_EDITOR_TCM_GETCURSEL, 0, 0);
            DlumaEditorOptionsAction action =
                dluma_editor_options_model_select(
                    &options->model,
                    (unsigned int)SendMessageW(
                        options->tab,
                        DLUMA_EDITOR_TCM_GETITEMCOUNT, 0, 0),
                    selected);
            if (action == DLUMA_EDITOR_OPTIONS_SHOW_CUSTOM) {
                dluma_editor_presence_show_custom_page(1);
                dluma_editor_presence_callback_leave();
                return 0;
            }
            dluma_editor_presence_show_custom_page(0);
            if (action == DLUMA_EDITOR_OPTIONS_FORWARD_STOCK)
                options->last_stock_index = selected;
        }
    }
    if (message == WM_COMMAND) {
        HWND control = (HWND)lparam;
        unsigned int notification = (unsigned int)(wparam >> 16);
        if (control == options->checkbox &&
            notification == DLUMA_EDITOR_BN_CLICKED) {
            int enabled = SendMessageW(
                options->checkbox, DLUMA_EDITOR_BM_GETCHECK, 0, 0) ==
                DLUMA_EDITOR_BST_CHECKED;
            if (dluma_editor_options_model_toggle(
                    &options->model, enabled) ==
                DLUMA_EDITOR_OPTIONS_PERSIST_TOGGLE)
                (void)dluma_editor_presence_persist_enabled(enabled);
            dluma_editor_presence_callback_leave();
            return 0;
        }
        if (control == options->reset &&
            notification == DLUMA_EDITOR_BN_CLICKED) {
            result = predecessor
                ? CallWindowProcW(
                      predecessor, window, message, wparam, lparam) : 0;
            if (dluma_editor_options_model_reset(&options->model) ==
                DLUMA_EDITOR_OPTIONS_PERSIST_RESET) {
                (void)SendMessageW(
                    options->checkbox, DLUMA_EDITOR_BM_SETCHECK,
                    DLUMA_EDITOR_BST_CHECKED, 0);
                (void)dluma_editor_presence_persist_enabled(1);
            }
            dluma_editor_presence_callback_leave();
            return result;
        }
    }
    if (message == WM_SIZE)
        dluma_editor_presence_position_options_page();
    result = predecessor
        ? CallWindowProcW(predecessor, window, message, wparam, lparam) : 0;
    if (message == WM_NCDESTROY) {
        if (options->model.admitted)
            (void)dluma_editor_options_model_teardown(&options->model);
        options->dialog = 0;
        options->tab = 0;
        options->page = 0;
        options->checkbox = 0;
        options->hint = 0;
        options->reset = 0;
        options->predecessor = 0;
        memset(options->stock_pages, 0, sizeof(options->stock_pages));
    }
    dluma_editor_presence_callback_leave();
    return result;
}

static void dluma_editor_presence_options_clear_created(
    DlumaEditorOptionsState* options, int remove_tab)
{
    if (!options) return;
    if (options->page && IsWindow(options->page))
        (void)DestroyWindow(options->page);
    if (remove_tab && options->tab && IsWindow(options->tab) &&
        SendMessageW(
            options->tab, DLUMA_EDITOR_TCM_GETITEMCOUNT, 0, 0) == 5)
        (void)SendMessageW(
            options->tab, DLUMA_EDITOR_TCM_DELETEITEM,
            DLUMA_EDITOR_OPTIONS_TAB_INDEX, 0);
    if (options->model.admitted)
        (void)dluma_editor_options_model_teardown(&options->model);
    options->page = 0;
    options->checkbox = 0;
    options->hint = 0;
}

static int dluma_editor_presence_attach_options(HWND dialog)
{
    DlumaEditorOptionsState candidate;
    DlumaEditorStockPageSearch page_search;
    DlumaEditorResetSearch reset_search;
    DlumaEditorTcItemW item;
    const DlumaDualHostRuntimeSnapshot* runtime =
        dluma_dual_host_runtime_snapshot();
    WCHAR title[64];
    WCHAR class_name[128];
    DWORD process_id = 0;
    DWORD thread_id;
    int selected;
    WNDPROC observed;
    WNDPROC installed;
    if (!dialog || g_dluma_editor_presence.stopping ||
        g_dluma_editor_presence.options.dialog)
        return 0;
    memset(&candidate, 0, sizeof(candidate));
    memset(&page_search, 0, sizeof(page_search));
    memset(&reset_search, 0, sizeof(reset_search));
    thread_id = GetWindowThreadProcessId(dialog, &process_id);
    if (!thread_id ||
        thread_id != g_dluma_editor_presence.owner_thread_id ||
        process_id != GetCurrentProcessId() ||
        !GetWindowTextW(dialog, title, (int)DLUMA_ARRAY_COUNT(title)) ||
        !dluma_editor_presence_wide_equal_ci(title, L"Editor options") ||
        !GetClassNameW(dialog, class_name,
                       (int)DLUMA_ARRAY_COUNT(class_name)) ||
        (!dluma_editor_presence_wide_equal_ci(class_name, L"#32770") &&
         !dluma_editor_presence_wide_starts_ci(class_name, L"Afx:")))
        return 0;
    (void)EnumChildWindows(
        dialog, dluma_editor_presence_find_tab,
        (LPARAM)&candidate.tab);
    if (!dluma_editor_presence_stock_tabs_exact(candidate.tab)) return 0;
    page_search.tab = candidate.tab;
    (void)EnumChildWindows(
        dialog, dluma_editor_presence_find_stock_page,
        (LPARAM)&page_search);
    (void)EnumChildWindows(
        dialog, dluma_editor_presence_find_reset,
        (LPARAM)&reset_search);
    if (page_search.refused ||
        page_search.count != DLUMA_EDITOR_OPTIONS_STOCK_TABS ||
        !reset_search.reset)
        return 0;
    candidate.dialog = dialog;
    candidate.reset = reset_search.reset;
    memcpy(candidate.stock_pages, page_search.pages,
           sizeof(candidate.stock_pages));
    selected = (int)SendMessageW(
        candidate.tab, DLUMA_EDITOR_TCM_GETCURSEL, 0, 0);
    if (selected < 0 ||
        selected >= (int)DLUMA_EDITOR_OPTIONS_STOCK_TABS)
        return 0;
    if (!dluma_editor_options_model_admit(
            &candidate.model,
            runtime && runtime->classification.exact_profile,
            1, 1,
            DLUMA_EDITOR_OPTIONS_STOCK_TABS,
            page_search.count, 0,
            g_dluma.config.editor_discord_rich_presence_enabled))
        return 0;
    candidate.model.selected_index = selected;
    candidate.last_stock_index = selected;
    memset(&item, 0, sizeof(item));
    item.mask = DLUMA_EDITOR_TCIF_TEXT;
    item.text = L"Workbench Mods";
    if (SendMessageW(
            candidate.tab, DLUMA_EDITOR_TCM_INSERTITEMW,
            DLUMA_EDITOR_OPTIONS_TAB_INDEX, (LPARAM)&item) !=
        DLUMA_EDITOR_OPTIONS_TAB_INDEX)
        return 0;
    candidate.page = CreateWindowExW(
        WS_EX_CONTROLPARENT, L"STATIC", L"",
        DLUMA_EDITOR_WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 1, 1, dialog,
        (HMENU)(UINT_PTR)DLUMA_EDITOR_OPTIONS_CONTROL_PAGE,
        GetModuleHandleW(0), 0);
    if (!candidate.page) {
        dluma_editor_presence_options_clear_created(&candidate, 1);
        return 0;
    }
    candidate.checkbox = CreateWindowExW(
        0, L"BUTTON", L"Discord Rich Presence",
        DLUMA_EDITOR_WS_CHILD | DLUMA_EDITOR_WS_VISIBLE |
            DLUMA_EDITOR_WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 1, 1, candidate.page,
        (HMENU)(UINT_PTR)DLUMA_EDITOR_OPTIONS_CONTROL_DISCORD,
        GetModuleHandleW(0), 0);
    candidate.hint = CreateWindowExW(
        0, L"STATIC",
        L"Shares only the sanitized document type and filename with Discord. Paths and project details are never published.",
        DLUMA_EDITOR_WS_CHILD | DLUMA_EDITOR_WS_VISIBLE | SS_LEFT,
        0, 0, 1, 1, candidate.page,
        (HMENU)(UINT_PTR)DLUMA_EDITOR_OPTIONS_CONTROL_HINT,
        GetModuleHandleW(0), 0);
    if (!candidate.checkbox || !candidate.hint) {
        dluma_editor_presence_options_clear_created(&candidate, 1);
        return 0;
    }
    (void)SendMessageW(
        candidate.checkbox, DLUMA_EDITOR_BM_SETCHECK,
        g_dluma.config.editor_discord_rich_presence_enabled
            ? DLUMA_EDITOR_BST_CHECKED : 0, 0);
    observed = (WNDPROC)GetWindowLongPtrW(dialog, GWLP_WNDPROC);
    if (!observed || observed == dluma_editor_presence_options_proc) {
        dluma_editor_presence_options_clear_created(&candidate, 1);
        return 0;
    }
    candidate.predecessor = observed;
    g_dluma_editor_presence.options = candidate;
    installed = (WNDPROC)SetWindowLongPtrW(
        dialog, GWLP_WNDPROC,
        (LONG_PTR)dluma_editor_presence_options_proc);
    if (installed != observed ||
        (WNDPROC)GetWindowLongPtrW(dialog, GWLP_WNDPROC) !=
            dluma_editor_presence_options_proc) {
        if ((WNDPROC)GetWindowLongPtrW(dialog, GWLP_WNDPROC) ==
            dluma_editor_presence_options_proc)
            (void)SetWindowLongPtrW(
                dialog, GWLP_WNDPROC, (LONG_PTR)observed);
        dluma_editor_presence_options_clear_created(
            &g_dluma_editor_presence.options, 1);
        memset(&g_dluma_editor_presence.options, 0,
               sizeof(g_dluma_editor_presence.options));
        return 0;
    }
    dluma_editor_presence_position_options_page();
    dluma_log_event(
        "editor_presence", "INFO", "options_tab_attached", 0);
    return 1;
}

static BOOL CALLBACK dluma_editor_presence_find_options_dialog(
    HWND window, LPARAM parameter)
{
    (void)parameter;
    if (g_dluma_editor_presence.options.dialog) return FALSE;
    (void)dluma_editor_presence_attach_options(window);
    return g_dluma_editor_presence.options.dialog ? FALSE : TRUE;
}

static void dluma_editor_presence_discover_options(void)
{
    if (!g_dluma_editor_presence.owner_thread_id ||
        g_dluma_editor_presence.options.dialog ||
        g_dluma_editor_presence.stopping)
        return;
    (void)EnumThreadWindows(
        g_dluma_editor_presence.owner_thread_id,
        dluma_editor_presence_find_options_dialog, 0);
}

static LRESULT CALLBACK dluma_editor_presence_main_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    WNDPROC predecessor = g_dluma_editor_presence.main_predecessor;
    LRESULT result;
    dluma_editor_presence_callback_enter();
    if (message == DLUMA_EDITOR_PRESENCE_DISCOVER_OPTIONS)
        dluma_editor_presence_discover_options();
    result = predecessor
        ? CallWindowProcW(predecessor, window, message, wparam, lparam) : 0;
    if (message == WM_NCDESTROY) {
        g_dluma_editor_presence.main_window = 0;
        g_dluma_editor_presence.main_predecessor = 0;
        g_dluma_editor_presence.owner_thread_id = 0;
    }
    dluma_editor_presence_callback_leave();
    return result;
}

static int dluma_editor_presence_attach_main_window(void)
{
    DlumaEditorWindowCandidate candidate;
    WNDPROC observed;
    WNDPROC installed;
    if (g_dluma_editor_presence.main_window ||
        g_dluma_editor_presence.stopping)
        return g_dluma_editor_presence.main_window ? 1 : 0;
    memset(&candidate, 0, sizeof(candidate));
    candidate.process_id = GetCurrentProcessId();
    (void)EnumWindows(
        dluma_editor_presence_find_main_window, (LPARAM)&candidate);
    if (!candidate.window || !candidate.thread_id) return 0;
    observed = (WNDPROC)GetWindowLongPtrW(
        candidate.window, GWLP_WNDPROC);
    if (!observed || observed == dluma_editor_presence_main_proc) return 0;
    g_dluma_editor_presence.main_window = candidate.window;
    g_dluma_editor_presence.owner_thread_id = candidate.thread_id;
    g_dluma_editor_presence.main_predecessor = observed;
    installed = (WNDPROC)SetWindowLongPtrW(
        candidate.window, GWLP_WNDPROC,
        (LONG_PTR)dluma_editor_presence_main_proc);
    if (!installed) {
        g_dluma_editor_presence.main_window = 0;
        g_dluma_editor_presence.owner_thread_id = 0;
        g_dluma_editor_presence.main_predecessor = 0;
        return 0;
    }
    /* Preserve any subclass installed between observation and installation. */
    if (installed != dluma_editor_presence_main_proc)
        g_dluma_editor_presence.main_predecessor = installed;
    return 1;
}

static int dluma_editor_presence_ascii_equal_ci(
    const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return left == right;
    while (left[index] && right[index]) {
        char a = left[index];
        char b = right[index];
        if (a >= 'a' && a <= 'z') a = (char)(a - ('a' - 'A'));
        if (b >= 'a' && b <= 'z') b = (char)(b - ('a' - 'A'));
        if (a != b) return 0;
        ++index;
    }
    return left[index] == right[index];
}

static void dluma_editor_presence_playtest_lock(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma_editor_presence.playtest_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_editor_presence_playtest_unlock(void)
{
    (void)dluma_atomic_exchange(
        &g_dluma_editor_presence.playtest_lock, 0);
}

static DWORD dluma_editor_presence_process_parent(DWORD process_id)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    DlumaEditorCreateToolhelp32SnapshotFn create_snapshot;
    DlumaEditorProcess32FirstWFn first;
    DlumaEditorProcess32NextWFn next;
    DlumaEditorProcessEntry32W entry;
    HANDLE snapshot;
    DWORD parent = 0;
    if (!kernel32 || !process_id) return 0;
    create_snapshot = (DlumaEditorCreateToolhelp32SnapshotFn)GetProcAddress(
        kernel32, "CreateToolhelp32Snapshot");
    first = (DlumaEditorProcess32FirstWFn)GetProcAddress(
        kernel32, "Process32FirstW");
    next = (DlumaEditorProcess32NextWFn)GetProcAddress(
        kernel32, "Process32NextW");
    if (!create_snapshot || !first || !next) return 0;
    snapshot = create_snapshot(DLUMA_EDITOR_TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    memset(&entry, 0, sizeof(entry));
    entry.size = sizeof(entry);
    if (first(snapshot, &entry)) {
        do {
            if (entry.process_id == process_id) {
                parent = entry.parent_process_id;
                break;
            }
            entry.size = sizeof(entry);
        } while (next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return parent;
}

static int dluma_editor_presence_query_process_path(
    HANDLE process, WCHAR* output, unsigned int capacity)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    DlumaEditorQueryFullProcessImageNameWFn query;
    DWORD count;
    if (!process || !output || capacity < 2U || !kernel32) return 0;
    output[0] = 0;
    query = (DlumaEditorQueryFullProcessImageNameWFn)GetProcAddress(
        kernel32, "QueryFullProcessImageNameW");
    if (!query) return 0;
    count = capacity;
    if (!query(process, 0, output, &count) || !count ||
        count >= capacity)
        return 0;
    output[count] = 0;
    return 1;
}

static int dluma_editor_presence_process_exact(
    HANDLE process, DWORD process_id, const WCHAR* expected_path,
    ULONGLONG* start_time)
{
    WCHAR process_path[DLUMA_MAX_WPATH];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    FILETIME created;
    FILETIME exited;
    FILETIME kernel;
    FILETIME user;
    if (!process || !process_id || !expected_path || !expected_path[0] ||
        dluma_editor_presence_process_parent(process_id) !=
            GetCurrentProcessId() ||
        !dluma_editor_presence_query_process_path(
            process, process_path, DLUMA_ARRAY_COUNT(process_path)) ||
        !dluma_editor_presence_wide_equal_ci(
            dluma_editor_presence_skip_extended(process_path),
            dluma_editor_presence_skip_extended(expected_path)) ||
        !dluma_sha256_file_hex(
            process_path, DLUMA_EDITOR_PLAYER_MAX_BYTES, sha256) ||
        !dluma_editor_presence_ascii_equal_ci(
            sha256, DLUMA_EDITOR_PLAYER_SHA256) ||
        !GetProcessTimes(
            process, &created, &exited, &kernel, &user))
        return 0;
    *start_time = dluma_editor_presence_filetime(&created);
    return *start_time != 0ULL;
}

static int dluma_editor_presence_launch_valid(
    const DlumaEditorPresencePlaytestLaunch* launch)
{
    const DlumaDualHostRuntimeSnapshot* runtime =
        dluma_dual_host_runtime_snapshot();
    ULONGLONG now = GetTickCount64();
    if (!launch || !runtime || !runtime->initialized ||
        runtime->classification.kind != DLUMA_HOST_CHROME_EDITOR ||
        runtime->classification.forwarding_only ||
        launch->handoff.struct_size != sizeof(launch->handoff) ||
        launch->handoff.schema_version != 1U ||
        launch->handoff.parent_pid != GetCurrentProcessId() ||
        launch->handoff.parent_start_time != runtime->process_start_time ||
        !launch->handoff.generation ||
        launch->handoff.expires_ms < now ||
        launch->handoff.created_ms > now ||
        !runtime->evidence.loader_sha256[0] ||
        !dluma_editor_presence_ascii_equal_ci(
            launch->handoff.loader_sha256,
            runtime->evidence.loader_sha256) ||
        !launch->player_path[0])
        return 0;
    return 1;
}

static int dluma_editor_presence_begin_tracking(
    HANDLE source_process, DWORD process_id,
    const DlumaEditorPresencePlaytestLaunch* launch)
{
    HANDLE process = 0;
    ULONGLONG start_time = 0;
    DlumaEditorPlaytestOwnershipEvidence evidence;
    int accepted = 0;
    if (!source_process || !process_id ||
        !dluma_editor_presence_launch_valid(launch) ||
        !DuplicateHandle(
            GetCurrentProcess(), source_process, GetCurrentProcess(),
            &process, 0, FALSE, DUPLICATE_SAME_ACCESS) ||
        !dluma_editor_presence_process_exact(
            process, process_id, launch->player_path, &start_time)) {
        if (process) CloseHandle(process);
        return 0;
    }
    memset(&evidence, 0, sizeof(evidence));
    evidence.pid_match = 1U;
    evidence.start_time_match = 1U;
    evidence.executable_match = 1U;
    evidence.loader_match = 1U;
    evidence.handoff_match = 1U;
    evidence.unexpired = 1U;
    dluma_editor_presence_playtest_lock();
    if (!g_dluma_editor_presence.playtest_process &&
        dluma_editor_playtest_ownership_begin(
            &g_dluma_editor_presence.ownership, &evidence,
            launch->handoff.generation)) {
        g_dluma_editor_presence.playtest_process = process;
        g_dluma_editor_presence.playtest_pid = process_id;
        g_dluma_editor_presence.playtest_start_time = start_time;
        g_dluma_editor_presence.playtest_generation =
            g_dluma_editor_presence.ownership.generation;
        g_dluma_editor_presence.expectation_valid = 0U;
        accepted = 1;
    }
    dluma_editor_presence_playtest_unlock();
    if (!accepted) CloseHandle(process);
    if (accepted && g_dluma_editor_presence.wake_event)
        (void)SetEvent(g_dluma_editor_presence.wake_event);
    return accepted;
}

static void dluma_editor_presence_discover_expected_process(void)
{
    DlumaEditorPresencePlaytestLaunch launch;
    ULONGLONG deadline = 0;
    HMODULE kernel32;
    DlumaEditorCreateToolhelp32SnapshotFn create_snapshot;
    DlumaEditorProcess32FirstWFn first;
    DlumaEditorProcess32NextWFn next;
    DlumaEditorProcessEntry32W entry;
    HANDLE snapshot;
    int found = 0;
    memset(&launch, 0, sizeof(launch));
    dluma_editor_presence_playtest_lock();
    if (g_dluma_editor_presence.expectation_valid) {
        launch = g_dluma_editor_presence.expectation;
        deadline = g_dluma_editor_presence.expectation_deadline_ms;
    }
    dluma_editor_presence_playtest_unlock();
    if (!deadline) return;
    if (GetTickCount64() > deadline) {
        dluma_editor_presence_playtest_lock();
        if (g_dluma_editor_presence.expectation_valid &&
            g_dluma_editor_presence.expectation.handoff.generation ==
                launch.handoff.generation)
            g_dluma_editor_presence.expectation_valid = 0U;
        dluma_editor_presence_playtest_unlock();
        dluma_log_event(
            "editor_presence", "WARN",
            "playtest_process_discovery_expired", 0);
        return;
    }
    kernel32 = GetModuleHandleW(L"kernel32.dll");
    create_snapshot = kernel32
        ? (DlumaEditorCreateToolhelp32SnapshotFn)GetProcAddress(
              kernel32, "CreateToolhelp32Snapshot") : 0;
    first = kernel32
        ? (DlumaEditorProcess32FirstWFn)GetProcAddress(
              kernel32, "Process32FirstW") : 0;
    next = kernel32
        ? (DlumaEditorProcess32NextWFn)GetProcAddress(
              kernel32, "Process32NextW") : 0;
    if (!create_snapshot || !first || !next) return;
    snapshot = create_snapshot(DLUMA_EDITOR_TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    memset(&entry, 0, sizeof(entry));
    entry.size = sizeof(entry);
    if (first(snapshot, &entry)) {
        do {
            if (entry.parent_process_id == GetCurrentProcessId() &&
                dluma_editor_presence_wide_equal_ci(
                    entry.executable, L"DyingLightPlayer.exe")) {
                HANDLE process = OpenProcess(
                    DLUMA_EDITOR_SYNCHRONIZE |
                        DLUMA_EDITOR_PROCESS_QUERY_LIMITED_INFORMATION,
                    FALSE, entry.process_id);
                if (process) {
                    found = dluma_editor_presence_begin_tracking(
                        process, entry.process_id, &launch);
                    CloseHandle(process);
                    if (found) break;
                }
            }
            entry.size = sizeof(entry);
        } while (next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

static void dluma_editor_presence_service_playtest(void)
{
    HANDLE process;
    DWORD process_id;
    ULONGLONG start_time;
    ULONGLONG generation;
    unsigned int suspended;
    DWORD wait_result;
    dluma_editor_presence_playtest_lock();
    process = g_dluma_editor_presence.playtest_process;
    process_id = g_dluma_editor_presence.playtest_pid;
    start_time = g_dluma_editor_presence.playtest_start_time;
    generation = g_dluma_editor_presence.playtest_generation;
    suspended = g_dluma_editor_presence.playtest_suspended;
    dluma_editor_presence_playtest_unlock();
    if (!process) {
        dluma_editor_presence_discover_expected_process();
        return;
    }
    wait_result = WaitForSingleObject(process, 0);
    if (wait_result == WAIT_TIMEOUT) {
        if (!suspended) {
            dluma_editor_presence_playtest_lock();
            if (g_dluma_editor_presence.playtest_process == process &&
                g_dluma_editor_presence.playtest_pid == process_id &&
                g_dluma_editor_presence.playtest_start_time == start_time &&
                g_dluma_editor_presence.playtest_generation == generation) {
                g_dluma_editor_presence.playtest_suspended = 1U;
                suspended = 1U;
            }
            dluma_editor_presence_playtest_unlock();
            if (suspended) {
                dluma_discord_presence_set_suspended(1);
                dluma_log_event(
                    "editor_presence", "INFO",
                    "playtest_presence_takeover", 0);
            }
        }
        return;
    }
    dluma_editor_presence_playtest_lock();
    if (g_dluma_editor_presence.playtest_process == process &&
        g_dluma_editor_presence.playtest_pid == process_id &&
        g_dluma_editor_presence.playtest_start_time == start_time &&
        g_dluma_editor_presence.playtest_generation == generation &&
        dluma_editor_playtest_ownership_finish(
            &g_dluma_editor_presence.ownership, generation)) {
        g_dluma_editor_presence.playtest_process = 0;
        g_dluma_editor_presence.playtest_pid = 0;
        g_dluma_editor_presence.playtest_start_time = 0;
        g_dluma_editor_presence.playtest_suspended = 0U;
    } else {
        process = 0;
    }
    dluma_editor_presence_playtest_unlock();
    if (process) CloseHandle(process);
    if (suspended) {
        dluma_discord_presence_set_suspended(0);
        dluma_log_event(
            "editor_presence", "INFO",
            wait_result == WAIT_OBJECT_0
                ? "playtest_presence_restored"
                : "playtest_presence_restored_after_wait_failure",
            0);
    }
}

void dluma_editor_presence_note_playtest_launch(
    const DlumaEditorPresencePlaytestLaunch* launch,
    const void* process_information)
{
    const DlumaEditorProcessInformation* information =
        (const DlumaEditorProcessInformation*)process_information;
    ULONGLONG deadline;
    int accepted = 0;
    if (!g_dluma_editor_presence.started ||
        g_dluma_editor_presence.stopping ||
        !dluma_editor_presence_launch_valid(launch)) {
        dluma_log_event(
            "editor_presence", "WARN",
            "playtest_presence_launch_rejected", 0);
        return;
    }
    if (information && information->process &&
        information->process_id)
        accepted = dluma_editor_presence_begin_tracking(
            information->process, information->process_id, launch);
    else {
        deadline = GetTickCount64() + 10000ULL;
        if (deadline > launch->handoff.expires_ms)
            deadline = launch->handoff.expires_ms;
        dluma_editor_presence_playtest_lock();
        if (!g_dluma_editor_presence.playtest_process &&
            (!g_dluma_editor_presence.expectation_valid ||
             launch->handoff.generation >
                g_dluma_editor_presence.expectation.handoff.generation) &&
            launch->handoff.generation >
                g_dluma_editor_presence.playtest_generation) {
            g_dluma_editor_presence.expectation = *launch;
            g_dluma_editor_presence.expectation_deadline_ms = deadline;
            g_dluma_editor_presence.expectation_valid = 1U;
            accepted = 1;
        }
        dluma_editor_presence_playtest_unlock();
        if (accepted && g_dluma_editor_presence.wake_event)
            (void)SetEvent(g_dluma_editor_presence.wake_event);
    }
    dluma_log_event(
        "editor_presence", accepted ? "INFO" : "WARN",
        accepted ? "playtest_presence_launch_admitted"
                 : "playtest_presence_launch_rejected",
        0);
}

static DWORD WINAPI dluma_editor_presence_worker(void* parameter)
{
    (void)parameter;
    while (WaitForSingleObject(
               g_dluma_editor_presence.stop_event, 0) == WAIT_TIMEOUT) {
        DlumaEditorPresenceObservation observation;
        /* Startup can precede creation of the main window. */
        (void)dluma_editor_exit_observer_start();
        if (dluma_editor_presence_attach_main_window() &&
            g_dluma_editor_presence.main_window)
            (void)PostMessageW(
                g_dluma_editor_presence.main_window,
                DLUMA_EDITOR_PRESENCE_DISCOVER_OPTIONS, 0, 0);
        dluma_editor_presence_service_playtest();
        dluma_editor_presence_observe_document(&observation);
        if (!g_dluma_editor_presence.last_observation_valid ||
            !dluma_editor_presence_observation_equal(
                &observation,
                &g_dluma_editor_presence.last_observation)) {
            g_dluma_editor_presence.last_observation = observation;
            g_dluma_editor_presence.last_observation_valid = 1U;
            dluma_discord_presence_observe_editor(
                &observation.snapshot,
                observation.document_identity);
        }
        if (g_dluma_editor_presence.wake_event) {
            (void)WaitForSingleObject(
                g_dluma_editor_presence.wake_event,
                DLUMA_EDITOR_PRESENCE_POLL_MS);
            (void)ResetEvent(g_dluma_editor_presence.wake_event);
        } else {
            Sleep(DLUMA_EDITOR_PRESENCE_POLL_MS);
        }
    }
    return 0;
}

static void dluma_editor_presence_detach_windows(void)
{
    DlumaEditorOptionsState* options = &g_dluma_editor_presence.options;
    if (options->dialog && IsWindow(options->dialog) &&
        options->predecessor &&
        (WNDPROC)GetWindowLongPtrW(
            options->dialog, GWLP_WNDPROC) ==
            dluma_editor_presence_options_proc)
        (void)SetWindowLongPtrW(
            options->dialog, GWLP_WNDPROC,
            (LONG_PTR)options->predecessor);
    dluma_editor_presence_options_clear_created(options, 1);
    memset(options, 0, sizeof(*options));
    if (g_dluma_editor_presence.main_window &&
        IsWindow(g_dluma_editor_presence.main_window) &&
        g_dluma_editor_presence.main_predecessor &&
        (WNDPROC)GetWindowLongPtrW(
            g_dluma_editor_presence.main_window, GWLP_WNDPROC) ==
            dluma_editor_presence_main_proc)
        (void)SetWindowLongPtrW(
            g_dluma_editor_presence.main_window, GWLP_WNDPROC,
            (LONG_PTR)g_dluma_editor_presence.main_predecessor);
    g_dluma_editor_presence.main_window = 0;
    g_dluma_editor_presence.main_predecessor = 0;
    g_dluma_editor_presence.owner_thread_id = 0;
}

int dluma_editor_presence_start(void)
{
    const DlumaDualHostRuntimeSnapshot* runtime =
        dluma_dual_host_runtime_snapshot();
    if (g_dluma_editor_presence.started)
        return g_dluma_editor_presence.worker_thread ? 1 : 0;
    if (!runtime || !runtime->initialized ||
        runtime->classification.kind != DLUMA_HOST_CHROME_EDITOR ||
        runtime->classification.forwarding_only)
        return 0;
    memset(&g_dluma_editor_presence, 0,
           sizeof(g_dluma_editor_presence));
    g_dluma_editor_presence.stop_event =
        CreateEventW(0, TRUE, FALSE, 0);
    g_dluma_editor_presence.wake_event =
        CreateEventW(0, TRUE, FALSE, 0);
    if (!g_dluma_editor_presence.stop_event ||
        !g_dluma_editor_presence.wake_event) {
        if (g_dluma_editor_presence.stop_event)
            CloseHandle(g_dluma_editor_presence.stop_event);
        if (g_dluma_editor_presence.wake_event)
            CloseHandle(g_dluma_editor_presence.wake_event);
        memset(&g_dluma_editor_presence, 0,
               sizeof(g_dluma_editor_presence));
        return 0;
    }
    g_dluma_editor_presence.started = 1;
    g_dluma_editor_presence.worker_thread = CreateThread(
        0, 0, dluma_editor_presence_worker, 0, 0, 0);
    if (!g_dluma_editor_presence.worker_thread) {
        CloseHandle(g_dluma_editor_presence.stop_event);
        CloseHandle(g_dluma_editor_presence.wake_event);
        memset(&g_dluma_editor_presence, 0,
               sizeof(g_dluma_editor_presence));
        return 0;
    }
    dluma_log_event(
        "editor_presence", "INFO", "observer_started", 0);
    return 1;
}

int dluma_editor_presence_stop(void)
{
    HANDLE process = 0;
    unsigned int suspended = 0;
    ULONGLONG deadline;
    if (!g_dluma_editor_presence.started) return 1;
    g_dluma_editor_presence.stopping = 1;
    if (g_dluma_editor_presence.stop_event)
        (void)SetEvent(g_dluma_editor_presence.stop_event);
    if (g_dluma_editor_presence.wake_event)
        (void)SetEvent(g_dluma_editor_presence.wake_event);
    if (g_dluma_editor_presence.worker_thread &&
        WaitForSingleObject(
            g_dluma_editor_presence.worker_thread,
            DLUMA_EDITOR_PRESENCE_STOP_TIMEOUT_MS) != WAIT_OBJECT_0) {
        dluma_log_event(
            "editor_presence", "ERROR", "observer_stop_timeout", 0);
        return 0;
    }
    if (g_dluma_editor_presence.worker_thread) {
        CloseHandle(g_dluma_editor_presence.worker_thread);
        g_dluma_editor_presence.worker_thread = 0;
    }
    dluma_editor_presence_playtest_lock();
    process = g_dluma_editor_presence.playtest_process;
    if (g_dluma_editor_presence.ownership.active)
        (void)dluma_editor_playtest_ownership_finish(
            &g_dluma_editor_presence.ownership, g_dluma_editor_presence.ownership.generation);
    suspended = g_dluma_editor_presence.playtest_suspended;
    g_dluma_editor_presence.playtest_process = 0;
    g_dluma_editor_presence.playtest_suspended = 0U;
    g_dluma_editor_presence.expectation_valid = 0U;
    dluma_editor_presence_playtest_unlock();
    if (process) CloseHandle(process);
    if (suspended) dluma_discord_presence_set_suspended(0);
    dluma_editor_presence_detach_windows();
    deadline = GetTickCount64() +
        DLUMA_EDITOR_PRESENCE_STOP_TIMEOUT_MS;
    while (g_dluma_editor_presence.callbacks_in_flight &&
           GetTickCount64() < deadline)
        Sleep(1);
    if (g_dluma_editor_presence.callbacks_in_flight) {
        dluma_log_event(
            "editor_presence", "ERROR", "callback_drain_timeout", 0);
        return 0;
    }
    if (g_dluma_editor_presence.stop_event)
        CloseHandle(g_dluma_editor_presence.stop_event);
    if (g_dluma_editor_presence.wake_event)
        CloseHandle(g_dluma_editor_presence.wake_event);
    memset(&g_dluma_editor_presence, 0,
           sizeof(g_dluma_editor_presence));
    dluma_log_event(
        "editor_presence", "INFO", "observer_stopped", 0);
    return 1;
}

int dluma_editor_presence_callbacks_drained(void)
{
    return !g_dluma_editor_presence.started ||
        (!g_dluma_editor_presence.worker_thread &&
         !g_dluma_editor_presence.callbacks_in_flight);
}
