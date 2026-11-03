#ifndef DLW_EDITOR_THEME_CORE_H
#define DLW_EDITOR_THEME_CORE_H
#include "../dlw_editor_theme_system.h"
typedef enum DlumaEditorThemePreference { DLUMA_EDITOR_THEME_SYSTEM=0, DLUMA_EDITOR_THEME_LIGHT=1, DLUMA_EDITOR_THEME_DARK=2 } DlumaEditorThemePreference;
typedef enum DlumaEditorThemeKind { DLUMA_EDITOR_THEME_KIND_LIGHT=0, DLUMA_EDITOR_THEME_KIND_DARK=1, DLUMA_EDITOR_THEME_KIND_HIGH_CONTRAST=2 } DlumaEditorThemeKind;
/* All schedule operations, including stop, are serialized by the caller. */
typedef struct DlumaEditorThemeSchedule { DlumaEditorThemeResolved latest; int has_latest, pending, stopped; } DlumaEditorThemeSchedule;
#ifdef __cplusplus
extern "C" {
#endif
int dluma_editor_theme_core_resolve(unsigned int, const DlumaEditorThemeSystemState *, DlumaEditorThemeResolved *);
void dluma_editor_theme_schedule_initialize(DlumaEditorThemeSchedule *);
int dluma_editor_theme_schedule_publish(DlumaEditorThemeSchedule *, const DlumaEditorThemeResolved *);
int dluma_editor_theme_schedule_take(DlumaEditorThemeSchedule *, DlumaEditorThemeResolved *);
void dluma_editor_theme_schedule_stop(DlumaEditorThemeSchedule *);
#ifdef __cplusplus
}
#endif
#endif
