#include "dlw_boss_receiver_route.h"
#include <string.h>
typedef struct {
    DWORD architecture, page_size;
    void *minimum, *maximum; ULONG_PTR affinity;
    DWORD processors, processor_type, granularity;
    WORD level, revision;
} DlumaBossRouteSystemInfo;
__declspec(dllimport) void WINAPI GetSystemInfo(DlumaBossRouteSystemInfo *);
typedef struct { ULONG_PTR base; DWORD original; volatile LONG pending; } DlumaBossRoutePage;

static struct {
    DlumaBossReceiverBinding binding;
    DlumaBossReceiverCallbacks callbacks;
    volatile LONG prepared, ready, stopping, active, custom_gate;
    volatile LONG preparing, mutating, stop_requested;
    DlumaBossRoutePage pages[3];
    unsigned int cell_pages[3], page_count;
    DWORD page_size;
} g_dluma_boss_route;

static void __fastcall dluma_boss_route_update(void *);
static void __fastcall dluma_boss_route_destroy(void *);
static void __fastcall dluma_boss_route_cleanup(void *);
static const DlumaBossReceiverMethod g_dluma_boss_route_hooks[3] = {
    dluma_boss_route_update, dluma_boss_route_destroy, dluma_boss_route_cleanup
};

static int dluma_boss_route_swap(unsigned int index, void *expected, void *replacement)
{
    void **cell = g_dluma_boss_route.binding.cells[index];
    DlumaBossRoutePage *page = &g_dluma_boss_route.pages[g_dluma_boss_route.cell_pages[index]];
    MEMORY_BASIC_INFORMATION memory;
    DWORD ignored;
    void *observed;
    int changed, protected_again;
    /* Never wait on the engine callback thread. A conflicting stop retains the
     * module and retries later; no other mutation may restore our shared page
     * between making it writable and the pointer CAS. */
    if (dluma_atomic_compare_exchange(&g_dluma_boss_route.mutating, 1, 0)) return 0;
    __try {
    if (!VirtualProtect(cell, sizeof(void *), PAGE_READWRITE, &ignored)) return 0;
    dluma_atomic_exchange(&page->pending, 1);
    observed = dluma_atomic_pointer_compare_exchange((PVOID volatile *)cell, replacement, expected);
    changed = observed == expected || observed == replacement;
    protected_again = VirtualProtect((void *)page->base, g_dluma_boss_route.page_size, page->original, &ignored) &&
        VirtualQuery((void *)page->base, &memory, sizeof(memory)) && memory.Protect == page->original;
    if (protected_again) dluma_atomic_exchange(&page->pending, 0);
    return changed && protected_again;
    } __finally { dluma_atomic_exchange(&g_dluma_boss_route.mutating, 0); }
}

static int dluma_boss_route_owner(void *owner)
{
    int valid = 0;
    __try { valid = owner && dluma_is_readable(owner, sizeof(void *)) &&
        *(const void **)owner == g_dluma_boss_route.binding.owner_vtable; }
    __except (1) { valid = 0; }
    return valid;
}

static void __fastcall dluma_boss_route_update(void *owner)
{
    int entered = 0, borrowed = 0;
    dluma_atomic_increment(&g_dluma_boss_route.active);
    __try {
        if (dluma_boss_receiver_route_ready() &&
            !dluma_atomic_compare_exchange(&g_dluma_boss_route.custom_gate, 1, 0)) {
            entered = 1;
            __try {
                if (dluma_boss_route_owner(owner)) {
                    borrowed = g_dluma_boss_route.callbacks.begin(owner);
                    if (borrowed) g_dluma_boss_route.callbacks.update(owner);
                }
            } __except (1) {
                /* The original engine callback still owns its receiver. */
                __try { g_dluma_boss_route.callbacks.retire(owner); }
                __except (1) { }
            }
        }
        g_dluma_boss_route.binding.originals[0](owner);
    } __finally {
        if (borrowed) {
            __try { g_dluma_boss_route.callbacks.end(owner); }
            __except (1) { }
        }
        if (entered) dluma_atomic_exchange(&g_dluma_boss_route.custom_gate, 0);
        dluma_atomic_decrement(&g_dluma_boss_route.active);
    }
}

static void dluma_boss_route_retire(unsigned int index, void *owner)
{
    dluma_atomic_increment(&g_dluma_boss_route.active);
    __try {
        if (!dluma_atomic_compare_exchange(&g_dluma_boss_route.stopping, 0, 0) &&
            !dluma_atomic_compare_exchange(&g_dluma_boss_route.stop_requested, 0, 0)) {
            __try { g_dluma_boss_route.callbacks.retire(owner); }
            __except (1) { }
        }
        g_dluma_boss_route.binding.originals[index](owner);
    } __finally { dluma_atomic_decrement(&g_dluma_boss_route.active); }
}
static void __fastcall dluma_boss_route_destroy(void *o)
{ dluma_boss_route_retire(1, o); }
static void __fastcall dluma_boss_route_cleanup(void *o)
{ dluma_boss_route_retire(2, o); }

static int dluma_boss_route_prepare(const DlumaBossReceiverBinding *binding, const DlumaBossReceiverCallbacks *callbacks)
{
    HMODULE pinned;
    unsigned int i, j;
    DlumaBossRouteSystemInfo info;
    MEMORY_BASIC_INFORMATION memory;
    if (!binding || !callbacks || !callbacks->begin || !callbacks->update || !callbacks->end || !callbacks->retire ||
        !binding->module || !binding->owner_vtable || g_dluma_boss_route.prepared ||
        dluma_atomic_compare_exchange(&g_dluma_boss_route.stop_requested, 0, 0)) return 0;
    GetSystemInfo(&info);
    if (!info.page_size || (info.page_size & (info.page_size - 1U))) return 0;
    g_dluma_boss_route.page_count = 0;
    g_dluma_boss_route.page_size = info.page_size;
    for (i = 0; i < 3; ++i) {
        ULONG_PTR page_base;
        if (!binding->cells[i] || ((ULONG_PTR)binding->cells[i] & 7U) || !binding->originals[i] ||
            !dluma_is_readable(binding->cells[i], sizeof(void *)) ||
            *binding->cells[i] != (void *)binding->originals[i]) return 0;
        page_base = (ULONG_PTR)binding->cells[i] & ~((ULONG_PTR)info.page_size - 1U);
        if (!VirtualQuery((void *)page_base, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
        for (j = 0; j < g_dluma_boss_route.page_count; ++j)
            if (g_dluma_boss_route.pages[j].base == page_base) break;
        if (j == g_dluma_boss_route.page_count) {
            DlumaBossRoutePage *page = &g_dluma_boss_route.pages[g_dluma_boss_route.page_count++];
            page->base = page_base; page->original = memory.Protect; page->pending = 0;
        }
        g_dluma_boss_route.cell_pages[i] = j;
    }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        (LPCWSTR)(ULONG_PTR)dluma_boss_route_update, &pinned)) return 0;
    for (i = 0; i < 3; ++i)
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            (LPCWSTR)(ULONG_PTR)binding->originals[i], &pinned) || pinned != binding->module) return 0;
    g_dluma_boss_route.binding = *binding;
    g_dluma_boss_route.callbacks = *callbacks;
    dluma_atomic_exchange(&g_dluma_boss_route.stopping, 1);
    dluma_atomic_exchange(&g_dluma_boss_route.prepared, 1);
    for (i = 0; i < 3; ++i)
        if (!dluma_boss_route_swap(i, (void *)binding->originals[i], (void *)g_dluma_boss_route_hooks[i])) {
            (void)dluma_boss_receiver_route_stop(); return 0;
        }
    if (dluma_atomic_compare_exchange(&g_dluma_boss_route.stop_requested, 0, 0)) {
        (void)dluma_boss_receiver_route_stop(); return 0;
    }
    dluma_atomic_exchange(&g_dluma_boss_route.ready, 1);
    dluma_atomic_exchange(&g_dluma_boss_route.stopping, 0);
    return dluma_boss_receiver_route_ready();
}

int dluma_boss_receiver_route_start(const DlumaBossReceiverBinding *binding, const DlumaBossReceiverCallbacks *callbacks)
{
    int result;
    if (dluma_atomic_compare_exchange(&g_dluma_boss_route.preparing, 1, 0)) return 0;
    __try { result = dluma_boss_route_prepare(binding, callbacks); }
    __finally { dluma_atomic_exchange(&g_dluma_boss_route.preparing, 0); }
    return result;
}

int dluma_boss_receiver_route_ready(void)
{
    unsigned int i;
    int valid = 1;
    if (!dluma_atomic_compare_exchange(&g_dluma_boss_route.ready, 0, 0) ||
        dluma_atomic_compare_exchange(&g_dluma_boss_route.stopping, 0, 0) ||
        dluma_atomic_compare_exchange(&g_dluma_boss_route.stop_requested, 0, 0)) return 0;
    __try {
        for (i = 0; i < 3; ++i)
            if (!dluma_is_readable(g_dluma_boss_route.binding.cells[i], sizeof(void *)) ||
                *g_dluma_boss_route.binding.cells[i] != (void *)g_dluma_boss_route_hooks[i] ||
                dluma_atomic_compare_exchange(&g_dluma_boss_route.pages[g_dluma_boss_route.cell_pages[i]].pending, 0, 0)) valid = 0;
    } __except (1) { valid = 0; }
    if (!valid) dluma_atomic_exchange(&g_dluma_boss_route.stopping, 1);
    return valid;
}

int dluma_boss_receiver_route_stop(void)
{
    int restored = 1;
    unsigned int i;
    dluma_atomic_exchange(&g_dluma_boss_route.stop_requested, 1);
    dluma_atomic_exchange(&g_dluma_boss_route.stopping, 1);
    dluma_atomic_exchange(&g_dluma_boss_route.ready, 0);
    if (!g_dluma_boss_route.prepared) return !dluma_atomic_compare_exchange(&g_dluma_boss_route.preparing, 0, 0);
    for (i = 0; i < 3; ++i)
        if (!dluma_boss_route_swap(i, (void *)g_dluma_boss_route_hooks[i],
            (void *)g_dluma_boss_route.binding.originals[i])) restored = 0;
    return restored && !dluma_atomic_compare_exchange(&g_dluma_boss_route.active, 0, 0) &&
        !dluma_atomic_compare_exchange(&g_dluma_boss_route.preparing, 0, 0);
}
