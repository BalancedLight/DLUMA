#ifndef DLUMA_EDITOR_SOURCE_CONTROL_OPTIONS_H
#define DLUMA_EDITOR_SOURCE_CONTROL_OPTIONS_H
#include "dlw_editor_ui.h"
#ifdef __cplusplus
extern "C" {
#endif
int dluma_editor_source_control_options_start(const WCHAR *config_path);
void dluma_editor_source_control_options_stop(void);
int dluma_editor_source_control_options_state(int *enabled, int *ready);
int dluma_editor_source_control_options_set_enabled(int enabled);
void dluma_editor_source_control_options_loaded(void *editor, DWORD owner_thread,
    unsigned int legacy_enabled, void *context);
void dluma_editor_source_control_options_init_completed(void *editor,
    DWORD owner_thread, void *context);
const WCHAR *dluma_editor_source_control_options_message(void);
#ifdef __cplusplus
}
#endif
#endif
