#ifndef DLUMA_EDITOR_MISSING_ASSET_PROBE_FIX_H
#define DLUMA_EDITOR_MISSING_ASSET_PROBE_FIX_H

#include "dlw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DlumaEditorMissingAssetProbeFixSnapshot {
    int installed;
    char guarded_drive_letter;
    ULONGLONG create_file_rejections;
    ULONGLONG attribute_rejections;
    ULONGLONG enumeration_rejections;
} DlumaEditorMissingAssetProbeFixSnapshot;

int dluma_editor_missing_asset_probe_fix_start(void);
int dluma_editor_missing_asset_probe_fix_snapshot(
    DlumaEditorMissingAssetProbeFixSnapshot* output);

#ifdef __cplusplus
}
#endif

#endif
