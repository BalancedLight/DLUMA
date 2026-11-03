#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <stdint.h>
#include <string.h>
#include "dlw_editor_theme_mfc.h"
#include "dlw_editor_theme_profile.h"
#include "../build/generated/editor-theme/editor_theme_palette.generated.h"

extern int dluma_editor_is_main_window(HWND, DWORD);
#define MANAGER_VTABLE_RVA 0x14E7C58U
#define MANAGER_CLASS_RVA 0x14E7C20U
#define MANAGER_CLASS_FN_RVA 0x29130U
#define MANAGER_CONTENT_RVA 0x29140U
#define MANAGER_GLOBAL_RVA 0x3DA310U
#define MANAGER_READ_RVA 0x8443U
#define MFC_PERMANENT_RVA 0x26B460U
#define MFC_KIND_RVA 0x297190U
#define MFC_MENU_CLASS_RVA 0x3CA788U
#define MFC_POPUP_CLASS_RVA 0x3CB130U
#define MFC_STATUS_CLASS_RVA 0x2F9480U
#define MFC_TOOLBAR_CLASS_RVA 0x3CDAA8U
#define MFC_COLORBAR_CLASS_RVA 0x3C9280U
#define MFC_CAPTIONBAR_CLASS_RVA 0x2C8308U
#define MANAGER_SLOTS 202U

typedef void *(__cdecl *PermanentFn)(HWND);
typedef int (__fastcall *IsKindFn)(void *, const void *);
typedef void *(__fastcall *ClassFn)(void *);
typedef void (__fastcall *BarFn)(void *, void *, void *, const RECT *, const RECT *, int);
typedef void (__fastcall *SeparatorFn)(void *, void *, void *, const RECT *, int);
typedef COLORREF (__fastcall *CaptionFn)(void *, void *, void *, int, const RECT *, const RECT *);
typedef void (__fastcall *HighlightFn)(void *, void *, void *, const RECT *, COLORREF *);
typedef void (__fastcall *ButtonFn)(void *, void *, void *, const RECT *, int);
typedef void (__fastcall *TabFn)(void *, void *, const RECT *, int, int, void *);
typedef void (__fastcall *TabContentFn)(void *, void *, RECT *, int, int, void *, COLORREF);
typedef COLORREF (__fastcall *StatusTextFn)(void *, void *, void *);
typedef COLORREF (__fastcall *ItemTextFn)(void *, void *);
typedef COLORREF (__fastcall *MenuTextFn)(void *, void *, int, int);
typedef COLORREF (__fastcall *ToolbarTextFn)(void *, void *, int);
typedef void (__fastcall *CaptionAreaFn)(void *, void *, void *, const RECT *);
typedef void (__fastcall *AutoFillFn)(void *, void *, const RECT *, void *);
typedef void (__fastcall *CaptionIconFn)(void *, void *, void *, unsigned int, int, int, POINT);
typedef void (__fastcall *TabCloseFn)(void *, void *, const RECT *, const void *, int, int, int);
typedef COLORREF (__fastcall *MiniCaptionFn)(void *, void *, const RECT *, void *, int);
typedef void (__fastcall *MiniBorderFn)(void *, void *, void *, const RECT *, const RECT *);
typedef struct MfcPalette {
    volatile LONG references;
    DlumaEditorThemeResolved theme;
    HBRUSH brushes[17];
    HPEN pens[17][3];
    COLORREF known[64];
    unsigned int known_count;
} MfcPalette;
typedef struct MfcPane {
    HWND window; void *object; unsigned int kind;
    LONG64 identity;
    uint64_t redraw_generation;
    int redraw_valid;
} MfcPane;
static struct {
    SRWLOCK lock;
    volatile LONG admission, installed, callbacks, panes;
    volatile LONG64 next_pane_identity;
    int retired;
    void *object;
    void **original;
    void *shadow[MANAGER_SLOTS + 1U];
    HWND owner;
    MfcPalette *palette;
    PermanentFn permanent;
    IsKindFn is_kind;
    void **global;
    void *manager_vtable, *manager_class, *manager_class_fn;
    void *classes[6];
    COLORREF *global_roles[6];
    BarFn bar;
    SeparatorFn separator;
    CaptionFn caption;
    HighlightFn highlight;
    TabFn tab;
    TabContentFn tab_content;
    ButtonFn button;
    StatusTextFn status_text;
    ItemTextFn highlighted_text, caption_text, autohide_text;
    MenuTextFn menu_text;
    ToolbarTextFn toolbar_text;
    CaptionAreaFn caption_area;
    AutoFillFn autohide_fill;
    CaptionIconFn caption_icon;
    TabCloseFn tab_close;
    MiniCaptionFn mini_caption;
    MiniBorderFn mini_border;
} manager;
static const WCHAR manager_property[] = L"DLUMA.EditorTheme.MfcManager.1";
static const WCHAR pane_property[] = L"DLUMA.EditorTheme.MfcPane.1";
static const BYTE get_instance_read[] = {0x48,0x8B,0x05,0xC6,0x1E,0x3D,0x00};
static const BYTE permanent_prologue[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0x46,0x61,0xEB,0xFF,
    0x48,0x8B,0x48,0x28,0x33,0xC0,0x48,0x85,0xC9,0x74,0x0C,0x48,0x83,0xC1,
    0x28,0x48,0x8B,0xD3,0xE8,0xD3,0x9F,0xFA,0xFF,0x48,0x83,0xC4,0x20,0x5B,0xC3
};
static const BYTE kind_prologue[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0x48,0x85,0xC9,0x75,0x06,0xE8,0xB5,0x27,0xF7,0xFF,0xCC,0x48,0x8B,0x01,0xFF,0x10};
static int readable(const void *pointer, size_t size, int write)
{
    MEMORY_BASIC_INFORMATION info; uintptr_t p = (uintptr_t)pointer; DWORD protection;
    if (!pointer || !size || p > UINTPTR_MAX-size || !VirtualQuery(pointer, &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS|PAGE_GUARD)) ||
        p + size > (uintptr_t)info.BaseAddress + info.RegionSize) return 0;
    protection = info.Protect & 0xffU;
    if (write) return protection == PAGE_READWRITE || protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    return protection == PAGE_READONLY || protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
        protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}
static COLORREF mfc_color(const DlumaEditorThemeResolved *t, unsigned int role)
{
    uint32_t c=t->argb[role]; return RGB((c>>16)&255U,(c>>8)&255U,c&255U);
}
static void palette_release(MfcPalette *p)
{
    unsigned int i, j;
    if (!p || InterlockedDecrement(&p->references)) return;
    for (i=0; i<17U; ++i) { if (p->brushes[i]) DeleteObject(p->brushes[i]); for (j=0; j<3U; ++j) if (p->pens[i][j]) DeleteObject(p->pens[i][j]); }
    HeapFree(GetProcessHeap(), 0, p);
}
static MfcPalette *palette_acquire(void *object)
{
    MfcPalette *p = NULL;
    InterlockedIncrement(&manager.callbacks);
    AcquireSRWLockShared(&manager.lock);
    if (manager.installed && manager.object == object && manager.palette) {
        p=manager.palette; InterlockedIncrement(&p->references);
    }
    ReleaseSRWLockShared(&manager.lock);
    return p;
}
static void callback_end(MfcPalette *p)
{
    palette_release(p); InterlockedDecrement(&manager.callbacks);
}
static HDC mfc_dc(void *dc) { return dc ? ((HDC *)dc)[1] : NULL; }
static int dc_dip(HDC dc, int value)
{
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    int result = MulDiv(value, dpi > 0 ? dpi : 96, 96);
    return result > 0 ? result : 1;
}
static unsigned int cached_pane(void *pane)
{
    HWND window; MfcPane *entry;
    if (!pane) return 0;
    window = *(HWND *)((BYTE *)pane + 64U);
    if (GetWindowThreadProcessId(window,NULL)!=GetCurrentThreadId()) return 0;
    entry = (MfcPane *)GetPropW(window, pane_property);
    return entry && entry->window == window && entry->object == pane ? entry->kind : 0U;
}
static int known_text_color(const MfcPalette *p, COLORREF color)
{
    unsigned int i;
    for (i=0; i<p->known_count; ++i) if (p->known[i]==color) return 1;
    return 0;
}
static COLORREF mapped_text(MfcPalette *p, COLORREF original, unsigned int role)
{
    return p && (original==CLR_DEFAULT || original==(COLORREF)-1 || known_text_color(p,original)) ? mfc_color(&p->theme,role) : original;
}
static void __fastcall themed_bar(void *self, void *dc, void *pane, const RECT *rect, const RECT *clip, int horizontal)
{
    MfcPalette *p=palette_acquire(self); HDC hdc=mfc_dc(dc);
    if (p && hdc && clip && cached_pane(pane) && (!IsRectEmpty(clip) || rect))
        FillRect(hdc, IsRectEmpty(clip) ? rect : clip, p->brushes[DLUMA_EDITOR_THEME_ROLE_PANEL]);
    else manager.bar(self,dc,pane,rect,clip,horizontal);
    callback_end(p);
}
static void __fastcall themed_separator(void *self, void *dc, void *pane, const RECT *rect, int horizontal)
{
    MfcPalette *p=palette_acquire(self); HDC hdc=mfc_dc(dc);
    if (p && hdc && rect && cached_pane(pane)) {
        RECT line=*rect;
        if (horizontal) { line.left=(line.left+line.right)/2; line.right=line.left+dc_dip(hdc,1); }
        else { line.top=(line.top+line.bottom)/2; line.bottom=line.top+dc_dip(hdc,1); }
        FillRect(hdc,&line,p->brushes[DLUMA_EDITOR_THEME_ROLE_BORDER]);
    } else manager.separator(self,dc,pane,rect,horizontal);
    callback_end(p);
}
static COLORREF __fastcall themed_caption(void *self, void *dc, void *pane, int active, const RECT *rect, const RECT *text_rect)
{
    MfcPalette *p=palette_acquire(self); HDC hdc=mfc_dc(dc); COLORREF result;
    if (p && hdc && rect) {
        FillRect(hdc,rect,p->brushes[active ? DLUMA_EDITOR_THEME_ROLE_SELECTION : DLUMA_EDITOR_THEME_ROLE_PANEL]);
        result=mfc_color(&p->theme,active ? DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT);
    } else result=manager.caption(self,dc,pane,active,rect,text_rect);
    callback_end(p); return result;
}
static void __fastcall themed_highlight(void *self, void *dc, void *item, const RECT *rect, COLORREF *text)
{
    MfcPalette *p=palette_acquire(self); HDC hdc=mfc_dc(dc);
    if (p && hdc && rect && text && item) {
        unsigned int role=(*(uint32_t *)((BYTE *)item+40U)&0x40000U) ? DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT : DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT;
        FillRect(hdc,rect,p->brushes[DLUMA_EDITOR_THEME_ROLE_SELECTION]); *text=mapped_text(p,*text,role);
    }
    else manager.highlight(self,dc,item,rect,text);
    callback_end(p);
}
static void __fastcall themed_button(void *self, void *dc, void *button, const RECT *rect, int state)
{
    MfcPalette *p=palette_acquire(self); HDC hdc=mfc_dc(dc); unsigned int kind=0, style=0;
    if (button) { kind=cached_pane(*(void **)((BYTE *)button+120U)); style=*(uint32_t *)((BYTE *)button+40U); }
    if (p && hdc && rect && (kind==1U || kind==4U) && !(style&0x40000U) &&
        (state==1 || state==2 || (style&0x30000U))) FillRect(hdc,rect,p->brushes[DLUMA_EDITOR_THEME_ROLE_SELECTION]);
    else manager.button(self,dc,button,rect,state);
    callback_end(p);
}
static COLORREF __fastcall themed_toolbar_text(void *self, void *button, int state)
{
    MfcPalette *p=palette_acquire(self); COLORREF original=manager.toolbar_text(self,button,state), result=original;
    if (p && button && cached_pane(*(void **)((BYTE *)button+120U))) {
        unsigned int style=*(uint32_t *)((BYTE *)button+40U);
        unsigned int role=(style&0x40000U) ? DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT :
            (state==1 || state==2 || (style&0x30000U)) ? DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT;
        result=mapped_text(p,original,role);
    }
    callback_end(p); return result;
}
static COLORREF __fastcall themed_menu_text(void *self, void *button, int highlighted, int disabled)
{
    MfcPalette *p=palette_acquire(self); COLORREF original=manager.menu_text(self,button,highlighted,disabled);
    COLORREF result=mapped_text(p,original,disabled ? DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT : highlighted ? DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT);
    callback_end(p); return result;
}
static COLORREF __fastcall themed_highlighted_text(void *self, void *button)
{
    MfcPalette *p=palette_acquire(self); COLORREF original=manager.highlighted_text(self,button);
    COLORREF result=mapped_text(p,original,DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT);
    callback_end(p); return result;
}
static COLORREF __fastcall themed_status_text(void *self, void *bar, void *info)
{
    MfcPalette *p=palette_acquire(self); COLORREF original=manager.status_text(self,bar,info), result=original;
    if (p && info && cached_pane(bar)) {
        uint32_t flags=*(uint32_t *)((BYTE *)info+20U), custom=*(uint32_t *)((BYTE *)info+36U);
        if ((flags&0x4000000U) || custom==UINT32_MAX)
            result=mapped_text(p,original,(flags&0x4000000U) ? DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT);
    }
    callback_end(p); return result;
}
static COLORREF __fastcall themed_caption_text(void *self, void *bar)
{
    MfcPalette *p=palette_acquire(self); COLORREF original=manager.caption_text(self,bar), result=original;
    if (cached_pane(bar)==6U) result=mapped_text(p,original,DLUMA_EDITOR_THEME_ROLE_TEXT);
    callback_end(p); return result;
}
static COLORREF __fastcall themed_autohide_text(void *self, void *button)
{
    MfcPalette *p=palette_acquire(self); COLORREF original=manager.autohide_text(self,button), result=mapped_text(p,original,DLUMA_EDITOR_THEME_ROLE_TEXT);
    callback_end(p); return result;
}
static void __fastcall themed_caption_area(void *self, void *dc, void *bar, const RECT *rect)
{
    MfcPalette *p=palette_acquire(self); HDC hdc=mfc_dc(dc);
    if (p && hdc && rect && cached_pane(bar)==6U) { FillRect(hdc,rect,p->brushes[DLUMA_EDITOR_THEME_ROLE_PANEL]); FrameRect(hdc,rect,p->brushes[DLUMA_EDITOR_THEME_ROLE_BORDER]); }
    else manager.caption_area(self,dc,bar,rect);
    callback_end(p);
}
static void __fastcall themed_autohide_fill(void *self, void *dc, const RECT *rect, void *button)
{
    MfcPalette *p=palette_acquire(self); HDC hdc=mfc_dc(dc);
    if (p && hdc && rect) FillRect(hdc,rect,p->brushes[DLUMA_EDITOR_THEME_ROLE_PANEL]);
    else manager.autohide_fill(self,dc,rect,button);
    callback_end(p);
}
static int interface_glyph(HDC hdc, MfcPalette *p, POINT position, unsigned int image, unsigned int role)
{
    int extent=dc_dip(hdc,9), x=position.x, y=position.y, width=dc_dip(hdc,1);
    HPEN pen; HGDIOBJ old;
    if (width>3) width=3;
    pen=p->pens[role][width-1]; if (!pen) return 0;
    switch (image) {
    case 0: case 1: case 3: case 4: case 5: case 6: case 7: case 9:
    case 13: case 14: case 15: case 16: case 17: case 24: case 27: case 28: break;
    default: return 0;
    }
    old=SelectObject(hdc,pen);
#define LINE(x1,y1,x2,y2) do { MoveToEx(hdc,x+MulDiv((x1),extent,9),y+MulDiv((y1),extent,9),NULL); LineTo(hdc,x+MulDiv((x2),extent,9),y+MulDiv((y2),extent,9)); } while (0)
    if (image==5U) { LINE(1,1,8,8); LINE(7,1,0,8); }
    else if (image==3U) { LINE(1,7,8,7); }
    else if (image==4U) { LINE(3,1,8,1); LINE(8,1,8,6); LINE(1,3,6,3); LINE(6,3,6,8); LINE(6,8,1,8); LINE(1,8,1,3); }
    else if (image==6U) { LINE(1,1,8,1); LINE(8,1,8,8); LINE(8,8,1,8); LINE(1,8,1,1); }
    else if (image==15U) { LINE(1,4,7,4); LINE(3,1,3,7); LINE(5,2,5,6); LINE(5,2,7,2); LINE(5,6,7,6); }
    else if (image==16U) { LINE(4,1,4,8); LINE(1,3,7,3); LINE(2,5,6,5); LINE(2,5,2,1); LINE(6,5,6,1); }
    else if (image==0U || image==13U) { LINE(1,3,4,6); LINE(4,6,8,2); }
    else if (image==7U || image==24U) { LINE(1,6,4,3); LINE(4,3,8,7); }
    else if (image==1U || image==14U || image==28U) { LINE(3,1,6,4); LINE(6,4,2,8); }
    else { LINE(6,1,3,4); LINE(3,4,7,8); }
#undef LINE
    SelectObject(hdc,old); return 1;
}
static void __fastcall themed_caption_icon(void *self, void *dc, void *button, unsigned int image, int active, int disabled, POINT position)
{
    MfcPalette *p=palette_acquire(self); HDC hdc=mfc_dc(dc);
    if (!p || !hdc || !interface_glyph(hdc,p,position,image,disabled ? DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT : active ? DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT))
        manager.caption_icon(self,dc,button,image,active,disabled,position);
    callback_end(p);
}
static void __fastcall themed_tab_close(void *self, void *dc, const RECT *rect, const void *tabs, int highlighted, int pressed, int other)
{
    MfcPalette *p=palette_acquire(self); HDC hdc=mfc_dc(dc); POINT point; int extent;
    if (!p || !hdc || !rect || other) { manager.tab_close(self,dc,rect,tabs,highlighted,pressed,other); callback_end(p); return; }
    extent=dc_dip(hdc,9);
    if (rect->right-rect->left<extent || rect->bottom-rect->top<extent) { manager.tab_close(self,dc,rect,tabs,highlighted,pressed,other); callback_end(p); return; }
    if (highlighted || pressed) FillRect(hdc,rect,p->brushes[pressed ? DLUMA_EDITOR_THEME_ROLE_SELECTION : DLUMA_EDITOR_THEME_ROLE_HOVER]);
    point.x=rect->left+(rect->right-rect->left-extent)/2; point.y=rect->top+(rect->bottom-rect->top-extent)/2;
    (void)interface_glyph(hdc,p,point,5U,pressed ? DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT);
    callback_end(p);
}
static COLORREF __fastcall themed_mini_caption(void *self, void *dc, const RECT *rect, void *frame, int active)
{
    MfcPalette *p=palette_acquire(self); HDC hdc=mfc_dc(dc); COLORREF result;
    if (p && hdc && rect) {
        FillRect(hdc,rect,p->brushes[active ? DLUMA_EDITOR_THEME_ROLE_SELECTION : DLUMA_EDITOR_THEME_ROLE_PANEL]);
        result=mfc_color(&p->theme,active ? DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT);
    } else result=manager.mini_caption(self,dc,rect,frame,active);
    callback_end(p); return result;
}
static void __fastcall themed_mini_border(void *self, void *dc, void *frame, const RECT *rect, const RECT *widths)
{
    MfcPalette *p=palette_acquire(self); HDC hdc=mfc_dc(dc);
    if (p && hdc && rect && widths && widths->left>=0 && widths->right>=0 && widths->top>=0 && widths->bottom>=0 &&
        widths->left<=rect->right-rect->left && widths->right<=rect->right-rect->left &&
        widths->top<=rect->bottom-rect->top && widths->bottom<=rect->bottom-rect->top) {
        RECT edge=*rect;
        edge.right=edge.left+widths->left; FillRect(hdc,&edge,p->brushes[DLUMA_EDITOR_THEME_ROLE_BORDER]);
        edge=*rect; edge.left=edge.right-widths->right; FillRect(hdc,&edge,p->brushes[DLUMA_EDITOR_THEME_ROLE_BORDER]);
        edge=*rect; edge.bottom=edge.top+widths->top; FillRect(hdc,&edge,p->brushes[DLUMA_EDITOR_THEME_ROLE_BORDER]);
        edge=*rect; edge.top=edge.bottom-widths->bottom; FillRect(hdc,&edge,p->brushes[DLUMA_EDITOR_THEME_ROLE_BORDER]);
    } else manager.mini_border(self,dc,frame,rect,widths);
    callback_end(p);
}
static void __fastcall themed_tab(void *self, void *dc, const RECT *rect, int index, int active, void *tabs)
{
    MfcPalette *p=palette_acquire(self); HDC hdc=mfc_dc(dc);
    int ordinary=0;
    if (p && hdc && rect && tabs) {
        typedef int (__fastcall *TabFlagFn)(void *);
        void **table=*(void ***)tabs;
        ordinary=!((TabFlagFn)table[163])(tabs) && !((TabFlagFn)table[171])(tabs) &&
                 !((TabFlagFn)table[164])(tabs) && !((TabFlagFn)table[165])(tabs);
    }
    if (p && hdc && rect && tabs && ordinary) {
        RECT copy=*rect, edge=*rect;
        FillRect(hdc,rect,p->brushes[active ? DLUMA_EDITOR_THEME_ROLE_SURFACE : DLUMA_EDITOR_THEME_ROLE_PANEL]);
        if (active) { edge.top=edge.bottom-dc_dip(hdc,3); if (edge.top<rect->top) edge.top=rect->top; FillRect(hdc,&edge,p->brushes[DLUMA_EDITOR_THEME_ROLE_ACCENT]); }
        manager.tab_content(self,dc,&copy,index,active,tabs,mfc_color(&p->theme,DLUMA_EDITOR_THEME_ROLE_TEXT));
    } else manager.tab(self,dc,rect,index,active,tabs);
    callback_end(p);
}

int dluma_editor_theme_mfc_admit(void)
{
    LONG state = InterlockedCompareExchange(&manager.admission, 0, 0);
    HMODULE mfc; void *permanent, *kind, *read; void **table; unsigned int i;
    static const uintptr_t class_rvas[6] = { MFC_MENU_CLASS_RVA, MFC_POPUP_CLASS_RVA, MFC_STATUS_CLASS_RVA, MFC_TOOLBAR_CLASS_RVA, MFC_COLORBAR_CLASS_RVA, MFC_CAPTIONBAR_CLASS_RVA };
    static const uintptr_t global_color_rvas[6] = {0x3DAA20U,0x3DAA34U,0x3DAA3CU,0x3DAA44U,0x3DAA70U,0x3DAA74U};
    static const unsigned int slots[] = {0,15,20,22,26,35,38,39,41,47,53,54,63,65,66,83,84,109,111,192};
    static const uintptr_t functions[] = {0x29130U,0xC8694EU,0xC8696CU,0xC86978U,0xC86990U,0xC869C6U,0xC869D8U,0xC869DEU,0xC869EAU,0xC86A08U,0xC86A14U,0xC86A1AU,0xC86A4AU,0x29140U,0xC86924U,0xC86A86U,0xC86A8CU,0xC86B1CU,0xC86B28U,0xC86CB4U};
    if (state) return state > 0;
    mfc = GetModuleHandleW(L"mfc100.dll");
    if (!mfc) return 0;
    AcquireSRWLockExclusive(&manager.lock);
    state = manager.admission;
    if (state) { ReleaseSRWLockExclusive(&manager.lock); return state > 0; }
    if (!dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR) ||
        !dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_MFC)) goto failed;
    permanent=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,MFC_PERMANENT_RVA,sizeof(permanent_prologue));
    kind=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,MFC_KIND_RVA,sizeof(kind_prologue));
    read=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,MANAGER_READ_RVA,sizeof(get_instance_read));
    manager.global=(void **)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,MANAGER_GLOBAL_RVA,sizeof(void *));
    manager.manager_vtable=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,MANAGER_VTABLE_RVA-sizeof(void *),(MANAGER_SLOTS+1U)*sizeof(void *));
    manager.manager_class=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,MANAGER_CLASS_RVA,sizeof(void *));
    manager.manager_class_fn=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,MANAGER_CLASS_FN_RVA,1);
    for (i=0; i<6U; ++i) {
        manager.classes[i]=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,class_rvas[i],12);
        manager.global_roles[i]=(COLORREF *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,global_color_rvas[i],sizeof(COLORREF));
    }
    if (!permanent || !kind || !read || !manager.global || !manager.manager_vtable || !manager.manager_class || !manager.manager_class_fn ||
        (void *)GetProcAddress(mfc,(LPCSTR)(ULONG_PTR)4194U)!=permanent || (void *)GetProcAddress(mfc,(LPCSTR)(ULONG_PTR)7071U)!=kind ||
        memcmp(permanent,permanent_prologue,sizeof(permanent_prologue)) || memcmp(kind,kind_prologue,sizeof(kind_prologue)) ||
        memcmp(read,get_instance_read,sizeof(get_instance_read))) goto failed;
    for (i=0; i<6U; ++i) if (!manager.classes[i] || !manager.global_roles[i]) goto failed;
    table=(void **)manager.manager_vtable+1;
    for (i=0; i<sizeof(slots)/sizeof(slots[0]); ++i) {
        void *expected=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,functions[i],1);
        if (!expected || table[slots[i]] != expected) goto failed;
    }
    manager.manager_vtable=table;
    manager.permanent=(PermanentFn)permanent; manager.is_kind=(IsKindFn)kind;
    InterlockedExchange(&manager.admission,1);
    ReleaseSRWLockExclusive(&manager.lock); return 1;
failed:
    InterlockedExchange(&manager.admission,-1);
    ReleaseSRWLockExclusive(&manager.lock); return 0;
}
static int install_manager(HWND window)
{
    void *object; void **table;
    if (!dluma_editor_is_main_window(window,GetCurrentProcessId())) return 0;
    AcquireSRWLockExclusive(&manager.lock);
    if (manager.installed) { int same=manager.owner==window; ReleaseSRWLockExclusive(&manager.lock); return same; }
    if (manager.retired) { ReleaseSRWLockExclusive(&manager.lock); return 0; }
    object=*manager.global;
    if (!readable(object,0x10AE0U,1) || *(void **)object!=manager.manager_vtable ||
        ((ClassFn)manager.manager_class_fn)(object)!=manager.manager_class || !SetPropW(window,manager_property,&manager)) {
        ReleaseSRWLockExclusive(&manager.lock); return 0;
    }
    table=(void **)manager.manager_vtable;
    manager.object=object; manager.original=table; manager.owner=window;
    manager.bar=(BarFn)table[15]; manager.separator=(SeparatorFn)table[20]; manager.caption=(CaptionFn)table[22];
    manager.highlight=(HighlightFn)table[38]; manager.tab=(TabFn)table[63]; manager.tab_content=(TabContentFn)table[65];
    manager.status_text=(StatusTextFn)table[26]; manager.button=(ButtonFn)table[35]; manager.highlighted_text=(ItemTextFn)table[39];
    manager.menu_text=(MenuTextFn)table[41]; manager.toolbar_text=(ToolbarTextFn)table[47]; manager.caption_text=(ItemTextFn)table[53];
    manager.caption_area=(CaptionAreaFn)table[54]; manager.autohide_fill=(AutoFillFn)table[109]; manager.autohide_text=(ItemTextFn)table[111];
    manager.caption_icon=(CaptionIconFn)table[192];
    manager.tab_close=(TabCloseFn)table[66]; manager.mini_caption=(MiniCaptionFn)table[83]; manager.mini_border=(MiniBorderFn)table[84];
    memcpy(manager.shadow,table-1,sizeof(manager.shadow));
    manager.shadow[16]=(void *)themed_bar; manager.shadow[21]=(void *)themed_separator;
    manager.shadow[23]=(void *)themed_caption; manager.shadow[39]=(void *)themed_highlight; manager.shadow[64]=(void *)themed_tab;
    manager.shadow[27]=(void *)themed_status_text; manager.shadow[36]=(void *)themed_button;
    manager.shadow[40]=(void *)themed_highlighted_text; manager.shadow[42]=(void *)themed_menu_text; manager.shadow[48]=(void *)themed_toolbar_text;
    manager.shadow[54]=(void *)themed_caption_text; manager.shadow[55]=(void *)themed_caption_area;
    manager.shadow[110]=(void *)themed_autohide_fill; manager.shadow[112]=(void *)themed_autohide_text;
    manager.shadow[193]=(void *)themed_caption_icon;
    manager.shadow[67]=(void *)themed_tab_close; manager.shadow[84]=(void *)themed_mini_caption; manager.shadow[85]=(void *)themed_mini_border;
    if (InterlockedCompareExchangePointer((PVOID volatile *)object,&manager.shadow[1],table)!=table) {
        RemovePropW(window,manager_property); manager.owner=NULL; manager.object=NULL;
        ReleaseSRWLockExclusive(&manager.lock); return 0;
    }
    InterlockedExchange(&manager.installed,1);
    ReleaseSRWLockExclusive(&manager.lock);
    (void)dluma_editor_ui_request_refresh();
    return 1;
}
static int attach_pane(HWND window)
{
    MfcPane *pane; void *object; unsigned int i;
    if (GetPropW(window,pane_property)) return 1;
    object=manager.permanent(window);
    if (!readable(object,72U,0) || *(HWND *)((BYTE *)object+64U)!=window || manager.is_kind(object,manager.classes[4])) return 0;
    for (i=0; i<4U; ++i) if (manager.is_kind(object,manager.classes[i])) break;
    if (i==4U) {
        if (!manager.is_kind(object,manager.classes[5]) || !readable(object,1004U,0) ||
            *(uint32_t *)((BYTE *)object+1000U)!=UINT32_MAX) return 0;
        i=5U;
    }
    pane=(MfcPane *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*pane));
    if (!pane) return 0;
    pane->window=window; pane->object=object; pane->kind=i+1U;
    pane->identity=InterlockedIncrement64(&manager.next_pane_identity);
    if (!SetPropW(window,pane_property,pane)) { HeapFree(GetProcessHeap(),0,pane); return 0; }
    InterlockedIncrement(&manager.panes); return 1;
}
int dluma_editor_theme_mfc_attach(HWND window)
{
    DWORD pid=0;
    if (!IsWindow(window) || GetWindowThreadProcessId(window,&pid)!=GetCurrentThreadId() || pid!=GetCurrentProcessId() ||
        !dluma_editor_theme_mfc_admit()) return 0;
    if (!manager.installed) (void)install_manager(window);
    if (!manager.installed) return 0;
    (void)attach_pane(window);
    return 1;
}
static void redraw_pane(HWND window, uint64_t requested_generation)
{
    MfcPane *pane, *current;
    uint64_t generation=0, previous_generation;
    LONG64 identity;
    int ready=0, previous_valid;
    DWORD pid=0;
    if (!IsWindow(window) || GetWindowThreadProcessId(window,&pid)!=GetCurrentThreadId() ||
        pid!=GetCurrentProcessId()) return;
    pane=(MfcPane *)GetPropW(window,pane_property);
    if (!pane || pane->window!=window) return;
    AcquireSRWLockShared(&manager.lock);
    if (manager.installed && !manager.retired && manager.global && *manager.global==manager.object &&
        manager.palette && manager.palette->theme.generation>=requested_generation) {
        generation=manager.palette->theme.generation; ready=1;
    }
    ReleaseSRWLockShared(&manager.lock);
    if (!ready || (pane->redraw_valid && pane->redraw_generation==generation)) return;
    identity=pane->identity;
    previous_generation=pane->redraw_generation; previous_valid=pane->redraw_valid;
    pane->redraw_generation=generation; pane->redraw_valid=1;
    if (!RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_FRAME|RDW_NOCHILDREN)) {
        current=(MfcPane *)GetPropW(window,pane_property);
        if (IsWindow(window) && GetWindowThreadProcessId(window,&pid)==GetCurrentThreadId() &&
            pid==GetCurrentProcessId() && current==pane && current->identity==identity &&
            current->redraw_valid && current->redraw_generation==generation) {
            current->redraw_generation=previous_generation; current->redraw_valid=previous_valid;
        }
    }
}
void dluma_editor_theme_mfc_apply(HWND window, const DlumaEditorThemeResolved *theme)
{
    MfcPalette *p, *old; unsigned int i,j;
    if (!theme || theme->size!=sizeof(*theme) || theme->version!=1 ||
        GetWindowThreadProcessId(window,NULL)!=GetCurrentThreadId()) return;
    AcquireSRWLockShared(&manager.lock);
    if (!manager.installed || manager.retired || !manager.global || *manager.global!=manager.object) {
        ReleaseSRWLockShared(&manager.lock); return;
    }
    if (manager.palette && manager.palette->theme.generation>=theme->generation) {
        ReleaseSRWLockShared(&manager.lock); redraw_pane(window,theme->generation); return;
    }
    ReleaseSRWLockShared(&manager.lock);
    p=(MfcPalette *)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*p));
    if (!p) return;
    p->references=1; p->theme=*theme;
    for (i=0; i<17U; ++i) {
        p->brushes[i]=CreateSolidBrush(mfc_color(theme,i));
        if (!p->brushes[i]) { palette_release(p); return; }
        for (j=0; j<3U; ++j) {
            p->pens[i][j]=CreatePen(PS_SOLID,(int)j+1,mfc_color(theme,i));
            if (!p->pens[i][j]) { palette_release(p); return; }
        }
    }
    AcquireSRWLockExclusive(&manager.lock);
    if (!manager.installed || manager.retired || !manager.global || *manager.global!=manager.object ||
        (manager.palette && manager.palette->theme.generation>=theme->generation)) {
        ReleaseSRWLockExclusive(&manager.lock); palette_release(p); redraw_pane(window,theme->generation); return;
    }
    for (i=0; i<=30U; ++i) p->known[p->known_count++]=GetSysColor((int)i);
    for (i=12667U; i<=12677U; ++i) p->known[p->known_count++]=((COLORREF *)manager.object)[i];
    p->known[p->known_count++]=((COLORREF *)manager.object)[12714U];
    for (i=0; i<6U; ++i) p->known[p->known_count++]=*manager.global_roles[i];
    old=manager.palette; manager.palette=p;
    ReleaseSRWLockExclusive(&manager.lock);
    palette_release(old);
    redraw_pane(window,theme->generation);
}
void dluma_editor_theme_mfc_detach(HWND window)
{
    MfcPane *pane=(MfcPane *)GetPropW(window,pane_property); MfcPalette *old=NULL;
    if (GetWindowThreadProcessId(window,NULL)!=GetCurrentThreadId()) return;
    if (pane) { RemovePropW(window,pane_property); HeapFree(GetProcessHeap(),0,pane); InterlockedDecrement(&manager.panes); }
    if (GetPropW(window,manager_property)!=&manager) return;
    AcquireSRWLockExclusive(&manager.lock);
    old=manager.palette; manager.palette=NULL;
    manager.retired=1;
    if (manager.installed && *manager.global==manager.object && readable(manager.object,sizeof(void *),1) &&
        InterlockedCompareExchangePointer((PVOID volatile *)manager.object,manager.original,&manager.shadow[1])==&manager.shadow[1]) {
        InterlockedExchange(&manager.installed,0); manager.owner=NULL; RemovePropW(window,manager_property);
    }
    ReleaseSRWLockExclusive(&manager.lock);
    palette_release(old);
}
int dluma_editor_theme_mfc_message(HWND window, unsigned int stage, UINT message)
{
    if (stage==DLUMA_EDITOR_UI_BEFORE && message==WM_NCDESTROY) dluma_editor_theme_mfc_detach(window);
    else if (stage==DLUMA_EDITOR_UI_AFTER && (message==WM_SHOWWINDOW || message==WM_ACTIVATE) &&
             !InterlockedCompareExchange(&manager.installed,0,0)) (void)dluma_editor_theme_mfc_attach(window);
    return 0;
}
int dluma_editor_theme_mfc_callbacks_drained(void)
{
    return !InterlockedCompareExchange(&manager.installed,0,0) &&
        !InterlockedCompareExchange(&manager.callbacks,0,0) && !InterlockedCompareExchange(&manager.panes,0,0);
}
