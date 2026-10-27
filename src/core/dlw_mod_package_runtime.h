#ifndef DLUMA_MOD_PACKAGE_RUNTIME_H
#define DLUMA_MOD_PACKAGE_RUNTIME_H

#include "../dlw_common.h"
#include "dlw_mod_manifest.h"

#define DLUMA_MOD_PACKAGE_MAX_TREE_FILES 2048U
#define DLUMA_MOD_PACKAGE_MAX_TREE_DEPTH 16U

typedef struct DlumaModArtifactSnapshot {
    unsigned int struct_size;
    unsigned int struct_version;
    unsigned int kind;
    unsigned int size;
    unsigned char* bytes;
    WCHAR final_path[DLUMA_MAX_WPATH];
    char relative_path[DLUMA_MOD_RELATIVE_PATH_CAPACITY];
    char sha256[DLUMA_MOD_SHA256_CAPACITY];
    DWORD volume_serial;
    DWORD file_index_high;
    DWORD file_index_low;
    char diagnostic[192];
    unsigned int valid;
} DlumaModArtifactSnapshot;

typedef struct DlumaModPackageSnapshot {
    unsigned int struct_size;
    unsigned int struct_version;
    DlumaModManifest manifest;
    WCHAR package_root[DLUMA_MAX_WPATH];
    WCHAR manifest_path[DLUMA_MAX_WPATH];
    WCHAR lock_path[DLUMA_MAX_WPATH];
    WCHAR entry_path[DLUMA_MAX_WPATH];
    char manifest_sha256[DLUMA_MOD_SHA256_CAPACITY];
    char lock_sha256[DLUMA_MOD_SHA256_CAPACITY];
    char entry_sha256[DLUMA_MOD_SHA256_CAPACITY];
    uint64_t entry_size;
    DWORD entry_volume_serial;
    DWORD entry_file_index_high;
    DWORD entry_file_index_low;
    char diagnostic[256];
    unsigned int valid;
} DlumaModPackageSnapshot;

#ifdef __cplusplus
extern "C" {
#endif

DlumaStatus dluma_mod_package_discover(
    const WCHAR* package_root,
    DlumaModPackageSnapshot* output);

DlumaStatus dluma_mod_package_revalidate(
    const DlumaModPackageSnapshot* approved,
    DlumaModPackageSnapshot* current);

DlumaStatus dluma_mod_package_read_artifact(
    const DlumaModPackageSnapshot* approved,
    const char* relative_path,
    unsigned int expected_kind,
    unsigned int maximum_bytes,
    DlumaModArtifactSnapshot* output);

DlumaStatus dluma_mod_package_list_artifacts(
    const DlumaModPackageSnapshot* approved,
    DlumaModLockedArtifact* artifacts,
    unsigned int artifact_capacity,
    unsigned int* artifact_count);

void dluma_mod_artifact_release(DlumaModArtifactSnapshot* artifact);

#ifdef __cplusplus
}
#endif

#endif
