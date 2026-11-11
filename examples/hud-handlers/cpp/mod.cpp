#include "../../../sdk/cpp/dluma.hpp"
#include <cstdio>

static int length(DlumaStringView value) { return value.data ? static_cast<int>(value.size > 127 ? 127 : value.size) : 0; }
static const char* data(DlumaStringView value) { return value.data ? value.data : ""; }

static DlumaStatus DLUMA_API_CALL activate(const DlumaApiV1* api, const DlumaScopeActivationV1*)
{
    dluma::context context(api);
    auto handlers = context.mod_handlers();
    DlumaModHandlerRegistrationRequest request{};
    DlumaTokenResult result{};
    request.struct_size = sizeof request; request.struct_version = 1;
    result.struct_size = sizeof result; result.struct_version = 1;
    result.call.struct_size = sizeof result.call; result.call.struct_version = 1;
    request.handler = dluma::string_view("hide-radar"); request.callback_id = 1;
    auto status = handlers.register_handler(request, result);
    if (status != DLUMA_STATUS_OK) return status;
    request.handler = dluma::string_view("restore-radar"); request.callback_id = 2;
    return handlers.register_handler(request, result);
}
static DlumaStatus DLUMA_API_CALL handle(const DlumaApiV1* api, const DlumaModHandlerEventV1* event)
{
    if (!event || event->struct_size < sizeof *event || (event->callback_id != 1 && event->callback_id != 2))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    dluma::context context(api);
    DlumaLogRequest log{}; DlumaCallResult logged{}; char message[640];
    std::snprintf(message, sizeof message,
        "HUD handler source=%u map=%u provider=%.*s handler=%.*s quest=%.*s phase=%.*s",
        event->source_kind, event->map_generation,
        length(event->provider), data(event->provider), length(event->handler), data(event->handler),
        length(event->quest_id), data(event->quest_id), length(event->phase_id), data(event->phase_id));
    log.struct_size = sizeof log; log.struct_version = 1;
    log.level = dluma::string_view("INFO"); log.message = dluma::string_view(message);
    logged.struct_size = sizeof logged; logged.struct_version = 1;
    logged.call.struct_size = sizeof logged.call; logged.call.struct_version = 1;
    (void)context.core().log(log, logged);
    log.message = event->arguments_json;
    (void)context.core().log(log, logged);
    DlumaHudVisibilityRequest request{}; DlumaBoolResult result{};
    request.struct_size = sizeof request; request.struct_version = 1;
    request.screen = dluma::string_view("HUD.xui"); request.target = dluma::string_view("radar");
    result.struct_size = sizeof result; result.struct_version = 1;
    result.call.struct_size = sizeof result.call; result.call.struct_version = 1;
    auto hud = context.hud();
    return event->callback_id == 1 ? hud.hide(request, result) : hud.reset(request, result);
}
static DlumaStatus DLUMA_API_CALL ready(const DlumaApiV1*) { return DLUMA_STATUS_OK; }
static void DLUMA_API_CALL deactivate(const DlumaApiV1*, const DlumaScopeActivationV1*) {}
static void DLUMA_API_CALL stop(const DlumaApiV1*, uint32_t) {}
static DlumaStatus DLUMA_API_CALL event_callback(const DlumaApiV1*, const DlumaModEventV1*) { return DLUMA_STATUS_OK; }
static const DlumaNativeModApiV2 mod_api = {
    sizeof(DlumaNativeModApiV2), DLUMA_NATIVE_MOD_ABI_V2,
    ready, ready, activate, deactivate, stop, event_callback, nullptr, handle
};
extern "C" DLUMA_API_EXPORT const DlumaNativeModApiV2* DLUMA_API_CALL DLUMANativeMod_GetApiV2() { return &mod_api; }
