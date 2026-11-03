#ifndef DLUMA_EDITOR_THEME_CONTROLS_H
#define DLUMA_EDITOR_THEME_CONTROLS_H
#include "dlw_editor_theme.h"
#ifdef __cplusplus
extern "C" {
#endif
int dluma_editor_theme_controls_attach(HWND window);
void dluma_editor_theme_controls_apply(HWND window,
                                       const DlumaEditorThemeResolved *theme);
int dluma_editor_theme_controls_message(HWND window, unsigned int stage,
    UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result);
void dluma_editor_theme_controls_detach(HWND window);
void dluma_editor_theme_controls_stop(void);
int dluma_editor_theme_controls_drained(void);
void dluma_editor_theme_controls_layout_options(HWND page, HWND label,
                                                HWND combo, int top_dip);
#ifdef __cplusplus
}
#endif
#endif
