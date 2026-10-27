#ifndef DLUMA_ANIMATION_PACK_RUNTIME_H
#define DLUMA_ANIMATION_PACK_RUNTIME_H

#include "dlw_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DlumaAnimationPackSwap {
    char requested_path[DLUMA_MAX_WPATH];
    char previous_path[DLUMA_MAX_WPATH];
    void* requested_handle;
    void* previous_handle;
    unsigned int changed;
    unsigned int registered;
    unsigned int previous_unmounted;
    unsigned int rollback_attempted;
    unsigned int rollback_succeeded;
} DlumaAnimationPackSwap;

int dluma_animation_pack_runtime_swap(
    const WCHAR* absolute_pack_path,
    DlumaAnimationPackSwap* swap);
int dluma_animation_pack_runtime_rollback(DlumaAnimationPackSwap* swap);
int dluma_animation_pack_runtime_retained(void);
void dluma_animation_pack_runtime_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
