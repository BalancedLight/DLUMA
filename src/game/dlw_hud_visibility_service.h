#ifndef DLW_HUD_VISIBILITY_SERVICE_H
#define DLW_HUD_VISIBILITY_SERVICE_H
#include "dlw_game_host_context.h"
#ifdef __cplusplus
extern "C" {
#endif
void dluma_hud_visibility_prepare_capabilities(DlumaCapabilityRegistry* registry);
DlumaStatus dluma_hud_visibility_invoke(DlumaModOwner* owner,uint32_t operation_id,const void* request,void* result);
void dluma_hud_visibility_revoke_owner(uint64_t owner_id);
void dluma_hud_visibility_reset_map(uint32_t generation);
void dluma_hud_visibility_pump(void);
int dluma_hud_visibility_shutdown(void);
/* Aggregate of visible, bound HUD surfaces; shares arbitration with mods. */
void dluma_hud_visibility_surface_mask(uint32_t mask);
#ifdef __cplusplus
}
#endif
#endif
