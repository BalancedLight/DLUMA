#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>
#include "dlw_editor_theme_grid.h"
#include "dlw_editor_theme_profile.h"
#include "../build/generated/editor-theme/editor_theme_palette.generated.h"

/* Exact Editor and MFC10 profiles; all offsets are module-relative or into the
 * admitted instances. The style object remains owned by the Editor. */
#define GRID3_VTABLE_RVA 0x15ADFF8U
#define GRID1_VTABLE_RVA 0x15B2548U
#define GRID1_CLASS_RVA 0x15B2198U
#define GRID1_GET_CLASS_RVA 0x4B38E0U
#define GRID1_RESET_RVA 0x4B4490U
#define GRID1_VIRTUAL_COUNT 132U
#define GRID1_RESET_SLOT 103U
#define GRID1_CELL_SLOT 111U
#define GRID1_CELL_RVA 0x4B4A30U
#define GRID3_CLASS_RVA 0x15AD9E0U
#define GRID3_GET_CLASS_RVA 0x1D1D20U
#define GRID3_TYPE_RVA 0x1B0CF60U
#define STYLE_VTABLE_RVA 0x15B75D0U
#define STYLE_RESET_RVA 0x4CC6C0U
#define MFC_PERMANENT_RVA 0x26B460U
#define MFC_IS_KIND_OF_RVA 0x297190U
#define EDITOR_FREE_IAT_RVA 0x114C1A8U
#define EDITOR_CWND_DTOR_IAT_RVA 0x114BAC8U
#define MFC_FREE_RVA 0x16C4U
#define MFC_CWND_DTOR_RVA 0x26BEACU
#define GRID_STYLE_OFFSET 1848U
#define GRID_OWNS_STYLE_OFFSET 1856U
#define QUEST_GRID_VTABLE_RVA 0xC3B28U
#define QUEST_GRID_CLASS_RVA 0xC3510U
#define QUEST_GRID_GET_CLASS_RVA 0x489A0U
#define QUEST_STYLE_VTABLE_RVA 0xC4E58U
#define QUEST_STYLE_RESET_RVA 0x523A0U
#define QUEST_DELETE_IAT_RVA 0xB2D90U

typedef void *(__fastcall *GridResetFn)(void *);
typedef void *(__fastcall *GridClassFn)(void *);
typedef void *(__cdecl *PermanentFn)(HWND);
typedef int (__fastcall *IsKindOfFn)(void *, const void *);
typedef void (__fastcall *GridCellFn)(void *, void *, int, void *, uint32_t,
    uint32_t, COLORREF, uint32_t, uint32_t, unsigned char, uint32_t, uint32_t, void *);
typedef struct GridState GridState;
typedef struct Grid1State Grid1State;
typedef struct GridShadow {
    void *rtti;
    GridResetFn reset;
    GridState *owner;
} GridShadow;
struct GridState {
    HWND window;
    DWORD thread_id;
    BYTE *object, *style;
    void **object_vtable;
    void *runtime_class;
    void **original_vtable;
    GridResetFn original_reset;
    GridShadow shadow;
    DlumaEditorThemeResolved theme;
    uint32_t original[6], applied[6];
    volatile LONG references, retired, active;
    int focus_queued, quarantined;
};
struct Grid1State {
    HWND window;
    DWORD thread_id;
    BYTE *object;
    void **original_vtable;
    void *shadow[GRID1_VIRTUAL_COUNT + 2U];
    void (__fastcall *original_reset)(void *);
    GridCellFn original_cell;
    DlumaEditorThemeResolved theme;
    uint32_t original[9], applied[9];
    volatile LONG references, retired, active;
    int focus_queued, quarantined;
};
typedef struct GridWatchSlot { PVOID volatile key; void *state; } GridWatchSlot;
typedef struct GridIatSlot {
    PVOID volatile *slot;
    void *original, *replacement;
    DWORD protection;
    volatile LONG installed, protection_pending;
} GridIatSlot;
#define GRID_WATCH_BUCKETS 1024U
#define GRID_WATCH_WAYS 4U
static GridWatchSlot style_watches[GRID_WATCH_BUCKETS][GRID_WATCH_WAYS];
static GridWatchSlot object_watches[GRID_WATCH_BUCKETS][GRID_WATCH_WAYS];
static SRWLOCK watch_lock;
static GridIatSlot free_iat, destructor_iat, quest_delete_iat;
static volatile LONG grid_stopping, matched_callbacks, iat_callbacks;
static volatile LONG watch_full_logged;
static void (__cdecl *original_free)(void *);
static void (__fastcall *original_cwnd_destructor)(void *);
extern void dluma_log_pair(const char *, const char *, const char *);
static const WCHAR grid_property[] = L"DLUMA.EditorTheme.Grid.1";
static const WCHAR grid1_property[] = L"DLUMA.EditorTheme.GridLegacy.1";
static volatile LONG grid_count, admission_state;
static UINT focus_refresh_message;
static volatile LONG grid1_admission;
static volatile LONG quest_admission;
static SRWLOCK admission_lock;
static PermanentFn permanent_lookup;
static IsKindOfFn is_kind_of;
static void *grid_vtable, *grid_class, *grid_class_fn, *style_vtable, *style_reset;
static void *grid1_vtable, *grid1_class, *grid1_class_fn, *grid1_reset, *grid1_cell;
static void *log_cell_returns[2];
static void *quest_grid_vtable, *quest_grid_class, *quest_grid_class_fn;
static void *quest_style_vtable, *quest_style_reset;
static int grid1_attach(HWND window);
static void grid1_apply(HWND window, const DlumaEditorThemeResolved *theme);
static void grid1_detach(HWND window);
static const BYTE permanent_prologue[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0x46,0x61,0xEB,0xFF,
    0x48,0x8B,0x48,0x28,0x33,0xC0,0x48,0x85,0xC9,0x74,0x0C,0x48,0x83,0xC1,
    0x28,0x48,0x8B,0xD3,0xE8,0xD3,0x9F,0xFA,0xFF,0x48,0x83,0xC4,0x20,0x5B,0xC3
};
static const BYTE is_kind_of_prologue[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0x48,0x85,0xC9,
    0x75,0x06,0xE8,0xB5,0x27,0xF7,0xFF,0xCC,0x48,0x8B,0x01,0xFF,0x10
};
static const BYTE free_prologue[] = {0x48,0xFF,0x25,0xCD,0xD6,0x2B,0x00};
static const BYTE destructor_prologue[] = {0x48,0x89,0x4C,0x24,0x08,0x53,0x48,0x83,0xEC,0x30,0x48,0xC7,0x44,0x24,0x20,0xFE,0xFF,0xFF,0xFF};
static void __cdecl grid_free_hook(void *pointer);
static void __fastcall grid_cwnd_destructor_hook(void *object);
static int install_iat_hooks(HMODULE mfc);
static int memory_access(const void *address, size_t bytes, int writable);

static unsigned int watch_bucket(const void *key)
{
    return (unsigned int)(((uintptr_t)key >> 4U) & (GRID_WATCH_BUCKETS-1U));
}
static int watch_add(GridWatchSlot slots[GRID_WATCH_BUCKETS][GRID_WATCH_WAYS], void *key, void *state)
{
    unsigned int index=watch_bucket(key), i; GridWatchSlot *empty=NULL;
    AcquireSRWLockExclusive(&watch_lock);
    if (InterlockedCompareExchange(&grid_stopping,0,0)) { ReleaseSRWLockExclusive(&watch_lock); return 0; }
    for (i=0; i<GRID_WATCH_WAYS; ++i) {
        GridWatchSlot *slot=&slots[index][i];
        if (slot->key==key) { ReleaseSRWLockExclusive(&watch_lock); return 0; }
        if (!slot->key && !empty) empty=slot;
    }
    if (empty) { empty->state=state; InterlockedExchangePointer(&empty->key,key); }
    ReleaseSRWLockExclusive(&watch_lock);
    if (!empty && InterlockedCompareExchange(&watch_full_logged,1,0)==0)
        dluma_log_pair("WARN","editor_grid_theme","instance_capacity_reached");
    return empty != NULL;
}
static int watch_remove_locked(GridWatchSlot slots[GRID_WATCH_BUCKETS][GRID_WATCH_WAYS], void *key, void *state)
{
    unsigned int index=watch_bucket(key), i; int removed=0;
    for (i=0; i<GRID_WATCH_WAYS; ++i) {
        GridWatchSlot *slot=&slots[index][i];
        if (slot->key==key && slot->state==state) {
            InterlockedExchangePointer(&slot->key,NULL); slot->state=NULL; removed=1; break;
        }
    }
    return removed;
}
static void grid3_release(GridState *s)
{
    if (InterlockedDecrement(&s->references)) return;
    HeapFree(GetProcessHeap(),0,s); InterlockedDecrement(&grid_count);
}
static void grid1_release(Grid1State *s)
{
    if (InterlockedDecrement(&s->references)) return;
    HeapFree(GetProcessHeap(),0,s); InterlockedDecrement(&grid_count);
}
static GridState *take_style(void *key)
{
    unsigned int index=watch_bucket(key), i;
    for (i=0; i<GRID_WATCH_WAYS; ++i) {
        GridWatchSlot *slot=&style_watches[index][i]; GridState *s;
        if (InterlockedCompareExchangePointer(&slot->key,NULL,NULL)!=key) continue;
        AcquireSRWLockExclusive(&watch_lock);
        s=slot->key==key ? (GridState *)slot->state : NULL;
        if (s && !s->retired) {
            s->quarantined=s->thread_id!=GetCurrentThreadId();
            InterlockedIncrement(&s->references); InterlockedExchange(&s->retired,1); InterlockedExchange(&s->active,0);
            InterlockedExchangePointer(&slot->key,NULL); slot->state=NULL;
        } else s=NULL;
        ReleaseSRWLockExclusive(&watch_lock);
        return s;
    }
    return NULL;
}
static Grid1State *take_object(void *key)
{
    unsigned int index=watch_bucket(key), i;
    for (i=0; i<GRID_WATCH_WAYS; ++i) {
        GridWatchSlot *slot=&object_watches[index][i]; Grid1State *s;
        if (InterlockedCompareExchangePointer(&slot->key,NULL,NULL)!=key) continue;
        AcquireSRWLockExclusive(&watch_lock);
        s=slot->key==key ? (Grid1State *)slot->state : NULL;
        if (s && !s->retired) {
            s->quarantined=s->thread_id!=GetCurrentThreadId();
            InterlockedIncrement(&s->references); InterlockedExchange(&s->retired,1); InterlockedExchange(&s->active,0);
            InterlockedExchangePointer(&slot->key,NULL); slot->state=NULL;
        } else s=NULL;
        ReleaseSRWLockExclusive(&watch_lock);
        return s;
    }
    return NULL;
}
static void __cdecl grid_free_hook(void *pointer)
{
    GridState *s; int completed=0, property_released=1, shadow_safe=1;
    InterlockedIncrement(&iat_callbacks);
    s=pointer ? take_style(pointer) : NULL;
    if (s) {
        InterlockedIncrement(&matched_callbacks);
        if (memory_access(pointer,sizeof(void *),1))
            (void)InterlockedCompareExchangePointer((PVOID volatile *)pointer,s->original_vtable,&s->shadow.reset);
        else shadow_safe=0;
        if (s->quarantined) property_released=0;
        else if (GetPropW(s->window,grid_property)==s && RemovePropW(s->window,grid_property)!=s && GetPropW(s->window,grid_property)==s)
            property_released=0;
        if (property_released && shadow_safe) grid3_release(s); /* release the window/registry reference */
    }
    __try { original_free(pointer); completed=1; }
    __finally {
        if (s) {
            if (completed) {
                if (!s->quarantined && !InterlockedCompareExchange(&grid_stopping,0,0)) (void)dluma_editor_ui_request_refresh();
                grid3_release(s);
            }
            InterlockedDecrement(&matched_callbacks);
        }
        InterlockedDecrement(&iat_callbacks);
    }
}
static void __fastcall grid_cwnd_destructor_hook(void *object)
{
    Grid1State *s; int completed=0, property_released=1;
    InterlockedIncrement(&iat_callbacks);
    s=object ? take_object(object) : NULL;
    if (s) {
        InterlockedIncrement(&matched_callbacks);
        if (s->quarantined) property_released=0;
        else if (GetPropW(s->window,grid1_property)==s && RemovePropW(s->window,grid1_property)!=s && GetPropW(s->window,grid1_property)==s)
            property_released=0;
        if (property_released) grid1_release(s);
    }
    __try { original_cwnd_destructor(object); completed=1; }
    __finally {
        if (s) {
            if (completed) grid1_release(s);
            InterlockedDecrement(&matched_callbacks);
        }
        InterlockedDecrement(&iat_callbacks);
    }
}
static int memory_access(const void *address, size_t bytes, int writable)
{
    MEMORY_BASIC_INFORMATION info; DWORD protection;
    uintptr_t p = (uintptr_t)address, end;
    if (!address || !bytes || p > UINTPTR_MAX - bytes ||
        !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
    end = (uintptr_t)info.BaseAddress + info.RegionSize;
    if (p + bytes > end) return 0;
    protection = info.Protect & 0xffU;
    if (!writable) return protection == PAGE_READONLY || protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    return protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}
static int executable_address(const void *address)
{
    MEMORY_BASIC_INFORMATION info; DWORD protection;
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
    protection = info.Protect & 0xffU;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}
static void *editor_rva(uint32_t rva, size_t size)
{
    return dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, rva, size);
}
static int editor_pointer(const void *pointer, size_t size)
{
    BYTE *base = (BYTE *)editor_rva(0, 1);
    uintptr_t value = (uintptr_t)pointer, start = (uintptr_t)base;
    return base && value >= start && value - start <= UINT32_MAX &&
        editor_rva((uint32_t)(value - start), size) == pointer;
}
/* MSVC x64 RTTI uses image-relative 32-bit COL/CHD/BCD records. Reject
 * secondary/virtual/ambiguous base paths before reading base object fields. */
static int rtti_has_primary_grid3(void **table)
{
    BYTE *base = (BYTE *)editor_rva(0, 1);
    uint32_t *col, *hierarchy, *array;
    uint32_t count, index, matches = 0;
    uintptr_t locator;
    if (!base || !editor_pointer(table - 1, 2U * sizeof(void *))) return 0;
    locator = (uintptr_t)table[-1];
    if (!editor_pointer((const void *)locator, 24U)) return 0;
    col = (uint32_t *)locator;
    if (col[0] != 1U || col[1] || col[2] ||
        (uintptr_t)base + col[5] != locator || !editor_rva(col[3], 17U)) return 0;
    hierarchy = (uint32_t *)editor_rva(col[4], 16U);
    if (!hierarchy || hierarchy[0] != 0U || (hierarchy[1] & 4U)) return 0;
    count = hierarchy[2];
    if (!count || count > 64U) return 0;
    array = (uint32_t *)editor_rva(hierarchy[3], (size_t)count * sizeof(uint32_t));
    if (!array) return 0;
    for (index = 0; index < count; ++index) {
        uint32_t *descriptor = (uint32_t *)editor_rva(array[index], 28U);
        if (!descriptor) return 0;
        if (descriptor[0] == GRID3_TYPE_RVA) {
            if (descriptor[2] != 0U || descriptor[3] != UINT32_MAX ||
                (descriptor[5] & 3U)) return 0;
            ++matches;
        }
    }
    return matches == 1U;
}
static void *grid3_runtime_class(BYTE *object, void **table)
{
    void *runtime_class;
    uint32_t object_size;
    if (!rtti_has_primary_grid3(table) || !editor_pointer(table[0], 1) || !executable_address(table[0])) return NULL;
    runtime_class = ((GridClassFn)table[0])(object);
    if (!editor_pointer(runtime_class, 12U)) return NULL;
    object_size = *(uint32_t *)((BYTE *)runtime_class + 8U);
    if (object_size < GRID_OWNS_STYLE_OFFSET + 1U || object_size > 16U * 1024U * 1024U ||
        !is_kind_of(object, grid_class)) return NULL;
    return runtime_class;
}
static COLORREF grid_color(const DlumaEditorThemeResolved *theme, unsigned int role)
{
    uint32_t c = theme->argb[role];
    return RGB((c >> 16) & 255U, (c >> 8) & 255U, c & 255U);
}
static void set_colors(GridState *s)
{
    uint32_t *colors = (uint32_t *)(void *)(s->style + 8U);
    int active = GetFocus() == s->window || IsChild(s->window, GetFocus());
    int enabled = IsWindowEnabled(s->window);
    unsigned int selection = active ? DLUMA_EDITOR_THEME_ROLE_SELECTION : DLUMA_EDITOR_THEME_ROLE_INACTIVE_SELECTION;
    unsigned int selection_text = active || s->theme.kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST ?
        DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT;
    s->applied[0] = grid_color(&s->theme, DLUMA_EDITOR_THEME_ROLE_SURFACE);
    s->applied[1] = grid_color(&s->theme, DLUMA_EDITOR_THEME_ROLE_ALTERNATE);
    s->applied[2] = grid_color(&s->theme, enabled ? DLUMA_EDITOR_THEME_ROLE_TEXT : DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT);
    s->applied[3] = grid_color(&s->theme, enabled ? selection_text : DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT);
    s->applied[4] = grid_color(&s->theme, DLUMA_EDITOR_THEME_ROLE_BORDER);
    s->applied[5] = s->theme.argb[selection]; /* GDI+ uses ARGB. */
    colors[0] = s->applied[0]; colors[1] = s->applied[1]; colors[2] = s->applied[2];
    colors[3] = s->applied[3]; colors[4] = s->applied[4]; colors[5] = s->applied[5];
}
static void *__fastcall style_reset_adapter(void *style)
{
    void **table = *(void ***)style;
    GridShadow *shadow = (GridShadow *)((BYTE *)table - offsetof(GridShadow, reset));
    GridState *s = shadow->owner;
    void *result = NULL;
    InterlockedIncrement(&s->references);
    __try {
        result = s->original_reset(style);
        AcquireSRWLockShared(&watch_lock);
        __try {
            if (!InterlockedCompareExchange(&s->retired,0,0) && InterlockedCompareExchange(&s->active,0,0))
                memcpy((BYTE *)style + 8U, s->applied, sizeof(s->applied));
        } __finally { ReleaseSRWLockShared(&watch_lock); }
    } __finally { grid3_release(s); }
    return result;
}
static int install_slot(GridIatSlot *route)
{
    DWORD previous, ignored;
    void *observed;
    if (!route->slot || ((uintptr_t)route->slot & (sizeof(void *)-1U)) ||
        !VirtualProtect((void *)route->slot,sizeof(void *),PAGE_READWRITE,&previous)) return 0;
    route->protection=previous; InterlockedExchange(&route->protection_pending,1);
    observed=InterlockedCompareExchangePointer(route->slot,route->replacement,route->original);
    if (observed==route->original) InterlockedExchange(&route->installed,1);
    if (VirtualProtect((void *)route->slot,sizeof(void *),route->protection,&ignored)) InterlockedExchange(&route->protection_pending,0);
    return InterlockedCompareExchange(&route->installed,0,0) && !InterlockedCompareExchange(&route->protection_pending,0,0);
}
static int restore_slot(GridIatSlot *route)
{
    DWORD previous, ignored; void *observed;
    if (!route->slot) return 1;
    if (InterlockedCompareExchange(&route->installed,0,0)) {
        if (!VirtualProtect((void *)route->slot,sizeof(void *),PAGE_READWRITE,&previous)) return 0;
        if (!InterlockedCompareExchange(&route->protection_pending,0,0)) route->protection=previous;
        InterlockedExchange(&route->protection_pending,1);
        observed=InterlockedCompareExchangePointer(route->slot,route->original,route->replacement);
        if (observed==route->replacement) InterlockedExchange(&route->installed,0);
        if (VirtualProtect((void *)route->slot,sizeof(void *),route->protection,&ignored)) InterlockedExchange(&route->protection_pending,0);
        if (observed!=route->replacement) return 0;
    } else if (InterlockedCompareExchange(&route->protection_pending,0,0)) {
        if (!VirtualProtect((void *)route->slot,sizeof(void *),route->protection,&ignored)) return 0;
        InterlockedExchange(&route->protection_pending,0);
    }
    return !InterlockedCompareExchange(&route->installed,0,0) && !InterlockedCompareExchange(&route->protection_pending,0,0);
}
static int install_iat_hooks(HMODULE mfc)
{
    void *free_target, *destructor_target;
    free_target=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,MFC_FREE_RVA,sizeof(free_prologue));
    destructor_target=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC,MFC_CWND_DTOR_RVA,sizeof(destructor_prologue));
    free_iat.slot=(PVOID volatile *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,EDITOR_FREE_IAT_RVA,sizeof(void *));
    destructor_iat.slot=(PVOID volatile *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR,EDITOR_CWND_DTOR_IAT_RVA,sizeof(void *));
    if (!free_target || !destructor_target || !free_iat.slot || !destructor_iat.slot ||
        (void *)GetProcAddress(mfc,(LPCSTR)(ULONG_PTR)1274U)!=free_target ||
        (void *)GetProcAddress(mfc,(LPCSTR)(ULONG_PTR)1270U)!=destructor_target ||
        memcmp(free_target,free_prologue,sizeof(free_prologue)) ||
        memcmp(destructor_target,destructor_prologue,sizeof(destructor_prologue))) return 0;
    original_free=(void (__cdecl *)(void *))free_target;
    original_cwnd_destructor=(void (__fastcall *)(void *))destructor_target;
    free_iat.original=free_target; free_iat.replacement=(void *)grid_free_hook;
    destructor_iat.original=destructor_target; destructor_iat.replacement=(void *)grid_cwnd_destructor_hook;
    if (!install_slot(&free_iat)) return 0;
    if (!install_slot(&destructor_iat)) { (void)restore_slot(&free_iat); return 0; }
    return 1;
}
int dluma_editor_theme_grid_admit(void)
{
    LONG state = InterlockedCompareExchange(&admission_state, 0, 0);
    HMODULE mfc;
    void *permanent, *kind_of;
    if (InterlockedCompareExchange(&grid_stopping,0,0)) return 0;
    if (state) return state > 0;
    mfc = GetModuleHandleW(L"mfc100.dll");
    if (!mfc) return 0;
    AcquireSRWLockExclusive(&admission_lock);
    state = InterlockedCompareExchange(&admission_state, 0, 0);
    if (InterlockedCompareExchange(&grid_stopping,0,0)) { ReleaseSRWLockExclusive(&admission_lock); return 0; }
    if (state) { ReleaseSRWLockExclusive(&admission_lock); return state > 0; }
    if (!dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_EDITOR) ||
        !dluma_editor_theme_profile_admit(DLUMA_EDITOR_THEME_PROFILE_MFC)) goto failed;
    focus_refresh_message = RegisterWindowMessageW(L"DLUMA.EditorTheme.Grid.Focus.1");
    if (!focus_refresh_message) goto failed;
    permanent = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC, MFC_PERMANENT_RVA, sizeof(permanent_prologue));
    kind_of = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_MFC, MFC_IS_KIND_OF_RVA, sizeof(is_kind_of_prologue));
    grid_vtable = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, GRID3_VTABLE_RVA, sizeof(void *));
    grid_class = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, GRID3_CLASS_RVA, sizeof(void *));
    grid_class_fn = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, GRID3_GET_CLASS_RVA, 1);
    style_vtable = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, STYLE_VTABLE_RVA - sizeof(void *), 2U * sizeof(void *));
    style_reset = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, STYLE_RESET_RVA, 1);
    if (!permanent || !kind_of || !grid_vtable || !grid_class || !grid_class_fn || !style_vtable || !style_reset ||
        (void *)GetProcAddress(mfc, (LPCSTR)(ULONG_PTR)4194U) != permanent ||
        (void *)GetProcAddress(mfc, (LPCSTR)(ULONG_PTR)7071U) != kind_of ||
        memcmp(permanent, permanent_prologue, sizeof(permanent_prologue)) != 0 ||
        memcmp(kind_of, is_kind_of_prologue, sizeof(is_kind_of_prologue)) != 0 ||
        *(void **)grid_vtable != grid_class_fn || *((void **)style_vtable + 1) != style_reset) goto failed;
    style_vtable = (BYTE *)style_vtable + sizeof(void *);
    permanent_lookup = (PermanentFn)permanent;
    is_kind_of = (IsKindOfFn)kind_of;
    if (!install_iat_hooks(mfc)) goto failed;
    InterlockedExchange(&admission_state, 1);
    ReleaseSRWLockExclusive(&admission_lock);
    return 1;
failed:
    InterlockedExchange(&admission_state, -1);
    dluma_log_pair("WARN", "editor_grid_theme", "profile_or_lifetime_admission_failed");
    ReleaseSRWLockExclusive(&admission_lock);
    return 0;
}
static int valid_object(GridState *s)
{
    BYTE *object;
    if (!s || GetCurrentThreadId() != s->thread_id || !IsWindow(s->window)) return 0;
    object = (BYTE *)permanent_lookup(s->window);
    return object == s->object && memory_access(object, GRID_OWNS_STYLE_OFFSET + 1U, 1) &&
        *(void ***)object == s->object_vtable && *(HWND *)(void *)(object + 64U) == s->window;
}
static int quest_grid_admit(void)
{
    LONG state=InterlockedCompareExchange(&quest_admission,0,0);
    if (state) return state>0;
    if (!GetModuleHandleW(L"QuestEditor.dll") || !dluma_editor_theme_grid_admit()) return 0;
    AcquireSRWLockExclusive(&admission_lock);
    state=InterlockedCompareExchange(&quest_admission,0,0);
    if (state || InterlockedCompareExchange(&grid_stopping,0,0)) {
        ReleaseSRWLockExclusive(&admission_lock); return state>0;
    }
    quest_grid_vtable=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_QUEST,QUEST_GRID_VTABLE_RVA,sizeof(void *));
    quest_grid_class=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_QUEST,QUEST_GRID_CLASS_RVA,24U);
    quest_grid_class_fn=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_QUEST,QUEST_GRID_GET_CLASS_RVA,1);
    quest_style_vtable=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_QUEST,QUEST_STYLE_VTABLE_RVA-sizeof(void *),2U*sizeof(void *));
    quest_style_reset=dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_QUEST,QUEST_STYLE_RESET_RVA,1);
    quest_delete_iat.slot=(PVOID volatile *)dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_QUEST,QUEST_DELETE_IAT_RVA,sizeof(void *));
    if (!quest_grid_vtable || !quest_grid_class || !quest_grid_class_fn ||
        !quest_style_vtable || !quest_style_reset || !quest_delete_iat.slot ||
        *(void **)quest_grid_vtable!=quest_grid_class_fn ||
        *((void **)quest_style_vtable+1)!=quest_style_reset ||
        *(uint32_t *)((BYTE *)quest_grid_class+8U)<GRID_OWNS_STYLE_OFFSET+1U) goto failed;
    quest_style_vtable=(BYTE *)quest_style_vtable+sizeof(void *);
    quest_delete_iat.original=(void *)original_free;
    quest_delete_iat.replacement=(void *)grid_free_hook;
    if (!install_slot(&quest_delete_iat)) goto failed;
    InterlockedExchange(&quest_admission,1);
    ReleaseSRWLockExclusive(&admission_lock); return 1;
failed:
    InterlockedExchange(&quest_admission,-1);
    ReleaseSRWLockExclusive(&admission_lock); return 0;
}
static int grid1_admit(void)
{
    LONG state = InterlockedCompareExchange(&grid1_admission, 0, 0);
    void **table; void *next_rtti;
    if (state) return state > 0;
    if (!dluma_editor_theme_grid_admit()) return 0;
    AcquireSRWLockExclusive(&admission_lock);
    state = InterlockedCompareExchange(&grid1_admission, 0, 0);
    if (state) { ReleaseSRWLockExclusive(&admission_lock); return state > 0; }
    grid1_vtable = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, GRID1_VTABLE_RVA - sizeof(void *),
        (GRID1_VIRTUAL_COUNT + 2U) * sizeof(void *));
    grid1_class = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, GRID1_CLASS_RVA, sizeof(void *));
    grid1_class_fn = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, GRID1_GET_CLASS_RVA, 1);
    grid1_reset = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, GRID1_RESET_RVA, 1);
    grid1_cell = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, GRID1_CELL_RVA, 1);
    log_cell_returns[0] = editor_rva(0x2DC265U,1);
    log_cell_returns[1] = editor_rva(0x2DC3EEU,1);
    next_rtti = dluma_editor_theme_profile_address(DLUMA_EDITOR_THEME_PROFILE_EDITOR, 0x166EFB0U, sizeof(void *));
    if (!grid1_vtable || !grid1_class || !grid1_class_fn || !grid1_reset || !grid1_cell ||
        !log_cell_returns[0] || !log_cell_returns[1] || !next_rtti) goto fail;
    table = (void **)grid1_vtable + 1;
    if (table[0] != grid1_class_fn || table[GRID1_RESET_SLOT] != grid1_reset ||
        table[GRID1_CELL_SLOT] != grid1_cell || table[GRID1_VIRTUAL_COUNT] != next_rtti) goto fail;
    grid1_vtable = table;
    InterlockedExchange(&grid1_admission, 1);
    ReleaseSRWLockExclusive(&admission_lock);
    return 1;
fail:
    InterlockedExchange(&grid1_admission, -1);
    ReleaseSRWLockExclusive(&admission_lock);
    return 0;
}
static void grid1_set_colors(Grid1State *s)
{
    uint32_t *colors = (uint32_t *)(void *)(s->object + 384U);
    HWND focused = GetFocus();
    int active = focused == s->window || (focused && IsChild(s->window, focused));
    int enabled = IsWindowEnabled(s->window);
    unsigned int selection = active ? DLUMA_EDITOR_THEME_ROLE_SELECTION : DLUMA_EDITOR_THEME_ROLE_INACTIVE_SELECTION;
    unsigned int selected_text = active || s->theme.kind == DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST ?
        DLUMA_EDITOR_THEME_ROLE_SELECTION_TEXT : DLUMA_EDITOR_THEME_ROLE_TEXT;
    s->applied[0] = grid_color(&s->theme, enabled ? DLUMA_EDITOR_THEME_ROLE_TEXT : DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT);
    s->applied[1] = grid_color(&s->theme, DLUMA_EDITOR_THEME_ROLE_SURFACE);
    s->applied[2] = grid_color(&s->theme, DLUMA_EDITOR_THEME_ROLE_ALTERNATE);
    s->applied[3] = grid_color(&s->theme, DLUMA_EDITOR_THEME_ROLE_PANEL);
    s->applied[4] = s->applied[5] = s->applied[6] = grid_color(&s->theme, selection);
    s->applied[7] = grid_color(&s->theme, enabled ? selected_text : DLUMA_EDITOR_THEME_ROLE_DISABLED_TEXT);
    s->applied[8] = grid_color(&s->theme, DLUMA_EDITOR_THEME_ROLE_BORDER);
    memcpy(colors, s->applied, sizeof(s->applied));
}
static void __fastcall grid1_cell_adapter(void *object, void *draw, int column,
    void *text, uint32_t format, uint32_t alignment, COLORREF foreground,
    uint32_t background, uint32_t selected, unsigned char flag,
    uint32_t first, uint32_t last, void *font)
{
    void **table=*(void ***)object;
    Grid1State *s=(Grid1State *)table[GRID1_VIRTUAL_COUNT];
    void *caller=_ReturnAddress();
    InterlockedIncrement(&s->references);
    __try {
        if (GetCurrentThreadId()==s->thread_id &&
            !InterlockedCompareExchange(&s->retired,0,0) &&
            InterlockedCompareExchange(&s->active,0,0)) {
            unsigned int role=DLUMA_EDITOR_THEME_ROLE_COUNT;
            if (foreground==RGB(0,0,0)) role=DLUMA_EDITOR_THEME_ROLE_TEXT;
            else if (caller==log_cell_returns[0] || caller==log_cell_returns[1]) {
                if (foreground==RGB(255,0,0)) role=DLUMA_EDITOR_THEME_ROLE_ERROR;
                else if (foreground==RGB(0,159,0)) role=DLUMA_EDITOR_THEME_ROLE_SUCCESS;
                else if (foreground==RGB(147,100,33)) role=DLUMA_EDITOR_THEME_ROLE_WARNING;
            }
            if (role!=DLUMA_EDITOR_THEME_ROLE_COUNT) {
                foreground=grid_color(&s->theme,role);
                if (memory_access(draw,20U,0) && *(uint32_t *)((BYTE *)draw+16U))
                    foreground=s->applied[7];
            }
        }
        s->original_cell(object,draw,column,text,format,alignment,foreground,
            background,selected,flag,first,last,font);
    } __finally { grid1_release(s); }
}
static void __fastcall grid1_reset_adapter(void *object)
{
    void **table = *(void ***)object;
    Grid1State *s = (Grid1State *)table[GRID1_VIRTUAL_COUNT];
    InterlockedIncrement(&s->references);
    __try {
        s->original_reset(object);
        AcquireSRWLockShared(&watch_lock);
        __try {
            if (!InterlockedCompareExchange(&s->retired,0,0) && InterlockedCompareExchange(&s->active,0,0))
                memcpy((BYTE *)object + 384U, s->applied, sizeof(s->applied));
        } __finally { ReleaseSRWLockShared(&watch_lock); }
    } __finally { grid1_release(s); }
}
static int grid1_valid(Grid1State *s)
{
    BYTE *object;
    if (!s || GetCurrentThreadId() != s->thread_id || !IsWindow(s->window)) return 0;
    object = (BYTE *)permanent_lookup(s->window);
    return object == s->object && memory_access(object, 420U, 1) &&
        *(void ***)object == &s->shadow[1] && *(HWND *)(void *)(object + 64U) == s->window &&
        ((GridClassFn)grid1_class_fn)(object) == grid1_class;
}
static int grid1_attach(HWND window)
{
    BYTE *object; Grid1State *s;
    if (GetPropW(window, grid1_property)) return 1;
    if (!grid1_admit()) return 0;
    object = (BYTE *)permanent_lookup(window);
    if (!memory_access(object, 420U, 1) || *(void **)object != grid1_vtable ||
        *(HWND *)(void *)(object + 64U) != window || ((GridClassFn)grid1_class_fn)(object) != grid1_class) return 0;
    s = (Grid1State *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return 0;
    s->references=1;
    s->window = window; s->thread_id = GetCurrentThreadId(); s->object = object;
    s->original_vtable = (void **)grid1_vtable; s->original_reset = (void (__fastcall *)(void *))grid1_reset;
    s->original_cell = (GridCellFn)grid1_cell;
    memcpy(s->original, object + 384U, sizeof(s->original));
    memcpy(s->shadow, s->original_vtable - 1, (GRID1_VIRTUAL_COUNT + 1U) * sizeof(void *));
    s->shadow[GRID1_RESET_SLOT + 1U] = (void *)grid1_reset_adapter;
    s->shadow[GRID1_CELL_SLOT + 1U] = (void *)grid1_cell_adapter;
    s->shadow[GRID1_VIRTUAL_COUNT + 1U] = s;
    if (!SetPropW(window, grid1_property, s)) { HeapFree(GetProcessHeap(), 0, s); return 0; }
    InterlockedIncrement(&grid_count);
    if (!watch_add(object_watches,object,s)) {
        InterlockedExchange(&s->retired,1);
        if (RemovePropW(window,grid1_property)==s || GetPropW(window,grid1_property)!=s) grid1_release(s);
        else s->quarantined=1;
        return 0;
    }
    *(void ***)object = &s->shadow[1];
    return 1;
}
static void grid1_apply(HWND window, const DlumaEditorThemeResolved *theme)
{
    Grid1State *s = (Grid1State *)GetPropW(window, grid1_property);
    if (!s || !theme || theme->size != sizeof(*theme) || theme->version != 1 ||
        InterlockedCompareExchange(&s->retired,0,0) || !grid1_valid(s)) return;
    AcquireSRWLockShared(&watch_lock);
    __try {
        if (!InterlockedCompareExchange(&s->retired,0,0) && *(void ***)s->object==&s->shadow[1]) {
            s->theme = *theme; grid1_set_colors(s); InterlockedExchange(&s->active,1);
        }
    } __finally { ReleaseSRWLockShared(&watch_lock); }
    InvalidateRect(window, NULL, FALSE);
    { HWND header=GetDlgItem(window,3); if (header) InvalidateRect(header,NULL,FALSE); }
}
static void grid1_detach(HWND window)
{
    Grid1State *s = (Grid1State *)GetPropW(window, grid1_property);
    uint32_t *colors; unsigned int i;
    if (s && InterlockedCompareExchange(&s->retired,0,0)) {
        if (!s->quarantined && RemovePropW(window,grid1_property)==s) grid1_release(s);
        return;
    }
    if (!s || !grid1_valid(s)) return;
    AcquireSRWLockExclusive(&watch_lock);
    if (InterlockedCompareExchange(&s->retired,0,0) || !watch_remove_locked(object_watches,s->object,s)) {
        ReleaseSRWLockExclusive(&watch_lock); return;
    }
    InterlockedExchange(&s->retired,1); InterlockedExchange(&s->active,0);
    s->quarantined=1;
    __try {
        *(void ***)s->object = s->original_vtable;
        colors = (uint32_t *)(void *)(s->object + 384U);
        if (s->theme.version == 1)
            for (i = 0; i < 9U; ++i) if (colors[i] == s->applied[i]) colors[i] = s->original[i];
        s->quarantined=0;
    } __finally { ReleaseSRWLockExclusive(&watch_lock); }
    if (RemovePropW(window, grid1_property)==s) grid1_release(s);
    InvalidateRect(window, NULL, FALSE);
}
int dluma_editor_theme_grid_attach(HWND window)
{
    WCHAR name[64]; DWORD pid = 0; BYTE *object, *style; GridState *s; void **table; void *runtime_class;
    void *expected_style, *reset;
    unsigned int i;
    if (!IsWindow(window) || GetWindowThreadProcessId(window, &pid) != GetCurrentThreadId() ||
        pid != GetCurrentProcessId() || GetClassNameW(window, name, 64) <= 0) return 0;
    if (!lstrcmpiW(name, L"CONTAINER_CTRL")) return grid1_attach(window);
    if (lstrcmpiW(name, L"CONTAINER_CTRL3") || !dluma_editor_theme_grid_admit()) return 0;
    if (GetPropW(window, grid_property)) return 1;
    object = (BYTE *)permanent_lookup(window);
    if (!memory_access(object, GRID_OWNS_STYLE_OFFSET + 1U, 1) ||
        *(HWND *)(void *)(object + 64U) != window || !object[GRID_OWNS_STYLE_OFFSET]) return 0;
    table = *(void ***)object;
    runtime_class = grid3_runtime_class(object, table);
    expected_style=style_vtable; reset=style_reset;
    if (!runtime_class) {
        if (!quest_grid_admit() || table!=quest_grid_vtable ||
            ((GridClassFn)quest_grid_class_fn)(object)!=quest_grid_class ||
            !is_kind_of(object,quest_grid_class)) return 0;
        runtime_class=quest_grid_class;
        expected_style=quest_style_vtable; reset=quest_style_reset;
    }
    style = *(BYTE **)(void *)(object + GRID_STYLE_OFFSET);
    if (!memory_access(style, 40U, 1) || *(void **)style != expected_style) return 0;
    s = (GridState *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s) return 0;
    s->references=1;
    s->window = window; s->thread_id = GetCurrentThreadId(); s->object = object; s->style = style;
    s->object_vtable = table; s->runtime_class = runtime_class;
    s->original_vtable = (void **)expected_style; s->original_reset = (GridResetFn)reset;
    s->shadow.rtti = s->original_vtable[-1]; s->shadow.reset = style_reset_adapter; s->shadow.owner = s;
    for (i = 0; i < 6U; ++i) s->original[i] = ((uint32_t *)(void *)(style + 8U))[i];
    if (!SetPropW(window, grid_property, s)) { HeapFree(GetProcessHeap(), 0, s); return 0; }
    InterlockedIncrement(&grid_count);
    if (!watch_add(style_watches,style,s)) {
        InterlockedExchange(&s->retired,1);
        if (RemovePropW(window,grid_property)==s || GetPropW(window,grid_property)!=s) grid3_release(s);
        else s->quarantined=1;
        return 0;
    }
    *(void ***)style = (void **)(void *)&s->shadow.reset;
    return 1;
}
void dluma_editor_theme_grid_apply(HWND window, const DlumaEditorThemeResolved *theme)
{
    GridState *s = (GridState *)GetPropW(window, grid_property);
    if (!s) { grid1_apply(window, theme); return; }
    if (!s || !theme || theme->size != sizeof(*theme) || theme->version != 1 ||
        InterlockedCompareExchange(&s->retired,0,0) || !valid_object(s)) return;
    AcquireSRWLockShared(&watch_lock);
    __try {
        if (!InterlockedCompareExchange(&s->retired,0,0) &&
            *(BYTE **)(void *)(s->object + GRID_STYLE_OFFSET)==s->style &&
            memory_access(s->style,40U,1) &&
            *(void ***)s->style==(void **)(void *)&s->shadow.reset) {
            s->theme = *theme; set_colors(s); InterlockedExchange(&s->active,1);
        }
    } __finally { ReleaseSRWLockShared(&watch_lock); }
    InvalidateRect(window, NULL, FALSE);
    { HWND header=GetDlgItem(window,3); if (header) InvalidateRect(header,NULL,FALSE); }
}
void dluma_editor_theme_grid_detach(HWND window)
{
    GridState *s = (GridState *)GetPropW(window, grid_property);
    uint32_t *colors; unsigned int i;
    if (!s) { grid1_detach(window); return; }
    if (InterlockedCompareExchange(&s->retired,0,0)) {
        if (!s->quarantined && RemovePropW(window,grid_property)==s) grid3_release(s);
        return;
    }
    if (!s || !valid_object(s)) return;
    AcquireSRWLockExclusive(&watch_lock);
    if (InterlockedCompareExchange(&s->retired,0,0) ||
        *(BYTE **)(void *)(s->object + GRID_STYLE_OFFSET)!=s->style ||
        !memory_access(s->style,40U,1) || *(void ***)s->style!=(void **)(void *)&s->shadow.reset ||
        !watch_remove_locked(style_watches,s->style,s)) {
        ReleaseSRWLockExclusive(&watch_lock); return;
    }
    InterlockedExchange(&s->retired,1); InterlockedExchange(&s->active,0);
    s->quarantined=1;
    __try {
        *(void ***)s->style = s->original_vtable;
        colors = (uint32_t *)(void *)(s->style + 8U);
        if (s->theme.version == 1) {
            for (i = 0; i < 6U; ++i)
                if (colors[i] == s->applied[i] || (i == 0U && colors[i] == s->applied[1])) colors[i] = s->original[i];
        }
        s->quarantined=0;
    } __finally { ReleaseSRWLockExclusive(&watch_lock); }
    if (RemovePropW(window, grid_property)==s) grid3_release(s);
    InvalidateRect(window, NULL, FALSE);
}
int dluma_editor_theme_grid_message(HWND window, unsigned int stage, UINT message)
{
    GridState *s;
    Grid1State *legacy;
    HWND target = window;
    unsigned int depth = 0;
    if (stage == DLUMA_EDITOR_UI_AFTER && (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE)) {
        while (target && depth++ < 32U) {
            s = (GridState *)GetPropW(target, grid_property);
            legacy = (Grid1State *)GetPropW(target, grid1_property);
            if (s || legacy) {
                int *queued = s ? &s->focus_queued : &legacy->focus_queued;
                if (!*queued) {
                    *queued = 1;
                    if (!PostMessageW(target, focus_refresh_message, 0, 0)) *queued = 0;
                }
                break;
            }
            target = GetParent(target);
        }
        return 0;
    }
    s = (GridState *)GetPropW(window, grid_property);
    legacy = (Grid1State *)GetPropW(window, grid1_property);
    if (!s && !legacy) return 0;
    if (stage == DLUMA_EDITOR_UI_BEFORE && message == WM_NCDESTROY) dluma_editor_theme_grid_detach(window);
    else if (stage == DLUMA_EDITOR_UI_AFTER && message == focus_refresh_message) {
        if (s) s->focus_queued = 0;
        if (legacy) legacy->focus_queued = 0;
        if (legacy && InterlockedCompareExchange(&legacy->active,0,0)) grid1_apply(window,&legacy->theme);
        if (s && InterlockedCompareExchange(&s->active,0,0)) dluma_editor_theme_grid_apply(window,&s->theme);
        InvalidateRect(window, NULL, FALSE);
    }
    return 0;
}
int dluma_editor_theme_grid_owns_header(HWND window)
{
    HWND parent=GetParent(window);
    GridState *grid;
    Grid1State *legacy;
    WCHAR name[32];
    if (!parent || GetWindowThreadProcessId(window,NULL)!=GetCurrentThreadId() ||
        GetDlgCtrlID(window)!=3 || GetClassNameW(window,name,32)<=0 ||
        lstrcmpiW(name,L"SysHeader32")) return 0;
    grid=(GridState *)GetPropW(parent,grid_property);
    legacy=(Grid1State *)GetPropW(parent,grid1_property);
    return (grid && !grid->retired && grid->active && valid_object(grid)) ||
        (legacy && !legacy->retired && legacy->active && grid1_valid(legacy));
}
int dluma_editor_theme_grid_callbacks_drained(void)
{
    return InterlockedCompareExchange(&grid_count, 0, 0) == 0 &&
        InterlockedCompareExchange(&matched_callbacks,0,0) == 0 &&
        InterlockedCompareExchange(&iat_callbacks,0,0) == 0 &&
        !InterlockedCompareExchange(&free_iat.installed,0,0) && !InterlockedCompareExchange(&free_iat.protection_pending,0,0) &&
        !InterlockedCompareExchange(&destructor_iat.installed,0,0) && !InterlockedCompareExchange(&destructor_iat.protection_pending,0,0) &&
        !InterlockedCompareExchange(&quest_delete_iat.installed,0,0) && !InterlockedCompareExchange(&quest_delete_iat.protection_pending,0,0);
}
int dluma_editor_theme_grid_stop(void)
{
    int first, second, third;
    AcquireSRWLockExclusive(&watch_lock);
    InterlockedExchange(&grid_stopping,1);
    ReleaseSRWLockExclusive(&watch_lock);
    if (InterlockedCompareExchange(&grid_count,0,0) || InterlockedCompareExchange(&matched_callbacks,0,0)) return 0;
    AcquireSRWLockExclusive(&admission_lock);
    if (InterlockedCompareExchange(&grid_count,0,0) || InterlockedCompareExchange(&matched_callbacks,0,0)) {
        ReleaseSRWLockExclusive(&admission_lock); return 0;
    }
    first=restore_slot(&destructor_iat);
    second=restore_slot(&free_iat);
    third=restore_slot(&quest_delete_iat);
    ReleaseSRWLockExclusive(&admission_lock);
    return first && second && third && dluma_editor_theme_grid_callbacks_drained();
}
