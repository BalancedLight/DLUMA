#ifndef DLUMA_EDITOR_THEME_SCINTILLA_H
#define DLUMA_EDITOR_THEME_SCINTILLA_H
#include "dlw_editor_theme.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Attach only after the host's exact QuestEditor profile has been admitted. */
int dluma_editor_theme_scintilla_attach(HWND window);
int dluma_editor_theme_scintilla_owns_parent(HWND window);
/* Call on the Quest dialog's owner thread after its children are created. */
void dluma_editor_theme_scintilla_retry_dialog(HWND dialog, const DlumaEditorThemeResolved *theme);
void dluma_editor_theme_scintilla_apply(HWND window, const DlumaEditorThemeResolved *theme);
int dluma_editor_theme_scintilla_message(HWND window, unsigned int stage, UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result);
void dluma_editor_theme_scintilla_detach(HWND window);
void dluma_editor_theme_scintilla_stop(void);
int dluma_editor_theme_scintilla_callbacks_drained(void);
#ifdef __cplusplus
}
#endif
#endif
