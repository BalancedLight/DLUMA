#include "dlw_quest_phase_service.h"

#include "../dlw_common.h"
#include "../dlw_log.h"
#include "../dlw_quest_phase_player.h"

#include <string.h>

#define DLUMA_QPS_MAX_OWNERS 192U
#define DLUMA_QPS_MAX_REGISTRATIONS \
    (DLUMA_QUEST_PHASE_MAX_PROVIDERS + DLUMA_QUEST_PHASE_MAX_OBSERVERS)

typedef struct DlumaQuestPhaseServiceOwner {
    unsigned int active;
    DlumaModOwner* owner;
    const DlumaModPackageSnapshot* package;
    int load_priority;
    DlumaQuestPhaseServiceEventSinkFn sink;
    void* sink_user;
} DlumaQuestPhaseServiceOwner;

typedef struct DlumaQuestPhaseServiceRegistration {
    unsigned int active;
    unsigned int event_kind;
    uint64_t callback_id;
    uint64_t dispatcher_token;
    uint64_t owner_id;
    char key[257];
} DlumaQuestPhaseServiceRegistration;

static DlumaQuestPhaseServiceOwner g_dluma_qps_owners[DLUMA_QPS_MAX_OWNERS];
static DlumaQuestPhaseServiceRegistration
    g_dluma_qps_registrations[DLUMA_QPS_MAX_REGISTRATIONS];

static unsigned int dluma_qps_length(const char* value)
{
    unsigned int size = 0;
    if (!value) return 0;
    while (value[size]) ++size;
    return size;
}

static int dluma_qps_equal(const char* left, const char* right)
{
    unsigned int index = 0;
    if (!left || !right) return 0;
    while (left[index] && left[index] == right[index]) ++index;
    return left[index] == right[index];
}

static int dluma_qps_identifier(const char* value, unsigned int maximum)
{
    unsigned int index = 0;
    if (!value || !value[0]) return 0;
    while (value[index]) {
        char current = value[index];
        if (!((current >= 'a' && current <= 'z') ||
              (current >= '0' && current <= '9') || current == '.' ||
              current == '_' || current == '-'))
            return 0;
        if (++index > maximum) return 0;
    }
    return index != 0U;
}

static int dluma_qps_contract(const char* value)
{
    const char* colon = value;
    char provider[128];
    char handler[128];
    unsigned int provider_size;
    unsigned int handler_size;
    if (!value) return 0;
    while (*colon && *colon != ':') ++colon;
    if (*colon != ':' || !colon[1]) return 0;
    provider_size = (unsigned int)(colon - value);
    handler_size = dluma_qps_length(colon + 1);
    if (!provider_size || provider_size >= sizeof(provider) ||
        !handler_size || handler_size >= sizeof(handler) ||
        dluma_qps_length(value) > 256U)
        return 0;
    memcpy(provider, value, provider_size);
    provider[provider_size] = 0;
    memcpy(handler, colon + 1, handler_size + 1U);
    return dluma_qps_identifier(provider, 127U) &&
        dluma_qps_identifier(handler, 127U);
}

static DlumaQuestPhaseServiceOwner* dluma_qps_find_owner(uint64_t owner_id)
{
    unsigned int index;
    if (!owner_id) return 0;
    for (index = 0; index < DLUMA_QPS_MAX_OWNERS; ++index)
        if (g_dluma_qps_owners[index].active &&
            g_dluma_qps_owners[index].owner &&
            g_dluma_qps_owners[index].owner->owner_id == owner_id)
            return &g_dluma_qps_owners[index];
    return 0;
}

static int dluma_qps_handler_declared(
    const DlumaQuestPhaseServiceOwner* endpoint,
    const char* handler)
{
    const DlumaModManifest* manifest;
    if (!endpoint || !endpoint->owner || !endpoint->package ||
        !endpoint->package->valid || !handler)
        return 0;
    manifest = &endpoint->package->manifest;
    if (dluma_stricmp(manifest->id, endpoint->owner->stable_id) != 0 ||
        !dluma_mod_manifest_has_access(
            manifest, DLUMA_ACCESS_QUEST_PHASE_HANDLE))
        return 0;
    return dluma_mod_manifest_declares_quest_phase_handler(
        manifest, handler);
}

static void dluma_qps_dispatch(
    void* user,
    const DlumaQuestPhaseEvent* event_value)
{
    DlumaQuestPhaseServiceRegistration* registration =
        (DlumaQuestPhaseServiceRegistration*)user;
    DlumaQuestPhaseServiceOwner* endpoint;
    DlumaQuestPhaseServiceEventSinkFn sink;
    void* sink_user;
    uint64_t callback_id;
    uint64_t token;
    uint32_t event_kind;
    char key[257];
    int result;
    if (!registration || !registration->active || !event_value)
        return;
    endpoint = dluma_qps_find_owner(registration->owner_id);
    if (!endpoint || !endpoint->owner ||
        endpoint->owner->state != DLUMA_MOD_OWNER_ACTIVE ||
        endpoint->owner->map_generation != event_value->map_generation ||
        !endpoint->sink)
        return;
    if (registration->event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER) {
        if (!dluma_qps_handler_declared(endpoint, event_value->handler_id) ||
            !dluma_qps_equal(registration->key, event_value->handler_id) ||
            ((endpoint->owner->language == DLUMA_MOD_LANGUAGE_LUA) !=
             (event_value->runtime_kind == DLUMA_QUEST_PHASE_RUNTIME_LUA)))
            return;
    } else if (registration->event_kind !=
                   DLUMA_MOD_EVENT_QUEST_PHASE_OBSERVER ||
               !dluma_qps_equal(
                   registration->key, event_value->contract_id)) {
        return;
    }
    /* Copy everything needed before entering third-party code. A callback may
     * unregister itself and immediately reuse this slot for a replacement. */
    sink = endpoint->sink;
    sink_user = endpoint->sink_user;
    callback_id = registration->callback_id;
    token = registration->dispatcher_token;
    event_kind = registration->event_kind;
    dluma_str_copy(key, DLUMA_ARRAY_COUNT(key), registration->key);
    result = sink(
        sink_user, event_kind, callback_id, token, event_value);
    if (!result) {
        dluma_log_pair(
            "WARN",
            event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER
                ? "quest_phase_provider_callback_failed"
                : "quest_phase_observer_callback_failed",
            key);
    }
}

int dluma_quest_phase_service_attach(
    DlumaModOwner* owner,
    const DlumaModPackageSnapshot* package,
    int load_priority,
    DlumaQuestPhaseServiceEventSinkFn sink,
    void* sink_user)
{
    unsigned int index;
    if (!owner || owner->state != DLUMA_MOD_OWNER_ACTIVE || !owner->owner_id ||
        !package || !package->valid || !sink ||
        dluma_stricmp(owner->stable_id, package->manifest.id) != 0)
        return 0;
    if (dluma_qps_find_owner(owner->owner_id)) return 1;
    for (index = 0; index < DLUMA_QPS_MAX_OWNERS; ++index)
        if (!g_dluma_qps_owners[index].active) {
            memset(&g_dluma_qps_owners[index], 0,
                   sizeof(g_dluma_qps_owners[index]));
            g_dluma_qps_owners[index].owner = owner;
            g_dluma_qps_owners[index].package = package;
            g_dluma_qps_owners[index].load_priority = load_priority;
            g_dluma_qps_owners[index].sink = sink;
            g_dluma_qps_owners[index].sink_user = sink_user;
            g_dluma_qps_owners[index].active = 1U;
            return 1;
        }
    return 0;
}

int dluma_quest_phase_service_detach(uint64_t owner_id)
{
    DlumaQuestPhaseServiceOwner* endpoint = dluma_qps_find_owner(owner_id);
    unsigned int index;
    if (!endpoint) return 1;
    endpoint->active = 0U;
    for (index = 0; index < DLUMA_QPS_MAX_REGISTRATIONS; ++index) {
        DlumaQuestPhaseServiceRegistration* registration =
            &g_dluma_qps_registrations[index];
        if (!registration->active || registration->owner_id != owner_id)
            continue;
        (void)dluma_quest_phase_player_unregister(
            registration->dispatcher_token);
        memset(registration, 0, sizeof(*registration));
        if (endpoint->owner && endpoint->owner->active_subscriptions)
            --endpoint->owner->active_subscriptions;
    }
    memset(endpoint, 0, sizeof(*endpoint));
    return 1;
}

int dluma_quest_phase_service_unregister_callback(
    uint64_t owner_id,
    uint32_t event_kind,
    uint64_t callback_id,
    const char* key)
{
    DlumaQuestPhaseServiceOwner* endpoint = dluma_qps_find_owner(owner_id);
    unsigned int index;
    for (index = 0; index < DLUMA_QPS_MAX_REGISTRATIONS; ++index) {
        DlumaQuestPhaseServiceRegistration* registration =
            &g_dluma_qps_registrations[index];
        if (!registration->active || registration->owner_id != owner_id ||
            registration->event_kind != event_kind ||
            registration->callback_id != callback_id ||
            !dluma_qps_equal(registration->key, key))
            continue;
        (void)dluma_quest_phase_player_unregister(
            registration->dispatcher_token);
        memset(registration, 0, sizeof(*registration));
        if (endpoint && endpoint->owner &&
            endpoint->owner->active_subscriptions)
            --endpoint->owner->active_subscriptions;
        return 1;
    }
    return 1;
}

static DlumaStatus dluma_qps_register(
    DlumaModOwner* owner,
    uint32_t event_kind,
    const DlumaQuestPhaseRegistrationRequest* request,
    DlumaTokenResult* result)
{
    DlumaQuestPhaseServiceOwner* endpoint;
    DlumaQuestPhaseServiceRegistration* slot = 0;
    char key[257];
    unsigned int index;
    unsigned int owned_count = 0;
    int registered;
    if (!owner || !request || !result || !request->callback_id ||
        !request->key.data || !request->key.size ||
        request->key.size >= sizeof(key))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    endpoint = dluma_qps_find_owner(owner->owner_id);
    if (!endpoint) return DLUMA_STATUS_UNAVAILABLE;
    for (index = 0; index < request->key.size; ++index) {
        if (!request->key.data[index]) return DLUMA_STATUS_INVALID_ARGUMENT;
        key[index] = request->key.data[index];
    }
    key[request->key.size] = 0;
    if ((event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER &&
         !dluma_qps_identifier(key, 127U)) ||
        (event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_OBSERVER &&
         !dluma_qps_contract(key)))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if (event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER &&
        !dluma_qps_handler_declared(endpoint, key))
        return DLUMA_STATUS_ACCESS_DENIED;
    for (index = 0; index < DLUMA_QPS_MAX_REGISTRATIONS; ++index) {
        DlumaQuestPhaseServiceRegistration* current =
            &g_dluma_qps_registrations[index];
        if (!current->active) {
            if (!slot) slot = current;
            continue;
        }
        if (current->owner_id != owner->owner_id) continue;
        ++owned_count;
        if (current->event_kind == event_kind &&
            dluma_qps_equal(current->key, key))
            return DLUMA_STATUS_LIMIT_EXCEEDED;
    }
    if (!slot || owned_count >= owner->max_subscriptions)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    memset(slot, 0, sizeof(*slot));
    slot->owner_id = owner->owner_id;
    slot->event_kind = event_kind;
    slot->callback_id = request->callback_id;
    dluma_str_copy(slot->key, DLUMA_ARRAY_COUNT(slot->key), key);
    slot->active = 1U;
    if (event_kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER)
        registered = dluma_quest_phase_player_register_provider(
            owner->stable_id, key, endpoint->load_priority,
            dluma_qps_dispatch, slot, &slot->dispatcher_token);
    else
        registered = dluma_quest_phase_player_register_observer(
            owner->stable_id, key, endpoint->load_priority,
            dluma_qps_dispatch, slot, &slot->dispatcher_token);
    if (!registered) {
        memset(slot, 0, sizeof(*slot));
        return DLUMA_STATUS_UNAVAILABLE;
    }
    ++owner->active_subscriptions;
    result->token = slot->dispatcher_token;
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_qps_unregister(
    DlumaModOwner* owner,
    const DlumaTokenRequest* request,
    DlumaBoolResult* result)
{
    unsigned int index;
    if (!owner || !request || !result) return DLUMA_STATUS_INVALID_ARGUMENT;
    result->value = 0U;
    for (index = 0; index < DLUMA_QPS_MAX_REGISTRATIONS; ++index) {
        DlumaQuestPhaseServiceRegistration* registration =
            &g_dluma_qps_registrations[index];
        if (!registration->active ||
            registration->owner_id != owner->owner_id ||
            registration->dispatcher_token != request->token)
            continue;
        (void)dluma_quest_phase_player_unregister(
            registration->dispatcher_token);
        memset(registration, 0, sizeof(*registration));
        if (owner->active_subscriptions) --owner->active_subscriptions;
        result->value = 1U;
        return DLUMA_STATUS_OK;
    }
    return DLUMA_STATUS_OK;
}

DlumaStatus dluma_quest_phase_service_invoke(
    DlumaModOwner* owner,
    uint32_t operation_id,
    const void* request,
    void* result)
{
    if (operation_id == 12001U)
        return dluma_qps_register(
            owner, DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER,
            (const DlumaQuestPhaseRegistrationRequest*)request,
            (DlumaTokenResult*)result);
    if (operation_id == 12002U)
        return dluma_qps_register(
            owner, DLUMA_MOD_EVENT_QUEST_PHASE_OBSERVER,
            (const DlumaQuestPhaseRegistrationRequest*)request,
            (DlumaTokenResult*)result);
    if (operation_id == 12003U)
        return dluma_qps_unregister(
            owner, (const DlumaTokenRequest*)request,
            (DlumaBoolResult*)result);
    return DLUMA_STATUS_UNAVAILABLE;
}

void dluma_quest_phase_service_prepare_capabilities(
    DlumaCapabilityRegistry* registry)
{
    DlumaStringView status;
    int ready = dluma_quest_phase_player_started();
    if (!registry) return;
    status.data = ready ? "ready" : "exact-build phase observer unavailable";
    status.size = (uint64_t)strlen(status.data);
    (void)dluma_capability_registry_set(
        registry, DLUMA_CAPABILITY_QUESTS_PHASE_HANDLERS,
        1, ready, status);
}
