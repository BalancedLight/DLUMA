#ifndef DLUMA_PLAYER_SERVICE_H
#define DLUMA_PLAYER_SERVICE_H

#include "../core/dlw_capability_registry.h"
#include "../core/dlw_mod_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

void dluma_player_service_prepare_capabilities(
    DlumaCapabilityRegistry* registry);
DlumaStatus dluma_player_service_invoke(
    DlumaModOwner* owner,
    uint32_t operation_id,
    const void* request,
    void* result);
void dluma_player_service_revoke_owner(uint64_t owner_id);

#ifdef __cplusplus
}
#endif

#endif
