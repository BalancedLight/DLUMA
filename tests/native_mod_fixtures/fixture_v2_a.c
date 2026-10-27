#include <windows.h>
#include "../../sdk/c/dluma_api.h"

static DlumaCoreV1 g_core;

static DlumaStringView fixture_string(const char *value)
{
    DlumaStringView view;
    const char *cursor = value;
    view.data = value;
    while (*cursor) ++cursor;
    view.size = (uint64_t)(cursor - value);
    return view;
}

static DlumaStatus fixture_log(const DlumaApiV1 *api, const char *message)
{
    DlumaLogRequest request;
    DlumaCallResult result;
    DlumaStatus status;
    if (!api || !api->get_service) return DLUMA_STATUS_INVALID_ARGUMENT;
    if (!g_core.log) {
        g_core.struct_size = (uint32_t)sizeof(g_core);
        status = api->get_service(
            api->context, fixture_string(DLUMA_SERVICE_CORE_V1), 1U,
            &g_core, (uint32_t)sizeof(g_core));
        if (status != DLUMA_STATUS_OK || !g_core.log) return status;
    }
    request.struct_size = (uint32_t)sizeof(request);
    request.struct_version = 1U;
    request.level = fixture_string("INFO");
    request.message = fixture_string(message);
    result.struct_size = (uint32_t)sizeof(result);
    result.struct_version = 1U;
    result.call.status = DLUMA_STATUS_INTERNAL_ERROR;
    return g_core.log(g_core.context, &request, &result);
}

static DlumaStatus DLUMA_API_CALL fixture_pre(const DlumaApiV1 *api)
{
    return fixture_log(api, "A");
}

static DlumaStatus DLUMA_API_CALL fixture_post(const DlumaApiV1 *api)
{
    return fixture_log(api, "a");
}

static DlumaStatus DLUMA_API_CALL fixture_activate(
    const DlumaApiV1 *api,
    const DlumaScopeActivationV1 *activation)
{
    if (!activation || activation->scope != DLUMA_SCOPE_GLOBAL)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    return fixture_log(api, "1");
}

static void DLUMA_API_CALL fixture_deactivate(
    const DlumaApiV1 *api,
    const DlumaScopeActivationV1 *activation)
{
    (void)activation;
    (void)fixture_log(api, "2");
}

static void DLUMA_API_CALL fixture_shutdown(
    const DlumaApiV1 *api,
    uint32_t stop_reason)
{
    (void)stop_reason;
    (void)fixture_log(api, "x");
}

static DlumaStatus DLUMA_API_CALL fixture_event(
    const DlumaApiV1* api,
    const DlumaModEventV1* event_value)
{
    (void)api;
    (void)event_value;
    return DLUMA_STATUS_OK;
}

static const DlumaNativeModApiV2 g_api = {
    sizeof(g_api),
    DLUMA_NATIVE_MOD_ABI_V2,
    fixture_pre,
    fixture_post,
    fixture_activate,
    fixture_deactivate,
    fixture_shutdown,
    fixture_event
};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}

__declspec(dllexport) const DlumaNativeModApiV2 *DLUMA_API_CALL
DLUMANativeMod_GetApiV2(void)
{
    return &g_api;
}
