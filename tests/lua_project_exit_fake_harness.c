#include "project_exit_lua.generated.h"
#include <stdio.h>
#include <string.h>

DlumaRuntime g_dluma;
typedef struct FakeScript { unsigned int active;int destroy_allowed;unsigned int destroyed; } FakeScript;
static FakeScript scripts[3];
static DlumaLuaRuntimeSlot saved_global;
static int authority_available=1;
static int status_available=1;
static unsigned int user_callbacks;
static unsigned int revoked_contexts;
static unsigned int revoked_on_current_owner;
static unsigned int cleared_quest_owners;
static int simulated_map_alive;
static unsigned int notification_after_destroy;
static unsigned int host_globals=1;
static unsigned int resumed_hosts;
static unsigned int host_stop_order[4],host_stops;
static unsigned long long route_token=21;
static unsigned int quest_generation_calls;

void dluma_str_copy(char* to,unsigned int cap,const char* from)
{ unsigned int i=0;if(!cap)return;while(from&&from[i]&&i+1U<cap){to[i]=from[i];++i;}to[i]=0; }
void dluma_log_pair(const char* a,const char* b,const char* c){(void)a;(void)b;(void)c;}
void dluma_lua_update_global_runtime_status(const char* a,const char* b,int c,const char* d)
{(void)a;(void)b;(void)c;(void)d;++user_callbacks;}
void dluma_quest_hud_clear_owner(uint64_t owner){(void)owner;++cleared_quest_owners;}
static void dluma_lua_runtime_clear_quest_snapshots_for_owner(uint64_t owner){(void)owner;}
static int dluma_lua_runtime_dispatch_slot(DlumaLuaRuntimeSlot* slot,uint32_t kind,const char* name,double value,int state)
{(void)slot;(void)kind;(void)name;(void)value;(void)state;++user_callbacks;if(!simulated_map_alive)++notification_after_destroy;return 1;}
void dluma_game_host_context_revoke(DlumaGameHostContext* context)
{
    unsigned int i;
    ++revoked_contexts;
    for(i=1;i<3;++i)if(context==&g_dluma_lua_runtime.slots[i].api_host &&
        g_dluma_lua_runtime.slots[i].api_owner.game_thread_id==GetCurrentThreadId())++revoked_on_current_owner;
}
int dluma_engine_current_thread_can_finalize_content_ex(DlumaEngineState* state,LONG window,
    const DlumaNaturalShutdownOwnerProof* natural,DlumaContentFinalizationAuthorityEvidence* out)
{
    (void)state;(void)window;(void)natural;memset(out,0,sizeof(*out));
    out->selected_proof=DLUMA_CONTENT_FINALIZATION_PROOF_SERIALIZED_FRAME_ROUTE;
    out->current_thread_id=(LONG)GetCurrentThreadId();out->serialized_route_token=route_token;
    return authority_available;
}
static int fake_status(void* handle,DlumaLua54Status* out)
{if(!status_available)return 0;out->active_callbacks=((FakeScript*)handle)->active;return 1;}
static int fake_destroy(void* handle)
{FakeScript* s=handle;if(!s->destroy_allowed)return 0;++s->destroyed;return 1;}
static int fake_begin_pump(void* handle){return ((FakeScript*)handle)->active==0;}
typedef struct FinishWork { DlumaProjectMapExitKey key;DlumaProjectMapExitReceipt receipt;int succeeded;DWORD thread; } FinishWork;
static DWORD WINAPI finish_on_migrated_worker(void* argument)
{
    FinishWork* work=argument;DlumaProjectExitDrainAuthority proof;
    memset(&proof,0,sizeof(proof));proof.map_generation=work->key.map_generation;
    (void)dluma_engine_current_thread_can_finalize_content_ex(&g_dluma.engine,0,0,&proof.owner);
    work->thread=GetCurrentThreadId();
    work->succeeded=dluma_project_runtime_finish_outgoing_map(&work->key,&proof,&work->receipt);
    return 0;
}
int dluma_game_host_context_suspend_map_for_exit(DlumaGameHostContext* context)
{
    if(context!=&g_dluma_lua_runtime.slots[0].api_host)return 0;
    dluma_mod_owner_begin_revoke(&g_dluma_lua_runtime.slots[0].api_owner);return 1;
}
void dluma_game_host_context_bind_clean_frontend(DlumaGameHostContext* context){(void)context;}
int dluma_managed_host_freeze_map_exit(unsigned int gen,const DlumaProjectExitDrainAuthority* a){(void)gen;(void)a;return 1;}
int dluma_native_mods_freeze_map_exit(unsigned int gen,const DlumaProjectExitDrainAuthority* a){(void)gen;(void)a;return 1;}
int dluma_managed_host_revoke_project_for_exit_ex(unsigned int gen,const DlumaProjectExitDrainAuthority* a,DlumaProjectHostExitDrain* out)
{(void)gen;(void)a;memset(out,0,sizeof(*out));out->complete=1;host_stop_order[host_stops++]=2;return 1;}
int dluma_native_mods_revoke_project_for_exit_ex(unsigned int gen,const DlumaProjectExitDrainAuthority* a,DlumaProjectHostExitDrain* out)
{(void)gen;(void)a;memset(out,0,sizeof(*out));out->complete=1;host_stop_order[host_stops++]=3;return 1;}
int dluma_managed_host_suspend_global_map(unsigned int gen,const DlumaProjectExitDrainAuthority* a,unsigned int* count)
{(void)gen;(void)a;*count=host_globals;return 1;}
int dluma_native_mods_suspend_global_map(unsigned int gen,const DlumaProjectExitDrainAuthority* a,unsigned int* count)
{(void)gen;(void)a;*count=host_globals;return 1;}
int dluma_managed_host_bind_frontend_after_exit(unsigned int a,unsigned int b,const DlumaProjectExitDrainAuthority* c)
{(void)a;(void)b;(void)c;return 1;}
int dluma_native_mods_bind_frontend_after_exit(unsigned int a,unsigned int b,const DlumaProjectExitDrainAuthority* c)
{(void)a;(void)b;(void)c;return 1;}
void dluma_managed_host_resume_frontend_after_exit(void){++resumed_hosts;}
void dluma_native_mods_resume_frontend_after_exit(void){++resumed_hosts;}
void dluma_native_gameplay_on_map_generation_changed(uint32_t generation,const char* name,void* level)
{(void)generation;(void)name;(void)level;}
int dluma_quest_phase_player_started(void){return 1;}
int dluma_quest_phase_player_on_map_generation_changed(const WCHAR* root,const char* map,unsigned int generation)
{(void)root;(void)map;++quest_generation_calls;return generation!=0;}
static void reset(void)
{
    unsigned int i;
    memset(&g_dluma_lua_runtime,0,sizeof(g_dluma_lua_runtime));memset(scripts,0,sizeof(scripts));
    g_dluma_lua_project_exit_gate=0;authority_available=status_available=1;
    g_dluma_map_exit_active=g_dluma_map_exit_lock=0;memset(&g_dluma_map_exit_receipt,0,sizeof(g_dluma_map_exit_receipt));
    simulated_map_alive=1;notification_after_destroy=0;host_globals=1;resumed_hosts=host_stops=0;route_token=21;
    user_callbacks=revoked_contexts=revoked_on_current_owner=cleared_quest_owners=0;
    quest_generation_calls=0;
    g_dluma_lua_runtime.map_generation=7;g_dluma_lua_runtime.api.get_status=fake_status;
    g_dluma_lua_runtime.api.destroy_script=fake_destroy;
    g_dluma_lua_runtime.api.begin_pump=fake_begin_pump;
    for(i=0;i<3;++i){
        DlumaLuaRuntimeSlot* slot=&g_dluma_lua_runtime.slots[i];slot->occupied=1;
        slot->metadata.scope=i==0?DLUMA_LUA_SCOPE_GLOBAL:i==1?DLUMA_LUA_SCOPE_PROJECT:DLUMA_LUA_SCOPE_MAP;
        slot->owner_id=i+1U;slot->api_owner.owner_id=i+1U;slot->api_owner.state=DLUMA_MOD_OWNER_ACTIVE;
        slot->api_owner.map_generation=7;slot->api_owner.game_thread_id=0xeeeeU;
        slot->helper_script=&scripts[i];scripts[i].destroy_allowed=1;
    }
    g_dluma_lua_runtime.event_count=1;g_dluma_lua_runtime.events[0].owner_count=3;
    for(i=0;i<3;++i)g_dluma_lua_runtime.events[0].owner_ids[i]=i+1U;
    saved_global=g_dluma_lua_runtime.slots[0];
}
#define CHECK(x) do {if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    DlumaLuaProjectExitDrain proof;
    DlumaProjectMapExitKey key;
    DlumaProjectMapExitReceipt receipt;
    DlumaProjectExitDrainAuthority current;
    FinishWork work;
    HANDLE worker;
    reset();authority_available=0;CHECK(!dluma_lua_runtime_revoke_local_scopes_for_exit(7,&proof));
    CHECK(!revoked_contexts);
    reset();CHECK(!dluma_lua_runtime_revoke_local_scopes_for_exit(8,&proof));
    reset();g_dluma_lua_runtime.pump_active=1;CHECK(!dluma_lua_runtime_revoke_local_scopes_for_exit(7,&proof));
    reset();scripts[1].active=1;CHECK(!dluma_lua_runtime_revoke_local_scopes_for_exit(7,&proof));
    CHECK(proof.active_callbacks==1&&!revoked_contexts&&g_dluma_lua_project_exit_gate);
    reset();status_available=0;CHECK(!dluma_lua_runtime_revoke_local_scopes_for_exit(7,&proof));
    CHECK(!revoked_contexts);
    reset();g_dluma_lua_runtime.event_lock=1;CHECK(!dluma_lua_runtime_revoke_local_scopes_for_exit(7,&proof));
    CHECK(!revoked_contexts);
    reset();CHECK(dluma_lua_runtime_revoke_local_scopes_for_exit(7,&proof));
    CHECK(proof.complete&&proof.local_slots==2&&!proof.remaining_slots);
    CHECK(revoked_contexts==2&&revoked_on_current_owner==2&&cleared_quest_owners==2&&!user_callbacks);
    CHECK(memcmp(&saved_global,&g_dluma_lua_runtime.slots[0],sizeof(saved_global))==0);
    CHECK(g_dluma_lua_runtime.events[0].owner_count==1&&g_dluma_lua_runtime.events[0].owner_ids[0]==1);
    CHECK(dluma_lua_runtime_revoke_local_scopes_for_exit(7,&proof)&&revoked_contexts==2);
    reset();scripts[1].destroy_allowed=0;
    CHECK(!dluma_lua_runtime_revoke_local_scopes_for_exit(7,&proof));
    CHECK(proof.remaining_slots==1&&g_dluma_lua_runtime.slots[1].teardown_pending);
    scripts[1].destroy_allowed=1;CHECK(dluma_lua_runtime_revoke_local_scopes_for_exit(7,&proof));
    CHECK(!user_callbacks&&revoked_contexts==2);
    reset();dluma_str_copy(g_dlw_lua_runtime.current_map,sizeof(g_dlw_lua_runtime.current_map),"test_map");
    memset(&key,0,sizeof(key));key.map_generation=7;key.project_epoch=3;
    key.owner_thread_id=GetCurrentThreadId();key.serialized_route_token=21;
    key.outgoing_session=(const void*)1;key.outgoing_level=(const void*)2;
    CHECK(dluma_project_runtime_begin_outgoing_map(&key,&receipt));
    CHECK(receipt.begin_complete && receipt.lua_notifications==3 && user_callbacks==5);
    CHECK(host_stops==2 && host_stop_order[0]==2 && host_stop_order[1]==3);
    CHECK(dluma_project_runtime_begin_outgoing_map(&key,&receipt) && user_callbacks==5);
    CHECK(!scripts[0].destroyed && g_dluma_map_exit_active);
    CHECK(!dluma_project_runtime_resume_frontend(&key));
    simulated_map_alive=0;route_token=22;
    memset(&current,0,sizeof(current));current.map_generation=7;
    CHECK(dluma_engine_current_thread_can_finalize_content_ex(&g_dluma.engine,0,0,&current.owner));
    memset(&work,0,sizeof(work));work.key=key;
    worker=CreateThread(0,0,finish_on_migrated_worker,&work,0,0);
    CHECK(worker && WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
    CHECK(work.succeeded && work.thread!=key.owner_thread_id);
    receipt=work.receipt;
    CHECK(g_dluma_lua_runtime.slots[0].api_owner.game_thread_id==work.thread);
    CHECK(receipt.context_rebound && receipt.new_map_generation==8 && user_callbacks==5 && !notification_after_destroy);
    CHECK(quest_generation_calls==1);
    CHECK(!g_dlw_lua_runtime.current_map[0] && g_dluma_lua_runtime.map_generation==8);
    CHECK(g_dluma_lua_runtime.slots[0].occupied && g_dluma_lua_runtime.slots[0].api_owner.state==DLUMA_MOD_OWNER_REVOKING);
    current.map_generation=8;
    CHECK(dluma_project_runtime_finish_outgoing_map(&key,&current,&receipt) && user_callbacks==5);
    CHECK(dluma_project_runtime_resume_frontend(&key) && resumed_hosts==2);
    CHECK(!g_dluma_map_exit_active && g_dluma_lua_runtime.slots[0].api_owner.state==DLUMA_MOD_OWNER_ACTIVE);
    reset();memset(g_dluma_lua_runtime.slots,0,sizeof(g_dluma_lua_runtime.slots));
    g_dluma_lua_runtime.map_generation=0;host_globals=0;key.map_generation=0;key.outgoing_level=0;
    CHECK(dluma_project_runtime_begin_outgoing_map(&key,&receipt) && !receipt.lua_notifications && !user_callbacks);
    current.map_generation=0;current.owner.serialized_route_token=21;
    CHECK(dluma_project_runtime_finish_outgoing_map(&key,&current,&receipt) && receipt.new_map_generation==0);
    CHECK(!quest_generation_calls);
    CHECK(!user_callbacks);
    /* A fresh current route cannot substitute a different Begin identity. */
    reset();memset(g_dluma_lua_runtime.slots,0,sizeof(g_dluma_lua_runtime.slots));
    g_dluma_lua_runtime.map_generation=0;host_globals=0;key.map_generation=0;key.outgoing_level=0;
    CHECK(dluma_project_runtime_begin_outgoing_map(&key,&receipt));
    key.serialized_route_token=99;
    CHECK(!dluma_project_runtime_finish_outgoing_map(&key,&current,&receipt));
    CHECK(!receipt.context_rebound && !user_callbacks);
    key.serialized_route_token=21;
    /* Idempotent success still requires current owner proof. */
    reset();memset(g_dluma_lua_runtime.slots,0,sizeof(g_dluma_lua_runtime.slots));
    g_dluma_lua_runtime.map_generation=0;host_globals=0;
    CHECK(dluma_project_runtime_begin_outgoing_map(&key,&receipt));
    CHECK(dluma_project_runtime_finish_outgoing_map(&key,&current,&receipt));
    current.owner.serialized_route_token=99;
    CHECK(!dluma_project_runtime_finish_outgoing_map(&key,&current,&receipt));
    CHECK(!dluma_project_runtime_resume_frontend(&key));
    current.owner.serialized_route_token=21;
    reset();key.map_generation=7;key.outgoing_level=0;
    CHECK(!dluma_project_runtime_begin_outgoing_map(&key,&receipt)); /* live local state with no admitted map */
    CHECK(!dluma_project_runtime_resume_frontend(&key) && g_dluma_map_exit_active);
    puts("Lua project retirement: owner/generation proof, migration, callback/queue blockers, deferred retry, preserved globals and no user unload execution passed");
    return 0;
}
