#ifndef DLUMA_EDITOR_EXIT_OBSERVER_H
#define DLUMA_EDITOR_EXIT_OBSERVER_H

#include "core/dlw_editor_exit_intent.h"
#include "dlw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared frame admission for presence and process-close observation. */
int dluma_editor_is_main_window(HWND window, DWORD expected_process_id);
int dluma_editor_exit_observer_start(void);
int dluma_editor_exit_observer_copy_snapshot(
    DlumaEditorExitIntentSnapshot *snapshot_out);
int dluma_editor_exit_observer_dispatch_command(unsigned int command_id);

#ifdef __cplusplus
}
#endif

#endif
