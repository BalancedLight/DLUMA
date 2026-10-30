#ifndef DLUMA_HUD_SURFACE_SERVICE_H
#define DLUMA_HUD_SURFACE_SERVICE_H

#include "dlw_game_host_context.h"

void dluma_hud_surface_prepare_capabilities(DlumaCapabilityRegistry* registry);
DlumaStatus dluma_hud_surface_invoke(DlumaModOwner* owner, uint32_t operation_id,
                                     const void* request, void* result);
void dluma_hud_surface_revoke_owner(uint64_t owner_id);
void dluma_hud_surface_pump(void);

#endif
