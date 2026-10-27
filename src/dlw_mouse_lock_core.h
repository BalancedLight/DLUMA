#ifndef DLUMA_MOUSE_LOCK_CORE_H
#define DLUMA_MOUSE_LOCK_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DlumaMouseLockPoint
{
    long x;
    long y;
} DlumaMouseLockPoint;

typedef struct DlumaMouseLockRect
{
    long left;
    long top;
    long right;
    long bottom;
} DlumaMouseLockRect;

typedef struct DlumaMouseLockCoreState
{
    void *window;
    DlumaMouseLockRect last_owned_rect;
    unsigned char enabled;
    unsigned char bound;
    unsigned char owns_clip;
    unsigned char suspended;
    unsigned char quiesced;
} DlumaMouseLockCoreState;

typedef struct DlumaMouseLockPlatform
{
    void *context;
    void *(*get_foreground_window)(void *context);
    int (*is_iconic)(void *context, void *window);
    int (*get_client_rect)(
        void *context, void *window, DlumaMouseLockRect *rect_out);
    int (*client_to_screen)(
        void *context, void *window, DlumaMouseLockPoint *point);
    int (*get_clip_cursor)(
        void *context, DlumaMouseLockRect *rect_out);
    int (*clip_cursor)(
        void *context, const DlumaMouseLockRect *rect);
} DlumaMouseLockPlatform;

typedef enum DlumaMouseLockCoreResult
{
    DLUMA_MOUSE_LOCK_CORE_NONE = 0,
    DLUMA_MOUSE_LOCK_CORE_APPLIED,
    DLUMA_MOUSE_LOCK_CORE_RELEASED,
    DLUMA_MOUSE_LOCK_CORE_FOREIGN_CLIP_PRESERVED,
    DLUMA_MOUSE_LOCK_CORE_GEOMETRY_UNAVAILABLE,
    DLUMA_MOUSE_LOCK_CORE_APPLY_FAILED,
    DLUMA_MOUSE_LOCK_CORE_RELEASE_FAILED,
    DLUMA_MOUSE_LOCK_CORE_PLATFORM_UNAVAILABLE
} DlumaMouseLockCoreResult;

void dluma_mouse_lock_core_initialize(
    DlumaMouseLockCoreState *state, int enabled);
void dluma_mouse_lock_core_bind(
    DlumaMouseLockCoreState *state, void *window);
int dluma_mouse_lock_core_rect_equal(
    const DlumaMouseLockRect *left, const DlumaMouseLockRect *right);
int dluma_mouse_lock_core_rect_valid(
    const DlumaMouseLockRect *rect);
DlumaMouseLockCoreResult dluma_mouse_lock_core_maintain(
    DlumaMouseLockCoreState *state,
    const DlumaMouseLockPlatform *platform);
DlumaMouseLockCoreResult dluma_mouse_lock_core_release(
    DlumaMouseLockCoreState *state,
    const DlumaMouseLockPlatform *platform,
    int synchronous);
DlumaMouseLockCoreResult dluma_mouse_lock_core_suspend(
    DlumaMouseLockCoreState *state,
    const DlumaMouseLockPlatform *platform);
void dluma_mouse_lock_core_resume(DlumaMouseLockCoreState *state);
DlumaMouseLockCoreResult dluma_mouse_lock_core_quiesce(
    DlumaMouseLockCoreState *state,
    const DlumaMouseLockPlatform *platform);
DlumaMouseLockCoreResult dluma_mouse_lock_core_unbind(
    DlumaMouseLockCoreState *state,
    const DlumaMouseLockPlatform *platform);

#ifdef __cplusplus
}
#endif

#endif
