#ifndef DLUMA_ASSET_MODS_H
#define DLUMA_ASSET_MODS_H

#include "dlw_common.h"

#define DLUMA_ASSET_MOD_MAX_ITEMS 128U
#define DLUMA_ASSET_MOD_ID_CAPACITY 128U
#define DLUMA_ASSET_MOD_SOURCE_CAPACITY 512U

typedef struct DlumaAssetModSnapshot {
    char id[DLUMA_ASSET_MOD_ID_CAPACITY];
    char name[128];
    char author[128];
    char version[64];
    char description[1025];
    char diagnostic[192];
    char source_identity[DLUMA_ASSET_MOD_SOURCE_CAPACITY];
    char local_source_identity[DLUMA_ASSET_MOD_SOURCE_CAPACITY];
    char steam_source_identity[DLUMA_ASSET_MOD_SOURCE_CAPACITY];
    char fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR project_root[DLUMA_MAX_WPATH];
    unsigned int desired_order;
    unsigned int effective_order;
    ULONGLONG desired_revision;
    ULONGLONG effective_revision;
    unsigned char valid;
    unsigned char desired_enabled;
    unsigned char effective_enabled;
    unsigned char pending;
    unsigned char source_is_subscribed;
    unsigned char has_local;
    unsigned char has_steam;
    unsigned char source_selection_missing;
} DlumaAssetModSnapshot;

typedef struct DlumaAssetModSourceSnapshot {
    char id[DLUMA_ASSET_MOD_ID_CAPACITY];
    char source_identity[DLUMA_ASSET_MOD_SOURCE_CAPACITY];
    char fingerprint[DLUMA_SHA256_HEX_CAPACITY];
    WCHAR project_root[DLUMA_MAX_WPATH];
    unsigned int order;
} DlumaAssetModSourceSnapshot;

#ifdef __cplusplus
extern "C" {
#endif

void dluma_asset_mods_discover(void);
unsigned int dluma_asset_mods_snapshot(
    DlumaAssetModSnapshot *output, unsigned int capacity,
    unsigned int *total_out, ULONGLONG *revision_out);
/* Sources are copied in descending precedence: the first source wins. */
unsigned int dluma_asset_mods_snapshot_sources(
    DlumaAssetModSourceSnapshot *output, unsigned int capacity,
    ULONGLONG *revision_out);
int dluma_asset_mods_set_enabled(
    const char *id, int enabled, char *diagnostic,
    unsigned int diagnostic_capacity);
/* direction is -1 for up and +1 for down. */
int dluma_asset_mods_move(
    const char *id, int direction, char *diagnostic,
    unsigned int diagnostic_capacity);
int dluma_asset_mods_select_source(
    const char *id, const char *source_identity,
    char *diagnostic, unsigned int diagnostic_capacity);
/* Call after the native layer has prepared the desired next-level stack. */
int dluma_asset_mods_commit_effective(ULONGLONG expected_revision);
ULONGLONG dluma_asset_mods_desired_revision(void);
ULONGLONG dluma_asset_mods_effective_revision(void);
/* Returns 1 for an override, 0 for no matching file, -1 for invalid input
 * or an unreadable winning file.  use_desired is for transition preparation;
 * normal runtime callers use the effective stack.  virtual_path is relative
 * to the project root, including data/ or assets_pc/. */
int dluma_asset_mods_resolve_file(
    const char *virtual_path, int use_desired,
    WCHAR *physical_path, unsigned int physical_capacity,
    char *owner_id, unsigned int owner_capacity);
int dluma_asset_mods_resolve_file_root(
    const char *virtual_path, int use_desired,
    WCHAR *physical_path, unsigned int physical_capacity,
    WCHAR *project_root, unsigned int root_capacity,
    char *owner_id, unsigned int owner_capacity);

#ifdef __cplusplus
}
#endif

#endif
