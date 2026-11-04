#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <stdint.h>
#include <intrin.h>
#include "dlw_editor_theme_properties.h"
#include "dlw_editor_theme_profile.h"
#include "../build/generated/editor-theme/editor_theme_palette.generated.h"

/* Exact Editor property lists embedded in the Options and Fields pages. */
#define PROPERTIES_DIALOG_VTABLE 0x1571E18U
#define PROPERTIES_FIELDS_VTABLE 0x154C0B8U
#define PROPERTIES_LIST_VTABLE 0x15B82B8U
#define PROPERTIES_LIST_OFFSET 0x1F8U
#define PROPERTIES_LIST_ID 1303
#define PROPERTIES_FIELDS_OFFSET 0x248U
#define PROPERTIES_FIELDS_ID 1077
#define PROPERTIES_BRUSH_IAT 0x1146980U
#define PROPERTIES_REGION_IAT 0x11451E8U
#define BOOLEAN_VTABLE 0x15BFE78U
#define BOOLEAN_DRAW_RVA 0x514600U
#define BOOLEAN_RECT_IAT 0x11451B0U
#define BOOLEAN_TEXT_IAT 0x2BE1D8U
#define PROPERTIES_COLOR_COUNT 10U
static const unsigned int color_offsets[PROPERTIES_COLOR_COUNT]={616U,620U,624U,628U,632U,636U,640U,644U,656U,660U};

typedef void *(__cdecl *PropertiesPermanentFn)(HWND);
typedef struct PropertiesScope {
    struct PropertiesScope *previous;
    HWND owner, child;
    HDC dc;
    BYTE *object;
    void *control_token;
    COLORREF original[PROPERTIES_COLOR_COUNT], applied[PROPERTIES_COLOR_COUNT];
    COLORREF surface;
    COLORREF button_text, button_disabled, button_border;
    HBRUSH button_brush;
    HPEN button_pen;
    HRGN paint_damage;
    uint64_t paint_generation;
    int kind, rectangle_seen, text_seen, paint_post_done, paint_content_failed;
    int fields_active;
    HDC boolean_buffer;
    int boolean_erase, boolean_rectangle_done, boolean_text_done;
} PropertiesScope;
extern void *dluma_editor_theme_controls_hold_listview(HWND window);
extern void *dluma_editor_theme_controls_hold_button(HWND window);
extern int dluma_editor_theme_controls_listview_alive(HWND window, void *token);
extern int dluma_editor_theme_controls_token_generation(HWND window, void *token, uint64_t generation);
extern void dluma_editor_theme_controls_release_listview(void *token);
extern int dluma_editor_theme_properties_grid_valid(HWND, void *, uint64_t);
extern int dluma_editor_theme_properties_grid_begin(HWND, void *, uint64_t);
extern void dluma_editor_theme_properties_grid_fail(HWND, void *, uint64_t);
static SRWLOCK properties_lock;
static DWORD properties_tls=TLS_OUT_OF_INDEXES, properties_protection;
static BYTE *editor_base;
static void *dialog_vtable, *fields_vtable, *list_vtable;
static PropertiesPermanentFn permanent_lookup;
static PVOID volatile *properties_slot;
static HBRUSH (WINAPI *original_brush)(int);
static int properties_resolved, properties_installed, properties_pending, properties_stopping;
static volatile LONG properties_callbacks, properties_frames, properties_tls_failed;
typedef struct PropertiesIat {
    PVOID volatile *slot;
    void *original, *replacement;
    DWORD protection;
    int installed, pending;
} PropertiesIat;
static PropertiesIat boolean_rectangle_iat, boolean_text_iat;
static PropertiesIat properties_region_iat;
static BOOL (WINAPI *original_rectangle)(HDC,int,int,int,int);
static COLORREF (WINAPI *original_text_color)(HDC,COLORREF);
static HRGN (WINAPI *original_rect_region)(const RECT *);
static BYTE *mfc_base;
static void *boolean_vtable, *boolean_draw;

static int memory_readable(const void *pointer, size_t size)
{
    MEMORY_BASIC_INFORMATION info; uintptr_t p=(uintptr_t)pointer;
    return pointer && size && p<=UINTPTR_MAX-size && VirtualQuery(pointer,&info,sizeof(info)) &&
        info.State==MEM_COMMIT && !(info.Protect & (PAGE_GUARD|PAGE_NOACCESS)) &&
        p+size<=(uintptr_t)info.BaseAddress+info.RegionSize;
}
static int memory_writable(const void *pointer, size_t size)
{
    MEMORY_BASIC_INFORMATION info; DWORD protection;
    if (!memory_readable(pointer,size) || !VirtualQuery(pointer,&info,sizeof(info))) return 0;
    protection=info.Protect & 0xffU;
    return protection==PAGE_READWRITE || protection==PAGE_WRITECOPY ||
        protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
static PropertiesScope *properties_raw(void)
{
    return properties_tls==TLS_OUT_OF_INDEXES ? NULL : (PropertiesScope *)TlsGetValue(properties_tls);
}
static int properties_tls_ready(void)
{
    int ready;
    AcquireSRWLockExclusive(&properties_lock);
    if (!properties_stopping && properties_tls==TLS_OUT_OF_INDEXES) properties_tls=TlsAlloc();
    ready=!properties_stopping && properties_tls!=TLS_OUT_OF_INDEXES;
    ReleaseSRWLockExclusive(&properties_lock);
    return ready;
}
static PropertiesScope *properties_unmask(PropertiesScope *scope)
{
    return (PropertiesScope *)((uintptr_t)scope & ~(uintptr_t)1U);
}
static COLORREF role(const DlumaEditorThemeResolved *theme, unsigned int index)
{
    uint32_t c=theme->argb[index]; return RGB((c>>16)&255U,(c>>8)&255U,c&255U);
}
static int properties_dc_translation(HDC dc, POINT *origin)
{
    POINT basis[3]={{0,0},{1,0},{0,1}};
    if (!dc || GetMapMode(dc)!=MM_TEXT || GetGraphicsMode(dc)!=GM_COMPATIBLE ||
        !LPtoDP(dc,basis,3) || (int64_t)basis[1].x-basis[0].x!=1 ||
        basis[1].y!=basis[0].y || basis[2].x!=basis[0].x ||
        (int64_t)basis[2].y-basis[0].y!=1) return 0;
    *origin=basis[0]; return 1;
}
static void properties_clip_failed(PropertiesScope *draw)
{
    PropertiesScope *scope=draw;
    while (scope) {
        if (scope->kind==3 && scope->child==draw->child && scope->object==draw->object) {
            scope->paint_content_failed=1; return;
        }
        scope=properties_unmask(scope->previous);
    }
}
static HRGN WINAPI properties_rect_region(const RECT *rect)
{
    uintptr_t site=(uintptr_t)_ReturnAddress()-(uintptr_t)editor_base;
    PropertiesScope *scope=properties_raw(); RECT device; POINT origin;
    HRGN result;
    InterlockedIncrement(&properties_callbacks);
    __try {
        if (!InterlockedCompareExchange(&properties_tls_failed,0,0) && scope &&
            !((uintptr_t)scope&1U) && scope->kind==1 &&
            (site==0x4E6B82U || site==0x4E6F6CU) && GetObjectType(scope->dc)==OBJ_MEMDC) {
            /* Native row rectangles use client coordinates; GDI regions use
               device coordinates even when the list's buffer has an origin. */
            if (memory_readable(rect,sizeof(*rect)) && properties_dc_translation(scope->dc,&origin)) {
                device=*rect;
                if (LPtoDP(scope->dc,(POINT *)&device,2)) rect=&device;
                else properties_clip_failed(scope);
            } else properties_clip_failed(scope);
        }
        result=original_rect_region(rect);
    } __finally {
        InterlockedDecrement(&properties_callbacks);
    }
    return result;
}
static HBRUSH WINAPI properties_brush(int index)
{
    uintptr_t site=(uintptr_t)_ReturnAddress()-(uintptr_t)editor_base;
    PropertiesScope *scope=properties_raw(); HBRUSH brush=NULL;
    InterlockedIncrement(&properties_callbacks);
    if (!InterlockedCompareExchange(&properties_tls_failed,0,0) && scope && !((uintptr_t)scope&1U) && scope->kind==1 &&
        index==COLOR_WINDOW && (site==0x4E6A83U || site==0x4E6B1EU))
        brush=CreateSolidBrush(scope->surface);
    if (!brush) brush=original_brush(index);
    InterlockedDecrement(&properties_callbacks);
    return brush;
}
/* Locate only the active print for this same admitted object and DC. */
static PropertiesScope *boolean_buffer_scope(PropertiesScope *scope, HDC dc)
{
    PropertiesScope *current=scope;
    if (!scope || ((uintptr_t)scope & 1U) || scope->kind!=2) return NULL;
    while (current) {
        if (current->kind==2 && current->boolean_buffer==dc && current->child==scope->child &&
            current->object==scope->object && current->control_token==scope->control_token) return current;
        current=properties_unmask(current->previous);
    }
    return NULL;
}
static BOOL WINAPI boolean_rectangle(HDC dc, int left, int top, int right, int bottom)
{
    uintptr_t site=(uintptr_t)_ReturnAddress()-(uintptr_t)editor_base;
    PropertiesScope *scope=properties_raw(); HGDIOBJ old_brush=NULL, old_pen=NULL; BOOL result;
    InterlockedIncrement(&properties_callbacks);
    __try {
        if (!InterlockedCompareExchange(&properties_tls_failed,0,0) && scope && !((uintptr_t)scope&1U) &&
            scope->kind==2 && scope->dc==dc && site==0x5146ACU && scope->button_brush && scope->button_pen) {
            old_brush=SelectObject(dc,scope->button_brush);
            if (old_brush && old_brush!=HGDI_ERROR) {
                old_pen=SelectObject(dc,scope->button_pen);
                scope->rectangle_seen=1;
            }
        }
        result=original_rectangle(dc,left,top,right,bottom);
        if (result && scope && !((uintptr_t)scope&1U) && scope->kind==2 && scope->dc==dc &&
            site==0x5146ACU && scope->rectangle_seen) {
            PropertiesScope *paint=boolean_buffer_scope(scope,dc);
            if (paint) paint->boolean_rectangle_done=1;
        }
    } __finally {
        if (old_pen && old_pen!=HGDI_ERROR) (void)SelectObject(dc,old_pen);
        if (old_brush && old_brush!=HGDI_ERROR) (void)SelectObject(dc,old_brush);
        InterlockedDecrement(&properties_callbacks);
    }
    return result;
}
static COLORREF WINAPI boolean_text_color(HDC dc, COLORREF value)
{
    uintptr_t site=(uintptr_t)_ReturnAddress()-(uintptr_t)mfc_base;
    PropertiesScope *scope=properties_raw(); COLORREF result;
    int prior=scope && !((uintptr_t)scope&1U) && scope->kind==2 && scope->text_seen;
    InterlockedIncrement(&properties_callbacks);
    __try {
        if (!InterlockedCompareExchange(&properties_tls_failed,0,0) && scope && !((uintptr_t)scope&1U) &&
            scope->kind==2 && scope->rectangle_seen && scope->dc==dc &&
            (site==0x28326EU || site==0x28327FU) && !prior) {
            scope->text_seen=1;
            if (value==RGB(0,0,0)) value=scope->button_text;
            else if (value==RGB(128,128,128)) value=scope->button_disabled;
        }
        result=original_text_color(dc,value);
        /* MFC calls the output HDC at most once per setter. The native
           renderer restores text color after its DrawText call. */
        if (result!=CLR_INVALID && prior && scope && !((uintptr_t)scope&1U) && scope->kind==2 &&
            scope->dc==dc && (site==0x28326EU || site==0x28327FU)) {
            PropertiesScope *paint=boolean_buffer_scope(scope,dc);
            if (paint) paint->boolean_text_done=1;
        }
    } __finally { InterlockedDecrement(&properties_callbacks); }
    return result;
}
static int properties_exchange(void *expected, void *replacement)
{
    DWORD old, ignored; void *observed;
    if (!properties_slot || ((uintptr_t)properties_slot & (sizeof(void *)-1U)) ||
        !VirtualProtect((void *)properties_slot,sizeof(void *),PAGE_READWRITE,&old)) return 0;
    if (!properties_pending) properties_protection=old;
    properties_pending=1;
    observed=InterlockedCompareExchangePointer(properties_slot,replacement,expected);
    if (VirtualProtect((void *)properties_slot,sizeof(void *),properties_protection,&ignored)) properties_pending=0;
    return observed==expected;
}
static int boolean_exchange(PropertiesIat *route, void *expected, void *replacement)
{
    DWORD old, ignored; void *observed;
    if (!route->slot || ((uintptr_t)route->slot & (sizeof(void *)-1U)) ||
        !VirtualProtect((void *)route->slot,sizeof(void *),PAGE_READWRITE,&old)) return 0;
    if (!route->pending) route->protection=old;
    route->pending=1;
    observed=InterlockedCompareExchangePointer(route->slot,replacement,expected);
    if (VirtualProtect((void *)route->slot,sizeof(void *),route->protection,&ignored)) route->pending=0;
    return observed==expected;
}
static void boolean_restore(PropertiesIat *route)
{
    DWORD ignored;
    if (!route->slot) return;
    if (route->installed && boolean_exchange(route,route->replacement,route->original)) route->installed=0;
    if (!route->installed && route->pending && VirtualProtect((void *)route->slot,sizeof(void *),route->protection,&ignored)) route->pending=0;
}
static int properties_resolve(void)
{
    HMODULE mfc=GetModuleHandleW(L"mfc100.dll"); void *permanent; int ok=0;
    if (!mfc) return 0;
    AcquireSRWLockExclusive(&properties_lock);
    if (properties_resolved) { ok=1; goto done; }
    if (properties_stopping) goto done;
    if (!dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR) ||
        !dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_MFC)) goto done;
    editor_base=(BYTE *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,0,1);
    mfc_base=(BYTE *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,0,1);
    dialog_vtable=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,PROPERTIES_DIALOG_VTABLE,sizeof(void *));
    fields_vtable=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,PROPERTIES_FIELDS_VTABLE,sizeof(void *));
    list_vtable=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,PROPERTIES_LIST_VTABLE,sizeof(void *));
    permanent=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,0x26B460U,1);
    properties_slot=(PVOID volatile *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,PROPERTIES_BRUSH_IAT,sizeof(void *));
    if (!editor_base || !mfc_base || !dialog_vtable || !fields_vtable || !list_vtable || !permanent || !properties_slot ||
        (void *)GetProcAddress(mfc,(LPCSTR)(ULONG_PTR)4194U)!=permanent) goto done;
    permanent_lookup=(PropertiesPermanentFn)permanent; properties_resolved=ok=1;
done:
    ReleaseSRWLockExclusive(&properties_lock); return ok;
}
static int boolean_install(void)
{
    HMODULE gdi=GetModuleHandleW(L"gdi32.dll"); int ok=0;
    if (!gdi) return 0;
    AcquireSRWLockExclusive(&properties_lock);
    if (properties_stopping || !properties_resolved) goto done;
    if (boolean_rectangle_iat.installed && boolean_text_iat.installed) {
        ok=!boolean_rectangle_iat.pending && !boolean_text_iat.pending; goto done;
    }
    if (properties_tls==TLS_OUT_OF_INDEXES) properties_tls=TlsAlloc();
    if (properties_tls==TLS_OUT_OF_INDEXES) goto done;
    boolean_rectangle_iat.slot=(PVOID volatile *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,BOOLEAN_RECT_IAT,sizeof(void *));
    boolean_text_iat.slot=(PVOID volatile *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,BOOLEAN_TEXT_IAT,sizeof(void *));
    original_rectangle=(BOOL (WINAPI *)(HDC,int,int,int,int))(void *)GetProcAddress(gdi,"Rectangle");
    original_text_color=(COLORREF (WINAPI *)(HDC,COLORREF))(void *)GetProcAddress(gdi,"SetTextColor");
    if (!boolean_rectangle_iat.slot || !boolean_text_iat.slot || !original_rectangle || !original_text_color) goto done;
    boolean_rectangle_iat.original=(void *)original_rectangle; boolean_rectangle_iat.replacement=(void *)boolean_rectangle;
    boolean_text_iat.original=(void *)original_text_color; boolean_text_iat.replacement=(void *)boolean_text_color;
    if (!boolean_rectangle_iat.installed) boolean_rectangle_iat.installed=boolean_exchange(&boolean_rectangle_iat,boolean_rectangle_iat.original,boolean_rectangle_iat.replacement);
    if (boolean_rectangle_iat.installed && !boolean_rectangle_iat.pending && !boolean_text_iat.installed)
        boolean_text_iat.installed=boolean_exchange(&boolean_text_iat,boolean_text_iat.original,boolean_text_iat.replacement);
    ok=boolean_rectangle_iat.installed && !boolean_rectangle_iat.pending && boolean_text_iat.installed && !boolean_text_iat.pending;
done:
    ReleaseSRWLockExclusive(&properties_lock); return ok;
}
static int properties_install(void)
{
    HMODULE user=GetModuleHandleW(L"user32.dll"), gdi=GetModuleHandleW(L"gdi32.dll"); int ok=0;
    if (!user || !gdi) return 0;
    AcquireSRWLockExclusive(&properties_lock);
    if (properties_stopping || !properties_resolved) goto done;
    if (properties_installed && properties_region_iat.installed) {
        ok=!properties_pending && !properties_region_iat.pending; goto done;
    }
    if (properties_tls==TLS_OUT_OF_INDEXES) properties_tls=TlsAlloc();
    if (properties_tls==TLS_OUT_OF_INDEXES) goto done;
    original_brush=(HBRUSH (WINAPI *)(int))(void *)GetProcAddress(user,"GetSysColorBrush");
    original_rect_region=(HRGN (WINAPI *)(const RECT *))(void *)GetProcAddress(gdi,"CreateRectRgnIndirect");
    properties_region_iat.slot=(PVOID volatile *)dluma_editor_theme_profile_address(
        DLUMA_EDITOR_THEME_PROFILE_EDITOR,PROPERTIES_REGION_IAT,sizeof(void *));
    if (!original_brush || !original_rect_region || !properties_region_iat.slot) goto done;
    properties_region_iat.original=(void *)original_rect_region;
    properties_region_iat.replacement=(void *)properties_rect_region;
    if (!properties_region_iat.installed)
        properties_region_iat.installed=boolean_exchange(&properties_region_iat,
            properties_region_iat.original,properties_region_iat.replacement);
    if (properties_region_iat.installed && !properties_region_iat.pending && !properties_installed)
        properties_installed=properties_exchange((void *)original_brush,(void *)properties_brush);
    ok=properties_installed && !properties_pending && properties_region_iat.installed && !properties_region_iat.pending;
done:
    ReleaseSRWLockExclusive(&properties_lock); return ok;
}
static int properties_identity(HWND owner, HWND child, BYTE **object)
{
    BYTE *dialog, *list; LONG_PTR style; int id; unsigned int offset; void *owner_vtable;
    if (!owner || !child || GetParent(child)!=owner ||
        GetWindowThreadProcessId(owner,NULL)!=GetCurrentThreadId() || GetWindowThreadProcessId(child,NULL)!=GetCurrentThreadId()) return 0;
    id=GetDlgCtrlID(child);
    if (id==PROPERTIES_LIST_ID) offset=PROPERTIES_LIST_OFFSET;
    else if (id==PROPERTIES_FIELDS_ID) offset=PROPERTIES_FIELDS_OFFSET;
    else return 0;
    style=GetWindowLongPtrW(child,GWL_STYLE);
    if (!(style & LVS_OWNERDRAWFIXED) || (style & LVS_TYPEMASK)!=LVS_REPORT || !properties_resolve()) return 0;
    dialog=(BYTE *)permanent_lookup(owner); list=(BYTE *)permanent_lookup(child);
    owner_vtable=id==PROPERTIES_LIST_ID ? dialog_vtable : fields_vtable;
    if (!memory_readable(dialog,offset+sizeof(void *)) || !memory_writable(list,664U) ||
        list!=dialog+offset || *(void **)dialog!=owner_vtable || *(void **)list!=list_vtable ||
        *(HWND *)(dialog+64U)!=owner || *(HWND *)(list+64U)!=child) return 0;
    *object=list; return 1;
}
int dluma_editor_theme_properties_is_list(HWND window)
{
    BYTE *object=NULL;
    return properties_identity(GetParent(window),window,&object);
}
static int boolean_identity(HWND owner, HWND child, BYTE **object)
{
    BYTE *list, *button; void **table; HWND dialog;
    if (!owner || !child || GetParent(child)!=owner ||
        GetWindowThreadProcessId(child,NULL)!=GetCurrentThreadId() ||
        (GetWindowLongPtrW(child,GWL_STYLE) & BS_TYPEMASK)!=BS_OWNERDRAW) return 0;
    dialog=GetParent(owner);
    if (!properties_identity(dialog,owner,&list)) return 0;
    boolean_vtable=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,BOOLEAN_VTABLE,92U*sizeof(void *));
    boolean_draw=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,BOOLEAN_DRAW_RVA,1);
    button=(BYTE *)permanent_lookup(child);
    if (!boolean_vtable || !boolean_draw || !memory_readable(button,280U) ||
        *(void **)button!=boolean_vtable || *(HWND *)(button+64U)!=child) return 0;
    table=*(void ***)button;
    if (table[91]!=boolean_draw) return 0;
    *object=button; return 1;
}
static void restore_fields(PropertiesScope *scope)
{
    unsigned int i; BYTE *object;
    if (!scope->fields_active) return;
    if (dluma_editor_theme_controls_listview_alive(scope->child,scope->control_token) &&
        properties_identity(scope->owner,scope->child,&object) && object==scope->object) {
        for (i=0;i<PROPERTIES_COLOR_COUNT;++i) {
            DWORD *field=(DWORD *)(object+color_offsets[i]);
            if (*field==scope->applied[i]) *field=scope->original[i];
        }
    }
    scope->fields_active=0;
}
static void properties_after(uint64_t *scratch);

static int boolean_scope_prepare(PropertiesScope *scope, HDC dc)
{
    if (!dc || !boolean_install()) return 0;
    if (!scope->button_brush) scope->button_brush=CreateSolidBrush(scope->surface);
    if (!scope->button_brush) return 0;
    if (!scope->button_pen) scope->button_pen=CreatePen(PS_SOLID,1,scope->button_border);
    if (!scope->button_pen) return 0;
    scope->dc=dc; scope->rectangle_seen=scope->text_seen=0;
    return 1;
}

static int boolean_scope_before(HWND owner, HWND child, HDC dc,
    const DlumaEditorThemeResolved *theme, uint64_t *scratch)
{
    PropertiesScope *scope; BYTE *object=NULL;
    void *token=dluma_editor_theme_controls_hold_button(child);
    if (!token) return 0;
    if (boolean_identity(owner,child,&object) && properties_tls_ready()) {
        scope=(PropertiesScope *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*scope));
        if (scope) {
            scope->kind=2; scope->owner=owner; scope->child=child;
            scope->object=object; scope->control_token=token;
            scope->surface=role(theme,DLUMA_EDITOR_THEME_ROLE_SURFACE);
            scope->button_text=role(theme,DLUMA_EDITOR_THEME_ROLE_CONTROL_TEXT);
            scope->button_disabled=role(theme,DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT);
            scope->button_border=role(theme,DLUMA_EDITOR_THEME_ROLE_BORDER);
            scope->previous=properties_raw();
            if ((!dc || boolean_scope_prepare(scope,dc)) && TlsSetValue(properties_tls,scope)) {
                InterlockedIncrement(&properties_frames); *scratch=(uint64_t)(uintptr_t)scope;
                if (dluma_editor_ui_invocation_cleanup(DLUMA_EDITOR_UI_OWNER_THEME,
                    DLUMA_EDITOR_UI_SLOT_PROPERTIES,properties_after)) return 1;
                properties_after(scratch); return 0;
            }
            if (scope->button_brush) DeleteObject(scope->button_brush);
            if (scope->button_pen) DeleteObject(scope->button_pen);
            HeapFree(GetProcessHeap(),0,scope);
        }
    }
    dluma_editor_theme_controls_release_listview(token);
    return 0;
}

static void properties_mask_before(uint64_t *scratch)
{
    PropertiesScope *prior;
    if (!scratch || *scratch) return;
    prior=properties_raw();
    if (prior) {
        if (TlsSetValue(properties_tls,(void *)((uintptr_t)prior|1U))) {
            InterlockedIncrement(&properties_frames); *scratch=(uint64_t)((uintptr_t)prior|2U);
            if (!dluma_editor_ui_invocation_cleanup(DLUMA_EDITOR_UI_OWNER_THEME,
                DLUMA_EDITOR_UI_SLOT_PROPERTIES,properties_after)) properties_after(scratch);
        } else InterlockedIncrement(&properties_tls_failed);
    }
}

static PropertiesScope *properties_row_paint(HWND owner, const DRAWITEMSTRUCT *draw)
{
    PropertiesScope *scope;
    if (!memory_readable(draw,sizeof(*draw)) || draw->CtlType!=ODT_LISTVIEW || draw->itemID==(UINT)-1 ||
        !draw->hwndItem || GetParent(draw->hwndItem)!=owner) return NULL;
    for (scope=properties_unmask(properties_raw()); scope; scope=properties_unmask(scope->previous))
        if (scope->kind==3 && scope->child==draw->hwndItem) return scope;
    return NULL;
}

static void properties_before(HWND owner, LPARAM lparam, const DlumaEditorThemeResolved *theme, uint64_t *scratch)
{
    DRAWITEMSTRUCT *draw=(DRAWITEMSTRUCT *)lparam; PropertiesScope *scope;
    PropertiesScope *paint=properties_row_paint(owner,draw);
    BYTE *object=NULL; void *token=NULL; unsigned int i;
    if (!scratch || *scratch) {
        if (paint) paint->paint_content_failed=1;
        return;
    }
    if (theme && theme->size==sizeof(*theme) && theme->version==1 && draw && draw->CtlType==ODT_LISTVIEW &&
        draw->hwndItem && draw->hDC && draw->itemID!=(UINT)-1 &&
        !InterlockedCompareExchange(&properties_tls_failed,0,0)) {
        token=dluma_editor_theme_controls_hold_listview(draw->hwndItem);
        if (token && properties_identity(owner,draw->hwndItem,&object) &&
            (!paint || (paint->control_token==token && paint->object==object)) && properties_install()) {
            scope=(PropertiesScope *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*scope));
            if (scope) {
                scope->kind=1; scope->owner=owner; scope->child=draw->hwndItem; scope->dc=draw->hDC;
                scope->object=object; scope->control_token=token; scope->surface=role(theme,DLUMA_EDITOR_THEME_ROLE_SURFACE);
                scope->applied[0]=role(theme,DLUMA_EDITOR_THEME_ROLE_PANEL);
                scope->applied[1]=role(theme,DLUMA_EDITOR_THEME_ROLE_ALTERNATE);
                scope->applied[2]=role(theme,DLUMA_EDITOR_THEME_ROLE_PANEL);
                scope->applied[3]=role(theme,DLUMA_EDITOR_THEME_ROLE_ALTERNATE);
                scope->applied[4]=scope->surface;
                scope->applied[5]=role(theme,DLUMA_EDITOR_THEME_ROLE_ALTERNATE);
                scope->applied[6]=scope->surface;
                scope->applied[7]=scope->applied[8]=scope->applied[9]=role(theme,DLUMA_EDITOR_THEME_ROLE_TEXT);
                scope->previous=properties_raw();
                if (TlsSetValue(properties_tls,scope)) {
                    InterlockedIncrement(&properties_frames); *scratch=(uint64_t)(uintptr_t)scope;
                    if (!dluma_editor_ui_invocation_cleanup(DLUMA_EDITOR_UI_OWNER_THEME,
                        DLUMA_EDITOR_UI_SLOT_PROPERTIES,properties_after)) {
                        if (paint) paint->paint_content_failed=1;
                        properties_after(scratch); return;
                    }
                    for (i=0;i<PROPERTIES_COLOR_COUNT;++i) scope->original[i]=*(DWORD *)(object+color_offsets[i]);
                    scope->fields_active=1;
                    for (i=0;i<PROPERTIES_COLOR_COUNT;++i) *(DWORD *)(object+color_offsets[i])=scope->applied[i];
                    return;
                }
                HeapFree(GetProcessHeap(),0,scope);
            }
        }
        if (token) dluma_editor_theme_controls_release_listview(token);
    }
    if (paint) paint->paint_content_failed=1;
    if (theme && theme->size==sizeof(*theme) && theme->version==1 && draw && draw->CtlType==ODT_BUTTON &&
        draw->hwndItem && draw->hDC && !InterlockedCompareExchange(&properties_tls_failed,0,0)) {
        if (boolean_scope_before(owner,draw->hwndItem,draw->hDC,theme,scratch)) return;
    }
    properties_mask_before(scratch);
}
static void properties_after(uint64_t *scratch)
{
    PropertiesScope *scope; uintptr_t prior;
    if (!scratch || !*scratch) return;
    if (*scratch & 2U) {
        prior=(uintptr_t)(*scratch & ~(uint64_t)2U); *scratch=0;
        if (!TlsSetValue(properties_tls,(void *)prior)) { InterlockedIncrement(&properties_tls_failed); return; }
    } else {
        scope=(PropertiesScope *)(uintptr_t)*scratch; *scratch=0;
        restore_fields(scope);
        if (!TlsSetValue(properties_tls,scope->previous)) { InterlockedIncrement(&properties_tls_failed); return; }
        if (scope->button_brush) DeleteObject(scope->button_brush);
        if (scope->button_pen) DeleteObject(scope->button_pen);
        if (scope->paint_damage) DeleteObject(scope->paint_damage);
        dluma_editor_theme_controls_release_listview(scope->control_token);
        HeapFree(GetProcessHeap(),0,scope);
    }
    InterlockedDecrement(&properties_frames);
}

static void properties_paint_before(HWND window, uint64_t *scratch, const DlumaEditorThemeResolved *theme)
{
    PropertiesScope *scope;
    BYTE *object = NULL;
    void *token;
    HRGN damage;
    int region;
    if (!scratch || *scratch || !theme || theme->size!=sizeof(*theme) || theme->version!=1 ||
        theme->kind==DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST) return;
    token = dluma_editor_theme_controls_hold_listview(window);
    if (!token) return;
    if (!properties_identity(GetParent(window), window, &object) || !properties_tls_ready() || !properties_install()) {
        dluma_editor_theme_properties_grid_fail(window,token,theme->generation);
        dluma_editor_theme_controls_release_listview(token); return;
    }
    damage = CreateRectRgn(0, 0, 0, 0);
    region = damage ? GetUpdateRgn(window, damage, FALSE) : ERROR;
    if (region == ERROR || region == NULLREGION) {
        if (damage) DeleteObject(damage);
        if (region==ERROR) dluma_editor_theme_properties_grid_fail(window,token,theme->generation);
        dluma_editor_theme_controls_release_listview(token);
        return;
    }
    scope = (PropertiesScope *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*scope));
    if (scope) {
        scope->kind = 3; scope->owner = scope->child = window;
        scope->object = object; scope->control_token = token; scope->paint_damage = damage;
        scope->paint_generation = theme->generation;
        scope->previous = properties_raw();
        if (TlsSetValue(properties_tls, scope)) {
            InterlockedIncrement(&properties_frames); *scratch = (uint64_t)(uintptr_t)scope;
            if (!dluma_editor_ui_invocation_cleanup(DLUMA_EDITOR_UI_OWNER_THEME,
                DLUMA_EDITOR_UI_SLOT_PROPERTIES, properties_after)) { dluma_editor_theme_properties_grid_fail(window,token,theme->generation); properties_after(scratch); return; }
            if (!dluma_editor_theme_properties_grid_begin(window,token,scope->paint_generation)) properties_after(scratch);
            return;
        }
        HeapFree(GetProcessHeap(), 0, scope);
    }
    DeleteObject(damage);
    dluma_editor_theme_properties_grid_fail(window,token,theme->generation);
    dluma_editor_theme_controls_release_listview(token);
}

/* Keep the separator pass within the damage captured before native BeginPaint.
 * Native row painting and the list background own all other client pixels. */
static int properties_paint_separators(HWND window, const DlumaEditorThemeResolved *theme, HRGN *damage, void *token, uint64_t generation, BYTE *expected_object, HDC dc)
{
    HWND header; BYTE *object=NULL; HPEN pen=NULL, old=NULL;
    RECT client, header_rect, item; POINT origin;
    HRGN device_damage=NULL;
    int saved=0, count, top, visible, columns, line_top, row_height=0, next_line=0, i, painted=0;
    if (!theme || theme->size!=sizeof(*theme) || theme->version!=1 || !damage || !*damage ||
        !dc || GetObjectType(dc)!=OBJ_MEMDC) return 0;
    if (!properties_identity(GetParent(window),window,&object) || object!=expected_object ||
        !dluma_editor_theme_properties_grid_valid(window,token,generation)) goto done;
    header=ListView_GetHeader(window);
    if (!dluma_editor_theme_controls_listview_alive(window,token) || !header ||
        GetParent(header)!=window || !IsWindowVisible(header) || !GetClientRect(window,&client) ||
        !GetWindowRect(header,&header_rect)) goto done;
    origin.x=header_rect.left; origin.y=header_rect.bottom;
    if (!ScreenToClient(window,&origin)) goto done;
    if (origin.y>client.top) client.top=origin.y;
    if (client.top>=client.bottom) goto done;
    count=ListView_GetItemCount(window); top=ListView_GetTopIndex(window);
    visible=ListView_GetCountPerPage(window); columns=Header_GetItemCount(header);
    if (!dluma_editor_theme_controls_listview_alive(window,token) || GetParent(header)!=window ||
        count<0 || visible<0 || columns<=0 || columns>256) goto done;
    if (visible>4096) visible=4096;
    line_top=client.top;
    if (!count) { top=0; }
    else {
        if (top<0 || top>=count) goto done;
        if (!dluma_editor_theme_controls_listview_alive(window,token) ||
            !ListView_GetItemRect(window,top,&item,LVIR_BOUNDS)) goto done;
        if (item.top>line_top) line_top=item.top;
        row_height=item.bottom-item.top;
        if (row_height<=0) goto done;
    }
    if (line_top>=client.bottom) { painted=1; goto done; }
    if (!dluma_editor_theme_controls_listview_alive(window,token) ||
        !properties_identity(GetParent(window),window,&object) || object!=expected_object || GetParent(header)!=window) goto done;
    if (!dluma_editor_theme_controls_listview_alive(window,token)) goto done;
    saved=SaveDC(dc);
    /* Keep the paint damage in the same device coordinates as native clips. */
    if (!saved || !properties_dc_translation(dc,&origin)) goto done;
    device_damage=CreateRectRgn(0,0,0,0);
    if (!device_damage || CombineRgn(device_damage,*damage,NULL,RGN_COPY)==ERROR ||
        OffsetRgn(device_damage,origin.x,origin.y)==ERROR || ExtSelectClipRgn(dc,device_damage,RGN_AND)==ERROR ||
        IntersectClipRect(dc,client.left,client.top,client.right,client.bottom)==ERROR) goto done;
    pen=CreatePen(PS_SOLID,1,role(theme,DLUMA_EDITOR_THEME_ROLE_BORDER));
    if (!pen) goto done;
    old=(HPEN)SelectObject(dc,pen);
    if (!old || old==(HPEN)HGDI_ERROR) { old=NULL; goto done; }
    for (i=0;i<columns;++i) {
        RECT column; POINT edge;
        if (!dluma_editor_theme_properties_grid_valid(window,token,generation) ||
            !Header_GetItemRect(header,i,&column)) goto done;
        if (!dluma_editor_theme_properties_grid_valid(window,token,generation) || GetParent(header)!=window) goto done;
        edge.x=column.right; edge.y=0;
        SetLastError(ERROR_SUCCESS);
        if (!MapWindowPoints(header,window,&edge,1) && GetLastError()!=ERROR_SUCCESS) goto done;
        if (edge.x>client.left && edge.x<client.right) {
            if (!MoveToEx(dc,edge.x,line_top,NULL) || !LineTo(dc,edge.x,client.bottom)) goto done;
        }
    }
    for (i=top;i<count && i-top<=visible+1;++i) {
        if (!dluma_editor_theme_properties_grid_valid(window,token,generation) ||
            !ListView_GetItemRect(window,i,&item,LVIR_BOUNDS)) goto done;
        if (!dluma_editor_theme_properties_grid_valid(window,token,generation)) goto done;
        if (item.bottom>client.top && item.bottom<=client.bottom) {
            if (!MoveToEx(dc,client.left,item.bottom-1,NULL) || !LineTo(dc,client.right,item.bottom-1)) goto done;
        }
        next_line=item.bottom-1;
        if (item.bottom>=client.bottom) break;
    }
    /* The native grid continues below the last populated row. */
    if (row_height>0 && i==count && next_line<client.bottom) {
        for (i=0;i<4096 && client.bottom-next_line>row_height;++i) {
            next_line+=row_height;
            if (next_line>=client.top) {
                if (!MoveToEx(dc,client.left,next_line,NULL) || !LineTo(dc,client.right,next_line)) goto done;
            }
        }
    }
    painted=dluma_editor_theme_properties_grid_valid(window,token,generation);
done:
    if (old) (void)SelectObject(dc,old);
    if (pen) (void)DeleteObject(pen);
    if (saved) (void)RestoreDC(dc,saved);
    if (device_damage) (void)DeleteObject(device_damage);
    return painted;
}

int dluma_editor_theme_properties_custom_draw(HWND owner, LPARAM lparam, LRESULT *result)
{
    NMCUSTOMDRAW *draw=(NMCUSTOMDRAW *)lparam;
    PropertiesScope *scope=properties_raw(); BYTE *object=NULL;
    DlumaEditorThemeResolved theme;
    if (!draw || !result || !scope || ((uintptr_t)scope & 1U) || scope->kind!=3 ||
        draw->hdr.hwndFrom!=scope->child || GetParent(scope->child)!=owner ||
        !properties_identity(owner,scope->child,&object) || object!=scope->object ||
        !dluma_editor_theme_properties_grid_valid(scope->child,scope->control_token,scope->paint_generation)) return 0;
    if (draw->dwDrawStage==CDDS_PREPAINT) {
        if (!(*result & CDRF_SKIPDEFAULT)) *result |= CDRF_NOTIFYPOSTPAINT;
    } else if (draw->dwDrawStage==CDDS_POSTPAINT && !(*result & CDRF_SKIPDEFAULT)) {
        ZeroMemory(&theme,sizeof(theme)); theme.size=sizeof(theme); theme.version=1;
        if (dluma_editor_theme_copy_snapshot(&theme) && theme.generation==scope->paint_generation)
            scope->paint_post_done=properties_paint_separators(scope->child,&theme,&scope->paint_damage,
                scope->control_token,scope->paint_generation,scope->object,draw->hdc);
    }
    return 1;
}

/* Called only from the retained WM_PAINT invocation on the button owner thread. */
int dluma_editor_theme_properties_boolean_erase(HWND window)
{
    PropertiesScope *scope=properties_unmask(properties_raw());
    while (scope) {
        if (scope->kind==2 && scope->child==window && scope->boolean_erase &&
            dluma_editor_theme_controls_listview_alive(window,scope->control_token)) return 1;
        scope=properties_unmask(scope->previous);
    }
    return 0;
}
int dluma_editor_theme_properties_paint_boolean(HWND window, const DlumaEditorThemeResolved *theme)
{
    PropertiesScope *scope=properties_raw(); BYTE *object=NULL;
    RECT rect, after, damage; PAINTSTRUCT paint; HDC compatible=NULL, buffer=NULL, dc=NULL;
    HBITMAP bitmap=NULL; HGDIOBJ old_bitmap=NULL, old_font=NULL;
    HFONT font; DWORD layout; UINT align; int rop, ok=0, began=0;
    if (!theme || theme->kind==DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST || !scope ||
        ((uintptr_t)scope&1U) || scope->kind!=2 || scope->child!=window || scope->boolean_buffer ||
        !dluma_editor_theme_controls_token_generation(window,scope->control_token,theme->generation) ||
        !boolean_identity(scope->owner,window,&object) || object!=scope->object || !boolean_install()) return 0;
    if (!GetClientRect(window,&rect) || rect.right<=rect.left || rect.bottom<=rect.top) return 0;
    ZeroMemory(&paint,sizeof(paint)); SetRectEmpty(&damage);
    __try {
        compatible=GetDC(window);
        if (!compatible) __leave;
        buffer=CreateCompatibleDC(compatible);
        if (buffer) bitmap=CreateCompatibleBitmap(compatible,rect.right-rect.left,rect.bottom-rect.top);
        ReleaseDC(window,compatible); compatible=NULL;
        if (!bitmap) __leave;
        old_bitmap=SelectObject(buffer,bitmap);
        if (!old_bitmap || old_bitmap==HGDI_ERROR) { old_bitmap=NULL; __leave; }
        if (!dluma_editor_theme_controls_token_generation(window,scope->control_token,theme->generation) ||
            !boolean_identity(scope->owner,window,&object) || object!=scope->object) __leave;
        (void)GetUpdateRect(window,&damage,FALSE);
        scope->boolean_buffer=buffer; scope->boolean_rectangle_done=scope->boolean_text_done=0;
        scope->boolean_erase=1; ok=-1;
        dc=BeginPaint(window,&paint); began=dc!=NULL;
        scope->boolean_erase=0;
        if (!dc) __leave;
        damage=paint.rcPaint;
        layout=GetLayout(dc); align=GetTextAlign(dc); rop=GetROP2(dc);
        if (layout==GDI_ERROR || align==GDI_ERROR || !rop || SetLayout(buffer,layout)==GDI_ERROR ||
            SetTextAlign(buffer,align)==GDI_ERROR || !SetROP2(buffer,rop) ||
            !SetBkMode(buffer,GetBkMode(dc)) || SetTextColor(buffer,GetTextColor(dc))==CLR_INVALID ||
            SetBkColor(buffer,GetBkColor(dc))==CLR_INVALID) __leave;
        font=(HFONT)SendMessageW(window,WM_GETFONT,0,0);
        if (font) { old_font=SelectObject(buffer,font); if (!old_font || old_font==HGDI_ERROR) { old_font=NULL; __leave; } }
        if (!dluma_editor_theme_controls_token_generation(window,scope->control_token,theme->generation) ||
            !boolean_identity(scope->owner,window,&object) || object!=scope->object) __leave;
        (void)SendMessageW(window,WM_PRINTCLIENT,(WPARAM)buffer,PRF_CLIENT);
        if (!scope->boolean_rectangle_done || !scope->boolean_text_done ||
            !dluma_editor_theme_controls_token_generation(window,scope->control_token,theme->generation) ||
            !boolean_identity(scope->owner,window,&object) || object!=scope->object ||
            !GetClientRect(window,&after) || !EqualRect(&rect,&after)) __leave;
        if (!IsRectEmpty(&paint.rcPaint) && !BitBlt(dc,paint.rcPaint.left,paint.rcPaint.top,
            paint.rcPaint.right-paint.rcPaint.left,paint.rcPaint.bottom-paint.rcPaint.top,
            buffer,paint.rcPaint.left,paint.rcPaint.top,SRCCOPY)) __leave;
        ok=1;
    } __finally {
        if (AbnormalTermination()) ok=-1;
        scope->boolean_erase=0; scope->boolean_buffer=NULL;
        if (began) EndPaint(window,&paint);
        if (ok<0 && dluma_editor_theme_controls_listview_alive(window,scope->control_token) && !IsRectEmpty(&damage))
            InvalidateRect(window,&damage,TRUE);
        if (old_font) SelectObject(buffer,old_font);
        if (old_bitmap) SelectObject(buffer,old_bitmap);
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        if (compatible) ReleaseDC(window,compatible);
    }
    return ok;
}

void dluma_editor_theme_properties_message(HWND owner, unsigned int stage, UINT message,
    LPARAM lparam, const DlumaEditorThemeResolved *theme)
{
    uint64_t *scratch; PropertiesScope *scope;
    if (stage==DLUMA_EDITOR_UI_BEFORE && message==WM_NCDESTROY) {
        scope=properties_unmask(properties_raw());
        while (scope) { if (scope->child==owner) restore_fields(scope); scope=properties_unmask(scope->previous); }
    }
    scratch=dluma_editor_ui_invocation_slot(DLUMA_EDITOR_UI_OWNER_THEME,DLUMA_EDITOR_UI_SLOT_PROPERTIES);
    if (!scratch) return;
    if (stage==DLUMA_EDITOR_UI_BEFORE) {
        if (message==WM_DRAWITEM) properties_before(owner,lparam,theme,scratch);
        else if (message==WM_PAINT) {
            properties_paint_before(owner,scratch,theme);
            if (!*scratch && theme && theme->size==sizeof(*theme) && theme->version==1 &&
                !InterlockedCompareExchange(&properties_tls_failed,0,0) &&
                boolean_scope_before(GetParent(owner),owner,NULL,theme,scratch)) return;
            if (!*scratch) properties_mask_before(scratch);
        }
        else if (!*scratch) {
            if (message!=WM_NCDESTROY && theme && theme->size==sizeof(*theme) && theme->version==1 &&
                !InterlockedCompareExchange(&properties_tls_failed,0,0) &&
                boolean_scope_before(GetParent(owner),owner,NULL,theme,scratch)) return;
            properties_mask_before(scratch);
        }
    }
    else if (stage==DLUMA_EDITOR_UI_AFTER) {
        if (message==WM_PAINT && *scratch && !(*scratch & 2U)) {
            scope=(PropertiesScope *)(uintptr_t)*scratch;
            if (scope->kind==3 && scope->child==owner && (!scope->paint_post_done || scope->paint_content_failed))
                dluma_editor_theme_properties_grid_fail(owner,scope->control_token,scope->paint_generation);
        }
        properties_after(scratch);
    }
}

void dluma_editor_theme_properties_observe(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam, void *context)
{
    PropertiesScope *scope=properties_raw(); DRAWITEMSTRUCT *draw=(DRAWITEMSTRUCT *)lparam;
    (void)wparam; (void)context;
    if (message!=WM_DRAWITEM || !scope || ((uintptr_t)scope&1U) || scope->kind!=2 ||
        scope->owner!=window || !memory_readable(draw,sizeof(*draw)) || draw->CtlType!=ODT_BUTTON ||
        draw->hwndItem!=scope->child || !draw->hDC ||
        !dluma_editor_theme_controls_listview_alive(scope->child,scope->control_token) ||
        !memory_readable(scope->object,280U) || *(void **)scope->object!=boolean_vtable ||
        *(HWND *)(scope->object+64U)!=scope->child) return;
    /* MFC can consume the parent draw without reaching its inner subclass. */
    (void)boolean_scope_prepare(scope,draw->hDC);
}
void dluma_editor_theme_properties_stop(void)
{
    DWORD ignored;
    AcquireSRWLockExclusive(&properties_lock); properties_stopping=1;
    if (properties_installed && properties_exchange((void *)properties_brush,(void *)original_brush)) properties_installed=0;
    if (!properties_installed && properties_pending && VirtualProtect((void *)properties_slot,sizeof(void *),properties_protection,&ignored)) properties_pending=0;
    boolean_restore(&boolean_rectangle_iat); boolean_restore(&boolean_text_iat);
    boolean_restore(&properties_region_iat);
    ReleaseSRWLockExclusive(&properties_lock);
}
int dluma_editor_theme_properties_drained(void)
{
    int drained;
    AcquireSRWLockShared(&properties_lock);
    drained=!properties_installed && !properties_pending && !boolean_rectangle_iat.installed &&
        !boolean_rectangle_iat.pending && !boolean_text_iat.installed && !boolean_text_iat.pending;
    drained=drained && !properties_region_iat.installed && !properties_region_iat.pending;
    ReleaseSRWLockShared(&properties_lock);
    return drained && InterlockedCompareExchange(&properties_callbacks,0,0)==0 &&
        InterlockedCompareExchange(&properties_frames,0,0)==0 && InterlockedCompareExchange(&properties_tls_failed,0,0)==0;
}
