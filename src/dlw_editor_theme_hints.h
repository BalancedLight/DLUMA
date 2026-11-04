#ifndef DLUMA_EDITOR_THEME_HINTS_H
#define DLUMA_EDITOR_THEME_HINTS_H
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
int dluma_editor_theme_hints_foreground(HWND window, void *owner, COLORREF expected, int expected_auto,
                                       COLORREF desired, int desired_auto);
void *dluma_editor_theme_hints_freeze(HWND window, void *owner);
void dluma_editor_theme_hints_unfreeze(void *token, int destroying);
#ifdef __cplusplus
}
#endif
#endif
