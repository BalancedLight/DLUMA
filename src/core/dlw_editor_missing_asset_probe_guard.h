#ifndef DLUMA_EDITOR_MISSING_ASSET_PROBE_GUARD_H
#define DLUMA_EDITOR_MISSING_ASSET_PROBE_GUARD_H

#ifdef __cplusplus
extern "C" {
#endif

int dluma_editor_missing_asset_probe_is_drive_root(
    const char* path,
    char drive_letter);

int dluma_editor_missing_asset_probe_should_reject(
    const char* requested_path,
    const char* current_directory,
    char guarded_drive_letter);

#ifdef __cplusplus
}
#endif

#endif
