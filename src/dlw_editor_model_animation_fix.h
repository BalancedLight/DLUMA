#ifndef DLUMA_EDITOR_MODEL_ANIMATION_FIX_H
#define DLUMA_EDITOR_MODEL_ANIMATION_FIX_H

#include "dlw_win32.h"

typedef enum DlumaEditorModelAnimationFixInstallResult {
    DLUMA_EDITOR_MODEL_ANIMATION_FIX_REFUSED = 0,
    DLUMA_EDITOR_MODEL_ANIMATION_FIX_INSTALLED = 1,
    DLUMA_EDITOR_MODEL_ANIMATION_FIX_RETAINED = -1
} DlumaEditorModelAnimationFixInstallResult;

#ifdef __cplusplus
extern "C" {
#endif

DlumaEditorModelAnimationFixInstallResult
dluma_editor_model_animation_fix_install(
    HMODULE gamedll,
    ULONGLONG generation);
int dluma_editor_model_animation_fix_quiesce(void);
int dluma_editor_model_animation_fix_restore(void);
void dluma_editor_model_animation_fix_retain(void);

#ifdef __cplusplus
}
#endif

#endif
