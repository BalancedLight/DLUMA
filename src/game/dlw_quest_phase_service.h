#ifndef DLUMA_QUEST_PHASE_SERVICE_H
#define DLUMA_QUEST_PHASE_SERVICE_H

#include "../core/dlw_api_runtime.h"
#include "../core/dlw_mod_package_runtime.h"
#include "../core/dlw_quest_phase.h"

typedef int (*DlumaQuestPhaseServiceEventSinkFn)(
    void* user,
    uint32_t event_kind,
    uint64_t callback_id,
    uint64_t registration_token,
    const DlumaQuestPhaseEvent* event_value);

#ifdef __cplusplus
extern "C" {
#endif

/* Attach one already-approved language host. The package snapshot remains
 * owned by that host and must outlive this attachment. */
int dluma_quest_phase_service_attach(
    DlumaModOwner* owner,
    const DlumaModPackageSnapshot* package,
    int load_priority,
    DlumaQuestPhaseServiceEventSinkFn sink,
    void* sink_user);

/* Removes every dispatcher token for this exact owner. Idempotent. */
int dluma_quest_phase_service_detach(uint64_t owner_id);

/* Lua's private helper uses callback IDs as its authored cancellation tokens.
 * Native callers normally use the public token returned by the service. */
int dluma_quest_phase_service_unregister_callback(
    uint64_t owner_id,
    uint32_t event_kind,
    uint64_t callback_id,
    const char* key);

DlumaStatus dluma_quest_phase_service_invoke(
    DlumaModOwner* owner,
    uint32_t operation_id,
    const void* request,
    void* result);

void dluma_quest_phase_service_prepare_capabilities(
    DlumaCapabilityRegistry* registry);

#ifdef __cplusplus
}
#endif

#endif
