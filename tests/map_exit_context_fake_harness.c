#include "project_exit_context.generated.h"
#include <stdio.h>
#include <string.h>

static DlumaModOwner owner;
static DlumaGameHostState state;
static DlumaGameHostContext context;
static DlumaGameHostTimer timers[2];
static DlumaGameHostSubscription subscriptions[3];
static DlumaGameHostCompanion companions[1];
static unsigned int media_calls,remove_calls,add_calls,player_clears,hud_clears;
static int media_ready=1,remove_mode=1;
static unsigned int native_generation=8;
int dluma_media_service_revoke_owner(uint64_t id,uint32_t generation)
{(void)id;(void)generation;++media_calls;return media_ready;}
void dluma_player_service_revoke_owner(uint64_t id){(void)id;++player_clears;}
static void dluma_game_host_quest_hud_clear(DlumaGameHostState* s){(void)s;++hud_clears;}
int dluma_native_gameplay_subscription_remove_ex(uint64_t id,uint64_t token,DlumaGameError* error)
{
    (void)id;(void)token;++remove_calls;
    if(remove_mode==1)return 1;
    if(remove_mode==2)memcpy(error->code,"unknown_subscription",sizeof("unknown_subscription"));
    else memcpy(error->code,"wrong_thread",sizeof("wrong_thread"));return 0;
}
int dluma_native_gameplay_subscription_add_ex(uint64_t id,uint32_t gen,uint64_t token,
    uint64_t callback,uint32_t kind,const char* name,DlumaGameError* error)
{(void)id;(void)token;(void)callback;(void)kind;(void)name;(void)error;++add_calls;return gen==native_generation;}
static void reset(void)
{
    memset(&owner,0,sizeof(owner));memset(&state,0,sizeof(state));memset(&context,0,sizeof(context));
    memset(timers,0,sizeof(timers));memset(subscriptions,0,sizeof(subscriptions));memset(companions,0,sizeof(companions));
    owner.owner_id=17;owner.state=DLUMA_MOD_OWNER_ACTIVE;owner.map_generation=7;owner.generation=3;
    owner.active_handles=4;owner.active_timers=2;owner.active_subscriptions=3;owner.active_companion_artifacts=1;
    context.struct_size=sizeof(context);context.struct_version=1;context.state=&state;
    state.magic=DLUMA_GAME_HOST_STATE_MAGIC;state.owner=&owner;
    state.timer_capacity=2;state.timers=timers;timers[0].active=timers[1].active=1;
    timers[0].due_ms=1234;timers[1].due_ms=5678;
    state.subscription_capacity=3;state.subscriptions=subscriptions;
    subscriptions[0].active=1;subscriptions[0].event_kind=DLUMA_GAME_EVENT_UPDATE;
    subscriptions[1].active=1;subscriptions[1].native_registered=1;
    subscriptions[1].native_registration_token=55;subscriptions[1].callback_id=22;
    subscriptions[1].native_kind=DLUMA_GAME_EVENT_SENSOR_ENTER;memcpy(subscriptions[1].name,"test_sensor",sizeof("test_sensor"));
    subscriptions[2].active=1;subscriptions[2].event_kind=DLUMA_GAME_EVENT_CONFIGURATION_CHANGED;
    state.companion_capacity=1;state.companions=companions;companions[0].active=1;companions[0].size=32;
    media_calls=remove_calls=add_calls=player_clears=hud_clears=0;media_ready=remove_mode=1;native_generation=8;
}
#define CHECK(x) do{if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void)
{
    reset();CHECK(dluma_game_host_context_suspend_map_for_exit(&context));
    CHECK(owner.state==DLUMA_MOD_OWNER_REVOKING && !owner.active_handles);
    CHECK(media_calls==1 && remove_calls==1 && player_clears==1 && hud_clears==1);
    CHECK(!subscriptions[1].native_registered && subscriptions[1].native_exit_suspended && subscriptions[1].active);
    CHECK(timers[0].active && timers[0].due_ms==1234 && timers[1].due_ms==5678);
    CHECK(companions[0].active && companions[0].size==32 && !add_calls);
    owner.state=DLUMA_MOD_OWNER_ACTIVE;dluma_mod_owner_advance_scope(&owner,8);owner.state=DLUMA_MOD_OWNER_REVOKING;
    dluma_game_host_context_bind_clean_frontend(&context);
    CHECK(owner.active_timers==2 && owner.active_subscriptions==2 && owner.active_companion_artifacts==1);
    CHECK(!add_calls && dluma_game_host_context_needs_map_rebind(&context));
    owner.state=DLUMA_MOD_OWNER_ACTIVE;dluma_game_host_context_rebind_map(&context);
    CHECK(add_calls==1 && subscriptions[1].native_registered && !subscriptions[1].native_exit_suspended);
    CHECK(owner.active_subscriptions==3 && owner.active_timers==2 && !dluma_game_host_context_needs_map_rebind(&context));
    reset();media_ready=0;CHECK(!dluma_game_host_context_suspend_map_for_exit(&context));
    CHECK(owner.state==DLUMA_MOD_OWNER_REVOKING && !add_calls);
    reset();remove_mode=0;CHECK(!dluma_game_host_context_suspend_map_for_exit(&context));
    CHECK(subscriptions[1].native_registered);
    reset();remove_mode=2;CHECK(dluma_game_host_context_suspend_map_for_exit(&context));
    CHECK(subscriptions[1].native_exit_suspended);
    puts("global map context: checked media/subscription drain, preserved process timers/config/companions, clean frontend binding and future map rebind passed");
    return 0;
}
