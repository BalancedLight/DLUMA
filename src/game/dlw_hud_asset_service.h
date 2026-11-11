#ifndef DLUMA_HUD_ASSET_SERVICE_H
#define DLUMA_HUD_ASSET_SERVICE_H
#include "../core/dlw_mod_owner.h"
#ifdef __cplusplus
extern "C" {
#endif
DlumaStatus dluma_game_document_invoke(DlumaModOwner*,uint32_t,const void*,void*);
DlumaStatus dluma_game_texture_invoke(DlumaModOwner*,uint32_t,const void*,void*);
#ifdef __cplusplus
}
#endif
#endif
