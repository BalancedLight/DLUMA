#ifndef DLUMA_BUILD_IDENTITY_H
#define DLUMA_BUILD_IDENTITY_H

#include "dlw_win32.h"

#define DLUMA_BUILD_IDENTITY_SHA256_CAPACITY 65U
#define DLUMA_BUILD_IDENTITY_TEXT_CAPACITY 384U

typedef struct DlumaBuildIdentity {
    DWORD pe_timestamp;
    DWORD size_of_image;
    ULONGLONG file_size;
    char sha256[DLUMA_BUILD_IDENTITY_SHA256_CAPACITY];
    unsigned char pe_identity_valid;
    unsigned char file_size_valid;
    unsigned char file_hash_valid;
    unsigned char complete;
} DlumaBuildIdentity;

/* Pure helpers used by both the runtime capture and deterministic tests. */
int dluma_build_identity_parse_pe_header(
    const BYTE* image,
    SIZE_T available,
    DWORD* timestamp,
    DWORD* size_of_image);
void dluma_build_identity_format(
    const DlumaBuildIdentity* identity,
    const char* loader_version,
    char* out,
    unsigned int capacity);

/* Initialize once from the loader module itself.  The returned snapshots are
 * immutable for the remainder of the process and are safe to copy into later
 * diagnostic/report headers without hashing from a hook. */
int dluma_build_identity_initialize(HMODULE loader_module);
const DlumaBuildIdentity* dluma_build_identity_snapshot(void);
const char* dluma_build_identity_text(void);
/* Git metadata is generated on the build host. It identifies the committed
 * source that produced this binary; the runtime identity snapshot separately
 * verifies the loaded module's bytes. */
const char* dluma_build_identity_git_head(void);
const char* dluma_build_git_short(void);
int dluma_build_worktree_dirty(void);
void dluma_build_identity_log(void);

#endif
