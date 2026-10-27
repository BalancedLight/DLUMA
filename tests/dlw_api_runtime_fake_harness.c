#include "../src/core/dlw_api_runtime.h"

#include <stdio.h>
#include <string.h>

typedef struct FakeBackend {
    uint32_t thread_id;
    uint32_t invoke_count;
    uint32_t log_count;
    uint64_t next_token;
} FakeBackend;

typedef struct ResultBuffers {
    DlumaError error;
    char code[64];
    char message[256];
} ResultBuffers;

static uint32_t fake_thread_id(void* user)
{
    return ((FakeBackend*)user)->thread_id;
}

static int copy_text(DlumaStringBuffer* output, DlumaStringView value)
{
    output->required = value.size + 1U;
    if (!output->data || output->capacity < output->required) return 0;
    memcpy(output->data, value.data, (size_t)value.size);
    output->data[value.size] = 0;
    return 1;
}

static DlumaStatus fake_invoke(
    void* user,
    DlumaModOwner* owner,
    uint32_t operation_id,
    const void* request,
    uint32_t request_size,
    void* result,
    uint32_t result_size)
{
    FakeBackend* backend = (FakeBackend*)user;
    (void)request_size;
    (void)result_size;
    ++backend->invoke_count;
    switch (operation_id) {
    case 1001U:
        ++backend->log_count;
        return DLUMA_STATUS_OK;
    case 1003U:
        ((DlumaU64Result*)result)->value = 123456U;
        return DLUMA_STATUS_OK;
    case 1004U:
    case 1005U:
    case 4001U:
    case 4003U:
    case 4004U:
    case 4005U:
    case 9002U:
        ((DlumaTokenResult*)result)->token = ++backend->next_token;
        return DLUMA_STATUS_OK;
    case 1006U:
        ((DlumaBoolResult*)result)->value =
            ((const DlumaTokenRequest*)request)->token != 0;
        return DLUMA_STATUS_OK;
    case 2001U: {
        const DlumaWorldFindRequest* find =
            (const DlumaWorldFindRequest*)request;
        DlumaObjectResult* object = (DlumaObjectResult*)result;
        static const char class_name[] = "SensorArea";
        object->handle.owner_id = owner->owner_id;
        object->handle.handle_id = 0x1234U;
        object->handle.generation = owner->map_generation;
        object->handle.kind = 1U;
        if (!copy_text(&object->name, find->name) ||
            !copy_text(
                &object->class_name,
                (DlumaStringView){class_name, sizeof(class_name) - 1U}))
            return DLUMA_STATUS_INSUFFICIENT_BUFFER;
        return DLUMA_STATUS_OK;
    }
    case 2002U:
    case 3002U:
        ((DlumaVec3Result*)result)->value =
            (DlumaVec3){10.0f, 20.0f, 30.0f};
        return DLUMA_STATUS_OK;
    case 3001U: {
        DlumaObjectResult* player = (DlumaObjectResult*)result;
        static const char name[] = "LocalPlayer";
        static const char class_name[] = "PlayerDI";
        player->handle.owner_id = owner->owner_id;
        player->handle.handle_id = 0x777U;
        player->handle.generation = owner->map_generation;
        player->handle.kind = 2U;
        if (!copy_text(
                &player->name,
                (DlumaStringView){name, sizeof(name) - 1U}) ||
            !copy_text(
                &player->class_name,
                (DlumaStringView){class_name, sizeof(class_name) - 1U}))
            return DLUMA_STATUS_INSUFFICIENT_BUFFER;
        return DLUMA_STATUS_OK;
    }
    case 4002U:
        ((DlumaBoolResult*)result)->value = 1U;
        return DLUMA_STATUS_OK;
    case 9001U:
        ((DlumaScalarResult*)result)->value.kind = DLUMA_SCALAR_BOOLEAN;
        ((DlumaScalarResult*)result)->value.integer_value = 1;
        return DLUMA_STATUS_OK;
    default:
        return DLUMA_STATUS_UNAVAILABLE;
    }
}

static void init_error(ResultBuffers* buffers)
{
    memset(buffers, 0, sizeof(*buffers));
    buffers->error.struct_size = sizeof(buffers->error);
    buffers->error.struct_version = 1U;
    buffers->error.code.data = buffers->code;
    buffers->error.code.capacity = sizeof(buffers->code);
    buffers->error.message.data = buffers->message;
    buffers->error.message.capacity = sizeof(buffers->message);
}

#define INIT_RESULT(value, buffers)                  \
    do {                                             \
        memset(&(value), 0, sizeof(value));          \
        (value).struct_size = sizeof(value);         \
        (value).struct_version = 1U;                 \
        (value).call.error = &(buffers).error;       \
    } while (0)

#define INIT_REQUEST(value)                          \
    do {                                             \
        memset(&(value), 0, sizeof(value));          \
        (value).struct_size = sizeof(value);         \
        (value).struct_version = 1U;                 \
    } while (0)

static int passed;
static int failed;

static void check(int condition, const char* name)
{
    if (condition) {
        ++passed;
        printf("[PASS] %s\n", name);
    } else {
        ++failed;
        printf("[FAIL] %s\n", name);
    }
}

static int initialize_runtime(
    uint32_t language,
    uint64_t owner_id,
    FakeBackend* fake,
    DlumaModOwner* owner,
    DlumaCapabilityRegistry* capabilities,
    DlumaApiRuntime* runtime)
{
    static const uint32_t access[] = {
        DLUMA_ACCESS_WORLD_QUERY,
        DLUMA_ACCESS_EVENTS_SENSOR,
        DLUMA_ACCESS_CONFIGURATION_READ
    };
    static const char stable_id[] = "test.parity";
    DlumaModOwnerDefinition definition;
    DlumaApiBackendV1 backend;
    DlumaStringView ready = {"ready", 5U};
    INIT_REQUEST(definition);
    definition.owner_id = owner_id;
    definition.language = language;
    definition.scope = DLUMA_SCOPE_GLOBAL;
    definition.map_generation = 7U;
    definition.game_thread_id = fake->thread_id;
    definition.stable_id =
        (DlumaStringView){stable_id, sizeof(stable_id) - 1U};
    definition.access_ids = access;
    definition.access_count = sizeof(access) / sizeof(access[0]);
    definition.max_handles = 32U;
    definition.max_subscriptions = 32U;
    definition.max_timers = 32U;
    definition.max_companion_artifacts = 8U;
    if (dluma_mod_owner_initialize(owner, &definition) != DLUMA_STATUS_OK)
        return 0;
    dluma_capability_registry_initialize(capabilities);
    if (dluma_capability_registry_set(
            capabilities, DLUMA_CAPABILITY_CORE_RUNTIME, 1, 1, ready) ||
        dluma_capability_registry_set(
            capabilities, DLUMA_CAPABILITY_CORE_TIMERS, 1, 1, ready) ||
        dluma_capability_registry_set(
            capabilities, DLUMA_CAPABILITY_WORLD_FIND, 1, 1, ready) ||
        dluma_capability_registry_set(
            capabilities, DLUMA_CAPABILITY_WORLD_POSITION, 1, 1, ready) ||
        dluma_capability_registry_set(
            capabilities, DLUMA_CAPABILITY_PLAYER_READ, 1, 1, ready) ||
        dluma_capability_registry_set(
            capabilities, DLUMA_CAPABILITY_PLAYER_TRANSFORM, 1, 1, ready) ||
        dluma_capability_registry_set(
            capabilities, DLUMA_CAPABILITY_EVENTS_SENSOR, 1, 1, ready) ||
        dluma_capability_registry_set(
            capabilities, DLUMA_CAPABILITY_CONFIGURATION_MOD, 1, 1, ready))
        return 0;
    memset(&backend, 0, sizeof(backend));
    backend.struct_size = sizeof(backend);
    backend.struct_version = 1U;
    backend.user = fake;
    backend.current_thread_id = fake_thread_id;
    backend.invoke = fake_invoke;
    return dluma_api_runtime_initialize(
               runtime, owner, capabilities, &backend) == DLUMA_STATUS_OK;
}

static DlumaStatus call_world_find(
    DlumaApiRuntime* runtime,
    const char* name,
    uint64_t name_size,
    DlumaObjectResult* result,
    ResultBuffers* buffers)
{
    DlumaWorldV1 world;
    DlumaWorldFindRequest request;
    char object_name[96];
    char class_name[96];
    memset(&world, 0, sizeof(world));
    world.struct_size = sizeof(world);
    INIT_REQUEST(request);
    request.name = (DlumaStringView){name, name_size};
    INIT_RESULT(*result, *buffers);
    result->name.data = object_name;
    result->name.capacity = sizeof(object_name);
    result->class_name.data = class_name;
    result->class_name.capacity = sizeof(class_name);
    if (runtime->api.get_service(
            runtime->api.context,
            (DlumaStringView){DLUMA_SERVICE_WORLD_V1,
                            sizeof(DLUMA_SERVICE_WORLD_V1) - 1U},
            1U,
            &world,
            sizeof(world)) != DLUMA_STATUS_OK)
        return DLUMA_STATUS_INTERNAL_ERROR;
    return world.find(world.context, &request, result);
}

int main(void)
{
    FakeBackend fake = {77U, 0U, 0U, 100U};
    DlumaModOwner lua_owner;
    DlumaModOwner csharp_owner;
    DlumaModOwner native_owner;
    DlumaCapabilityRegistry lua_caps;
    DlumaCapabilityRegistry csharp_caps;
    DlumaCapabilityRegistry native_caps;
    DlumaApiRuntime lua_runtime;
    DlumaApiRuntime csharp_runtime;
    DlumaApiRuntime native_runtime;
    ResultBuffers error;
    DlumaObjectResult lua_object;
    DlumaObjectResult csharp_object;
    DlumaObjectResult native_object;
    DlumaObjectHandle lua_handle;
    DlumaStatus lua_status;
    DlumaStatus csharp_status;
    DlumaStatus native_status;

    check(
        initialize_runtime(
            DLUMA_MOD_LANGUAGE_LUA, 1U, &fake,
            &lua_owner, &lua_caps, &lua_runtime),
        "lua_owner_initializes");
    check(
        initialize_runtime(
            DLUMA_MOD_LANGUAGE_CSHARP, 2U, &fake,
            &csharp_owner, &csharp_caps, &csharp_runtime),
        "csharp_owner_initializes");
    check(
        initialize_runtime(
            DLUMA_MOD_LANGUAGE_NATIVE, 3U, &fake,
            &native_owner, &native_caps, &native_runtime),
        "native_owner_initializes");

    init_error(&error);
    lua_status = call_world_find(
        &lua_runtime, "DLUMA_Sensor", 10U, &lua_object, &error);
    init_error(&error);
    csharp_status = call_world_find(
        &csharp_runtime, "DLUMA_Sensor", 10U, &csharp_object, &error);
    init_error(&error);
    native_status = call_world_find(
        &native_runtime, "DLUMA_Sensor", 10U, &native_object, &error);
    lua_handle = lua_object.handle;
    check(
        lua_status == DLUMA_STATUS_OK &&
        csharp_status == DLUMA_STATUS_OK &&
        native_status == DLUMA_STATUS_OK,
        "cross_language_world_find_status_parity");
    check(
        lua_object.handle.handle_id == csharp_object.handle.handle_id &&
        csharp_object.handle.handle_id == native_object.handle.handle_id &&
        lua_object.handle.generation == csharp_object.handle.generation &&
        csharp_object.handle.generation == native_object.handle.generation,
        "cross_language_world_find_value_parity");
    check(
        lua_object.handle.owner_id != csharp_object.handle.owner_id &&
        csharp_object.handle.owner_id != native_object.handle.owner_id,
        "handles_remain_owner_bound");

    {
        static const char bad_name[] = {'B', 'a', 'd', 0, 'X'};
        uint32_t before = fake.invoke_count;
        init_error(&error);
        lua_status = call_world_find(
            &lua_runtime, bad_name, sizeof(bad_name), &lua_object, &error);
        check(
            lua_status == DLUMA_STATUS_INVALID_ARGUMENT &&
            fake.invoke_count == before &&
            strcmp(error.code, "invalid_argument") == 0,
            "embedded_nul_fails_before_backend");
    }

    {
        DlumaCapabilityQueryRequest request;
        DlumaCapabilityQueryResult result;
        DlumaCoreV1 core;
        char status_text[32];
        init_error(&error);
        INIT_REQUEST(request);
        request.capability_id = DLUMA_CAPABILITY_WORLD_FIND;
        INIT_RESULT(result, error);
        result.status.data = status_text;
        result.status.capacity = sizeof(status_text);
        memset(&core, 0, sizeof(core));
        core.struct_size = sizeof(core);
        check(
            lua_runtime.api.get_service(
                lua_runtime.api.context,
                (DlumaStringView){DLUMA_SERVICE_CORE_V1,
                                sizeof(DLUMA_SERVICE_CORE_V1) - 1U},
                1U, &core, sizeof(core)) == DLUMA_STATUS_OK &&
            core.capability_query(
                core.context, &request, &result) == DLUMA_STATUS_OK &&
            result.configured && result.ready &&
            strcmp(status_text, "ready") == 0,
            "versioned_capability_query");
    }

    {
        DlumaAudioV1 audio;
        DlumaAudioCuePlayRequest request;
        DlumaTokenResult result;
        uint32_t before = fake.invoke_count;
        init_error(&error);
        INIT_REQUEST(request);
        request.bank_token = 1U;
        request.cue = (DlumaStringView){"cue", 3U};
        request.initial_gain = 1.0f;
        INIT_RESULT(result, error);
        memset(&audio, 0, sizeof(audio));
        audio.struct_size = sizeof(audio);
        check(
            lua_runtime.api.get_service(
                lua_runtime.api.context,
                (DlumaStringView){DLUMA_SERVICE_AUDIO_V1,
                                sizeof(DLUMA_SERVICE_AUDIO_V1) - 1U},
                1U, &audio, sizeof(audio)) == DLUMA_STATUS_OK &&
            audio.cue_play(audio.context, &request, &result) ==
                DLUMA_STATUS_ACCESS_DENIED &&
            fake.invoke_count == before,
            "implemented_operation_is_permission_gated");
    }

    {
        DlumaQuestsV1 quests;
        DlumaQuestTransitionRequest request;
        DlumaBoolResult result;
        init_error(&error);
        INIT_REQUEST(request);
        request.quest_id = (DlumaStringView){"quest", 5U};
        INIT_RESULT(result, error);
        memset(&quests, 0, sizeof(quests));
        quests.struct_size = sizeof(quests);
        check(
            lua_runtime.api.get_service(
                lua_runtime.api.context,
                (DlumaStringView){DLUMA_SERVICE_QUESTS_V1,
                                sizeof(DLUMA_SERVICE_QUESTS_V1) - 1U},
                1U, &quests, sizeof(quests)) == DLUMA_STATUS_OK &&
            quests.start(quests.context, &request, &result) ==
                DLUMA_STATUS_REMOVED,
            "removed_operation_returns_removed");
    }

    {
        DlumaWorldV1 world;
        memset(&world, 0, sizeof(world));
        world.struct_size = sizeof(world);
        check(
            lua_runtime.api.get_service(
                lua_runtime.api.context,
                (DlumaStringView){"dluma.unknown.v1", 14U},
                1U, &world, sizeof(world)) == DLUMA_STATUS_NOT_FOUND,
            "unknown_service_is_rejected");
        check(
            lua_runtime.api.get_service(
                lua_runtime.api.context,
                (DlumaStringView){DLUMA_SERVICE_WORLD_V1,
                                sizeof(DLUMA_SERVICE_WORLD_V1) - 1U},
                2U, &world, sizeof(world)) == DLUMA_STATUS_INVALID_ARGUMENT,
            "unsupported_service_version_is_rejected");
        check(
            lua_runtime.api.get_service(
                lua_runtime.api.context,
                (DlumaStringView){DLUMA_SERVICE_WORLD_V1,
                                sizeof(DLUMA_SERVICE_WORLD_V1) - 1U},
                1U, &world, 4U) == DLUMA_STATUS_INSUFFICIENT_BUFFER,
            "truncated_service_header_is_rejected");
    }

    {
        DlumaObjectRequest request;
        DlumaVec3Result result;
        DlumaWorldV1 world;
        DlumaStatus foreign_status;
        DlumaStatus stale_status;
        init_error(&error);
        INIT_REQUEST(request);
        request.handle = lua_handle;
        request.handle.owner_id = csharp_owner.owner_id;
        INIT_RESULT(result, error);
        memset(&world, 0, sizeof(world));
        world.struct_size = sizeof(world);
        lua_runtime.api.get_service(
            lua_runtime.api.context,
            (DlumaStringView){DLUMA_SERVICE_WORLD_V1,
                            sizeof(DLUMA_SERVICE_WORLD_V1) - 1U},
            1U, &world, sizeof(world));
        foreign_status = world.position(world.context, &request, &result);
        check(
            foreign_status == DLUMA_STATUS_FOREIGN_HANDLE,
            "foreign_handle_is_rejected");
        request.handle.owner_id = lua_owner.owner_id;
        ++request.handle.generation;
        stale_status = world.position(world.context, &request, &result);
        check(
            stale_status == DLUMA_STATUS_STALE_HANDLE,
            "stale_handle_is_rejected");
    }

    {
        uint32_t before = fake.invoke_count;
        dluma_mod_owner_begin_revoke(&lua_owner);
        dluma_mod_owner_finish_revoke(&lua_owner);
        init_error(&error);
        lua_status = call_world_find(
            &lua_runtime, "DLUMA_Sensor", 10U, &lua_object, &error);
        check(
            lua_status == DLUMA_STATUS_OWNER_REVOKED &&
            fake.invoke_count == before,
            "revoked_owner_cannot_enter_backend");
    }

    {
        DlumaModOwner owner;
        DlumaModOwnerDefinition definition;
        uint32_t duplicate[] = {
            DLUMA_ACCESS_WORLD_QUERY,
            DLUMA_ACCESS_WORLD_QUERY
        };
        INIT_REQUEST(definition);
        definition.owner_id = 99U;
        definition.language = DLUMA_MOD_LANGUAGE_LUA;
        definition.scope = DLUMA_SCOPE_GLOBAL;
        definition.stable_id = (DlumaStringView){"duplicate", 9U};
        definition.access_ids = duplicate;
        definition.access_count = 2U;
        check(
            dluma_mod_owner_initialize(&owner, &definition) ==
                DLUMA_STATUS_INVALID_ARGUMENT,
            "duplicate_access_is_rejected");
    }

    printf(
        "DLUMA API runtime fake cases passed: %d; failed: %d\n",
        passed, failed);
    return failed ? 1 : 0;
}
