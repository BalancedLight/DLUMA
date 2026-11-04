#ifndef DLUMA_EDITOR_SOURCE_CONTROL_H
#define DLUMA_EDITOR_SOURCE_CONTROL_H

#include "dlw_editor_ui.h"

#define DLUMA_EDITOR_SOURCE_CONTROL_VERSION 1U

typedef void (*DlumaEditorSourceControlOptionsLoadedFn)(
    void* editor, DWORD owner_thread, unsigned int legacy_enabled, void* context);
/* options_loaded follows a matched legacy-enabled provider factory invocation.
 * Legacy-disabled startup skips that provider initializer. */
/* init_attempt_completed follows the Editor initializer's matched normal
 * return for either legacy setting. It does not certify parsing or frame setup. */
typedef void (*DlumaEditorSourceControlInitCompletedFn)(
    void* editor, DWORD owner_thread, void* context);

typedef struct DlumaEditorSourceControlCallbacksV1 {
    unsigned int size;
    unsigned int version;
    DlumaEditorSourceControlOptionsLoadedFn options_loaded;
    void* context;
    DlumaEditorSourceControlInitCompletedFn init_attempt_completed;
} DlumaEditorSourceControlCallbacksV1;

typedef struct DlumaEditorSourceControlSnapshotV1 {
    unsigned int size;
    unsigned int version;
    void* editor;
    DWORD owner_thread;
    unsigned int legacy_enabled;
    unsigned int boundary_seen;
    unsigned int init_returned;
} DlumaEditorSourceControlSnapshotV1;

#ifdef __cplusplus
extern "C" {
#endif

/* Install before the native provider factory. A zero result requires the
 * host to decline protected Git startup before reaching that factory.
 * Notifications borrow context through stop's completed notification drain.
 * Guard code/minimal late-callback state is retained for the process lifetime;
 * callbacks_drained is an activity query, never permission to unload it. */
int dluma_editor_source_control_start_early(
    const DlumaEditorSourceControlCallbacksV1* callbacks);
/* Owner-thread query. boundary_seen identifies a matched, retired native
 * factory object. init_returned identifies the matched Init's normal return;
 * it does not indicate successful parsing or that frame setup has completed.
 * Query at the host's qualified post-load ready point; a failed snapshot
 * cannot admit Git or preference persistence. No preference is changed. */
int dluma_editor_source_control_native_snapshot(
    DlumaEditorSourceControlSnapshotV1* snapshot);
int dluma_editor_source_control_stop(void);
int dluma_editor_source_control_callbacks_drained(void);
unsigned int dluma_editor_source_control_blocked_count(void);

#ifdef __cplusplus
}
#endif
#endif
