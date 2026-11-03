#ifndef DLUMA_EDITOR_THEME_H
#define DLUMA_EDITOR_THEME_H
#include "dlw_editor_ui.h"
#include "core/dlw_editor_theme_core.h"
#ifdef __cplusplus
extern "C" {
#endif
int dluma_editor_theme_start(void);
int dluma_editor_theme_stop(void);
int dluma_editor_theme_callbacks_drained(void);
int dluma_editor_theme_copy_snapshot(DlumaEditorThemeResolved *out);
int dluma_editor_theme_set_preference(unsigned int preference);
int dluma_editor_theme_options_attach(HWND page, HWND dialog, UINT first_id);
void dluma_editor_theme_options_layout(HWND page, int top_dip);
void dluma_editor_theme_options_detach(HWND page);
#ifdef __cplusplus
}
#endif
#endif
