#ifndef DLUMA_MEDIA_SERVICE_H
#define DLUMA_MEDIA_SERVICE_H

#include "dlw_game_host_context.h"

#ifdef __cplusplus
extern "C" {
#endif

void dluma_media_service_start(void);
void dluma_media_service_bind(
    HMODULE original_gamedll,
    void* game_script_manager);
void dluma_media_service_tick(
    void* game_subobject,
    float delta_seconds);
int dluma_media_service_begin_transition(
    unsigned int map_generation, void* outgoing_level);
void dluma_media_service_frontend_ready(void);
void dluma_media_service_prepare_capabilities(
    DlumaCapabilityRegistry* registry);
DlumaStatus dluma_media_service_invoke(
    DlumaGameHostContext* host_context,
    DlumaModOwner* owner,
    uint32_t operation_id,
    const void* request,
    void* result);
int dluma_media_service_revoke_owner(
    uint64_t owner_id,
    uint32_t owner_generation);
int dluma_media_service_archive_in_use(
    const DlumaCompanionArchive* archive);
int dluma_media_service_quiesce_all(void);
int dluma_media_service_all_drained(void);

#ifdef __cplusplus
}
#endif

#endif
