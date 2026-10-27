#ifndef DLUMA_RUNTIME_TREE_H
#define DLUMA_RUNTIME_TREE_H

#include "../dlw_common.h"

#define DLUMA_RUNTIME_TREE_MAX_FILES 512U
#define DLUMA_RUNTIME_TREE_DIAGNOSTIC_CAPACITY 256U

typedef struct DlumaRuntimeTreeLease {
    unsigned int struct_size;
    unsigned int file_count;
    HANDLE* files;
    char manifest_sha256[DLUMA_SHA256_HEX_CAPACITY];
    char diagnostic[DLUMA_RUNTIME_TREE_DIAGNOSTIC_CAPACITY];
} DlumaRuntimeTreeLease;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Validates the exact manifest-bound private runtime and retains read-only
 * handles so reviewed runtime bytes cannot be replaced before CoreCLR starts.
 */
int dluma_runtime_tree_acquire(
    const WCHAR* runtime_root,
    const char* expected_runtime_version,
    const char* expected_catalog_sha256,
    DlumaRuntimeTreeLease* lease);

void dluma_runtime_tree_release(DlumaRuntimeTreeLease* lease);

#ifdef __cplusplus
}
#endif

#endif
