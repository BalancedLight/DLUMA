#ifndef DLUMA_EDITOR_THEME_PROPERTIES_H
#define DLUMA_EDITOR_THEME_PROPERTIES_H
#include "dlw_editor_theme.h"
#ifdef __cplusplus
extern "C" {
#endif
int dluma_editor_theme_properties_is_list(HWND window);
int dluma_editor_theme_properties_boolean_erase(HWND window);
int dluma_editor_theme_properties_paint_boolean(HWND window, const DlumaEditorThemeResolved *theme);
int dluma_editor_theme_properties_custom_draw(HWND owner, LPARAM lparam, LRESULT *result);
void dluma_editor_theme_properties_message(HWND owner, unsigned int stage, UINT message,
    LPARAM lparam, const DlumaEditorThemeResolved *theme);
void dluma_editor_theme_properties_observe(HWND window, UINT message,
    WPARAM wparam, LPARAM lparam, void *context);
void dluma_editor_theme_properties_stop(void);
int dluma_editor_theme_properties_drained(void);
#ifdef __cplusplus
}
#endif
#endif
