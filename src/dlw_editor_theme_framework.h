#ifndef DLUMA_EDITOR_THEME_FRAMEWORK_H
#define DLUMA_EDITOR_THEME_FRAMEWORK_H
#include "dlw_editor_theme.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The controller retains its verified runtime-tree lease through shutdown. */
int dluma_editor_theme_framework_apply(HWND window,
                                       const DlumaEditorThemeResolved *theme);
int dluma_editor_theme_framework_stop(void);
int dluma_editor_theme_framework_callbacks_drained(void);
void dluma_editor_theme_framework_settings_changed(void);
#ifdef __cplusplus
}
#endif
#endif
