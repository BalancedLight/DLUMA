#include "dlw_boss_actions.h"
#include "dlw_log.h"
#include "dlw_path_security.h"
#include "core/dlw_quest_phase.h"
#include "game/dlw_mod_handler_service.h"
#include "bindings/dlw_player_actor_binding.h"
#include "dlw_isolation_startup.h"
#include "core/dlw_engine_script_bootstrap.h"
#include "core/dlw_boss_scheduler_registry.h"
#include "game/dlw_boss_template_resolver.h"
#include "game/dlw_boss_receiver_route.h"
#if defined(DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC) && DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC
#include "dlw_project_owner_identity.h"
#endif
#define DLUMA_BOSS_PERIODIC_IMPLEMENTATION 1
#include <stddef.h>
#include <limits.h>
#include <string.h>

#define DLUMA_BOSS_FACTORY_RVA 0x00A916F0UL
#define DLUMA_BOSS_STRING_RVA 0x01289FE0UL
#define DLUMA_BOSS_BASE_VTABLE_RVA 0x017C1FA8UL
#define DLUMA_BOSS_FREE_IAT_RVA 0x013CC198UL
#define DLUMA_BOSS_STOLEN 15U
#define DLUMA_BOSS_JUMP 14U
#define DLUMA_BOSS_MAX_INSTRUCTIONS 1024L
enum { DLUMA_BOSS_CALL_MOD = 0, DLUMA_BOSS_PERIODIC_START, DLUMA_BOSS_PERIODIC_STOP };

typedef struct DlumaBossString {
    char* data;
    unsigned int size;
    unsigned int capacity;
} DlumaBossString;

typedef struct DlumaBossParameter {
    const void* source;
    const void* list;
    const void* element;
    const void* context;
    unsigned int index;
    unsigned int padding;
} DlumaBossParameter;

typedef void* (__fastcall* DlumaBossFactory)(void* source, void* phase);
typedef DlumaBossString* (__fastcall* DlumaBossReadString)(
    DlumaBossParameter* parameter, DlumaBossString* output);
typedef void (__cdecl* DlumaBossFree)(void* memory);
typedef void* (__cdecl* DlumaBossMalloc)(size_t size);

typedef struct DlumaBossInstruction DlumaBossInstruction;
typedef struct DlumaBossVtable {
    void* (__fastcall* destroy)(DlumaBossInstruction*, unsigned int);
    void (__fastcall* load)(DlumaBossInstruction*, void*);
    void (__fastcall* execute)(DlumaBossInstruction*);
    void (__fastcall* reset)(DlumaBossInstruction*);
    void (__fastcall* save_rollback)(DlumaBossInstruction*);
    void (__fastcall* rollback)(DlumaBossInstruction*);
    DlumaBossString* (__fastcall* debug)(DlumaBossInstruction*, DlumaBossString*);
} DlumaBossVtable;

struct DlumaBossInstruction {
    const DlumaBossVtable* vtable;
    unsigned char done;
    unsigned char padding[7];
    void* phase;
    unsigned int valid;
    unsigned int map_generation;
    unsigned int kind;
#if DLUMA_BOSS_PERIODIC_IMPLEMENTATION
    uint64_t periodic_owner, periodic_token, periodic_epoch;
    unsigned int periodic_pins, periodic_retired, periodic_free_pending;
    char periodic_start_phase[128];
#endif
    char mod_id[128];
    char handler_id[128];
    char arguments[DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES + 1U];
};

typedef char DlumaBossDoneOffset[(offsetof(DlumaBossInstruction, done) == 8) ? 1 : -1];
typedef char DlumaBossPhaseOffset[(offsetof(DlumaBossInstruction, phase) == 16) ? 1 : -1];

static struct {
    HMODULE module;
    BYTE* target;
    BYTE* trampoline;
    DlumaBossFactory original;
    DlumaBossReadString read_string;
    DlumaBossFree free_string;
    DlumaBossMalloc malloc_string;
    BYTE original_bytes[DLUMA_BOSS_STOLEN];
    BYTE patch[DLUMA_BOSS_STOLEN];
    volatile LONG installed;
    volatile LONG ever_published;
    DWORD factory_protection;
    volatile LONG factory_protection_pending;
    volatile LONG stopping;
    volatile LONG callbacks;
    volatile LONG instructions;
    volatile LONG map_generation;
} g_dluma_boss;

#if DLUMA_BOSS_PERIODIC_IMPLEMENTATION
#if defined(DLUMA_BOSS_LIFECYCLE_TEST)
extern int dluma_boss_periodic_host_sample(void*, uint64_t*, uint32_t*, uint32_t*, const void**, const void**, const void**);
extern int dluma_boss_periodic_host_available(void);
#else
static int dluma_boss_periodic_host_sample(void*, uint64_t*, uint32_t*, uint32_t*, const void**, const void**, const void**);
#endif
static DlumaBossSchedulerRegistry g_dluma_boss_periodic_registry;
static int dluma_boss_periodic_available(void);
static DlumaBossInstruction* g_dluma_boss_periodic_starts[DLUMA_BOSS_REGISTRY_CAPACITY];
static volatile LONG g_dluma_boss_periodic_lock;
typedef struct DlumaBossPendingAdmission {
    uint64_t owner, revision;
    unsigned int references, poisoned;
} DlumaBossPendingAdmission;
static DlumaBossPendingAdmission g_dluma_boss_pending[DLUMA_BOSS_REGISTRY_CAPACITY];
#define DLUMA_BOSS_STOP_JOURNAL_CAPACITY 64U
typedef struct DlumaBossStopRecord { uint64_t owner, sequence; } DlumaBossStopRecord;
static DlumaBossStopRecord g_dluma_boss_stop_journal[DLUMA_BOSS_STOP_JOURNAL_CAPACITY];
static uint64_t g_dluma_boss_stop_sequence;
static unsigned int g_dluma_boss_stop_exhausted;
static void dluma_boss_periodic_enter(void)
{
    while (dluma_atomic_compare_exchange(&g_dluma_boss_periodic_lock, 1, 0) != 0) Sleep(0);
}
static void dluma_boss_periodic_leave(void) { dluma_atomic_exchange(&g_dluma_boss_periodic_lock, 0); }
static void dluma_boss_periodic_note_stop_locked(uint64_t owner)
{
    unsigned int i;
    if (g_dluma_boss_stop_sequence == UINT64_MAX) g_dluma_boss_stop_exhausted = 1;
    else {
        DlumaBossStopRecord* record;
        ++g_dluma_boss_stop_sequence;
        record = &g_dluma_boss_stop_journal[(g_dluma_boss_stop_sequence - 1U) % DLUMA_BOSS_STOP_JOURNAL_CAPACITY];
        record->owner = owner; record->sequence = g_dluma_boss_stop_sequence;
    }
    for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i) {
        DlumaBossPendingAdmission* pending = &g_dluma_boss_pending[i];
        if (pending->owner && (!owner || pending->owner == owner)) {
            if (pending->revision == UINT64_MAX) pending->poisoned = 1;
            else ++pending->revision;
        }
    }
}
static int dluma_boss_periodic_read(const void* p, size_t n, void* unused)
{ (void)unused; return dluma_is_readable(p, n); }
static void dluma_boss_periodic_retire_locked(DlumaBossInstruction* self)
{
    unsigned int i;
    if (self->periodic_owner && self->periodic_token)
        (void)dluma_boss_registry_stop(&g_dluma_boss_periodic_registry, self->periodic_owner, self->periodic_token);
    self->periodic_owner = self->periodic_token = 0;
    for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i)
        if (g_dluma_boss_periodic_starts[i] == self) g_dluma_boss_periodic_starts[i] = 0;
    if (self->periodic_epoch == UINT64_MAX) self->periodic_retired = 1;
    else ++self->periodic_epoch;
}
#if defined(DLUMA_BOSS_LIFECYCLE_TEST)
static int dluma_boss_periodic_is_active(DlumaBossInstruction* self)
{
    unsigned int i; int active = 0;
    dluma_boss_periodic_enter();
    for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i)
        if (self->periodic_token && g_dluma_boss_periodic_registry.slots[i].owner_key == self->periodic_owner &&
            g_dluma_boss_periodic_registry.slots[i].token == self->periodic_token) active = 1;
    dluma_boss_periodic_leave();
    return active;
}
#endif
#endif

static const BYTE g_dluma_boss_factory_prefix[DLUMA_BOSS_STOLEN] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,
    0x24,0x10,0x48,0x89,0x74,0x24,0x18
};
static const BYTE g_dluma_boss_string_prefix[] = {
    0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,
    0x24,0x20,0x57,0x48,0x83,0xEC,0x40,0x48,
    0x83,0x79,0x08,0x00,0x48,0x8B,0xFA,0x48
};

static int dluma_boss_section(BYTE* base, DWORD image_size,
    DWORD rva, SIZE_T size, DWORD required, DWORD forbidden)
{
    DWORD nt;
    WORD count, optional_size;
    BYTE* section;
    unsigned int i;
    if (!rva || rva >= image_size || size > image_size - rva ||
        !dluma_is_readable(base, 64U)) return 0;
    nt = *(const DWORD*)(base + 0x3CU);
    if (nt > 0x100000U || !dluma_is_readable(base + nt, 24U)) return 0;
    count = *(const WORD*)(base + nt + 6U);
    optional_size = *(const WORD*)(base + nt + 20U);
    section = base + nt + 24U + optional_size;
    if (!count || count > 96U || !dluma_is_readable(section, count * 40U)) return 0;
    for (i = 0; i < count; ++i, section += 40U) {
        DWORD span = *(const DWORD*)(section + 8U);
        DWORD address = *(const DWORD*)(section + 12U);
        DWORD raw_size = *(const DWORD*)(section + 16U);
        DWORD flags = *(const DWORD*)(section + 36U);
        if (raw_size > span) span = raw_size;
        if (rva >= address && rva - address < span &&
            size <= span - (rva - address))
            return (flags & required) == required && !(flags & forbidden) &&
                dluma_is_readable(base + rva, size);
    }
    return 0;
}

static int dluma_boss_bind(HMODULE module)
{
    static const DWORD methods[7] = {
        0xA91580UL,0x130A144UL,0x130A144UL,0xA91420UL,
        0x428080UL,0x428080UL,0x130A144UL
    };
    BYTE* base = (BYTE*)module;
    WCHAR path[DLUMA_MAX_WPATH];
    char hash[DLUMA_SHA256_HEX_CAPACITY];
    DWORD timestamp, image_size;
    HMODULE crt;
    FARPROC free_proc, malloc_proc;
    unsigned int i;
    if (!module || module != GetModuleHandleW(L"gamedll_x64_rwdi.dll") ||
        !dluma_is_readable(base, 4096U) ||
        !dluma_build_identity_parse_pe_header(base, 4096U, &timestamp, &image_size) ||
        timestamp != 0x623397AAUL || image_size != 0x02082000UL ||
        !dluma_get_module_path_checked(module, path, DLUMA_ARRAY_COUNT(path)) ||
        !dluma_sha256_file_hex(path, 512ULL * 1024ULL * 1024ULL, hash) ||
        (dluma_stricmp(hash,
            "8E19C89C316E319E1E88D0C7A6F6A935C4B9C381905CAB48449CEE8FD6CCBA54") &&
         dluma_stricmp(hash,
            "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95")) ||
        !dluma_boss_section(base, image_size, DLUMA_BOSS_FACTORY_RVA,
            sizeof(g_dluma_boss_factory_prefix), 0x60000000UL, 0x80000000UL) ||
        !dluma_boss_section(base, image_size, DLUMA_BOSS_STRING_RVA,
            sizeof(g_dluma_boss_string_prefix), 0x60000000UL, 0x80000000UL) ||
        !dluma_boss_section(base, image_size, DLUMA_BOSS_BASE_VTABLE_RVA,
            sizeof(methods) * 2U, 0x40000000UL, 0xA0000000UL) ||
        !dluma_boss_section(base, image_size, DLUMA_BOSS_FREE_IAT_RVA,
            sizeof(void*), 0x40000000UL, 0x20000000UL) ||
        memcmp(base + DLUMA_BOSS_FACTORY_RVA, g_dluma_boss_factory_prefix,
            sizeof(g_dluma_boss_factory_prefix)) ||
        memcmp(base + DLUMA_BOSS_STRING_RVA, g_dluma_boss_string_prefix,
            sizeof(g_dluma_boss_string_prefix))) return 0;
    for (i = 0; i < DLUMA_ARRAY_COUNT(methods); ++i)
        if (*(const ULONG_PTR*)(base + DLUMA_BOSS_BASE_VTABLE_RVA + i * 8U) !=
            (ULONG_PTR)(base + methods[i])) return 0;
    crt = GetModuleHandleW(L"MSVCR100.dll");
    free_proc = crt ? GetProcAddress(crt, "free") : 0;
    malloc_proc = crt ? GetProcAddress(crt, "malloc") : 0;
    if (!free_proc || !malloc_proc ||
        *(const ULONG_PTR*)(base + DLUMA_BOSS_FREE_IAT_RVA) !=
            (ULONG_PTR)free_proc) return 0;
    g_dluma_boss.module = module;
    g_dluma_boss.read_string = (DlumaBossReadString)(base + DLUMA_BOSS_STRING_RVA);
    g_dluma_boss.free_string = (DlumaBossFree)free_proc;
    g_dluma_boss.malloc_string = (DlumaBossMalloc)malloc_proc;
    return 1;
}

static int dluma_boss_copy_string(const DlumaBossString* value,
    char* output, unsigned int capacity)
{
    if (!value || !value->data || value->size >= capacity ||
        value->size > value->capacity ||
        !dluma_is_readable(value->data, (SIZE_T)value->size + 1U) ||
        value->data[value->size] || memchr(value->data, 0, value->size)) return 0;
    memcpy(output, value->data, value->size);
    output[value->size] = 0;
    return 1;
}

static int dluma_boss_identifier(const char* value)
{
    unsigned int i;
    if (!value[0]) return 0;
    for (i = 0; value[i]; ++i) {
        char c = value[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static int dluma_boss_authored_name(const char* value)
{
    unsigned int i;
    if (!value[0]) return 0;
    for (i = 0; value[i]; ++i) {
        char c = value[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static void __fastcall dluma_boss_load(DlumaBossInstruction* self, void* source)
{
    const BYTE* cursor = (const BYTE*)source;
    const BYTE* node;
    int depth;
    unsigned int i, json_size = 0;
    DlumaBossParameter parameter;
    DlumaQuestPhaseDiagnostic diagnostic;
    char json[DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES + 1U];
    self->valid = 0;
    if (dluma_atomic_compare_exchange(&g_dluma_boss.stopping, 0, 0)) return;
    if (!cursor || !dluma_is_readable(cursor, 896U)) return;
    node = *(const BYTE* const*)cursor;
    depth = *(const int*)(cursor + 400U);
    if (!node || !dluma_is_readable(node, 96U) || depth < 0 || depth > 28) return;
    if (self->kind == DLUMA_BOSS_PERIODIC_STOP) {
        self->valid = *(const unsigned int*)(node + 88U) == 0U;
        return;
    }
    if (*(const unsigned int*)(node + 88U) != 3U) return;
    memset(&parameter, 0, sizeof(parameter));
    parameter.source = source;
    parameter.list = node + 72U;
    parameter.element = *(const void* const*)(node + 72U);
    parameter.context = *(const void* const*)(cursor + 416U + 16U * (unsigned int)depth);
    for (i = 0; i < 3U; ++i) {
        DlumaBossString value;
        char* output = i == 0 ? self->mod_id : i == 1 ? self->handler_id : json;
        unsigned int capacity = i < 2U ? 128U : sizeof(json);
        int copied;
        if (!parameter.element || !dluma_is_readable(parameter.element, 24U)) return;
        memset(&value, 0, sizeof(value));
        g_dluma_boss.read_string(&parameter, &value);
        copied = dluma_boss_copy_string(&value, output, capacity);
        g_dluma_boss.free_string(value.data);
        if (!copied) return;
        parameter.element = *(const void* const*)parameter.element;
        ++parameter.index;
    }
    if (self->kind == DLUMA_BOSS_PERIODIC_START) {
        if (!dluma_boss_authored_name(self->mod_id) || !dluma_boss_authored_name(self->handler_id) ||
            !dluma_boss_authored_name(json) || strlen(json) >= 128U ||
            !dluma_strcmp(self->mod_id, self->handler_id)) return;
        memcpy(self->arguments, json, strlen(json) + 1U);
        self->valid = 1U;
        return;
    }
    if (!dluma_boss_identifier(self->mod_id) || !dluma_boss_identifier(self->handler_id) ||
        !dluma_quest_phase_json_canonicalize_object(json, (unsigned int)strlen(json),
            self->arguments, sizeof(self->arguments), &json_size, &diagnostic)) return;
    self->valid = 1U;
}

#if DLUMA_BOSS_PERIODIC_IMPLEMENTATION
static const DlumaBossVtable* dluma_boss_own_vtable(void);
static void dluma_boss_periodic_start(DlumaBossInstruction* self)
{
    const BYTE* phase;
    void* owner = 0;
    uint64_t epoch, now = 0, token = 0, stop_sequence;
    uint64_t pending_revision = 0;
    uint32_t delay = 0, choice_random = 0;
    const void *flash = 0, *chatter = 0, *transition = 0;
    char start_phase[128], flash_phase[128], chatter_phase[128], group[128];
    DlumaBossTemplateSpec spec;
    int valid = 0, free_now = 0, accepted = 0;
    unsigned int i, pending_index = DLUMA_BOSS_REGISTRY_CAPACITY;
    unsigned int admission_generation = (unsigned int)dluma_atomic_compare_exchange(&g_dluma_boss.map_generation, 0, 0);
    dluma_boss_periodic_enter();
    if (self->periodic_retired || self->periodic_pins == UINT_MAX || self->periodic_token ||
        !self->map_generation || self->map_generation != admission_generation) {
        dluma_boss_periodic_leave(); return;
    }
    ++self->periodic_pins;
    epoch = self->periodic_epoch;
    stop_sequence = g_dluma_boss_stop_sequence;
    phase = (const BYTE*)self->phase;
    memcpy(flash_phase, self->mod_id, sizeof(flash_phase));
    memcpy(chatter_phase, self->handler_id, sizeof(chatter_phase));
    memcpy(group, self->arguments, sizeof(group));
    dluma_boss_periodic_leave();
    __try {
    if (phase && dluma_is_readable(phase, 32U) &&
        dluma_boss_copy_string((const DlumaBossString*)(phase + 16U), start_phase, sizeof(start_phase))) {
        owner = *(void* const*)phase;
        if (owner) {
            int cancelled = 0;
            dluma_boss_periodic_enter();
            if (g_dluma_boss_stop_exhausted || g_dluma_boss_stop_sequence < stop_sequence ||
                g_dluma_boss_stop_sequence - stop_sequence > DLUMA_BOSS_STOP_JOURNAL_CAPACITY) cancelled = 1;
            for (i = 0; !cancelled && i < DLUMA_BOSS_STOP_JOURNAL_CAPACITY; ++i)
                if (g_dluma_boss_stop_journal[i].sequence > stop_sequence &&
                    (!g_dluma_boss_stop_journal[i].owner || g_dluma_boss_stop_journal[i].owner == (uint64_t)(ULONG_PTR)owner)) cancelled = 1;
            if (!cancelled) {
            for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i)
                if (g_dluma_boss_pending[i].owner == (uint64_t)(ULONG_PTR)owner) { pending_index = i; break; }
            if (pending_index == DLUMA_BOSS_REGISTRY_CAPACITY)
                for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i)
                    if (!g_dluma_boss_pending[i].owner) { pending_index = i; break; }
            if (pending_index != DLUMA_BOSS_REGISTRY_CAPACITY) {
                DlumaBossPendingAdmission* pending = &g_dluma_boss_pending[pending_index];
                if (pending->poisoned || pending->references == UINT_MAX) pending_index = DLUMA_BOSS_REGISTRY_CAPACITY;
                else {
                    pending->owner = (uint64_t)(ULONG_PTR)owner;
                    ++pending->references;
                    pending_revision = pending->revision;
                }
            }
            }
            dluma_boss_periodic_leave();
        }
        if (pending_index != DLUMA_BOSS_REGISTRY_CAPACITY && dluma_boss_periodic_host_sample(owner, &now, &delay, &choice_random, &flash, &chatter, &transition)) {
            spec.flash_phase = flash_phase; spec.chatter_phase = chatter_phase; spec.chatter_group = group;
            spec.flash_vtable = flash; spec.chatter_vtable = chatter; spec.start_phase = start_phase;
            spec.start_instruction = self; spec.start_vtable = dluma_boss_own_vtable();
            spec.transition_vtable = transition;
            valid = dluma_boss_template_visit(owner, &spec, DLUMA_BOSS_CHOICE_NONE,
                dluma_boss_periodic_read, 0, 0, 0) == DLUMA_BT_OK;
        }
    }
    } __except (1) { valid = 0; }
    dluma_boss_periodic_enter();
    if (valid && dluma_boss_periodic_available() && !g_dluma_boss_stop_exhausted && pending_index != DLUMA_BOSS_REGISTRY_CAPACITY &&
        admission_generation == (unsigned int)dluma_atomic_compare_exchange(&g_dluma_boss.map_generation, 0, 0) &&
        !g_dluma_boss_pending[pending_index].poisoned &&
        g_dluma_boss_pending[pending_index].revision == pending_revision &&
        !self->periodic_retired && self->periodic_epoch == epoch && !self->periodic_token &&
        dluma_boss_registry_start(&g_dluma_boss_periodic_registry, (uint64_t)(ULONG_PTR)owner,
            now, delay, &token) == DLUMA_BS_OK) {
        self->periodic_owner = (uint64_t)(ULONG_PTR)owner;
        self->periodic_token = token;
        memcpy(self->periodic_start_phase, start_phase, sizeof(start_phase));
        for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i)
            if (g_dluma_boss_periodic_registry.slots[i].owner_key == self->periodic_owner &&
                g_dluma_boss_periodic_registry.slots[i].token == token) g_dluma_boss_periodic_starts[i] = self;
        accepted = 1;
    }
    if (pending_index != DLUMA_BOSS_REGISTRY_CAPACITY) {
        DlumaBossPendingAdmission* pending = &g_dluma_boss_pending[pending_index];
        if (--pending->references == 0) memset(pending, 0, sizeof(*pending));
    }
    --self->periodic_pins;
    free_now = !self->periodic_pins && self->periodic_free_pending;
    dluma_boss_periodic_leave();
    if (!accepted) dluma_log_line("WARN", "Periodic boss choice start was rejected");
#if !defined(DLUMA_BOSS_LIFECYCLE_TEST) && defined(DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC) && DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC
    else dluma_log_u64("INFO", "boss_periodic_start_generation", admission_generation);
#endif
    if (free_now) { dluma_free(self); dluma_atomic_decrement(&g_dluma_boss.instructions); }
}

static void dluma_boss_periodic_stop(DlumaBossInstruction* self)
{
    const BYTE* phase = (const BYTE*)self->phase;
    uint64_t owner;
    unsigned int i;
    if (!phase || !dluma_is_readable(phase, sizeof(void*))) return;
    owner = (uint64_t)(ULONG_PTR)*(void* const*)phase;
    if (!owner) return;
    dluma_boss_periodic_enter();
    dluma_boss_periodic_note_stop_locked(owner);
    for (i = 0; i < DLUMA_BOSS_REGISTRY_CAPACITY; ++i) {
        DlumaBossSchedulerSlot* slot = &g_dluma_boss_periodic_registry.slots[i];
        if (slot->owner_key == owner) {
            (void)dluma_boss_registry_stop(&g_dluma_boss_periodic_registry, owner, slot->token);
            break;
        }
    }
    dluma_boss_periodic_leave();
}
#endif

static void* __fastcall dluma_boss_destroy(DlumaBossInstruction* self, unsigned int flags)
{
#if DLUMA_BOSS_PERIODIC_IMPLEMENTATION
    if (self->kind == DLUMA_BOSS_PERIODIC_START) {
        int free_now;
        dluma_boss_periodic_enter();
        dluma_boss_periodic_retire_locked(self);
        self->periodic_retired = 1;
        if (flags & 1U) self->periodic_free_pending = 1;
        free_now = self->periodic_free_pending && !self->periodic_pins;
        dluma_boss_periodic_leave();
        if (free_now) { dluma_free(self); dluma_atomic_decrement(&g_dluma_boss.instructions); }
        return self;
    }
#endif
    if (flags & 1U) {
        dluma_free(self);
        dluma_atomic_decrement(&g_dluma_boss.instructions);
    }
    return self;
}

static void __fastcall dluma_boss_execute(DlumaBossInstruction* self)
{
    unsigned int generation;
    DlumaStatus status;
    char scenario[512] = {0}, phase_name[128] = {0};
    char mod_id[128], handler_id[128];
    const BYTE* phase = (const BYTE*)self->phase;
    const BYTE* boss;
    if (dluma_atomic_compare_exchange(&g_dluma_boss.stopping, 0, 0)) { self->done = 1; return; }
    dluma_atomic_increment(&g_dluma_boss.callbacks);
    __try {
    if (dluma_atomic_compare_exchange(&g_dluma_boss.stopping, 0, 0)) { self->done = 1; return; }
    if (self->done) return;
    self->done = 1;
    if (self->kind != DLUMA_BOSS_CALL_MOD) {
        if (!self->valid)
            dluma_log_line("WARN", "Periodic boss choice has invalid arguments");
#if DLUMA_BOSS_PERIODIC_IMPLEMENTATION
        else if (!dluma_boss_periodic_available()) dluma_log_line("WARN", "Periodic boss choice is unavailable on this build");
        else if (self->kind == DLUMA_BOSS_PERIODIC_START) dluma_boss_periodic_start(self);
        else dluma_boss_periodic_stop(self);
#else
        else
            dluma_log_line("WARN", "Periodic boss choice is unavailable on this build");
#endif
        return;
    }
    if (!self->valid || dluma_atomic_compare_exchange(&g_dluma_boss.stopping, 0, 0)) return;
    generation = (unsigned int)dluma_atomic_compare_exchange(&g_dluma_boss.map_generation, 0, 0);
    if (!generation) return;
    if (!self->map_generation) self->map_generation = generation;
    if (self->map_generation != generation) return;
    if (phase && dluma_is_readable(phase, 32U)) {
        (void)dluma_boss_copy_string((const DlumaBossString*)(phase + 16U),
            phase_name, sizeof(phase_name));
        boss = *(const BYTE* const*)phase;
        if (boss && dluma_is_readable(boss, 88U))
            (void)dluma_boss_copy_string((const DlumaBossString*)(boss + 72U),
                scenario, sizeof(scenario));
    }
    memcpy(mod_id, self->mod_id, sizeof(mod_id));
    memcpy(handler_id, self->handler_id, sizeof(handler_id));
    status = dluma_mod_handler_service_enqueue_boss(mod_id, handler_id,
        self->arguments, generation, scenario, phase_name);
    if (status != DLUMA_STATUS_OK) {
        dluma_log_pair("WARN", "boss_call_mod_target", mod_id);
        dluma_log_pair("WARN", "boss_call_mod_handler", handler_id);
        dluma_log_u64("WARN", "boss_call_mod_status", (unsigned int)status);
    }
    } __finally { dluma_atomic_decrement(&g_dluma_boss.callbacks); }
}

static void __fastcall dluma_boss_reset(DlumaBossInstruction* self)
{
#if DLUMA_BOSS_PERIODIC_IMPLEMENTATION
    if (self->kind == DLUMA_BOSS_PERIODIC_START) {
        dluma_boss_periodic_enter();
        dluma_boss_periodic_retire_locked(self);
        self->done = 0;
        dluma_boss_periodic_leave();
        return;
    }
#endif
    self->done = 0;
}
static void __fastcall dluma_boss_noop(DlumaBossInstruction* self) { (void)self; }
static DlumaBossString* __fastcall dluma_boss_debug(
    DlumaBossInstruction* self, DlumaBossString* output)
{
    const char* text = self->kind == DLUMA_BOSS_PERIODIC_START ? "DLUMAPeriodicChoiceStart\n" :
        self->kind == DLUMA_BOSS_PERIODIC_STOP ? "DLUMAPeriodicChoiceStop\n" : "CallMod\n";
    size_t size = strlen(text);
    memset(output, 0, sizeof(*output));
    output->data = (char*)g_dluma_boss.malloc_string(size + 1U);
    if (output->data) {
        memcpy(output->data, text, size + 1U);
        output->size = output->capacity = (unsigned int)size;
    }
    return output;
}
static const DlumaBossVtable g_dluma_boss_vtable = {
    dluma_boss_destroy, dluma_boss_load, dluma_boss_execute, dluma_boss_reset,
    dluma_boss_noop, dluma_boss_noop, dluma_boss_debug
};
#if DLUMA_BOSS_PERIODIC_IMPLEMENTATION
static const DlumaBossVtable* dluma_boss_own_vtable(void) { return &g_dluma_boss_vtable; }
#include "dlw_boss_periodic_native.inc"
#endif

static void* __fastcall dluma_boss_factory_hook(void* source, void* phase)
{
    const BYTE* node = 0;
    const char* command = 0;
    void* result;
    DlumaBossInstruction* pending_instruction = 0;
    unsigned int kind = DLUMA_BOSS_CALL_MOD;
    int recognized = 0;
    /* Retained pass-through never reads parser data or retired mod services. */
    if (dluma_atomic_compare_exchange(&g_dluma_boss.stopping, 0, 0)) return g_dluma_boss.original(source, phase);
    dluma_atomic_increment(&g_dluma_boss.callbacks);
    __try {
    if (dluma_atomic_compare_exchange(&g_dluma_boss.stopping, 0, 0)) result = g_dluma_boss.original(source, phase);
    else {
    if (source && dluma_is_readable(source, sizeof(void*)))
        node = *(const BYTE* const*)source;
    if (node && dluma_is_readable(node, 64U))
        command = *(const char* const*)(node + 56U);
    if (command && dluma_is_readable(command, 8U) && !memcmp(command, "CallMod", 8U)) recognized = 1;
    else if (command && dluma_is_readable(command, sizeof("DLUMAPeriodicChoiceStart")) &&
        !memcmp(command, "DLUMAPeriodicChoiceStart", sizeof("DLUMAPeriodicChoiceStart"))) {
        recognized = 1; kind = DLUMA_BOSS_PERIODIC_START;
    } else if (command && dluma_is_readable(command, sizeof("DLUMAPeriodicChoiceStop")) &&
        !memcmp(command, "DLUMAPeriodicChoiceStop", sizeof("DLUMAPeriodicChoiceStop"))) {
        recognized = 1; kind = DLUMA_BOSS_PERIODIC_STOP;
    }
    if (recognized) {
        DlumaBossInstruction* instruction = 0;
        if (!dluma_atomic_compare_exchange(&g_dluma_boss.stopping, 0, 0)) {
            LONG reserved = dluma_atomic_increment(&g_dluma_boss.instructions);
            if (reserved <= DLUMA_BOSS_MAX_INSTRUCTIONS)
                instruction = (DlumaBossInstruction*)dluma_alloc(sizeof(*instruction));
            if (instruction) {
                pending_instruction = instruction;
                memset(instruction, 0, sizeof(*instruction));
                instruction->vtable = &g_dluma_boss_vtable;
                instruction->phase = phase;
                instruction->kind = kind;
                if (kind == DLUMA_BOSS_PERIODIC_START)
                    instruction->map_generation = (unsigned int)dluma_atomic_compare_exchange(&g_dluma_boss.map_generation, 0, 0);
                dluma_boss_load(instruction, source);
                if (!instruction->valid)
                    dluma_log_line("WARN", kind == DLUMA_BOSS_CALL_MOD ?
                        "CallMod requires a mod ID, handler ID, and JSON object string" :
                        "Periodic boss choice has invalid arguments");
            } else dluma_atomic_decrement(&g_dluma_boss.instructions);
        }
        result = instruction;
        pending_instruction = 0;
    } else result = g_dluma_boss.original(source, phase);
    }
    } __finally {
        if (pending_instruction) { dluma_free(pending_instruction); dluma_atomic_decrement(&g_dluma_boss.instructions); }
        dluma_atomic_decrement(&g_dluma_boss.callbacks);
    }
    return result;
}

static void dluma_boss_jump(BYTE* output, ULONG_PTR destination)
{
    memset(output, 0, DLUMA_BOSS_JUMP);
    output[0] = 0xFF; output[1] = 0x25;
    memcpy(output + 6U, &destination, sizeof(destination));
}

static int dluma_boss_factory_pin_modules(void)
{
    HMODULE loader, game;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        (LPCWSTR)(ULONG_PTR)dluma_boss_factory_hook, &loader) &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            (LPCWSTR)g_dluma_boss.module, &game) && game == g_dluma_boss.module;
}

static int dluma_boss_factory_restore_protection(void)
{
    DWORD ignored;
    MEMORY_BASIC_INFORMATION memory;
    if (!g_dluma_boss.factory_protection_pending) return 1;
    if (!g_dluma_boss.target || !VirtualProtect(g_dluma_boss.target, DLUMA_BOSS_STOLEN,
        g_dluma_boss.factory_protection, &ignored) ||
        !VirtualQuery(g_dluma_boss.target, &memory, sizeof(memory)) || memory.Protect != g_dluma_boss.factory_protection) return 0;
    dluma_atomic_exchange(&g_dluma_boss.factory_protection_pending, 0); return 1;
}

typedef struct DlumaBossColdReader { uintptr_t base; DWORD size; } DlumaBossColdReader;
static int dluma_boss_cold_read(void *context, uintptr_t address, void *output, size_t bytes)
{
    const DlumaBossColdReader *reader = context;
    uintptr_t offset;
    int valid = 0;
    if (!bytes || address < reader->base) return 0;
    offset = address - reader->base;
    if (offset >= reader->size || bytes > reader->size - offset) return 0;
    __try {
        if (dluma_is_readable((const void *)address, bytes)) { memcpy(output, (const void *)address, bytes); valid = 1; }
    } __except (1) { valid = 0; }
    return valid;
}
static int dluma_boss_cold_current(const DlumaEngineScriptBootstrapInput *input)
{
    DlumaIsolationScriptEntryObservation entry;
    DlumaEngineScriptBootstrapResult result;
    DlumaEngineScriptBootstrapInput current;
    if (!input || !dluma_isolation_snapshot_script_entry(&entry) ||
        (uintptr_t)entry.return_address != input->actual_return_address ||
        (uintptr_t)entry.callback_address != input->proxy_initializer || entry.thread_id != input->entry_thread) return 0;
    current = *input; current.current_thread = GetCurrentThreadId();
    return dluma_engine_script_bootstrap_entered(&current, &result) && result.entry_validated;
}

static int dluma_boss_factory_start(HMODULE gamedll, const DlumaEngineScriptBootstrapInput *cold)
{
    DWORD old_protect;
    BYTE* target;
    if (g_dluma_boss.installed) return !g_dluma_boss.stopping;
    if (g_dluma_boss.ever_published || g_dluma_boss.trampoline || g_dluma_boss.instructions ||
        !dluma_boss_cold_current(cold) || !dluma_boss_factory_restore_protection() ||
        !dluma_boss_bind(gamedll) || !dluma_boss_factory_pin_modules()) return 0;
    target = (BYTE*)gamedll + DLUMA_BOSS_FACTORY_RVA;
    g_dluma_boss.trampoline = (BYTE*)VirtualAlloc(0, DLUMA_BOSS_STOLEN + DLUMA_BOSS_JUMP,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_dluma_boss.trampoline) return 0;
    memcpy(g_dluma_boss.original_bytes, target, DLUMA_BOSS_STOLEN);
    memcpy(g_dluma_boss.trampoline, target, DLUMA_BOSS_STOLEN);
    dluma_boss_jump(g_dluma_boss.trampoline + DLUMA_BOSS_STOLEN,
        (ULONG_PTR)(target + DLUMA_BOSS_STOLEN));
    if (!VirtualProtect(g_dluma_boss.trampoline, DLUMA_BOSS_STOLEN + DLUMA_BOSS_JUMP,
        PAGE_EXECUTE_READ, &old_protect)) goto failed;
    FlushInstructionCache(GetCurrentProcess(), g_dluma_boss.trampoline,
        DLUMA_BOSS_STOLEN + DLUMA_BOSS_JUMP);
    g_dluma_boss.original = (DlumaBossFactory)g_dluma_boss.trampoline;
    g_dluma_boss.target = target;
    dluma_atomic_exchange(&g_dluma_boss.stopping, 0);
    memset(g_dluma_boss.patch, 0x90, sizeof(g_dluma_boss.patch));
    dluma_boss_jump(g_dluma_boss.patch, (ULONG_PTR)dluma_boss_factory_hook);
    if (!VirtualProtect(target, DLUMA_BOSS_STOLEN, PAGE_EXECUTE_READWRITE, &old_protect)) goto failed;
    g_dluma_boss.factory_protection = old_protect;
    dluma_atomic_exchange(&g_dluma_boss.factory_protection_pending, 1);
    if (memcmp(target, g_dluma_boss.original_bytes, DLUMA_BOSS_STOLEN)) {
        (void)dluma_boss_factory_restore_protection();
        goto failed;
    }
    if (!dluma_boss_cold_current(cold)) { (void)dluma_boss_factory_restore_protection(); goto failed; }
    /* Initial entry-code publication requires a separately qualified cold
     * engine stage. Once any byte can be observed, forwarding state is final. */
    dluma_atomic_exchange(&g_dluma_boss.ever_published, 1);
    memcpy(target, g_dluma_boss.patch, DLUMA_BOSS_STOLEN);
    dluma_atomic_exchange(&g_dluma_boss.installed, 1);
    FlushInstructionCache(GetCurrentProcess(), target, DLUMA_BOSS_STOLEN);
    if (!dluma_boss_factory_restore_protection()) goto failed;
    dluma_log_line("INFO", "CallMod boss instruction enabled");
#if DLUMA_BOSS_PERIODIC_IMPLEMENTATION && !defined(DLUMA_BOSS_LIFECYCLE_TEST)
    (void)dluma_boss_periodic_native_start(gamedll);
#endif
    return 1;
failed:
    dluma_atomic_exchange(&g_dluma_boss.stopping, 1);
    if (g_dluma_boss.ever_published) return 0;
    if (g_dluma_boss.callbacks || g_dluma_boss.instructions) return 0;
    if (g_dluma_boss.trampoline && !VirtualFree(g_dluma_boss.trampoline, 0, MEM_RELEASE)) return 0;
    g_dluma_boss.trampoline = 0;
    g_dluma_boss.original = 0;
    return 0;
}

int dluma_boss_actions_start(HMODULE gamedll)
{
    /* Legacy internal callers cannot supply the current lexical cold stage. */
    (void)gamedll; return 0;
}

int dluma_boss_actions_start_cold(HMODULE gamedll, int before_original_initializer)
{
    DlumaPlayerActorColdProfile profile;
    DlumaIsolationScriptEntryObservation entry;
    DlumaEngineScriptBootstrapInput input;
    DlumaBossColdReader reader;
    DWORD timestamp;
    HMODULE engine;
    if (before_original_initializer != 1 || g_dluma_boss.ever_published ||
        !dluma_player_actor_binding_probe_cold_profile((uintptr_t)gamedll, &profile) ||
        profile.gamedll_base != (uintptr_t)gamedll || !dluma_isolation_snapshot_script_entry(&entry)) return 0;
    engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    if (!engine || !dluma_is_readable(engine, 4096U) ||
        !dluma_build_identity_parse_pe_header((const BYTE *)engine, 4096U, &timestamp, &reader.size)) return 0;
    reader.base = (uintptr_t)engine;
    memset(&input, 0, sizeof(input)); input.engine_base = reader.base;
    input.proxy_initializer = (uintptr_t)entry.callback_address; input.actual_return_address = (uintptr_t)entry.return_address;
    input.entry_thread = entry.thread_id; input.current_thread = GetCurrentThreadId();
    /* The shared profile authority above checks the complete exact A18 image;
     * the shared core then checks current caller/thread/publication ordering. */
    input.exact_engine_profile = 1; input.before_original_initializer = 1;
    input.read_memory = dluma_boss_cold_read; input.user = &reader;
    return dluma_boss_factory_start(gamedll, &input);
}

void dluma_boss_actions_on_map_generation_changed(unsigned int map_generation)
{
#if DLUMA_BOSS_PERIODIC_IMPLEMENTATION
    dluma_boss_periodic_enter();
#if !defined(DLUMA_BOSS_LIFECYCLE_TEST) && defined(DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC) && DLUMA_PRIVATE_BOSS_PERIODIC_DIAGNOSTIC
    dluma_atomic_exchange(&g_dluma_boss_periodic_diagnostic.generation, 0);
#endif
    dluma_atomic_exchange(&g_dluma_boss.map_generation, (LONG)map_generation);
    dluma_boss_periodic_retire_owner_locked(0);
    dluma_boss_periodic_leave();
#else
    dluma_atomic_exchange(&g_dluma_boss.map_generation, (LONG)map_generation);
#endif
    if (!dluma_atomic_compare_exchange(&g_dluma_boss.stopping, 0, 0))
        dluma_mod_handler_service_queue_map_changed(map_generation);
}

int dluma_boss_actions_started(void)
{
    return dluma_atomic_compare_exchange(&g_dluma_boss.installed, 0, 0) &&
        !dluma_atomic_compare_exchange(&g_dluma_boss.stopping, 0, 0);
}

int dluma_boss_actions_stop(void)
{
    LONG was_stopping = dluma_atomic_exchange(&g_dluma_boss.stopping, 1);
    if (!was_stopping) dluma_mod_handler_service_queue_map_changed(0U);
#if DLUMA_BOSS_PERIODIC_IMPLEMENTATION
    dluma_boss_periodic_retire_owner(0);
    if (!dluma_boss_receiver_route_stop()) return 0;
#endif
    if (!dluma_boss_factory_restore_protection() || dluma_atomic_compare_exchange(&g_dluma_boss.callbacks, 0, 0)) return 0;
    /* Published code and relay stay installed/pinned in stopped pass-through.
     * Owned instructions may be destroyed by the following stock shutdown. */
    if (g_dluma_boss.ever_published) return 1;
    if (g_dluma_boss.trampoline) {
        if (!VirtualFree(g_dluma_boss.trampoline, 0, MEM_RELEASE)) return 0;
        g_dluma_boss.trampoline = 0;
    }
    g_dluma_boss.original = 0;
    return 1;
}
