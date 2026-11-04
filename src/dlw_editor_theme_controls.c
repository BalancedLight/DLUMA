#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#pragma warning(push)
#pragma warning(disable: 4201) /* Windows SDK Rich Edit anonymous unions. */
#include <richedit.h>
#pragma warning(pop)
#include <dwmapi.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>
#include "dlw_editor_theme_controls.h"
#include "dlw_editor_theme_grid.h"
#include "dlw_editor_theme_scintilla.h"
#include "dlw_editor_theme_profile.h"
#include "dlw_editor_theme_properties.h"
#include "dlw_editor_theme_hints.h"
#include "../build/generated/editor-theme/editor_theme_palette.generated.h"

extern int dluma_editor_is_main_window(HWND window, DWORD expected_process_id);

typedef enum ControlFamily {
    FAMILY_FRAME, FAMILY_DIALOG, FAMILY_EDIT, FAMILY_STATIC, FAMILY_BUTTON,
    FAMILY_COMBO, FAMILY_LIST, FAMILY_LISTVIEW, FAMILY_TREE, FAMILY_HEADER,
    FAMILY_TAB, FAMILY_TOOLBAR, FAMILY_TRACKBAR, FAMILY_TOOLTIP,
    FAMILY_STATUS, FAMILY_SCROLLBAR, FAMILY_SOURCE, FAMILY_WORKSPACE, FAMILY_HINTS
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

typedef struct ToolbarScope ToolbarScope;

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
    int report_grid_wanted;
    int report_grid_owned;
    int properties_grid_known, properties_grid_wanted, properties_grid_owned, properties_grid_sending;
    int properties_buffer_wanted;
    uint64_t properties_grid_failed_generation;
    int properties_grid_failed;
    unsigned int scrollbar_forwarding;
    uint64_t boolean_print_failed_generation;
    int boolean_print_failed, boolean_print_busy;
    int opaque_buffer_busy, opaque_erase_active, opaque_buffer_failed;
    int edge_owned, edge_sending, edge_external, edge_failed;
    int header_clip_owned, header_clip_sending, header_clip_external, header_clip_failed;
    ToolbarScope *toolbar_scope;
    uint64_t toolbar_failed_generation;
    int toolbar_buffer_failed;
    int hints_busy, hints_foreground_owned, hints_background_owned;
    int hints_background_system;
    unsigned int hints_stream_depth;
    void *hints_frozen_document;
    COLORREF hints_original_background, hints_foreground;
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

struct ToolbarScope {
    ToolbarScope *previous;
    ThemedControl *control;
    ThemeResources *resources;
    HWND window;
    BYTE *object;
    HDC buffer;
    COLORREF original_color, applied_color;
    UINT message;
    int color_owned, erase_suppressed, prepaint, postpaint, failed;
};

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
static void control_release(ThemedControl *);
static int ordinary_background(const ThemeResources *, COLORREF);
static int ordinary_foreground(const ThemeResources *, COLORREF);
static int picker_listview(HWND, HWND, ThemedControl *);
static int report_listview(HWND, ThemedControl *);
static void report_grid_apply(HWND, ThemedControl *, ThemeResources *);
static void hints_apply(HWND, ThemedControl *, ThemeResources *);
static void hints_restore(HWND, ThemedControl *);

static int hints_alive(HWND window, ThemedControl *control)
{
    return GetPropW(window, control_property) == control && !control->detached;
}

static void hints_finish_pending(HWND window, ThemedControl *control)
{
    if (!control->applying && !control->hints_busy && control->pending_detach) {
        control->pending_detach = 0;
        if (IsWindow(window)) control_finish_detach(window, control);
        else control_release(control);
    }
}

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
            info.fType & (MFT_OWNERDRAW | MFT_BITMAP | MFT_SEPARATOR) || info.hbmpItem) continue;
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
int dluma_editor_theme_controls_token_generation(HWND window, void *token, uint64_t generation)
{
    ThemedControl *control=(ThemedControl *)token;
    return dluma_editor_theme_controls_listview_alive(window,token) && control->resources &&
        control->resources->theme.generation==generation &&
        control->resources->theme.kind!=DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST;
}
void dluma_editor_theme_controls_release_listview(void *token)
{
    control_release((ThemedControl *)token);
}

/* Separate from report custom draw: these lists own their row renderer. */
int dluma_editor_theme_properties_grid_valid(HWND window, void *token, uint64_t generation)
{
    ThemedControl *c=(ThemedControl *)token;
    return dluma_editor_theme_controls_listview_alive(window,token) && c->resources &&
        c->resources->theme.generation==generation &&
        c->resources->theme.kind!=DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST &&
        c->properties_grid_owned && c->properties_grid_wanted;
}
void dluma_editor_theme_properties_grid_restore(HWND window, void *token)
{
    ThemedControl *c=(ThemedControl *)token; DWORD style, wanted;
    const DWORD mask=LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER;
    if (!dluma_editor_theme_controls_listview_alive(window,token) || !c->properties_grid_owned) return;
    c->properties_grid_owned=0;
    style=ListView_GetExtendedListViewStyle(window);
    if (!dluma_editor_theme_controls_listview_alive(window,token)) return;
    wanted=(c->properties_grid_wanted ? LVS_EX_GRIDLINES : 0) |
        (c->properties_buffer_wanted ? LVS_EX_DOUBLEBUFFER : 0);
    if ((style & mask)!=wanted) {
        ++c->properties_grid_sending;
        (void)ListView_SetExtendedListViewStyleEx(window,mask,wanted);
        --c->properties_grid_sending;
    }
}
void dluma_editor_theme_properties_grid_fail(HWND window, void *token, uint64_t generation)
{
    ThemedControl *c=(ThemedControl *)token;
    if (!dluma_editor_theme_properties_grid_valid(window,token,generation)) return;
    c->properties_grid_failed=1; c->properties_grid_failed_generation=generation;
    dluma_editor_theme_properties_grid_restore(window,token);
    if (dluma_editor_theme_controls_listview_alive(window,token)) InvalidateRect(window,NULL,FALSE);
}
int dluma_editor_theme_properties_grid_begin(HWND window, void *token, uint64_t generation)
{
    ThemedControl *c=(ThemedControl *)token; DWORD style;
    if (!dluma_editor_theme_controls_listview_alive(window,token) || !c->resources ||
        c->resources->theme.generation!=generation ||
        c->resources->theme.kind==DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST) return 0;
    if (c->properties_grid_failed && c->properties_grid_failed_generation==generation) return 0;
    c->properties_grid_failed=0;
    style=ListView_GetExtendedListViewStyle(window);
    if (!dluma_editor_theme_controls_listview_alive(window,token)) return 0;
    if (!c->properties_grid_known) {
        c->properties_grid_known=1;
        c->properties_grid_wanted=(style & LVS_EX_GRIDLINES)!=0;
        c->properties_buffer_wanted=(style & LVS_EX_DOUBLEBUFFER)!=0;
    }
    if (!c->properties_grid_wanted) return 0;
    if (!c->properties_grid_owned) {
        if (!(style & LVS_EX_GRIDLINES)) return 0;
        c->properties_grid_owned=1;
        ++c->properties_grid_sending;
        (void)ListView_SetExtendedListViewStyleEx(window,LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER,LVS_EX_DOUBLEBUFFER);
        --c->properties_grid_sending;
    }
    if (!dluma_editor_theme_properties_grid_valid(window,token,generation)) {
        dluma_editor_theme_properties_grid_restore(window,token); return 0;
    }
    style=ListView_GetExtendedListViewStyle(window);
    if (!dluma_editor_theme_properties_grid_valid(window,token,generation)) return 0;
    if ((style & (LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER)) != LVS_EX_DOUBLEBUFFER) {
        dluma_editor_theme_properties_grid_fail(window,token,generation); return 0;
    }
    return 1;
}

static int hints_window(HWND window)
{
    static const int siblings[] = { 1, 1977, 1883, 1884, 2328, 3150 };
    WCHAR name[64], title[128]; HWND parent = GetParent(window); unsigned int i;
    if (!parent || GetDlgCtrlID(window) != 3181 ||
        !GetClassNameW(window, name, 64) || lstrcmpiW(name, L"RichEdit20A") ||
        !GetClassNameW(parent, name, 64) || lstrcmpW(name, L"#32770") ||
        !GetWindowTextW(parent, title, 128) || lstrcmpW(title, L"Dying Light Developer Tools Hints") ||
        !dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR)) return 0;
    for (i = 0; i < sizeof(siblings) / sizeof(siblings[0]); ++i)
        if (!GetDlgItem(parent, siblings[i])) return 0;
    return 1;
}

static int family_for_window(HWND window, ControlFamily *family)
{
    WCHAR name[128];
    if (!GetClassNameW(window, name, 128)) return 0;
    if (!lstrcmpiW(name, L"#32770")) *family = FAMILY_DIALOG;
    else if (!lstrcmpiW(name, L"Edit")) *family = FAMILY_EDIT;
    else if (!lstrcmpiW(name, L"RichEdit20A") && hints_window(window)) *family = FAMILY_HINTS;
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
        control->report_grid_wanted = (ListView_GetExtendedListViewStyle(window) & LVS_EX_GRIDLINES) != 0;
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

/* Ownership covers only the client edge; all other style bits remain native. */
/* CToolBarEx keeps its nonclient color separately from the common control. */
#define TOOLBAR_VTABLE_RVA 0x15BDDE8U
#define TOOLBAR_FIRST_METHOD_RVA 0x50FC20U
#define TOOLBAR_OBJECT_SIZE 608U
#define TOOLBAR_COLOR_OFFSET 384U

static int toolbar_identity(HWND window, ThemedControl *control, BYTE **object)
{
    HMODULE mfc;
    void *vtable, *first, *permanent;
    BYTE *candidate;
    MEMORY_BASIC_INFORMATION memory;
    DWORD pid=0, access;
    if (!control || control->detached || control->family!=FAMILY_TOOLBAR ||
        GetPropW(window,control_property)!=control ||
        GetWindowThreadProcessId(window,&pid)!=GetCurrentThreadId() || pid!=GetCurrentProcessId()) return 0;
    mfc=GetModuleHandleW(L"mfc100.dll");
    if (!mfc || !dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR) ||
        !dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_MFC)) return 0;
    vtable=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,TOOLBAR_VTABLE_RVA,sizeof(void *));
    first=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,TOOLBAR_FIRST_METHOD_RVA,1);
    permanent=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,0x26B460U,1);
    if (!vtable || !first || !permanent || *(void **)vtable!=first ||
        (void *)GetProcAddress(mfc,(LPCSTR)(ULONG_PTR)4194U)!=permanent) return 0;
    candidate=(BYTE *)((ChecklistPermanentFn)permanent)(window);
    if (!checklist_memory(candidate,TOOLBAR_OBJECT_SIZE) || *(void **)candidate!=vtable ||
        *(HWND *)(candidate+64U)!=window || ((uintptr_t)(candidate+TOOLBAR_COLOR_OFFSET)&3U) ||
        !VirtualQuery(candidate+TOOLBAR_COLOR_OFFSET,&memory,sizeof(memory))) return 0;
    access=memory.Protect&0xffU;
    if (access!=PAGE_READWRITE && access!=PAGE_WRITECOPY &&
        access!=PAGE_EXECUTE_READWRITE && access!=PAGE_EXECUTE_WRITECOPY) return 0;
    if (GetPropW(window,control_property)!=control || control->detached) return 0;
    *object=candidate; return 1;
}

static int toolbar_scope_alive(const ToolbarScope *scope)
{
    return scope && !scope->control->detached && GetPropW(scope->window,control_property)==scope->control &&
        GetWindowThreadProcessId(scope->window,NULL)==GetCurrentThreadId();
}

static void toolbar_restore_color(ToolbarScope *scope)
{
    BYTE *object=NULL;
    if (!scope->color_owned) return;
    if (toolbar_scope_alive(scope) && toolbar_identity(scope->window,scope->control,&object) && object==scope->object)
        (void)InterlockedCompareExchange((volatile LONG *)(object+TOOLBAR_COLOR_OFFSET),
            (LONG)scope->original_color,(LONG)scope->applied_color);
    scope->color_owned=0;
}

static void toolbar_restore_active(ThemedControl *control)
{
    ToolbarScope *scope;
    for (scope=control->toolbar_scope; scope; scope=scope->previous) toolbar_restore_color(scope);
}

static void toolbar_fail_paint(ThemedControl *control, uint64_t generation)
{
    ToolbarScope *scope;
    control->toolbar_buffer_failed=1;
    control->toolbar_failed_generation=generation;
    for (scope=control->toolbar_scope; scope; scope=scope->previous)
        if (scope->message==WM_PAINT && scope->resources->theme.generation==generation) scope->failed=1;
}

static void toolbar_scope_after(uint64_t *scratch)
{
    ToolbarScope *scope;
    ThemedControl *control;
    if (!scratch || !*scratch) return;
    scope=(ToolbarScope *)(uintptr_t)*scratch; *scratch=0;
    control=scope->control;
    toolbar_restore_color(scope);
    if (scope->message==WM_PAINT && (scope->erase_suppressed || scope->prepaint) &&
        (scope->failed || !scope->prepaint || !scope->postpaint)) {
        toolbar_fail_paint(control,scope->resources->theme.generation);
        if (toolbar_scope_alive(scope)) InvalidateRect(scope->window,NULL,FALSE);
    }
    control->toolbar_scope=scope->previous;
    resources_release(scope->resources);
    control_release(control);
    HeapFree(GetProcessHeap(),0,scope);
}

static void toolbar_scope_before(HWND window, ThemedControl *control, UINT message, uint64_t *scratch)
{
    ToolbarScope *scope;
    ThemeResources *resources;
    BYTE *object=NULL;
    COLORREF original;
    if (!control || control->family!=FAMILY_TOOLBAR || !(resources=control->resources) ||
        resources->theme.kind==DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST) return;
    if (message==WM_PAINT && control->toolbar_buffer_failed &&
        control->toolbar_failed_generation==resources->theme.generation) return;
    if (!scratch || *scratch || !toolbar_identity(window,control,&object)) {
        if (message==WM_PAINT) toolbar_fail_paint(control,resources->theme.generation);
        return;
    }
    scope=(ToolbarScope *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*scope));
    if (!scope) {
        if (message==WM_PAINT) toolbar_fail_paint(control,resources->theme.generation);
        return;
    }
    ++control->references; resources_retain(resources);
    scope->control=control; scope->resources=resources; scope->window=window;
    scope->object=object; scope->message=message;
    *scratch=(uint64_t)(uintptr_t)scope;
    if (!dluma_editor_ui_invocation_cleanup(DLUMA_EDITOR_UI_OWNER_THEME,
        DLUMA_EDITOR_UI_SLOT_CONTROLS,toolbar_scope_after)) {
        *scratch=0;
        if (message==WM_PAINT) toolbar_fail_paint(control,resources->theme.generation);
        resources_release(resources); control_release(control); HeapFree(GetProcessHeap(),0,scope);
        return;
    }
    scope->previous=control->toolbar_scope; control->toolbar_scope=scope;
    if (message==WM_NCPAINT) {
        original=*(COLORREF *)(object+TOOLBAR_COLOR_OFFSET);
        if (ordinary_background(resources,original)) {
            scope->original_color=original;
            scope->applied_color=color(resources,DLUMA_EDITOR_THEME_ROLE_BORDER);
            scope->color_owned=(COLORREF)InterlockedCompareExchange(
                (volatile LONG *)(object+TOOLBAR_COLOR_OFFSET),(LONG)scope->applied_color,(LONG)original)==original;
        }
    }
}

static ToolbarScope *toolbar_paint_scope(ThemedControl *control)
{
    ToolbarScope *scope;
    BYTE *object=NULL;
    if (!control || !control->resources || (control->toolbar_buffer_failed &&
        control->toolbar_failed_generation==control->resources->theme.generation)) return NULL;
    for (scope=control->toolbar_scope; scope; scope=scope->previous) {
        if (scope->message!=WM_PAINT) continue;
        if (!toolbar_scope_alive(scope) || control->resources!=scope->resources ||
            scope->resources->theme.kind==DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST ||
            !toolbar_identity(scope->window,control,&object) || object!=scope->object) return NULL;
        return scope;
    }
    return NULL;
}

static void toolbar_custom_draw(ThemedControl *control, NMCUSTOMDRAW *draw, LRESULT *result)
{
    ToolbarScope *scope=toolbar_paint_scope(control);
    RECT client;
    if (!scope || draw->hdr.hwndFrom!=scope->window) return;
    if (draw->dwDrawStage==CDDS_PREPAINT) {
        if ((*result&CDRF_SKIPDEFAULT) || GetObjectType(draw->hdc)!=OBJ_MEMDC ||
            !GetClientRect(scope->window,&client) ||
            !FillRect(draw->hdc,&client,scope->resources->brushes[DLUMA_EDITOR_THEME_ROLE_PANEL])) {
            scope->failed=1; return;
        }
        scope->buffer=draw->hdc; scope->prepaint=1;
        *result|=CDRF_NOTIFYPOSTPAINT;
    } else if (draw->dwDrawStage==CDDS_POSTPAINT) {
        if (!scope->prepaint || draw->hdc!=scope->buffer || GetObjectType(draw->hdc)!=OBJ_MEMDC)
            scope->failed=1;
        else scope->postpaint=1;
    }
}

static int edge_valid(HWND window, ThemedControl *control, int detaching)
{
    return GetWindowThreadProcessId(window,NULL)==GetCurrentThreadId() &&
        GetPropW(window,detaching ? detaching_property : control_property)==control &&
        (detaching || !control->detached);
}
static int edge_eligible(HWND window, ThemedControl *control)
{
    LONG_PTR style=GetWindowLongPtrW(window,GWL_STYLE);
    WCHAR parent_class[64]; HWND parent;
    if (!(style & WS_CHILD)) return 0;
    if (control->family==FAMILY_LISTVIEW)
        return dluma_editor_theme_properties_is_list(window) && edge_valid(window,control,0);
    if (control->family!=FAMILY_EDIT || (style & (ES_MULTILINE | WS_VSCROLL | WS_HSCROLL))) return 0;
    parent=GetParent(window);
    if (!parent || !GetClassNameW(parent,parent_class,64)) return 0;
    return lstrcmpiW(parent_class,L"ComboBox") && lstrcmpiW(parent_class,L"ComboBoxEx32") &&
        edge_valid(window,control,0);
}
/* Set one style bit while retaining the control identity across messages. */
static int control_style_bit(HWND window, ThemedControl *control, int detaching,
    int index, LONG_PTR bit, int wanted, int *sending)
{
    LONG_PTR style, previous; DWORD error;
    int ok=0;
    if (!edge_valid(window,control,detaching)) return 0;
    ++*sending;
    SetLastError(ERROR_SUCCESS); style=GetWindowLongPtrW(window,index);
    if (!style && GetLastError()!=ERROR_SUCCESS) goto done;
    if (!!(style & bit)!=!!wanted) {
        SetLastError(ERROR_SUCCESS);
        previous=SetWindowLongPtrW(window,index,wanted ? style | bit : style & ~bit);
        error=GetLastError();
        if ((!previous && error!=ERROR_SUCCESS) || !edge_valid(window,control,detaching)) goto done;
        SetLastError(ERROR_SUCCESS); style=GetWindowLongPtrW(window,index);
        if ((!style && GetLastError()!=ERROR_SUCCESS) || !!(style & bit)!=!!wanted ||
            !edge_valid(window,control,detaching)) goto done;
    }
    if (!SetWindowPos(window,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED) ||
        !edge_valid(window,control,detaching)) goto done;
    SetLastError(ERROR_SUCCESS); style=GetWindowLongPtrW(window,index);
    if ((!style && GetLastError()!=ERROR_SUCCESS) || !edge_valid(window,control,detaching)) goto done;
    ok=!!(style & bit)==!!wanted;
done:
    --*sending;
    return ok;
}
static int edge_change(HWND window, ThemedControl *control, int restore, int detaching)
{
    return control_style_bit(window,control,detaching,GWL_EXSTYLE,WS_EX_CLIENTEDGE,restore,&control->edge_sending);
}
static void edge_restore(HWND window, ThemedControl *control, int detaching)
{
    LONG_PTR style;
    if (!control->edge_owned || !edge_valid(window,control,detaching)) return;
    SetLastError(ERROR_SUCCESS); style=GetWindowLongPtrW(window,GWL_EXSTYLE);
    if (!style && GetLastError()!=ERROR_SUCCESS) return;
    if (edge_change(window,control,1,detaching)) control->edge_owned=0;
}
static void edge_apply(HWND window, ThemedControl *control, ThemeResources *resources)
{
    LONG_PTR style;
    if (!edge_valid(window,control,0)) return;
    if (resources->theme.kind==DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST || !edge_eligible(window,control)) {
        edge_restore(window,control,0); return;
    }
    if (control->edge_owned || control->edge_external || control->edge_failed || !edge_valid(window,control,0)) return;
    SetLastError(ERROR_SUCCESS); style=GetWindowLongPtrW(window,GWL_EXSTYLE);
    if ((!style && GetLastError()!=ERROR_SUCCESS) || !(style & WS_EX_CLIENTEDGE)) return;
    control->edge_owned=1;
    if (!edge_change(window,control,0,0)) {
        control->edge_failed=1;
        if (edge_valid(window,control,0)) edge_restore(window,control,0);
    }
}

/* The list header can overlap an inline sibling at its lower edge. */
static int header_clip_eligible(HWND window, ThemedControl *control)
{
    HWND parent, header;
    if (!edge_valid(window,control,0) || control->family!=FAMILY_HEADER) return 0;
    parent=GetParent(window);
    if (!parent || !dluma_editor_theme_properties_is_list(parent) || !edge_valid(window,control,0) ||
        GetParent(window)!=parent) return 0;
    header=ListView_GetHeader(parent);
    return header==window && edge_valid(window,control,0) && GetParent(window)==parent;
}
static void header_clip_restore(HWND window, ThemedControl *control, int detaching)
{
    if (!control->header_clip_owned || !edge_valid(window,control,detaching)) return;
    if (control_style_bit(window,control,detaching,GWL_STYLE,WS_CLIPSIBLINGS,0,&control->header_clip_sending))
        control->header_clip_owned=0;
}
static void header_clip_apply(HWND window, ThemedControl *control)
{
    LONG_PTR style;
    if (!edge_valid(window,control,0) || control->family!=FAMILY_HEADER) return;
    if (!header_clip_eligible(window,control)) { header_clip_restore(window,control,0); return; }
    if (control->header_clip_owned || control->header_clip_external || control->header_clip_failed ||
        !edge_valid(window,control,0)) return;
    SetLastError(ERROR_SUCCESS); style=GetWindowLongPtrW(window,GWL_STYLE);
    if ((!style && GetLastError()!=ERROR_SUCCESS) || (style & WS_CLIPSIBLINGS)) return;
    control->header_clip_owned=1;
    if (!control_style_bit(window,control,0,GWL_STYLE,WS_CLIPSIBLINGS,1,&control->header_clip_sending)) {
        control->header_clip_failed=1;
        if (edge_valid(window,control,0)) header_clip_restore(window,control,0);
    }
}
/* A header can qualify after its first application in the same generation. */
static int header_clip_pending(HWND window, ThemedControl *control)
{
    return control->family==FAMILY_HEADER && !control->header_clip_owned &&
        !control->header_clip_external && !control->header_clip_failed &&
        !(GetWindowLongPtrW(window,GWL_STYLE) & WS_CLIPSIBLINGS) &&
        dluma_editor_theme_properties_is_list(GetParent(window));
}

static int edge_pending(HWND window, ThemedControl *control,
    const DlumaEditorThemeResolved *theme)
{
    if (theme->kind==DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST) return control->edge_owned;
    return !control->edge_owned && !control->edge_external && !control->edge_failed &&
        (GetWindowLongPtrW(window,GWL_EXSTYLE) & WS_EX_CLIENTEDGE) &&
        edge_eligible(window,control);
}

void dluma_editor_theme_controls_apply(HWND window,
                                       const DlumaEditorThemeResolved *theme)
{
    ThemedControl *control = (ThemedControl *)GetPropW(window, control_property);
    ThemeResources *previous, *resources;
    HFONT current_font;
    if (!control || !theme || GetWindowThreadProcessId(window, NULL) != GetCurrentThreadId()) return;
    if (control->applying) { (void)dluma_editor_ui_request_refresh(); return; }
    if (!header_clip_pending(window,control) && !edge_pending(window,control,theme) &&
        !control->colors_dirty && control->resources && control->resources->theme.generation == theme->generation) {
        UINT dpi = window_dpi(window);
        if (control->resources->dpi == dpi && control->resources->font_height == font_height_for_dpi(dpi)) return;
    }
    ++control->references;
    control->applying = 1;
    control->colors_dirty = 0;
    resources = resources_acquire(window, theme);
    if (!resources) { control->colors_dirty=1; control->applying = 0; control_release(control); return; }
    previous = control->resources; control->previous_resources = previous; control->resources = resources;
    edge_apply(window,control,resources);
    if (!control->detached) header_clip_apply(window,control);
    if (!control->detached && GetPropW(window,control_property)==control &&
        control->family != FAMILY_FRAME && control->family != FAMILY_SCROLLBAR &&
        control->family != FAMILY_SOURCE && control->family != FAMILY_WORKSPACE && control->family != FAMILY_HINTS &&
        !(control->family == FAMILY_TAB && (control->style & TCS_VERTICAL))) {
        current_font = (HFONT)SendMessageW(window, WM_GETFONT, 0, 0);
        if (!control->detached && (current_font == control->original_font || (control->font_owned && previous && current_font == previous->font))) {
            control->font_owned = 1;
            (void)SendMessageW(window, WM_SETFONT, (WPARAM)resources->font, FALSE);
        } else control->font_owned = 0;
    }
    if (!control->detached && control->family == FAMILY_HINTS) {
        hints_apply(window, control, resources);
    } else if (!control->detached && control->family == FAMILY_LISTVIEW) {
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
        if (!control->detached) report_grid_apply(window, control, resources);
        if (!control->detached && resources->theme.kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST)
            dluma_editor_theme_properties_grid_restore(window, control);
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
    /* Refresh the dialog background without erasing its child windows. */
    if (!control->detached && control->family == FAMILY_DIALOG &&
        GetPropW(window, control_property) == control && control->resources == resources)
        (void)RedrawWindow(window, NULL, NULL,
            RDW_INVALIDATE | RDW_ERASE | RDW_NOCHILDREN);
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

static void hints_apply(HWND window, ThemedControl *control, ThemeResources *resources)
{
    COLORREF foreground, background;
    LRESULT prior;
    int high_contrast, outcome;
    if (control->hints_busy || !hints_alive(window, control) || !resources ||
        !(GetWindowLongPtrW(window, GWL_STYLE) & ES_READONLY)) return;
    control->hints_busy = 1;
    high_contrast = resources->theme.kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST;
    foreground = color(resources, DLUMA_EDITOR_THEME_ROLE_TEXT);
    background = color(resources, DLUMA_EDITOR_THEME_ROLE_SURFACE);
    if (control->hints_foreground_owned) {
        outcome = dluma_editor_theme_hints_foreground(window, control, control->hints_foreground, 0,
                foreground, high_contrast);
        if (!outcome) goto unsupported;
        control->hints_foreground_owned = !high_contrast;
        control->hints_foreground = foreground;
        if (!hints_alive(window, control)) goto done;
        if (outcome != 1) goto unsupported;
    } else if (!high_contrast) {
        outcome = dluma_editor_theme_hints_foreground(window, control, 0, 1, foreground, 0);
        if (!outcome) goto unsupported;
        control->hints_foreground_owned = 1;
        control->hints_foreground = foreground;
        if (!hints_alive(window, control)) goto done;
        if (outcome != 1) goto unsupported;
    }
    control->hints_foreground = foreground;
    if (!control->hints_background_owned) {
        prior = SendMessageW(window, EM_SETBKGNDCOLOR, high_contrast ? TRUE : FALSE,
                             high_contrast ? 0 : (LPARAM)background);
        control->hints_original_background = (COLORREF)prior;
        control->hints_background_system = 0;
        control->hints_background_owned = 1;
        if (!hints_alive(window, control)) goto done;
    } else {
        (void)SendMessageW(window, EM_SETBKGNDCOLOR, high_contrast ? TRUE : FALSE,
                           high_contrast ? 0 : (LPARAM)background);
    }
    goto done;
unsupported:
    if (hints_alive(window, control) && control->hints_background_owned) {
        (void)SendMessageW(window, EM_SETBKGNDCOLOR, control->hints_background_system ? TRUE : FALSE,
            control->hints_background_system ? 0 : (LPARAM)control->hints_original_background);
        control->hints_background_owned = 0;
    }
done:
    if (hints_alive(window, control) && control->resources != resources) {
        control->colors_dirty = 1;
        (void)dluma_editor_ui_request_refresh();
    }
    control->hints_busy = 0;
    hints_finish_pending(window, control);
}

static void hints_restore(HWND window, ThemedControl *control)
{
    if (control->hints_busy) return;
    control->hints_busy = 1;
    if (control->hints_foreground_owned) {
        if (dluma_editor_theme_hints_foreground(window, control, control->hints_foreground, 0, 0, 1))
            control->hints_foreground_owned = 0;
    }
    if (GetPropW(window, detaching_property) == control && control->hints_background_owned)
        (void)SendMessageW(window, EM_SETBKGNDCOLOR, control->hints_background_system ? TRUE : FALSE,
            control->hints_background_system ? 0 : (LPARAM)control->hints_original_background);
    control->hints_busy = 0;
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

static void restore_paint_damage(HWND, ThemedControl *, PAINTSTRUCT *);

static int paint_static_frame(HWND window, ThemedControl *control)
{
    UINT type = (UINT)(control->style & SS_TYPEMASK);
    ThemeResources *r = control->resources;
    PAINTSTRUCT paint;
    RECT rect;
    HDC dc;
    HPEN pen;
    HGDIOBJ previous, previous_brush;
    if (type != SS_ETCHEDFRAME && type != SS_ETCHEDHORZ && type != SS_ETCHEDVERT)
        return 0;
    if (!r || r->theme.kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST) return 0;
    pen = CreatePen(PS_SOLID, dip(r, 1), color(r, DLUMA_EDITOR_THEME_ROLE_BORDER));
    if (!pen) return 0;
    ZeroMemory(&paint, sizeof(paint));
    dc = BeginPaint(window, &paint);
    if (!dc) { DeleteObject(pen); restore_paint_damage(window, control, &paint); return 0; }
    GetClientRect(window, &rect);
    previous = SelectObject(dc, pen);
    if (type == SS_ETCHEDFRAME) {
        RECT edge = rect;
        int thickness = dip(r, 2);
        edge.bottom = min(rect.bottom, rect.top + thickness);
        FillRect(dc, &edge, r->brushes[DLUMA_EDITOR_THEME_ROLE_WINDOW]);
        edge = rect; edge.top = max(rect.top, rect.bottom - thickness);
        FillRect(dc, &edge, r->brushes[DLUMA_EDITOR_THEME_ROLE_WINDOW]);
        edge = rect; edge.right = min(rect.right, rect.left + thickness);
        FillRect(dc, &edge, r->brushes[DLUMA_EDITOR_THEME_ROLE_WINDOW]);
        edge = rect; edge.left = max(rect.left, rect.right - thickness);
        FillRect(dc, &edge, r->brushes[DLUMA_EDITOR_THEME_ROLE_WINDOW]);
        previous_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(dc, previous_brush);
    } else {
        FillRect(dc, &rect, r->brushes[DLUMA_EDITOR_THEME_ROLE_WINDOW]);
        if (type == SS_ETCHEDHORZ) {
            int y = (rect.top + rect.bottom) / 2;
            MoveToEx(dc, rect.left, y, NULL); LineTo(dc, rect.right, y);
        } else {
            int x = (rect.left + rect.right) / 2;
            MoveToEx(dc, x, rect.top, NULL); LineTo(dc, x, rect.bottom);
        }
    }
    SelectObject(dc, previous); DeleteObject(pen);
    EndPaint(window, &paint);
    return 1;
}

static void fit_button_font(HDC dc, ThemedControl *control, const WCHAR *text,
                            const RECT *bounds, UINT flags)
{
    RECT measured = *bounds;
    HFONT current;
    if (!(flags & DT_SINGLELINE) || !control->original_font ||
        control->original_font == control->resources->font) return;
    if (!DrawTextW(dc, text, -1, &measured, flags | DT_CALCRECT)) return;
    if (measured.right - measured.left <= bounds->right - bounds->left &&
        measured.bottom - measured.top <= bounds->bottom - bounds->top) return;
    current = (HFONT)SelectObject(dc, control->original_font);
    measured = *bounds;
    if (!DrawTextW(dc, text, -1, &measured, flags | DT_CALCRECT) ||
        measured.right - measured.left > bounds->right - bounds->left ||
        measured.bottom - measured.top > bounds->bottom - bounds->top)
        SelectObject(dc, current);
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

typedef int (*OpaquePaintFn)(HWND, ThemedControl *, HDC);
/* 1 = buffered paint completed, 0 = preallocation unavailable, -1 =
   paint began but failed; the caller falls back to stock for that damage. */
static int paint_opaque_buffer(HWND window, ThemedControl *control, OpaquePaintFn draw)
{
    ThemeResources *r=control->resources; LONG_PTR style=control->style;
    RECT rect, after, damage; PAINTSTRUCT paint;
    HDC reference=NULL, buffer=NULL, dc=NULL; HBITMAP bitmap=NULL; HGDIOBJ old_bitmap=NULL;
    DWORD layout; int ok=0, began=0;
    if (!r || !GetClientRect(window,&rect) || rect.right<=rect.left || rect.bottom<=rect.top) return 0;
    ++control->references; resources_retain(r);
    ZeroMemory(&paint,sizeof(paint)); SetRectEmpty(&damage);
    __try {
        reference=GetDC(window);
        if (!reference) __leave;
        buffer=CreateCompatibleDC(reference);
        if (buffer) bitmap=CreateCompatibleBitmap(reference,rect.right-rect.left,rect.bottom-rect.top);
        ReleaseDC(window,reference); reference=NULL;
        if (!bitmap) __leave;
        old_bitmap=SelectObject(buffer,bitmap);
        if (!old_bitmap || old_bitmap==HGDI_ERROR) { old_bitmap=NULL; __leave; }
        if (!edge_valid(window,control,0) || control->resources!=r || GetWindowLongPtrW(window,GWL_STYLE)!=style) __leave;
        (void)GetUpdateRect(window,&damage,FALSE);
        control->opaque_buffer_busy=1; control->opaque_erase_active=1; ok=-1;
        dc=BeginPaint(window,&paint); began=dc!=NULL;
        control->opaque_erase_active=0;
        if (!dc) __leave;
        damage=paint.rcPaint; layout=GetLayout(dc);
        if (layout==GDI_ERROR || SetLayout(buffer,layout)==GDI_ERROR || !edge_valid(window,control,0) ||
            control->resources!=r || !draw(window,control,buffer) || !edge_valid(window,control,0) ||
            control->resources!=r || GetWindowLongPtrW(window,GWL_STYLE)!=style ||
            !GetClientRect(window,&after) || !EqualRect(&rect,&after)) __leave;
        if (!IsRectEmpty(&paint.rcPaint) && !BitBlt(dc,paint.rcPaint.left,paint.rcPaint.top,
            paint.rcPaint.right-paint.rcPaint.left,paint.rcPaint.bottom-paint.rcPaint.top,
            buffer,paint.rcPaint.left,paint.rcPaint.top,SRCCOPY)) __leave;
        ok=1;
    } __finally {
        if (AbnormalTermination()) ok=-1;
        control->opaque_erase_active=0; control->opaque_buffer_busy=0;
        if (began) EndPaint(window,&paint);
        if (ok<0) {
            control->opaque_buffer_failed=1;
            if (edge_valid(window,control,0) && !IsRectEmpty(&damage)) InvalidateRect(window,&damage,TRUE);
        }
        if (old_bitmap) SelectObject(buffer,old_bitmap);
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        if (reference) ReleaseDC(window,reference);
        resources_release(r); control_release(control);
    }
    return ok;
}

static int draw_button_buffer(HWND window, ThemedControl *control, HDC dc)
{
    ThemeResources *r=control->resources;
    UINT type=(UINT)(control->style & BS_TYPEMASK), flags;
    RECT rect, text_rect; WCHAR text[1024]; LRESULT state;
    unsigned int background; COLORREF foreground; HFONT old_font;
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
    fit_button_font(dc, control, text, &text_rect, flags);
    (void)DrawTextW(dc, text, -1, &text_rect, flags);
    if ((state & BST_FOCUS) && !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        RECT focus = rect; InflateRect(&focus, -dip(r, 3), -dip(r, 3));
        DrawFocusRect(dc, &focus);
    }
    SelectObject(dc, old_font);
    return 1;
}

static int paint_button(HWND window, ThemedControl *control)
{
    UINT type = (UINT)(control->style & BS_TYPEMASK);
    PAINTSTRUCT paint;
    HDC dc;
    int saved = 0;
    if (type == BS_OWNERDRAW || type == BS_USERBUTTON ||
        (control->style & (BS_ICON | BS_BITMAP))) return 0;
    if (control->opaque_buffer_busy) return 0;
    if (type!=BS_GROUPBOX && !control->opaque_buffer_failed) {
        int buffered=paint_opaque_buffer(window,control,draw_button_buffer);
        if (buffered) return buffered>0;
    }
    ZeroMemory(&paint, sizeof(paint));
    dc = BeginPaint(window, &paint);
    if (!dc) { restore_paint_damage(window, control, &paint); return 0; }
    if (type == BS_GROUPBOX && (!(saved = SaveDC(dc)) ||
        !exclude_overlapping_windows(window, dc))) {
        if (saved) RestoreDC(dc, saved);
        restore_paint_damage(window, control, &paint);
        return 0;
    }
    (void)draw_button_buffer(window,control,dc);
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

static int draw_combo_content(HWND window, ThemedControl *control, HDC dc, COMBOBOXINFO info, int focused)
{
    ThemeResources *r=control->resources; RECT rect, text_rect, arrow;
    HFONT previous; WCHAR text[2048]; HPEN pen; HGDIOBJ previous_pen;
    int x, y, delta;
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
    SelectObject(dc, previous);
    return 1;
}

static int draw_combo_buffer(HWND window, ThemedControl *control, HDC dc)
{
    COMBOBOXINFO info; int focused;
    ZeroMemory(&info,sizeof(info)); info.cbSize=sizeof(info);
    if (!GetComboBoxInfo(window,&info)) return 0;
    focused=GetFocus()==window || (info.hwndItem && GetFocus()==info.hwndItem);
    return draw_combo_content(window,control,dc,info,focused);
}

static int paint_combo(HWND window, ThemedControl *control)
{
    COMBOBOXINFO info;
    PAINTSTRUCT paint;
    HDC dc;
    int focused;
    if (control->style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) return 0;
    if (control->opaque_buffer_busy) return 0;
    if ((control->style & 3)==CBS_DROPDOWNLIST && !control->opaque_buffer_failed) {
        int buffered=paint_opaque_buffer(window,control,draw_combo_buffer);
        if (buffered) return buffered>0;
    }
    ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
    if (!GetComboBoxInfo(window, &info)) return 0;
    focused=GetFocus()==window || (info.hwndItem && GetFocus()==info.hwndItem);
    dc = BeginPaint(window, &paint);
    (void)draw_combo_content(window,control,dc,info,focused);
    EndPaint(window, &paint);
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

static int picker_listview(HWND parent, HWND window, ThemedControl *control)
{
    WCHAR parent_class[32]; DWORD parent_pid = 0, child_pid = 0;
    LONG_PTR style;
    if (!control || control->detached || control->family != FAMILY_LISTVIEW ||
        GetDlgCtrlID(window) != 1934 || GetParent(window) != parent ||
        GetWindowThreadProcessId(parent, &parent_pid) != GetCurrentThreadId() ||
        GetWindowThreadProcessId(window, &child_pid) != GetCurrentThreadId() ||
        parent_pid != GetCurrentProcessId() || child_pid != parent_pid ||
        !GetClassNameW(parent, parent_class, 32) ||
        lstrcmpiW(parent_class, L"#32770")) return 0;
    style = GetWindowLongPtrW(window, GWL_STYLE);
    return (style & LVS_TYPEMASK) == LVS_REPORT &&
        (style & (LVS_OWNERDATA | LVS_SINGLESEL)) == (LVS_OWNERDATA | LVS_SINGLESEL);
}

static int report_listview(HWND window, ThemedControl *control)
{
    LONG_PTR style;
    if (!control || control->detached || control->family != FAMILY_LISTVIEW ||
        GetWindowThreadProcessId(window, NULL) != GetCurrentThreadId()) return 0;
    style = GetWindowLongPtrW(window, GWL_STYLE);
    return (style & LVS_TYPEMASK) == LVS_REPORT && !(style & LVS_OWNERDRAWFIXED);
}

/* Keep the native row renderer; draw its report separators in the palette. */
static void report_grid_apply(HWND window, ThemedControl *control, ThemeResources *resources)
{
    DWORD style;
    int themed, qualified = report_listview(window, control);
    if (!qualified && !control->report_grid_owned) return;
    style = ListView_GetExtendedListViewStyle(window);
    if (GetPropW(window, control_property) != control || control->detached ||
        control->resources != resources) return;
    themed = qualified && control->report_grid_wanted && resources->theme.kind != DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST;
    if (themed) {
        control->report_grid_owned = 1;
        if (style & LVS_EX_GRIDLINES)
            (void)ListView_SetExtendedListViewStyleEx(window, LVS_EX_GRIDLINES, 0);
    } else if (control->report_grid_owned) {
        control->report_grid_owned = 0;
        if (!(style & LVS_EX_GRIDLINES) && control->report_grid_wanted)
            (void)ListView_SetExtendedListViewStyleEx(window, LVS_EX_GRIDLINES, LVS_EX_GRIDLINES);
    }
}

static void report_grid_paint(HWND window, ThemedControl *control, HDC dc)
{
    ThemeResources *r = control->resources;
    HWND header;
    RECT body, bounds, line;
    LRESULT columns, items, top;
    int saved = 0, i;
    if (!dc || !r || !control->report_grid_owned || !control->report_grid_wanted ||
        r->theme.kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST ||
        !report_listview(window, control)) return;
    ++control->references;
    resources_retain(r);
#define REPORT_GRID_VALID() (GetPropW(window, control_property) == control && !control->detached && control->resources == r)
    __try {
        if (!GetClientRect(window, &body)) __leave;
        header = ListView_GetHeader(window);
        if (!REPORT_GRID_VALID() || !header || GetParent(header) != window ||
            !GetWindowRect(header, &bounds)) __leave;
        SetLastError(0);
        if (!MapWindowPoints(NULL, window, (POINT *)&bounds, 2) && GetLastError()) __leave;
        if (bounds.bottom > body.top) body.top = bounds.bottom;
        if (IsRectEmpty(&body)) __leave;
        columns = Header_GetItemCount(header);
        if (!REPORT_GRID_VALID() || columns < 1 || columns > 256) __leave;
        saved = SaveDC(dc);
        if (!saved || IntersectClipRect(dc, body.left, body.top, body.right, body.bottom) == ERROR) __leave;
        for (i = 0; i < (int)columns; ++i) {
            if (!Header_GetItemRect(header, i, &bounds) || !REPORT_GRID_VALID()) __leave;
            SetLastError(0);
            if (!MapWindowPoints(header, window, (POINT *)&bounds, 2) && GetLastError()) __leave;
            if (bounds.right <= bounds.left || bounds.right <= body.left || bounds.right > body.right) continue;
            line = body; line.left = max(bounds.left, bounds.right - dip(r, 1)); line.right = bounds.right;
            FillRect(dc, &line, r->brushes[DLUMA_EDITOR_THEME_ROLE_BORDER]);
        }
        items = ListView_GetItemCount(window);
        if (!REPORT_GRID_VALID() || items < 1) __leave;
        top = ListView_GetTopIndex(window);
        if (!REPORT_GRID_VALID() || top < 0 || top >= items) __leave;
        for (i = 0; i < 4096 && top + i < items; ++i) {
            bounds.left = LVIR_BOUNDS;
            if (!SendMessageW(window, LVM_GETITEMRECT, (WPARAM)(top + i), (LPARAM)&bounds) ||
                !REPORT_GRID_VALID() || bounds.bottom <= bounds.top) __leave;
            if (bounds.top >= body.bottom) break;
            if (bounds.bottom <= body.top || bounds.bottom > body.bottom) continue;
            line = body; line.top = max(bounds.top, bounds.bottom - dip(r, 1)); line.bottom = bounds.bottom;
            FillRect(dc, &line, r->brushes[DLUMA_EDITOR_THEME_ROLE_BORDER]);
        }
    } __finally {
        if (saved) RestoreDC(dc, saved);
        resources_release(r);
        control_release(control);
    }
#undef REPORT_GRID_VALID
}

/* Custom-draw state can retain a highlight after the native row is deselected. */
static int listview_selected(HWND parent, HWND window, ThemedControl *child,
                           ThemeResources *resources, ULONG_PTR item, int *selected)
{
    LRESULT count, state;
    int valid = 0;
    if (!selected || item > 0x7FFFFFFFU || child->family != FAMILY_LISTVIEW ||
        GetParent(window) != parent || GetWindowThreadProcessId(window, NULL) != GetCurrentThreadId()) return 0;
    ++child->references;
    resources_retain(resources);
    __try {
        count = SendMessageW(window, LVM_GETITEMCOUNT, 0, 0);
        if (count <= 0 || item >= (ULONG_PTR)count ||
            GetPropW(window, control_property) != child || child->detached ||
            child->resources != resources || GetParent(window) != parent) __leave;
        state = SendMessageW(window, LVM_GETITEMSTATE, (WPARAM)item, LVIS_SELECTED);
        if ((state != 0 && state != LVIS_SELECTED) ||
            GetPropW(window, control_property) != child || child->detached ||
            child->resources != resources || GetParent(window) != parent) __leave;
        *selected = (state & LVIS_SELECTED) != 0;
        valid = 1;
    } __finally {
        resources_release(resources);
        control_release(child);
    }
    return valid;
}

static int theme_item_background(const ThemeResources *r, COLORREF value)
{
    return ordinary_background(r, value) ||
        value == color(r, DLUMA_EDITOR_THEME_ROLE_HOVER) ||
        value == color(r, DLUMA_EDITOR_THEME_ROLE_SELECTION) ||
        value == color(r, DLUMA_EDITOR_THEME_ROLE_INACTIVE_SELECTION);
}

/* Linear sRGB for byte channels, independent of the native math runtime. */
static const double linear_srgb[256] = {
    0.000000000000, 0.000303526984, 0.000607053967, 0.000910580951, 0.001214107934, 0.001517634918, 0.001821161901, 0.002124688885,
    0.002428215868, 0.002731742852, 0.003035269835, 0.003346535764, 0.003676507324, 0.004024717018, 0.004391442037, 0.004776953481,
    0.005181516702, 0.005605391624, 0.006048833023, 0.006512090793, 0.006995410187, 0.007499032043, 0.008023192985, 0.008568125618,
    0.009134058702, 0.009721217320, 0.010329823030, 0.010960094006, 0.011612245180, 0.012286488357, 0.012983032342, 0.013702083047,
    0.014443843596, 0.015208514423, 0.015996293366, 0.016807375753, 0.017641954488, 0.018500220128, 0.019382360957, 0.020288563057,
    0.021219010376, 0.022173884793, 0.023153366178, 0.024157632449, 0.025186859627, 0.026241221895, 0.027320891639, 0.028426039504,
    0.029556834438, 0.030713443733, 0.031896033073, 0.033104766571, 0.034339806809, 0.035601314875, 0.036889450401, 0.038204371595,
    0.039546235277, 0.040915196907, 0.042311410621, 0.043735029257, 0.045186204386, 0.046665086337, 0.048171824227, 0.049706565984,
    0.051269458374, 0.052860647023, 0.054480276442, 0.056128490050, 0.057805430191, 0.059511238163, 0.061246054232, 0.063010017653,
    0.064803266693, 0.066625938644, 0.068478169844, 0.070360095697, 0.072271850682, 0.074213568380, 0.076185381481, 0.078187421805,
    0.080219820314, 0.082282707130, 0.084376211544, 0.086500462037, 0.088655586286, 0.090841711183, 0.093058962847, 0.095307466631,
    0.097587347142, 0.099898728247, 0.102241733088, 0.104616484091, 0.107023102978, 0.109461710778, 0.111932427837, 0.114435373827,
    0.116970667759, 0.119538427988, 0.122138772230, 0.124771817561, 0.127437680436, 0.130136476690, 0.132868321554, 0.135633329655,
    0.138431615032, 0.141263291140, 0.144128470858, 0.147027266498, 0.149959789811, 0.152926151996, 0.155926463708, 0.158960835061,
    0.162029375639, 0.165132194502, 0.168269400190, 0.171441100733, 0.174647403656, 0.177888415984, 0.181164244250, 0.184474994500,
    0.187820772301, 0.191201682741, 0.194617830442, 0.198069319560, 0.201556253794, 0.205078736390, 0.208636870145, 0.212230757414,
    0.215860500114, 0.219526199729, 0.223227957317, 0.226965873510, 0.230740048524, 0.234550582161, 0.238397573812, 0.242281122466,
    0.246201326708, 0.250158284730, 0.254152094331, 0.258182852922, 0.262250657530, 0.266355604803, 0.270497791013, 0.274677312060,
    0.278894263477, 0.283148740430, 0.287440837727, 0.291770649818, 0.296138270798, 0.300543794416, 0.304987314070, 0.309468922818,
    0.313988713376, 0.318546778125, 0.323143209113, 0.327778098057, 0.332451536346, 0.337163615048, 0.341914424909, 0.346704056355,
    0.351532599500, 0.356400144146, 0.361306779784, 0.366252595599, 0.371237680474, 0.376262122991, 0.381326011433, 0.386429433787,
    0.391572477750, 0.396755230726, 0.401977779832, 0.407240211902, 0.412542613484, 0.417885070848, 0.423267669986, 0.428690496614,
    0.434153636175, 0.439657173841, 0.445201194516, 0.450785782838, 0.456411023180, 0.462076999654, 0.467783796112, 0.473531496148,
    0.479320183101, 0.485149940056, 0.491020849848, 0.496932995061, 0.502886458033, 0.508881320855, 0.514917665377, 0.520995573204,
    0.527115125706, 0.533276404011, 0.539479489012, 0.545724461370, 0.552011401512, 0.558340389634, 0.564711505705, 0.571124829465,
    0.577580440430, 0.584078417891, 0.590618840919, 0.597201788364, 0.603827338855, 0.610495570808, 0.617206562420, 0.623960391675,
    0.630757136346, 0.637596873994, 0.644479681971, 0.651405637420, 0.658374817279, 0.665387298282, 0.672443156958, 0.679542469633,
    0.686685312435, 0.693871761292, 0.701101891933, 0.708375779892, 0.715693500506, 0.723055128922, 0.730460740090, 0.737910408773,
    0.745404209540, 0.752942216776, 0.760524504675, 0.768151147248, 0.775822218317, 0.783537791526, 0.791297940333, 0.799102738014,
    0.806952257669, 0.814846572216, 0.822785754396, 0.830769876775, 0.838799011741, 0.846873231510, 0.854992608124, 0.863157213454,
    0.871367119199, 0.879622396888, 0.887923117882, 0.896269353374, 0.904661174391, 0.913098651793, 0.921581856277, 0.930110858375,
    0.938685728458, 0.947306536733, 0.955973353249, 0.964686247894, 0.973445290398, 0.982250550333, 0.991102097114, 1.000000000000,
};

static double color_luminance(COLORREF value)
{
    return linear_srgb[GetRValue(value)] * 0.2126 +
        linear_srgb[GetGValue(value)] * 0.7152 +
        linear_srgb[GetBValue(value)] * 0.0722;
}

static double gray_contrast(unsigned int gray, double background)
{
    double foreground = color_luminance(RGB(gray, gray, gray));
    return foreground > background ? (foreground + 0.05) / (background + 0.05) :
        (background + 0.05) / (foreground + 0.05);
}

static COLORREF readable_gray_text(COLORREF foreground, COLORREF background)
{
    unsigned int low, high, middle, gray = GetRValue(foreground);
    double luminance;
    if ((foreground & 0xFF000000U) || (background & 0xFF000000U) ||
        gray != GetGValue(foreground) || gray != GetBValue(foreground)) return foreground;
    luminance = color_luminance(background);
    if (gray_contrast(gray, luminance) >= 4.5) return foreground;
    if (gray_contrast(0, luminance) >= gray_contrast(255, luminance)) {
        low = 0; high = gray;
        while (low < high) {
            middle = (low + high + 1U) / 2U;
            if (gray_contrast(middle, luminance) >= 4.5) low = middle;
            else high = middle - 1U;
        }
    } else {
        low = gray; high = 255;
        while (low < high) {
            middle = (low + high) / 2U;
            if (gray_contrast(middle, luminance) >= 4.5) high = middle;
            else low = middle + 1U;
        }
    }
    return RGB(low, low, low);
}

static int custom_draw(HWND window, LPARAM lparam, LRESULT *result)
{
    NMCUSTOMDRAW *draw = (NMCUSTOMDRAW *)lparam;
    ThemedControl *child;
    ThemeResources *r;
    unsigned int background, foreground;
    int is_picker, selected;
    if (!draw || draw->hdr.code != NM_CUSTOMDRAW) return 0;
    if (dluma_editor_theme_properties_custom_draw(window,lparam,result)) return 0;
    child = (ThemedControl *)GetPropW(draw->hdr.hwndFrom, control_property);
    if (!child || !(r = child->resources)) return 0;
    if (child->family==FAMILY_TOOLBAR) toolbar_custom_draw(child,draw,result);
    if (*result & CDRF_SKIPDEFAULT) return 0;
    if (draw->dwDrawStage == CDDS_PREPAINT) {
        *result |= CDRF_NOTIFYITEMDRAW;
        if (child->family == FAMILY_LISTVIEW && child->report_grid_owned)
            *result |= CDRF_NOTIFYPOSTPAINT;
        return 0;
    }
    if (draw->dwDrawStage == CDDS_POSTPAINT && child->family == FAMILY_LISTVIEW && child->report_grid_owned) {
        report_grid_paint(draw->hdr.hwndFrom, child, draw->hdc);
        return 0;
    }
    if (draw->dwDrawStage == CDDS_ITEMPOSTPAINT && child->family == FAMILY_TOOLBAR) {
        if (draw->uItemState & CDIS_FOCUS) { RECT focus = draw->rc; InflateRect(&focus, -dip(r, 2), -dip(r, 2)); DrawFocusRect(draw->hdc, &focus); }
        return 0;
    }
    if (draw->dwDrawStage != CDDS_ITEMPREPAINT &&
        draw->dwDrawStage != (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) return 0;
    is_picker = picker_listview(window, draw->hdr.hwndFrom, child);
    selected = (draw->uItemState & CDIS_SELECTED) != 0;
    if (child->family == FAMILY_LISTVIEW && !listview_selected(window, draw->hdr.hwndFrom, child, r,
                                     draw->dwItemSpec, &selected)) return 0;
    background = DLUMA_EDITOR_THEME_ROLE_SURFACE;
    foreground = DLUMA_EDITOR_THEME_ROLE_TEXT;
    if (selected) {
        int active = GetFocus() == draw->hdr.hwndFrom;
        background = active ? DLUMA_EDITOR_THEME_ROLE_SELECTION : DLUMA_EDITOR_THEME_ROLE_INACTIVE_SELECTION;
        foreground = active || r->theme.kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST ? DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT;
    } else if (draw->uItemState & CDIS_HOT) background = DLUMA_EDITOR_THEME_ROLE_HOVER;
    else if (child->family == FAMILY_LISTVIEW && (draw->dwItemSpec & 1U)) background = DLUMA_EDITOR_THEME_ROLE_ALTERNATE;
    if ((draw->uItemState & (CDIS_DISABLED | CDIS_GRAYED)) ||
        (is_picker && !IsWindowEnabled(draw->hdr.hwndFrom))) foreground = DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT;
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
        int state_color=selected || (draw->uItemState & (CDIS_DISABLED | CDIS_GRAYED))!=0;
        if (is_picker || state_color || list->clrTextBk==CLR_DEFAULT || list->clrTextBk==CLR_NONE ||
            theme_item_background(r,list->clrTextBk)) {
            if (is_picker || state_color || list->clrText==CLR_DEFAULT || ordinary_foreground(r,list->clrText) ||
                list->clrText==color(r,DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT))
                list->clrText=color(r,foreground);
            list->clrTextBk=color(r,background);
        } else if (!selected && r->theme.kind != DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST &&
            !(draw->uItemState & (CDIS_DISABLED | CDIS_GRAYED)) && IsWindowEnabled(draw->hdr.hwndFrom)) {
            /* Keep native cell indicators while making neutral labels readable. */
            list->clrText=readable_gray_text(list->clrText,list->clrTextBk);
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

static void paint_control_border(HWND window, ThemedControl *control)
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
    if (!(style & WS_CHILD) || !((style & WS_BORDER) || (exstyle & WS_EX_CLIENTEDGE) ||
        (control->family == FAMILY_BUTTON && (style & BS_TYPEMASK) == BS_GROUPBOX &&
         (exstyle & (WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME)))) ||
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
    FillRect(dc, &frame, r->brushes[control->family == FAMILY_EDIT &&
        GetFocus() == window && IsWindowEnabled(window) ?
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

static int standalone_scrollbar(HWND window, ThemedControl *control, ThemeResources *resources)
{
    return control && !control->detached && control->family == FAMILY_SCROLLBAR &&
        GetPropW(window, control_property) == control && control->resources == resources && resources &&
        resources->theme.kind != DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST &&
        !(GetWindowLongPtrW(window, GWL_STYLE) & (SBS_SIZEBOX | SBS_SIZEGRIP));
}

static int draw_scrollbar_client(HWND window, HDC dc, const RECT *client,
    const SCROLLBARINFO *info, int vertical, ThemeResources *resources)
{
    RECT track = info->rcScrollBar, thumb, arrow;
    HPEN pen = NULL;
    HGDIOBJ previous = NULL;
    int end, ok = 0, enabled = IsWindowEnabled(window);
    SetLastError(ERROR_SUCCESS);
    if (!MapWindowPoints(NULL, window, (POINT *)&track, 2) && GetLastError() != ERROR_SUCCESS) return 0;
    if (track.left < client->left || track.top < client->top || track.right > client->right ||
        track.bottom > client->bottom || IsRectEmpty(&track) || info->dxyLineButton < 0) return 0;
    if (!FillRect(dc, client, resources->brushes[DLUMA_EDITOR_THEME_ROLE_PANEL])) return 0;
    thumb = track;
    if (vertical) {
        thumb.top += info->xyThumbTop; thumb.bottom = track.top + info->xyThumbBottom;
        InflateRect(&thumb, -dip(resources, 3), 0);
    } else {
        thumb.left += info->xyThumbTop; thumb.right = track.left + info->xyThumbBottom;
        InflateRect(&thumb, 0, -dip(resources, 3));
    }
    if (!IsRectEmpty(&thumb) && !(info->rgstate[3] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN))) {
        unsigned int role = !enabled || (info->rgstate[3] & STATE_SYSTEM_UNAVAILABLE) ?
            DLUMA_EDITOR_THEME_ROLE_BORDER : (info->rgstate[3] & STATE_SYSTEM_PRESSED) ?
            DLUMA_EDITOR_THEME_ROLE_ACCENT : DLUMA_EDITOR_THEME_ROLE_MUTED_TEXT;
        if (!FillRect(dc, &thumb, resources->brushes[role])) return 0;
        if (enabled && GetFocus() == window && !DrawFocusRect(dc, &thumb)) return 0;
    }
    arrow = track;
    if (vertical) arrow.bottom = min(arrow.bottom, arrow.top + info->dxyLineButton);
    else arrow.right = min(arrow.right, arrow.left + info->dxyLineButton);
    for (end = 0; end < 2; ++end) {
        DWORD state = info->rgstate[end ? 5 : 1];
        int x = (arrow.left + arrow.right) / 2, y = (arrow.top + arrow.bottom) / 2;
        int direction = end ? 1 : -1, delta = dip(resources, 3);
        if ((state & STATE_SYSTEM_PRESSED) && !FillRect(dc, &arrow,
            resources->brushes[DLUMA_EDITOR_THEME_ROLE_HOVER])) goto done;
        pen = CreatePen(PS_SOLID, dip(resources, 1), color(resources,
            !enabled || (state & STATE_SYSTEM_UNAVAILABLE) ? DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT :
            DLUMA_EDITOR_THEME_ROLE_MUTED_TEXT));
        if (!pen) goto done;
        previous = SelectObject(dc, pen);
        if (!previous || previous == HGDI_ERROR) { previous = NULL; goto done; }
        if (vertical) {
            if (!MoveToEx(dc, x - delta, y - direction * delta / 2, NULL) ||
                !LineTo(dc, x, y + direction * delta / 2) ||
                !LineTo(dc, x + delta, y - direction * delta / 2)) goto done;
            OffsetRect(&arrow, 0, track.bottom - track.top - info->dxyLineButton);
        } else {
            if (!MoveToEx(dc, x - direction * delta / 2, y - delta, NULL) ||
                !LineTo(dc, x + direction * delta / 2, y) ||
                !LineTo(dc, x - direction * delta / 2, y + delta)) goto done;
            OffsetRect(&arrow, track.right - track.left - info->dxyLineButton, 0);
        }
        SelectObject(dc, previous); previous = NULL;
        DeleteObject(pen); pen = NULL;
    }
    ok = 1;
done:
    if (previous) SelectObject(dc, previous);
    if (pen) DeleteObject(pen);
    return ok;
}

static int paint_scrollbar_client(HWND window, ThemedControl *control, ThemeResources *resources)
{
    SCROLLBARINFO info;
    PAINTSTRUCT paint;
    RECT client;
    HDC dc = NULL, buffer = NULL;
    HBITMAP bitmap = NULL;
    HGDIOBJ previous = NULL;
    int painted = 0, width, height, vertical;
    if (!standalone_scrollbar(window, control, resources) || !GetClientRect(window, &client)) return 0;
    width = client.right - client.left; height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return 0;
    resources_retain(resources);
    ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
    if (!GetScrollBarInfo(window, OBJID_CLIENT, &info) ||
        !standalone_scrollbar(window, control, resources)) goto finished;
    vertical = (GetWindowLongPtrW(window, GWL_STYLE) & SBS_VERT) != 0;
    ZeroMemory(&paint, sizeof(paint));
    dc = BeginPaint(window, &paint);
    if (!dc) { restore_paint_damage(window, control, &paint); goto finished; }
    buffer = CreateCompatibleDC(dc);
    if (!buffer) goto done;
    bitmap = CreateCompatibleBitmap(dc, width, height);
    if (!bitmap) goto done;
    previous = SelectObject(buffer, bitmap);
    if (!previous || previous == HGDI_ERROR) { previous = NULL; goto done; }
    if (!standalone_scrollbar(window, control, resources) ||
        !draw_scrollbar_client(window, buffer, &client, &info, vertical, resources) ||
        !standalone_scrollbar(window, control, resources)) goto done;
    painted = BitBlt(dc, client.left, client.top, width, height, buffer, 0, 0, SRCCOPY) != 0;
done:
    if (previous) SelectObject(buffer, previous);
    if (bitmap) DeleteObject(bitmap);
    if (buffer) DeleteDC(buffer);
    if (painted) EndPaint(window, &paint);
    else restore_paint_damage(window, control, &paint);
finished:
    resources_release(resources);
    return painted;
}

static int scrollbar_state_message(HWND window, ThemedControl *control, ThemeResources *resources,
    UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result)
{
    UINT forwarded = message;
    if (!standalone_scrollbar(window, control, resources) || control->scrollbar_forwarding) return 0;
    if (message == SBM_SETSCROLLINFO && wparam) wparam = FALSE;
    else if (message == SBM_SETPOS && lparam) lparam = FALSE;
    else if (message == SBM_SETRANGEREDRAW) forwarded = SBM_SETRANGE;
    else return 0;
    /* Keep the native range, position, notifications and return value. Only
       its immediate drawing is replaced by the next buffered paint. */
    resources_retain(resources);
    ++control->scrollbar_forwarding;
    __try {
        *result = SendMessageW(window, forwarded, wparam, lparam);
        if (standalone_scrollbar(window, control, resources)) InvalidateRect(window, NULL, FALSE);
    } __finally {
        --control->scrollbar_forwarding;
        resources_release(resources);
    }
    return 1;
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
    if (control && stage==DLUMA_EDITOR_UI_AFTER && message==WM_STYLECHANGED &&
        (INT_PTR)wparam==GWL_EXSTYLE && lparam && !control->edge_sending) {
        STYLESTRUCT *change=(STYLESTRUCT *)lparam;
        if ((change->styleOld ^ change->styleNew) & WS_EX_CLIENTEDGE) {
            control->edge_owned=0; control->edge_external=1;
        }
    }
    if (control && stage==DLUMA_EDITOR_UI_AFTER && message==WM_STYLECHANGED &&
        (INT_PTR)wparam==GWL_STYLE && lparam && !control->header_clip_sending) {
        STYLESTRUCT *change=(STYLESTRUCT *)lparam;
        if ((change->styleOld ^ change->styleNew) & WS_CLIPSIBLINGS) {
            control->header_clip_owned=0; control->header_clip_external=1;
        }
    }
    if (control && stage==DLUMA_EDITOR_UI_AFTER &&
        (message==WM_STYLECHANGED || (control->family==FAMILY_EDIT && message==EM_SETREADONLY)))
        control->style=GetWindowLongPtrW(window,GWL_STYLE);
    if (control && stage==DLUMA_EDITOR_UI_AFTER && message==WM_STYLECHANGED &&
        (INT_PTR)wparam==GWL_STYLE && control->edge_owned && !control->edge_sending) {
        control->colors_dirty=1;
        if (r) { DlumaEditorThemeResolved theme=r->theme; dluma_editor_theme_controls_apply(window,&theme); }
    }
    if (stage == DLUMA_EDITOR_UI_AFTER && message == WM_NOTIFY)
        return custom_draw(window, lparam, result);
    if (!r) return 0;
    if (stage == DLUMA_EDITOR_UI_AFTER && message == WM_INITDIALOG && control &&
        control->family == FAMILY_DIALOG) {
        HWND child = GetDlgItem(window, 3181);
        if (child && hints_window(child) && dluma_editor_theme_controls_attach(child))
            dluma_editor_theme_controls_apply(child, &r->theme);
    }
    if (control && control->family == FAMILY_HINTS && !control->hints_busy) {
        if (stage == DLUMA_EDITOR_UI_BEFORE && message == EM_STREAMIN && (wparam & SF_RTF)) {
            ++control->hints_stream_depth;
            if (control->hints_stream_depth != 1 || !control->hints_foreground_owned) return 0;
            control->hints_busy = 1;
            control->hints_frozen_document = dluma_editor_theme_hints_freeze(window, control);
            if (!hints_alive(window, control)) {
                if (GetPropW(window, detaching_property) != control) {
                    dluma_editor_theme_hints_unfreeze(control->hints_frozen_document, 1);
                    control->hints_frozen_document = NULL;
                }
                control->hints_busy = 0;
                hints_finish_pending(window, control);
                return 0;
            }
            if (!control->hints_frozen_document && control->hints_background_owned) {
                (void)SendMessageW(window, EM_SETBKGNDCOLOR, control->hints_background_system ? TRUE : FALSE,
                    control->hints_background_system ? 0 : (LPARAM)control->hints_original_background);
                control->hints_background_owned = 0;
            }
            if (dluma_editor_theme_hints_foreground(window, control, control->hints_foreground, 0, 0, 1))
                control->hints_foreground_owned = 0;
            else if (hints_alive(window, control) && control->hints_background_owned) {
                (void)SendMessageW(window, EM_SETBKGNDCOLOR, control->hints_background_system ? TRUE : FALSE,
                    control->hints_background_system ? 0 : (LPARAM)control->hints_original_background);
                control->hints_background_owned = 0;
            }
            control->hints_busy = 0;
            hints_finish_pending(window, control);
        } else if (stage == DLUMA_EDITOR_UI_AFTER && message == EM_STREAMIN && (wparam & SF_RTF)) {
            EDITSTREAM *stream = (EDITSTREAM *)lparam;
            int success = 0;
            __try { success = stream && stream->dwError == 0; } __except(EXCEPTION_EXECUTE_HANDLER) { success = 0; }
            if (control->hints_stream_depth) --control->hints_stream_depth;
            if (!control->hints_stream_depth) {
                if (success && hints_alive(window, control)) hints_apply(window, control, r);
                else if (hints_alive(window, control) && control->hints_background_owned) {
                    control->hints_busy = 1;
                    (void)SendMessageW(window, EM_SETBKGNDCOLOR, control->hints_background_system ? TRUE : FALSE,
                        control->hints_background_system ? 0 : (LPARAM)control->hints_original_background);
                    control->hints_background_owned = 0;
                    control->hints_busy = 0;
                    hints_finish_pending(window, control);
                }
                if (hints_alive(window, control) && control->hints_frozen_document) {
                    void *frozen = control->hints_frozen_document;
                    control->hints_frozen_document = NULL;
                    dluma_editor_theme_hints_unfreeze(frozen, 0);
                }
            }
        } else if (stage == DLUMA_EDITOR_UI_AFTER && message == EM_SETBKGNDCOLOR) {
            control->hints_background_system = wparam != 0;
            control->hints_original_background = (COLORREF)lparam;
            control->hints_background_owned = 1;
            hints_apply(window, control, r);
        } else if (stage == DLUMA_EDITOR_UI_AFTER && message == EM_SETREADONLY && wparam) {
            hints_apply(window, control, r);
        }
    }
    if (control && stage==DLUMA_EDITOR_UI_AFTER && message==WM_CREATE && !control->applying &&
        (control->family==FAMILY_LISTVIEW || control->family==FAMILY_TREE)) {
        DlumaEditorThemeResolved theme=r->theme;
        /* Native creation initializes its own colors after early enrollment. */
        control->colors_dirty=1;
        dluma_editor_theme_controls_apply(window,&theme);
        return 0;
    }
    if (control && stage==DLUMA_EDITOR_UI_AFTER && control->family==FAMILY_LISTVIEW &&
        message==LVM_SETEXTENDEDLISTVIEWSTYLE && control->properties_grid_known &&
        !control->properties_grid_sending &&
        (!wparam || (wparam & (LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER)))) {
        if (!wparam || (wparam & LVS_EX_GRIDLINES))
            control->properties_grid_wanted=((DWORD)lparam & LVS_EX_GRIDLINES)!=0;
        if (!wparam || (wparam & LVS_EX_DOUBLEBUFFER))
            control->properties_buffer_wanted=((DWORD)lparam & LVS_EX_DOUBLEBUFFER)!=0;
        control->properties_grid_failed=0;
        if (control->properties_grid_owned && !control->properties_grid_wanted)
            dluma_editor_theme_properties_grid_restore(window,control);
        else if (control->properties_grid_owned && r->theme.kind!=DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST) {
            ++control->properties_grid_sending;
            (void)ListView_SetExtendedListViewStyleEx(window,LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER,LVS_EX_DOUBLEBUFFER);
            --control->properties_grid_sending;
        }
    }
    if (control && stage==DLUMA_EDITOR_UI_AFTER && !control->applying &&
        control->family==FAMILY_LISTVIEW && message==LVM_SETEXTENDEDLISTVIEWSTYLE &&
        report_listview(window, control)) {
        DlumaEditorThemeResolved theme=r->theme;
        if (!wparam || (wparam & LVS_EX_GRIDLINES))
            control->report_grid_wanted=((DWORD)lparam & LVS_EX_GRIDLINES)!=0;
        control->colors_dirty=1;
        dluma_editor_theme_controls_apply(window,&theme);
        return 0;
    }
    if (control && stage==DLUMA_EDITOR_UI_AFTER && !control->applying &&
        control->family==FAMILY_LISTVIEW && (message==WM_STYLECHANGED || message==LVM_SETVIEW) &&
        (control->report_grid_owned || report_listview(window, control))) {
        DlumaEditorThemeResolved theme=r->theme;
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
    if (standalone_scrollbar(window, control, r)) {
        if (stage == DLUMA_EDITOR_UI_BEFORE) {
            if (scrollbar_state_message(window, control, r, message, wparam, lparam, result)) return 1;
            if (message == WM_ERASEBKGND) { *result = 1; return 1; }
            if (message == WM_PAINT && paint_scrollbar_client(window, control, r)) { *result = 0; return 1; }
        } else if (stage == DLUMA_EDITOR_UI_AFTER &&
            (message == WM_SIZE || message == WM_ENABLE || message == SBM_ENABLE_ARROWS ||
             message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_STYLECHANGED ||
             message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
             message == WM_CAPTURECHANGED || message == WM_CANCELMODE ||
             message == WM_KEYDOWN || message == WM_KEYUP)) {
            InvalidateRect(window, NULL, FALSE);
        }
    }
    if (stage == DLUMA_EDITOR_UI_AFTER && control->family != FAMILY_SCROLLBAR &&
        (message == WM_NCPAINT || message == WM_HSCROLL || message == WM_VSCROLL ||
         message == WM_SIZE)) {
        LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
        if (control->family == FAMILY_EDIT || control->family == FAMILY_LISTVIEW ||
            control->family == FAMILY_TREE || control->family == FAMILY_LIST ||
            control->family == FAMILY_COMBO || control->family == FAMILY_TAB ||
            control->family == FAMILY_STATIC ||
            control->family == FAMILY_WORKSPACE ||
            (control->family == FAMILY_BUTTON && (style & BS_TYPEMASK) == BS_GROUPBOX))
            if (!control->edge_owned) paint_control_border(window, control);
        if (style & WS_VSCROLL) paint_scrollbar(window, control, OBJID_VSCROLL, style);
        if (style & WS_HSCROLL) paint_scrollbar(window, control, OBJID_HSCROLL, style);
    }
    if (stage == DLUMA_EDITOR_UI_AFTER && control->family == FAMILY_EDIT && !control->edge_owned &&
        (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE))
        paint_control_border(window, control);
    if (stage != DLUMA_EDITOR_UI_BEFORE) return 0;
    if (message==WM_ERASEBKGND && control->opaque_erase_active &&
        (control->family==FAMILY_BUTTON || control->family==FAMILY_COMBO)) { *result=1; return 1; }
    if (message==WM_ERASEBKGND && control->family==FAMILY_BUTTON &&
        dluma_editor_theme_properties_boolean_erase(window)) { *result=1; return 1; }
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
        ToolbarScope *scope=control->family==FAMILY_TOOLBAR ? toolbar_paint_scope(control) : NULL;
        if (scope && GetObjectType((HDC)wparam)==OBJ_DC) {
            scope->erase_suppressed=1; *result=1; return 1;
        }
        RECT rect; GetClientRect(window, &rect);
        FillRect((HDC)wparam, &rect, r->brushes[control->family == FAMILY_TOOLBAR ?
            DLUMA_EDITOR_THEME_ROLE_PANEL : DLUMA_EDITOR_THEME_ROLE_WINDOW]);
        *result = 1; return 1;
    }
    if (message==WM_PAINT && control->family==FAMILY_BUTTON && !control->boolean_print_busy &&
        !(control->boolean_print_failed && control->boolean_print_failed_generation==r->theme.generation)) {
        int painted;
        control->boolean_print_busy=1;
        __try { painted=dluma_editor_theme_properties_paint_boolean(window,&r->theme); }
        __finally {
            control->boolean_print_busy=0;
            if (AbnormalTermination()) {
                control->boolean_print_failed=1;
                control->boolean_print_failed_generation=r->theme.generation;
            }
        }
        if (painted>0) { *result=0; return 1; }
        if (painted<0) { control->boolean_print_failed=1; control->boolean_print_failed_generation=r->theme.generation; }
    }
    if (message == WM_PAINT && control->family == FAMILY_BUTTON &&
        paint_button(window, control)) { *result = 0; return 1; }
    if (message == WM_PAINT && control->family == FAMILY_STATIC &&
        paint_static_frame(window, control)) { *result = 0; return 1; }
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
    uint64_t *invocation = (message == WM_NCDESTROY || message == WM_DRAWITEM ||
        message == WM_PAINT || message == WM_NCPAINT) ?
        dluma_editor_ui_invocation_slot(DLUMA_EDITOR_UI_OWNER_THEME, DLUMA_EDITOR_UI_SLOT_CONTROLS) : NULL;
    ThemeResources *resources = NULL;
    int handled;
    /* Property geometry queries can dispatch nested window messages. Retain
       the control before those queries, including their cleanup callbacks. */
    if (control) {
        ++control->references;
        resources = control->resources;
        resources_retain(resources);
    }
    if (control && stage==DLUMA_EDITOR_UI_BEFORE && message==WM_NCDESTROY)
        toolbar_restore_active(control);
    if (!control && stage == DLUMA_EDITOR_UI_BEFORE && message == WM_NCDESTROY) {
        ThemedControl *detaching = (ThemedControl *)GetPropW(window, detaching_property);
        if (detaching && detaching->family == FAMILY_HINTS) {
            void *frozen = detaching->hints_frozen_document;
            detaching->hints_frozen_document = NULL;
            RemovePropW(window, detaching_property);
            dluma_editor_theme_hints_unfreeze(frozen, 1);
        }
    }
    dluma_editor_theme_properties_message(window,stage,message,lparam,
        resources ? &resources->theme : NULL);
    if (stage == DLUMA_EDITOR_UI_AFTER && message == WM_NCDESTROY && invocation && *invocation) {
        ThemedControl *destroyed = (ThemedControl *)(ULONG_PTR)*invocation;
        *invocation = 0;
        control_release(destroyed);
    }
    if (control) {
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
    if (stage==DLUMA_EDITOR_UI_BEFORE && (message==WM_PAINT || message==WM_NCPAINT))
        toolbar_scope_before(window,control,message,invocation);
    handled = controls_message_impl(window, stage, message, wparam, lparam, result, resources);
    if (stage==DLUMA_EDITOR_UI_AFTER && (message==WM_PAINT || message==WM_NCPAINT))
        toolbar_scope_after(invocation);
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
    toolbar_restore_active(control);
    control->detached = 1;
    RemovePropW(window, control_property);
    menu_release(window, NULL);
    if (destroying && control->hints_frozen_document) {
        void *frozen = control->hints_frozen_document;
        control->hints_frozen_document = NULL;
        dluma_editor_theme_hints_unfreeze(frozen, destroying);
    }
    if (destroying) { control_release(control); return; }
    if (control->applying || control->hints_busy) { control->pending_detach = 1; return; }
    control_finish_detach(window, control);
}

static void control_finish_detach(HWND window, ThemedControl *control)
{
    ThemeResources *r = control->resources, *previous = control->previous_resources;
#define DETACH_VALID() (GetPropW(window, detaching_property) == control)
    if (!DETACH_VALID()) goto done;
    edge_restore(window,control,1);
    header_clip_restore(window,control,1);
    if (control->family == FAMILY_HINTS) hints_restore(window, control);
    if (control->hints_frozen_document) {
        void *frozen = control->hints_frozen_document;
        control->hints_frozen_document = NULL;
        dluma_editor_theme_hints_unfreeze(frozen, !DETACH_VALID());
    }
    if (control->font_owned && r) {
        HFONT current = (HFONT)SendMessageW(window, WM_GETFONT, 0, 0);
        if (DETACH_VALID() && (current == r->font || (previous && current == previous->font)))
            (void)SendMessageW(window, WM_SETFONT, (WPARAM)control->original_font, FALSE);
    }
    if (DETACH_VALID() && control->colors_owned && r) {
        if (control->family == FAMILY_LISTVIEW) {
            if (DETACH_VALID() && control->properties_grid_owned) {
                DWORD style=ListView_GetExtendedListViewStyle(window);
                DWORD mask=LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER;
                DWORD wanted=(control->properties_grid_wanted ? LVS_EX_GRIDLINES : 0) |
                    (control->properties_buffer_wanted ? LVS_EX_DOUBLEBUFFER : 0);
                if (DETACH_VALID() && (style & mask)!=wanted)
                    (void)ListView_SetExtendedListViewStyleEx(window,mask,wanted);
            }
            if (DETACH_VALID() && control->report_grid_owned && control->report_grid_wanted &&
                !(ListView_GetExtendedListViewStyle(window) & LVS_EX_GRIDLINES) && DETACH_VALID())
                (void)ListView_SetExtendedListViewStyleEx(window, LVS_EX_GRIDLINES, LVS_EX_GRIDLINES);
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
