#include "../src/dlw_common.h"
#include <stdio.h>
#include <string.h>
static void (*before_metadata_lock)(void);
static LONG fake_compare_exchange(volatile LONG* target,LONG value,LONG comparand)
{
    if(before_metadata_lock) { void (*hook)(void)=before_metadata_lock; before_metadata_lock=0; hook(); }
    return _InterlockedCompareExchange(target,value,comparand);
}
#undef dluma_atomic_compare_exchange
#define dluma_atomic_compare_exchange fake_compare_exchange
static unsigned protects, fail_protect, fail_remaining, probes, forwards, invalidated_before_forward;
static unsigned init_mode,premature_publication;
static DWORD protection=PAGE_READONLY;
static BOOL WINAPI fake_protect(LPVOID p, SIZE_T size, DWORD mode, DWORD* old)
{ (void)p; (void)size; *old=protection; ++protects; if (protects==fail_protect || (fail_remaining && protects>=fail_protect)) return 0; protection=mode; return 1; }
SIZE_T WINAPI fake_query(LPCVOID p,MEMORY_BASIC_INFORMATION* m,SIZE_T size)
{ (void)size; memset(m,0,sizeof(*m)); m->BaseAddress=(PVOID)((uintptr_t)p & ~(uintptr_t)4095); m->RegionSize=4096; m->State=MEM_COMMIT; m->Protect=protection; return sizeof(*m); }
#define VirtualProtect fake_protect
#define VirtualQuery fake_query
#include "../src/game/dlw_player_infection_callbacks.c"
#include "../src/core/dlw_infection_request_queue.c"
DlumaRuntime g_dluma;
int dluma_is_readable(const void* p,SIZE_T size) { return p && size; }
int dluma_player_actor_binding_probe_cold_profile(uintptr_t m,DlumaPlayerActorColdProfile* p)
{ (void)m; (void)p; ++probes; return 0; }
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static void __fastcall forward(void* receiver) { (void)receiver; ++forwards; }
static uintptr_t __fastcall unregister_forward(void* receiver)
{ (void)receiver; ++forwards; invalidated_before_forward=g_infection_cb.queue.target_generation==0; return 19; }
static uintptr_t __fastcall init_forward(void* receiver,void* owner)
{
    *(void**)((BYTE*)receiver+8)=owner; ++forwards;
    if(init_mode==1) { cb_inactive(receiver); premature_publication=g_infection_cb.queue.target_generation!=0; }
    if(init_mode==2) (void)cb_unregister(receiver);
    return 23;
}
static void __fastcall respawn_forward(void* receiver)
{
    ++forwards;
    invalidated_before_forward=!g_infection_cb.queue.target_generation;
    /* A post-forward receiver read would now see an invalid identity. */
    memset(receiver,0,128);
}
__declspec(align(4096)) static PVOID page_cells[2];
static int protection_retry(void)
{
    unsigned first=0,second=0;
    memset(&g_infection_cb,0,sizeof(g_infection_cb));
    protection=PAGE_READONLY; protects=0; fail_protect=2; fail_remaining=1;
    page_cells[0]=page_cells[1]=(PVOID)forward;
    CHECK(!cb_change_cell(&page_cells[0],(PVOID)forward,(PVOID)cb_active,&first) && first);
    CHECK(protection==PAGE_READWRITE);
    fail_remaining=0; fail_protect=0;
    CHECK(cb_change_cell(&page_cells[1],(PVOID)forward,(PVOID)cb_inactive,&second) && second);
    CHECK(protection==PAGE_READONLY);
    CHECK(g_infection_cb.pages[0].used && !g_infection_cb.pages[1].used);
    g_infection_cb.cells[0]=&page_cells[0]; g_infection_cb.original[0]=(PVOID)forward; g_infection_cb.owned[0]=1;
    g_infection_cb.cells[1]=&page_cells[1]; g_infection_cb.original[1]=(PVOID)forward; g_infection_cb.owned[1]=1;
    protects=0; fail_protect=2; fail_remaining=1;
    CHECK(!dluma_player_infection_callbacks_stop());
    CHECK(g_infection_cb.pages[0].pending && protection==PAGE_READWRITE);
    /* Foreign routing survives cleanup while original protection is repaired. */
    page_cells[1]=(PVOID)cb_active; fail_remaining=0; fail_protect=0;
    CHECK(!dluma_player_infection_callbacks_stop());
    CHECK(page_cells[1]==(PVOID)cb_active && protection==PAGE_READONLY);
    CHECK(!g_infection_cb.pages[0].pending);
    page_cells[1]=(PVOID)forward;
    CHECK(dluma_player_infection_callbacks_stop());
    return 0;
}
static int lifecycle_ledger_cases(void)
{
    uint64_t first,second,outer,inner; unsigned i; CbLifecycle* row;
    memset(&g_infection_cb,0,sizeof(g_infection_cb));
    first=cb_lifecycle_begin(11,101);
    CHECK(first && (row=cb_lifecycle_find(11,101))!=0 && row->init_pending);
    cb_lifecycle_retire(11);
    second=cb_lifecycle_begin(11,101);
    CHECK(second>first && cb_lifecycle_find(11,101)->init_ticket==second);
    CHECK(cb_lifecycle_finish(first,1));
    CHECK(!cb_lifecycle_finish(first,1));
    CHECK(cb_lifecycle_find(11,101)->init_pending);
    CHECK(cb_lifecycle_finish(second,1));
    CHECK(cb_lifecycle_find(11,101)->initialized);
    outer=cb_respawn_begin(101,0,0);
    CHECK(outer && cb_lifecycle_find(11,101)->respawn_refs==1);
    inner=cb_respawn_begin(101,0,0);
    CHECK(inner>outer && cb_lifecycle_find(11,101)->respawn_refs==2);
    CHECK(cb_respawn_finish(inner,1));
    CHECK(cb_lifecycle_find(11,101)->respawn_refs==1);
    CHECK(!cb_respawn_finish(inner,1));
    second=cb_lifecycle_begin(22,101);
    CHECK(second && cb_lifecycle_finish(second,1));
    CHECK(cb_lifecycle_find(22,101)->respawn_refs==1);
    cb_lifecycle_retire(11);
    CHECK(cb_respawn_finish(outer,1));
    CHECK(!cb_lifecycle_find(11,101));
    CHECK(cb_lifecycle_find(22,101)->respawn_refs==0);
    /* Interrupted init cannot become observable, including stale completion. */
    first=cb_lifecycle_begin(33,303);
    CHECK(first && cb_lifecycle_finish(first,0));
    CHECK(!cb_lifecycle_find(33,303) && !cb_lifecycle_finish(first,1));
    /* Retired rows cannot be reused while their init tickets are in flight. */
    memset(&g_infection_cb,0,sizeof(g_infection_cb));
    for(i=0;i<CB_LIFECYCLE_CAPACITY;++i) {
        CHECK(cb_lifecycle_begin(100+i,200+i)); cb_lifecycle_retire(100+i);
    }
    CHECK(!cb_lifecycle_begin(999,999) && g_infection_cb.identity_poisoned);
    memset(&g_infection_cb,0,sizeof(g_infection_cb));
    first=cb_lifecycle_begin(1,2); CHECK(first && cb_lifecycle_finish(first,1));
    g_infection_cb.native_sequence=UINT64_MAX;
    CHECK(!cb_lifecycle_begin(3,4) && g_infection_cb.identity_poisoned);
    CHECK(cb_lifecycle_find(1,2)->init_ticket==first);
    memset(&g_infection_cb,0,sizeof(g_infection_cb));
    first=cb_lifecycle_begin(1,2); CHECK(first && cb_lifecycle_finish(first,1));
    g_infection_cb.native_sequence=UINT64_MAX;
    CHECK(!cb_respawn_begin(2,0,0) && g_infection_cb.identity_poisoned);
    CHECK(cb_lifecycle_find(1,2)->respawn_refs==0);
    /* A retired controller retains its slot while respawn completion is due. */
    memset(&g_infection_cb,0,sizeof(g_infection_cb));
    for(i=0;i<CB_LIFECYCLE_CAPACITY;++i) {
        first=cb_lifecycle_begin(100+i,200+i); CHECK(first && cb_lifecycle_finish(first,1));
    }
    outer=cb_respawn_begin(200,0,0); CHECK(outer);
    cb_lifecycle_retire(100);
    CHECK(!cb_lifecycle_begin(999,999));
    CHECK(cb_respawn_finish(outer,1));
    CHECK(!cb_lifecycle_begin(999,999)); /* exhaustion remains fail-closed */
    /* Stop must retain unfinished tickets and completion must not reanimate. */
    memset(&g_infection_cb,0,sizeof(g_infection_cb));
    first=cb_lifecycle_begin(1,2); CHECK(first);
    CHECK(!dluma_player_infection_callbacks_stop() && g_infection_cb.retained);
    CHECK(!cb_lifecycle_find(1,2));
    CHECK(cb_lifecycle_finish(first,1) && !cb_lifecycle_find(1,2));
    CHECK(dluma_player_infection_callbacks_stop());
    /* Bounded nested-event capacity also closes permanently on exhaustion. */
    memset(&g_infection_cb,0,sizeof(g_infection_cb));
    for(i=0;i<CB_RESPAWN_CAPACITY;++i) CHECK(cb_respawn_begin(123,0,0));
    CHECK(!cb_respawn_begin(123,0,0) && g_infection_cb.identity_poisoned);
    return 0;
}
static int late_identity_cases(void)
{
    union { uint64_t align; BYTE bytes[128]; } controller={0},actor={0},data={0},remote={0},remote_actor={0};
    DlumaInfectionQueueOwner owner={1,1}; uint64_t token,generation;
    memset(&g_infection_cb,0,sizeof(g_infection_cb));
    g_infection_cb.reviewed=g_infection_cb.identity_assignment_qualified=g_infection_cb.bootstrap_qualified=1;
    g_infection_cb.respawn_route_qualified=1; g_infection_cb.respawn_original=(PVOID)respawn_forward;
    g_infection_cb.installed=1; g_infection_cb.original[CB_INIT]=(PVOID)init_forward;
    g_infection_cb.original[CB_INACTIVE]=(PVOID)forward;
    CHECK(dluma_infection_queue_initialize(&g_infection_cb.queue,3,0)==DLUMA_IQ_OK);
    *(uintptr_t*)controller.bytes=*(uintptr_t*)remote.bytes=0x18098A8;
    *(uint32_t*)(controller.bytes+20)=*(uint32_t*)(remote.bytes+20)=35;
    /* Init must not require local flag, ActorID data or primary actor vtable. */
    CHECK(cb_init(controller.bytes,actor.bytes)==23);
    CHECK(cb_lifecycle_find((uint64_t)(uintptr_t)controller.bytes,(uint64_t)(uintptr_t)actor.bytes)->initialized);
    CHECK(!g_infection_cb.queue.target_generation);
    CHECK(cb_init(remote.bytes,remote_actor.bytes)==23);
    dluma_player_infection_callbacks_publish_frame(8,1);
    *(uintptr_t*)actor.bytes=0x17DD738; actor.bytes[80]=1;
    *(BYTE**)(actor.bytes+72)=data.bytes; *(uint32_t*)(data.bytes+0x50)=7;
    cb_inactive(controller.bytes);
    generation=g_infection_cb.queue.target_generation; CHECK(generation);
    CHECK(dluma_player_infection_callbacks_request(&owner,8,1000,&token)==DLUMA_IQ_OK);
    CHECK(cb_init(remote.bytes,remote_actor.bytes)==23);
    CHECK(g_infection_cb.queue.target_generation==generation);
    cb_inactive(remote.bytes);
    CHECK(g_infection_cb.queue.target_generation==generation && !*(uint32_t*)(remote.bytes+44));
    g_infection_cb.frame_sequence=2; cb_inactive(controller.bytes);
    CHECK(*(uint32_t*)(controller.bytes+44)==1);
    init_mode=1; premature_publication=0;
    CHECK(cb_init(controller.bytes,actor.bytes)==23 && !premature_publication);
    init_mode=0; cb_inactive(controller.bytes);
    CHECK(g_infection_cb.queue.target_generation);
    g_infection_cb.original[CB_UNREGISTER]=(PVOID)unregister_forward;
    init_mode=2; CHECK(cb_init(controller.bytes,actor.bytes)==23);
    init_mode=0; cb_inactive(controller.bytes);
    CHECK(!g_infection_cb.queue.target_generation);
    return 0;
}
static void poison_before_request_lock(void)
{
    g_infection_cb.lock=1;
    dluma_player_infection_callbacks_publish_frame(9,2);
    g_infection_cb.lock=0;
}
static int published_identity_failures(void)
{
    unsigned scenario,failures=0;
    for(scenario=0;scenario<10;++scenario) {
        union { uint64_t align; BYTE bytes[128]; } controller={0},actor={0},data={0};
        DlumaInfectionQueueOwner owner={1,1}; DlumaInfectionQueueSnapshot snapshot={0};
        uint64_t token=0,retry=0;
        memset(&g_infection_cb,0,sizeof(g_infection_cb));
        g_infection_cb.reviewed=g_infection_cb.identity_assignment_qualified=g_infection_cb.bootstrap_qualified=1;
        g_infection_cb.respawn_route_qualified=1; g_infection_cb.respawn_original=(PVOID)respawn_forward;
        g_infection_cb.installed=1; g_infection_cb.original[CB_INIT]=(PVOID)init_forward;
        g_infection_cb.original[CB_INACTIVE]=(PVOID)forward;
        CHECK(dluma_infection_queue_initialize(&g_infection_cb.queue,20+scenario,0)==DLUMA_IQ_OK);
        *(uintptr_t*)controller.bytes=0x18098A8; *(uint32_t*)(controller.bytes+20)=35;
        *(uintptr_t*)actor.bytes=0x17DD738; actor.bytes[80]=1;
        *(BYTE**)(actor.bytes+72)=data.bytes; *(uint32_t*)(data.bytes+0x50)=7;
        CHECK(cb_init(controller.bytes,actor.bytes)==23);
        dluma_player_infection_callbacks_publish_frame(8,1); cb_inactive(controller.bytes);
        CHECK(dluma_player_infection_callbacks_request(&owner,8,1000,&token)==DLUMA_IQ_OK);
        if(scenario==0) { *(BYTE**)(actor.bytes+72)=0; dluma_player_infection_callbacks_respawn_draft(actor.bytes); }
        if(scenario==1) { *(uint32_t*)(data.bytes+0x50)=8; cb_inactive(controller.bytes); }
        if(scenario==2) { actor.bytes[80]=0; cb_inactive(controller.bytes); }
        if(scenario==3) { *(BYTE**)(controller.bytes+8)=0; cb_inactive(controller.bytes); }
        if(scenario==4) {
            g_infection_cb.lock=1;
            dluma_player_infection_callbacks_publish_frame(9,2);
            cb_inactive(controller.bytes);
            CHECK(!*(uint32_t*)(controller.bytes+44));
            g_infection_cb.lock=0;
        }
        if(scenario==5) { *(uint32_t*)(data.bytes+0x50)=8; dluma_player_infection_callbacks_respawn_draft(actor.bytes); }
        if(scenario==6) { actor.bytes[80]=0; dluma_player_infection_callbacks_respawn_draft(actor.bytes); }
        if(scenario==7) { *(uintptr_t*)actor.bytes=0; cb_inactive(controller.bytes); }
        if(scenario==8) { *(uintptr_t*)controller.bytes=0; cb_inactive(controller.bytes); }
        if(scenario==9) {
            before_metadata_lock=poison_before_request_lock;
            CHECK(dluma_player_infection_callbacks_request(&owner,8,1000,&retry)==DLUMA_IQ_UNAVAILABLE && retry==0);
        }
        if(!g_infection_cb.identity_poisoned) { fprintf(stderr,"scenario %u retained admission\n",scenario); ++failures; }
        CHECK(dluma_player_infection_callbacks_status(&owner,token,&snapshot)==DLUMA_IQ_OK);
        if(snapshot.state!=DLUMA_IQ_CANCELLED) { fprintf(stderr,"scenario %u retained old queued request\n",scenario); ++failures; }
        /* Recovering the old fields must never replay an old admitted token. */
        *(uintptr_t*)controller.bytes=0x18098A8; *(BYTE**)(controller.bytes+8)=actor.bytes;
        *(uintptr_t*)actor.bytes=0x17DD738; actor.bytes[80]=1;
        *(BYTE**)(actor.bytes+72)=data.bytes; *(uint32_t*)(data.bytes+0x50)=7;
        g_infection_cb.frame_sequence=3; cb_inactive(controller.bytes);
        if(*(uint32_t*)(controller.bytes+44)) { fprintf(stderr,"scenario %u replayed old request\n",scenario); ++failures; }
        if(dluma_player_infection_callbacks_request(&owner,8,1000,&retry)!=DLUMA_IQ_UNAVAILABLE) { fprintf(stderr,"scenario %u admitted another request\n",scenario); ++failures; }
    }
    CHECK(!failures);
    return 0;
}
int main(void)
{
    union { uint64_t align; BYTE bytes[64]; } controller={0};
    union { uint64_t align; BYTE bytes[128]; } actor={0}, data={0},remote_actor={0},remote_data={0};
    DlumaInfectionQueueOwner owner={1,2}; DlumaInfectionQueueSnapshot snapshot={0};
    PVOID cell=(PVOID)forward; unsigned changed=0, issues=0; uint64_t token=99;
    CHECK(!dluma_player_infection_callbacks_prepare_cold(1));
    CHECK(probes==0 && protects==0);
    CHECK(dluma_player_infection_callbacks_request(&owner,1,100,&token)==DLUMA_IQ_UNAVAILABLE && !token);
    g_infection_cb.original[CB_ACTIVE]=(PVOID)forward;
    g_infection_cb.original[CB_INACTIVE]=(PVOID)forward;
    cb_active(controller.bytes); cb_inactive(controller.bytes);
    CHECK(forwards==2 && !g_infection_cb.active);
    CHECK(!cb_change_cell(&cell,(PVOID)cb_active,(PVOID)cb_inactive,&changed));
    CHECK(!changed && cell==(PVOID)forward);
    protects=0; fail_protect=2;
    CHECK(!cb_change_cell(&cell,(PVOID)forward,(PVOID)cb_active,&changed));
    CHECK(changed && cell==(PVOID)cb_active && g_infection_cb.retained);
    fail_protect=0;
    CHECK(cb_change_cell(&cell,(PVOID)cb_active,(PVOID)forward,&changed));
    CHECK(changed && cell==(PVOID)forward);
    if (protection!=PAGE_READONLY) { fputs("failed restore retry lost original page protection\n",stderr); ++issues; }
    g_infection_cb.reviewed=g_infection_cb.identity_assignment_qualified=g_infection_cb.bootstrap_qualified=1;
    g_infection_cb.respawn_route_qualified=1; g_infection_cb.respawn_original=(PVOID)respawn_forward;
    g_infection_cb.installed=1;
    g_infection_cb.original[CB_INIT]=(PVOID)init_forward;
    g_infection_cb.map_generation=1; g_infection_cb.frame_sequence=1;
    *(uintptr_t*)controller.bytes=0x18098A8;
    *(BYTE**)(controller.bytes+8)=actor.bytes;
    *(uint32_t*)(controller.bytes+20)=35;
    actor.bytes[80]=1; *(BYTE**)(actor.bytes+72)=data.bytes;
    *(uintptr_t*)actor.bytes=0x17DD738;
    *(uint32_t*)(data.bytes+0x50)=7;
    CHECK(dluma_infection_queue_initialize(&g_infection_cb.queue,1,1)==DLUMA_IQ_OK);
    CHECK(cb_init(controller.bytes,actor.bytes)==23);
    cb_inactive(controller.bytes);
    CHECK(g_infection_cb.queue.target_generation);
    CHECK(dluma_player_infection_callbacks_request(&owner,1,1000,&token)==DLUMA_IQ_OK);
    *(uint32_t*)(controller.bytes+44)=UINT32_MAX;
    cb_inactive(controller.bytes);
    CHECK(*(uint32_t*)(controller.bytes+44)==0);
    CHECK(dluma_player_infection_callbacks_status(&owner,token,&snapshot)==DLUMA_IQ_OK);
    CHECK(snapshot.state==DLUMA_IQ_APPLIED && snapshot.outcome==DLUMA_IQ_ACKNOWLEDGED);
    cb_active(controller.bytes); CHECK(*(uint32_t*)(controller.bytes+44)==0);
    g_infection_cb.frame_sequence=2;
    CHECK(dluma_player_infection_callbacks_request(&owner,1,1000,&token)==DLUMA_IQ_OK);
    CHECK(dluma_player_infection_callbacks_cancel(&owner,token)==DLUMA_IQ_OK);
    cb_active(controller.bytes); CHECK(*(uint32_t*)(controller.bytes+44)==0);
    g_infection_cb.original[CB_UNREGISTER]=(PVOID)unregister_forward;
    CHECK(cb_unregister(controller.bytes)==19);
    CHECK(invalidated_before_forward && !g_infection_cb.local_generation && !g_infection_cb.active);
    memset(&g_infection_cb,0,sizeof(g_infection_cb));
    g_infection_cb.reviewed=g_infection_cb.identity_assignment_qualified=g_infection_cb.bootstrap_qualified=1;
    g_infection_cb.respawn_route_qualified=1; g_infection_cb.respawn_original=(PVOID)respawn_forward;
    g_infection_cb.installed=1; g_infection_cb.original[CB_INIT]=(PVOID)init_forward;
    g_infection_cb.original[CB_INACTIVE]=(PVOID)forward;
    CHECK(dluma_infection_queue_initialize(&g_infection_cb.queue,2,0)==DLUMA_IQ_OK);
    CHECK(cb_init(controller.bytes,actor.bytes)==23);
    CHECK(!g_infection_cb.queue.target_generation);
    dluma_player_infection_callbacks_publish_frame(2,1);
    CHECK(dluma_player_infection_callbacks_request(&owner,2,1000,&token)==DLUMA_IQ_UNAVAILABLE);
    cb_inactive(controller.bytes);
    CHECK(g_infection_cb.queue.target_generation!=0);
    CHECK(dluma_player_infection_callbacks_request(&owner,2,1000,&token)==DLUMA_IQ_OK);
    {
        uint64_t prior=g_infection_cb.queue.target_generation;
        dluma_player_infection_callbacks_publish_frame(3,2);
        CHECK(!g_infection_cb.queue.target_generation);
        CHECK(dluma_player_infection_callbacks_status(&owner,token,&snapshot)==DLUMA_IQ_OK && snapshot.state==DLUMA_IQ_CANCELLED);
        CHECK(dluma_player_infection_callbacks_request(&owner,3,1000,&token)==DLUMA_IQ_UNAVAILABLE);
        cb_inactive(controller.bytes);
        CHECK(g_infection_cb.queue.target_generation>prior);
        CHECK(dluma_player_infection_callbacks_request(&owner,3,1000,&token)==DLUMA_IQ_OK);
        prior=g_infection_cb.queue.target_generation;
        {
            unsigned calls=forwards;
            *(uintptr_t*)remote_actor.bytes=0x17DD738;
            *(BYTE**)(remote_actor.bytes+72)=remote_data.bytes;
            dluma_player_infection_callbacks_respawn_draft(remote_actor.bytes);
            CHECK(forwards==calls+1 && g_infection_cb.queue.target_generation==prior);
            *(uintptr_t*)remote_actor.bytes=0x17DD738;
            remote_actor.bytes[80]=1; *(BYTE**)(remote_actor.bytes+72)=remote_data.bytes;
            *(uint32_t*)(remote_data.bytes+0x50)=8;
            dluma_player_infection_callbacks_respawn_draft(remote_actor.bytes);
            CHECK(forwards==calls+2 && g_infection_cb.queue.target_generation==prior);
            *(uintptr_t*)actor.bytes=0x17DD738;
            actor.bytes[80]=1; *(BYTE**)(actor.bytes+72)=data.bytes;
            *(uint32_t*)(data.bytes+0x50)=7;
        }
        invalidated_before_forward=0;
        dluma_player_infection_callbacks_respawn_draft(actor.bytes);
        CHECK(invalidated_before_forward && !g_infection_cb.queue.target_generation);
        CHECK(!g_infection_cb.active);
        CHECK(dluma_player_infection_callbacks_status(&owner,token,&snapshot)==DLUMA_IQ_OK && snapshot.state==DLUMA_IQ_CANCELLED);
        *(uintptr_t*)actor.bytes=0x17DD738;
        actor.bytes[80]=1; *(BYTE**)(actor.bytes+72)=data.bytes;
        cb_inactive(controller.bytes);
        CHECK(g_infection_cb.queue.target_generation>prior);
        {
            DlumaInfectionQueuedRequest work={0};
            DlumaInfectionQueueCallback context={3,3,1,1,0};
            context.target_generation=g_infection_cb.queue.target_generation;
            CHECK(dluma_player_infection_callbacks_request(&owner,3,1000,&token)==DLUMA_IQ_OK);
            CHECK(dluma_infection_queue_take(&g_infection_cb.queue,&context,GetTickCount64(),&work)==DLUMA_IQ_OK);
            dluma_player_infection_callbacks_respawn_draft(actor.bytes);
            CHECK(dluma_infection_queue_complete(&g_infection_cb.queue,&work,DLUMA_IQ_ACKNOWLEDGED)==DLUMA_IQ_OK);
            CHECK(dluma_player_infection_callbacks_status(&owner,token,&snapshot)==DLUMA_IQ_OK);
            CHECK(snapshot.state==DLUMA_IQ_FAILED && snapshot.outcome==DLUMA_IQ_UNCERTAIN && snapshot.may_have_applied);
        }
    }
    CHECK(!issues);
    CHECK(protection_retry()==0);
    CHECK(lifecycle_ledger_cases()==0);
    CHECK(late_identity_cases()==0);
    CHECK(published_identity_failures()==0);
    puts("PASS: disabled infection callback and borrowed receiver"); return 0;
}
