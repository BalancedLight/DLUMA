#ifndef DLUMA_EDITOR_COMPANION_RUNTIME_H
#define DLUMA_EDITOR_COMPANION_RUNTIME_H
#include "dlw_editor_ui.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Retain the verified runtime files for the complete companion lifetime. */
void *dluma_editor_companion_runtime_acquire(
    const WCHAR *runtime_root, const char *runtime_version);
void dluma_editor_companion_runtime_release(void *lease);
#ifdef __cplusplus
}
#endif
#endif
