#ifndef DLUMA_EDITOR_LAUNCH_H
#define DLUMA_EDITOR_LAUNCH_H

#include "dlw_common.h"

typedef struct DlumaEditorLaunchHookSnapshot {
    ULONG_PTR iat_slot;
    ULONG_PTR original_target;
    ULONG_PTR shell_execute_iat_slot;
    ULONG_PTR shell_execute_original_target;
    ULONGLONG next_generation;
    ULONGLONG published_count;
    ULONGLONG rejected_count;
    unsigned int installed;
    unsigned int stopping;
    unsigned int active_calls;
    unsigned int detached;
} DlumaEditorLaunchHookSnapshot;

#ifdef __cplusplus
extern "C" {
#endif

int dluma_editor_launch_hook_start(void);
int dluma_editor_launch_hook_stop(void);
int dluma_editor_launch_hook_callbacks_drained(void);
int dluma_editor_launch_hook_snapshot(DlumaEditorLaunchHookSnapshot* output);
int dluma_editor_launch_resolve_current_project(
    WCHAR* project_root,
    unsigned int project_capacity,
    char* map_name,
    unsigned int map_capacity);
int dluma_editor_launch_resolve_workshop_root(
    WCHAR* workshop_root,
    unsigned int workshop_capacity);

#ifdef __cplusplus
}
#endif

#endif
