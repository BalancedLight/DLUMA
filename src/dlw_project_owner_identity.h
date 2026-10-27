#ifndef DLUMA_PROJECT_OWNER_IDENTITY_H
#define DLUMA_PROJECT_OWNER_IDENTITY_H

#include "dlw_common.h"

typedef struct DlumaDirectoryIdentity {
    WCHAR final_path[DLUMA_MAX_WPATH];
    DWORD volume_serial;
    DWORD file_index_high;
    DWORD file_index_low;
} DlumaDirectoryIdentity;

int dluma_checked_path_join(
    WCHAR* out, unsigned int capacity,
    const WCHAR* left, const WCHAR* right);
int dluma_valid_map_segment(const char* map_name);
int dluma_open_directory_identity(
    const WCHAR* path, DlumaDirectoryIdentity* identity);
int dluma_directory_identity_equal(
    const DlumaDirectoryIdentity* left,
    const DlumaDirectoryIdentity* right);
int dluma_final_path_equal_or_under(
    const WCHAR* path, const WCHAR* root);
int dluma_validate_candidate_under_root(
    const WCHAR* candidate,
    const DlumaDirectoryIdentity* discovery_identity,
    DlumaDirectoryIdentity* candidate_identity);
int dluma_validate_map_anchor_under_project(
    const WCHAR* project, const char* map_name,
    const DlumaDirectoryIdentity* project_identity);
int dluma_project_regular_file_is_contained(
    const WCHAR* project_root, const WCHAR* file_path);
int dluma_project_owner_pending_proof_retains_context(
    unsigned int active_owner_epoch,
    ULONGLONG active_owner_transition_generation,
    const DlumaProjectOwnerProofSnapshot* proof);

#endif
