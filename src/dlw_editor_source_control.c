#include "dlw_common.h"
#include "dlw_editor_source_control.h"
#include "dlw_editor_theme_profile.h"
#include "dlw_log.h"

#if defined(_MSC_VER)
void* _ReturnAddress(void);
#pragma intrinsic(_ReturnAddress)
#define SOURCE_CONTROL_CALLER() _ReturnAddress()
#else
#define SOURCE_CONTROL_CALLER() __builtin_return_address(0)
#endif

#define SC_EDITOR_GLOBAL 0x01B70238U
#define SC_EDITOR_TABLE 0x01504F98U
#define SC_GETTER_SLOT 0x01505840U
#define SC_GETTER_TARGET 0x00098570U
#define SC_EDITOR_INIT_SLOT 0x01505228U
#define SC_EDITOR_INIT_TARGET 0x000742C0U
#define SC_EDITOR_INIT_RETURN 0x000ACA5AU
#define SC_PROVIDER_TABLE 0x0151FD48U
#define SC_INIT_TARGET 0x000E0940U
#define SC_DELETE_TARGET 0x000E0840U
#define SC_FACTORY_RETURN 0x00075F06U
#define SC_PROVIDER_OFFSET 0x24B0U
#define SC_LEGACY_OFFSET 0x1057U
#define SC_PROVIDER_BYTES 384U

typedef void* (__fastcall *ScDeleteFn)(void*, unsigned char);
typedef BOOL (__fastcall *ScEditorInitFn)(void*, void*, void*);

typedef struct ScOwnedSlot {
    PVOID volatile* address;
    void* original;
    void* replacement;
    DWORD protection;
    unsigned int protection_pending;
} ScOwnedSlot;

/* Dispatch code and this minimal state stay pinned even after notifications
 * stop: a native caller can have fetched a pointer before a counter changes. */
static struct {
    volatile LONG state;
    volatile LONG lock;
    volatile LONG active;
    volatile LONG notifying;
    volatile LONG blocked;
    volatile LONG rejected;
    unsigned int notify_enabled;
    unsigned int stopped;
    DWORD owner_thread;
    void** editor_global;
    void* editor_table;
    void* provider_table;
    void* factory_return;
    void* editor_init_return;
    ScDeleteFn destroy;
    ScOwnedSlot getter;
    ScOwnedSlot initialize;
    ScOwnedSlot editor_initialize;
    DlumaEditorSourceControlCallbacksV1 callbacks;
    DlumaEditorSourceControlSnapshotV1 last;
} sc;

static void sc_lock(void)
{
    while (dluma_atomic_compare_exchange(&sc.lock, 1, 0) != 0) Sleep(0);
}

static void sc_unlock(void) { dluma_atomic_exchange(&sc.lock, 0); }

static int sc_access(const void* pointer, SIZE_T size, int write)
{
    uintptr_t current = (uintptr_t)pointer, end;
    if (!current || size > (SIZE_T)(UINTPTR_MAX - current)) return 0;
    end = current + size;
    while (current < end) {
        MEMORY_BASIC_INFORMATION memory;
        uintptr_t next;
        DWORD protection;
        if (!VirtualQuery((void*)current, &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return 0;
        protection = memory.Protect & 0xFFU;
        if (write && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY)
            return 0;
        next = (uintptr_t)memory.BaseAddress + memory.RegionSize;
        if (next <= current) return 0;
        current = next;
    }
    return 1;
}

static int sc_aligned(const void* pointer)
{
    return pointer && ((uintptr_t)pointer & (sizeof(void*) - 1U)) == 0;
}

static void* sc_read_slot(const ScOwnedSlot* slot)
{
    /* Aligned Win64 pointer loads are atomic. A compare/exchange used merely
     * to read would still issue an RMW against a read-only vtable page. */
    return *slot->address;
}

/* Atomic DATA-pointer transaction, with original page protection retained
 * across failed restoration. A foreign owner's pointer is never replaced. */
static int sc_exchange(ScOwnedSlot* slot, void* expected, void* replacement)
{
    DWORD protection = 0, unused = 0, final_protection;
    void* previous;
    if (!sc_aligned((const void*)slot->address) ||
        !sc_access((const void*)slot->address, sizeof(void*), 0) ||
        sc_read_slot(slot) != expected ||
        !VirtualProtect((void*)slot->address, sizeof(void*), PAGE_READWRITE, &protection))
        return 0;
    final_protection = slot->protection_pending ? slot->protection : protection;
    previous = dluma_atomic_pointer_compare_exchange(slot->address, replacement, expected);
    if (!VirtualProtect((void*)slot->address, sizeof(void*), final_protection, &unused)) {
        slot->protection = final_protection;
        slot->protection_pending = 1U;
        return 0;
    }
    slot->protection_pending = 0;
    return previous == expected;
}

static int sc_restore(ScOwnedSlot* slot)
{
    DWORD unused = 0;
    void* current;
    if (!slot->address) return 1;
    current = sc_read_slot(slot);
    if (current == slot->replacement && !sc_exchange(slot, slot->replacement, slot->original))
        return 0;
    /* A foreign value is left alone, including during failed startup. */
    if (slot->protection_pending) {
        if (!VirtualProtect((void*)slot->address, sizeof(void*), slot->protection, &unused)) return 0;
        slot->protection_pending = 0;
    }
    return sc_read_slot(slot) != slot->replacement;
}

static int sc_owned(void)
{
    return sc.getter.address && sc.initialize.address && sc.editor_initialize.address &&
        sc_read_slot(&sc.getter) == sc.getter.replacement &&
        sc_read_slot(&sc.initialize) == sc.initialize.replacement &&
        sc_read_slot(&sc.editor_initialize) == sc.editor_initialize.replacement;
}

static int sc_editor(void** editor, void*** provider, unsigned int* legacy)
{
    BYTE* value;
    if (GetCurrentThreadId() != sc.owner_thread || !sc.editor_global ||
        !sc_access(sc.editor_global, sizeof(void*), 0)) return 0;
    value = (BYTE*)*sc.editor_global;
    if (!sc_aligned(value) || !sc_access(value, sizeof(void*), 0) ||
        *(void**)value != sc.editor_table ||
        !sc_access(value + SC_LEGACY_OFFSET, 1U, 0) ||
        value[SC_LEGACY_OFFSET] > 1U ||
        !sc_aligned(value + SC_PROVIDER_OFFSET) ||
        !sc_access(value + SC_PROVIDER_OFFSET, sizeof(void*), 1)) return 0;
    *editor = value;
    *provider = (void**)(value + SC_PROVIDER_OFFSET);
    *legacy = value[SC_LEGACY_OFFSET];
    return 1;
}

static void sc_reject(void)
{
    if (dluma_atomic_compare_exchange(&sc.rejected, 1, 0) == 0)
        dluma_log_line("ERROR", "editor_source_control_boundary_rejected");
}

static void sc_notify(void* editor, unsigned int legacy)
{
    DlumaEditorSourceControlOptionsLoadedFn notify = 0;
    void* context = 0;
    sc_lock();
    sc.last.size = sizeof(sc.last);
    sc.last.version = DLUMA_EDITOR_SOURCE_CONTROL_VERSION;
    sc.last.editor = editor;
    sc.last.owner_thread = sc.owner_thread;
    sc.last.legacy_enabled = legacy;
    sc.last.boundary_seen = 1U;
    if (sc.notify_enabled) {
        notify = sc.callbacks.options_loaded;
        context = sc.callbacks.context;
        dluma_atomic_increment(&sc.notifying);
    }
    sc_unlock();
    if (notify) {
        __try { notify(editor, sc.owner_thread, legacy, context); }
        __finally { dluma_atomic_decrement(&sc.notifying); }
    }
}

static void* __fastcall sc_getter(void* editor)
{
    (void)editor;
    /* Native no-provider state. Git uses the separately admitted panel. No
     * preference, policy context or native object is borrowed here. */
    dluma_atomic_increment(&sc.active);
    dluma_atomic_decrement(&sc.active);
    return 0;
}

static BOOL __fastcall sc_editor_initialize(void* editor, void* frame, void* progress)
{
    void* current = 0;
    void** provider = 0;
    unsigned int legacy = 0;
    int matched;
    BOOL result;
    void* caller = SOURCE_CONTROL_CALLER();
    dluma_atomic_increment(&sc.active);
    __try {
        matched = caller == sc.editor_init_return &&
            dluma_atomic_compare_exchange(&sc.state, 0, 0) == 2 && sc_owned() &&
            sc_editor(&current, &provider, &legacy) && current == editor;
        if (matched) {
            sc_lock();
            if (sc.last.editor != editor) sc.last.boundary_seen = 0;
            sc.last.editor = editor;
            sc.last.init_returned = 0;
            sc_unlock();
        } else sc_reject();

        /* Preserve native initialization even for an unmatched observer call.
         * The independent provider guard still prevents legacy connection. */
        result = ((ScEditorInitFn)sc.editor_initialize.original)(editor, frame, progress);
        if (matched) {
            if (dluma_atomic_compare_exchange(&sc.rejected, 0, 0) == 0 && sc_owned() &&
                sc_editor(&current, &provider, &legacy) && current == editor && !*provider) {
                DlumaEditorSourceControlInitCompletedFn notify = 0;
                void* context = 0;
                sc_lock();
                sc.last.legacy_enabled = legacy;
                sc.last.init_returned = 1U;
                if (sc.notify_enabled) {
                    notify = sc.callbacks.init_attempt_completed;
                    context = sc.callbacks.context;
                    dluma_atomic_increment(&sc.notifying);
                }
                sc_unlock();
                if (notify) {
                    __try { notify(editor, sc.owner_thread, context); }
                    __finally { dluma_atomic_decrement(&sc.notifying); }
                }
            } else sc_reject();
        }
    } __finally { dluma_atomic_decrement(&sc.active); }
    /* Native Init returns its final profiling call, not a success status. */
    return result;
}

static char __fastcall sc_initialize(void* object, unsigned char asynchronous,
    const void* server, const void* user, const void* workspace)
{
    void* editor = 0;
    void** provider = 0;
    unsigned int legacy = 0;
    void* caller = SOURCE_CONTROL_CALLER();
    (void)asynchronous; (void)server; (void)user; (void)workspace;
    dluma_atomic_increment(&sc.active);
    __try {
        /* Only the fresh, unexposed factory object may be retired here. A
         * mismatch denies connection but does not delete an unknown object. */
        if (caller != sc.factory_return ||
            dluma_atomic_compare_exchange(&sc.state, 0, 0) != 2 || !sc_owned() ||
            !sc_editor(&editor, &provider, &legacy) || !legacy ||
            !sc_aligned(object) || !sc_access(object, SC_PROVIDER_BYTES, 1) ||
            *(void**)object != sc.provider_table ||
            *(void**)((BYTE*)sc.provider_table + 34U * sizeof(void*)) != (void*)sc.destroy ||
            *(void**)((BYTE*)object + 144U) || ((BYTE*)object)[152U] ||
            *(void**)((BYTE*)object + 160U) || *(void**)((BYTE*)object + 352U) ||
            dluma_atomic_pointer_compare_exchange((PVOID volatile*)provider, 0, object) != object) {
            sc_reject();
        } else {
            /* Original native scalar delete owns its constructor's allocator,
             * strings and callback container. The factory only returns after
             * this call and never accesses the retired object again. */
            (void)sc.destroy(object, 1U);
            dluma_atomic_increment(&sc.blocked);
            sc_notify(editor, legacy);
        }
    } __finally { dluma_atomic_decrement(&sc.active); }
    return 0;
}

int dluma_editor_source_control_start_early(
    const DlumaEditorSourceControlCallbacksV1* callbacks)
{
    HMODULE owner = 0;
    void* editor = 0;
    void** provider = 0;
    unsigned int legacy = 0;
    LONG prior;
    if (!callbacks || !sc_access(callbacks, sizeof(*callbacks), 0) ||
        callbacks->size < sizeof(*callbacks) ||
        callbacks->version != DLUMA_EDITOR_SOURCE_CONTROL_VERSION || !callbacks->options_loaded ||
        !callbacks->init_attempt_completed)
        return 0;
    prior = dluma_atomic_compare_exchange(&sc.state, 1, 0);
    if (prior) {
        int same;
        sc_lock();
        same = prior == 2 && !sc.stopped && sc.notify_enabled &&
            sc.callbacks.options_loaded == callbacks->options_loaded && sc.callbacks.context == callbacks->context &&
            sc.callbacks.init_attempt_completed == callbacks->init_attempt_completed;
        sc_unlock();
        return same && sc_owned();
    }
    if (sizeof(void*) != 8U) goto refused;
    sc.owner_thread = GetCurrentThreadId();
    sc.editor_global = (void**)dluma_editor_theme_profile_address(0, SC_EDITOR_GLOBAL, sizeof(void*));
    sc.editor_table = dluma_editor_theme_profile_address(0, SC_EDITOR_TABLE, sizeof(void*));
    sc.provider_table = dluma_editor_theme_profile_address(0, SC_PROVIDER_TABLE, sizeof(void*) * 35U);
    sc.factory_return = dluma_editor_theme_profile_address(0, SC_FACTORY_RETURN, 1U);
    sc.editor_init_return = dluma_editor_theme_profile_address(0, SC_EDITOR_INIT_RETURN, 1U);
    sc.destroy = (ScDeleteFn)dluma_editor_theme_profile_address(0, SC_DELETE_TARGET, 1U);
    sc.getter.address = (PVOID volatile*)dluma_editor_theme_profile_address(0, SC_GETTER_SLOT, sizeof(void*));
    sc.getter.original = dluma_editor_theme_profile_address(0, SC_GETTER_TARGET, 1U);
    sc.getter.replacement = (void*)sc_getter;
    sc.initialize.address = (PVOID volatile*)sc.provider_table;
    sc.initialize.original = dluma_editor_theme_profile_address(0, SC_INIT_TARGET, 1U);
    sc.initialize.replacement = (void*)sc_initialize;
    sc.editor_initialize.address = (PVOID volatile*)dluma_editor_theme_profile_address(0, SC_EDITOR_INIT_SLOT, sizeof(void*));
    sc.editor_initialize.original = dluma_editor_theme_profile_address(0, SC_EDITOR_INIT_TARGET, 1U);
    sc.editor_initialize.replacement = (void*)sc_editor_initialize;
    if (!sc.editor_global || !sc.editor_table || !sc.provider_table || !sc.factory_return || !sc.destroy ||
        !sc.getter.address || !sc.getter.original || !sc.initialize.original || !sc.editor_init_return ||
        !sc.editor_initialize.address || !sc.editor_initialize.original ||
        !sc_aligned((void*)sc.getter.address) || !sc_aligned((void*)sc.initialize.address) ||
        !sc_aligned((void*)sc.editor_initialize.address) ||
        *(void**)((BYTE*)sc.provider_table + 34U * sizeof(void*)) != (void*)sc.destroy ||
        !sc_editor(&editor, &provider, &legacy) || *provider)
        goto refused;
    /* The profile admits the exact executable; pin replacement code before
     * publishing any pointer that another thread can fetch. */
    if (!GetModuleHandleExW(0x00000005U, (LPCWSTR)(const void*)sc_initialize, &owner)) goto refused;
    if (!sc_exchange(&sc.getter, sc.getter.original, sc.getter.replacement) ||
        !sc_exchange(&sc.initialize, sc.initialize.original, sc.initialize.replacement) ||
        !sc_exchange(&sc.editor_initialize, sc.editor_initialize.original, sc.editor_initialize.replacement))
        goto rollback;
    sc_lock();
    sc.callbacks = *callbacks;
    sc.notify_enabled = 1U;
    sc.stopped = 0;
    sc.last.size = sizeof(sc.last);
    sc.last.version = DLUMA_EDITOR_SOURCE_CONTROL_VERSION;
    sc.last.editor = editor;
    sc.last.owner_thread = sc.owner_thread;
    sc.last.boundary_seen = 0;
    sc.last.init_returned = 0;
    sc_unlock();
    dluma_atomic_exchange(&sc.state, 2);
    dluma_log_line("INFO", "editor_source_control_boundary_installed");
    return 1;
rollback:
    /* Do not release code/state if either pointer/protection remains owned. */
    {
        int editor_initialize_restored = sc_restore(&sc.editor_initialize);
        int initialize_restored = sc_restore(&sc.initialize);
        int getter_restored = sc_restore(&sc.getter);
        if (!editor_initialize_restored || !initialize_restored || !getter_restored) {
            dluma_atomic_exchange(&sc.state, -1);
            dluma_log_line("ERROR", "editor_source_control_boundary_retained");
            return 0;
        }
    }
refused:
    dluma_atomic_exchange(&sc.state, 0);
    dluma_log_line("WARN", "editor_source_control_boundary_unavailable");
    return 0;
}

int dluma_editor_source_control_native_snapshot(DlumaEditorSourceControlSnapshotV1* snapshot)
{
    void* editor = 0;
    void** provider = 0;
    unsigned int legacy = 0;
    if (!snapshot || !sc_access(snapshot, sizeof(*snapshot), 1) ||
        snapshot->size < sizeof(*snapshot) || snapshot->version != DLUMA_EDITOR_SOURCE_CONTROL_VERSION ||
        dluma_atomic_compare_exchange(&sc.state, 0, 0) != 2)
        return 0;
    sc_lock();
    if (sc.stopped) { sc_unlock(); return 0; }
    sc_unlock();
    if (!sc_owned() || dluma_atomic_compare_exchange(&sc.rejected, 0, 0) != 0 ||
        !sc_editor(&editor, &provider, &legacy) || *provider) return 0;
    sc_lock();
    if (sc.stopped) { sc_unlock(); return 0; }
    *snapshot = sc.last;
    if (snapshot->editor != editor) {
        snapshot->boundary_seen = 0;
        snapshot->init_returned = 0;
    }
    snapshot->editor = editor;
    snapshot->owner_thread = sc.owner_thread;
    snapshot->legacy_enabled = legacy;
    sc_unlock();
    return 1;
}

int dluma_editor_source_control_stop(void)
{
    LONG state;
    sc_lock();
    sc.notify_enabled = 0;
    sc.stopped = 1U;
    if (dluma_atomic_compare_exchange(&sc.notifying, 0, 0) != 0) { sc_unlock(); return 0; }
    memset(&sc.callbacks, 0, sizeof(sc.callbacks));
    sc_unlock();
    state = dluma_atomic_compare_exchange(&sc.state, 0, 0);
    if (state == 1) return 0;
    if (state == -1) {
        int editor_initialize_restored = sc_restore(&sc.editor_initialize);
        int initialize_restored = sc_restore(&sc.initialize);
        int getter_restored = sc_restore(&sc.getter);
        if (!editor_initialize_restored || !initialize_restored || !getter_restored) return 0;
        dluma_atomic_exchange(&sc.state, 0);
    }
    /* The native routes remain safe and process-pinned. They borrow no
     * stopped policy context and never authorize legacy reconnection. */
    return 1;
}

int dluma_editor_source_control_callbacks_drained(void)
{
    return dluma_atomic_compare_exchange(&sc.active, 0, 0) == 0 &&
        dluma_atomic_compare_exchange(&sc.notifying, 0, 0) == 0;
}

unsigned int dluma_editor_source_control_blocked_count(void)
{
    return (unsigned int)dluma_atomic_compare_exchange(&sc.blocked, 0, 0);
}
