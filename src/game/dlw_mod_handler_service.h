#ifndef DLUMA_MOD_HANDLER_SERVICE_H
#define DLUMA_MOD_HANDLER_SERVICE_H

#include "../core/dlw_api_runtime.h"
#include "../core/dlw_mod_package_runtime.h"

typedef int (*DlumaModHandlerServiceSink)(void* user,
    const DlumaModHandlerEventV1* event_value);

#ifdef __cplusplus
extern "C" {
#endif
int dluma_mod_handler_service_attach(DlumaModOwner* owner,
    const DlumaModPackageSnapshot* package, DlumaModHandlerServiceSink sink, void* user);
void dluma_mod_handler_service_detach(uint64_t owner_id);
void dluma_mod_handler_service_clear(uint64_t owner_id);
unsigned int dluma_mod_handler_service_count(uint64_t owner_id);
int dluma_mod_handler_service_unregister_callback(uint64_t owner_id, uint64_t callback_id);
DlumaStatus dluma_mod_handler_service_dispatch(const char* mod_id,
    const char* handler_id, const char* arguments_json, uint32_t map_generation,
    uint32_t source_kind, const char* source_mod, const char* quest_id, const char* phase_id);
/* Copies one invocation for the next authoritative loader frame. */
DlumaStatus dluma_mod_handler_service_enqueue(const char* mod_id,
    const char* handler_id, const char* arguments_json, uint32_t map_generation,
    uint32_t source_kind, const char* source_mod, const char* quest_id, const char* phase_id);
/* Native boss callbacks may run outside loader dispatch. This internal producer
 * copies metadata only; acceptance is not delivery. Registry changes retire the
 * request before resolution on the next authoritative loader frame. */
DlumaStatus dluma_mod_handler_service_enqueue_boss(const char* mod_id,
    const char* handler_id, const char* arguments_json, uint32_t map_generation,
    const char* scenario_name, const char* phase_name);
/* Notify after a named-route mutation, including quest-provider fallback routes.
 * Does not inspect a registry or invoke a callback; safe for nested producers. */
void dluma_mod_handler_service_registry_changed(void);
void dluma_mod_handler_service_pump(void);
/* A zero generation closes the queue during shutdown. */
void dluma_mod_handler_service_queue_map_changed(uint32_t map_generation);
DlumaStatus dluma_mod_handler_service_invoke(DlumaModOwner* owner,
    uint32_t operation_id, const void* request, void* result);
void dluma_mod_handler_service_prepare_capabilities(DlumaCapabilityRegistry* registry);
#ifdef __cplusplus
}
#endif
#endif
