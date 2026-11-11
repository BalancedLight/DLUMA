#ifndef DLUMA_EDITOR_SOURCE_CONTROL_UI_H
#define DLUMA_EDITOR_SOURCE_CONTROL_UI_H
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include "dlw_editor_source_control_host.h"

/* Parser and child-validation primitives. These do not admit an operation or
 * make a project identity authoritative. Call on the session owner thread. */
/* Allocate this bounded payload buffer on the heap, not the UI stack. */
typedef struct DlumaEditorGitParsedRequest {
    unsigned int kind;
    char project_root[65537];
    char destination[65537];
    char git_executable[65537];
    char operation[32];
    char lease_id[33], arm_id[33];
    uint64_t project_generation, creation_time, process_handle;
    DWORD process_id;
    int mutating, has_destination, has_executable, has_lease, has_arm;
} DlumaEditorGitParsedRequest;
enum { DLUMA_GIT_STATE = 1, DLUMA_GIT_PROJECT_ACQUIRE, DLUMA_GIT_WORKTREE_ACQUIRE,
       DLUMA_GIT_CHILD_STARTED, DLUMA_GIT_LEASE_RELEASE, DLUMA_GIT_PROJECT_CANCEL };
#ifdef __cplusplus
extern "C" {
#endif
int dluma_editor_source_control_parse_request(const char *type, const char *payload,
    size_t size, DlumaEditorGitParsedRequest *request);
/* expected_executable is an already-open, regular-file identity held by the
 * caller. Returns an owned SYNCHRONIZE/query handle only on full match. */
int dluma_editor_source_control_verify_child(HANDLE companion, HANDLE job,
    HANDLE expected_executable, DWORD process_id, uint64_t creation_time,
    uint64_t companion_handle, HANDLE *verified_process);
#ifdef __cplusplus
}
#endif
#endif
