#include "dlw_mod_handler_service.h"
#include "../dlw_common.h"
#include "../dlw_log.h"
#include "dlw_quest_phase_service.h"
#include "../core/dlw_quest_phase.h"
#include <string.h>

#define DLUMA_MHS_OWNERS 192U
#define DLUMA_MHS_HANDLERS 256U
#define DLUMA_MHS_MAX_DEPTH 16U
#define DLUMA_MHS_QUEUE_CAPACITY 128U
#define DLUMA_MHS_QUEUE_FRAME_BUDGET 64U
typedef struct DlumaHandlerOwner {
    DlumaModOwner* owner;
    const DlumaModPackageSnapshot* package;
    DlumaModHandlerServiceSink sink;
    void* user;
} DlumaHandlerOwner;
typedef struct DlumaHandlerRegistration {
    uint64_t owner_id, callback_id, token;
    char handler[128];
} DlumaHandlerRegistration;
typedef struct DlumaQueuedHandlerInvocation {
    uint64_t owner_id, registration_token;
    uint64_t registry_epoch;
    uint32_t owner_generation, map_generation, source_kind;
    char mod_id[128], handler_id[128], source_mod[128];
    char quest_id[512], phase_id[128];
    char arguments[DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES + 1U];
} DlumaQueuedHandlerInvocation;
static DlumaHandlerOwner g_mhs_owners[DLUMA_MHS_OWNERS];
static DlumaHandlerRegistration g_mhs_handlers[DLUMA_MHS_HANDLERS];
static uint64_t g_mhs_next_token;
static unsigned int g_mhs_depth;
static DlumaQueuedHandlerInvocation g_mhs_queue[DLUMA_MHS_QUEUE_CAPACITY];
static unsigned int g_mhs_queue_head, g_mhs_queue_count;
static uint32_t g_mhs_queue_generation;
static void* g_mhs_queue_lock;
/* Zero is an exhausted/closed native-producer namespace, never reused. */
static uint64_t g_mhs_registry_epoch = 1U;
__declspec(dllimport) void WINAPI AcquireSRWLockExclusive(void* lock);
__declspec(dllimport) void WINAPI ReleaseSRWLockExclusive(void* lock);

static void dluma_mhs_registry_changed_locked(void)
{
    if (!g_mhs_registry_epoch || g_mhs_registry_epoch == UINT64_MAX) {
        g_mhs_registry_epoch = 0U;
        g_mhs_queue_generation = 0U;
        g_mhs_queue_head = g_mhs_queue_count = 0U;
        memset(g_mhs_queue, 0, sizeof(g_mhs_queue));
    } else ++g_mhs_registry_epoch;
}

void dluma_mod_handler_service_registry_changed(void)
{
    AcquireSRWLockExclusive(&g_mhs_queue_lock);
    dluma_mhs_registry_changed_locked();
    ReleaseSRWLockExclusive(&g_mhs_queue_lock);
}

static DlumaStringView dluma_mhs_view(const char* value)
{
    DlumaStringView result;
    result.data = value ? value : "";
    result.size = (uint64_t)strlen(result.data);
    return result;
}
static int dluma_mhs_identifier(const char* value)
{
    unsigned int i;
    if (!value || !value[0]) return 0;
    for (i = 0; value[i]; ++i) {
        char c = value[i];
        if (i >= 127U || !((c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return 0;
    }
    return 1;
}
static int dluma_mhs_copy(DlumaStringView value, char* output, unsigned int capacity)
{
    unsigned int i;
    if (!value.data || !value.size || value.size >= capacity) return 0;
    for (i = 0; i < value.size; ++i) if (!value.data[i]) return 0;
    memcpy(output, value.data, (size_t)value.size);
    output[value.size] = 0;
    return 1;
}
static DlumaHandlerOwner* dluma_mhs_owner(uint64_t owner_id)
{
    unsigned int i;
    for (i = 0; i < DLUMA_MHS_OWNERS; ++i)
        if (g_mhs_owners[i].owner && g_mhs_owners[i].owner->owner_id == owner_id)
            return &g_mhs_owners[i];
    return 0;
}
static int dluma_mhs_resolve(const char* mod_id, const char* handler_id,
    uint32_t map_generation, uint64_t* owner_id, uint32_t* owner_generation,
    uint64_t* registration_token)
{
    unsigned int index;
    for (index = 0; index < DLUMA_MHS_HANDLERS; ++index) {
        const DlumaHandlerRegistration* registration = &g_mhs_handlers[index];
        DlumaHandlerOwner* endpoint;
        if (!registration->owner_id || dluma_strcmp(registration->handler, handler_id)) continue;
        endpoint = dluma_mhs_owner(registration->owner_id);
        if (!endpoint || dluma_strcmp(endpoint->owner->stable_id, mod_id) ||
            endpoint->owner->state != DLUMA_MOD_OWNER_ACTIVE ||
            endpoint->owner->map_generation != map_generation) continue;
        *owner_id = endpoint->owner->owner_id;
        *owner_generation = endpoint->owner->generation;
        *registration_token = registration->token;
        return 1;
    }
    return dluma_quest_phase_service_resolve_named(mod_id, handler_id, map_generation,
        owner_id, owner_generation, registration_token);
}
static int dluma_mhs_copy_context(char* output, unsigned int capacity, const char* value)
{
    unsigned int index = 0;
    if (!value) value = "";
    while (index < capacity && value[index]) ++index;
    if (index == capacity) return 0;
    memcpy(output, value, index + 1U);
    return 1;
}

static DlumaStatus dluma_mhs_enqueue(const char* mod_id,
    const char* handler_id, const char* arguments_json, uint32_t map_generation,
    uint32_t source_kind, const char* source_mod, const char* quest_id, const char* phase_id,
    int native_producer)
{
    DlumaQueuedHandlerInvocation invocation;
    DlumaQuestPhaseDiagnostic diagnostic;
    unsigned int arguments_size = 0, length = 0;
    if (!native_producer && (LONG)GetCurrentThreadId() !=
            dluma_engine_game_frame_current_dispatch_thread_id(&g_dluma.engine))
        return DLUMA_STATUS_WRONG_THREAD;
    if (!dluma_mhs_identifier(mod_id) || !dluma_mhs_identifier(handler_id) ||
        !map_generation || source_kind < 1U || source_kind > 3U || !arguments_json)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    memset(&invocation, 0, sizeof(invocation));
    if (!dluma_mhs_copy_context(invocation.source_mod, sizeof(invocation.source_mod), source_mod) ||
        !dluma_mhs_copy_context(invocation.quest_id, sizeof(invocation.quest_id), quest_id) ||
        !dluma_mhs_copy_context(invocation.phase_id, sizeof(invocation.phase_id), phase_id))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    while (length <= DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES && arguments_json[length]) ++length;
    if (length > DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES ||
        !dluma_quest_phase_json_canonicalize_object(arguments_json, length, invocation.arguments,
            sizeof(invocation.arguments), &arguments_size, &diagnostic)) return DLUMA_STATUS_INVALID_ARGUMENT;
    if (!native_producer && !dluma_mhs_resolve(mod_id, handler_id, map_generation, &invocation.owner_id,
            &invocation.owner_generation, &invocation.registration_token)) return DLUMA_STATUS_NOT_FOUND;
    memcpy(invocation.mod_id, mod_id, strlen(mod_id) + 1U);
    memcpy(invocation.handler_id, handler_id, strlen(handler_id) + 1U);
    invocation.map_generation = map_generation; invocation.source_kind = source_kind;
    AcquireSRWLockExclusive(&g_mhs_queue_lock);
    if (native_producer && !g_mhs_registry_epoch) {
        ReleaseSRWLockExclusive(&g_mhs_queue_lock);
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    }
    if (g_dluma.stop_requested || map_generation != g_mhs_queue_generation) {
        ReleaseSRWLockExclusive(&g_mhs_queue_lock);
        return DLUMA_STATUS_OWNER_REVOKED;
    }
    if (native_producer) invocation.registry_epoch = g_mhs_registry_epoch;
    if (g_mhs_queue_count == DLUMA_MHS_QUEUE_CAPACITY) {
        ReleaseSRWLockExclusive(&g_mhs_queue_lock);
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    }
    g_mhs_queue[(g_mhs_queue_head + g_mhs_queue_count) % DLUMA_MHS_QUEUE_CAPACITY] = invocation;
    ++g_mhs_queue_count;
    ReleaseSRWLockExclusive(&g_mhs_queue_lock);
    return DLUMA_STATUS_OK;
}

DlumaStatus dluma_mod_handler_service_enqueue(const char* mod_id,
    const char* handler_id, const char* arguments_json, uint32_t map_generation,
    uint32_t source_kind, const char* source_mod, const char* quest_id, const char* phase_id)
{
    return dluma_mhs_enqueue(mod_id, handler_id, arguments_json, map_generation,
        source_kind, source_mod, quest_id, phase_id, 0);
}

DlumaStatus dluma_mod_handler_service_enqueue_boss(const char* mod_id,
    const char* handler_id, const char* arguments_json, uint32_t map_generation,
    const char* scenario_name, const char* phase_name)
{
    return dluma_mhs_enqueue(mod_id, handler_id, arguments_json, map_generation,
        3U, "", scenario_name, phase_name, 1);
}

void dluma_mod_handler_service_queue_map_changed(uint32_t map_generation)
{
    AcquireSRWLockExclusive(&g_mhs_queue_lock);
    dluma_mhs_registry_changed_locked();
    g_mhs_queue_generation = g_mhs_registry_epoch ? map_generation : 0U;
    g_mhs_queue_head = 0U; g_mhs_queue_count = 0U;
    memset(g_mhs_queue, 0, sizeof(g_mhs_queue));
    ReleaseSRWLockExclusive(&g_mhs_queue_lock);
}

void dluma_mod_handler_service_pump(void)
{
    unsigned int index, budget;
    if (g_dluma.stop_requested || !dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine)) return;
    AcquireSRWLockExclusive(&g_mhs_queue_lock);
    budget = g_mhs_queue_count < DLUMA_MHS_QUEUE_FRAME_BUDGET ? g_mhs_queue_count : DLUMA_MHS_QUEUE_FRAME_BUDGET;
    ReleaseSRWLockExclusive(&g_mhs_queue_lock);
    for (index = 0; index < budget; ++index) {
        DlumaQueuedHandlerInvocation invocation;
        uint64_t owner_id = 0, token = 0, registry_epoch;
        uint32_t owner_generation = 0, map_generation;
        DlumaStatus status;
        AcquireSRWLockExclusive(&g_mhs_queue_lock);
        if (!g_mhs_queue_count) { ReleaseSRWLockExclusive(&g_mhs_queue_lock); break; }
        invocation = g_mhs_queue[g_mhs_queue_head];
        memset(&g_mhs_queue[g_mhs_queue_head], 0, sizeof(g_mhs_queue[g_mhs_queue_head]));
        g_mhs_queue_head = (g_mhs_queue_head + 1U) % DLUMA_MHS_QUEUE_CAPACITY;
        --g_mhs_queue_count; map_generation = g_mhs_queue_generation;
        registry_epoch = g_mhs_registry_epoch;
        ReleaseSRWLockExclusive(&g_mhs_queue_lock);
        if (invocation.map_generation != map_generation) continue;
        if (invocation.registry_epoch && invocation.registry_epoch != registry_epoch) {
            dluma_log_pair("INFO", "queued_mod_handler_retired", invocation.mod_id);
            continue;
        }
        if (!dluma_mhs_resolve(invocation.mod_id, invocation.handler_id, map_generation,
                &owner_id, &owner_generation, &token) ||
            (!invocation.registry_epoch && (owner_id != invocation.owner_id ||
             owner_generation != invocation.owner_generation || token != invocation.registration_token))) {
            dluma_log_pair("INFO", "queued_mod_handler_retired", invocation.mod_id);
            continue;
        }
        status = dluma_mod_handler_service_dispatch(invocation.mod_id, invocation.handler_id,
            invocation.arguments, invocation.map_generation, invocation.source_kind,
            invocation.source_mod, invocation.quest_id, invocation.phase_id);
        if (status != DLUMA_STATUS_OK) {
            dluma_log_pair("WARN", "queued_mod_handler_target", invocation.mod_id);
            dluma_log_pair("WARN", "queued_mod_handler_handler", invocation.handler_id);
            dluma_log_u64("WARN", "queued_mod_handler_status", status);
        }
    }
}
int dluma_mod_handler_service_attach(DlumaModOwner* owner,
    const DlumaModPackageSnapshot* package, DlumaModHandlerServiceSink sink, void* user)
{
    unsigned int i;
    if (!owner || owner->state != DLUMA_MOD_OWNER_ACTIVE || !package ||
        !package->valid || !sink || dluma_strcmp(owner->stable_id, package->manifest.id)) return 0;
    if (dluma_mhs_owner(owner->owner_id)) return 1;
    for (i = 0; i < DLUMA_MHS_OWNERS; ++i) if (!g_mhs_owners[i].owner) {
        g_mhs_owners[i].owner = owner;
        g_mhs_owners[i].package = package;
        g_mhs_owners[i].sink = sink;
        g_mhs_owners[i].user = user;
        dluma_mod_handler_service_registry_changed();
        return 1;
    }
    return 0;
}
int dluma_mod_handler_service_unregister_callback(uint64_t owner_id, uint64_t callback_id)
{
    unsigned int i; int changed = 0;
    DlumaHandlerOwner* endpoint = dluma_mhs_owner(owner_id);
    for (i = 0; i < DLUMA_MHS_HANDLERS; ++i)
        if (g_mhs_handlers[i].owner_id == owner_id &&
            g_mhs_handlers[i].callback_id == callback_id) {
            memset(&g_mhs_handlers[i], 0, sizeof(g_mhs_handlers[i]));
            changed = 1;
            if (endpoint && endpoint->owner->active_subscriptions)
                --endpoint->owner->active_subscriptions;
        }
    if (changed) dluma_mod_handler_service_registry_changed();
    return 1;
}
void dluma_mod_handler_service_detach(uint64_t owner_id)
{
    DlumaHandlerOwner* endpoint = dluma_mhs_owner(owner_id);
    dluma_mod_handler_service_clear(owner_id);
    if (endpoint) {
        memset(endpoint, 0, sizeof(*endpoint));
        dluma_mod_handler_service_registry_changed();
    }
}
void dluma_mod_handler_service_clear(uint64_t owner_id)
{
    unsigned int i;
    for (i = 0; i < DLUMA_MHS_HANDLERS; ++i)
        if (g_mhs_handlers[i].owner_id == owner_id)
            dluma_mod_handler_service_unregister_callback(owner_id, g_mhs_handlers[i].callback_id);
}
unsigned int dluma_mod_handler_service_count(uint64_t owner_id)
{
    unsigned int i, count = 0;
    for (i = 0; i < DLUMA_MHS_HANDLERS; ++i)
        if (g_mhs_handlers[i].owner_id == owner_id) ++count;
    return count;
}
DlumaStatus dluma_mod_handler_service_dispatch(const char* mod_id,
    const char* handler_id, const char* arguments_json, uint32_t map_generation,
    uint32_t source_kind, const char* source_mod, const char* quest_id, const char* phase_id)
{
    unsigned int i, size = 0;
    char arguments[DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES + 1U];
    DlumaQuestPhaseDiagnostic diagnostic;
    if (!dluma_engine_game_frame_dispatch_active_on_current_thread(&g_dluma.engine) ||
        dluma_engine_game_frame_authority_status(&g_dluma.engine, GetTickCount64()) !=
            DLUMA_GAME_FRAME_AUTHORITY_STATUS_AUTHORITATIVE) return DLUMA_STATUS_WRONG_THREAD;
    if (!dluma_mhs_identifier(mod_id) || !dluma_mhs_identifier(handler_id) ||
        !map_generation || source_kind < 1U || source_kind > 3U || !arguments_json)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if (strlen(arguments_json) > DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES ||
        !dluma_quest_phase_json_canonicalize_object(arguments_json,
            (unsigned int)strlen(arguments_json), arguments, sizeof(arguments),
            &size, &diagnostic)) return DLUMA_STATUS_INVALID_ARGUMENT;
    if (g_mhs_depth >= DLUMA_MHS_MAX_DEPTH) return DLUMA_STATUS_LIMIT_EXCEEDED;
    for (i = 0; i < DLUMA_MHS_HANDLERS; ++i) {
        DlumaHandlerRegistration registration = g_mhs_handlers[i];
        DlumaHandlerOwner* endpoint;
        DlumaModHandlerServiceSink sink;
        void* user;
        DlumaModHandlerEventV1 event;
        int success;
        if (!registration.owner_id || dluma_strcmp(registration.handler, handler_id)) continue;
        endpoint = dluma_mhs_owner(registration.owner_id);
        if (!endpoint || dluma_strcmp(endpoint->owner->stable_id, mod_id) ||
            endpoint->owner->state != DLUMA_MOD_OWNER_ACTIVE ||
            endpoint->owner->map_generation != map_generation) continue;
        memset(&event, 0, sizeof(event));
        event.struct_size = sizeof(event); event.struct_version = 1U;
        event.source_kind = source_kind; event.map_generation = map_generation;
        event.callback_id = registration.callback_id; event.token = registration.token;
        event.source_mod = dluma_mhs_view(source_mod);
        event.quest_id = dluma_mhs_view(quest_id); event.phase_id = dluma_mhs_view(phase_id);
        event.provider = dluma_mhs_view(mod_id); event.handler = dluma_mhs_view(handler_id);
        event.arguments_json = dluma_mhs_view(arguments);
        sink = endpoint->sink; user = endpoint->user;
        endpoint->owner->game_thread_id = GetCurrentThreadId();
        ++g_mhs_depth;
        success = sink(user, &event);
        --g_mhs_depth;
        return success ? DLUMA_STATUS_OK : DLUMA_STATUS_INTERNAL_ERROR;
    }
    ++g_mhs_depth;
    {
        DlumaStatus status = dluma_quest_phase_service_dispatch_named(mod_id,
            handler_id, arguments, map_generation, quest_id, phase_id);
        --g_mhs_depth;
        return status;
    }
}
DlumaStatus dluma_mod_handler_service_invoke(DlumaModOwner* owner,
    uint32_t operation_id, const void* request, void* result)
{
    unsigned int i;
    if (!owner || !request || !result) return DLUMA_STATUS_INVALID_ARGUMENT;
    if (operation_id == 15001U) {
        const DlumaModHandlerRegistrationRequest* input = request;
        DlumaTokenResult* output = result;
        DlumaHandlerRegistration* slot = 0;
        char handler[128];
        if (!input->callback_id || !dluma_mhs_copy(input->handler, handler, sizeof(handler)) ||
            !dluma_mhs_identifier(handler)) return DLUMA_STATUS_INVALID_ARGUMENT;
        if (!dluma_mhs_owner(owner->owner_id)) return DLUMA_STATUS_UNAVAILABLE;
        for (i = 0; i < DLUMA_MHS_HANDLERS; ++i) {
            if (!g_mhs_handlers[i].owner_id) { if (!slot) slot = &g_mhs_handlers[i]; }
            else if (g_mhs_handlers[i].owner_id == owner->owner_id &&
                !dluma_strcmp(g_mhs_handlers[i].handler, handler)) return DLUMA_STATUS_LIMIT_EXCEEDED;
        }
        if (!slot || owner->active_subscriptions >= owner->max_subscriptions ||
            g_mhs_next_token >= ((1ULL << 63U) - 1U))
            return DLUMA_STATUS_LIMIT_EXCEEDED;
        slot->owner_id = owner->owner_id; slot->callback_id = input->callback_id;
        /* Distinct token namespace from quest/timer registrations. */
        slot->token = (++g_mhs_next_token) | (1ULL << 63U);
        memcpy(slot->handler, handler, strlen(handler) + 1U);
        ++owner->active_subscriptions;
        output->token = slot->token;
        dluma_mod_handler_service_registry_changed();
        return DLUMA_STATUS_OK;
    }
    if (operation_id == 15002U) {
        const DlumaModHandlerInvokeRequest* input = request;
        char mod[128], handler[128], arguments[DLUMA_QUEST_PHASE_MAX_ARGUMENT_BYTES + 1U];
        DlumaStatus status;
        if (!dluma_mhs_copy(input->mod, mod, sizeof(mod)) ||
            !dluma_mhs_copy(input->handler, handler, sizeof(handler)) ||
            !dluma_mhs_copy(input->arguments_json, arguments, sizeof(arguments)))
            return DLUMA_STATUS_INVALID_ARGUMENT;
        status = dluma_mod_handler_service_dispatch(mod, handler, arguments,
            owner->map_generation, 1U, owner->stable_id, "", "");
        ((DlumaBoolResult*)result)->value = status == DLUMA_STATUS_OK;
        return status;
    }
    if (operation_id == 15003U) {
        uint64_t token = ((const DlumaTokenRequest*)request)->token;
        ((DlumaBoolResult*)result)->value = 0U;
        for (i = 0; i < DLUMA_MHS_HANDLERS; ++i)
            if (g_mhs_handlers[i].owner_id == owner->owner_id && g_mhs_handlers[i].token == token) {
                memset(&g_mhs_handlers[i], 0, sizeof(g_mhs_handlers[i]));
                if (owner->active_subscriptions) --owner->active_subscriptions;
                dluma_mod_handler_service_registry_changed();
                ((DlumaBoolResult*)result)->value = 1U; break;
            }
        return DLUMA_STATUS_OK;
    }
    return DLUMA_STATUS_UNAVAILABLE;
}
void dluma_mod_handler_service_prepare_capabilities(DlumaCapabilityRegistry* registry)
{
    (void)dluma_capability_registry_set(registry, DLUMA_CAPABILITY_ACTIONS_MOD_HANDLERS,
        1, 1, dluma_mhs_view("ready"));
}
