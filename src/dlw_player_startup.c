#include "dlw_player_startup.h"

#include "dlw_common.h"
#include "dlw_dual_host_runtime.h"
#include "dlw_log.h"

#define DLUMA_PLAYER_STARTUP_ENGINE_TIMESTAMP 0x623395D0UL
#define DLUMA_PLAYER_STARTUP_ENGINE_IMAGE_SIZE 0x00E8D000UL
#define DLUMA_PLAYER_STARTUP_ENGINE_SHA256 \
    "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C"
#define DLUMA_PLAYER_STARTUP_IGAME_INITIALIZE_IAT_RVA 0x000524C8UL
#define DLUMA_PLAYER_STARTUP_HIDE_SPLASH_IAT_RVA 0x000524E0UL
#define DLUMA_PLAYER_STARTUP_SET_FOCUS_IAT_RVA 0x00856CD0UL
#define DLUMA_PLAYER_STARTUP_SHOW_WINDOW_IAT_RVA 0x00856D48UL
#define DLUMA_PLAYER_STARTUP_PE64_MAGIC 0x020BU
#define DLUMA_PLAYER_STARTUP_IMPORT_DIRECTORY_OFFSET 0x78UL
#define DLUMA_PLAYER_STARTUP_SPLASH_CONTROL_ID 1002
#define DLUMA_PLAYER_STARTUP_MAX_MODULE_BYTES (64ULL * 1024ULL * 1024ULL)
#define DLUMA_PLAYER_STARTUP_HOOK_COUNT 2U

#define DLUMA_GWL_STYLE (-16)
#define DLUMA_SS_TYPEMASK 0x0000001FUL
#define DLUMA_SS_BITMAP 0x0000000EUL
#define DLUMA_WM_PAINT 0x000FU
#define DLUMA_RDW_INVALIDATE 0x0001UL
#define DLUMA_RDW_UPDATENOW 0x0100UL
#define DLUMA_TRANSPARENT 1
#define DLUMA_DEFAULT_GUI_FONT 17
#define DLUMA_DT_LEFT 0x0000U
#define DLUMA_DT_BOTTOM 0x0008U
#define DLUMA_DT_SINGLELINE 0x0020U
#define DLUMA_DT_NOPREFIX 0x0800U
#define DLUMA_DT_END_ELLIPSIS 0x8000U
#define DLUMA_SW_SHOWNOACTIVATE 4

#define DLUMA_PLAYER_STARTUP_IDLE 0L
#define DLUMA_PLAYER_STARTUP_PREPARING 1L
#define DLUMA_PLAYER_STARTUP_ARMED 2L
#define DLUMA_PLAYER_STARTUP_INITIALIZING 3L
#define DLUMA_PLAYER_STARTUP_FINISHING 4L
#define DLUMA_PLAYER_STARTUP_COMPLETE 5L
#define DLUMA_PLAYER_STARTUP_FAIL_OPEN 6L

typedef void *HICON;
typedef void *HDC;
typedef void *HGDIOBJ;
typedef DWORD COLORREF;

typedef int (__fastcall *DlumaPlayerStartupInitializeFn)(
    void *game,
    char *command_line,
    int show_command,
    HICON large_icon,
    HICON small_icon,
    DWORD unknown_a,
    DWORD unknown_b,
    void *progress_indicator);
typedef void (__cdecl *DlumaPlayerStartupHideSplashFn)(void);
typedef BOOL (WINAPI *DlumaPlayerStartupShowWindowFn)(HWND, int);
typedef HWND (WINAPI *DlumaPlayerStartupSetFocusFn)(HWND);
typedef unsigned int (WINAPI *DlumaPlayerStartupGetDpiForWindowFn)(HWND);
typedef HGDIOBJ (WINAPI *DlumaPlayerStartupGetStockObjectFn)(int);
typedef HGDIOBJ (WINAPI *DlumaPlayerStartupSelectObjectFn)(HDC, HGDIOBJ);
typedef int (WINAPI *DlumaPlayerStartupSetBkModeFn)(HDC, int);
typedef COLORREF (WINAPI *DlumaPlayerStartupSetTextColorFn)(HDC, COLORREF);
typedef void (__fastcall *DlumaPlayerStartupProgressMethod)(void *, const void *);

typedef struct DlumaPlayerStartupProgressIndicator {
    DlumaPlayerStartupProgressMethod *vtable;
} DlumaPlayerStartupProgressIndicator;

typedef struct DlumaPlayerStartupSplashSearch {
    DWORD process_id;
    DWORD thread_id;
    HWND splash_window;
    HWND bitmap_control;
} DlumaPlayerStartupSplashSearch;

typedef struct DlumaPlayerStartupRuntime {
    volatile LONG lifecycle;
    volatile LONG state_lock;
    volatile LONG active_calls;
    DlumaPlayerStartupSnapshot snapshot;
    HMODULE player_module;
    HMODULE engine_module;
    HWND splash_window;
    HWND splash_bitmap;
    WNDPROC splash_original_proc;
    HWND game_window;
    int captured_show_command;
    DlumaPlayerStartupHook player_hooks[DLUMA_PLAYER_STARTUP_HOOK_COUNT];
    DlumaPlayerStartupHook engine_hooks[DLUMA_PLAYER_STARTUP_HOOK_COUNT];
    DlumaPlayerStartupInitializeFn original_initialize;
    DlumaPlayerStartupHideSplashFn original_hide_splash;
    DlumaPlayerStartupShowWindowFn original_show_window;
    DlumaPlayerStartupSetFocusFn original_set_focus;
} DlumaPlayerStartupRuntime;

__declspec(dllimport) BOOL WINAPI EnumThreadWindows(
    DWORD thread_id, WNDENUMPROC callback, LPARAM context);
__declspec(dllimport) HWND WINAPI GetDlgItem(HWND window, int control_id);
__declspec(dllimport) int WINAPI GetClassNameW(
    HWND window, WCHAR *class_name, int capacity);
__declspec(dllimport) HDC WINAPI GetDC(HWND window);
__declspec(dllimport) int WINAPI ReleaseDC(HWND window, HDC dc);
__declspec(dllimport) BOOL WINAPI RedrawWindow(
    HWND window, const RECT *update_rect, HANDLE update_region, DWORD flags);
__declspec(dllimport) BOOL WINAPI UpdateWindow(HWND window);
__declspec(dllimport) HWND WINAPI GetFocus(void);
__declspec(dllimport) int WINAPI DrawTextA(
    HDC dc, LPCSTR text, int count, RECT *rect, unsigned int format);

static const char g_dluma_player_startup_initialize_export[] =
    "?Initialize@IGame@@QEAAHPEADHPEAUHICON__@@1KKPEAVIProgressIndicator@@@Z";
static const char g_dluma_player_startup_hide_export[] = "HideSplashscreen";
static const char g_dluma_player_startup_show_window_import[] = "ShowWindow";
static const char g_dluma_player_startup_set_focus_import[] = "SetFocus";
static DlumaPlayerStartupRuntime g_dluma_player_startup;

static int __fastcall dluma_player_startup_initialize_hook(
    void *game,
    char *command_line,
    int show_command,
    HICON large_icon,
    HICON small_icon,
    DWORD unknown_a,
    DWORD unknown_b,
    void *progress_indicator);
static void __cdecl dluma_player_startup_hide_splash_hook(void);
static BOOL WINAPI dluma_player_startup_show_window_hook(
    HWND window, int show_command);
static HWND WINAPI dluma_player_startup_set_focus_hook(HWND window);
static LRESULT CALLBACK dluma_player_startup_splash_proc(
    HWND window, unsigned int message, WPARAM wparam, LPARAM lparam);

static void dluma_player_startup_lock(void)
{
    while (dluma_atomic_compare_exchange(
               &g_dluma_player_startup.state_lock, 1, 0) != 0)
        Sleep(0);
}

static void dluma_player_startup_unlock(void)
{
    dluma_atomic_exchange(&g_dluma_player_startup.state_lock, 0);
}

static WORD dluma_player_startup_read_u16(const void *address)
{
    const BYTE *bytes = (const BYTE *)address;
    return (WORD)((WORD)bytes[0] | ((WORD)bytes[1] << 8));
}

static DWORD dluma_player_startup_read_u32(const void *address)
{
    const BYTE *bytes = (const BYTE *)address;
    return (DWORD)bytes[0] | ((DWORD)bytes[1] << 8) |
           ((DWORD)bytes[2] << 16) | ((DWORD)bytes[3] << 24);
}

static ULONGLONG dluma_player_startup_read_u64(const void *address)
{
    const BYTE *bytes = (const BYTE *)address;
    ULONGLONG value = 0;
    unsigned int index;
    for (index = 0; index < 8U; ++index)
        value |= ((ULONGLONG)bytes[index]) << (index * 8U);
    return value;
}

static int dluma_player_startup_pe_identity(
    HMODULE module,
    DWORD *timestamp,
    DWORD *image_size,
    DWORD *nt_offset)
{
    BYTE *base = (BYTE *)module;
    DWORD nt;
    if (!base || !dluma_is_readable(base, 0x100U) ||
        base[0] != 'M' || base[1] != 'Z')
        return 0;
    nt = dluma_player_startup_read_u32(base + 0x3CU);
    if (nt > 0x100000U || !dluma_is_readable(base + nt, 0x108U) ||
        base[nt] != 'P' || base[nt + 1U] != 'E' ||
        dluma_player_startup_read_u16(base + nt + 24U) !=
            DLUMA_PLAYER_STARTUP_PE64_MAGIC)
        return 0;
    if (timestamp)
        *timestamp = dluma_player_startup_read_u32(base + nt + 8U);
    if (image_size)
        *image_size = dluma_player_startup_read_u32(base + nt + 0x50U);
    if (nt_offset) *nt_offset = nt;
    return 1;
}

static int dluma_player_startup_rva_valid(
    DWORD rva,
    SIZE_T size,
    DWORD image_size)
{
    return rva != 0U && rva < image_size && size <= image_size - rva;
}

static int dluma_player_startup_image_string_equal(
    const BYTE *base,
    DWORD image_size,
    DWORD rva,
    const char *expected,
    int fold_case)
{
    unsigned int index = 0;
    if (!expected || !dluma_player_startup_rva_valid(rva, 1U, image_size))
        return 0;
    while (expected[index]) {
        char left;
        char right;
        if (index >= 255U ||
            !dluma_player_startup_rva_valid(rva + index, 1U, image_size) ||
            !dluma_is_readable(base + rva + index, 1U))
            return 0;
        left = (char)base[rva + index];
        right = expected[index];
        if (fold_case) {
            if (left >= 'a' && left <= 'z') left = (char)(left - 'a' + 'A');
            if (right >= 'a' && right <= 'z') right = (char)(right - 'a' + 'A');
        }
        if (left != right) return 0;
        ++index;
    }
    return dluma_player_startup_rva_valid(rva + index, 1U, image_size) &&
           dluma_is_readable(base + rva + index, 1U) &&
           base[rva + index] == 0;
}

static void **dluma_player_startup_find_named_iat_slot(
    HMODULE module,
    DWORD timestamp,
    DWORD image_size,
    DWORD exact_iat_rva,
    const char *provider_name,
    const char *import_name,
    void *expected_target)
{
    BYTE *base = (BYTE *)module;
    DWORD observed_timestamp = 0;
    DWORD observed_image_size = 0;
    DWORD nt = 0;
    DWORD import_rva;
    DWORD import_size;
    DWORD descriptor_offset;
    void **found = 0;

    if (!dluma_player_startup_pe_identity(
            module, &observed_timestamp, &observed_image_size, &nt) ||
        observed_timestamp != timestamp || observed_image_size != image_size ||
        !dluma_player_startup_rva_valid(
            exact_iat_rva, sizeof(void *), image_size))
        return 0;
    import_rva = dluma_player_startup_read_u32(
        base + nt + 24U + DLUMA_PLAYER_STARTUP_IMPORT_DIRECTORY_OFFSET);
    import_size = dluma_player_startup_read_u32(
        base + nt + 24U + DLUMA_PLAYER_STARTUP_IMPORT_DIRECTORY_OFFSET + 4U);
    if (!dluma_player_startup_rva_valid(import_rva, 20U, image_size) ||
        import_size < 20U || import_size > image_size - import_rva)
        return 0;

    for (descriptor_offset = 0U;
         descriptor_offset + 20U <= import_size;
         descriptor_offset += 20U) {
        BYTE *descriptor = base + import_rva + descriptor_offset;
        DWORD original_thunk = dluma_player_startup_read_u32(descriptor);
        DWORD name_rva = dluma_player_startup_read_u32(descriptor + 12U);
        DWORD first_thunk = dluma_player_startup_read_u32(descriptor + 16U);
        unsigned int index;
        if (!original_thunk && !first_thunk) break;
        if (!dluma_player_startup_image_string_equal(
                base, image_size, name_rva, provider_name, 1))
            continue;
        if (!dluma_player_startup_rva_valid(
                original_thunk, 8U, image_size) ||
            !dluma_player_startup_rva_valid(first_thunk, 8U, image_size))
            return 0;
        for (index = 0U; ; ++index) {
            DWORD original_rva;
            DWORD slot_rva;
            ULONGLONG thunk;
            DWORD import_by_name_rva;
            void **slot;
            if (index > image_size / 8U) return 0;
            original_rva = original_thunk + index * 8U;
            slot_rva = first_thunk + index * 8U;
            if (!dluma_player_startup_rva_valid(
                    original_rva, 8U, image_size) ||
                !dluma_player_startup_rva_valid(
                    slot_rva, 8U, image_size))
                return 0;
            thunk = dluma_player_startup_read_u64(base + original_rva);
            if (!thunk) break;
            if ((thunk & 0x8000000000000000ULL) != 0ULL ||
                thunk > 0xFFFFFFFFULL)
                continue;
            import_by_name_rva = (DWORD)thunk;
            if (!dluma_player_startup_rva_valid(
                    import_by_name_rva, 3U, image_size))
                return 0;
            if (!dluma_player_startup_image_string_equal(
                    base, image_size, import_by_name_rva + 2U,
                    import_name, 0))
                continue;
            if (slot_rva != exact_iat_rva || found) return 0;
            slot = (void **)(base + slot_rva);
            if (!dluma_is_readable(slot, sizeof(void *)) ||
                *slot != expected_target)
                return 0;
            found = slot;
        }
    }
    return found;
}

static int dluma_player_startup_compare_exchange_slot(
    void *context,
    void **slot,
    void *expected,
    void *replacement,
    void **observed_out)
{
    DWORD old_protect = 0;
    DWORD restored_protect = 0;
    void *observed;
    int wrote = 0;
    (void)context;
    if (observed_out) *observed_out = 0;
    if (!slot || !dluma_is_readable(slot, sizeof(void *))) return 0;
    observed = *slot;
    if (observed_out) *observed_out = observed;
    if (observed != expected) return 0;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old_protect))
        return 0;
    observed = *slot;
    if (observed == expected) {
        (void)dluma_atomic_pointer_exchange(
            (PVOID volatile *)slot, replacement);
        wrote = *slot == replacement;
    }
    if (!VirtualProtect(
            slot, sizeof(void *), old_protect, &restored_protect))
        wrote = 0;
    (void)FlushInstructionCache(
        GetCurrentProcess(), slot, sizeof(void *));
    if (observed_out) *observed_out = *slot;
    return wrote;
}

static int dluma_player_startup_wide_class_equal(
    const WCHAR *value,
    const WCHAR *expected)
{
    unsigned int index = 0;
    if (!value || !expected) return 0;
    while (value[index] && expected[index]) {
        WCHAR left = value[index];
        WCHAR right = expected[index];
        if (left >= L'a' && left <= L'z') left = (WCHAR)(left - L'a' + L'A');
        if (right >= L'a' && right <= L'z') right = (WCHAR)(right - L'a' + L'A');
        if (left != right) return 0;
        ++index;
    }
    return value[index] == expected[index];
}

static BOOL CALLBACK dluma_player_startup_find_splash_window(
    HWND window,
    LPARAM context_value)
{
    DlumaPlayerStartupSplashSearch *context =
        (DlumaPlayerStartupSplashSearch *)context_value;
    HWND bitmap;
    DWORD process_id = 0;
    DWORD thread_id;
    WCHAR class_name[32];
    LONG_PTR style;
    if (!context || !window) return TRUE;
    thread_id = GetWindowThreadProcessId(window, &process_id);
    if (thread_id != context->thread_id || process_id != context->process_id)
        return TRUE;
    bitmap = GetDlgItem(window, DLUMA_PLAYER_STARTUP_SPLASH_CONTROL_ID);
    if (!bitmap ||
        GetWindowThreadProcessId(bitmap, &process_id) != context->thread_id ||
        process_id != context->process_id)
        return TRUE;
    class_name[0] = 0;
    if (!GetClassNameW(bitmap, class_name, (int)DLUMA_ARRAY_COUNT(class_name)) ||
        !dluma_player_startup_wide_class_equal(class_name, L"Static"))
        return TRUE;
    style = GetWindowLongPtrW(bitmap, DLUMA_GWL_STYLE);
    if (((DWORD)style & DLUMA_SS_TYPEMASK) != DLUMA_SS_BITMAP) return TRUE;
    context->splash_window = window;
    context->bitmap_control = bitmap;
    return FALSE;
}

static int dluma_player_startup_find_splash(
    HWND *splash_out,
    HWND *bitmap_out)
{
    DlumaPlayerStartupSplashSearch context;
    memset(&context, 0, sizeof(context));
    context.process_id = GetCurrentProcessId();
    context.thread_id = GetCurrentThreadId();
    (void)EnumThreadWindows(
        context.thread_id, dluma_player_startup_find_splash_window,
        (LPARAM)&context);
    if (!context.splash_window || !context.bitmap_control) return 0;
    *splash_out = context.splash_window;
    *bitmap_out = context.bitmap_control;
    return 1;
}

static int dluma_player_startup_repaint(void)
{
    HWND bitmap = g_dluma_player_startup.splash_bitmap;
    if (!bitmap || !IsWindow(bitmap)) return 0;
    if (!RedrawWindow(
            bitmap, 0, 0,
            DLUMA_RDW_INVALIDATE | DLUMA_RDW_UPDATENOW))
        return 0;
    return UpdateWindow(bitmap) != FALSE;
}

static void dluma_player_startup_set_status(
    const char *status,
    int native_status)
{
    int repaint;
    LONG lifecycle = dluma_atomic_compare_exchange(
        &g_dluma_player_startup.lifecycle, 0, 0);
    if (lifecycle != DLUMA_PLAYER_STARTUP_ARMED &&
        lifecycle != DLUMA_PLAYER_STARTUP_INITIALIZING)
        return;
    dluma_player_startup_lock();
    dluma_player_startup_core_status(
        &g_dluma_player_startup.snapshot, status, native_status, 0);
    dluma_player_startup_unlock();
    repaint = dluma_player_startup_repaint();
    if (repaint) {
        dluma_player_startup_lock();
        ++g_dluma_player_startup.snapshot.splash_repaint_count;
        dluma_player_startup_unlock();
    }
}

static void dluma_player_startup_draw_status(HWND window)
{
    char status[DLUMA_PLAYER_STARTUP_STATUS_CAPACITY];
    RECT rect;
    HDC dc;
    HGDIOBJ font;
    HGDIOBJ previous_font = 0;
    unsigned int dpi = 96U;
    LONG margin_x;
    LONG margin_y;
    HMODULE user32;
    HMODULE gdi32;
    DlumaPlayerStartupGetDpiForWindowFn get_dpi;
    DlumaPlayerStartupGetStockObjectFn get_stock_object;
    DlumaPlayerStartupSelectObjectFn select_object;
    DlumaPlayerStartupSetBkModeFn set_bk_mode;
    DlumaPlayerStartupSetTextColorFn set_text_color;
    dluma_player_startup_lock();
    (void)dluma_player_startup_core_copy_bounded(
        status, sizeof(status),
        g_dluma_player_startup.snapshot.current_status,
        sizeof(status) - 1U);
    dluma_player_startup_unlock();
    if (!status[0] || !GetClientRect(window, &rect)) return;
    user32 = GetModuleHandleW(L"user32.dll");
    gdi32 = GetModuleHandleW(L"gdi32.dll");
    get_dpi = user32 ? (DlumaPlayerStartupGetDpiForWindowFn)GetProcAddress(
        user32, "GetDpiForWindow") : 0;
    get_stock_object = gdi32 ?
        (DlumaPlayerStartupGetStockObjectFn)GetProcAddress(
            gdi32, "GetStockObject") : 0;
    select_object = gdi32 ?
        (DlumaPlayerStartupSelectObjectFn)GetProcAddress(
            gdi32, "SelectObject") : 0;
    set_bk_mode = gdi32 ?
        (DlumaPlayerStartupSetBkModeFn)GetProcAddress(
            gdi32, "SetBkMode") : 0;
    set_text_color = gdi32 ?
        (DlumaPlayerStartupSetTextColorFn)GetProcAddress(
            gdi32, "SetTextColor") : 0;
    if (!get_stock_object || !select_object ||
        !set_bk_mode || !set_text_color)
        return;
    if (get_dpi) {
        unsigned int observed = get_dpi(window);
        if (observed >= 48U && observed <= 768U) dpi = observed;
    }
    margin_x = (LONG)((10U * dpi + 48U) / 96U);
    margin_y = (LONG)((8U * dpi + 48U) / 96U);
    rect.left += margin_x;
    rect.right -= margin_x;
    rect.bottom -= margin_y;
    dc = GetDC(window);
    if (!dc) return;
    font = get_stock_object(DLUMA_DEFAULT_GUI_FONT);
    if (font) previous_font = select_object(dc, font);
    (void)set_bk_mode(dc, DLUMA_TRANSPARENT);
    (void)set_text_color(dc, 0x00FFFFFFUL);
    (void)DrawTextA(
        dc, status, -1, &rect,
        DLUMA_DT_LEFT | DLUMA_DT_BOTTOM | DLUMA_DT_SINGLELINE |
            DLUMA_DT_NOPREFIX | DLUMA_DT_END_ELLIPSIS);
    if (previous_font) (void)select_object(dc, previous_font);
    (void)ReleaseDC(window, dc);
}

static int dluma_player_startup_restore_splash_subclass(void)
{
    HWND bitmap = g_dluma_player_startup.splash_bitmap;
    WNDPROC original = g_dluma_player_startup.splash_original_proc;
    WNDPROC observed;
    if (!bitmap || !original) return 1;
    if (!IsWindow(bitmap)) {
        g_dluma_player_startup.splash_bitmap = 0;
        g_dluma_player_startup.splash_original_proc = 0;
        return 1;
    }
    observed = (WNDPROC)GetWindowLongPtrW(bitmap, GWLP_WNDPROC);
    if (observed == original) {
        dluma_player_startup_lock();
        dluma_player_startup_core_restore_splash(
            &g_dluma_player_startup.snapshot);
        dluma_player_startup_unlock();
        g_dluma_player_startup.splash_bitmap = 0;
        g_dluma_player_startup.splash_original_proc = 0;
        return 1;
    }
    if (observed != dluma_player_startup_splash_proc) {
        dluma_player_startup_lock();
        g_dluma_player_startup.snapshot.ownership_conflict = 1U;
        dluma_player_startup_unlock();
        return 0;
    }
    (void)SetWindowLongPtrW(bitmap, GWLP_WNDPROC, (LONG_PTR)original);
    if ((WNDPROC)GetWindowLongPtrW(bitmap, GWLP_WNDPROC) != original)
        return 0;
    dluma_player_startup_lock();
    dluma_player_startup_core_restore_splash(
        &g_dluma_player_startup.snapshot);
    dluma_player_startup_unlock();
    g_dluma_player_startup.splash_bitmap = 0;
    g_dluma_player_startup.splash_original_proc = 0;
    return 1;
}

static LRESULT CALLBACK dluma_player_startup_splash_proc(
    HWND window,
    unsigned int message,
    WPARAM wparam,
    LPARAM lparam)
{
    WNDPROC original = g_dluma_player_startup.splash_original_proc;
    LRESULT result;
    dluma_atomic_increment(&g_dluma_player_startup.active_calls);
    if (!original) {
        dluma_atomic_decrement(&g_dluma_player_startup.active_calls);
        return 0;
    }
    if (message == WM_NCDESTROY &&
        (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC) ==
            dluma_player_startup_splash_proc) {
        (void)SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)original);
        dluma_player_startup_lock();
        dluma_player_startup_core_restore_splash(
            &g_dluma_player_startup.snapshot);
        dluma_player_startup_unlock();
        g_dluma_player_startup.splash_bitmap = 0;
        g_dluma_player_startup.splash_original_proc = 0;
    }
    result = CallWindowProcW(original, window, message, wparam, lparam);
    if (message == DLUMA_WM_PAINT) dluma_player_startup_draw_status(window);
    dluma_atomic_decrement(&g_dluma_player_startup.active_calls);
    return result;
}

static int dluma_player_startup_bind_splash(void)
{
    HWND splash = 0;
    HWND bitmap = 0;
    WNDPROC original;
    if (!dluma_player_startup_find_splash(&splash, &bitmap)) return 0;
    original = (WNDPROC)GetWindowLongPtrW(bitmap, GWLP_WNDPROC);
    if (!original || original == dluma_player_startup_splash_proc) return 0;
    g_dluma_player_startup.splash_window = splash;
    g_dluma_player_startup.splash_bitmap = bitmap;
    g_dluma_player_startup.splash_original_proc = original;
    (void)SetWindowLongPtrW(
        bitmap, GWLP_WNDPROC, (LONG_PTR)dluma_player_startup_splash_proc);
    if ((WNDPROC)GetWindowLongPtrW(bitmap, GWLP_WNDPROC) !=
            dluma_player_startup_splash_proc) {
        g_dluma_player_startup.splash_bitmap = 0;
        g_dluma_player_startup.splash_original_proc = 0;
        return 0;
    }
    dluma_player_startup_lock();
    dluma_player_startup_core_bind_splash(
        &g_dluma_player_startup.snapshot);
    dluma_player_startup_unlock();
    return 1;
}

static int dluma_player_startup_validate_profile(void)
{
    const DlumaDualHostRuntimeSnapshot *runtime =
        dluma_dual_host_runtime_snapshot();
    DWORD timestamp = 0;
    DWORD image_size = 0;
    int player_host;
    if (!runtime || !runtime->initialized ||
        !runtime->evidence.canonical_image_verified)
        return 0;
    player_host =
        runtime->classification.kind == DLUMA_HOST_PLAYER_STANDALONE ||
        runtime->classification.kind == DLUMA_HOST_PLAYER_EDITOR_PLAYTEST;
    if (!dluma_player_startup_pe_identity(
            GetModuleHandleW(0), &timestamp, &image_size, 0))
        return 0;
    return dluma_player_startup_core_profile_matches(
        player_host,
        runtime->classification.exact_profile != 0U,
        timestamp,
        image_size,
        runtime->evidence.image.sha256);
}

static int dluma_player_startup_validate_engine_identity(HMODULE engine)
{
    DWORD timestamp = 0;
    DWORD image_size = 0;
    WCHAR path[DLUMA_MAX_WPATH];
    char sha256[DLUMA_SHA256_HEX_CAPACITY];
    DWORD length;
    if (!engine || !dluma_player_startup_pe_identity(
            engine, &timestamp, &image_size, 0) ||
        timestamp != DLUMA_PLAYER_STARTUP_ENGINE_TIMESTAMP ||
        image_size != DLUMA_PLAYER_STARTUP_ENGINE_IMAGE_SIZE)
        return 0;
    length = GetModuleFileNameW(engine, path, DLUMA_ARRAY_COUNT(path));
    if (!length || length >= DLUMA_ARRAY_COUNT(path) ||
        !dluma_sha256_file_hex(
            path, DLUMA_PLAYER_STARTUP_MAX_MODULE_BYTES, sha256))
        return 0;
    return dluma_stricmp(sha256, DLUMA_PLAYER_STARTUP_ENGINE_SHA256) == 0;
}

static int dluma_player_startup_resolve_slots(void)
{
    HMODULE player = GetModuleHandleW(0);
    HMODULE engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    void *initialize_target;
    void *hide_target;
    void *show_target;
    void *focus_target;
    void **initialize_slot;
    void **hide_slot;
    void **show_slot;
    void **focus_slot;
    if (!player || !engine || !user32 ||
        !dluma_player_startup_validate_engine_identity(engine))
        return 0;
    initialize_target = (void *)GetProcAddress(
        engine, g_dluma_player_startup_initialize_export);
    hide_target = (void *)GetProcAddress(
        engine, g_dluma_player_startup_hide_export);
    show_target = (void *)GetProcAddress(
        user32, g_dluma_player_startup_show_window_import);
    focus_target = (void *)GetProcAddress(
        user32, g_dluma_player_startup_set_focus_import);
    if (!initialize_target || !hide_target || !show_target || !focus_target)
        return 0;
    initialize_slot = dluma_player_startup_find_named_iat_slot(
        player, 0x623397B8UL, 0x00103000UL,
        DLUMA_PLAYER_STARTUP_IGAME_INITIALIZE_IAT_RVA,
        "engine_x64_rwdi.dll", g_dluma_player_startup_initialize_export,
        initialize_target);
    hide_slot = dluma_player_startup_find_named_iat_slot(
        player, 0x623397B8UL, 0x00103000UL,
        DLUMA_PLAYER_STARTUP_HIDE_SPLASH_IAT_RVA,
        "engine_x64_rwdi.dll", g_dluma_player_startup_hide_export,
        hide_target);
    show_slot = dluma_player_startup_find_named_iat_slot(
        engine, DLUMA_PLAYER_STARTUP_ENGINE_TIMESTAMP,
        DLUMA_PLAYER_STARTUP_ENGINE_IMAGE_SIZE,
        DLUMA_PLAYER_STARTUP_SHOW_WINDOW_IAT_RVA,
        "USER32.dll", g_dluma_player_startup_show_window_import,
        show_target);
    focus_slot = dluma_player_startup_find_named_iat_slot(
        engine, DLUMA_PLAYER_STARTUP_ENGINE_TIMESTAMP,
        DLUMA_PLAYER_STARTUP_ENGINE_IMAGE_SIZE,
        DLUMA_PLAYER_STARTUP_SET_FOCUS_IAT_RVA,
        "USER32.dll", g_dluma_player_startup_set_focus_import,
        focus_target);
    if (!initialize_slot || !hide_slot || !show_slot || !focus_slot)
        return 0;

    g_dluma_player_startup.player_module = player;
    g_dluma_player_startup.engine_module = engine;
    g_dluma_player_startup.original_initialize =
        (DlumaPlayerStartupInitializeFn)initialize_target;
    g_dluma_player_startup.original_hide_splash =
        (DlumaPlayerStartupHideSplashFn)hide_target;
    g_dluma_player_startup.original_show_window =
        (DlumaPlayerStartupShowWindowFn)show_target;
    g_dluma_player_startup.original_set_focus =
        (DlumaPlayerStartupSetFocusFn)focus_target;
    g_dluma_player_startup.player_hooks[0].slot = initialize_slot;
    g_dluma_player_startup.player_hooks[0].original = initialize_target;
    g_dluma_player_startup.player_hooks[0].replacement =
        (void *)dluma_player_startup_initialize_hook;
    g_dluma_player_startup.player_hooks[1].slot = hide_slot;
    g_dluma_player_startup.player_hooks[1].original = hide_target;
    g_dluma_player_startup.player_hooks[1].replacement =
        (void *)dluma_player_startup_hide_splash_hook;
    g_dluma_player_startup.engine_hooks[0].slot = show_slot;
    g_dluma_player_startup.engine_hooks[0].original = show_target;
    g_dluma_player_startup.engine_hooks[0].replacement =
        (void *)dluma_player_startup_show_window_hook;
    g_dluma_player_startup.engine_hooks[1].slot = focus_slot;
    g_dluma_player_startup.engine_hooks[1].original = focus_target;
    g_dluma_player_startup.engine_hooks[1].replacement =
        (void *)dluma_player_startup_set_focus_hook;
    return 1;
}

static void dluma_player_startup_note_hook_state(
    DlumaPlayerStartupHook *hooks,
    unsigned int hook_count,
    int engine_hooks)
{
    unsigned int index;
    unsigned int installed = 1U;
    unsigned int owned = 1U;
    unsigned int restored = 1U;
    unsigned int conflict = 0U;
    for (index = 0; index < hook_count; ++index) {
        if (!hooks[index].installed) installed = 0U;
        if (!hooks[index].installed || hooks[index].restored ||
            !hooks[index].slot ||
            !dluma_is_readable(hooks[index].slot, sizeof(void *)) ||
            *hooks[index].slot != hooks[index].replacement)
            owned = 0U;
        if (hooks[index].installed && !hooks[index].restored)
            restored = 0U;
        if (hooks[index].ownership_conflict) conflict = 1U;
    }
    dluma_player_startup_lock();
    if (engine_hooks) {
        g_dluma_player_startup.snapshot.engine_hooks_installed = installed;
        g_dluma_player_startup.snapshot.engine_hooks_owned = owned;
        g_dluma_player_startup.snapshot.engine_hooks_restored = restored;
    } else {
        g_dluma_player_startup.snapshot.player_hooks_installed = installed;
        g_dluma_player_startup.snapshot.player_hooks_owned = owned;
        g_dluma_player_startup.snapshot.player_hooks_restored = restored;
    }
    if (conflict) g_dluma_player_startup.snapshot.ownership_conflict = 1U;
    dluma_player_startup_unlock();
}

static int dluma_player_startup_install_player_hooks(void)
{
    int result = dluma_player_startup_core_install_hooks(
        g_dluma_player_startup.player_hooks,
        DLUMA_PLAYER_STARTUP_HOOK_COUNT,
        dluma_player_startup_compare_exchange_slot,
        0);
    dluma_player_startup_note_hook_state(
        g_dluma_player_startup.player_hooks,
        DLUMA_PLAYER_STARTUP_HOOK_COUNT,
        0);
    return result;
}

static int dluma_player_startup_restore_player_hooks(void)
{
    int result = dluma_player_startup_core_restore_hooks(
        g_dluma_player_startup.player_hooks,
        DLUMA_PLAYER_STARTUP_HOOK_COUNT,
        dluma_player_startup_compare_exchange_slot,
        0);
    dluma_player_startup_note_hook_state(
        g_dluma_player_startup.player_hooks,
        DLUMA_PLAYER_STARTUP_HOOK_COUNT,
        0);
    return result;
}

static int dluma_player_startup_install_engine_hooks(void)
{
    unsigned int index;
    for (index = 0; index < DLUMA_PLAYER_STARTUP_HOOK_COUNT; ++index) {
        DlumaPlayerStartupHook *hook =
            &g_dluma_player_startup.engine_hooks[index];
        if (!hook->slot || !dluma_is_readable(hook->slot, sizeof(void *)) ||
            *hook->slot != hook->original)
            return 0;
    }
    if (!dluma_player_startup_core_install_hooks(
            g_dluma_player_startup.engine_hooks,
            DLUMA_PLAYER_STARTUP_HOOK_COUNT,
            dluma_player_startup_compare_exchange_slot,
            0)) {
        dluma_player_startup_note_hook_state(
            g_dluma_player_startup.engine_hooks,
            DLUMA_PLAYER_STARTUP_HOOK_COUNT,
            1);
        return 0;
    }
    dluma_player_startup_note_hook_state(
        g_dluma_player_startup.engine_hooks,
        DLUMA_PLAYER_STARTUP_HOOK_COUNT,
        1);
    return 1;
}

static int dluma_player_startup_restore_engine_hooks(void)
{
    int result = dluma_player_startup_core_restore_hooks(
        g_dluma_player_startup.engine_hooks,
        DLUMA_PLAYER_STARTUP_HOOK_COUNT,
        dluma_player_startup_compare_exchange_slot,
        0);
    dluma_player_startup_note_hook_state(
        g_dluma_player_startup.engine_hooks,
        DLUMA_PLAYER_STARTUP_HOOK_COUNT,
        1);
    return result;
}

static void dluma_player_startup_close_splash(void)
{
    DlumaPlayerStartupHideSplashFn hide =
        g_dluma_player_startup.original_hide_splash;
    (void)dluma_player_startup_restore_splash_subclass();
    if (hide) hide();
    g_dluma_player_startup.splash_window = 0;
}

static void dluma_player_startup_mark_fail_open(const char *reason)
{
    dluma_player_startup_lock();
    dluma_player_startup_core_fail_open(
        &g_dluma_player_startup.snapshot, reason);
    dluma_player_startup_unlock();
    dluma_log_pair("WARN", "player_startup_fail_open", reason);
}

static void dluma_player_startup_fail_open_before_initialize(
    const char *reason)
{
    unsigned int deferred_hide;
    dluma_atomic_exchange(
        &g_dluma_player_startup.lifecycle, DLUMA_PLAYER_STARTUP_FAIL_OPEN);
    dluma_player_startup_mark_fail_open(reason);
    (void)dluma_player_startup_restore_engine_hooks();
    (void)dluma_player_startup_restore_player_hooks();
    (void)dluma_player_startup_restore_splash_subclass();
    dluma_player_startup_lock();
    deferred_hide = g_dluma_player_startup.snapshot.deferred_hide_count;
    dluma_player_startup_unlock();
    if (deferred_hide && g_dluma_player_startup.original_hide_splash)
        g_dluma_player_startup.original_hide_splash();
}

static int dluma_player_startup_is_game_window(HWND window)
{
    WCHAR class_name[64];
    char class_ascii[64];
    DWORD process_id = 0;
    unsigned int index;
    if (!window || !IsWindow(window)) return 0;
    if (g_dluma_player_startup.game_window)
        return window == g_dluma_player_startup.game_window;
    (void)GetWindowThreadProcessId(window, &process_id);
    class_name[0] = 0;
    if (!GetClassNameW(
            window, class_name, (int)DLUMA_ARRAY_COUNT(class_name)))
        return 0;
    for (index = 0; index + 1U < DLUMA_ARRAY_COUNT(class_ascii) &&
                    class_name[index]; ++index) {
        if (class_name[index] > 0x7FU) return 0;
        class_ascii[index] = (char)class_name[index];
    }
    class_ascii[index] = 0;
    if (!dluma_player_startup_core_window_matches(
            GetCurrentProcessId(), process_id, class_ascii))
        return 0;
    g_dluma_player_startup.game_window = window;
    return 1;
}

static BOOL WINAPI dluma_player_startup_show_window_hook(
    HWND window,
    int show_command)
{
    DlumaPlayerStartupShowWindowFn original =
        g_dluma_player_startup.original_show_window;
    LONG lifecycle;
    dluma_atomic_increment(&g_dluma_player_startup.active_calls);
    lifecycle = dluma_atomic_compare_exchange(
        &g_dluma_player_startup.lifecycle, 0, 0);
    if (lifecycle == DLUMA_PLAYER_STARTUP_INITIALIZING &&
        dluma_player_startup_is_game_window(window)) {
        dluma_player_startup_lock();
        if (g_dluma_player_startup.snapshot.deferred_show_count == 0U)
            g_dluma_player_startup.captured_show_command = show_command;
        ++g_dluma_player_startup.snapshot.deferred_show_count;
        dluma_player_startup_unlock();
        dluma_atomic_decrement(&g_dluma_player_startup.active_calls);
        return FALSE;
    }
    dluma_atomic_decrement(&g_dluma_player_startup.active_calls);
    return original ? original(window, show_command) : FALSE;
}

static HWND WINAPI dluma_player_startup_set_focus_hook(HWND window)
{
    DlumaPlayerStartupSetFocusFn original =
        g_dluma_player_startup.original_set_focus;
    LONG lifecycle;
    dluma_atomic_increment(&g_dluma_player_startup.active_calls);
    lifecycle = dluma_atomic_compare_exchange(
        &g_dluma_player_startup.lifecycle, 0, 0);
    if (lifecycle == DLUMA_PLAYER_STARTUP_INITIALIZING &&
        dluma_player_startup_is_game_window(window)) {
        HWND previous = GetFocus();
        dluma_player_startup_lock();
        ++g_dluma_player_startup.snapshot.deferred_focus_count;
        dluma_player_startup_unlock();
        dluma_atomic_decrement(&g_dluma_player_startup.active_calls);
        return previous;
    }
    dluma_atomic_decrement(&g_dluma_player_startup.active_calls);
    return original ? original(window) : 0;
}

static void __fastcall dluma_player_startup_progress_noop(
    void *self,
    const void *value)
{
    (void)self;
    (void)value;
}

static void __fastcall dluma_player_startup_progress_update(
    void *self,
    const void *ttl_string)
{
    const char *source;
    char copied[DLUMA_PLAYER_STARTUP_STATUS_CAPACITY];
    SIZE_T readable = 0;
    ULONG_PTR address;
    (void)self;
    if (!ttl_string || !dluma_is_readable(ttl_string, sizeof(const char *)))
        return;
    source = *(const char * const *)ttl_string;
    if (!source) return;
    address = (ULONG_PTR)source;
    while (readable < DLUMA_PLAYER_STARTUP_TTL_COPY_LIMIT) {
        if (address + readable < address ||
            !dluma_is_readable((const void *)(address + readable), 1U))
            break;
        ++readable;
        if (source[readable - 1U] == 0) break;
    }
    if (!readable) return;
    (void)dluma_player_startup_core_copy_bounded(
        copied, sizeof(copied), source, readable);
    if (copied[0]) dluma_player_startup_set_status(copied, 1);
}

static DlumaPlayerStartupProgressMethod g_dluma_player_startup_progress_vtable[7] = {
    dluma_player_startup_progress_noop,
    dluma_player_startup_progress_noop,
    dluma_player_startup_progress_noop,
    dluma_player_startup_progress_noop,
    dluma_player_startup_progress_noop,
    dluma_player_startup_progress_noop,
    dluma_player_startup_progress_update
};
static DlumaPlayerStartupProgressIndicator g_dluma_player_startup_progress = {
    g_dluma_player_startup_progress_vtable
};

static void dluma_player_startup_reveal_or_fail(int initialization_result)
{
    HWND foreground = GetForegroundWindow();
    DWORD foreground_process_id = 0;
    DWORD process_id = GetCurrentProcessId();
    DlumaPlayerStartupRevealMode mode;
    if (foreground)
        (void)GetWindowThreadProcessId(
            foreground, &foreground_process_id);
    dluma_player_startup_lock();
    dluma_player_startup_core_finish(
        &g_dluma_player_startup.snapshot,
        initialization_result,
        process_id,
        foreground_process_id);
    mode = g_dluma_player_startup.snapshot.reveal_mode;
    dluma_player_startup_unlock();

    if (initialization_result == 0 && g_dluma_player_startup.game_window &&
        g_dluma_player_startup.original_show_window) {
        if (mode == DLUMA_PLAYER_STARTUP_REVEAL_FOREGROUND) {
            (void)g_dluma_player_startup.original_show_window(
                g_dluma_player_startup.game_window,
                g_dluma_player_startup.captured_show_command);
            (void)UpdateWindow(g_dluma_player_startup.game_window);
            if (g_dluma_player_startup.original_set_focus)
                (void)g_dluma_player_startup.original_set_focus(
                    g_dluma_player_startup.game_window);
        } else {
            (void)g_dluma_player_startup.original_show_window(
                g_dluma_player_startup.game_window,
                DLUMA_SW_SHOWNOACTIVATE);
            (void)UpdateWindow(g_dluma_player_startup.game_window);
        }
    }
    dluma_player_startup_close_splash();
}

static int __fastcall dluma_player_startup_initialize_hook(
    void *game,
    char *command_line,
    int show_command,
    HICON large_icon,
    HICON small_icon,
    DWORD unknown_a,
    DWORD unknown_b,
    void *progress_indicator)
{
    DlumaPlayerStartupInitializeFn original =
        g_dluma_player_startup.original_initialize;
    void *selected_reporter;
    int result;
    dluma_atomic_increment(&g_dluma_player_startup.active_calls);
    if (!original || dluma_atomic_compare_exchange(
            &g_dluma_player_startup.lifecycle,
            DLUMA_PLAYER_STARTUP_INITIALIZING,
            DLUMA_PLAYER_STARTUP_ARMED) != DLUMA_PLAYER_STARTUP_ARMED) {
        result = original ? original(
            game, command_line, show_command, large_icon, small_icon,
            unknown_a, unknown_b, progress_indicator) : -1;
        dluma_atomic_decrement(&g_dluma_player_startup.active_calls);
        return result;
    }
    if (!dluma_player_startup_install_engine_hooks()) {
        dluma_player_startup_fail_open_before_initialize(
            "engine_hook_install_failed");
        result = original(
            game, command_line, show_command, large_icon, small_icon,
            unknown_a, unknown_b, progress_indicator);
        dluma_atomic_decrement(&g_dluma_player_startup.active_calls);
        return result;
    }

    dluma_player_startup_set_status(
        dluma_player_startup_core_milestone(
            DLUMA_PLAYER_STARTUP_MILESTONE_GAME),
        0);
    selected_reporter = dluma_player_startup_core_select_reporter(
        progress_indicator, &g_dluma_player_startup_progress);
    result = original(
        game, command_line, show_command, large_icon, small_icon,
        unknown_a, unknown_b, selected_reporter);
    dluma_atomic_exchange(
        &g_dluma_player_startup.lifecycle, DLUMA_PLAYER_STARTUP_FINISHING);
    (void)dluma_player_startup_restore_engine_hooks();
    (void)dluma_player_startup_restore_player_hooks();
    dluma_player_startup_reveal_or_fail(result);
    dluma_atomic_exchange(
        &g_dluma_player_startup.lifecycle, DLUMA_PLAYER_STARTUP_COMPLETE);
    dluma_atomic_decrement(&g_dluma_player_startup.active_calls);
    return result;
}

static void __cdecl dluma_player_startup_hide_splash_hook(void)
{
    DlumaPlayerStartupHideSplashFn original =
        g_dluma_player_startup.original_hide_splash;
    LONG lifecycle;
    dluma_atomic_increment(&g_dluma_player_startup.active_calls);
    lifecycle = dluma_atomic_compare_exchange(
        &g_dluma_player_startup.lifecycle, 0, 0);
    if (lifecycle == DLUMA_PLAYER_STARTUP_ARMED ||
        lifecycle == DLUMA_PLAYER_STARTUP_INITIALIZING) {
        dluma_player_startup_lock();
        ++g_dluma_player_startup.snapshot.deferred_hide_count;
        dluma_player_startup_unlock();
        dluma_atomic_decrement(&g_dluma_player_startup.active_calls);
        return;
    }
    if (original) original();
    dluma_atomic_decrement(&g_dluma_player_startup.active_calls);
}

int dluma_player_startup_begin(void)
{
    if (dluma_atomic_compare_exchange(
            &g_dluma_player_startup.lifecycle,
            DLUMA_PLAYER_STARTUP_PREPARING,
            DLUMA_PLAYER_STARTUP_IDLE) != DLUMA_PLAYER_STARTUP_IDLE)
        return dluma_atomic_compare_exchange(
            &g_dluma_player_startup.lifecycle, 0, 0) ==
                DLUMA_PLAYER_STARTUP_ARMED;
    dluma_player_startup_core_initialize(
        &g_dluma_player_startup.snapshot);
    if (!dluma_player_startup_validate_profile()) {
        dluma_player_startup_mark_fail_open("player_profile_mismatch");
        dluma_atomic_exchange(
            &g_dluma_player_startup.lifecycle, DLUMA_PLAYER_STARTUP_FAIL_OPEN);
        return 0;
    }
    dluma_player_startup_lock();
    g_dluma_player_startup.snapshot.profile_matched = 1U;
    dluma_player_startup_unlock();
    if (!dluma_player_startup_resolve_slots()) {
        dluma_player_startup_mark_fail_open("exact_import_validation_failed");
        dluma_atomic_exchange(
            &g_dluma_player_startup.lifecycle, DLUMA_PLAYER_STARTUP_FAIL_OPEN);
        return 0;
    }
    if (!dluma_player_startup_bind_splash()) {
        dluma_player_startup_mark_fail_open("stock_splash_not_found");
        dluma_atomic_exchange(
            &g_dluma_player_startup.lifecycle, DLUMA_PLAYER_STARTUP_FAIL_OPEN);
        return 0;
    }
    if (!dluma_player_startup_install_player_hooks()) {
        (void)dluma_player_startup_restore_splash_subclass();
        dluma_player_startup_mark_fail_open("player_hook_install_failed");
        dluma_atomic_exchange(
            &g_dluma_player_startup.lifecycle, DLUMA_PLAYER_STARTUP_FAIL_OPEN);
        return 0;
    }
    dluma_atomic_exchange(
        &g_dluma_player_startup.lifecycle, DLUMA_PLAYER_STARTUP_ARMED);
    dluma_player_startup_set_status(
        dluma_player_startup_core_milestone(
            DLUMA_PLAYER_STARTUP_MILESTONE_WORKBENCH),
        0);
    dluma_log_line(
        "INFO",
        "Exact Player startup coordinator armed on the stock splash.");
    return 1;
}

void dluma_player_startup_status(const char *status)
{
    dluma_player_startup_set_status(status, 0);
}

void dluma_player_startup_abort(const char *reason)
{
    LONG lifecycle = dluma_atomic_compare_exchange(
        &g_dluma_player_startup.lifecycle, 0, 0);
    if (lifecycle == DLUMA_PLAYER_STARTUP_IDLE ||
        lifecycle == DLUMA_PLAYER_STARTUP_COMPLETE ||
        lifecycle == DLUMA_PLAYER_STARTUP_FAIL_OPEN)
        return;
    dluma_player_startup_fail_open_before_initialize(
        reason ? reason : "startup_aborted");
}

int dluma_player_startup_snapshot(
    DlumaPlayerStartupSnapshot *snapshot_out)
{
    if (!snapshot_out) return 0;
    dluma_player_startup_lock();
    *snapshot_out = g_dluma_player_startup.snapshot;
    dluma_player_startup_unlock();
    snapshot_out->active_call_count = (unsigned int)
        dluma_atomic_compare_exchange(
            &g_dluma_player_startup.active_calls, 0, 0);
    return 1;
}
