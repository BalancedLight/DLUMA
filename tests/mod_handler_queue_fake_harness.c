#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/game/dlw_mod_handler_service.c"

DlumaRuntime g_dluma;
static LONG dispatch_thread;
static unsigned delivered[3], fallback_calls, retired_logs;
static uint64_t fallback_token = 71;
static char delivered_json[128];
static DlumaModOwner owners[2];
static DlumaModPackageSnapshot packages[2];

LONG dluma_engine_game_frame_current_dispatch_thread_id(const DlumaEngineState *s)
{ (void)s; return dispatch_thread; }
int dluma_engine_game_frame_dispatch_active_on_current_thread(const DlumaEngineState *s)
{ (void)s; return dispatch_thread == (LONG)GetCurrentThreadId(); }
DlumaGameFrameAuthorityStatus dluma_engine_game_frame_authority_status(DlumaEngineState *s, ULONGLONG now)
{ (void)s; (void)now; return DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE; }
int dluma_strcmp(const char *a, const char *b) { return strcmp(a, b); }
void dluma_log_pair(const char *l, const char *key, const char *value)
{ (void)l; (void)value; if (!strcmp(key, "queued_mod_handler_retired")) ++retired_logs; }
void dluma_log_u64(const char *l, const char *key, ULONGLONG value)
{ (void)l; (void)key; (void)value; }
DlumaStatus dluma_capability_registry_set(DlumaCapabilityRegistry *r, uint32_t id,
    int configured, int ready, DlumaStringView status)
{ (void)r; (void)id; (void)configured; (void)ready; (void)status; return DLUMA_STATUS_OK; }
int dluma_quest_phase_json_canonicalize_object(const void *p, unsigned int n,
    char *out, unsigned int cap, unsigned int *size, DlumaQuestPhaseDiagnostic *diagnostic)
{ (void)diagnostic; if (n < 2 || n + 1 > cap || ((const char *)p)[0] != '{') return 0;
  memcpy(out, p, n); out[n] = 0; *size = n; return 1; }
int dluma_quest_phase_service_resolve_named(const char *mod, const char *handler,
    uint32_t map, uint64_t *owner, uint32_t *generation, uint64_t *token)
{
    assert(dispatch_thread == (LONG)GetCurrentThreadId());
    if (strcmp(mod, "mod.fallback") || strcmp(handler, "pulse") || map != 17) return 0;
    *owner = 99; *generation = 4; *token = fallback_token; return 1;
}
DlumaStatus dluma_quest_phase_service_dispatch_named(const char *mod, const char *handler,
    const char *json, uint32_t map, const char *quest, const char *phase)
{ (void)json; (void)quest; (void)phase; assert(!strcmp(mod,"mod.fallback") && !strcmp(handler,"pulse") && map == 17);
  ++fallback_calls; return DLUMA_STATUS_OK; }

static int sink(void *user, const DlumaModHandlerEventV1 *event)
{
    unsigned index = (unsigned)(uintptr_t)user;
    assert(dispatch_thread == (LONG)GetCurrentThreadId());
    assert(event->source_kind == 3 && event->handler.size == 5);
    assert(!memcmp(event->handler.data, "pulse", 5));
    assert(event->provider.size == 5 && !memcmp(event->provider.data, index == 1 ? "mod.a" : "mod.b", 5));
    assert(event->callback_id == 10 + index);
    assert(event->arguments_json.size < sizeof(delivered_json));
    memcpy(delivered_json,event->arguments_json.data,(size_t)event->arguments_json.size);
    delivered_json[event->arguments_json.size] = 0;
    ++delivered[index]; return 1;
}
static uint64_t register_handler(unsigned i)
{
    DlumaModHandlerRegistrationRequest request = {0}; DlumaTokenResult result = {0};
    request.handler.data = "pulse"; request.handler.size = 5; request.callback_id = 11 + i;
    assert(dluma_mod_handler_service_invoke(&owners[i],15001,&request,&result) == DLUMA_STATUS_OK);
    return result.token;
}
static void pump(void)
{ dispatch_thread = (LONG)GetCurrentThreadId(); dluma_mod_handler_service_pump(); dispatch_thread = 0; }
static DWORD WINAPI produce(void *unused)
{ (void)unused; assert(dluma_mod_handler_service_enqueue_boss("mod.a","pulse","{}",17,"scenario","phase") == DLUMA_STATUS_OK); return 0; }

int main(void)
{
    unsigned i; uint64_t token; HANDLE worker; DlumaModOwner *saved_owner;
    dluma_mod_handler_service_queue_map_changed(17);
    for (i = 0; i < 2; ++i) {
        owners[i].owner_id = i + 1; owners[i].generation = 1; owners[i].state = DLUMA_MOD_OWNER_ACTIVE;
        owners[i].map_generation = 17; owners[i].max_subscriptions = 8;
        memcpy(owners[i].stable_id,i ? "mod.b" : "mod.a",6);
        memcpy(packages[i].manifest.id,owners[i].stable_id,6); packages[i].valid = 1;
        assert(dluma_mod_handler_service_attach(&owners[i],&packages[i],sink,(void *)(uintptr_t)(i+1)));
        (void)register_handler(i);
    }
    assert(dluma_mod_handler_service_enqueue("mod.a","pulse","{}",17,3,"","s","p") == DLUMA_STATUS_WRONG_THREAD);
    /* A producer must not touch even the first live registry endpoint. */
    saved_owner = g_mhs_owners[0].owner;
    g_mhs_owners[0].owner = (DlumaModOwner *)(uintptr_t)1;
    worker = CreateThread(NULL,0,produce,NULL,0,NULL); assert(worker);
    assert(WaitForSingleObject(worker,5000) == WAIT_OBJECT_0); CloseHandle(worker);
    g_mhs_owners[0].owner = saved_owner;
    assert(!delivered[1]);
    assert(dluma_mod_handler_service_enqueue_boss("mod.b","pulse","{}",17,"scenario","phase") == DLUMA_STATUS_OK);
    dluma_mod_handler_service_pump(); assert(!delivered[1] && !delivered[2]);
    pump(); assert(delivered[1] == 1 && delivered[2] == 1);
    {
        char mod[] = "mod.a", json[] = "{\"value\":\"copied\"}";
        assert(dluma_mod_handler_service_enqueue_boss(mod,"pulse",json,17,"s","p") == DLUMA_STATUS_OK);
        memset(mod,'x',sizeof(mod)-1); memset(json,'x',sizeof(json)-1);
        pump(); assert(delivered[1] == 2 && !strcmp(delivered_json,"{\"value\":\"copied\"}"));
        delivered[1] = 1; /* The following checks count additional deliveries. */
    }
    assert(dluma_mod_handler_service_enqueue_boss("mod.a","pulse","{}",17,"s","p") == DLUMA_STATUS_OK);
    assert(dluma_mod_handler_service_unregister_callback(1,11)); token = register_handler(0); (void)token;
    pump(); assert(delivered[1] == 1 && retired_logs);
    assert(dluma_mod_handler_service_enqueue_boss("mod.a","pulse","{}",17,"s","p") == DLUMA_STATUS_OK);
    dluma_mod_handler_service_detach(1);
    assert(dluma_mod_handler_service_attach(&owners[0],&packages[0],sink,(void *)(uintptr_t)1)); (void)register_handler(0);
    pump(); assert(delivered[1] == 1);
    assert(dluma_mod_handler_service_enqueue_boss("mod.fallback","pulse","{}",17,"s","p") == DLUMA_STATUS_OK);
    ++fallback_token; dluma_mod_handler_service_registry_changed(); pump(); assert(!fallback_calls);
    assert(dluma_mod_handler_service_enqueue_boss("mod.fallback","pulse","{}",17,"s","p") == DLUMA_STATUS_OK);
    pump(); assert(fallback_calls == 1);
    for (i = 0; i < 128; ++i) assert(dluma_mod_handler_service_enqueue_boss("mod.a","pulse","{}",17,"s","p") == DLUMA_STATUS_OK);
    assert(dluma_mod_handler_service_enqueue_boss("mod.a","pulse","{}",17,"s","p") == DLUMA_STATUS_LIMIT_EXCEEDED);
    pump(); assert(delivered[1] == 65); pump(); assert(delivered[1] == 129);
    assert(dluma_mod_handler_service_enqueue_boss("bad id","pulse","{}",17,"s","p") == DLUMA_STATUS_INVALID_ARGUMENT);
    assert(dluma_mod_handler_service_enqueue_boss("mod.a","pulse","{}",17,"s","p") == DLUMA_STATUS_OK);
    dluma_mod_handler_service_queue_map_changed(0); pump(); assert(delivered[1] == 129);
    assert(dluma_mod_handler_service_enqueue_boss("mod.a","pulse","{}",17,"s","p") == DLUMA_STATUS_OWNER_REVOKED);
    {
        DlumaModHandlerRegistrationRequest request = {0}; DlumaTokenResult result = {0};
        unsigned subscriptions = owners[0].active_subscriptions;
        request.handler.data = "tail"; request.handler.size = 4; request.callback_id = 99;
        g_mhs_next_token = (1ULL << 63U) - 1U;
        assert(dluma_mod_handler_service_invoke(&owners[0],15001,&request,&result) == DLUMA_STATUS_LIMIT_EXCEEDED);
        assert(owners[0].active_subscriptions == subscriptions && !result.token);
    }
    g_mhs_registry_epoch = UINT64_MAX;
    dluma_mod_handler_service_registry_changed();
    dluma_mod_handler_service_queue_map_changed(17);
    assert(dluma_mod_handler_service_enqueue_boss("mod.a","pulse","{}",17,"s","p") == DLUMA_STATUS_LIMIT_EXCEEDED);
    puts("PASS: native boss producer, scoped routing and retired queue"); return 0;
}
