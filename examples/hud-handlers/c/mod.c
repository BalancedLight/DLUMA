#include "../../../sdk/c/dluma_api.h"
#include <string.h>
#include <stdio.h>

static int length(DlumaStringView value) { return value.data ? (int)(value.size > 127 ? 127 : value.size) : 0; }
static const char* data(DlumaStringView value) { return value.data ? value.data : ""; }
static DlumaStringView view(const char* s) { DlumaStringView v = {s, (uint64_t)strlen(s)}; return v; }
static DlumaStatus service(const DlumaApiV1* api, const char* name, void* table, uint32_t size)
{
    return api->get_service(api->context, view(name), 1, table, size);
}
static DlumaStatus register_handler(const DlumaApiV1* api, const char* name, uint64_t id)
{
    DlumaModHandlersV1 handlers = {0};
    DlumaModHandlerRegistrationRequest request = {0};
    DlumaTokenResult result = {0};
    DlumaStatus status = service(api, DLUMA_SERVICE_MOD_HANDLERS_V1, &handlers, sizeof handlers);
    if (status != DLUMA_STATUS_OK || !handlers.register_handler) return DLUMA_STATUS_UNAVAILABLE;
    request.struct_size = sizeof request; request.struct_version = 1;
    request.handler = view(name); request.callback_id = id;
    result.struct_size = sizeof result; result.struct_version = 1;
    result.call.struct_size = sizeof result.call; result.call.struct_version = 1;
    return handlers.register_handler(handlers.context, &request, &result);
}
static DlumaStatus DLUMA_API_CALL activate(const DlumaApiV1* api, const DlumaScopeActivationV1* scope)
{
    DlumaStatus status;
    (void)scope;
    status = register_handler(api, "hide-radar", 1);
    return status == DLUMA_STATUS_OK ? register_handler(api, "restore-radar", 2) : status;
}
static DlumaStatus DLUMA_API_CALL handle(const DlumaApiV1* api, const DlumaModHandlerEventV1* event)
{
    DlumaHudV1 hud = {0}; DlumaCoreV1 core = {0};
    DlumaHudVisibilityRequest request = {0}; DlumaBoolResult result = {0};
    DlumaStatus status;
    if (!event || event->struct_size < sizeof *event || (event->callback_id != 1 && event->callback_id != 2))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    if (service(api, DLUMA_SERVICE_CORE_V1, &core, sizeof core) == DLUMA_STATUS_OK && core.log) {
        DlumaLogRequest log = {0}; DlumaCallResult logged = {0}; char message[640];
        (void)snprintf(message, sizeof message,
            "HUD handler source=%u map=%u provider=%.*s handler=%.*s quest=%.*s phase=%.*s",
            event->source_kind, event->map_generation,
            length(event->provider), data(event->provider), length(event->handler), data(event->handler),
            length(event->quest_id), data(event->quest_id), length(event->phase_id), data(event->phase_id));
        log.struct_size = sizeof log; log.struct_version = 1; log.level = view("INFO");
        log.message = view(message);
        logged.struct_size = sizeof logged; logged.struct_version = 1;
        logged.call.struct_size = sizeof logged.call; logged.call.struct_version = 1;
        (void)core.log(core.context, &log, &logged);
        log.message = event->arguments_json;
        (void)core.log(core.context, &log, &logged);
    }
    status = service(api, DLUMA_SERVICE_HUD_V1, &hud, sizeof hud);
    if (status != DLUMA_STATUS_OK || !hud.hide || !hud.reset) return DLUMA_STATUS_UNAVAILABLE;
    request.struct_size = sizeof request; request.struct_version = 1;
    request.screen = view("HUD.xui"); request.target = view("radar");
    result.struct_size = sizeof result; result.struct_version = 1;
    result.call.struct_size = sizeof result.call; result.call.struct_version = 1;
    return event->callback_id == 1 ? hud.hide(hud.context, &request, &result) : hud.reset(hud.context, &request, &result);
}
static DlumaStatus DLUMA_API_CALL ready(const DlumaApiV1* api) { (void)api; return DLUMA_STATUS_OK; }
static void DLUMA_API_CALL deactivate(const DlumaApiV1* api, const DlumaScopeActivationV1* scope) { (void)api; (void)scope; }
static void DLUMA_API_CALL stop(const DlumaApiV1* api, uint32_t reason) { (void)api; (void)reason; }
static DlumaStatus DLUMA_API_CALL event_callback(const DlumaApiV1* api, const DlumaModEventV1* event) { (void)api; (void)event; return DLUMA_STATUS_OK; }
static const DlumaNativeModApiV2 mod_api = {
    sizeof(DlumaNativeModApiV2), DLUMA_NATIVE_MOD_ABI_V2,
    ready, ready, activate, deactivate, stop, event_callback, 0, handle
};
DLUMA_API_EXPORT const DlumaNativeModApiV2* DLUMA_API_CALL DLUMANativeMod_GetApiV2(void) { return &mod_api; }
