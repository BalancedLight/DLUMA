#ifndef DLUMA_EDITOR_GIT_SESSION_H
#define DLUMA_EDITOR_GIT_SESSION_H
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include "dlw_editor_panel_host.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { DLUMA_EDITOR_GIT_SESSION_VERSION = 1, DLUMA_EDITOR_GIT_FRAME_MAX = 65536 };
typedef enum DlumaEditorGitRequestResult {
    DLUMA_EDITOR_GIT_REQUEST_ERROR = 0,
    DLUMA_EDITOR_GIT_REQUEST_OK = 1
} DlumaEditorGitRequestResult;
/* Owner-thread only. type and UTF-8 payload are borrowed for the call.
 * Write one complete JSON value into output and its byte length into used.
 * ERROR output is a complete JSON error value, not an envelope. */
typedef DlumaEditorGitRequestResult (*DlumaEditorGitRequestHandler)(
    const char *type, const char *payload, size_t payload_size,
    char *output, size_t capacity, size_t *used, void *context);
typedef struct DlumaEditorGitSessionSpecV1 {
    uint32_t size, version;
    HWND owner;
    DlumaEditorPanelRef panel;
    const WCHAR *theme_config;
    const WCHAR *project_root;
    uint64_t project_generation;
    DlumaEditorGitRequestHandler handle_request;
    void *context;
} DlumaEditorGitSessionSpecV1;
typedef struct DlumaEditorGitJobSnapshotV1 {
    uint32_t size, version;
    HANDLE companion_process; /* borrowed until next owner-thread session call */
    HANDLE job;               /* borrowed until next owner-thread session call */
    DWORD companion_pid;
    DWORD active_processes;
} DlumaEditorGitJobSnapshotV1;
/* All entry points run on the owner thread. Open begins asynchronous admission.
 * dispatch returns 1 when consumed, setting result to 0 or -1 on failure.
 * notify queues a complete UTF-8 JSON value without blocking on pipe I/O.
 * close returns 1 only after job quiescence and final retirement; call again
 * after asynchronous cleanup if it returns 0. drained is safe at unload. */
int dluma_editor_git_session_open(const DlumaEditorGitSessionSpecV1 *spec);
int dluma_editor_git_session_dispatch(HWND owner, UINT message, WPARAM wparam,
    LPARAM lparam, LRESULT *result);
int dluma_editor_git_session_notify(const char *type, const char *json_payload);
int dluma_editor_git_session_close(void);
int dluma_editor_git_session_drained(void);
int dluma_editor_git_session_job_snapshot(DlumaEditorGitJobSnapshotV1 *out);
#ifdef __cplusplus
}
#endif
#endif
