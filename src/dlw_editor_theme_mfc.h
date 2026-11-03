#ifndef DLUMA_EDITOR_THEME_MFC_H
#define DLUMA_EDITOR_THEME_MFC_H
#include "dlw_editor_theme.h"
#ifdef __cplusplus
extern "C" {
#endif
int dluma_editor_theme_mfc_admit(void);
int dluma_editor_theme_mfc_attach(HWND window);
void dluma_editor_theme_mfc_apply(HWND window, const DlumaEditorThemeResolved *theme);
void dluma_editor_theme_mfc_detach(HWND window);
int dluma_editor_theme_mfc_message(HWND window, unsigned int stage, UINT message);
int dluma_editor_theme_mfc_callbacks_drained(void);
#ifdef __cplusplus
}
#endif
#endif
