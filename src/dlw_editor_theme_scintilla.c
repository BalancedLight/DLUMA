#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>
#include "dlw_editor_theme_scintilla.h"
#include "../build/generated/editor-theme/editor_theme_palette.generated.h"

/* Message values are from Scintilla 3.0.4's Scintilla.iface. */
enum {
    SCI_MARKERDEFINE=2040, SCI_MARKERSETFORE=2041, SCI_MARKERSETBACK=2042,
    SCI_MARKERDEFINEPIXMAP=2049, SCI_MARKERDEFINERGBAIMAGE=2626,
    SCI_STYLECLEARALL=2050, SCI_STYLESETFORE=2051, SCI_STYLESETBACK=2052,
    SCI_STYLESETUNDERLINE=2059, SCI_SETSELFORE=2067, SCI_SETSELBACK=2068,
    SCI_SETCARETFORE=2069, SCI_INDICSETSTYLE=2080, SCI_INDICGETSTYLE=2081,
    SCI_INDICSETFORE=2082, SCI_INDICGETFORE=2083, SCI_SETWHITESPACEFORE=2084, SCI_SETWHITESPACEBACK=2085,
    SCI_GETSTYLEBITS=2091, SCI_GETCARETLINEVISIBLE=2095, SCI_SETCARETLINEVISIBLE=2096,
    SCI_GETCARETLINEBACK=2097, SCI_SETCARETLINEBACK=2098, SCI_GETCARETFORE=2138,
    SCI_CALLTIPSETBACK=2205, SCI_CALLTIPSETFORE=2206, SCI_CALLTIPSETFOREHLT=2207,
    SCI_SETFOLDMARGINCOLOUR=2290, SCI_SETFOLDMARGINHICOLOUR=2291, SCI_MARKERSETBACKSELECTED=2292,
    SCI_STYLEGETFORE=2481, SCI_STYLEGETBACK=2482, SCI_STYLEGETUNDERLINE=2488,
    SCI_AUTOCSHOW=2100, SCI_GETDIRECTPOINTER=2185, SCI_SETADDITIONALSELFORE=2600,
    SCI_SETADDITIONALSELBACK=2601, SCI_GETLEXER=4002, SCI_SETLEXER=4001,
    STYLE_DEFAULT=32, STYLE_LINENUMBER=33, STYLE_BRACELIGHT=34,
    STYLE_BRACEBAD=35, STYLE_CALLTIP=38, SCLEX_CPP=3, SCLEX_LUA=15, SCLEX_CPPNOCASE=35, SCLEX_PXSL=152
};
typedef struct StyleSnapshot {
    COLORREF original_fore, original_back, applied_fore, applied_back;
    int original_underline, applied_underline;
} StyleSnapshot;
typedef struct SetterSnapshot {
    UINT message;
    WPARAM original_wparam;
    LPARAM original_lparam;
    int known;
} SetterSnapshot;
typedef struct MarkerSnapshot {
    COLORREF original[3], applied[3];
    DWORD symbol;
    int active;
} MarkerSnapshot;
typedef struct ScintillaTheme {
    DlumaEditorThemeResolved theme;
    StyleSnapshot styles[256];
    SetterSnapshot setter[9];
    MarkerSnapshot markers[7];
    COLORREF indicator_fore, indicator_applied;
    int indicator_style, indicator_active;
    COLORREF original_caret, applied_caret, original_line, applied_line;
    COLORREF original_additional_fore, original_additional_back;
    unsigned int additional_known;
    int original_line_visible, applied_line_visible;
    unsigned int count;
    int lexer, applying, queued, has_theme;
    unsigned int references;
    int detached, destroyed;
    BYTE *direct_object;
    HWND popup_list;
    HWND window;
} ScintillaTheme;
static const WCHAR property_name[] = L"DLUMA.EditorTheme.Scintilla.1";
static const WCHAR detaching_name[] = L"DLUMA.EditorTheme.Scintilla.Detaching.1";
static volatile LONG live_states;
static const WCHAR refresh_name[] = L"DLUMA.EditorTheme.Scintilla.Refresh.1";
static UINT refresh_message;
static const UINT setter_messages[] = {SCI_SETSELFORE, SCI_SETSELBACK, SCI_SETWHITESPACEFORE, SCI_SETWHITESPACEBACK, SCI_CALLTIPSETBACK, SCI_CALLTIPSETFORE, SCI_CALLTIPSETFOREHLT, SCI_SETFOLDMARGINCOLOUR, SCI_SETFOLDMARGINHICOLOUR};
extern int dluma_sha256_file_hex(const WCHAR *, unsigned long long, char *);
static SRWLOCK scintilla_profile_lock;
static HMODULE scintilla_profile_module;
static int scintilla_profile_ok;
static HMODULE scintilla_profile(void)
{
    HMODULE module = GetModuleHandleW(L"SciLexer_x64.dll"), pinned;
    WCHAR path[32768]; char digest[65]; DWORD length;
    int ok = 0;
    if (!module) return NULL;
    AcquireSRWLockExclusive(&scintilla_profile_lock);
    if (module == scintilla_profile_module) ok = scintilla_profile_ok;
    else {
        scintilla_profile_module = module; scintilla_profile_ok = 0;
        length = GetModuleFileNameW(module, path, 32768U);
        if (length && length < 32768U &&
            dluma_sha256_file_hex(path, 64ULL * 1024ULL * 1024ULL, digest) &&
            !lstrcmpiA(digest, "0A00F6889C890D6EE38A7F2C1147BDBC2A55DF8A4F431B07561294FA1CF55F09") &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               (LPCWSTR)module, &pinned) && pinned == module)
            scintilla_profile_ok = ok = 1;
    }
    ReleaseSRWLockExclusive(&scintilla_profile_lock);
    return ok ? module : NULL;
}

/* These fields have no public getters in the admitted Scintilla build. */
static int snapshot_calltip(HWND w, ScintillaTheme *s)
{
    HMODULE module = scintilla_profile();
    BYTE *object; MEMORY_BASIC_INFORMATION memory; unsigned int i, j;
    if (!module) return 0;
    object = (BYTE *)(uintptr_t)SendMessageW(w, SCI_GETDIRECTPOINTER, 0, 0);
    if (!object || !VirtualQuery(object, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        (uintptr_t)object > UINTPTR_MAX - 3804U ||
        (uintptr_t)object + 3804U > (uintptr_t)memory.BaseAddress + memory.RegionSize ||
        *(void **)object != (BYTE *)module + 0x1051C8U) return 0;
    s->setter[4].original_wparam = *(DWORD *)(object + 3792U);
    s->setter[5].original_wparam = *(DWORD *)(object + 3796U);
    s->setter[6].original_wparam = *(DWORD *)(object + 3800U);
    s->setter[4].known = s->setter[5].known = s->setter[6].known = 1;
    s->setter[0].original_wparam = object[0x7ECU] != 0;
    s->setter[0].original_lparam = *(DWORD *)(object + 0x7F0U);
    s->setter[1].original_wparam = object[0x7F8U] != 0;
    s->setter[1].original_lparam = *(DWORD *)(object + 0x7FCU);
    s->setter[2].original_wparam = object[0x811U] != 0;
    s->setter[2].original_lparam = *(DWORD *)(object + 0x814U);
    s->setter[3].original_wparam = object[0x818U] != 0;
    s->setter[3].original_lparam = *(DWORD *)(object + 0x81CU);
    s->setter[7].original_wparam = object[0x828U] != 0;
    s->setter[7].original_lparam = *(DWORD *)(object + 0x82CU);
    s->setter[8].original_wparam = object[0x830U] != 0;
    s->setter[8].original_lparam = *(DWORD *)(object + 0x834U);
    s->setter[0].known=s->setter[1].known=s->setter[2].known=s->setter[3].known=1;
    s->setter[7].known=s->setter[8].known=1;
    s->original_additional_fore=*(DWORD *)(object + 0x7F4U);
    s->original_additional_back=*(DWORD *)(object + 0x800U);
    s->additional_known=3U;
    for (i=0; i<7U; ++i) {
        s->markers[i].symbol=*(DWORD *)(object+0x50U+40U*(i+25U));
        for (j=0; j<3U; ++j)
            s->markers[i].original[j]=*(DWORD *)(object+0x54U+40U*(i+25U)+4U*j);
    }
    s->direct_object = object;
    return 1;
}

static LRESULT sci(HWND w, UINT message, WPARAM wp, LPARAM lp) { return SendMessageW(w, message, wp, lp); }
static void release_state(ScintillaTheme *s)
{
    if (!--s->references && s->detached) {
        HeapFree(GetProcessHeap(), 0, s);
        InterlockedDecrement(&live_states);
    }
}
static int active_state(HWND w, ScintillaTheme *s)
{
    return !s->detached && !s->destroyed && GetPropW(w, property_name) == s;
}
static void remove_state(HWND w, ScintillaTheme *s, int destroyed)
{
    if (s->popup_list && IsWindow(s->popup_list) && GetPropW(s->popup_list,L"DLUMA.EditorTheme.Scintilla.Popup.1")==w)
        RemovePropW(s->popup_list,L"DLUMA.EditorTheme.Scintilla.Popup.1");
    if (destroyed) {
        s->destroyed = 1;
        if (GetPropW(w, detaching_name) == s) RemovePropW(w, detaching_name);
    }
    if (!s->detached) {
        s->detached = 1;
        if (GetPropW(w, property_name) == s) RemovePropW(w, property_name);
        release_state(s);
    }
}
static int owner(HWND w) { return IsWindow(w) && GetWindowThreadProcessId(w, NULL) == GetCurrentThreadId(); }
static COLORREF role(const DlumaEditorThemeResolved *t, unsigned int index)
{
    uint32_t c = t->argb[index];
    return RGB((c >> 16) & 255U, (c >> 8) & 255U, c & 255U);
}
static int state_live(HWND w, ScintillaTheme *s)
{
    return !s->destroyed && owner(w) &&
        GetPropW(w, s->detached ? detaching_name : property_name)==s;
}
static DWORD *marker_field(ScintillaTheme *s, unsigned int marker, unsigned int field)
{
    return (DWORD *)(s->direct_object + 0x50U + marker*40U + field*4U);
}
static DWORD expected_marker(unsigned int marker)
{
    return marker==31U ? 7U : marker==30U ? 8U : 5U;
}
static void restore_marker(HWND w, ScintillaTheme *s, unsigned int marker)
{
    static const UINT messages[3]={SCI_MARKERSETFORE,SCI_MARKERSETBACK,SCI_MARKERSETBACKSELECTED};
    MarkerSnapshot *m=&s->markers[marker-25U]; unsigned int j;
    if (!s->direct_object || !m->active) return;
    for (j=0; j<3U && state_live(w,s); ++j)
        if (*marker_field(s,marker,j+1U)==m->applied[j])
            (void)sci(w,messages[j],marker,m->original[j]);
    m->active=0;
}
static void restore_indicator(HWND w, ScintillaTheme *s)
{
    if (s->indicator_active && state_live(w,s) &&
        (COLORREF)sci(w,SCI_INDICGETFORE,8,0)==s->indicator_applied && state_live(w,s))
        (void)sci(w,SCI_INDICSETFORE,8,s->indicator_fore);
    s->indicator_active=0;
}
static void apply_markers_and_indicator(HWND w, ScintillaTheme *s)
{
    static const UINT messages[3]={SCI_MARKERSETFORE,SCI_MARKERSETBACK,SCI_MARKERSETBACKSELECTED};
    unsigned int marker, j; COLORREF colors[3], current;
    colors[0]=role(&s->theme,s->theme.kind==DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST ? DLUMA_EDITOR_THEME_ROLE_TEXT : DLUMA_EDITOR_THEME_ROLE_MUTED_TEXT);
    colors[1]=role(&s->theme,DLUMA_EDITOR_THEME_ROLE_PANEL);
    colors[2]=role(&s->theme,s->theme.kind==DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST ? DLUMA_EDITOR_THEME_ROLE_PANEL : DLUMA_EDITOR_THEME_ROLE_HOVER);
    if (s->direct_object) for (marker=25U; marker<=31U && active_state(w,s); ++marker) {
        MarkerSnapshot *m=&s->markers[marker-25U];
        if (m->symbol!=expected_marker(marker) || *marker_field(s,marker,0)!=expected_marker(marker)) { restore_marker(w,s,marker); continue; }
        m->active=1;
        for (j=0; j<3U && active_state(w,s); ++j) {
            m->applied[j]=colors[j];
            (void)sci(w,messages[j],marker,colors[j]);
        }
    }
    if (!active_state(w,s)) return;
    if (s->indicator_style==1 && s->indicator_fore==RGB(255,0,0) &&
        (int)sci(w,SCI_INDICGETSTYLE,8,0)==1 && active_state(w,s)) {
        current=(COLORREF)sci(w,SCI_INDICGETFORE,8,0);
        if (active_state(w,s) && (current==RGB(255,0,0) || (s->indicator_active && current==s->indicator_applied))) {
            s->indicator_applied=role(&s->theme,DLUMA_EDITOR_THEME_ROLE_ERROR);
            s->indicator_active=1;
            (void)sci(w,SCI_INDICSETFORE,8,s->indicator_applied);
        } else s->indicator_active=0;
    } else restore_indicator(w,s);
}
typedef struct PopupFrame {
    struct PopupFrame *previous;
    HWND source;
    HDC dc;
    HBRUSH normal, selection;
    COLORREF back, text, selected_back, selected_text;
    int transaction, admitted;
} PopupFrame;
static DWORD popup_tls = TLS_OUT_OF_INDEXES;
static PopupFrame *popup_raw(void)
{
    return popup_tls==TLS_OUT_OF_INDEXES ? NULL : (PopupFrame *)TlsGetValue(popup_tls);
}
static PopupFrame *popup_unmask(PopupFrame *frame)
{
    return (PopupFrame *)((uintptr_t)frame & ~(uintptr_t)1U);
}
static const WCHAR popup_property[] = L"DLUMA.EditorTheme.Scintilla.Popup.1";
static SRWLOCK popup_lock;
static PVOID volatile *popup_color_slot, *popup_fill_slot;
static DWORD popup_color_protection, popup_fill_protection;
static int popup_color_installed, popup_fill_installed, popup_stopping;
static int popup_color_pending, popup_fill_pending;
static volatile LONG popup_ready;
static DWORD (WINAPI *popup_original_color)(int);
static int (WINAPI *popup_original_fill)(HDC, const RECT *, HBRUSH);
static BYTE *popup_module;
static volatile LONG popup_callbacks;
static volatile LONG popup_tls_failed;

static DWORD WINAPI popup_color(int index)
{
    uintptr_t site = (uintptr_t)_ReturnAddress() - (uintptr_t)popup_module;
    PopupFrame *f = popup_raw(); DWORD result;
    InterlockedIncrement(&popup_callbacks);
    if (!InterlockedCompareExchange(&popup_tls_failed,0,0) && f && !((uintptr_t)f & 1U) && f->admitted && ((site == 0xEC301U && index == COLOR_HIGHLIGHT) ||
        (site == 0xEC32DU && index == COLOR_WINDOW) || site == 0xEC344U)) {
        if (index == COLOR_WINDOW) result=f->back;
        else if (index == COLOR_WINDOWTEXT) result=f->text;
        else if (index == COLOR_HIGHLIGHT) result=f->selected_back;
        else if (index == COLOR_HIGHLIGHTTEXT) result=f->selected_text;
        else result=popup_original_color(index);
    } else result=popup_original_color(index);
    InterlockedDecrement(&popup_callbacks);
    return result;
}
static int WINAPI popup_fill(HDC dc, const RECT *rect, HBRUSH brush)
{
    uintptr_t site = (uintptr_t)_ReturnAddress() - (uintptr_t)popup_module;
    PopupFrame *f = popup_raw(); int result;
    InterlockedIncrement(&popup_callbacks);
    if (!InterlockedCompareExchange(&popup_tls_failed,0,0) && f && !((uintptr_t)f & 1U) && f->admitted && f->dc == dc) {
        if ((site == 0xEC2DFU || site == 0xEC322U) && brush == (HBRUSH)(INT_PTR)(COLOR_WINDOW+1) && f->normal) brush=f->normal;
        else if (site == 0xEC2F6U && brush == (HBRUSH)(INT_PTR)(COLOR_HIGHLIGHT+1) && f->selection) brush=f->selection;
    }
    result=popup_original_fill(dc, rect, brush);
    InterlockedDecrement(&popup_callbacks);
    return result;
}
static int popup_exchange(PVOID volatile *slot, void *expected, void *replacement, DWORD *saved, int *pending)
{
    DWORD old, ignored; void *observed;
    if (!VirtualProtect((void *)slot, sizeof(void *), PAGE_READWRITE, &old)) return 0;
    if (!*pending) *saved=old;
    *pending=1;
    observed=InterlockedCompareExchangePointer(slot, replacement, expected);
    if (VirtualProtect((void *)slot, sizeof(void *), *saved, &ignored)) *pending=0;
    return observed == expected;
}
static int popup_install(void)
{
    HMODULE module=scintilla_profile(), user=GetModuleHandleW(L"user32.dll");
    int ok=0;
    if (!module || !user) return 0;
    AcquireSRWLockExclusive(&popup_lock);
    if (popup_stopping) goto done;
    if (popup_color_installed && popup_fill_installed) { ok=!popup_color_pending && !popup_fill_pending; goto done; }
    if (popup_tls==TLS_OUT_OF_INDEXES) popup_tls=TlsAlloc();
    if (popup_tls==TLS_OUT_OF_INDEXES) goto done;
    popup_module=(BYTE *)module;
    popup_color_slot=(PVOID volatile *)(popup_module+0xFD588U);
    popup_fill_slot=(PVOID volatile *)(popup_module+0xFD640U);
    popup_original_color=(DWORD (WINAPI *)(int))GetProcAddress(user,"GetSysColor");
    popup_original_fill=(int (WINAPI *)(HDC,const RECT *,HBRUSH))GetProcAddress(user,"FillRect");
    if (!popup_original_color || !popup_original_fill) goto done;
    if (!popup_color_installed) popup_color_installed=popup_exchange(popup_color_slot,(void *)popup_original_color,(void *)popup_color,&popup_color_protection,&popup_color_pending);
    if (popup_color_installed && !popup_color_pending && !popup_fill_installed) popup_fill_installed=popup_exchange(popup_fill_slot,(void *)popup_original_fill,(void *)popup_fill,&popup_fill_protection,&popup_fill_pending);
    ok=popup_color_installed && popup_fill_installed && !popup_color_pending && !popup_fill_pending;
done:
    if (ok) InterlockedExchange(&popup_ready,1);
    ReleaseSRWLockExclusive(&popup_lock);
    return ok;
}
static void popup_restore(PVOID volatile *slot, void *original, void *replacement, DWORD *protection, int *installed, int *pending)
{
    DWORD ignored;
    if (*installed && popup_exchange(slot,replacement,original,protection,pending)) *installed=0;
    if (!*installed && *pending && VirtualProtect((void *)slot,sizeof(void *),*protection,&ignored)) *pending=0;
}
void dluma_editor_theme_scintilla_stop(void)
{
    AcquireSRWLockExclusive(&popup_lock);
    popup_stopping=1;
    InterlockedExchange(&popup_ready,0);
    if (popup_color_slot) popup_restore(popup_color_slot,(void *)popup_original_color,(void *)popup_color,&popup_color_protection,&popup_color_installed,&popup_color_pending);
    if (popup_fill_slot) popup_restore(popup_fill_slot,(void *)popup_original_fill,(void *)popup_fill,&popup_fill_protection,&popup_fill_installed,&popup_fill_pending);
    ReleaseSRWLockExclusive(&popup_lock);
}
static int admit(HWND w)
{
    WCHAR name[64]; HWND parent; HMODULE lexer;
    DWORD pid = 0;
    if (!owner(w) || !GetWindowThreadProcessId(w, &pid) || pid != GetCurrentProcessId() ||
        GetClassNameW(w, name, 64) <= 0 || lstrcmpiW(name, L"Scintilla") != 0 || GetDlgCtrlID(w) != 3581) return 0;
    parent = GetParent(w); lexer = GetModuleHandleW(L"SciLexer_x64.dll");
    return parent && lexer && (HMODULE)GetClassLongPtrW(w, GCLP_HMODULE) == lexer &&
        GetDlgItem(parent, 3581) == w && GetDlgItem(parent, 3602) && GetDlgItem(parent, 3603) && GetDlgItem(parent, 3608);
}
static void read_style(HWND w, ScintillaTheme *s, unsigned int i)
{
    s->styles[i].original_fore = (COLORREF)sci(w, SCI_STYLEGETFORE, i, 0);
    s->styles[i].original_back = (COLORREF)sci(w, SCI_STYLEGETBACK, i, 0);
    s->styles[i].original_underline = (int)sci(w, SCI_STYLEGETUNDERLINE, i, 0);
}
int dluma_editor_theme_scintilla_attach(HWND w)
{
    ScintillaTheme *s; unsigned int i; int bits;
    if (GetPropW(w, property_name)) return 1;
    if (GetPropW(w, detaching_name) || !admit(w)) return 0;
    if (!refresh_message) refresh_message = RegisterWindowMessageW(refresh_name);
    if (!refresh_message) return 0;
    s = (ScintillaTheme *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return 0;
    s->window=w;
    bits = (int)sci(w, SCI_GETSTYLEBITS, 0, 0);
    if (bits < 1 || bits > 8) { HeapFree(GetProcessHeap(), 0, s); return 0; }
    s->count = 1U << bits; if (s->count <= STYLE_CALLTIP) s->count = STYLE_CALLTIP + 1U;
    s->lexer = (int)sci(w, SCI_GETLEXER, 0, 0);
    for (i=0; i<s->count; ++i) read_style(w, s, i);
    s->original_caret = (COLORREF)sci(w, SCI_GETCARETFORE, 0, 0);
    s->original_line = (COLORREF)sci(w, SCI_GETCARETLINEBACK, 0, 0);
    s->original_line_visible = (int)sci(w, SCI_GETCARETLINEVISIBLE, 0, 0);
    s->indicator_style=(int)sci(w,SCI_INDICGETSTYLE,8,0);
    s->indicator_fore=(COLORREF)sci(w,SCI_INDICGETFORE,8,0);
    for (i=0; i<sizeof(setter_messages)/sizeof(setter_messages[0]); ++i) s->setter[i].message = setter_messages[i];
    (void)snapshot_calltip(w, s);
    if (s->direct_object) (void)popup_install();
    if (!SetPropW(w, property_name, s)) { HeapFree(GetProcessHeap(), 0, s); return 0; }
    s->references = 1;
    InterlockedIncrement(&live_states);
    return 1;
}
void dluma_editor_theme_scintilla_retry_dialog(HWND dialog, const DlumaEditorThemeResolved *theme)
{
    HWND source;
    if (!dialog || !theme || !owner(dialog)) return;
    source = GetDlgItem(dialog, 3581);
    if (source && dluma_editor_theme_scintilla_attach(source))
        dluma_editor_theme_scintilla_apply(source, theme);
}
int dluma_editor_theme_scintilla_owns_parent(HWND window)
{
    HWND source=GetDlgItem(window,3581);
    ScintillaTheme *state=source ? (ScintillaTheme *)GetPropW(source,property_name) : NULL;
    return state && !state->destroyed && !state->detached && owner(source) && GetParent(source)==window;
}
static COLORREF syntax(const ScintillaTheme *s, unsigned int style)
{
    int dark = s->theme.kind == DLUMA_EDITOR_THEME_KIND_DARK;
    int lua = s->lexer == SCLEX_LUA;
    int cpp = s->lexer == SCLEX_CPP || s->lexer == SCLEX_CPPNOCASE;
    if (s->theme.kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST) return role(&s->theme, DLUMA_EDITOR_THEME_ROLE_TEXT);
    if (s->lexer==SCLEX_PXSL) {
        COLORREF original=s->styles[style].original_fore;
        if (style==2 || original==RGB(0,128,0))
            return dark ? RGB(0x88,0xAF,0x78) : RGB(0x46,0x71,0x3E);
        if (style==1 || original==RGB(100,100,100))
            return role(&s->theme,DLUMA_EDITOR_THEME_ROLE_MUTED_TEXT);
        if (style==8 || original==RGB(128,0,128))
            return dark ? RGB(0xDC,0xA7,0x78) : RGB(0x8F,0x4E,0x0F);
        if (style==6 || style==11 || style==14 || original==RGB(0,0,128))
            return dark ? RGB(0x85,0xB8,0xF0) : RGB(0x18,0x5C,0xA8);
        if (original==RGB(128,0,0) || original==RGB(0,128,128))
            return dark ? RGB(0xC9,0xA4,0xE8) : RGB(0x74,0x46,0xA8);
        return role(&s->theme,DLUMA_EDITOR_THEME_ROLE_TEXT);
    }
    if (!lua && !cpp) return role(&s->theme, DLUMA_EDITOR_THEME_ROLE_TEXT);
    if (style == 1 || style == 2 || style == 3 || (cpp && (style == 15 || style == 17 || style == 18))) return dark ? RGB(0x88,0xAF,0x78) : RGB(0x46,0x71,0x3E);
    if (style == 4) return dark ? RGB(0xC9,0xA4,0xE8) : RGB(0x74,0x46,0xA8);
    if (style == 6 || style == 7 || style == 8 || style == 12 || (cpp && (style == 13 || style == 14 || (style >= 20 && style <= 22)))) return dark ? RGB(0xDC,0xA7,0x78) : RGB(0x8F,0x4E,0x0F);
    if (style == 5 || (lua && style >= 13 && style <= 19) || (cpp && (style == 16 || style == 19))) return dark ? RGB(0x85,0xB8,0xF0) : RGB(0x18,0x5C,0xA8);
    return role(&s->theme, DLUMA_EDITOR_THEME_ROLE_TEXT);
}
void dluma_editor_theme_scintilla_apply(HWND w, const DlumaEditorThemeResolved *theme)
{
    ScintillaTheme *s = (ScintillaTheme *)GetPropW(w, property_name);
    unsigned int i; COLORREF fore, back; int underline;
    if (!s || !theme || !owner(w) || theme->size != sizeof(*theme) || theme->version != 1 || s->applying) return;
    ++s->references;
    s->theme = *theme; s->has_theme = 1; s->applying = 1;
    s->lexer = (int)sci(w, SCI_GETLEXER, 0, 0);
    for (i=0; i<s->count && active_state(w, s); ++i) {
        fore = syntax(s, i); back = role(theme, DLUMA_EDITOR_THEME_ROLE_SURFACE);
        if (i == STYLE_LINENUMBER) { fore=role(theme, DLUMA_EDITOR_THEME_ROLE_MUTED_TEXT); back=role(theme, DLUMA_EDITOR_THEME_ROLE_PANEL); }
        else if (i == STYLE_BRACELIGHT || i == STYLE_BRACEBAD) {
            if (theme->kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST) { fore=role(theme, DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT); back=role(theme, DLUMA_EDITOR_THEME_ROLE_SELECTION); }
            else if (i == STYLE_BRACELIGHT) back = theme->kind == DLUMA_EDITOR_THEME_KIND_DARK ? RGB(0x26,0x47,0x2A) : RGB(0xD8,0xF2,0xD6);
            else back = theme->kind == DLUMA_EDITOR_THEME_KIND_DARK ? RGB(0x65,0x28,0x28) : RGB(0xFB,0xE3,0xE3);
        } else if (i == STYLE_CALLTIP) back=role(theme, DLUMA_EDITOR_THEME_ROLE_PANEL);
        underline = s->styles[i].original_underline || (theme->kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST &&
            (i == 5 || i == STYLE_BRACEBAD || (s->lexer==SCLEX_PXSL && (i==6 || i==11 || i==14))));
        s->styles[i].applied_fore=fore; s->styles[i].applied_back=back; s->styles[i].applied_underline=underline;
        (void)sci(w, SCI_STYLESETFORE, i, fore); (void)sci(w, SCI_STYLESETBACK, i, back);
        (void)sci(w, SCI_STYLESETUNDERLINE, i, underline);
    }
    if (!active_state(w, s)) goto done;
    s->applied_caret=role(theme, DLUMA_EDITOR_THEME_ROLE_TEXT); s->applied_line=role(theme, DLUMA_EDITOR_THEME_ROLE_ALTERNATE); s->applied_line_visible=1;
    (void)sci(w, SCI_SETCARETFORE, s->applied_caret, 0);
    (void)sci(w, SCI_SETCARETLINEBACK, s->applied_line, 0);
    (void)sci(w, SCI_SETCARETLINEVISIBLE, 1, 0);
    for (i=0; i<sizeof(setter_messages)/sizeof(setter_messages[0]) && active_state(w, s); ++i) if (s->setter[i].known) {
        UINT m=s->setter[i].message; COLORREF c=role(theme, DLUMA_EDITOR_THEME_ROLE_TEXT);
        if (m==SCI_SETSELFORE) c=role(theme, GetFocus()==w || theme->kind==DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST ? DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT);
        else if (m==SCI_SETSELBACK) c=role(theme, GetFocus()==w ? DLUMA_EDITOR_THEME_ROLE_SELECTION : DLUMA_EDITOR_THEME_ROLE_INACTIVE_SELECTION);
        else if (m==SCI_SETWHITESPACEFORE) c=role(theme, DLUMA_EDITOR_THEME_ROLE_MUTED_TEXT);
        else if (m==SCI_SETWHITESPACEBACK) c=role(theme, DLUMA_EDITOR_THEME_ROLE_SURFACE);
        else if (m==SCI_CALLTIPSETBACK || m==SCI_SETFOLDMARGINCOLOUR || m==SCI_SETFOLDMARGINHICOLOUR) c=role(theme, DLUMA_EDITOR_THEME_ROLE_PANEL);
        else if (m==SCI_CALLTIPSETFOREHLT) c=role(theme, DLUMA_EDITOR_THEME_ROLE_ACCENT);
        if (m==SCI_CALLTIPSETBACK || m==SCI_CALLTIPSETFORE || m==SCI_CALLTIPSETFOREHLT) (void)sci(w,m,c,0);
        else (void)sci(w,m,1,c);
    }
    if (active_state(w,s)) apply_markers_and_indicator(w,s);
done:
    s->applying=0;
    release_state(s);
}

int dluma_editor_theme_scintilla_message(HWND w, unsigned int stage, UINT message, WPARAM wp, LPARAM lp, LRESULT *result)
{
    ScintillaTheme *s = (ScintillaTheme *)GetPropW(w, property_name);
    uint64_t *scratch = NULL;
    unsigned int i; int changed = 0, handled = 0;
    if (message == SCI_AUTOCSHOW || message == WM_DRAWITEM) {
        PopupFrame *f, *transaction; DRAWITEMSTRUCT *draw;
        scratch=dluma_editor_ui_invocation_slot(DLUMA_EDITOR_UI_OWNER_THEME,DLUMA_EDITOR_UI_SLOT_SCINTILLA);
        if (stage == DLUMA_EDITOR_UI_AFTER && scratch && *scratch) {
            if (*scratch & 2U) {
                uintptr_t prior=(uintptr_t)(*scratch & ~(uint64_t)2U); *scratch=0;
                if (!TlsSetValue(popup_tls,(void *)prior)) InterlockedIncrement(&popup_tls_failed);
                return 0;
            }
            f=(PopupFrame *)(uintptr_t)*scratch; *scratch=0;
            if (!TlsSetValue(popup_tls,f->previous)) { InterlockedIncrement(&popup_tls_failed); return 0; }
            if (f->normal) DeleteObject(f->normal);
            if (f->selection) DeleteObject(f->selection);
            HeapFree(GetProcessHeap(),0,f);
            return 0;
        }
        if (stage == DLUMA_EDITOR_UI_BEFORE && scratch && !*scratch &&
            (InterlockedCompareExchange(&popup_ready,0,0) || (message==WM_DRAWITEM && popup_raw()))) {
            if (message == SCI_AUTOCSHOW && s && s->direct_object && s->has_theme) {
                f=(PopupFrame *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*f));
                if (f) {
                    f->transaction=1; f->source=w; f->previous=popup_raw();
                    if (TlsSetValue(popup_tls,f)) *scratch=(uint64_t)(uintptr_t)f;
                    else { InterlockedIncrement(&popup_tls_failed); HeapFree(GetProcessHeap(),0,f); }
                }
            } else if (message == WM_DRAWITEM && lp) {
                draw=(DRAWITEMSTRUCT *)lp;
                if (InterlockedCompareExchange(&popup_ready,0,0) && draw->CtlType == ODT_LISTBOX && draw->hwndItem && draw->hDC) {
                    s=(ScintillaTheme *)GetPropW((HWND)GetPropW(draw->hwndItem,popup_property),property_name);
                    if (!s || s->popup_list!=draw->hwndItem || s->detached || s->destroyed) {
                        transaction=popup_unmask(popup_raw());
                        while (transaction && !transaction->transaction) transaction=popup_unmask(transaction->previous);
                        s=transaction ? (ScintillaTheme *)GetPropW(transaction->source,property_name) : NULL;
                    }
                    if (s && s->has_theme && s->direct_object && !s->detached) {
                        HWND popup=NULL;
                        __try { BYTE *list=*(BYTE **)(s->direct_object+3632U); if (list) popup=*(HWND *)(list+8U); }
                        __except(EXCEPTION_EXECUTE_HANDLER) { popup=NULL; }
                        if (popup && (popup==draw->hwndItem || GetParent(draw->hwndItem)==popup) && GetParent(draw->hwndItem)==w) {
                            if (s->popup_list && s->popup_list!=draw->hwndItem && IsWindow(s->popup_list)) RemovePropW(s->popup_list,popup_property);
                            if (SetPropW(draw->hwndItem,popup_property,s->window)) {
                                s->popup_list=draw->hwndItem;
                                f=(PopupFrame *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*f));
                                if (f) {
                                    f->dc=draw->hDC; f->back=role(&s->theme,DLUMA_EDITOR_THEME_ROLE_SURFACE);
                                    f->text=role(&s->theme,DLUMA_EDITOR_THEME_ROLE_TEXT);
                                    f->selected_back=role(&s->theme,DLUMA_EDITOR_THEME_ROLE_SELECTION);
                                    f->selected_text=role(&s->theme,DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT);
                                    f->normal=CreateSolidBrush(f->back); f->selection=CreateSolidBrush(f->selected_back);
                                    if (f->normal && f->selection) {
                                        f->previous=popup_raw(); f->admitted=1;
                                        if (TlsSetValue(popup_tls,f)) *scratch=(uint64_t)(uintptr_t)f;
                                        else { DeleteObject(f->normal); DeleteObject(f->selection); HeapFree(GetProcessHeap(),0,f); }
                                    } else {
                                        if (f->normal) DeleteObject(f->normal);
                                        if (f->selection) DeleteObject(f->selection);
                                        HeapFree(GetProcessHeap(),0,f);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (message==WM_DRAWITEM && !*scratch && popup_raw()) {
                uintptr_t prior=(uintptr_t)popup_raw();
                if (TlsSetValue(popup_tls,(void *)(prior | (uintptr_t)1U)))
                    *scratch=(uint64_t)(prior | (uintptr_t)2U);
                else InterlockedIncrement(&popup_tls_failed);
            }
        }
        s=(ScintillaTheme *)GetPropW(w,property_name);
    }
    if (message == WM_NCDESTROY) {
        scratch = dluma_editor_ui_invocation_slot(DLUMA_EDITOR_UI_OWNER_THEME,DLUMA_EDITOR_UI_SLOT_SCINTILLA);
        if (stage == DLUMA_EDITOR_UI_AFTER && scratch && *scratch) {
            s = (ScintillaTheme *)(uintptr_t)*scratch;
            *scratch = 0;
            release_state(s);
            return 0;
        }
        if (!s) s = (ScintillaTheme *)GetPropW(w, detaching_name);
    }
    if (!s) return 0;
    ++s->references;
    if (stage == DLUMA_EDITOR_UI_BEFORE && message == WM_NCDESTROY) {
        if (scratch && !*scratch) { ++s->references; *scratch = (uint64_t)(uintptr_t)s; }
        remove_state(w, s, 1);
        goto done;
    }
    if (stage == DLUMA_EDITOR_UI_DESTROY) {
        if (message == 0 && owner(w)) dluma_editor_theme_scintilla_detach(w);
        else remove_state(w, s, 1);
        goto done;
    }
    if (!owner(w) || s->destroyed || s->detached) goto done;
    if (stage == DLUMA_EDITOR_UI_BEFORE && message == refresh_message) {
        s->queued = 0;
        if (s->has_theme) dluma_editor_theme_scintilla_apply(w, &s->theme);
        if (result) *result = 0;
        handled = 1;
        goto done;
    }
    if (stage == DLUMA_EDITOR_UI_BEFORE && !s->applying) {
        if (wp>=25U && wp<=31U &&
            ((message==SCI_MARKERDEFINE && (DWORD)lp!=expected_marker((unsigned int)wp)) ||
             message==SCI_MARKERDEFINEPIXMAP || message==SCI_MARKERDEFINERGBAIMAGE)) {
            s->applying=1; restore_marker(w,s,(unsigned int)wp); s->applying=0;
        } else if (message==SCI_INDICSETSTYLE && wp==8U && lp!=1) {
            s->applying=1; restore_indicator(w,s); s->applying=0;
        }
        goto done;
    }
    if (stage != DLUMA_EDITOR_UI_AFTER || s->applying) goto done;
    if (message == SCI_STYLESETFORE && wp < s->count) { s->styles[wp].original_fore=(COLORREF)lp; changed=1; }
    else if (message == SCI_STYLESETBACK && wp < s->count) { s->styles[wp].original_back=(COLORREF)lp; changed=1; }
    else if (message == SCI_STYLESETUNDERLINE && wp < s->count) { s->styles[wp].original_underline=lp ? 1 : 0; changed=1; }
    else if (message == SCI_STYLECLEARALL) {
        for (i=0; i<s->count; ++i) {
            s->styles[i].original_fore=s->styles[STYLE_DEFAULT].original_fore;
            s->styles[i].original_back=s->styles[STYLE_DEFAULT].original_back;
            s->styles[i].original_underline=s->styles[STYLE_DEFAULT].original_underline;
        }
        changed=1;
    } else if (message == SCI_SETCARETFORE) { s->original_caret=(COLORREF)wp; changed=1; }
    else if (message == SCI_SETCARETLINEBACK) { s->original_line=(COLORREF)wp; changed=1; }
    else if (message == SCI_SETCARETLINEVISIBLE) { s->original_line_visible=wp ? 1 : 0; changed=1; }
    else if (message == SCI_SETLEXER || message == WM_SETFOCUS || message == WM_KILLFOCUS) changed=1;
    else for (i=0; i<sizeof(setter_messages)/sizeof(setter_messages[0]); ++i) if (message==s->setter[i].message) {
        s->setter[i].original_wparam=wp; s->setter[i].original_lparam=lp; s->setter[i].known=1; changed=1; break;
    }
    if (message == SCI_CALLTIPSETBACK) s->styles[STYLE_CALLTIP].original_back=(COLORREF)wp;
    else if (message == SCI_CALLTIPSETFORE) s->styles[STYLE_CALLTIP].original_fore=(COLORREF)wp;
    if (message == SCI_SETSELFORE) { s->original_additional_fore=(COLORREF)lp; s->additional_known|=1U; }
    else if (message == SCI_SETSELBACK) { s->original_additional_back=(COLORREF)lp; s->additional_known|=2U; }
    else if (message == SCI_SETADDITIONALSELFORE) { s->original_additional_fore=(COLORREF)wp; s->additional_known|=1U; changed=1; }
    else if (message == SCI_SETADDITIONALSELBACK) { s->original_additional_back=(COLORREF)wp; s->additional_known|=2U; changed=1; }
    if (s->direct_object && wp>=25U && wp<=31U) {
        MarkerSnapshot *m=&s->markers[(unsigned int)wp-25U];
        if (message==SCI_MARKERSETFORE) { m->original[0]=(COLORREF)lp; changed=1; }
        else if (message==SCI_MARKERSETBACK) { m->original[1]=(COLORREF)lp; changed=1; }
        else if (message==SCI_MARKERSETBACKSELECTED) { m->original[2]=(COLORREF)lp; changed=1; }
        else if (message==SCI_MARKERDEFINE || message==SCI_MARKERDEFINEPIXMAP || message==SCI_MARKERDEFINERGBAIMAGE) {
            m->symbol=*marker_field(s,(unsigned int)wp,0); changed=1;
        }
    }
    if (wp==8U && message==SCI_INDICSETSTYLE) { s->indicator_style=(int)lp; changed=1; }
    else if (wp==8U && message==SCI_INDICSETFORE) { s->indicator_fore=(COLORREF)lp; changed=1; }
    if (changed && s->has_theme && !s->queued) {
        s->queued=1;
        if (!PostMessageW(w, refresh_message, 0, 0)) s->queued=0;
    }
done:
    release_state(s);
    return handled;
}
void dluma_editor_theme_scintilla_detach(HWND w)
{
    ScintillaTheme *s = (ScintillaTheme *)GetPropW(w, property_name);
    unsigned int i;
    COLORREF calltip_fore, calltip_back;
    if (!s || !owner(w)) return;
    if (s->popup_list && IsWindow(s->popup_list) && GetPropW(s->popup_list,popup_property)==w)
        RemovePropW(s->popup_list,popup_property);
    ++s->references;
    if (!SetPropW(w, detaching_name, s)) { release_state(s); return; }
    remove_state(w, s, 0);
    s->applying=1;
#define DETACH_LIVE() (!s->destroyed && owner(w) && GetPropW(w, detaching_name) == s)
    if (s->has_theme) {
        for (i=25U; i<=31U && DETACH_LIVE(); ++i) restore_marker(w,s,i);
        if (DETACH_LIVE()) restore_indicator(w,s);
        if (!DETACH_LIVE()) goto done;
        calltip_fore=(COLORREF)sci(w, SCI_STYLEGETFORE, STYLE_CALLTIP, 0);
        calltip_back=(COLORREF)sci(w, SCI_STYLEGETBACK, STYLE_CALLTIP, 0);
        /* Calltip setters also update style 38. Restore the independent style afterward. */
        for (i=4; i<=6 && DETACH_LIVE(); ++i) if (s->setter[i].known)
            (void)sci(w, s->setter[i].message, s->setter[i].original_wparam, s->setter[i].original_lparam);
        for (i=0; i<s->count && DETACH_LIVE(); ++i) {
            if (i==STYLE_CALLTIP) (void)sci(w, SCI_STYLESETFORE, i, calltip_fore==s->styles[i].applied_fore ? s->styles[i].original_fore : calltip_fore);
            else if ((COLORREF)sci(w, SCI_STYLEGETFORE, i, 0)==s->styles[i].applied_fore)
                (void)sci(w, SCI_STYLESETFORE, i, s->styles[i].original_fore);
            if (i==STYLE_CALLTIP) (void)sci(w, SCI_STYLESETBACK, i, calltip_back==s->styles[i].applied_back ? s->styles[i].original_back : calltip_back);
            else if ((COLORREF)sci(w, SCI_STYLEGETBACK, i, 0)==s->styles[i].applied_back)
                (void)sci(w, SCI_STYLESETBACK, i, s->styles[i].original_back);
            if ((int)sci(w, SCI_STYLEGETUNDERLINE, i, 0)==s->styles[i].applied_underline)
                (void)sci(w, SCI_STYLESETUNDERLINE, i, s->styles[i].original_underline);
        }
        if (!DETACH_LIVE()) goto done;
        if ((COLORREF)sci(w, SCI_GETCARETFORE, 0, 0)==s->applied_caret) (void)sci(w, SCI_SETCARETFORE, s->original_caret, 0);
        if ((COLORREF)sci(w, SCI_GETCARETLINEBACK, 0, 0)==s->applied_line) (void)sci(w, SCI_SETCARETLINEBACK, s->original_line, 0);
        if ((int)sci(w, SCI_GETCARETLINEVISIBLE, 0, 0)==s->applied_line_visible) (void)sci(w, SCI_SETCARETLINEVISIBLE, s->original_line_visible, 0);
        for (i=0; i<sizeof(setter_messages)/sizeof(setter_messages[0]) && DETACH_LIVE(); ++i) if ((i<4 || i>6) && s->setter[i].known)
            (void)sci(w, s->setter[i].message, s->setter[i].original_wparam, s->setter[i].original_lparam);
        if (DETACH_LIVE() && (s->additional_known & 1U)) (void)sci(w, SCI_SETADDITIONALSELFORE, s->original_additional_fore, 0);
        if (DETACH_LIVE() && (s->additional_known & 2U)) (void)sci(w, SCI_SETADDITIONALSELBACK, s->original_additional_back, 0);
    }
done:
    if (GetPropW(w, detaching_name) == s) RemovePropW(w, detaching_name);
    release_state(s);
#undef DETACH_LIVE
}
int dluma_editor_theme_scintilla_callbacks_drained(void)
{
    int drained;
    AcquireSRWLockShared(&popup_lock);
    drained=!popup_color_installed && !popup_fill_installed && !popup_color_pending && !popup_fill_pending;
    ReleaseSRWLockShared(&popup_lock);
    return drained && InterlockedCompareExchange(&popup_callbacks,0,0)==0 &&
        InterlockedCompareExchange(&popup_tls_failed,0,0)==0 &&
        InterlockedCompareExchange(&live_states, 0, 0) == 0;
}
