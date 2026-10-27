#ifndef DLUMA_ASSET_MOD_RUNTIME_H
#define DLUMA_ASSET_MOD_RUNTIME_H

#include "dlw_asset_mods.h"
#include "dlw_process_lifetime_sources.h"

/* The exact filesystem adapter owns native entry-point validation. All calls
 * occur at the serialized transition boundary, after outgoing consumers drain. */
typedef struct DlumaAssetModNativeAdapter {
    int (*mount_directory)(const char *path, unsigned int flags,
                           void **source_out);
    int (*bind_inventory)(void);
    int (*refresh_namespace)(const char *path);
} DlumaAssetModNativeAdapter;

#ifdef __cplusplus
extern "C" {
#endif

void dluma_asset_mod_runtime_set_adapter(
    const DlumaAssetModNativeAdapter *adapter);
int dluma_asset_mod_runtime_prepare(void);
int dluma_asset_mod_runtime_apply(int consumers_released);
int dluma_asset_mod_runtime_validate_source(
    const DlumaProcessLifetimeSourceRecord *record);
int dluma_asset_mod_runtime_source_owned(void *source);
int dluma_asset_mod_runtime_pending(void);
int dluma_asset_mod_runtime_has_overrides(void);
int dluma_asset_mod_runtime_resolve_open(const char *path,
    char *alias_out, unsigned int alias_capacity,
    char *physical_out, unsigned int physical_capacity, void **source_out);
int dluma_asset_mod_runtime_resolve_native(const char *path, const char *source_root,
    char *physical_out, unsigned int physical_capacity, void **source_out);
void *dluma_asset_mod_runtime_source_for_path(const char *physical_path);

#ifdef __cplusplus
}
#endif
#endif
