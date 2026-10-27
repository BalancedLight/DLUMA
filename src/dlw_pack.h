#ifndef DLUMA_PACK_H
#define DLUMA_PACK_H

#include "dlw_common.h"
#include "dlw_process_lifetime_sources.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_CMM_SOURCE_RETIRE_NONE 0
#define DLUMA_CMM_SOURCE_RETIRE_NEW 1
#define DLUMA_CMM_SOURCE_RETIRE_ALREADY 2

#define DLUMA_CMM_SOURCE_REBIND_NOT_APPLICABLE 0
#define DLUMA_CMM_SOURCE_REBIND_SUCCEEDED 1
#define DLUMA_CMM_SOURCE_REBIND_FAILED -1

int dluma_mount_content_pack(void);
int dluma_custom_main_menu_can_replace_sources(void);
int dluma_prepare_custom_main_menu_sources(
    const WCHAR *project_root,
    const WCHAR *overlay_root,
    const char *fingerprint);
int dluma_mount_custom_main_menu_overlay(
    const WCHAR *overlay_root,
    const char *fingerprint);
int dluma_custom_main_menu_bind_native_source(
    const char *source_path,
    void *exact_source,
    unsigned int source_flags);
int dluma_custom_main_menu_rebind_retired_native_source(
    const char *source_path,
    void *exact_source,
    unsigned int source_flags);
int dluma_custom_main_menu_refresh_active_frontend_sources(void);
ULONGLONG dluma_custom_main_menu_begin_frontend_generation(void);
void dluma_custom_main_menu_invalidate_frontend_generation(
    ULONGLONG frontend_generation);
int dluma_custom_main_menu_refresh_live_sources(
    ULONGLONG frontend_generation);
int dluma_custom_main_menu_capture_native_source_generation(
    const char *source_path,
    void *exact_source,
    DlumaProcessLifetimeSourceGenerationToken *generation_out,
    ULONGLONG *frontend_generation_out);
int dluma_custom_main_menu_retire_native_source_generation(
    const DlumaProcessLifetimeSourceGenerationToken *generation,
    ULONGLONG retirement_registry_generation);
int dluma_custom_main_menu_capture_published_sources(void);
int dluma_custom_main_menu_publication_scope_enter(void);
void dluma_custom_main_menu_publication_scope_leave(void);
int dluma_custom_main_menu_publication_scope_active(void);
int dluma_custom_main_menu_native_sources_ready(void);
int dluma_custom_main_menu_sources_ready(void);
unsigned int dluma_custom_main_menu_source_flags_for_owner(
    const char *owner_id);
void *dluma_custom_main_menu_source_object_for_owner(
    const char *owner_id);
int dluma_custom_main_menu_source_snapshot_for_owner(
    const char *owner_id,
    char *path_out,
    unsigned int path_capacity,
    char *fingerprint_out,
    unsigned int fingerprint_capacity);
void dluma_unmount_content_pack(void);

#ifdef __cplusplus
}
#endif

#endif
