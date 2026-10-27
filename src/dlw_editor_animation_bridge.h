#ifndef DLUMA_EDITOR_ANIMATION_BRIDGE_H
#define DLUMA_EDITOR_ANIMATION_BRIDGE_H

#include "core/dlw_animation_refresh.h"
#include "dlw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DlumaEditorAnimationReloadRequest {
    DlumaAnimationRefreshRoute route;
    const DlumaAnimationRefreshManifest* manifest;
    WCHAR project_root[DLUMA_MAX_WPATH];
    WCHAR runtime_pack_path[DLUMA_MAX_WPATH];
    char request_id[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
} DlumaEditorAnimationReloadRequest;

typedef struct DlumaEditorAnimationBreadcrumb {
    LONG sequence;
    DWORD owner_thread_id;
    char host_profile[DLUMA_SHA256_HEX_CAPACITY];
    char request_id[DLUMA_ANIMATION_REFRESH_ID_CAPACITY];
    char last_stage[64];
} DlumaEditorAnimationBreadcrumb;

int dluma_editor_animation_bridge_start(void);
int dluma_editor_animation_bridge_dispatch(
    const DlumaEditorAnimationReloadRequest* request,
    DlumaAnimationRefreshResult* result);
int dluma_editor_animation_bridge_copy_breadcrumb(
    DlumaEditorAnimationBreadcrumb* output);
int dluma_editor_animation_bridge_copy_crash_context(
    char* output,
    unsigned int capacity);
int dluma_editor_animation_bridge_copy_active_document_map(
    char* output,
    unsigned int capacity);

#ifdef __cplusplus
}
#endif

#endif
