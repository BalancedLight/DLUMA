#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>
#include "dlw_editor_theme_controls.h"
#include "dlw_editor_theme_grid.h"
#include "dlw_editor_theme_scintilla.h"
#include "dlw_editor_theme_profile.h"
#include "dlw_editor_theme_properties.h"
#include "../build/generated/editor-theme/editor_theme_palette.generated.h"

extern int dluma_editor_is_main_window(HWND window, DWORD expected_process_id);

typedef enum ControlFamily {
    FAMILY_FRAME, FAMILY_DIALOG, FAMILY_EDIT, FAMILY_STATIC, FAMILY_BUTTON,
    FAMILY_COMBO, FAMILY_LIST, FAMILY_LISTVIEW, FAMILY_TREE, FAMILY_HEADER,
    FAMILY_TAB, FAMILY_TOOLBAR, FAMILY_TRACKBAR, FAMILY_TOOLTIP,
    FAMILY_STATUS, FAMILY_SCROLLBAR, FAMILY_SOURCE, FAMILY_WORKSPACE
} ControlFamily;

typedef struct ThemeResources {
    struct ThemeResources *next;
    DlumaEditorThemeResolved theme;
    unsigned int references;
    UINT dpi;
    int font_height;
    HBRUSH brushes[17];
    HFONT font;
    HFONT vertical_font, vertical_right_font;
    COLORREF stock_text;
    COLORREF stock_colors[7];
} ThemeResources;

typedef struct ThemedControl {
    ControlFamily family;
    ThemeResources *resources;
    ThemeResources *previous_resources;
    HFONT original_font;
    COLORREF original_background, original_text, original_text_background;
    LONG_PTR style;
    int hover;
    int hover_index;
    int font_owned;
    int colors_owned;
    int colors_dirty;
    BOOL original_dark_frame;
    int dark_frame_owned;
    BOOL applied_dark_frame;
    unsigned int references;
    int detached;
    int applying;
    int pending_detach;
    BOOL (WINAPI *image_draw)(HIMAGELIST, int, HDC, int, int, UINT);
    BOOL (WINAPI *image_size)(HIMAGELIST, int *, int *);
} ThemedControl;

static SRWLOCK resources_lock;
static ThemeResources *all_resources;
static volatile LONG control_count;
static const WCHAR control_property[] = L"DLUMA.EditorTheme.Control.1";
static const WCHAR detaching_property[] = L"DLUMA.EditorTheme.Control.Detaching.1";

typedef struct ThemeMenuItem {
    struct ThemeMenuItem *next;
    HWND owner;
    HMENU menu;
    UINT original_type;
    ULONG_PTR original_data;
    WCHAR *text;
    HMENU submenu;
    int menu_bar;
} ThemeMenuItem;
static ThemeMenuItem *menu_items;
static SRWLOCK menu_lock;
static COLORREF color(const ThemeResources *, unsigned int);
static int dip(const ThemeResources *, int);
static void glyph(HDC, RECT, COLORREF, int, int);
static void control_finish_detach(HWND, ThemedControl *);
static int ordinary_background(const ThemeResources *, COLORREF);
static int ordinary_foreground(const ThemeResources *, COLORREF);

typedef struct ChecklistScope {
    struct ChecklistScope *previous;
    COLORREF text, back, selected_text, selected_back;
} ChecklistScope;
typedef void *(__cdecl *ChecklistPermanentFn)(HWND);
typedef int (__fastcall *ChecklistIsKindOfFn)(void *, const void *);
static SRWLOCK checklist_lock;
static DWORD checklist_tls=TLS_OUT_OF_INDEXES, checklist_protection;
static PVOID volatile *checklist_slot;
static BYTE *checklist_module;
static void *checklist_class, *checklist_draw_item, *checklist_position;
static void *checklist_draw_thunk, *checklist_position_thunk;
static ChecklistPermanentFn checklist_permanent;
static ChecklistIsKindOfFn checklist_is_kind_of;
static DWORD (WINAPI *checklist_original_color)(int);
static int checklist_installed, checklist_pending, checklist_stopping;
static volatile LONG checklist_callbacks, checklist_frames, checklist_tls_failed;

static int checklist_memory(const void *pointer, size_t size)
{
    MEMORY_BASIC_INFORMATION info; uintptr_t p=(uintptr_t)pointer;
    return pointer && size && p<=UINTPTR_MAX-size && VirtualQuery(pointer,&info,sizeof(info)) &&
        info.State==MEM_COMMIT && !(info.Protect & (PAGE_GUARD|PAGE_NOACCESS)) &&
        p+size<=(uintptr_t)info.BaseAddress+info.RegionSize;
}
static ChecklistScope *checklist_raw(void)
{
    return checklist_tls==TLS_OUT_OF_INDEXES ? NULL : (ChecklistScope *)TlsGetValue(checklist_tls);
}
static DWORD WINAPI checklist_color(int index)
{
    uintptr_t site=(uintptr_t)_ReturnAddress()-(uintptr_t)checklist_module;
    ChecklistScope *scope=checklist_raw(); DWORD value;
    InterlockedIncrement(&checklist_callbacks);
    if (!InterlockedCompareExchange(&checklist_tls_failed,0,0) && scope && !((uintptr_t)scope&1U) &&
        ((index==COLOR_WINDOWTEXT && site==0x2781C9U) ||
         (index==COLOR_WINDOW && (site==0x2781EEU || site==0x27882DU || site==0x278673U)) ||
         (index==COLOR_HIGHLIGHTTEXT && site==0x278215U) ||
         (index==COLOR_HIGHLIGHT && (site==0x27822BU || site==0x278866U || site==0x2786AFU)))) {
        if (index==COLOR_WINDOWTEXT) value=scope->text;
        else if (index==COLOR_WINDOW) value=scope->back;
        else if (index==COLOR_HIGHLIGHTTEXT) value=scope->selected_text;
        else value=scope->selected_back;
    } else value=checklist_original_color(index);
    InterlockedDecrement(&checklist_callbacks);
    return value;
}
static int checklist_exchange(void *expected, void *replacement)
{
    DWORD old, ignored; void *observed;
    if (!checklist_slot || ((uintptr_t)checklist_slot & (sizeof(void *)-1U)) ||
        !VirtualProtect((void *)checklist_slot,sizeof(void *),PAGE_READWRITE,&old)) return 0;
    if (!checklist_pending) checklist_protection=old;
    checklist_pending=1;
    observed=InterlockedCompareExchangePointer(checklist_slot,replacement,expected);
    if (VirtualProtect((void *)checklist_slot,sizeof(void *),checklist_protection,&ignored)) checklist_pending=0;
    return observed==expected;
}
static int checklist_install(void)
{
    HMODULE mfc=GetModuleHandleW(L"mfc100.dll"), user=GetModuleHandleW(L"user32.dll");
    static const BYTE draw_thunk_bytes[]={0xFF,0x25,0xD0,0x59,0x4C,0x00};
    static const BYTE position_thunk_bytes[]={0xFF,0x25,0x24,0x5A,0x4C,0x00};
    void *permanent, *is_kind_of, **draw_iat, **position_iat; int ok=0;
    if (!mfc || !user) return 0;
    AcquireSRWLockExclusive(&checklist_lock);
    if (checklist_stopping) goto done;
    if (checklist_installed) { ok=!checklist_pending; goto done; }
    if (!dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_MFC) ||
        !dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR)) goto done;
    permanent=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,0x26B460U,1);
    is_kind_of=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,0x297190U,1);
    checklist_class=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,0x327ED0U,24);
    checklist_draw_item=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,0x278124U,1);
    checklist_position=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,0x278110U,1);
    checklist_draw_thunk=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,0xC8617AU,sizeof(draw_thunk_bytes));
    checklist_position_thunk=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,0xC86156U,sizeof(position_thunk_bytes));
    draw_iat=(void **)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,0x114BB50U,sizeof(void *));
    position_iat=(void **)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,0x114BB80U,sizeof(void *));
    checklist_slot=(PVOID volatile *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,0x2BF250U,sizeof(void *));
    if (!permanent || !is_kind_of || !checklist_class || !checklist_draw_item || !checklist_position || !checklist_slot ||
        !checklist_draw_thunk || !checklist_position_thunk || !draw_iat || !position_iat ||
        memcmp(checklist_draw_thunk,draw_thunk_bytes,sizeof(draw_thunk_bytes)) ||
        memcmp(checklist_position_thunk,position_thunk_bytes,sizeof(position_thunk_bytes)) ||
        *draw_iat!=checklist_draw_item || *position_iat!=checklist_position ||
        (void *)GetProcAddress(mfc,(LPCSTR)(ULONG_PTR)4194U)!=permanent ||
        (void *)GetProcAddress(mfc,(LPCSTR)(ULONG_PTR)7071U)!=is_kind_of ||
        (void *)GetProcAddress(mfc,(LPCSTR)(ULONG_PTR)3598U)!=checklist_draw_item ||
        (void *)GetProcAddress(mfc,(LPCSTR)(ULONG_PTR)9014U)!=checklist_position) goto done;
    if (checklist_tls==TLS_OUT_OF_INDEXES) checklist_tls=TlsAlloc();
    if (checklist_tls==TLS_OUT_OF_INDEXES) goto done;
    checklist_permanent=(ChecklistPermanentFn)permanent;
    checklist_is_kind_of=(ChecklistIsKindOfFn)is_kind_of;
    checklist_module=(BYTE *)mfc;
    checklist_original_color=(DWORD (WINAPI *)(int))(void *)GetProcAddress(user,"GetSysColor");
    if (!checklist_original_color) goto done;
    checklist_installed=checklist_exchange((void *)checklist_original_color,(void *)checklist_color);
    ok=checklist_installed && !checklist_pending;
done:
    ReleaseSRWLockExclusive(&checklist_lock);
    return ok;
}
void dluma_editor_theme_controls_stop(void)
{
    DWORD ignored;
    dluma_editor_theme_properties_stop();
    AcquireSRWLockExclusive(&checklist_lock);
    checklist_stopping=1;
    if (checklist_installed && checklist_exchange((void *)checklist_color,(void *)checklist_original_color)) checklist_installed=0;
    if (!checklist_installed && checklist_pending &&
        VirtualProtect((void *)checklist_slot,sizeof(void *),checklist_protection,&ignored)) checklist_pending=0;
    ReleaseSRWLockExclusive(&checklist_lock);
}
static int checklist_matches(HWND owner, const DRAWITEMSTRUCT *draw)
{
    ThemedControl *child; void *object; void **table; LONG_PTR style;
    if (!draw || draw->CtlType!=ODT_LISTBOX || !draw->hwndItem || !draw->hDC ||
        GetWindowThreadProcessId(owner,NULL)!=GetCurrentThreadId() ||
        GetWindowThreadProcessId(draw->hwndItem,NULL)!=GetCurrentThreadId() || GetParent(draw->hwndItem)!=owner) return 0;
    child=(ThemedControl *)GetPropW(draw->hwndItem,control_property);
    if (!child || child->detached || child->family!=FAMILY_LIST) return 0;
    style=GetWindowLongPtrW(draw->hwndItem,GWL_STYLE);
    if (!(style & LBS_HASSTRINGS) || !(style & (LBS_OWNERDRAWFIXED|LBS_OWNERDRAWVARIABLE)) ||
        (style & LBS_NODATA) || !checklist_install()) return 0;
    object=checklist_permanent(draw->hwndItem);
    if (!checklist_memory(object,72U) || *(HWND *)((BYTE *)object+64U)!=draw->hwndItem) return 0;
    table=*(void ***)object;
    if (!checklist_memory(table,98U*sizeof(void *)) ||
        (table[91]!=checklist_draw_item && table[91]!=checklist_draw_thunk) ||
        (table[97]!=checklist_position && table[97]!=checklist_position_thunk) ||
        !checklist_is_kind_of(object,checklist_class)) return 0;
    return GetPropW(draw->hwndItem,control_property)==child && !child->detached;
}
static void checklist_scope_after(uint64_t *scratch);
static void checklist_scope_before(HWND owner, LPARAM lparam, ThemeResources *r, uint64_t *scratch)
{
    const DRAWITEMSTRUCT *draw=(const DRAWITEMSTRUCT *)lparam;
    ChecklistScope *scope, *prior; int active;
    if (!scratch || *scratch) return;
    if (r && !InterlockedCompareExchange(&checklist_tls_failed,0,0) && checklist_matches(owner,draw)) {
        scope=(ChecklistScope *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*scope));
        if (scope) {
            active=GetFocus()==draw->hwndItem;
            scope->text=color(r,DLUMA_EDITOR_THEME_ROLE_CONTROL_TEXT);
            scope->back=color(r,DLUMA_EDITOR_THEME_ROLE_SURFACE);
            scope->selected_back=color(r,active ? DLUMA_EDITOR_THEME_ROLE_SELECTION : DLUMA_EDITOR_THEME_ROLE_INACTIVE_SELECTION);
            scope->selected_text=color(r,active || r->theme.kind==DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST ?
                DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT);
            scope->previous=checklist_raw();
            if (TlsSetValue(checklist_tls,scope)) {
                InterlockedIncrement(&checklist_frames); *scratch=(uint64_t)(uintptr_t)scope;
                if (!dluma_editor_ui_invocation_cleanup(DLUMA_EDITOR_UI_OWNER_THEME,
                    DLUMA_EDITOR_UI_SLOT_CONTROLS,checklist_scope_after)) checklist_scope_after(scratch);
                return;
            }
            HeapFree(GetProcessHeap(),0,scope);
        }
    }
    prior=checklist_raw();
    if (prior) {
        if (TlsSetValue(checklist_tls,(void *)((uintptr_t)prior|1U))) {
            InterlockedIncrement(&checklist_frames); *scratch=(uint64_t)((uintptr_t)prior|2U);
            if (!dluma_editor_ui_invocation_cleanup(DLUMA_EDITOR_UI_OWNER_THEME,
                DLUMA_EDITOR_UI_SLOT_CONTROLS,checklist_scope_after)) checklist_scope_after(scratch);
        } else InterlockedIncrement(&checklist_tls_failed);
    }
}
static void checklist_scope_after(uint64_t *scratch)
{
    ChecklistScope *scope; uintptr_t prior;
    if (!scratch || !*scratch) return;
    if (*scratch & 2U) {
        prior=(uintptr_t)(*scratch & ~(uint64_t)2U); *scratch=0;
        if (!TlsSetValue(checklist_tls,(void *)prior)) { InterlockedIncrement(&checklist_tls_failed); return; }
    } else {
        scope=(ChecklistScope *)(uintptr_t)*scratch; *scratch=0;
        if (!TlsSetValue(checklist_tls,scope->previous)) { InterlockedIncrement(&checklist_tls_failed); return; }
        HeapFree(GetProcessHeap(),0,scope);
    }
    InterlockedDecrement(&checklist_frames);
}

static ThemeMenuItem *menu_find(ULONG_PTR data)
{
    ThemeMenuItem *item;
    AcquireSRWLockShared(&menu_lock);
    for (item = menu_items; item; item = item->next)
        if ((ULONG_PTR)item == data) break;
    ReleaseSRWLockShared(&menu_lock);
    return item;
}

static void menu_release(HWND owner, HMENU menu)
{
    for (;;) {
        ThemeMenuItem **link, *item = NULL;
        int i, count;
        AcquireSRWLockExclusive(&menu_lock);
        for (link = &menu_items; *link; link = &(*link)->next)
            if ((*link)->owner == owner && (!menu || (*link)->menu == menu)) {
                item = *link; *link = item->next; break;
            }
        ReleaseSRWLockExclusive(&menu_lock);
        if (!item) break;
        count = GetMenuItemCount(item->menu);
        for (i = 0; i < count; ++i) {
            MENUITEMINFOW info;
            ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
            info.fMask = MIIM_DATA;
            if (GetMenuItemInfoW(item->menu, (UINT)i, TRUE, &info) &&
                info.dwItemData == (ULONG_PTR)item) {
                info.fMask = MIIM_DATA | MIIM_FTYPE;
                info.dwItemData = item->original_data;
                info.fType = item->original_type;
                SetMenuItemInfoW(item->menu, (UINT)i, TRUE, &info);
                break;
            }
        }
        if (item->text) HeapFree(GetProcessHeap(), 0, item->text);
        HeapFree(GetProcessHeap(), 0, item);
    }
}

static void menu_prepare(HWND owner, HMENU menu, int menu_bar)
{
    int i, count = GetMenuItemCount(menu);
    if (count < 0 || count > 4096) return;
    for (i = 0; i < count; ++i) {
        MENUITEMINFOW info;
        ThemeMenuItem *item;
        ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STRING | MIIM_BITMAP | MIIM_SUBMENU;
        if (!GetMenuItemInfoW(menu, (UINT)i, TRUE, &info) ||
            info.fType & (MFT_OWNERDRAW | MFT_BITMAP) || info.hbmpItem) continue;
        item = (ThemeMenuItem *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*item));
        if (!item) continue;
        item->owner = owner; item->menu = menu;
        item->submenu = info.hSubMenu; item->menu_bar = menu_bar;
        item->original_type = info.fType; item->original_data = info.dwItemData;
        if (!(info.fType & MFT_SEPARATOR)) {
            if (info.cch > 32767U) { HeapFree(GetProcessHeap(), 0, item); continue; }
            item->text = (WCHAR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           ((SIZE_T)info.cch + 1U) * sizeof(WCHAR));
            if (!item->text) { HeapFree(GetProcessHeap(), 0, item); continue; }
            info.dwTypeData = item->text; ++info.cch;
            if (!GetMenuItemInfoW(menu, (UINT)i, TRUE, &info)) {
                HeapFree(GetProcessHeap(), 0, item->text); HeapFree(GetProcessHeap(), 0, item); continue;
            }
        }
        info.fMask = MIIM_FTYPE | MIIM_DATA;
        info.fType |= MFT_OWNERDRAW; info.dwItemData = (ULONG_PTR)item;
        AcquireSRWLockExclusive(&menu_lock);
        item->next = menu_items; menu_items = item;
        ReleaseSRWLockExclusive(&menu_lock);
        if (!SetMenuItemInfoW(menu, (UINT)i, TRUE, &info)) menu_release(owner, menu);
    }
}

static int menu_measure(HWND owner, ThemeResources *r, MEASUREITEMSTRUCT *measure)
{
    ThemeMenuItem *item;
    HDC dc;
    HFONT previous;
    RECT rect = {0, 0, 0, 0};
    if (!measure || measure->CtlType != ODT_MENU ||
        !(item = menu_find(measure->itemData)) || item->owner != owner) return 0;
    if (item->original_type & MFT_SEPARATOR) {
        measure->itemHeight = (UINT)dip(r, 7); measure->itemWidth = 1; return 1;
    }
    dc = GetDC(owner); previous = (HFONT)SelectObject(dc, r->font);
    DrawTextW(dc, item->text, -1, &rect, DT_CALCRECT | DT_SINGLELINE | DT_EXPANDTABS);
    measure->itemHeight = (UINT)(rect.bottom + dip(r, 8));
    measure->itemWidth = (UINT)(rect.right + dip(r, item->menu_bar ? 16 : 54));
    SelectObject(dc, previous); ReleaseDC(owner, dc);
    return 1;
}

static int menu_draw(HWND owner, ThemeResources *r, DRAWITEMSTRUCT *draw)
{
    ThemeMenuItem *item;
    RECT text_rect;
    HFONT previous;
    UINT foreground, background;
    const WCHAR *shortcut;
    UINT flags;
    if (!draw || draw->CtlType != ODT_MENU ||
        !(item = menu_find(draw->itemData)) || item->owner != owner) return 0;
    background = draw->itemState & ODS_SELECTED ? DLUMA_EDITOR_THEME_ROLE_SELECTION : DLUMA_EDITOR_THEME_ROLE_PANEL;
    foreground = draw->itemState & (ODS_DISABLED | ODS_GRAYED) ? DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT :
        draw->itemState & ODS_SELECTED ? DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_CONTROL_TEXT;
    FillRect(draw->hDC, &draw->rcItem, r->brushes[background]);
    if (item->original_type & MFT_SEPARATOR) {
        RECT separator = draw->rcItem;
        separator.top = (separator.top + separator.bottom) / 2;
        separator.bottom = separator.top + dip(r, 1); separator.left += dip(r, 24);
        FillRect(draw->hDC, &separator, r->brushes[DLUMA_EDITOR_THEME_ROLE_BORDER]); return 1;
    }
    text_rect = draw->rcItem; text_rect.left += dip(r, item->menu_bar ? 8 : 26); text_rect.right -= dip(r, item->menu_bar ? 8 : 24);
    previous = (HFONT)SelectObject(draw->hDC, r->font);
    SetTextColor(draw->hDC, color(r, foreground)); SetBkMode(draw->hDC, TRANSPARENT);
    shortcut = item->text;
    while (*shortcut && *shortcut != L'\t') ++shortcut;
    flags = DT_SINGLELINE | DT_VCENTER;
    if (draw->itemState & ODS_NOACCEL) flags |= DT_HIDEPREFIX;
    DrawTextW(draw->hDC, item->text, (int)(shortcut - item->text), &text_rect, flags);
    if (*shortcut) DrawTextW(draw->hDC, shortcut + 1, -1, &text_rect, flags | DT_RIGHT);
    if (draw->itemState & ODS_CHECKED) {
        RECT mark = draw->rcItem;
        mark.left += dip(r, 5); mark.right = mark.left + dip(r, 13);
        mark.top += (mark.bottom - mark.top - dip(r, 13)) / 2; mark.bottom = mark.top + dip(r, 13);
        glyph(draw->hDC, mark, color(r, foreground), (item->original_type & MFT_RADIOCHECK) != 0, 1);
    }
    if (item->submenu && !item->menu_bar) {
        HPEN pen = CreatePen(PS_SOLID, 1, color(r, foreground));
        if (pen) {
            HGDIOBJ old_pen = SelectObject(draw->hDC, pen);
            int x = draw->rcItem.right - dip(r, 11);
            int y = (draw->rcItem.top + draw->rcItem.bottom) / 2;
            MoveToEx(draw->hDC, x - dip(r, 2), y - dip(r, 4), NULL);
            LineTo(draw->hDC, x + dip(r, 2), y);
            LineTo(draw->hDC, x - dip(r, 2), y + dip(r, 4));
            SelectObject(draw->hDC, old_pen); DeleteObject(pen);
        }
    }
    SelectObject(draw->hDC, previous);
    return 1;
}

static COLORREF color(const ThemeResources *resources, unsigned int role)
{
    uint32_t argb = resources->theme.argb[role];
    return RGB((argb >> 16) & 255U, (argb >> 8) & 255U, argb & 255U);
}

static int dip(const ThemeResources *resources, int value)
{
    return MulDiv(value, (int)resources->dpi, 96);
}

static UINT window_dpi(HWND window)
{
    typedef UINT (WINAPI *GetDpiFn)(HWND);
    HMODULE user = GetModuleHandleW(L"user32.dll");
    GetDpiFn get_dpi = user ? (GetDpiFn)GetProcAddress(user, "GetDpiForWindow") : NULL;
    UINT dpi = get_dpi ? get_dpi(window) : 0;
    HDC dc;
    if (dpi) return dpi;
    dc = GetDC(window);
    dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(window, dc);
    return dpi ? dpi : 96;
}

static int font_height_for_dpi(UINT dpi)
{
    typedef BOOL (WINAPI *SpiForDpiFn)(UINT, UINT, PVOID, UINT, UINT);
    HMODULE user = GetModuleHandleW(L"user32.dll");
    SpiForDpiFn query = user ? (SpiForDpiFn)(void *)GetProcAddress(user, "SystemParametersInfoForDpi") : NULL;
    NONCLIENTMETRICSW metrics;
    int height = MulDiv(9, (int)dpi, 72);
    ZeroMemory(&metrics, sizeof(metrics)); metrics.cbSize = sizeof(metrics);
    if ((query && query(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)) ||
        (!query && SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))) {
        int system_height = metrics.lfMessageFont.lfHeight;
        if (system_height < 0) system_height = -system_height;
        if (system_height > height) height = system_height;
    }
    return height;
}

static void resources_retain(ThemeResources *resources)
{
    if (!resources) return;
    AcquireSRWLockExclusive(&resources_lock);
    ++resources->references;
    ReleaseSRWLockExclusive(&resources_lock);
}

static ThemeResources *resources_acquire(HWND window,
                                         const DlumaEditorThemeResolved *theme)
{
    ThemeResources *resources;
    UINT dpi = window_dpi(window);
    unsigned int i;
    int font_height = font_height_for_dpi(dpi);
    AcquireSRWLockExclusive(&resources_lock);
    for (resources = all_resources; resources; resources = resources->next)
        if (resources->theme.generation == theme->generation && resources->dpi == dpi && resources->font_height == font_height)
            break;
    if (resources) ++resources->references;
    ReleaseSRWLockExclusive(&resources_lock);
    if (resources) return resources;
    resources = (ThemeResources *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                           sizeof(*resources));
    if (!resources) return NULL;
    resources->theme = *theme; resources->dpi = dpi; resources->font_height = font_height; resources->references = 1;
    resources->stock_text = GetSysColor(COLOR_WINDOWTEXT);
    {
        static const int indices[7] = {COLOR_WINDOW, COLOR_BTNFACE, COLOR_WINDOWTEXT,
            COLOR_BTNTEXT, COLOR_GRAYTEXT, COLOR_INFOBK, COLOR_INFOTEXT};
        for (i = 0; i < 7U; ++i) resources->stock_colors[i] = GetSysColor(indices[i]);
    }
    for (i = 0; i < 17; ++i) {
        resources->brushes[i] = CreateSolidBrush(color(resources, i));
        if (!resources->brushes[i]) break;
    }
    resources->font = CreateFontW(-font_height, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    resources->vertical_font = CreateFontW(-font_height, 0, 900, 900, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    resources->vertical_right_font = CreateFontW(-font_height, 0, 2700, 2700, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (i != 17 || !resources->font || !resources->vertical_font || !resources->vertical_right_font) {
        while (i) DeleteObject(resources->brushes[--i]);
        if (resources->font) DeleteObject(resources->font);
        if (resources->vertical_font) DeleteObject(resources->vertical_font);
        if (resources->vertical_right_font) DeleteObject(resources->vertical_right_font);
        HeapFree(GetProcessHeap(), 0, resources);
        return NULL;
    }
    AcquireSRWLockExclusive(&resources_lock);
    resources->next = all_resources; all_resources = resources;
    ReleaseSRWLockExclusive(&resources_lock);
    return resources;
}

static void resources_release(ThemeResources *resources)
{
    ThemeResources **link;
    unsigned int i;
    int dispose = 0;
    if (!resources) return;
    AcquireSRWLockExclusive(&resources_lock);
    if (!--resources->references) {
        for (link = &all_resources; *link; link = &(*link)->next)
            if (*link == resources) { *link = resources->next; break; }
        dispose = 1;
    }
    ReleaseSRWLockExclusive(&resources_lock);
    if (!dispose) return;
    for (i = 0; i < 17; ++i) DeleteObject(resources->brushes[i]);
    DeleteObject(resources->font);
    DeleteObject(resources->vertical_font);
    DeleteObject(resources->vertical_right_font);
    HeapFree(GetProcessHeap(), 0, resources);
}

static void control_release(ThemedControl *control)
{
    if (!control || --control->references) return;
    resources_release(control->resources);
    HeapFree(GetProcessHeap(), 0, control);
    InterlockedDecrement(&control_count);
}
void *dluma_editor_theme_controls_hold_listview(HWND window)
{
    ThemedControl *control=(ThemedControl *)GetPropW(window,control_property);
    if (!control || control->detached || control->family!=FAMILY_LISTVIEW ||
        GetWindowThreadProcessId(window,NULL)!=GetCurrentThreadId()) return NULL;
    ++control->references;
    return control;
}
void *dluma_editor_theme_controls_hold_button(HWND window)
{
    ThemedControl *control=(ThemedControl *)GetPropW(window,control_property);
    if (!control || control->detached || control->family!=FAMILY_BUTTON ||
        GetWindowThreadProcessId(window,NULL)!=GetCurrentThreadId()) return NULL;
    ++control->references;
    return control;
}
int dluma_editor_theme_controls_listview_alive(HWND window, void *token)
{
    ThemedControl *control=(ThemedControl *)token;
    return control && GetWindowThreadProcessId(window,NULL)==GetCurrentThreadId() &&
        GetPropW(window,control_property)==control && !control->detached;
}
void dluma_editor_theme_controls_release_listview(void *token)
{
    control_release((ThemedControl *)token);
}

static int family_for_window(HWND window, ControlFamily *family)
{
    WCHAR name[128];
    if (!GetClassNameW(window, name, 128)) return 0;
    if (!lstrcmpiW(name, L"#32770")) *family = FAMILY_DIALOG;
    else if (!lstrcmpiW(name, L"Edit")) *family = FAMILY_EDIT;
    else if (!lstrcmpiW(name, L"Static")) *family = FAMILY_STATIC;
    else if (!lstrcmpiW(name, L"Button")) *family = FAMILY_BUTTON;
    else if (!lstrcmpiW(name, L"ComboBox") || !lstrcmpiW(name, L"ComboBoxEx32")) *family = FAMILY_COMBO;
    else if (!lstrcmpiW(name, L"ListBox") || !lstrcmpiW(name, L"ComboLBox")) *family = FAMILY_LIST;
    else if (!lstrcmpiW(name, WC_LISTVIEWW)) *family = FAMILY_LISTVIEW;
    else if (!lstrcmpiW(name, WC_TREEVIEWW)) *family = FAMILY_TREE;
    else if (!lstrcmpiW(name, WC_HEADERW)) *family = FAMILY_HEADER;
    else if (!lstrcmpiW(name, WC_TABCONTROLW)) *family = FAMILY_TAB;
    else if (!lstrcmpiW(name, TOOLBARCLASSNAMEW)) *family = FAMILY_TOOLBAR;
    else if (!lstrcmpiW(name, TRACKBAR_CLASSW)) *family = FAMILY_TRACKBAR;
    else if (!lstrcmpiW(name, TOOLTIPS_CLASSW)) *family = FAMILY_TOOLTIP;
    else if (!lstrcmpiW(name, STATUSCLASSNAMEW)) *family = FAMILY_STATUS;
    else if (!lstrcmpiW(name, L"ScrollBar")) *family = FAMILY_SCROLLBAR;
    else if (!lstrcmpiW(name, L"Scintilla") && GetDlgCtrlID(window)==3581 &&
             dluma_editor_theme_scintilla_owns_parent(GetParent(window)))
        *family = FAMILY_SOURCE;
    else if (!lstrcmpiW(name,L"AfxFrameOrView100") && GetDlgCtrlID(window)==59648 &&
             (GetWindowLongPtrW(window,GWL_STYLE) & WS_CLIPCHILDREN) &&
             dluma_editor_is_main_window(GetParent(window),GetCurrentProcessId()))
        *family = FAMILY_WORKSPACE;
    else if (!(GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD) &&
             (GetWindowLongPtrW(window, GWL_STYLE) & WS_CAPTION))
        *family = FAMILY_FRAME;
    else return 0;
    return 1;
}

int dluma_editor_theme_controls_attach(HWND window)
{
    ThemedControl *control;
    ControlFamily family;
    DWORD process_id = 0;
    if (GetPropW(window, detaching_property)) return 0;
    if (GetPropW(window, control_property)) return 1;
    if (GetWindowThreadProcessId(window, &process_id) != GetCurrentThreadId() ||
        process_id != GetCurrentProcessId() || !family_for_window(window, &family))
        return 0;
    control = (ThemedControl *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                        sizeof(*control));
    if (!control) return 0;
    control->family = family;
    control->hover_index = -1;
    control->references = 1;
    control->style = GetWindowLongPtrW(window, GWL_STYLE);
    control->original_font = (HFONT)SendMessageW(window, WM_GETFONT, 0, 0);
    if (family == FAMILY_TAB || family == FAMILY_HEADER) {
        HMODULE common = GetModuleHandleW(L"comctl32.dll");
        if (common) {
            control->image_draw = (BOOL (WINAPI *)(HIMAGELIST, int, HDC, int, int, UINT))(void *)GetProcAddress(common, "ImageList_Draw");
            control->image_size = (BOOL (WINAPI *)(HIMAGELIST, int *, int *))(void *)GetProcAddress(common, "ImageList_GetIconSize");
        }
    }
    if (family == FAMILY_LISTVIEW) {
        control->original_background = ListView_GetBkColor(window);
        control->original_text = ListView_GetTextColor(window);
        control->original_text_background = ListView_GetTextBkColor(window);
    } else if (family == FAMILY_TREE) {
        control->original_background = TreeView_GetBkColor(window);
        control->original_text = TreeView_GetTextColor(window);
    } else if (family == FAMILY_TOOLTIP) {
        control->original_background = (COLORREF)SendMessageW(window, TTM_GETTIPBKCOLOR, 0, 0);
        control->original_text = (COLORREF)SendMessageW(window, TTM_GETTIPTEXTCOLOR, 0, 0);
    }
    if (!SetPropW(window, control_property, control)) {
        HeapFree(GetProcessHeap(), 0, control); return 0;
    }
    InterlockedIncrement(&control_count);
    return 1;
}

static void frame_color(HWND window, ThemedControl *control, int restore)
{
    typedef HRESULT (WINAPI *DwmGetFn)(HWND, DWORD, PVOID, DWORD);
    typedef HRESULT (WINAPI *DwmSetFn)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    DwmGetFn get;
    DwmSetFn set;
    BOOL dark;
    if (!dwm) return;
    get = (DwmGetFn)GetProcAddress(dwm, "DwmGetWindowAttribute");
    set = (DwmSetFn)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!restore && !control->dark_frame_owned && get &&
        SUCCEEDED(get(window, DWMWA_USE_IMMERSIVE_DARK_MODE,
                      &control->original_dark_frame, sizeof(BOOL))))
        control->dark_frame_owned = 1;
    dark = restore ? control->original_dark_frame :
        control->resources->theme.kind == DLUMA_EDITOR_THEME_KIND_DARK;
    if (set && control->dark_frame_owned) {
        BOOL current;
        if (!restore || (get && SUCCEEDED(get(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &current, sizeof(current))) && current == control->applied_dark_frame)) {
            if (SUCCEEDED(set(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)))) control->applied_dark_frame = dark;
        }
    }
    FreeLibrary(dwm);
}

void dluma_editor_theme_controls_apply(HWND window,
                                       const DlumaEditorThemeResolved *theme)
{
    ThemedControl *control = (ThemedControl *)GetPropW(window, control_property);
    ThemeResources *previous, *resources;
    HFONT current_font;
    if (!control || !theme || GetWindowThreadProcessId(window, NULL) != GetCurrentThreadId()) return;
    if (control->applying) { (void)dluma_editor_ui_request_refresh(); return; }
    if (!control->colors_dirty && control->resources && control->resources->theme.generation == theme->generation) {
        UINT dpi = window_dpi(window);
        if (control->resources->dpi == dpi && control->resources->font_height == font_height_for_dpi(dpi)) return;
    }
    ++control->references;
    control->applying = 1;
    control->colors_dirty = 0;
    resources = resources_acquire(window, theme);
    if (!resources) { control->colors_dirty=1; control->applying = 0; control_release(control); return; }
    previous = control->resources; control->previous_resources = previous; control->resources = resources;
    if (control->family != FAMILY_FRAME && control->family != FAMILY_SCROLLBAR &&
        control->family != FAMILY_SOURCE && control->family != FAMILY_WORKSPACE &&
        !(control->family == FAMILY_TAB && (control->style & TCS_VERTICAL))) {
        current_font = (HFONT)SendMessageW(window, WM_GETFONT, 0, 0);
        if (!control->detached && (current_font == control->original_font || (control->font_owned && previous && current_font == previous->font))) {
            control->font_owned = 1;
            (void)SendMessageW(window, WM_SETFONT, (WPARAM)resources->font, FALSE);
        } else control->font_owned = 0;
    }
    if (!control->detached && control->family == FAMILY_LISTVIEW) {
        COLORREF current=ListView_GetBkColor(window);
        if (!control->detached && (ordinary_background(resources,current) || (previous && current==color(previous,DLUMA_EDITOR_THEME_ROLE_SURFACE))))
            ListView_SetBkColor(window, color(resources, DLUMA_EDITOR_THEME_ROLE_SURFACE));
        current=control->detached ? CLR_NONE : ListView_GetTextBkColor(window);
        if (!control->detached && (ordinary_background(resources,current) || (previous && current==color(previous,DLUMA_EDITOR_THEME_ROLE_SURFACE))))
            ListView_SetTextBkColor(window, color(resources, DLUMA_EDITOR_THEME_ROLE_SURFACE));
        current=control->detached ? CLR_NONE : ListView_GetTextColor(window);
        if (!control->detached && (ordinary_foreground(resources,current) || (previous && current==color(previous,DLUMA_EDITOR_THEME_ROLE_TEXT))))
            ListView_SetTextColor(window, color(resources, DLUMA_EDITOR_THEME_ROLE_TEXT));
        control->colors_owned = 1;
    } else if (!control->detached && control->family == FAMILY_TREE) {
        COLORREF current=TreeView_GetBkColor(window);
        if (!control->detached && (current==CLR_NONE || ordinary_background(resources,current) || (previous && current==color(previous,DLUMA_EDITOR_THEME_ROLE_SURFACE))))
            TreeView_SetBkColor(window, color(resources, DLUMA_EDITOR_THEME_ROLE_SURFACE));
        current=control->detached ? CLR_NONE : TreeView_GetTextColor(window);
        if (!control->detached && (current==CLR_NONE || ordinary_foreground(resources,current) || (previous && current==color(previous,DLUMA_EDITOR_THEME_ROLE_TEXT))))
            TreeView_SetTextColor(window, color(resources, DLUMA_EDITOR_THEME_ROLE_TEXT));
        control->colors_owned = 1;
    } else if (!control->detached && control->family == FAMILY_TOOLTIP) {
        COLORREF old_back = (COLORREF)SendMessageW(window, TTM_GETTIPBKCOLOR, 0, 0);
        COLORREF old_text = (COLORREF)SendMessageW(window, TTM_GETTIPTEXTCOLOR, 0, 0);
        if (!control->detached && (old_back == control->original_background || (previous && old_back == color(previous, DLUMA_EDITOR_THEME_ROLE_PANEL))))
            (void)SendMessageW(window, TTM_SETTIPBKCOLOR, color(resources, DLUMA_EDITOR_THEME_ROLE_PANEL), 0);
        if (!control->detached && (old_text == control->original_text || (previous && old_text == color(previous, DLUMA_EDITOR_THEME_ROLE_TEXT))))
            (void)SendMessageW(window, TTM_SETTIPTEXTCOLOR, color(resources, DLUMA_EDITOR_THEME_ROLE_TEXT), 0);
        control->colors_owned = 1;
    }
    if (!control->detached && !(control->style & WS_CHILD)) frame_color(window, control, 0);
    if (!control->detached && !(control->style & WS_CHILD) && GetMenu(window)) {
        menu_prepare(window, GetMenu(window), 1);
        (void)DrawMenuBar(window);
    }
    if (!control->detached) (void)RedrawWindow(window, NULL, NULL,
        RDW_INVALIDATE | RDW_FRAME | (control->family==FAMILY_WORKSPACE ?
            RDW_ERASE | RDW_NOCHILDREN : RDW_ALLCHILDREN));
    control->applying = 0;
    if (control->pending_detach) {
        control->pending_detach = 0;
        if (IsWindow(window)) control_finish_detach(window, control);
        else control_release(control);
    }
    control->previous_resources = NULL;
    resources_release(previous);
    control_release(control);
}

static void outline(HDC dc, const RECT *rect, COLORREF foreground, int width)
{
    HPEN pen = CreatePen(PS_SOLID, width, foreground);
    HGDIOBJ old_pen, old_brush;
    if (!pen) return;
    old_pen = SelectObject(dc, pen);
    old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(dc, rect->left, rect->top, rect->right, rect->bottom);
    SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(pen);
}

static void glyph(HDC dc, RECT rect, COLORREF foreground, int radio, int checked)
{
    HPEN pen = CreatePen(PS_SOLID, 1, foreground);
    HGDIOBJ old_pen, old_brush;
    if (!pen) return;
    old_pen = SelectObject(dc, pen);
    old_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    if (radio) Ellipse(dc, rect.left, rect.top, rect.right, rect.bottom);
    else Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    if (checked) {
        InflateRect(&rect, -3, -3);
        if (radio) {
            HBRUSH brush = CreateSolidBrush(foreground);
            SelectObject(dc, brush); Ellipse(dc, rect.left, rect.top, rect.right, rect.bottom);
            SelectObject(dc, old_brush); DeleteObject(brush);
        } else if (checked == 2) {
            MoveToEx(dc, rect.left, (rect.top + rect.bottom) / 2, NULL);
            LineTo(dc, rect.right, (rect.top + rect.bottom) / 2);
        } else {
            MoveToEx(dc, rect.left, (rect.top + rect.bottom) / 2, NULL);
            LineTo(dc, (rect.left + rect.right) / 2, rect.bottom);
            LineTo(dc, rect.right, rect.top);
        }
    }
    SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(pen);
}

static int exclude_overlapping_windows(HWND window, HDC dc);

static void restore_paint_damage(HWND window, ThemedControl *control, PAINTSTRUCT *paint)
{
    RECT damaged = paint->rcPaint;
    EndPaint(window, paint);
    if (IsWindow(window) && GetPropW(window, control_property) == control && !control->detached &&
        !IsRectEmpty(&damaged)) (void)InvalidateRect(window, &damaged, FALSE);
}

static int paint_button(HWND window, ThemedControl *control)
{
    ThemeResources *r = control->resources;
    UINT type = (UINT)(control->style & BS_TYPEMASK);
    PAINTSTRUCT paint;
    HDC dc;
    RECT rect, text_rect;
    WCHAR text[1024];
    LRESULT state;
    unsigned int background;
    COLORREF foreground;
    HFONT old_font;
    UINT flags;
    int saved = 0;
    if (type == BS_OWNERDRAW || type == BS_USERBUTTON ||
        (control->style & (BS_ICON | BS_BITMAP))) return 0;
    ZeroMemory(&paint, sizeof(paint));
    dc = BeginPaint(window, &paint);
    if (!dc) { restore_paint_damage(window, control, &paint); return 0; }
    if (type == BS_GROUPBOX && (!(saved = SaveDC(dc)) ||
        !exclude_overlapping_windows(window, dc))) {
        if (saved) RestoreDC(dc, saved);
        restore_paint_damage(window, control, &paint);
        return 0;
    }
    GetClientRect(window, &rect); text_rect = rect;
    state = SendMessageW(window, BM_GETSTATE, 0, 0);
    background = state & BST_PUSHED ? DLUMA_EDITOR_THEME_ROLE_SELECTION :
        control->hover ? DLUMA_EDITOR_THEME_ROLE_HOVER : DLUMA_EDITOR_THEME_ROLE_PANEL;
    foreground = color(r, !IsWindowEnabled(window) ? DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT :
        state & BST_PUSHED ? DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_CONTROL_TEXT);
    if (type == BS_GROUPBOX) {
        /* Grouped controls are siblings layered above the frame. */
        FillRect(dc, &rect, r->brushes[DLUMA_EDITOR_THEME_ROLE_WINDOW]);
    } else FillRect(dc, &rect, r->brushes[background]);
    old_font = (HFONT)SelectObject(dc, r->font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, foreground);
    GetWindowTextW(window, text, 1024);
    flags = DT_VCENTER | DT_SINGLELINE;
    if (control->style & BS_MULTILINE) flags = DT_WORDBREAK;
    if ((SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEACCEL) != 0) flags |= DT_HIDEPREFIX;
    if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE ||
        type == BS_AUTO3STATE || type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON) {
        RECT mark = rect;
        int size = dip(r, 13);
        if (control->style & BS_RIGHTBUTTON) { mark.right -= dip(r, 2); mark.left = mark.right - size; }
        else { mark.left += dip(r, 2); mark.right = mark.left + size; }
        mark.top = (rect.bottom - size) / 2; mark.bottom = mark.top + size;
        glyph(dc, mark, foreground, type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON,
               (int)SendMessageW(window, BM_GETCHECK, 0, 0));
        if (control->style & BS_RIGHTBUTTON) text_rect.right = mark.left - dip(r, 5);
        else text_rect.left = mark.right + dip(r, 5);
    } else if (type == BS_GROUPBOX) {
        RECT border = rect;
        border.top += dip(r, 7);
        outline(dc, &border, color(r, DLUMA_EDITOR_THEME_ROLE_BORDER), dip(r, 1));
        text_rect.left += dip(r, 8); text_rect.bottom = text_rect.top + dip(r, 18);
        flags = DT_LEFT | DT_SINGLELINE;
    } else {
        outline(dc, &rect, color(r, (state & BST_FOCUS) || type == BS_DEFPUSHBUTTON ?
            DLUMA_EDITOR_THEME_ROLE_ACCENT : DLUMA_EDITOR_THEME_ROLE_BORDER), dip(r, 1));
        flags |= DT_CENTER; InflateRect(&text_rect, -dip(r, 4), -dip(r, 2));
    }
    (void)DrawTextW(dc, text, -1, &text_rect, flags);
    if ((state & BST_FOCUS) && !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        RECT focus = rect; InflateRect(&focus, -dip(r, 3), -dip(r, 3));
        DrawFocusRect(dc, &focus);
    }
    SelectObject(dc, old_font);
    if (saved) RestoreDC(dc, saved);
    EndPaint(window, &paint);
    return 1;
}

static int exclude_window_rect(HWND window, HWND overlap, HDC dc)
{
    RECT bounds;
    if (!(GetWindowLongPtrW(overlap, GWL_STYLE) & WS_VISIBLE)) return 1;
    if (!GetWindowRect(overlap, &bounds)) return 0;
    SetLastError(0);
    if (!MapWindowPoints(NULL, window, (POINT *)&bounds, 2) && GetLastError()) return 0;
    return ExcludeClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom) != ERROR;
}

static int exclude_overlapping_windows(HWND window, HDC dc)
{
    HWND overlap;
    unsigned int count = 0;
    for (overlap = GetWindow(window, GW_HWNDPREV); overlap && count < 4096U;
         overlap = GetWindow(overlap, GW_HWNDPREV), ++count) {
        if (!exclude_window_rect(window, overlap, dc)) return 0;
    }
    if (overlap) return 0;
    count = 0;
    for (overlap = GetWindow(window, GW_CHILD); overlap && count < 4096U;
         overlap = GetWindow(overlap, GW_HWNDNEXT), ++count) {
        if (!exclude_window_rect(window, overlap, dc)) return 0;
    }
    return overlap == NULL;
}

static int paint_tabs(HWND window, ThemedControl *control)
{
    ThemeResources *r = control->resources;
    PAINTSTRUCT paint;
    HDC dc;
    RECT client;
    int count = TabCtrl_GetItemCount(window), i, selected = TabCtrl_GetCurSel(window);
    int saved, vertical = (control->style & TCS_VERTICAL) != 0;
    int right = vertical && (control->style & TCS_RIGHT) != 0;
    HFONT previous;
    HIMAGELIST images = (HIMAGELIST)SendMessageW(window, TCM_GETIMAGELIST, 0, 0);
    if (images && (!control->image_draw || !control->image_size)) return 0;
    ZeroMemory(&paint, sizeof(paint));
    dc = BeginPaint(window, &paint);
    if (!dc) { restore_paint_damage(window, control, &paint); return 0; }
    /* Property pages are commonly siblings layered over the tab control. */
    if (!(saved = SaveDC(dc)) || !exclude_overlapping_windows(window, dc)) {
        if (saved) RestoreDC(dc, saved);
        restore_paint_damage(window, control, &paint);
        return 0;
    }
    GetClientRect(window, &client); FillRect(dc, &client, r->brushes[DLUMA_EDITOR_THEME_ROLE_PANEL]);
    previous = (HFONT)SelectObject(dc, vertical ? (right ? r->vertical_right_font : r->vertical_font) : r->font); SetBkMode(dc, TRANSPARENT);
    for (i = 0; i < count; ++i) {
        RECT rect, text_rect;
        WCHAR text[1024];
        TCITEMW item;
        ZeroMemory(&item, sizeof(item)); item.mask = TCIF_TEXT | TCIF_IMAGE;
        text[0] = 0; item.pszText = text; item.cchTextMax = 1024;
        if (!TabCtrl_GetItemRect(window, i, &rect) ||
            !SendMessageW(window, TCM_GETITEMW, i, (LPARAM)&item)) continue;
        FillRect(dc, &rect, r->brushes[i == selected ? DLUMA_EDITOR_THEME_ROLE_SURFACE :
            i == control->hover_index ? DLUMA_EDITOR_THEME_ROLE_HOVER : DLUMA_EDITOR_THEME_ROLE_PANEL]);
        SetTextColor(dc, color(r, IsWindowEnabled(window) ? DLUMA_EDITOR_THEME_ROLE_TEXT : DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT));
        if (vertical) {
            SIZE measured;
            int top = rect.top + dip(r, 4), bottom = rect.bottom - dip(r, 4);
            if (images && item.iImage >= 0) {
                int width = 0, height = 0;
                if (control->image_size(images, &width, &height)) {
                    (void)control->image_draw(images, item.iImage, dc,
                        (rect.left + rect.right - width) / 2, top, ILD_TRANSPARENT);
                    top += height + dip(r, 4);
                }
            }
            if (top < bottom && GetTextExtentPoint32W(dc, text, lstrlenW(text), &measured)) {
                RECT clip = rect;
                int x = (rect.left + rect.right + (right ? measured.cy : -measured.cy)) / 2;
                int y = (top + bottom + (right ? -measured.cx : measured.cx)) / 2;
                clip.top = top; clip.bottom = bottom;
                ExtTextOutW(dc, x, y, ETO_CLIPPED, &clip, text, lstrlenW(text), NULL);
            }
            if (i == selected) {
                RECT accent = rect;
                if (right) accent.right = accent.left + dip(r, 3);
                else accent.left = accent.right - dip(r, 3);
                FillRect(dc, &accent, r->brushes[DLUMA_EDITOR_THEME_ROLE_ACCENT]);
            }
            if (GetFocus() == window && i == (int)SendMessageW(window, TCM_GETCURFOCUS, 0, 0) &&
                !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
                RECT focus = rect; InflateRect(&focus, -dip(r, 3), -dip(r, 3)); DrawFocusRect(dc, &focus);
            }
            continue;
        }
        {
            int horizontal=dip(r,8), vertical_padding=dip(r,4);
            SIZE measured;
            if (!images && GetTextExtentPoint32W(dc,text,lstrlenW(text),&measured)) {
                int free_width=(rect.right-rect.left-measured.cx)/2;
                int free_height=(rect.bottom-rect.top-measured.cy)/2;
                if (horizontal>free_width) horizontal=max(0,free_width);
                if (vertical_padding>free_height) vertical_padding=max(0,free_height);
            }
            text_rect=rect; InflateRect(&text_rect,-horizontal,-vertical_padding);
        }
        if (images && item.iImage >= 0) {
            int width = 0, height = 0;
            if (control->image_size(images, &width, &height)) {
                (void)control->image_draw(images, item.iImage, dc, text_rect.left,
                    rect.top + (rect.bottom - rect.top - height) / 2, ILD_TRANSPARENT);
                text_rect.left += width + dip(r, 4);
            }
        }
        DrawTextW(dc, text, -1, &text_rect, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
        if (i == selected) {
            rect.top = rect.bottom - dip(r, 3);
            FillRect(dc, &rect, r->brushes[DLUMA_EDITOR_THEME_ROLE_ACCENT]);
        }
        if (GetFocus() == window && i == (int)SendMessageW(window, TCM_GETCURFOCUS, 0, 0) &&
            !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
            RECT focus;
            if (TabCtrl_GetItemRect(window, i, &focus)) { InflateRect(&focus, -dip(r, 3), -dip(r, 3)); DrawFocusRect(dc, &focus); }
        }
    }
    SelectObject(dc, previous); RestoreDC(dc, saved); EndPaint(window, &paint);
    return 1;
}

static int paint_combo(HWND window, ThemedControl *control)
{
    ThemeResources *r = control->resources;
    COMBOBOXINFO info;
    PAINTSTRUCT paint;
    HDC dc;
    RECT rect, text_rect, arrow;
    HFONT previous;
    WCHAR text[2048];
    HPEN pen;
    HGDIOBJ previous_pen;
    int x, y, delta, focused;
    if (control->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) return 0;
    ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
    if (!GetComboBoxInfo(window, &info)) return 0;
    focused = GetFocus() == window || (info.hwndItem && GetFocus() == info.hwndItem);
    dc = BeginPaint(window, &paint);
    GetClientRect(window, &rect);
    if ((control->style & 3) != CBS_DROPDOWNLIST && info.hwndItem) {
        RECT edit_rect;
        GetWindowRect(info.hwndItem, &edit_rect);
        MapWindowPoints(NULL, window, (POINT *)&edit_rect, 2);
        ExcludeClipRect(dc, edit_rect.left, edit_rect.top, edit_rect.right, edit_rect.bottom);
    }
    FillRect(dc, &rect, r->brushes[((control->style & 3) == CBS_DROPDOWNLIST && focused && IsWindowEnabled(window)) ?
        DLUMA_EDITOR_THEME_ROLE_SELECTION : DLUMA_EDITOR_THEME_ROLE_SURFACE]);
    previous = (HFONT)SelectObject(dc, r->font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color(r, !IsWindowEnabled(window) ? DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT :
        ((control->style & 3) == CBS_DROPDOWNLIST && focused) ? DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT));
    if ((control->style & 3) == CBS_DROPDOWNLIST) {
        GetWindowTextW(window, text, 2048); text_rect = info.rcItem;
        InflateRect(&text_rect, -dip(r, 4), 0);
        DrawTextW(dc, text, -1, &text_rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    arrow = info.rcButton;
    FillRect(dc, &arrow, r->brushes[info.stateButton & STATE_SYSTEM_PRESSED ? DLUMA_EDITOR_THEME_ROLE_SELECTION :
        control->hover ? DLUMA_EDITOR_THEME_ROLE_HOVER : DLUMA_EDITOR_THEME_ROLE_PANEL]);
    pen = CreatePen(PS_SOLID, dip(r, 1), color(r, IsWindowEnabled(window) ?
        DLUMA_EDITOR_THEME_ROLE_CONTROL_TEXT : DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT));
    previous_pen = SelectObject(dc, pen);
    x = (arrow.left + arrow.right) / 2; y = (arrow.top + arrow.bottom) / 2; delta = dip(r, 3);
    MoveToEx(dc, x - delta, y - delta / 2, NULL); LineTo(dc, x, y + delta / 2); LineTo(dc, x + delta, y - delta / 2);
    SelectObject(dc, previous_pen); DeleteObject(pen);
    outline(dc, &rect, color(r, focused ? DLUMA_EDITOR_THEME_ROLE_ACCENT : DLUMA_EDITOR_THEME_ROLE_BORDER), dip(r, 1));
    if (focused && (control->style & 3) == CBS_DROPDOWNLIST &&
        !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        RECT focus = info.rcItem; InflateRect(&focus, -dip(r, 2), -dip(r, 2)); DrawFocusRect(dc, &focus);
    }
    SelectObject(dc, previous); EndPaint(window, &paint);
    return 1;
}

static int can_paint_header(HWND window, ThemedControl *control)
{
    int i, count = Header_GetItemCount(window);
    for (i = 0; i < count; ++i) {
        HDITEMW item;
        ZeroMemory(&item, sizeof(item)); item.mask = HDI_FORMAT;
        if (SendMessageW(window, HDM_GETITEMW, i, (LPARAM)&item) &&
            (item.fmt & HDF_OWNERDRAW) && !dluma_editor_theme_grid_owns_header(window)) return 0;
    }
    if (SendMessageW(window, HDM_GETIMAGELIST, 0, 0) && (!control->image_draw || !control->image_size)) return 0;
    return 1;
}

static int paint_header(HWND window, ThemedControl *control)
{
    ThemeResources *r = control->resources;
    PAINTSTRUCT paint;
    HDC dc;
    RECT client;
    HFONT previous;
    int i, count = Header_GetItemCount(window);
    if (!can_paint_header(window, control)) return 0;
    dc = BeginPaint(window, &paint);
    GetClientRect(window, &client); FillRect(dc, &client, r->brushes[DLUMA_EDITOR_THEME_ROLE_PANEL]);
    previous = (HFONT)SelectObject(dc, r->font); SetBkMode(dc, TRANSPARENT);
    for (i = 0; i < count; ++i) {
        HDITEMW item;
        RECT rect, text_rect, separator;
        WCHAR text[2048];
        ZeroMemory(&item, sizeof(item)); item.mask = HDI_FORMAT | HDI_TEXT | HDI_IMAGE | HDI_BITMAP;
        text[0] = 0; item.pszText = text; item.cchTextMax = 2048;
        if (!SendMessageW(window, HDM_GETITEMW, i, (LPARAM)&item) ||
            !Header_GetItemRect(window, i, &rect)) continue;
        if (i == control->hover_index) FillRect(dc, &rect, r->brushes[DLUMA_EDITOR_THEME_ROLE_HOVER]);
        text_rect = rect; InflateRect(&text_rect, -dip(r, 8), 0);
        if (item.fmt & (HDF_SORTUP | HDF_SORTDOWN)) text_rect.right -= dip(r, 12);
        if ((item.fmt & HDF_BITMAP) && item.hbm) {
            BITMAP bitmap;
            if (GetObjectW(item.hbm, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
                HDC source = CreateCompatibleDC(dc);
                if (source) {
                    HGDIOBJ old_bitmap = SelectObject(source, item.hbm);
                    int x = (item.fmt & HDF_BITMAP_ON_RIGHT) ? text_rect.right - bitmap.bmWidth : text_rect.left;
                    (void)BitBlt(dc, x, rect.top + (rect.bottom - rect.top - bitmap.bmHeight) / 2,
                                 bitmap.bmWidth, bitmap.bmHeight, source, 0, 0, SRCCOPY);
                    SelectObject(source, old_bitmap); DeleteDC(source);
                    if (item.fmt & HDF_BITMAP_ON_RIGHT) text_rect.right -= bitmap.bmWidth + dip(r, 4);
                    else text_rect.left += bitmap.bmWidth + dip(r, 4);
                }
            }
        }
        if ((item.fmt & HDF_IMAGE) && item.iImage >= 0) {
            HIMAGELIST images = (HIMAGELIST)SendMessageW(window, HDM_GETIMAGELIST, 0, 0);
            int width = 0, height = 0;
            if (images && control->image_size(images, &width, &height)) {
                int x = (item.fmt & HDF_BITMAP_ON_RIGHT) ? text_rect.right - width : text_rect.left;
                (void)control->image_draw(images, item.iImage, dc, x, rect.top + (rect.bottom - rect.top - height) / 2, ILD_TRANSPARENT);
                if (item.fmt & HDF_BITMAP_ON_RIGHT) text_rect.right -= width + dip(r, 4);
                else text_rect.left += width + dip(r, 4);
            }
        }
        SetTextColor(dc, color(r, IsWindowEnabled(window) ? DLUMA_EDITOR_THEME_ROLE_CONTROL_TEXT : DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT));
        DrawTextW(dc, text, -1, &text_rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
            ((item.fmt & HDF_JUSTIFYMASK) == HDF_CENTER ? DT_CENTER : (item.fmt & HDF_RIGHT) ? DT_RIGHT : DT_LEFT));
        separator = rect; separator.left = separator.right - dip(r, 1);
        FillRect(dc, &separator, r->brushes[DLUMA_EDITOR_THEME_ROLE_BORDER]);
        if (item.fmt & (HDF_SORTUP | HDF_SORTDOWN)) {
            HPEN pen = CreatePen(PS_SOLID, dip(r, 1), color(r, DLUMA_EDITOR_THEME_ROLE_MUTED_TEXT));
            HGDIOBJ old_pen = SelectObject(dc, pen);
            int x = rect.right - dip(r, 8), y = (rect.top + rect.bottom) / 2;
            int direction = item.fmt & HDF_SORTUP ? -1 : 1;
            MoveToEx(dc, x - dip(r, 3), y - direction * dip(r, 1), NULL);
            LineTo(dc, x, y + direction * dip(r, 1)); LineTo(dc, x + dip(r, 3), y - direction * dip(r, 1));
            SelectObject(dc, old_pen); DeleteObject(pen);
        }
    }
    SelectObject(dc, previous); EndPaint(window, &paint);
    return 1;
}

static int paint_status(HWND window, ThemedControl *control)
{
    ThemeResources *r = control->resources;
    int simple = (int)SendMessageW(window, SB_ISSIMPLE, 0, 0);
    int count = simple ? 1 : (int)SendMessageW(window, SB_GETPARTS, 0, 0);
    PAINTSTRUCT paint;
    RECT client;
    HDC dc;
    HFONT old_font;
    WCHAR *text;
    int i;
    if (count < 1 || count > 256) return 0;
    for (i = 0; i < count; ++i) {
        WPARAM part = simple ? SB_SIMPLEID : (WPARAM)i;
        LRESULT info = SendMessageW(window, SB_GETTEXTLENGTHW, part, 0);
        if (HIWORD(info) & SBT_OWNERDRAW) return 0;
    }
    /* SB_GETTEXT has no capacity parameter and may change after its length
     * query; the documented maximum is 65,535 characters. */
    text = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, 65536U * sizeof(WCHAR));
    if (!text) return 0;
    dc = BeginPaint(window, &paint);
    GetClientRect(window, &client);
    FillRect(dc, &client, r->brushes[DLUMA_EDITOR_THEME_ROLE_PANEL]);
    old_font = (HFONT)SelectObject(dc, r->font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color(r, IsWindowEnabled(window) ? DLUMA_EDITOR_THEME_ROLE_CONTROL_TEXT : DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT));
    for (i = 0; i < count; ++i) {
        WPARAM part = simple ? SB_SIMPLEID : (WPARAM)i;
        RECT rect; HICON icon;
        if (simple) rect = client;
        else if (!SendMessageW(window, SB_GETRECT, part, (LPARAM)&rect)) continue;
        text[0] = 0;
        (void)SendMessageW(window, SB_GETTEXTW, part, (LPARAM)text);
        icon = (HICON)SendMessageW(window, SB_GETICON, part, 0);
        rect.left += dip(r, 5); rect.right -= dip(r, 4);
        if (icon) {
            int size = dip(r, 16);
            DrawIconEx(dc, rect.left, rect.top + (rect.bottom - rect.top - size) / 2, icon, size, size, 0, NULL, DI_NORMAL);
            rect.left += size + dip(r, 4);
        }
        DrawTextW(dc, text, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (!simple && i + 1 < count) {
            RECT separator = rect; separator.left = rect.right + dip(r, 3); separator.right = separator.left + dip(r, 1);
            FillRect(dc, &separator, r->brushes[DLUMA_EDITOR_THEME_ROLE_BORDER]);
        }
    }
    SelectObject(dc, old_font);
    EndPaint(window, &paint);
    HeapFree(GetProcessHeap(), 0, text);
    return 1;
}

static int custom_draw(HWND window, LPARAM lparam, LRESULT *result)
{
    NMCUSTOMDRAW *draw = (NMCUSTOMDRAW *)lparam;
    ThemedControl *child;
    ThemeResources *r;
    unsigned int background, foreground;
    (void)window;
    if (!draw || draw->hdr.code != NM_CUSTOMDRAW) return 0;
    child = (ThemedControl *)GetPropW(draw->hdr.hwndFrom, control_property);
    if (!child || !(r = child->resources) || (*result & CDRF_SKIPDEFAULT)) return 0;
    if (draw->dwDrawStage == CDDS_PREPAINT) {
        *result |= CDRF_NOTIFYITEMDRAW;
        return 0;
    }
    if (draw->dwDrawStage == CDDS_ITEMPOSTPAINT && child->family == FAMILY_TOOLBAR) {
        if (draw->uItemState & CDIS_FOCUS) { RECT focus = draw->rc; InflateRect(&focus, -dip(r, 2), -dip(r, 2)); DrawFocusRect(draw->hdc, &focus); }
        return 0;
    }
    if (draw->dwDrawStage != CDDS_ITEMPREPAINT &&
        draw->dwDrawStage != (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) return 0;
    background = DLUMA_EDITOR_THEME_ROLE_SURFACE;
    foreground = DLUMA_EDITOR_THEME_ROLE_TEXT;
    if (draw->uItemState & CDIS_SELECTED) {
        int active = GetFocus() == draw->hdr.hwndFrom;
        background = active ? DLUMA_EDITOR_THEME_ROLE_SELECTION : DLUMA_EDITOR_THEME_ROLE_INACTIVE_SELECTION;
        foreground = active || r->theme.kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST ? DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT;
    } else if (draw->uItemState & CDIS_HOT) background = DLUMA_EDITOR_THEME_ROLE_HOVER;
    else if (child->family == FAMILY_LISTVIEW && (draw->dwItemSpec & 1U)) background = DLUMA_EDITOR_THEME_ROLE_ALTERNATE;
    if (draw->uItemState & (CDIS_DISABLED | CDIS_GRAYED)) foreground = DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT;
    if (child->family == FAMILY_TRACKBAR) {
        if (draw->dwItemSpec == TBCD_CHANNEL || draw->dwItemSpec == TBCD_THUMB) {
            RECT rect = draw->rc;
            unsigned int fill = draw->dwItemSpec == TBCD_CHANNEL ? DLUMA_EDITOR_THEME_ROLE_BORDER :
                (draw->uItemState & (CDIS_SELECTED | CDIS_HOT)) ? DLUMA_EDITOR_THEME_ROLE_ACCENT : DLUMA_EDITOR_THEME_ROLE_CONTROL_TEXT;
            if (!IsWindowEnabled(draw->hdr.hwndFrom)) fill = DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT;
            FillRect(draw->hdc, &rect, r->brushes[fill]);
            if (draw->dwItemSpec == TBCD_THUMB) outline(draw->hdc, &rect, color(r, DLUMA_EDITOR_THEME_ROLE_BORDER), dip(r, 1));
            *result = CDRF_SKIPDEFAULT;
        }
    } else if (child->family == FAMILY_LISTVIEW) {
        NMLVCUSTOMDRAW *list = (NMLVCUSTOMDRAW *)draw;
        int state_color=(draw->uItemState & (CDIS_SELECTED | CDIS_DISABLED | CDIS_GRAYED))!=0;
        if (state_color || list->clrTextBk==CLR_DEFAULT || list->clrTextBk==CLR_NONE ||
            ordinary_background(r,list->clrTextBk)) {
            if (state_color || list->clrText==CLR_DEFAULT || ordinary_foreground(r,list->clrText))
                list->clrText=color(r,foreground);
            list->clrTextBk=color(r,background);
        }
        *result |= CDRF_NOTIFYSUBITEMDRAW;
    } else if (child->family == FAMILY_TREE) {
        NMTVCUSTOMDRAW *tree = (NMTVCUSTOMDRAW *)draw;
        int state_color=(draw->uItemState & (CDIS_SELECTED | CDIS_DISABLED | CDIS_GRAYED))!=0;
        if (state_color || tree->clrTextBk==CLR_DEFAULT || tree->clrTextBk==CLR_NONE ||
            ordinary_background(r,tree->clrTextBk)) {
            if (state_color || tree->clrText==CLR_DEFAULT || ordinary_foreground(r,tree->clrText))
                tree->clrText=color(r,foreground);
            tree->clrTextBk=color(r,background);
        }
    } else if (child->family == FAMILY_TOOLBAR) {
        NMTBCUSTOMDRAW *toolbar = (NMTBCUSTOMDRAW *)draw;
        unsigned int face = (draw->uItemState & (CDIS_SELECTED | CDIS_CHECKED)) ? DLUMA_EDITOR_THEME_ROLE_SELECTION :
            (draw->uItemState & CDIS_HOT) ? DLUMA_EDITOR_THEME_ROLE_HOVER : DLUMA_EDITOR_THEME_ROLE_PANEL;
        FillRect(draw->hdc, &draw->rc, r->brushes[face]);
        if ((draw->uItemState & CDIS_CHECKED) && !(draw->uItemState & (CDIS_DISABLED | CDIS_GRAYED)))
            foreground = DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT;
        toolbar->clrText = color(r, foreground);
        toolbar->clrBtnFace = color(r, DLUMA_EDITOR_THEME_ROLE_PANEL);
        toolbar->clrBtnHighlight = color(r, DLUMA_EDITOR_THEME_ROLE_HOVER);
        toolbar->clrHighlightHotTrack = color(r, DLUMA_EDITOR_THEME_ROLE_HOVER);
        toolbar->hbrMonoDither = r->brushes[DLUMA_EDITOR_THEME_ROLE_PANEL];
        *result |= TBCDRF_USECDCOLORS | TBCDRF_NOBACKGROUND | TBCDRF_NOEDGES | CDRF_NOTIFYPOSTPAINT;
    }
    return 0;
}

static void paint_edit_border(HWND window, ThemedControl *control)
{
    ThemeResources *r = control->resources;
    LONG_PTR style, exstyle;
    RECT bounds, client, frame;
    HWND overlap;
    HDC dc;
    int saved, pass;
    DWORD pid = 0;
    if (!r || r->theme.kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST ||
        GetWindowThreadProcessId(window, &pid) != GetCurrentThreadId() || pid != GetCurrentProcessId()) return;
    style = GetWindowLongPtrW(window, GWL_STYLE);
    exstyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (!(style & WS_CHILD) || !((style & WS_BORDER) || (exstyle & WS_EX_CLIENTEDGE)) ||
        !GetWindowRect(window, &bounds) || !GetClientRect(window, &client)) return;
    SetLastError(0);
    if (!MapWindowPoints(window, NULL, (POINT *)&client, 2) && GetLastError()) return;
    if (client.left < bounds.left || client.top < bounds.top || client.right > bounds.right ||
        client.bottom > bounds.bottom || client.left > client.right || client.top > client.bottom) return;
    OffsetRect(&client, -bounds.left, -bounds.top);
    SetRect(&frame, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
    dc = GetWindowDC(window);
    if (!dc) return;
    saved = SaveDC(dc);
    if (!saved) { ReleaseDC(window, dc); return; }
    if (ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom) == ERROR) goto done;
    for (pass = 0; pass < 2; ++pass) {
        LONG object = pass ? OBJID_HSCROLL : OBJID_VSCROLL;
        SCROLLBARINFO info;
        if (!(style & (pass ? WS_HSCROLL : WS_VSCROLL))) continue;
        ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
        if (!GetScrollBarInfo(window, object, &info)) goto done;
        OffsetRect(&info.rcScrollBar, -bounds.left, -bounds.top);
        if (ExcludeClipRect(dc, info.rcScrollBar.left, info.rcScrollBar.top,
                            info.rcScrollBar.right, info.rcScrollBar.bottom) == ERROR) goto done;
    }
    for (pass = 0; pass < 2; ++pass) {
        unsigned int count = 0;
        for (overlap = GetWindow(window, pass ? GW_CHILD : GW_HWNDPREV);
             overlap && count < 4096U;
             overlap = GetWindow(overlap, pass ? GW_HWNDNEXT : GW_HWNDPREV), ++count) {
            RECT other;
            if (!(GetWindowLongPtrW(overlap, GWL_STYLE) & WS_VISIBLE)) continue;
            if (!GetWindowRect(overlap, &other)) goto done;
            OffsetRect(&other, -bounds.left, -bounds.top);
            if (ExcludeClipRect(dc, other.left, other.top, other.right, other.bottom) == ERROR) goto done;
        }
        if (overlap) goto done;
    }
    FillRect(dc, &frame, r->brushes[GetFocus() == window && IsWindowEnabled(window) ?
        DLUMA_EDITOR_THEME_ROLE_ACCENT : DLUMA_EDITOR_THEME_ROLE_BORDER]);
done:
    RestoreDC(dc, saved);
    ReleaseDC(window, dc);
}

static void paint_scrollbar(HWND window, ThemedControl *control, LONG object, LONG_PTR style)
{
    SCROLLBARINFO info;
    ThemeResources *r = control->resources;
    HDC dc;
    RECT window_rect, track, thumb, arrow;
    int vertical;
    ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
    if (!GetScrollBarInfo(window, object, &info) ||
        (info.rgstate[0] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN))) return;
    GetWindowRect(window, &window_rect);
    track = info.rcScrollBar; OffsetRect(&track, -window_rect.left, -window_rect.top);
    vertical = object == OBJID_VSCROLL ||
        (object == OBJID_CLIENT && (style & SBS_VERT));
    dc = GetWindowDC(window);
    if (!dc) return;
    FillRect(dc, &track, r->brushes[DLUMA_EDITOR_THEME_ROLE_PANEL]);
    thumb = track;
    if (vertical) {
        thumb.top += info.xyThumbTop; thumb.bottom = track.top + info.xyThumbBottom;
        InflateRect(&thumb, -dip(r, 3), 0);
    } else {
        thumb.left += info.xyThumbTop; thumb.right = track.left + info.xyThumbBottom;
        InflateRect(&thumb, 0, -dip(r, 3));
    }
    if (thumb.right > thumb.left && thumb.bottom > thumb.top)
        FillRect(dc, &thumb, r->brushes[DLUMA_EDITOR_THEME_ROLE_HOVER]);
    arrow = track;
    if (vertical) arrow.bottom = arrow.top + info.dxyLineButton;
    else arrow.right = arrow.left + info.dxyLineButton;
    {
        int end;
        HPEN pen = CreatePen(PS_SOLID, dip(r, 1), color(r, DLUMA_EDITOR_THEME_ROLE_MUTED_TEXT));
        HGDIOBJ previous = SelectObject(dc, pen);
        for (end = 0; end < 2; ++end) {
            int x = (arrow.left + arrow.right) / 2, y = (arrow.top + arrow.bottom) / 2;
            int direction = end ? 1 : -1, delta = dip(r, 3);
            if (vertical) {
                MoveToEx(dc, x - delta, y - direction * delta / 2, NULL);
                LineTo(dc, x, y + direction * delta / 2);
                LineTo(dc, x + delta, y - direction * delta / 2);
                OffsetRect(&arrow, 0, track.bottom - track.top - info.dxyLineButton);
            } else {
                MoveToEx(dc, x - direction * delta / 2, y - delta, NULL);
                LineTo(dc, x + direction * delta / 2, y);
                LineTo(dc, x - direction * delta / 2, y + delta);
                OffsetRect(&arrow, track.right - track.left - info.dxyLineButton, 0);
            }
        }
        SelectObject(dc, previous); DeleteObject(pen);
    }
    ReleaseDC(window, dc);
}

static int erase_dialog_background(HWND window, HDC dc, HBRUSH brush)
{
    RECT client;
    HWND child;
    unsigned int count = 0;
    int saved, painted = 0;
    if (!GetClientRect(window, &client) || !(saved = SaveDC(dc))) return 0;
    /* A dialog's erase DC need not clip children. Keep its background paint
       outside their rectangles even when the dialog lacks WS_CLIPCHILDREN. */
    for (child = GetWindow(window, GW_CHILD); child && count < 4096U;
         child = GetWindow(child, GW_HWNDNEXT), ++count) {
        RECT bounds;
        if (!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) continue;
        if (!GetWindowRect(child, &bounds)) goto done;
        SetLastError(0);
        if (!MapWindowPoints(NULL, window, (POINT *)&bounds, 2) && GetLastError()) goto done;
        if (ExcludeClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom) == ERROR) goto done;
    }
    if (!child) painted = FillRect(dc, &client, brush) != 0;
done:
    RestoreDC(dc, saved);
    return painted;
}

static void controls_detach_internal(HWND window, int destroying);

static int ordinary_background(const ThemeResources *r, COLORREF value)
{
    unsigned int i;
    if (value == r->stock_colors[0] || value == r->stock_colors[1] || value == r->stock_colors[5]) return 1;
    for (i = 0; i < 4U; ++i) {
        uint32_t dark = dluma_editor_theme_dark_argb[i], light = dluma_editor_theme_light_argb[i];
        if (value == RGB((dark >> 16) & 255U, (dark >> 8) & 255U, dark & 255U) ||
            value == RGB((light >> 16) & 255U, (light >> 8) & 255U, light & 255U)) return 1;
    }
    return 0;
}

static int ordinary_foreground(const ThemeResources *r, COLORREF value)
{
    unsigned int i;
    if (value == r->stock_colors[2] || value == r->stock_colors[3] ||
        value == r->stock_colors[4] || value == r->stock_colors[6]) return 1;
    for (i = 4; i < 8U; ++i) {
        uint32_t dark = dluma_editor_theme_dark_argb[i], light = dluma_editor_theme_light_argb[i];
        if (value == RGB((dark >> 16) & 255U, (dark >> 8) & 255U, dark & 255U) ||
            value == RGB((light >> 16) & 255U, (light >> 8) & 255U, light & 255U)) return 1;
    }
    return 0;
}

static int controls_message_impl(HWND window, unsigned int stage,
    UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result,
    ThemeResources *fallback)
{
    ThemedControl *control = (ThemedControl *)GetPropW(window, control_property);
    ThemeResources *r = control ? control->resources : fallback;
    if (control && stage==DLUMA_EDITOR_UI_AFTER &&
        (message==WM_STYLECHANGED || (control->family==FAMILY_EDIT && message==EM_SETREADONLY)))
        control->style=GetWindowLongPtrW(window,GWL_STYLE);
    if (stage == DLUMA_EDITOR_UI_AFTER && message == WM_NOTIFY)
        return custom_draw(window, lparam, result);
    if (!r) return 0;
    if (control && stage==DLUMA_EDITOR_UI_AFTER && message==WM_CREATE && !control->applying &&
        (control->family==FAMILY_LISTVIEW || control->family==FAMILY_TREE)) {
        DlumaEditorThemeResolved theme=r->theme;
        /* Native creation initializes its own colors after early enrollment. */
        control->colors_dirty=1;
        dluma_editor_theme_controls_apply(window,&theme);
        return 0;
    }
    if (control && stage==DLUMA_EDITOR_UI_AFTER && !control->applying &&
        ((control->family==FAMILY_LISTVIEW &&
          (message==LVM_SETBKCOLOR || message==LVM_SETTEXTBKCOLOR || message==LVM_SETTEXTCOLOR)) ||
         (control->family==FAMILY_TREE && (message==TVM_SETBKCOLOR || message==TVM_SETTEXTCOLOR)))) {
        DlumaEditorThemeResolved theme=r->theme;
        if (message==LVM_SETBKCOLOR || message==TVM_SETBKCOLOR) control->original_background=(COLORREF)lparam;
        else if (message==LVM_SETTEXTBKCOLOR) control->original_text_background=(COLORREF)lparam;
        else control->original_text=(COLORREF)lparam;
        control->colors_dirty=1;
        dluma_editor_theme_controls_apply(window,&theme);
        return 0;
    }
    if (stage == DLUMA_EDITOR_UI_AFTER &&
        message >= WM_CTLCOLORMSGBOX && message <= WM_CTLCOLORSTATIC) {
        HDC dc = (HDC)wparam;
        ThemedControl *child = (ThemedControl *)GetPropW((HWND)lparam, control_property);
        unsigned int background = message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX ?
            DLUMA_EDITOR_THEME_ROLE_SURFACE : DLUMA_EDITOR_THEME_ROLE_WINDOW;
        COLORREF original_back=GetBkColor(dc);
        if (!child || child->detached) return 0;
        if (child->family == FAMILY_STATIC &&
            ((child->style & SS_TYPEMASK) == SS_BITMAP || (child->style & SS_TYPEMASK) == SS_ICON ||
             (child->style & SS_TYPEMASK) == SS_OWNERDRAW)) return 0;
        if (child->family==FAMILY_EDIT) background=DLUMA_EDITOR_THEME_ROLE_SURFACE;
        if (child->family==FAMILY_EDIT && (child->style & ES_READONLY) &&
            dluma_editor_theme_scintilla_owns_parent(window)) {
            unsigned int foreground=DLUMA_EDITOR_THEME_ROLE_CONTROL_TEXT;
            if (r->theme.kind!=DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST && !ordinary_background(r,original_back)) {
                unsigned int red=GetRValue(original_back), green=GetGValue(original_back), blue=GetBValue(original_back);
                if (green>red && green>blue) foreground=DLUMA_EDITOR_THEME_ROLE_SUCCESS;
                else if (red>green && red>blue) foreground=DLUMA_EDITOR_THEME_ROLE_ERROR;
                else if (red>blue && green>blue) foreground=DLUMA_EDITOR_THEME_ROLE_WARNING;
            }
            SetBkColor(dc,color(r,background)); SetTextColor(dc,color(r,foreground));
            *result=(LRESULT)r->brushes[background]; return 0;
        }
        if (!ordinary_background(r,original_back)) return 0;
        SetBkColor(dc, color(r, background));
        if (ordinary_foreground(r, GetTextColor(dc)))
            SetTextColor(dc, color(r, IsWindowEnabled((HWND)lparam) ?
                DLUMA_EDITOR_THEME_ROLE_CONTROL_TEXT : DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT));
        *result = (LRESULT)r->brushes[background];
        return 0;
    }
    /* The wrapper retains resources across callbacks, but the window can be
       detached before this dispatch resumes. Only the color fallback above
       is valid without a control belonging to this window. */
    if (!control) return 0;
    if (stage == DLUMA_EDITOR_UI_AFTER && (message == WM_DPICHANGED || message == WM_DPICHANGED_AFTERPARENT)) {
        dluma_editor_theme_controls_apply(window, &r->theme);
        return 0;
    }
    if (stage == DLUMA_EDITOR_UI_AFTER &&
        (message == WM_NCPAINT || message == WM_HSCROLL || message == WM_VSCROLL ||
         message == WM_PAINT || message == WM_SIZE)) {
        LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        if (control->family == FAMILY_EDIT) paint_edit_border(window, control);
        if (style & WS_VSCROLL) paint_scrollbar(window, control, OBJID_VSCROLL, style);
        if (style & WS_HSCROLL) paint_scrollbar(window, control, OBJID_HSCROLL, style);
        if (control->family == FAMILY_SCROLLBAR) paint_scrollbar(window, control, OBJID_CLIENT, style);
    }
    if (stage == DLUMA_EDITOR_UI_AFTER && control->family == FAMILY_EDIT &&
        (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE))
        paint_edit_border(window, control);
    if (stage != DLUMA_EDITOR_UI_BEFORE) return 0;
    if (message==WM_ERASEBKGND && control->family==FAMILY_WORKSPACE &&
        (GetWindowLongPtrW(window,GWL_STYLE) & WS_CLIPCHILDREN)) {
        RECT rect; GetClientRect(window,&rect);
        FillRect((HDC)wparam,&rect,r->brushes[DLUMA_EDITOR_THEME_ROLE_WINDOW]);
        *result=1; return 1;
    }
    if (message == WM_INITMENU) menu_prepare(window, (HMENU)wparam, GetMenu(window) == (HMENU)wparam);
    else if (message == WM_INITMENUPOPUP) menu_prepare(window, (HMENU)wparam, 0);
    else if (message == WM_UNINITMENUPOPUP) menu_release(window, (HMENU)wparam);
    else if (message == WM_MEASUREITEM && menu_measure(window, r, (MEASUREITEMSTRUCT *)lparam)) {
        *result = TRUE; return 1;
    } else if (message == WM_DRAWITEM && menu_draw(window, r, (DRAWITEMSTRUCT *)lparam)) {
        *result = TRUE; return 1;
    }
    if (message == WM_ERASEBKGND && control->family == FAMILY_DIALOG &&
        erase_dialog_background(window, (HDC)wparam, r->brushes[DLUMA_EDITOR_THEME_ROLE_WINDOW])) {
        *result = 1; return 1;
    }
    if (message == WM_ERASEBKGND && control->family == FAMILY_BUTTON &&
        (control->style & BS_TYPEMASK) == BS_GROUPBOX &&
        !(control->style & (BS_ICON | BS_BITMAP))) {
        *result = 1; return 1;
    }
    if (message == WM_ERASEBKGND && control->family == FAMILY_TAB &&
        !(control->style & TCS_OWNERDRAWFIXED) &&
        (!SendMessageW(window, TCM_GETIMAGELIST, 0, 0) ||
         (control->image_draw && control->image_size))) {
        *result = 1; return 1;
    }
    if (message == WM_ERASEBKGND && (control->family == FAMILY_TOOLBAR ||
        control->family == FAMILY_STATUS || control->family == FAMILY_TRACKBAR)) {
        RECT rect; GetClientRect(window, &rect);
        FillRect((HDC)wparam, &rect, r->brushes[control->family == FAMILY_TOOLBAR ?
            DLUMA_EDITOR_THEME_ROLE_PANEL : DLUMA_EDITOR_THEME_ROLE_WINDOW]);
        *result = 1; return 1;
    }
    if (message == WM_PAINT && control->family == FAMILY_BUTTON &&
        paint_button(window, control)) { *result = 0; return 1; }
    if (message == WM_PAINT && control->family == FAMILY_COMBO &&
        paint_combo(window, control)) { *result = 0; return 1; }
    if (message == WM_PAINT && control->family == FAMILY_HEADER &&
        paint_header(window, control)) { *result = 0; return 1; }
    if (message == WM_PAINT && control->family == FAMILY_STATUS &&
        paint_status(window, control)) { *result = 0; return 1; }
    if (message == WM_PAINT && control->family == FAMILY_TAB &&
        !(control->style & TCS_OWNERDRAWFIXED) && paint_tabs(window, control)) {
        *result = 0; return 1;
    }
    if (message == WM_MOUSEMOVE &&
        (control->family == FAMILY_BUTTON || control->family == FAMILY_COMBO ||
         control->family == FAMILY_HEADER || control->family == FAMILY_TAB) &&
        !(control->family == FAMILY_BUTTON &&
          ((control->style & BS_TYPEMASK) == BS_GROUPBOX ||
           (control->style & BS_TYPEMASK) == BS_OWNERDRAW ||
           (control->style & BS_TYPEMASK) == BS_USERBUTTON ||
           (control->style & (BS_ICON | BS_BITMAP)))) &&
        !(control->family == FAMILY_COMBO && (control->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE))) &&
        !(control->family == FAMILY_HEADER && !can_paint_header(window, control)) &&
        !(control->family == FAMILY_TAB && SendMessageW(window, TCM_GETIMAGELIST, 0, 0) &&
          (!control->image_draw || !control->image_size)) &&
        !(control->family == FAMILY_TAB && (control->style & TCS_OWNERDRAWFIXED))) {
        int hover_index = control->hover_index;
        if (control->family == FAMILY_TAB) {
            TCHITTESTINFO hit; hit.pt.x = (short)LOWORD(lparam); hit.pt.y = (short)HIWORD(lparam); hit.flags = 0;
            hover_index = (int)SendMessageW(window, TCM_HITTEST, 0, (LPARAM)&hit);
        } else if (control->family == FAMILY_HEADER) {
            HDHITTESTINFO hit; hit.pt.x = (short)LOWORD(lparam); hit.pt.y = (short)HIWORD(lparam); hit.flags = 0;
            hover_index = (int)SendMessageW(window, HDM_HITTEST, 0, (LPARAM)&hit);
        }
        if (!control->hover || hover_index != control->hover_index) {
            TRACKMOUSEEVENT tracking = {sizeof(tracking), TME_LEAVE, window, 0};
            control->hover = 1; control->hover_index = hover_index; TrackMouseEvent(&tracking); InvalidateRect(window, NULL, FALSE);
        }
    } else if (message == WM_MOUSELEAVE && control->hover) {
        control->hover = 0; control->hover_index = -1; InvalidateRect(window, NULL, FALSE);
    } else if (((control->family == FAMILY_BUTTON &&
                 (control->style & BS_TYPEMASK) != BS_GROUPBOX &&
                 (control->style & BS_TYPEMASK) != BS_OWNERDRAW &&
                 (control->style & BS_TYPEMASK) != BS_USERBUTTON &&
                 !(control->style & (BS_ICON | BS_BITMAP))) ||
                (control->family == FAMILY_COMBO &&
                 !(control->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE))) ||
                (control->family == FAMILY_TAB &&
                 !(control->style & TCS_OWNERDRAWFIXED))) &&
               (message == WM_SETFOCUS || message == WM_KILLFOCUS ||
                message == WM_ENABLE || (control->family == FAMILY_BUTTON &&
                (message == BM_SETCHECK || message == BM_SETSTATE))))
        InvalidateRect(window, NULL, FALSE);
    return 0;
}

int dluma_editor_theme_controls_message(HWND window, unsigned int stage,
    UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    ThemedControl *control = (ThemedControl *)GetPropW(window, control_property);
    uint64_t *invocation = (message == WM_NCDESTROY || message == WM_DRAWITEM) ? dluma_editor_ui_invocation_slot(DLUMA_EDITOR_UI_OWNER_THEME, DLUMA_EDITOR_UI_SLOT_CONTROLS) : NULL;
    ThemeResources *resources = NULL;
    int handled;
    dluma_editor_theme_properties_message(window,stage,message,lparam,
        control && control->resources ? &control->resources->theme : NULL);
    if (stage == DLUMA_EDITOR_UI_AFTER && message == WM_NCDESTROY && invocation && *invocation) {
        ThemedControl *destroyed = (ThemedControl *)(ULONG_PTR)*invocation;
        *invocation = 0;
        control_release(destroyed);
    }
    if (control) {
        ++control->references;
        resources = control->resources;
        resources_retain(resources);
        if (stage == DLUMA_EDITOR_UI_BEFORE && message == WM_NCDESTROY && invocation) {
            ++control->references;
            *invocation = (uint64_t)(ULONG_PTR)control;
            controls_detach_internal(window, 1);
        }
    }
    if (!control && stage==DLUMA_EDITOR_UI_AFTER &&
        message>=WM_CTLCOLORMSGBOX && message<=WM_CTLCOLORSTATIC && lparam) {
        ThemedControl *child=(ThemedControl *)GetPropW((HWND)lparam,control_property);
        if (child && !child->detached &&
            GetWindowThreadProcessId((HWND)lparam,NULL)==GetCurrentThreadId()) {
            resources=child->resources; resources_retain(resources);
        }
    }
    if (stage==DLUMA_EDITOR_UI_BEFORE && message==WM_DRAWITEM)
        checklist_scope_before(window,lparam,resources,invocation);
    handled = controls_message_impl(window, stage, message, wparam, lparam, result, resources);
    if (stage==DLUMA_EDITOR_UI_AFTER && message==WM_DRAWITEM)
        checklist_scope_after(invocation);
    resources_release(resources);
    if (control) control_release(control);
    return handled;
}

static void controls_detach_internal(HWND window, int destroying)
{
    ThemedControl *control = (ThemedControl *)GetPropW(window, control_property);
    if (!control || GetWindowThreadProcessId(window, NULL) != GetCurrentThreadId()) return;
    if (!destroying && !SetPropW(window, detaching_property, control)) return;
    control->detached = 1;
    RemovePropW(window, control_property);
    menu_release(window, NULL);
    if (destroying) { control_release(control); return; }
    if (control->applying) { control->pending_detach = 1; return; }
    control_finish_detach(window, control);
}

static void control_finish_detach(HWND window, ThemedControl *control)
{
    ThemeResources *r = control->resources, *previous = control->previous_resources;
#define DETACH_VALID() (GetPropW(window, detaching_property) == control)
    if (!DETACH_VALID()) goto done;
    if (control->font_owned && r) {
        HFONT current = (HFONT)SendMessageW(window, WM_GETFONT, 0, 0);
        if (DETACH_VALID() && (current == r->font || (previous && current == previous->font)))
            (void)SendMessageW(window, WM_SETFONT, (WPARAM)control->original_font, FALSE);
    }
    if (DETACH_VALID() && control->colors_owned && r) {
        if (control->family == FAMILY_LISTVIEW) {
            if (DETACH_VALID() && (ListView_GetBkColor(window) == color(r, DLUMA_EDITOR_THEME_ROLE_SURFACE) || (previous && ListView_GetBkColor(window) == color(previous, DLUMA_EDITOR_THEME_ROLE_SURFACE))) && DETACH_VALID())
                ListView_SetBkColor(window, control->original_background);
            if (DETACH_VALID() && (ListView_GetTextBkColor(window) == color(r, DLUMA_EDITOR_THEME_ROLE_SURFACE) || (previous && ListView_GetTextBkColor(window) == color(previous, DLUMA_EDITOR_THEME_ROLE_SURFACE))) && DETACH_VALID())
                ListView_SetTextBkColor(window, control->original_text_background);
            if (DETACH_VALID() && (ListView_GetTextColor(window) == color(r, DLUMA_EDITOR_THEME_ROLE_TEXT) || (previous && ListView_GetTextColor(window) == color(previous, DLUMA_EDITOR_THEME_ROLE_TEXT))) && DETACH_VALID())
                ListView_SetTextColor(window, control->original_text);
        } else if (control->family == FAMILY_TREE) {
            if (DETACH_VALID() && (TreeView_GetBkColor(window) == color(r, DLUMA_EDITOR_THEME_ROLE_SURFACE) || (previous && TreeView_GetBkColor(window) == color(previous, DLUMA_EDITOR_THEME_ROLE_SURFACE))) && DETACH_VALID())
                TreeView_SetBkColor(window, control->original_background);
            if (DETACH_VALID() && (TreeView_GetTextColor(window) == color(r, DLUMA_EDITOR_THEME_ROLE_TEXT) || (previous && TreeView_GetTextColor(window) == color(previous, DLUMA_EDITOR_THEME_ROLE_TEXT))) && DETACH_VALID())
                TreeView_SetTextColor(window, control->original_text);
        } else if (control->family == FAMILY_TOOLTIP) {
            if (DETACH_VALID() && ((COLORREF)SendMessageW(window, TTM_GETTIPBKCOLOR, 0, 0) == color(r, DLUMA_EDITOR_THEME_ROLE_PANEL) || (previous && (COLORREF)SendMessageW(window, TTM_GETTIPBKCOLOR, 0, 0) == color(previous, DLUMA_EDITOR_THEME_ROLE_PANEL))) && DETACH_VALID())
                (void)SendMessageW(window, TTM_SETTIPBKCOLOR, control->original_background, 0);
            if (DETACH_VALID() && ((COLORREF)SendMessageW(window, TTM_GETTIPTEXTCOLOR, 0, 0) == color(r, DLUMA_EDITOR_THEME_ROLE_TEXT) || (previous && (COLORREF)SendMessageW(window, TTM_GETTIPTEXTCOLOR, 0, 0) == color(previous, DLUMA_EDITOR_THEME_ROLE_TEXT))) && DETACH_VALID())
                (void)SendMessageW(window, TTM_SETTIPTEXTCOLOR, control->original_text, 0);
        }
    }
    if (DETACH_VALID() && control->dark_frame_owned) frame_color(window, control, 1);
done:
    if (DETACH_VALID()) {
        RemovePropW(window, detaching_property);
        if (GetMenu(window)) (void)DrawMenuBar(window);
        RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_FRAME |
            (control->family==FAMILY_WORKSPACE ? RDW_ERASE | RDW_NOCHILDREN : 0U));
    }
    control_release(control);
#undef DETACH_VALID
}

void dluma_editor_theme_controls_detach(HWND window)
{
    controls_detach_internal(window, 0);
}

int dluma_editor_theme_controls_drained(void)
{
    int drained;
    AcquireSRWLockShared(&checklist_lock);
    drained=!checklist_installed && !checklist_pending;
    ReleaseSRWLockShared(&checklist_lock);
    return drained && dluma_editor_theme_properties_drained() && InterlockedCompareExchange(&checklist_callbacks,0,0)==0 &&
        InterlockedCompareExchange(&checklist_frames,0,0)==0 &&
        InterlockedCompareExchange(&checklist_tls_failed,0,0)==0 &&
        InterlockedCompareExchange(&control_count, 0, 0) == 0;
}

void dluma_editor_theme_controls_layout_options(HWND page, HWND label,
                                                HWND combo, int top_dip)
{
    RECT client;
    UINT dpi = window_dpi(page);
    int margin = MulDiv(14, (int)dpi, 96), top = MulDiv(top_dip, (int)dpi, 96);
    int label_width = MulDiv(88, (int)dpi, 96), height = MulDiv(26, (int)dpi, 96);
    HFONT font = (HFONT)SendMessageW(label, WM_GETFONT, 0, 0);
    HDC dc = GetDC(label);
    HFONT old_font = (HFONT)SelectObject(dc, font);
    SIZE text;
    if (GetTextExtentPoint32W(dc, L"Theme", 5, &text)) {
        if (label_width < text.cx + margin) label_width = text.cx + margin;
        if (height < text.cy + MulDiv(8, (int)dpi, 96)) height = text.cy + MulDiv(8, (int)dpi, 96);
    }
    SelectObject(dc, old_font); ReleaseDC(label, dc);
    if (!GetClientRect(page, &client)) return;
    SetWindowPos(label, NULL, margin, top, label_width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(combo, NULL, margin + label_width, top,
        max(MulDiv(80, (int)dpi, 96), min(MulDiv(180, (int)dpi, 96), client.right - label_width - 2 * margin)),
        height * 6, SWP_NOZORDER | SWP_NOACTIVATE);
}
