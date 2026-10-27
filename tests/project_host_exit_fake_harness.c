/* Compile twice. Exercise the actual host retirement functions while the
 * game authority, API and resource services are hermetic fakes. */
#ifdef DLUMA_TEST_MANAGED_HOST
#include "project_exit_managed.generated.h"
#define REGISTRY g_dluma_managed
#define ENTRY DlumaManagedModEntry
#define EXIT_API dluma_managed_host_revoke_project_for_exit
#define EXIT_EX dluma_managed_host_revoke_project_for_exit_ex
#define GATE g_dluma_managed_project_exit_gate
#define READY DLUMA_MANAGED_PHASE_READY
#define ACTIVE(e) ((e)->snapshot.active)
#define FREEZE dluma_managed_host_freeze_map_exit
#define SUSPEND dluma_managed_host_suspend_global_map
#define BIND dluma_managed_host_bind_frontend_after_exit
#define RESUME dluma_managed_host_resume_frontend_after_exit
#define FROZEN g_dluma_managed_map_exit_frozen
#else
#include "project_exit_native.generated.h"
#define REGISTRY g_dluma_native_mods
#define ENTRY DlumaNativeModEntry
#define EXIT_API dluma_native_mods_revoke_project_for_exit
#define EXIT_EX dluma_native_mods_revoke_project_for_exit_ex
#define GATE g_dluma_native_project_exit_gate
#define READY DLUMA_NATIVE_PHASE_READY
#define ACTIVE(e) ((e)->scope_active)
#define FREEZE dluma_native_mods_freeze_map_exit
#define SUSPEND dluma_native_mods_suspend_global_map
#define BIND dluma_native_mods_bind_frontend_after_exit
#define RESUME dluma_native_mods_resume_frontend_after_exit
#define FROZEN g_dluma_native_map_exit_frozen
#endif
#include <stdio.h>
#include <string.h>

DlumaRuntime g_dluma;
static ENTRY saved_global;
static int authority_available = 1;
static int stop_succeeds = 1;
static unsigned int callback_count;
static unsigned int callback_order[8];
static DlumaApiV1 fake_api;
static int media_drains=1;

void dluma_str_copy(char* to, unsigned int capacity, const char* from)
{
    unsigned int i=0;
    if (!capacity) return;
    while (from && from[i] && i+1U<capacity) { to[i]=from[i]; ++i; }
    to[i]=0;
}
int dluma_engine_current_thread_can_finalize_content_ex(DlumaEngineState* state,
    LONG window_owner, const DlumaNaturalShutdownOwnerProof* natural,
    DlumaContentFinalizationAuthorityEvidence* out)
{
    (void)state;(void)window_owner;(void)natural;
    memset(out,0,sizeof(*out));
    out->selected_proof=DLUMA_CONTENT_FINALIZATION_PROOF_SERIALIZED_FRAME_ROUTE;
    out->current_thread_id=(LONG)GetCurrentThreadId();
    out->serialized_route_token=9;
    return authority_available;
}
const DlumaApiV1* dluma_api_runtime_api(DlumaApiRuntime* runtime)
{ (void)runtime;return stop_succeeds ? &fake_api : 0; }
void dluma_game_host_context_advance_scope(DlumaGameHostContext* context)
{
    unsigned int i;
    for (i=1;i<REGISTRY.count;++i) if (context==&REGISTRY.entries[i].api_host) {
        REGISTRY.entries[i].api_owner.active_handles=0;
        REGISTRY.entries[i].api_owner.active_timers=0;
        REGISTRY.entries[i].api_owner.active_subscriptions=0;
        REGISTRY.entries[i].api_owner.active_companion_artifacts=0;
    }
}
int dluma_game_host_context_suspend_map_for_exit(DlumaGameHostContext* context)
{
    if(context!=&REGISTRY.entries[0].api_host)return 0;
    dluma_mod_owner_begin_revoke(&REGISTRY.entries[0].api_owner);
    return media_drains;
}
void dluma_game_host_context_bind_clean_frontend(DlumaGameHostContext* context){(void)context;}
#ifdef DLUMA_TEST_MANAGED_HOST
static int __cdecl fake_stop(uint64_t handle, uint32_t reason, char* diagnostic, uint32_t capacity)
{
    (void)reason;(void)diagnostic;(void)capacity;
    callback_order[callback_count++]=(unsigned int)handle;
    return stop_succeeds;
}
#else
static void fake_stop(const DlumaApiV1* api, const DlumaScopeActivationV1* activation)
{
    (void)api;
    callback_order[callback_count++]=activation->scope==DLUMA_SCOPE_MAP ? 3U : 2U;
}
#endif
static void reset(void)
{
    unsigned int i;
    memset(&REGISTRY,0,sizeof(REGISTRY));
    GATE=FROZEN=0;callback_count=0;authority_available=1;stop_succeeds=1;media_drains=1;
    REGISTRY.count=3;
    for (i=0;i<3;++i) {
        ENTRY* e=&REGISTRY.entries[i];
        e->package.manifest.scope=i==0 ? DLUMA_SCOPE_GLOBAL : i==1 ? DLUMA_SCOPE_PROJECT : DLUMA_SCOPE_MAP;
        e->api_owner.owner_id=i+1U;e->api_owner.state=DLUMA_MOD_OWNER_ACTIVE;
        e->api_owner.map_generation=7;e->api_owner.game_thread_id=0xeeeeU;
        e->api_owner.active_handles=2;e->api_owner.active_timers=1;
        e->api_owner.active_subscriptions=1;e->api_owner.active_companion_artifacts=1;
        e->lifecycle_phase=READY;ACTIVE(e)=1;
        dluma_str_copy(e->snapshot.id,DLUMA_ARRAY_COUNT(e->snapshot.id),i==0 ? "global" : i==1 ? "project" : "map");
#ifdef DLUMA_TEST_MANAGED_HOST
        e->managed_handle=i+1U;
#else
        e->snapshot.abi_kind=DLUMA_NATIVE_ABI_V2;
        e->callbacks.scope_deactivated=fake_stop;
#endif
    }
#ifdef DLUMA_TEST_MANAGED_HOST
    REGISTRY.runtime.initialized=1;
    REGISTRY.runtime.api.deactivate=fake_stop;
#endif
    REGISTRY.event_count=3;
    for (i=0;i<3;++i) REGISTRY.events[i].owner_id=i+1U;
    saved_global=REGISTRY.entries[0];
}
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n",__LINE__,#x);return 1; } } while(0)
int main(void)
{
    DlumaProjectHostExitDrain proof;
    DlumaProjectExitDrainAuthority authority;
    unsigned int global_count,generation;
    reset();authority_available=0;
    CHECK(!EXIT_API(7,&proof) && !callback_count);
    reset();CHECK(!EXIT_API(8,&proof) && !callback_count);
    reset();REGISTRY.pump_active=1;
    CHECK(!EXIT_API(7,&proof) && !callback_count);
    reset();
#ifdef DLUMA_TEST_MANAGED_HOST
    REGISTRY.entries[1].lifecycle_phase=DLUMA_MANAGED_PHASE_CALLBACK;
#else
    REGISTRY.entries[1].quest_phase_callbacks_in_flight=1;
#endif
    CHECK(!EXIT_API(7,&proof) && !callback_count && proof.active_callbacks==1);
    reset();memset(&authority,0,sizeof(authority));authority.map_generation=7;
    authority.owner.selected_proof=DLUMA_CONTENT_FINALIZATION_PROOF_SERIALIZED_FRAME_ROUTE;
    authority.owner.current_thread_id=(LONG)GetCurrentThreadId();authority.owner.serialized_route_token=8;
    CHECK(!EXIT_EX(7,&authority,&proof) && !callback_count);
    authority.owner.serialized_route_token=9;
    CHECK(EXIT_EX(7,&authority,&proof));
    CHECK(proof.complete && proof.project_owners==2 && proof.global_owners_preserved==1);
    CHECK(callback_count==2 && callback_order[0]==3 && callback_order[1]==2);
    CHECK(REGISTRY.entries[1].api_owner.state==DLUMA_MOD_OWNER_REVOKED);
    CHECK(REGISTRY.entries[1].api_owner.game_thread_id==GetCurrentThreadId());
    CHECK(!ACTIVE(&REGISTRY.entries[1]) && !ACTIVE(&REGISTRY.entries[2]));
    CHECK(memcmp(&saved_global,&REGISTRY.entries[0],sizeof(saved_global))==0);
    CHECK(REGISTRY.event_count==1 && REGISTRY.events[REGISTRY.event_head].owner_id==1);
    CHECK(EXIT_API(7,&proof) && callback_count==2); /* retry cannot invoke stop twice */
    reset();stop_succeeds=0;
    CHECK(!EXIT_API(7,&proof));
    CHECK(!EXIT_API(7,&proof)); /* a failed stop remains a blocker after active was cleared */
    CHECK(strstr(proof.blocker,"previous_deactivation_failed")!=0);
    reset();
    CHECK(FREEZE(7,&authority) && FROZEN);
    CHECK(EXIT_EX(7,&authority,&proof));
    CHECK(SUSPEND(7,&authority,&global_count) && global_count==1);
    CHECK(callback_count==2 && ACTIVE(&REGISTRY.entries[0]));
    CHECK(REGISTRY.entries[0].api_owner.state==DLUMA_MOD_OWNER_REVOKING);
    REGISTRY.event_count=2;REGISTRY.event_head=0;
    REGISTRY.events[0].owner_id=1;REGISTRY.events[0].kind=10;REGISTRY.events[0].map_generation=7;
    REGISTRY.events[1].owner_id=1;REGISTRY.events[1].kind=7;REGISTRY.events[1].map_generation=7;
    CHECK(BIND(7,8,&authority));generation=REGISTRY.entries[0].api_owner.generation;
    CHECK(BIND(7,8,&authority) && REGISTRY.entries[0].api_owner.generation==generation);
    CHECK(REGISTRY.event_count==1 && REGISTRY.events[0].kind==10 && REGISTRY.events[0].map_generation==8);
    CHECK(REGISTRY.entries[0].api_owner.owner_id==saved_global.api_owner.owner_id);
    CHECK(REGISTRY.entries[0].api_owner.active_timers==0); /* bind service is a fake; real recount tested separately */
    CHECK(FROZEN && callback_count==2);
    RESUME();CHECK(!FROZEN && REGISTRY.entries[0].api_owner.state==DLUMA_MOD_OWNER_ACTIVE);
    reset();CHECK(FREEZE(7,&authority));media_drains=0;
    CHECK(!SUSPEND(7,&authority,&global_count) && FROZEN);
    puts("project host retirement: exact authority, migrated owner, active-call rejection, reverse drain, preserved globals, event purge, sticky failure passed");
    return 0;
}
