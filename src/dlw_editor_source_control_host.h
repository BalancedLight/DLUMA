#ifndef DLUMA_EDITOR_SOURCE_CONTROL_HOST_H
#define DLUMA_EDITOR_SOURCE_CONTROL_HOST_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(DLUMA_WIN32_H) && !defined(_WINDOWS_)
#include <windows.h>
#endif
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DLUMA_EDITOR_GIT_CONTRACT_VERSION 1U
#define DLUMA_EDITOR_GIT_ENVELOPE_SCHEMA 2U
#define DLUMA_EDITOR_GIT_MAX_PAYLOAD (64U * 1024U)
#define DLUMA_EDITOR_GIT_LEASE_HEX_LENGTH 32U

typedef enum DlumaEditorGitOperation {
    DLUMA_EDITOR_GIT_INITIALIZE = 1,
    DLUMA_EDITOR_GIT_STAGE,
    DLUMA_EDITOR_GIT_UNSTAGE,
    DLUMA_EDITOR_GIT_COMMIT,
    DLUMA_EDITOR_GIT_CREATE_BRANCH,
    DLUMA_EDITOR_GIT_SWITCH_BRANCH,
    DLUMA_EDITOR_GIT_FETCH,
    DLUMA_EDITOR_GIT_PULL,
    DLUMA_EDITOR_GIT_PUBLISH_BRANCH,
    DLUMA_EDITOR_GIT_PUSH,
    DLUMA_EDITOR_GIT_ADD_REMOTE,
    DLUMA_EDITOR_GIT_CLONE
} DlumaEditorGitOperation;

/* These entry points run on the native owner thread. Worker transport code
 * posts requests to that thread; it never calls engine/UI methods directly. */
int dluma_editor_source_control_host_start(const WCHAR *config_path);
int dluma_editor_source_control_host_stop(void);
int dluma_editor_source_control_host_callbacks_drained(void);
int dluma_editor_source_control_host_show(HWND main_window);
int dluma_editor_source_control_host_options_attach(HWND page, HWND dialog);
void dluma_editor_source_control_host_options_layout(HWND page, int top_dip);
void dluma_editor_source_control_host_options_detach(HWND page);

#ifdef __cplusplus
}
#endif
#endif
