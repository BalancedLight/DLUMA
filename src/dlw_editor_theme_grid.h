#ifndef DLUMA_EDITOR_THEME_GRID_H
#define DLUMA_EDITOR_THEME_GRID_H
#include "dlw_editor_theme.h"
#ifdef __cplusplus
extern "C" {
#endif
int dluma_editor_theme_grid_admit(void);
int dluma_editor_theme_grid_attach(HWND window);
void dluma_editor_theme_grid_apply(HWND window, const DlumaEditorThemeResolved *theme);
void dluma_editor_theme_grid_detach(HWND window);
int dluma_editor_theme_grid_message(HWND window, unsigned int stage, UINT message);
int dluma_editor_theme_grid_owns_header(HWND window);
/* Stop admission and retire the owned IAT routes after all grids detach. */
int dluma_editor_theme_grid_stop(void);
int dluma_editor_theme_grid_callbacks_drained(void);
#ifdef __cplusplus
}
#endif
#endif
