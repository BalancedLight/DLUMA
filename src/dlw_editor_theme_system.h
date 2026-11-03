#ifndef DLW_EDITOR_THEME_SYSTEM_H
#define DLW_EDITOR_THEME_SYSTEM_H
#include <stdint.h>
#ifdef _WIN32
#define DLUMA_THEME_CALL __stdcall
#else
#define DLUMA_THEME_CALL
#endif
#ifdef DLUMA_EDITOR_THEME_SYSTEM_EXPORTS
#define DLUMA_THEME_API __declspec(dllexport)
#else
#define DLUMA_THEME_API
#endif
#pragma pack(push, 8)
typedef struct DlumaEditorThemeSystemState {
    uint32_t size, version;
    uint64_t generation;
    uint32_t apps_mode_available, apps_dark, high_contrast, reserved;
    uint32_t system_argb[17];
    uint32_t reserved_tail;
} DlumaEditorThemeSystemState;
typedef struct DlumaEditorThemeResolved {
    uint32_t size, version;
    uint64_t generation;
    uint32_t kind, preference;
    uint32_t argb[17];
    uint32_t reserved;
} DlumaEditorThemeResolved;
#pragma pack(pop)
typedef void (DLUMA_THEME_CALL *DlumaEditorThemeSystemChangedFn)(void *, const DlumaEditorThemeSystemState *);
/* Serialize start/stop calls at the owner. Callbacks run on the watcher thread;
 * a callback may request stop, but must return before its own invocation drains.
 * A later stop from another thread waits for the watcher to finish. */
#ifdef __cplusplus
extern "C" {
#endif
DLUMA_THEME_API int dluma_editor_theme_system_start(DlumaEditorThemeSystemChangedFn, void *, DlumaEditorThemeSystemState *);
DLUMA_THEME_API int dluma_editor_theme_system_read(DlumaEditorThemeSystemState *);
DLUMA_THEME_API int dluma_editor_theme_system_resolve(unsigned int, const DlumaEditorThemeSystemState *, DlumaEditorThemeResolved *);
DLUMA_THEME_API void dluma_editor_theme_system_stop(void);
#ifdef __cplusplus
}
#endif
#endif
