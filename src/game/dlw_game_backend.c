#include "dlw_game_backend.h"
#include "dlw_player_service.h"
#include "dlw_media_service.h"
#include "dlw_quest_phase_service.h"

#include "../dlw_common.h"
#include "../dlw_lua54_api.h"
#include "../dlw_native_gameplay.h"

#include <string.h>

static uint32_t dluma_game_backend_thread_id(void* user)
{
    (void)user;
    return GetCurrentThreadId();
}

static void dluma_game_backend_set_capability(
    DlumaCapabilityRegistry* registry,
    uint32_t capability_id,
    int configured,
    int ready,
    const char* status)
{
    DlumaStringView view;
    view.data = status;
    view.size = status ? (uint64_t)strlen(status) : 0U;
    (void)dluma_capability_registry_set(
        registry, capability_id, configured, ready, view);
}

void dluma_game_backend_prepare_capabilities(
    DlumaCapabilityRegistry* registry)
{
    uint32_t supported;
    uint32_t ready;
    if (!registry) return;
    supported = dluma_native_gameplay_supported_capabilities();
    ready = dluma_native_gameplay_capabilities();
    dluma_capability_registry_initialize(registry);
    dluma_game_backend_set_capability(
        registry, DLUMA_CAPABILITY_CORE_RUNTIME, 1, 1, "ready");
    dluma_game_backend_set_capability(
        registry, DLUMA_CAPABILITY_CORE_TIMERS, 0, 0, "planned");
#define DLUMA_GAME_SET_CAP(api_id, legacy_bit) \
    dluma_game_backend_set_capability( \
        registry, (api_id), \
        (supported & (legacy_bit)) != 0, \
        (ready & (legacy_bit)) != 0, \
        (ready & (legacy_bit)) != 0 \
            ? "ready" \
            : "exact-build capability unavailable")
    DLUMA_GAME_SET_CAP(
        DLUMA_CAPABILITY_WORLD_FIND, DLUMA_LUA54_CAP_WORLD_FIND);
    DLUMA_GAME_SET_CAP(
        DLUMA_CAPABILITY_WORLD_LOCAL_ENABLE,
        DLUMA_LUA54_CAP_WORLD_LOCAL_ENABLE);
    DLUMA_GAME_SET_CAP(
        DLUMA_CAPABILITY_WORLD_POSITION,
        DLUMA_LUA54_CAP_WORLD_POSITION);
    DLUMA_GAME_SET_CAP(
        DLUMA_CAPABILITY_EVENTS_SENSOR,
        DLUMA_LUA54_CAP_EVENTS_SENSOR);
    DLUMA_GAME_SET_CAP(
        DLUMA_CAPABILITY_EVENTS_INTERACT,
        DLUMA_LUA54_CAP_EVENTS_INTERACT);
    DLUMA_GAME_SET_CAP(
        DLUMA_CAPABILITY_EVENTS_ENEMY_KILLED,
        DLUMA_LUA54_CAP_EVENTS_ENEMY_KILLED);
    DLUMA_GAME_SET_CAP(
        DLUMA_CAPABILITY_UI_SUBTITLES,
        DLUMA_LUA54_CAP_UI_SUBTITLES);
    DLUMA_GAME_SET_CAP(
        DLUMA_CAPABILITY_UI_QUEST_HUD,
        DLUMA_LUA54_CAP_UI_QUEST_HUD);
    DLUMA_GAME_SET_CAP(
        DLUMA_CAPABILITY_AUDIO_RADIO_DIALOG,
        DLUMA_LUA54_CAP_AUDIO_RADIO_DIALOG);
    DLUMA_GAME_SET_CAP(
        DLUMA_CAPABILITY_QUESTS_NATIVE,
        DLUMA_LUA54_CAP_QUEST_NATIVE_CONTROL);
    DLUMA_GAME_SET_CAP(
        DLUMA_CAPABILITY_ACTIONS_CHAIN,
        DLUMA_LUA54_CAP_ACTION_CHAIN);
#undef DLUMA_GAME_SET_CAP
    dluma_player_service_prepare_capabilities(registry);
    dluma_media_service_prepare_capabilities(registry);
    dluma_quest_phase_service_prepare_capabilities(registry);
    dluma_game_backend_set_capability(
        registry, DLUMA_CAPABILITY_UI_OVERLAY, 0, 0, "planned");
    dluma_game_backend_set_capability(
        registry, DLUMA_CAPABILITY_AUDIO_REGISTERED_CUES, 0, 0, "planned");
    dluma_game_backend_set_capability(
        registry, DLUMA_CAPABILITY_CONFIGURATION_MOD, 0, 0,
        "host configuration adapter unavailable");
    dluma_game_backend_set_capability(
        registry, DLUMA_CAPABILITY_STORAGE_MOD, 0, 0,
        "host storage adapter unavailable");
    dluma_game_backend_set_capability(
        registry, DLUMA_CAPABILITY_COMPANION_CONTENT, 0, 0,
        "host companion adapter unavailable");
}

static int dluma_game_copy_view(
    char* output,
    uint32_t capacity,
    DlumaStringView value)
{
    if (!output || !capacity || value.size >= capacity ||
        (!value.data && value.size))
        return 0;
    if (value.size)
        memcpy(output, value.data, (size_t)value.size);
    output[value.size] = 0;
    return 1;
}

static DlumaStatus dluma_game_write_buffer(
    DlumaStringBuffer* output,
    const char* value)
{
    uint64_t length = 0;
    if (!output || !value) return DLUMA_STATUS_INVALID_ARGUMENT;
    while (value[length]) ++length;
    output->required = length + 1U;
    if (!output->data || output->capacity < output->required)
        return DLUMA_STATUS_INSUFFICIENT_BUFFER;
    memcpy(output->data, value, (size_t)length);
    output->data[length] = 0;
    return DLUMA_STATUS_OK;
}

static DlumaStatus dluma_game_legacy_status(
    int succeeded,
    const DlumaLua54Error* error)
{
    if (succeeded) return DLUMA_STATUS_OK;
    if (!error || !error->code[0]) return DLUMA_STATUS_INTERNAL_ERROR;
    if (dluma_stricmp(error->code, "unsupported_capability") == 0 ||
        dluma_stricmp(error->code, "not_ready") == 0)
        return DLUMA_STATUS_UNAVAILABLE;
    if (dluma_stricmp(error->code, "invalid_argument") == 0)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if (dluma_stricmp(error->code, "wrong_thread") == 0)
        return DLUMA_STATUS_WRONG_THREAD;
    if (dluma_stricmp(error->code, "stale_handle") == 0)
        return DLUMA_STATUS_STALE_HANDLE;
    if (dluma_stricmp(error->code, "foreign_handle") == 0)
        return DLUMA_STATUS_FOREIGN_HANDLE;
    if (dluma_stricmp(error->code, "not_found") == 0)
        return DLUMA_STATUS_NOT_FOUND;
    if (dluma_stricmp(error->code, "limit_exceeded") == 0 ||
        dluma_stricmp(error->code, "quota_exceeded") == 0)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    return DLUMA_STATUS_INTERNAL_ERROR;
}

static DlumaStatus dluma_game_world_find(
    DlumaModOwner* owner,
    const DlumaWorldFindRequest* request,
    DlumaObjectResult* result)
{
    DlumaLua54Object legacy;
    DlumaLua54Error error;
    char exact_name[DLUMA_LUA54_MAX_OBJECT_NAME];
    char expected_class[DLUMA_LUA54_MAX_CLASS_NAME];
    DlumaStatus status;
    int succeeded;
    if (owner->max_handles &&
        owner->active_handles >= owner->max_handles)
        return DLUMA_STATUS_LIMIT_EXCEEDED;
    memset(&legacy, 0, sizeof(legacy));
    memset(&error, 0, sizeof(error));
    legacy.struct_size = sizeof(legacy);
    error.struct_size = sizeof(error);
    if (!dluma_game_copy_view(
            exact_name, DLUMA_ARRAY_COUNT(exact_name), request->name) ||
        !dluma_game_copy_view(
            expected_class,
            DLUMA_ARRAY_COUNT(expected_class),
            request->expected_class))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    succeeded = dluma_native_gameplay_world_find(
        owner->owner_id,
        owner->map_generation,
        exact_name,
        expected_class,
        &legacy,
        &error);
    status = dluma_game_legacy_status(succeeded, &error);
    if (status != DLUMA_STATUS_OK) return status;
    result->handle.owner_id = legacy.owner_id;
    result->handle.handle_id = legacy.handle_id;
    result->handle.generation = legacy.map_generation;
    result->handle.kind = 1U;
    status = dluma_game_write_buffer(&result->name, legacy.name);
    if (status != DLUMA_STATUS_OK) return status;
    status = dluma_game_write_buffer(
        &result->class_name, legacy.class_name);
    if (status == DLUMA_STATUS_OK)
        ++owner->active_handles;
    return status;
}

static DlumaStatus dluma_game_world_vector(
    DlumaModOwner* owner,
    const DlumaObjectRequest* request,
    DlumaVec3Result* result,
    int forward)
{
    DlumaLua54Vec3 legacy;
    DlumaLua54Error error;
    DlumaStatus status;
    int succeeded;
    memset(&legacy, 0, sizeof(legacy));
    memset(&error, 0, sizeof(error));
    legacy.struct_size = sizeof(legacy);
    error.struct_size = sizeof(error);
    if (forward) {
        succeeded = dluma_native_gameplay_world_get_forward(
            owner->owner_id,
            request->handle.handle_id,
            owner->map_generation,
            &legacy,
            &error);
    } else {
        succeeded = dluma_native_gameplay_world_get_position(
            owner->owner_id,
            request->handle.handle_id,
            owner->map_generation,
            &legacy,
            &error);
    }
    status = dluma_game_legacy_status(succeeded, &error);
    if (status == DLUMA_STATUS_OK) {
        result->value.x = legacy.x;
        result->value.y = legacy.y;
        result->value.z = legacy.z;
    }
    return status;
}

static DlumaStatus dluma_game_world_enabled(
    DlumaModOwner* owner,
    const DlumaObjectRequest* request,
    DlumaBoolResult* result)
{
    DlumaLua54Error error;
    int enabled = 0;
    int succeeded;
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    succeeded = dluma_native_gameplay_world_get_enabled(
        owner->owner_id,
        request->handle.handle_id,
        owner->map_generation,
        &enabled,
        &error);
    result->value = enabled ? 1U : 0U;
    return dluma_game_legacy_status(succeeded, &error);
}

static DlumaStatus dluma_game_world_set_enabled(
    DlumaModOwner* owner,
    const DlumaSetEnabledManyRequest* request,
    DlumaSetEnabledManyResult* result)
{
    DlumaLua54SetEnabledRequest legacy_request;
    DlumaLua54SetEnabledResult legacy_result;
    DlumaLua54Error error;
    uint32_t index;
    int succeeded;
    memset(&legacy_request, 0, sizeof(legacy_request));
    memset(&legacy_result, 0, sizeof(legacy_result));
    memset(&error, 0, sizeof(error));
    legacy_request.struct_size = sizeof(legacy_request);
    legacy_request.owner_id = owner->owner_id;
    legacy_request.map_generation = owner->map_generation;
    legacy_request.requested_count = request->name_count;
    legacy_request.enabled = request->enabled;
    legacy_request.atomic = request->atomic;
    legacy_request.best_effort = request->atomic ? 0U : 1U;
    legacy_result.struct_size = sizeof(legacy_result);
    error.struct_size = sizeof(error);
    for (index = 0; index < request->name_count; ++index) {
        if (!dluma_game_copy_view(
                legacy_request.names[index],
                DLUMA_LUA54_MAX_OBJECT_NAME,
                request->names[index]))
            return DLUMA_STATUS_INVALID_ARGUMENT;
    }
    succeeded = dluma_native_gameplay_world_set_enabled(
        owner->owner_id,
        &legacy_request,
        &legacy_result,
        &error);
    result->requested_count = legacy_result.requested_count;
    result->changed_count = legacy_result.changed_count;
    result->unchanged_count = legacy_result.unchanged_count;
    result->partial = legacy_result.partial;
    return dluma_game_legacy_status(succeeded, &error);
}

static DlumaStatus dluma_game_sensor_inside(
    DlumaModOwner* owner,
    const DlumaNamedRequest* request,
    DlumaBoolResult* result)
{
    char exact_name[DLUMA_LUA54_MAX_OBJECT_NAME];
    DlumaLua54Error error;
    int inside = 0;
    int succeeded;
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_game_copy_view(
            exact_name, DLUMA_ARRAY_COUNT(exact_name), request->name))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    succeeded = dluma_native_gameplay_sensor_inside(
        owner->owner_id,
        owner->map_generation,
        exact_name,
        &inside,
        &error);
    result->value = inside ? 1U : 0U;
    return dluma_game_legacy_status(succeeded, &error);
}

static DlumaStatus dluma_game_subtitle_show(
    const DlumaSubtitleRequest* request,
    DlumaBoolResult* result)
{
    DlumaLua54SubtitleRequest legacy;
    DlumaLua54Error error;
    int succeeded;
    memset(&legacy, 0, sizeof(legacy));
    memset(&error, 0, sizeof(error));
    legacy.struct_size = sizeof(legacy);
    legacy.force = request->force;
    legacy.duration_seconds = request->duration_seconds;
    error.struct_size = sizeof(error);
    if (!dluma_game_copy_view(
            legacy.text, DLUMA_ARRAY_COUNT(legacy.text), request->text))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    succeeded = dluma_native_gameplay_subtitle_show(&legacy, &error);
    result->value = succeeded ? 1U : 0U;
    return dluma_game_legacy_status(succeeded, &error);
}

static DlumaStatus dluma_game_subtitle_update(
    const DlumaSubtitleUpdateRequest* request,
    DlumaBoolResult* result)
{
    DlumaLua54SubtitleUpdateRequest legacy;
    DlumaLua54Error error;
    int succeeded;
    memset(&legacy, 0, sizeof(legacy));
    memset(&error, 0, sizeof(error));
    legacy.struct_size = sizeof(legacy);
    legacy.fade = request->fade;
    error.struct_size = sizeof(error);
    if (!dluma_game_copy_view(
            legacy.text, DLUMA_ARRAY_COUNT(legacy.text), request->text))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    succeeded = dluma_native_gameplay_subtitle_update(&legacy, &error);
    result->value = succeeded ? 1U : 0U;
    return dluma_game_legacy_status(succeeded, &error);
}

static DlumaStatus dluma_game_radio_dialog(
    const DlumaRadioDialogRequest* request,
    DlumaBoolResult* result)
{
    DlumaLua54RadioDialogRequest legacy;
    DlumaLua54Error error;
    int succeeded;
    memset(&legacy, 0, sizeof(legacy));
    memset(&error, 0, sizeof(error));
    legacy.struct_size = sizeof(legacy);
    error.struct_size = sizeof(error);
    if (!dluma_game_copy_view(
            legacy.speaker,
            DLUMA_ARRAY_COUNT(legacy.speaker),
            request->speaker) ||
        !dluma_game_copy_view(
            legacy.group,
            DLUMA_ARRAY_COUNT(legacy.group),
            request->group))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    succeeded = dluma_native_gameplay_radio_dialog(&legacy, &error);
    result->value = succeeded ? 1U : 0U;
    return dluma_game_legacy_status(succeeded, &error);
}

static DlumaStatus dluma_game_action_chain(
    DlumaModOwner* owner,
    const DlumaNamedRequest* request,
    DlumaBoolResult* result)
{
    char exact_name[DLUMA_LUA54_MAX_OBJECT_NAME];
    DlumaLua54Error error;
    int succeeded;
    memset(&error, 0, sizeof(error));
    error.struct_size = sizeof(error);
    if (!dluma_game_copy_view(
            exact_name, DLUMA_ARRAY_COUNT(exact_name), request->name))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    succeeded = dluma_native_gameplay_action_run_chain(
        owner->owner_id,
        owner->map_generation,
        exact_name,
        &error);
    result->value = succeeded ? 1U : 0U;
    return dluma_game_legacy_status(succeeded, &error);
}

static DlumaStatus dluma_game_quest_advance(
    DlumaModOwner* owner,
    const DlumaQuestTransitionRequest* request,
    DlumaBoolResult* result)
{
    DlumaLua54QuestRequest legacy_request;
    DlumaLua54QuestResult legacy_result;
    DlumaLua54Error error;
    int succeeded;
    memset(&legacy_request, 0, sizeof(legacy_request));
    memset(&legacy_result, 0, sizeof(legacy_result));
    memset(&error, 0, sizeof(error));
    legacy_request.struct_size = sizeof(legacy_request);
    legacy_request.owner_id = owner->owner_id;
    legacy_request.map_generation = owner->map_generation;
    legacy_request.operation = DLUMA_LUA54_QUEST_JUMP_TO_PHASE;
    legacy_result.struct_size = sizeof(legacy_result);
    error.struct_size = sizeof(error);
    if (!dluma_game_copy_view(
            legacy_request.native_quest_id,
            DLUMA_ARRAY_COUNT(legacy_request.native_quest_id),
            request->quest_id) ||
        !dluma_game_copy_view(
            legacy_request.native_phase_id,
            DLUMA_ARRAY_COUNT(legacy_request.native_phase_id),
            request->phase_id))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    succeeded = dluma_native_gameplay_quest_mutate(
        owner->owner_id,
        &legacy_request,
        &legacy_result,
        &error);
    result->value = succeeded && legacy_result.changed ? 1U : 0U;
    return dluma_game_legacy_status(succeeded, &error);
}

static DlumaStatus dluma_game_backend_invoke(
    void* user,
    DlumaModOwner* owner,
    uint32_t operation_id,
    const void* request,
    uint32_t request_size,
    void* result,
    uint32_t result_size)
{
    (void)request_size;
    (void)result_size;
    switch (operation_id) {
    case 1001U: {
        const DlumaLogRequest* log = (const DlumaLogRequest*)request;
        char level[17];
        char message[1025];
        if (!dluma_game_copy_view(level, DLUMA_ARRAY_COUNT(level), log->level) ||
            !dluma_game_copy_view(
                message, DLUMA_ARRAY_COUNT(message), log->message))
            return DLUMA_STATUS_INVALID_ARGUMENT;
        dluma_log_line(level[0] ? level : "INFO", message);
        return DLUMA_STATUS_OK;
    }
    case 1003U:
        ((DlumaU64Result*)result)->value = GetTickCount64();
        return DLUMA_STATUS_OK;
    case 1004U:
    case 1005U:
    case 1006U:
    case 4001U:
    case 5003U:
    case 5004U:
    case 9001U:
    case 9002U:
    case 10001U:
    case 10002U:
    case 10003U:
    case 11001U:
    case 11002U:
        return dluma_game_host_context_invoke(
            (DlumaGameHostContext*)user,
            operation_id,
            request,
            result);
    case 12001U:
    case 12002U:
    case 12003U:
        return dluma_quest_phase_service_invoke(
            owner, operation_id, request, result);
    case 2001U:
        return dluma_game_world_find(
            owner,
            (const DlumaWorldFindRequest*)request,
            (DlumaObjectResult*)result);
    case 2002U:
        return dluma_game_world_vector(
            owner,
            (const DlumaObjectRequest*)request,
            (DlumaVec3Result*)result,
            0);
    case 2003U:
        return dluma_game_world_vector(
            owner,
            (const DlumaObjectRequest*)request,
            (DlumaVec3Result*)result,
            1);
    case 2004U:
        return dluma_game_world_enabled(
            owner,
            (const DlumaObjectRequest*)request,
            (DlumaBoolResult*)result);
    case 2005U:
        return dluma_game_world_set_enabled(
            owner,
            (const DlumaSetEnabledManyRequest*)request,
            (DlumaSetEnabledManyResult*)result);
    case 3001U:
    case 3002U:
    case 3003U:
    case 3004U:
    case 3005U:
    case 3006U:
    case 3007U:
    case 3008U:
    case 3009U:
    case 3010U:
    case 3011U:
        return dluma_player_service_invoke(
            owner, operation_id, request, result);
    case 3012U:
        return dluma_media_service_invoke(
            (DlumaGameHostContext*)user, owner,
            operation_id, request, result);
    case 4002U:
        return dluma_game_sensor_inside(
            owner,
            (const DlumaNamedRequest*)request,
            (DlumaBoolResult*)result);
    case 4003U:
    case 4004U:
    case 4005U:
        return dluma_game_host_context_invoke(
            (DlumaGameHostContext*)user,
            operation_id,
            request,
            result);
    case 5001U:
        return dluma_game_subtitle_show(
            (const DlumaSubtitleRequest*)request,
            (DlumaBoolResult*)result);
    case 5002U:
        return dluma_game_subtitle_update(
            (const DlumaSubtitleUpdateRequest*)request,
            (DlumaBoolResult*)result);
    case 6001U:
        return dluma_game_radio_dialog(
            (const DlumaRadioDialogRequest*)request,
            (DlumaBoolResult*)result);
    case 6002U:
    case 6003U:
    case 6004U:
    case 6005U:
    case 6006U:
    case 6007U:
    case 6008U:
    case 6009U:
    case 6010U:
    case 6011U:
    case 6012U:
    case 6013U:
        return dluma_media_service_invoke(
            (DlumaGameHostContext*)user, owner,
            operation_id, request, result);
    case 7001U:
        return dluma_game_quest_advance(
            owner,
            (const DlumaQuestTransitionRequest*)request,
            (DlumaBoolResult*)result);
    case 8001U:
        return dluma_game_action_chain(
            owner,
            (const DlumaNamedRequest*)request,
            (DlumaBoolResult*)result);
    default:
        return DLUMA_STATUS_UNAVAILABLE;
    }
}

void dluma_game_backend_initialize(DlumaApiBackendV1* backend)
{
    dluma_game_backend_initialize_for_host(
        backend, 0);
}

void dluma_game_backend_initialize_for_host(
    DlumaApiBackendV1* backend,
    DlumaGameHostContext* host_context)
{
    if (!backend) return;
    memset(backend, 0, sizeof(*backend));
    backend->struct_size = (uint32_t)sizeof(*backend);
    backend->struct_version = 1U;
    backend->user = host_context;
    backend->current_thread_id = dluma_game_backend_thread_id;
    backend->invoke = dluma_game_backend_invoke;
}
