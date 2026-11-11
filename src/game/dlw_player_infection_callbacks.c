#include "../dlw_common.h"
#include "../bindings/dlw_player_actor_binding.h"
#include "dlw_player_infection_callbacks.h"

enum { CB_ACTIVE, CB_INACTIVE, CB_INIT, CB_UNREGISTER, CB_DTOR, CB_COUNT };
typedef void (__fastcall *CbUpdate)(void*);
typedef uintptr_t (__fastcall *CbInit)(void*, void*);
typedef uintptr_t (__fastcall *CbUnregister)(void*);
typedef void* (__fastcall *CbDtor)(void*, unsigned char);
typedef void (__fastcall *CbRespawn)(void*);
static void __fastcall cb_active(void*);
static void __fastcall cb_inactive(void*);
static uintptr_t __fastcall cb_init(void*, void*);
static uintptr_t __fastcall cb_unregister(void*);
static void* __fastcall cb_dtor(void*, unsigned char);
/* Win32 SYSTEM_INFO layout, matching the public SDK declaration. Kept local
 * because this no-CRT translation unit uses the project's Win32 surface. */
typedef struct {
    DWORD architecture, page_size;
    void* minimum; void* maximum; uintptr_t affinity;
    DWORD processors, processor_type, granularity;
    WORD level, revision;
} CbSystemInfo;
__declspec(dllimport) void WINAPI GetSystemInfo(CbSystemInfo*);
typedef struct { uintptr_t base; DWORD original; unsigned used, pending; } CbPage;
#define CB_LIFECYCLE_CAPACITY 32u
#define CB_RESPAWN_CAPACITY 32u
typedef struct {
    uint64_t controller_key,owner_key,init_ticket,generation;
    uint32_t actor_id;
    unsigned occupied,retired,init_pending,initialized,has_actor_id,respawn_refs;
} CbLifecycle;
typedef struct { uint64_t ticket,owner_key; uint32_t rows; unsigned active; } CbRespawnEvent;

/* Process-retained data/code. Actor/controller keys are opaque integer equality
 * metadata only: never cast a stored key to a pointer or dereference it.
 * All review gates deliberately stay zero; no runtime setter exists. */
static struct {
    volatile LONG reviewed, identity_assignment_qualified, bootstrap_qualified;
    volatile LONG respawn_route_qualified;
    volatile LONG stopping, active, lock, identity_poisoned;
    unsigned installed, retained, pinned;
    DlumaPlayerActorColdProfile profile;
    PVOID original[CB_COUNT];
    /* Never populated by production code: no qualified retained Player entry
     * detour/trampoline facility exists here. No bespoke relocation is used. */
    PVOID respawn_original;
    PVOID volatile* cells[CB_COUNT];
    unsigned owned[CB_COUNT];
    uint64_t target_sequence,native_sequence,published_native_generation,local_generation,frame_sequence;
    uint32_t map_generation;
    CbLifecycle lifecycle[CB_LIFECYCLE_CAPACITY];
    CbRespawnEvent respawns[CB_RESPAWN_CAPACITY];
    DWORD page_size;
    CbPage pages[CB_COUNT];
    DlumaInfectionQueue queue;
} g_infection_cb;
static const uint32_t cb_cells[CB_COUNT] = {0x1809928,0x1809930,0x18098B0,0x18098B8,0x18098A8};
static const uint32_t cb_targets[CB_COUNT] = {0xCB86D0,0x428080,0xCEF940,0xCEF9B0,0xD05C10};
static const unsigned char cb_prefix[CB_COUNT][32] = {
    {0x40,0x55,0x57,0x48,0x8D,0x6C,0x24,0xB1,0x48,0x81,0xEC,0xB8,0,0,0,0x48,0x8B,0x01,0x48,0x8B,0xF9,0xFF,0x50,0x28,0x84,0xC0,0x0F,0x84,0xF4,0x02,0,0},
    {0xC2,0,0},
    {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x01,0x48,0x8B,0xD9,0x48,0x89,0x51,0x08,0xFF,0x50,0x20,0x48,0x85,0xC0,0x74,0x14,0x48,0x8B,0x03,0x48,0x8B,0xCB,0xFF,0x50},
    {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x01,0x48,0x8B,0xD9,0xFF,0x50,0x20,0x48,0x63,0x53,0x14,0x48,0x8B,0xCB,0x48,0x8B,0x80,0xB8,0x0B,0,0,0xC6,0x44,0x10},
    {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8D,0x05,0x6B,0x99,0xAD,0,0x48,0x8B,0xD9,0x48,0x89,0x01,0xF6,0xC2,0x01,0x74,0x06,0xFF,0x15,0x9A,0x65,0x6C,0,0x48,0x8B}
};
static int cb_try_lock(void) { return dluma_atomic_compare_exchange(&g_infection_cb.lock,1,0)==0; }
static void cb_unlock(void) { dluma_atomic_exchange(&g_infection_cb.lock,0); }
static int cb_gate(void)
{
    return g_infection_cb.reviewed && g_infection_cb.identity_assignment_qualified &&
        g_infection_cb.respawn_route_qualified && g_infection_cb.respawn_original &&
        g_infection_cb.bootstrap_qualified && !g_infection_cb.stopping &&
        !g_infection_cb.identity_poisoned;
}
/* All lifecycle helpers require the bounded metadata lock. No native pointer
 * enters this ledger and no helper resolves/dereferences a stored key. */
static void cb_drop_target(void)
{
    if(g_infection_cb.queue.initialized) (void)dluma_infection_queue_bind_target(&g_infection_cb.queue,0);
    g_infection_cb.local_generation=g_infection_cb.published_native_generation=0;
}
static void cb_poison(void)
{ dluma_atomic_exchange(&g_infection_cb.identity_poisoned,1); cb_drop_target(); }
static int cb_published_controller(uint64_t controller)
{
    unsigned i;
    if(!g_infection_cb.published_native_generation) return 0;
    for(i=0;i<CB_LIFECYCLE_CAPACITY;++i) {
        const CbLifecycle* row=&g_infection_cb.lifecycle[i];
        if(row->occupied && !row->retired && row->controller_key==controller &&
            row->generation==g_infection_cb.published_native_generation) return 1;
    }
    return 0;
}
static CbLifecycle* cb_lifecycle_find(uint64_t controller,uint64_t owner)
{
    unsigned i;
    for(i=0;i<CB_LIFECYCLE_CAPACITY;++i) {
        CbLifecycle* row=&g_infection_cb.lifecycle[i];
        if(row->occupied && !row->retired && row->controller_key==controller && row->owner_key==owner) return row;
    }
    return 0;
}
static void cb_lifecycle_retire(uint64_t controller)
{
    unsigned i;
    for(i=0;i<CB_LIFECYCLE_CAPACITY;++i) {
        CbLifecycle* row=&g_infection_cb.lifecycle[i];
        if(row->occupied && row->controller_key==controller) {
            if(row->generation==g_infection_cb.published_native_generation) cb_drop_target();
            row->retired=1; row->initialized=0;
            /* Pending Init/respawn tickets retain this row until settled. */
        }
    }
}
static uint64_t cb_lifecycle_begin(uint64_t controller,uint64_t owner)
{
    unsigned i; int slot=-1; CbLifecycle* row;
    if(!controller || !owner || g_infection_cb.identity_poisoned) return 0;
    for(i=0;i<CB_LIFECYCLE_CAPACITY;++i) {
        row=&g_infection_cb.lifecycle[i];
        if(!row->occupied || ((row->retired || row->controller_key==controller) && !row->init_pending && !row->respawn_refs)) {
            slot=(int)i; break;
        }
    }
    if(slot<0 || g_infection_cb.native_sequence==UINT64_MAX) { cb_poison(); return 0; }
    cb_lifecycle_retire(controller);
    row=&g_infection_cb.lifecycle[slot]; memset(row,0,sizeof(*row));
    row->occupied=row->init_pending=1;
    row->controller_key=controller; row->owner_key=owner;
    row->generation=row->init_ticket=++g_infection_cb.native_sequence;
    for(i=0;i<CB_RESPAWN_CAPACITY;++i) {
        CbRespawnEvent* event=&g_infection_cb.respawns[i];
        if(event->active && event->owner_key==owner) {
            event->rows|=1u<<(unsigned)slot; ++row->respawn_refs;
        }
    }
    return row->init_ticket;
}
static int cb_lifecycle_finish(uint64_t ticket,int succeeded)
{
    unsigned i;
    for(i=0;i<CB_LIFECYCLE_CAPACITY;++i) {
        CbLifecycle* row=&g_infection_cb.lifecycle[i];
        if(row->occupied && row->init_ticket==ticket && row->init_pending) {
            row->init_pending=0;
            if(!succeeded) {
                if(row->generation==g_infection_cb.published_native_generation) cb_drop_target();
                row->retired=1;
            }
            row->initialized=!row->retired; return 1;
        }
    }
    return 0;
}
static int cb_respawn_matches(const CbLifecycle* row,uint64_t owner,int local,uint32_t actor_id)
{
    return row->occupied && !row->retired && row->owner_key==owner &&
        (!row->has_actor_id || (local && row->actor_id==actor_id));
}
static uint64_t cb_respawn_begin(uint64_t owner,int local,uint32_t actor_id)
{
    unsigned i,needed=1; int slot=-1; CbRespawnEvent* event;
    if(!owner || g_infection_cb.identity_poisoned) return 0;
    for(i=0;i<CB_LIFECYCLE_CAPACITY;++i) {
        const CbLifecycle* row=&g_infection_cb.lifecycle[i];
        if(row->occupied && !row->retired && row->owner_key==owner &&
            g_infection_cb.published_native_generation &&
            row->generation==g_infection_cb.published_native_generation &&
            (!row->has_actor_id || !local || row->actor_id!=actor_id)) {
            /* A matching published owner cannot survive an uncertain identity
             * observation and later replay its old admitted requests. */
            cb_poison(); return 0;
        }
    }
    for(i=0;i<CB_RESPAWN_CAPACITY;++i) if(!g_infection_cb.respawns[i].active) { slot=(int)i; break; }
    for(i=0;i<CB_LIFECYCLE_CAPACITY;++i)
        if(cb_respawn_matches(&g_infection_cb.lifecycle[i],owner,local,actor_id)) ++needed;
    if(slot<0 || UINT64_MAX-g_infection_cb.native_sequence<needed) { cb_poison(); return 0; }
    event=&g_infection_cb.respawns[slot]; memset(event,0,sizeof(*event));
    event->active=1; event->owner_key=owner; event->ticket=++g_infection_cb.native_sequence;
    for(i=0;i<CB_LIFECYCLE_CAPACITY;++i) {
        CbLifecycle* row=&g_infection_cb.lifecycle[i];
        if(cb_respawn_matches(row,owner,local,actor_id)) {
            if(row->generation==g_infection_cb.published_native_generation) cb_drop_target();
            row->generation=++g_infection_cb.native_sequence;
            ++row->respawn_refs; event->rows|=1u<<i;
        }
    }
    return event->ticket;
}
static int cb_respawn_finish(uint64_t ticket,int succeeded)
{
    unsigned i,j;
    for(i=0;i<CB_RESPAWN_CAPACITY;++i) {
        CbRespawnEvent* event=&g_infection_cb.respawns[i];
        if(!event->active || event->ticket!=ticket) continue;
        for(j=0;j<CB_LIFECYCLE_CAPACITY;++j) if((event->rows&(1u<<j)) &&
            !g_infection_cb.lifecycle[j].respawn_refs) { cb_poison(); return 0; }
        for(j=0;j<CB_LIFECYCLE_CAPACITY;++j) if(event->rows&(1u<<j)) {
            CbLifecycle* row=&g_infection_cb.lifecycle[j];
            --row->respawn_refs;
            if(!succeeded) {
                if(row->generation==g_infection_cb.published_native_generation) cb_drop_target();
                row->retired=1; row->initialized=0;
            }
        }
        event->active=0; return 1;
    }
    return 0;
}
static PVOID cb_wrapper(unsigned i)
{
    switch (i) {
    case CB_ACTIVE:return (PVOID)cb_active;
    case CB_INACTIVE:return (PVOID)cb_inactive;
    case CB_INIT:return (PVOID)cb_init;
    case CB_UNREGISTER:return (PVOID)cb_unregister;
    default:return (PVOID)cb_dtor;
    }
}
/* Preserve foreign routing. Original pointers and wrapper state are never
 * cleared, even after successful restore: a native caller may have fetched a
 * wrapper target before the CAS. Both modules are pinned before publication. */
static CbPage* cb_page(PVOID volatile* cell)
{
    CbSystemInfo info; MEMORY_BASIC_INFORMATION memory; uintptr_t base;
    unsigned i;
    if (!g_infection_cb.page_size) {
        GetSystemInfo(&info);
        if (!info.page_size || (info.page_size&(info.page_size-1))) return 0;
        g_infection_cb.page_size=info.page_size;
    }
    base=(uintptr_t)cell&~((uintptr_t)g_infection_cb.page_size-1);
    if ((uintptr_t)cell-base>g_infection_cb.page_size-sizeof(*cell)) return 0;
    for (i=0;i<CB_COUNT;++i)
        if (g_infection_cb.pages[i].used && g_infection_cb.pages[i].base==base)
            return &g_infection_cb.pages[i];
    if (!VirtualQuery((void*)base,&memory,sizeof(memory)) || memory.State!=MEM_COMMIT ||
        (memory.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return 0;
    for (i=0;i<CB_COUNT;++i) if (!g_infection_cb.pages[i].used) {
        CbPage* page=&g_infection_cb.pages[i];
        page->base=base; page->original=memory.Protect; page->used=1;
        return page;
    }
    return 0;
}
static int cb_restore_page(CbPage* page)
{
    DWORD old; MEMORY_BASIC_INFORMATION memory;
    if (!page->pending) return 1;
    if (!VirtualProtect((void*)page->base,g_infection_cb.page_size,page->original,&old) ||
        !VirtualQuery((void*)page->base,&memory,sizeof(memory)) ||
        memory.State!=MEM_COMMIT || memory.Protect!=page->original) {
        g_infection_cb.retained=1; return 0;
    }
    page->pending=0; return 1;
}
static int cb_change_cell(PVOID volatile* cell, PVOID expected, PVOID replacement, unsigned* changed)
{
    DWORD old; PVOID previous; CbPage* page;
    if (changed) *changed=0;
    if (!cell || !(page=cb_page(cell)) ||
        !VirtualProtect((void*)cell,sizeof(*cell),PAGE_READWRITE,&old)) return 0;
    page->pending=1;
    previous=dluma_atomic_pointer_compare_exchange(cell,replacement,expected);
    if (changed) *changed=previous==expected;
    if (!cb_restore_page(page)) return 0;
    return previous==expected;
}
static int cb_signature(unsigned i)
{
    uintptr_t base=g_infection_cb.profile.gamedll_base;
    uint32_t size=g_infection_cb.profile.gamedll_size;
    unsigned n=i==CB_INACTIVE?3u:32u;
    if (cb_targets[i]>=size || n>size-cb_targets[i] || cb_cells[i]>=size ||
        sizeof(void*)>size-cb_cells[i]) return 0;
    return dluma_is_readable((void*)(base+cb_targets[i]),n) &&
        !memcmp((void*)(base+cb_targets[i]),cb_prefix[i],n) &&
        dluma_is_readable((void*)(base+cb_cells[i]),sizeof(void*));
}
/* Receiver-owned reads only. GetID's exact A18 body is data=IGSObject+0x20,
 * ID=data+0x50; PlayerDI's IGSObject subobject is+40. No game getter is called.
 * Identity assignment/local replacement ordering is NOT qualified yet; the
 * closed review gate prevents this draft from publishing a live identity. */
static int cb_primary_actor_id(BYTE* owner,uint32_t* out)
{
    BYTE* data; int valid=0;
    if (!out || !cb_gate() || !owner) return 0;
    __try {
        if (*(uintptr_t*)owner!=g_infection_cb.profile.gamedll_base+0x17DD738 || !owner[80]) __leave;
        data=*(BYTE**)(owner+40+0x20);
        if (!data) __leave;
        *out=*(uint32_t*)(data+0x50); valid=(int32_t)*out>=0;
    } __except(1) { valid=0; }
    return valid;
}
static int cb_actor_owner(void* receiver,BYTE** out)
{
    BYTE* self=(BYTE*)receiver;
    int valid=0;
    if (!out || !cb_gate() || !self) return 0;
    __try {
        if (*(uintptr_t*)self!=g_infection_cb.profile.gamedll_base+0x18098A8 ||
            *(uint32_t*)(self+20)!=35) __leave;
        *out=*(BYTE**)(self+8); valid=*out!=0;
    } __except(1) { valid=0; }
    return valid;
}
static void cb_invalidate(void* receiver)
{
    if (!receiver || !cb_gate()) return;
    if (!cb_try_lock()) { dluma_atomic_exchange(&g_infection_cb.identity_poisoned,1); return; }
    cb_lifecycle_retire((uint64_t)(uintptr_t)receiver);
    cb_unlock();
}
static void cb_receive(void* receiver)
{
    uint32_t actor_id; BYTE* owner=0; CbLifecycle* row; DlumaInfectionQueuedRequest work={0};
    DlumaInfectionQueueCallback ctx={0}; DlumaInfectionQueueOutcome outcome=DLUMA_IQ_NOT_APPLIED;
    if(!cb_gate()) return;
    if(!cb_actor_owner(receiver,&owner)) {
        if(cb_try_lock()) {
            if(cb_published_controller((uint64_t)(uintptr_t)receiver)) cb_poison();
            cb_unlock();
        } else dluma_atomic_exchange(&g_infection_cb.identity_poisoned,1);
        return;
    }
    if(!cb_try_lock()) return;
    row=cb_lifecycle_find((uint64_t)(uintptr_t)receiver,(uint64_t)(uintptr_t)owner);
    if (!cb_gate() || !row || !row->initialized || row->init_pending || row->respawn_refs ||
        !g_infection_cb.map_generation) {
        if(g_infection_cb.identity_poisoned || cb_published_controller((uint64_t)(uintptr_t)receiver)) cb_poison();
        cb_unlock(); return;
    }
    if(!cb_primary_actor_id(owner,&actor_id) || (row->has_actor_id && actor_id!=row->actor_id)) {
        if(row->generation==g_infection_cb.published_native_generation) cb_poison();
        cb_unlock(); return;
    }
    if(g_infection_cb.published_native_generation &&
        g_infection_cb.published_native_generation!=row->generation) {
        /* Two simultaneously local live incarnations are ambiguous. */
        cb_poison(); cb_unlock(); return;
    }
    /* A map bind never reuses the old target. Publish only on a fresh qualified
     * native receiver observation matching the copied live init incarnation.
     * Assignment/replacement ordering remains closed behind its review gate. */
    if (!g_infection_cb.local_generation) {
        if (g_infection_cb.target_sequence==UINT64_MAX ||
            dluma_infection_queue_bind_target(&g_infection_cb.queue,++g_infection_cb.target_sequence)!=DLUMA_IQ_OK) {
            cb_poison(); cb_unlock(); return;
        }
        g_infection_cb.local_generation=g_infection_cb.target_sequence;
        g_infection_cb.published_native_generation=row->generation;
        row->actor_id=actor_id; row->has_actor_id=1;
    }
    ctx.map_generation=g_infection_cb.map_generation;
    ctx.frame_sequence=g_infection_cb.frame_sequence;
    ctx.native_borrow_qualified=1; ctx.local_owner_ready=1;
    ctx.target_generation=g_infection_cb.local_generation;
    if (dluma_infection_queue_take(&g_infection_cb.queue,&ctx,GetTickCount64(),&work)==DLUMA_IQ_OK) {
        /* Qualified, known primitive work only while the caller supplies this
         * live receiver. No global GameDI lookup or other game/native call. */
        __try {
            volatile uint32_t* counter=(volatile uint32_t*)((BYTE*)receiver+44);
            uint32_t before=*counter;
            *counter=before+1u;
            outcome=(uint32_t)(*counter-before)==1u?DLUMA_IQ_ACKNOWLEDGED:DLUMA_IQ_UNCERTAIN;
        } __except(1) { outcome=DLUMA_IQ_UNCERTAIN; }
        if (!cb_gate()) outcome=DLUMA_IQ_UNCERTAIN;
        (void)dluma_infection_queue_complete(&g_infection_cb.queue,&work,outcome);
    }
    cb_unlock();
}
static void cb_forward_update(void* receiver,unsigned i)
{
    CbUpdate original=(CbUpdate)g_infection_cb.original[i];
    dluma_atomic_increment(&g_infection_cb.active);
    __try { cb_receive(receiver); if (original) original(receiver); }
    __finally { dluma_atomic_decrement(&g_infection_cb.active); }
    /* Never dereference receiver after forwarding. */
}
static void __fastcall cb_active(void* receiver) { cb_forward_update(receiver,CB_ACTIVE); }
static void __fastcall cb_inactive(void* receiver) { cb_forward_update(receiver,CB_INACTIVE); }
static uintptr_t __fastcall cb_init(void* receiver,void* owner)
{
    CbInit original=(CbInit)g_infection_cb.original[CB_INIT];
    uint64_t ticket=0; int settled=0; uintptr_t result=0;
    dluma_atomic_increment(&g_infection_cb.active);
    __try {
        if(cb_gate()) {
            if(cb_try_lock()) {
                ticket=cb_lifecycle_begin((uint64_t)(uintptr_t)receiver,(uint64_t)(uintptr_t)owner);
                cb_unlock();
            } else dluma_atomic_exchange(&g_infection_cb.identity_poisoned,1);
        }
        if (original) result=original(receiver,owner);
        /* Completion uses the copied ticket only. ActorID/local flag may be
         * assigned later; no native receiver or owner read occurs here. */
        if(ticket && cb_try_lock()) {
            settled=cb_lifecycle_finish(ticket,original!=0);
            if(!settled) cb_poison();
            cb_unlock();
        } else if(ticket) dluma_atomic_exchange(&g_infection_cb.identity_poisoned,1);
    } __finally {
        if(ticket && !settled) {
            if(cb_try_lock()) { (void)cb_lifecycle_finish(ticket,0); cb_unlock(); }
            else dluma_atomic_exchange(&g_infection_cb.identity_poisoned,1);
        }
        dluma_atomic_decrement(&g_infection_cb.active);
    }
    return result;
}
static uintptr_t __fastcall cb_unregister(void* receiver)
{
    CbUnregister original=(CbUnregister)g_infection_cb.original[CB_UNREGISTER]; uintptr_t result=0;
    dluma_atomic_increment(&g_infection_cb.active);
    __try { cb_invalidate(receiver); if (original) result=original(receiver); }
    __finally { dluma_atomic_decrement(&g_infection_cb.active); }
    return result;
}
static void* __fastcall cb_dtor(void* receiver,unsigned char flags)
{
    CbDtor original=(CbDtor)g_infection_cb.original[CB_DTOR]; void* result=receiver;
    dluma_atomic_increment(&g_infection_cb.active);
    __try { cb_invalidate(receiver); if (original) result=original(receiver,flags); }
    __finally { dluma_atomic_decrement(&g_infection_cb.active); }
    return result;
}
/* Exact CBC7 B53E60 consumes primary PlayerDI in RCX, with two observed direct
 * call routes and no callable vtable cell. Borrow only before the original.
 * The 17-byte prologue is NOT relocated here: this wrapper stays uninstalled. */
void dluma_player_infection_callbacks_respawn_draft(void* receiver)
{
    CbRespawn original=(CbRespawn)g_infection_cb.respawn_original;
    uint32_t actor_id=0; uint64_t ticket=0; int local=0,settled=0;
    dluma_atomic_increment(&g_infection_cb.active);
    __try {
        if (cb_gate()) {
            local=cb_primary_actor_id((BYTE*)receiver,&actor_id);
            if (cb_try_lock()) {
                ticket=cb_respawn_begin((uint64_t)(uintptr_t)receiver,local,actor_id);
                cb_unlock();
            } else dluma_atomic_exchange(&g_infection_cb.identity_poisoned,1);
        }
        if (original) original(receiver);
        if(ticket && cb_try_lock()) {
            settled=cb_respawn_finish(ticket,original!=0);
            if(!settled) cb_poison();
            cb_unlock();
        } else if(ticket) dluma_atomic_exchange(&g_infection_cb.identity_poisoned,1);
    } __finally {
        if(ticket && !settled) {
            if(cb_try_lock()) { (void)cb_respawn_finish(ticket,0); cb_unlock(); }
            else dluma_atomic_exchange(&g_infection_cb.identity_poisoned,1);
        }
        dluma_atomic_decrement(&g_infection_cb.active);
    }
    /* No receiver, owner or data read after forwarding. */
}
int dluma_player_infection_callbacks_prepare_cold(uintptr_t module)
{
    unsigned i; HMODULE self_pin=0,game_pin=0;
    if (!cb_gate() || g_infection_cb.installed || g_infection_cb.retained) return 0;
    if (!dluma_player_actor_binding_probe_cold_profile(module,&g_infection_cb.profile)) return 0;
    for (i=0;i<CB_COUNT;++i) {
        if (!cb_signature(i)) return 0;
        g_infection_cb.cells[i]=(PVOID volatile*)(module+cb_cells[i]);
        g_infection_cb.original[i]=(PVOID)(module+cb_targets[i]);
        if (*g_infection_cb.cells[i]!=g_infection_cb.original[i]) return 0;
    }
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (LPCWSTR)(const void*)cb_active,&self_pin) || self_pin!=g_dluma.self_module ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (LPCWSTR)(module+cb_targets[0]),&game_pin) || (uintptr_t)game_pin!=module) return 0;
    g_infection_cb.pinned=1;
    if (dluma_infection_queue_initialize(&g_infection_cb.queue,1,0)!=DLUMA_IQ_OK) return 0;
    for (i=0;i<CB_COUNT;++i) {
        if (!cb_change_cell(g_infection_cb.cells[i],g_infection_cb.original[i],cb_wrapper(i),&g_infection_cb.owned[i])) {
            g_infection_cb.retained=1; (void)dluma_player_infection_callbacks_stop(); return 0;
        }
    }
    g_infection_cb.installed=1; return 1;
}
int dluma_player_infection_callbacks_stop(void)
{
    unsigned i; int restored=1,pending=0;
    dluma_atomic_exchange(&g_infection_cb.stopping,1);
    if (cb_try_lock()) {
        cb_drop_target();
        if (g_infection_cb.queue.initialized) (void)dluma_infection_queue_bind_map(&g_infection_cb.queue,0);
        for(i=0;i<CB_LIFECYCLE_CAPACITY;++i) {
            CbLifecycle* row=&g_infection_cb.lifecycle[i];
            if(row->occupied) {
                row->retired=1; row->initialized=0;
                if(row->init_pending || row->respawn_refs) pending=1;
            }
        }
        for(i=0;i<CB_RESPAWN_CAPACITY;++i) if(g_infection_cb.respawns[i].active) pending=1;
        cb_unlock();
    }
    else { dluma_atomic_exchange(&g_infection_cb.identity_poisoned,1); restored=0; }
    for (i=0;i<CB_COUNT;++i) if (g_infection_cb.owned[i]) {
        unsigned changed=0;
        if (*g_infection_cb.cells[i]==g_infection_cb.original[i]) g_infection_cb.owned[i]=0;
        else {
            (void)cb_change_cell(g_infection_cb.cells[i],cb_wrapper(i),g_infection_cb.original[i],&changed);
            if (changed) g_infection_cb.owned[i]=0;
            else restored=0;
        }
    }
    for (i=0;i<CB_COUNT;++i) if (g_infection_cb.pages[i].used &&
        !cb_restore_page(&g_infection_cb.pages[i])) restored=0;
    if (!restored || pending || g_infection_cb.active) g_infection_cb.retained=1;
    /* No state, forwarder or original module is freed on either outcome. */
    return restored && !pending && !g_infection_cb.active;
}
int dluma_player_infection_callbacks_available(void) { return cb_gate() && g_infection_cb.installed; }
void dluma_player_infection_callbacks_publish_frame(uint32_t map,uint64_t frame)
{
    if (!g_infection_cb.queue.initialized) return;
    if (!cb_try_lock()) {
        /* A dropped map/frame publication cannot leave old-map work usable. */
        dluma_atomic_exchange(&g_infection_cb.identity_poisoned,1); return;
    }
    if(g_infection_cb.identity_poisoned) cb_drop_target();
    if (map!=g_infection_cb.map_generation) cb_drop_target();
    (void)dluma_infection_queue_bind_map(&g_infection_cb.queue,map);
    g_infection_cb.map_generation=map; g_infection_cb.frame_sequence=frame; cb_unlock();
}
void dluma_player_infection_callbacks_retire_owner(const DlumaInfectionQueueOwner* owner)
{
    if (!g_infection_cb.queue.initialized) return;
    if (!cb_try_lock()) { dluma_atomic_exchange(&g_infection_cb.identity_poisoned,1); return; }
    (void)dluma_infection_queue_retire_owner(&g_infection_cb.queue,owner); cb_unlock();
}
DlumaInfectionQueueResult dluma_player_infection_callbacks_request(const DlumaInfectionQueueOwner* owner,uint32_t map,uint32_t timeout,uint64_t* token)
{
    DlumaInfectionQueueResult result;
    if (token) *token=0;
    if (!dluma_player_infection_callbacks_available()) return DLUMA_IQ_UNAVAILABLE;
    if (!cb_try_lock()) return DLUMA_IQ_BUSY;
    /* Availability can change between the optimistic check and lock entry. */
    if(!cb_gate()) { cb_drop_target(); cb_unlock(); return DLUMA_IQ_UNAVAILABLE; }
    result=dluma_infection_queue_request(&g_infection_cb.queue,owner,map,GetTickCount64(),timeout,token);
    cb_unlock(); return result;
}
DlumaInfectionQueueResult dluma_player_infection_callbacks_status(const DlumaInfectionQueueOwner* owner,uint64_t token,DlumaInfectionQueueSnapshot* out)
{
    DlumaInfectionQueueResult result;
    if (!g_infection_cb.queue.initialized) return DLUMA_IQ_UNAVAILABLE;
    if (!cb_try_lock()) return DLUMA_IQ_BUSY;
    if(g_infection_cb.identity_poisoned) cb_drop_target();
    result=dluma_infection_queue_status(&g_infection_cb.queue,owner,token,GetTickCount64(),out); cb_unlock(); return result;
}
DlumaInfectionQueueResult dluma_player_infection_callbacks_cancel(const DlumaInfectionQueueOwner* owner,uint64_t token)
{
    DlumaInfectionQueueResult result;
    if (!g_infection_cb.queue.initialized) return DLUMA_IQ_UNAVAILABLE;
    if (!cb_try_lock()) return DLUMA_IQ_BUSY;
    if(g_infection_cb.identity_poisoned) cb_drop_target();
    result=dluma_infection_queue_cancel(&g_infection_cb.queue,owner,token,GetTickCount64()); cb_unlock(); return result;
}
DlumaInfectionQueueResult dluma_player_infection_callbacks_release(const DlumaInfectionQueueOwner* owner,uint64_t token)
{
    DlumaInfectionQueueResult result;
    if (!g_infection_cb.queue.initialized) return DLUMA_IQ_UNAVAILABLE;
    if (!cb_try_lock()) return DLUMA_IQ_BUSY;
    if(g_infection_cb.identity_poisoned) cb_drop_target();
    result=dluma_infection_queue_release(&g_infection_cb.queue,owner,token); cb_unlock(); return result;
}
