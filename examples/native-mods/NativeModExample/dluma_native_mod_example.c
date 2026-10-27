#include <windows.h>
#include "../../../sdk/c/dluma_api.h"

static DlumaCoreV1 g_core;
static DlumaQuestPhasesV1 g_quest_phases;
static uint64_t g_quest_phase_tokens[3];

static DlumaStringView dluma_example_view(const char* text)
{
    DlumaStringView value;
    uint64_t size = 0;
    while (text && text[size]) ++size;
    value.data = text;
    value.size = size;
    return value;
}

static DlumaStatus dluma_example_log(const char* message)
{
    DlumaLogRequest request;
    DlumaCallResult result;
    if (!g_core.log) return DLUMA_STATUS_UNAVAILABLE;
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.level = dluma_example_view("INFO");
    request.message = dluma_example_view(message);
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    result.call.struct_size = (uint32_t)sizeof(result.call);
    result.call.struct_version = 1U;
    result.call.status = DLUMA_STATUS_OK;
    result.call.reserved = 0U;
    result.call.error = 0;
    return g_core.log(g_core.context, &request, &result);
}

static void dluma_example_unregister_quest_phase(uint64_t* token)
{
    DlumaTokenRequest request = {0};
    DlumaBoolResult result = {0};
    if (!token || !*token || !g_quest_phases.unregister) return;
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.token = *token;
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    result.call.struct_size = (uint32_t)sizeof(result.call);
    result.call.struct_version = 1U;
    (void)g_quest_phases.unregister(
        g_quest_phases.context, &request, &result);
    *token = 0U;
}

static int dluma_example_register_quest_phase(
    int provider,
    const char* key,
    uint64_t callback_id,
    uint64_t* token)
{
    DlumaQuestPhaseRegistrationRequest request = {0};
    DlumaTokenResult result = {0};
    DlumaStatus status;
    if (!key || !token ||
        (provider && !g_quest_phases.register_provider) ||
        (!provider && !g_quest_phases.register_observer))
        return 0;
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.key = dluma_example_view(key);
    request.callback_id = callback_id;
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    result.call.struct_size = (uint32_t)sizeof(result.call);
    result.call.struct_version = 1U;
    status = provider
        ? g_quest_phases.register_provider(
              g_quest_phases.context, &request, &result)
        : g_quest_phases.register_observer(
              g_quest_phases.context, &request, &result);
    if (status != DLUMA_STATUS_OK || !result.token) return 0;
    *token = result.token;
    return 1;
}

static DlumaStatus DLUMA_API_CALL dluma_example_pre_game(
    const DlumaApiV1* api)
{
    DlumaStatus status;
    if (!api || api->struct_size < sizeof(*api) ||
        api->abi_version != DLUMA_API_ABI_VERSION ||
        api->api_major != 1U || !api->get_service)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    status = api->get_service(
        api->context,
        dluma_example_view(DLUMA_SERVICE_CORE_V1),
        1U,
        &g_core,
        (uint32_t)sizeof(g_core));
    if (status != DLUMA_STATUS_OK ||
        g_core.struct_size < sizeof(g_core) ||
        g_core.version != 1U)
        return status == DLUMA_STATUS_OK
            ? DLUMA_STATUS_INTERNAL_ERROR
            : status;
    (void)dluma_example_log(
        "Example native MOD pre-game bootstrap through DLUMA API 1.0.");
    status = api->get_service(
        api->context,
        dluma_example_view(DLUMA_SERVICE_QUEST_PHASES_V1),
        1U,
        &g_quest_phases,
        (uint32_t)sizeof(g_quest_phases));
    if (status != DLUMA_STATUS_OK ||
        g_quest_phases.struct_size < sizeof(g_quest_phases) ||
        g_quest_phases.version != 1U) {
        (void)dluma_example_log(
            "Example native MOD quest-phase service is unavailable on this host.");
        return DLUMA_STATUS_OK;
    }
    if (!dluma_example_register_quest_phase(
            1, "example.native.phase", 1U,
            &g_quest_phase_tokens[0]) ||
        !dluma_example_register_quest_phase(
            0, "example.native.dllmod:example.native.phase", 2U,
            &g_quest_phase_tokens[1]) ||
        !dluma_example_register_quest_phase(
            0, "example.global_loader_status:example.lua.phase", 3U,
            &g_quest_phase_tokens[2])) {
        dluma_example_unregister_quest_phase(&g_quest_phase_tokens[2]);
        dluma_example_unregister_quest_phase(&g_quest_phase_tokens[1]);
        dluma_example_unregister_quest_phase(&g_quest_phase_tokens[0]);
        (void)dluma_example_log(
            "Example native MOD quest-phase registration was rejected.");
        return DLUMA_STATUS_OK;
    }
    return dluma_example_log(
        "Example native MOD quest-phase provider and observers registered.");
}

static DlumaStatus DLUMA_API_CALL dluma_example_post_game(
    const DlumaApiV1* api)
{
    (void)api;
    return dluma_example_log("Example native MOD game services are ready.");
}

static DlumaStatus DLUMA_API_CALL dluma_example_scope_activated(
    const DlumaApiV1* api,
    const DlumaScopeActivationV1* activation)
{
    (void)api;
    if (!activation ||
        activation->struct_size < sizeof(*activation))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    return dluma_example_log("Example native MOD scope activated.");
}

static void DLUMA_API_CALL dluma_example_scope_deactivated(
    const DlumaApiV1* api,
    const DlumaScopeActivationV1* activation)
{
    (void)api;
    (void)activation;
    (void)dluma_example_log("Example native MOD scope deactivated.");
}

static void DLUMA_API_CALL dluma_example_shutdown(
    const DlumaApiV1* api,
    uint32_t stop_reason)
{
    (void)api;
    (void)stop_reason;
    dluma_example_unregister_quest_phase(&g_quest_phase_tokens[2]);
    dluma_example_unregister_quest_phase(&g_quest_phase_tokens[1]);
    dluma_example_unregister_quest_phase(&g_quest_phase_tokens[0]);
    (void)dluma_example_log("Example native MOD shutdown.");
}

static DlumaStatus DLUMA_API_CALL dluma_example_event(
    const DlumaApiV1* api,
    const DlumaModEventV1* event_value)
{
    (void)api;
    if (!event_value ||
        event_value->struct_size < sizeof(*event_value))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    return DLUMA_STATUS_OK;
}

static DlumaStatus DLUMA_API_CALL dluma_example_quest_phase_event(
    const DlumaApiV1* api,
    const DlumaQuestPhaseNativeEventV1* event_value)
{
    (void)api;
    if (!event_value ||
        event_value->struct_size < sizeof(*event_value) ||
        event_value->struct_version != 1U ||
        !event_value->quest_id.data || !event_value->quest_id.size ||
        !event_value->phase_id.data || !event_value->phase_id.size ||
        !event_value->contract_id.data || !event_value->contract_id.size ||
        !event_value->provider.data || !event_value->provider.size ||
        !event_value->handler.data || !event_value->handler.size ||
        !event_value->binding_hash.data ||
        event_value->binding_hash.size != 64U ||
        !event_value->arguments_json.data ||
        !event_value->arguments_json.size ||
        !event_value->local_peer)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if (event_value->callback_id == 1U &&
        event_value->kind == DLUMA_MOD_EVENT_QUEST_PHASE_PROVIDER)
        return dluma_example_log(
            "Example native MOD handled its primary quest phase.");
    if (event_value->callback_id == 2U &&
        event_value->kind == DLUMA_MOD_EVENT_QUEST_PHASE_OBSERVER)
        return dluma_example_log(
            "Example native MOD observed its quest-phase contract.");
    if (event_value->callback_id == 3U &&
        event_value->kind == DLUMA_MOD_EVENT_QUEST_PHASE_OBSERVER)
        return dluma_example_log(
            "Example native MOD observed the Lua quest-phase contract.");
    return DLUMA_STATUS_INVALID_ARGUMENT;
}

static const DlumaNativeModApiV2 g_example_api = {
    (uint32_t)sizeof(DlumaNativeModApiV2),
    DLUMA_NATIVE_MOD_ABI_V2,
    dluma_example_pre_game,
    dluma_example_post_game,
    dluma_example_scope_activated,
    dluma_example_scope_deactivated,
    dluma_example_shutdown,
    dluma_example_event,
    dluma_example_quest_phase_event
};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}

DLUMA_API_EXPORT const DlumaNativeModApiV2* DLUMA_API_CALL
DLUMANativeMod_GetApiV2(void)
{
    return &g_example_api;
}
