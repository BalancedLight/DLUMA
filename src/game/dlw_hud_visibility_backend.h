#ifndef DLW_HUD_VISIBILITY_BACKEND_H
#define DLW_HUD_VISIBILITY_BACKEND_H
#include <stdint.h>
#include <stddef.h>
#include "dlw_xui_element_core.h"
#define DLUMA_HUD_RULE_LIMIT 96U
#define DLUMA_HUD_SCREEN_CAPACITY 128U
#define DLUMA_HUD_TARGET_CAPACITY 384U
typedef struct DlumaHudVisibilityRule {
    char screen[DLUMA_HUD_SCREEN_CAPACITY];
    char target[DLUMA_HUD_TARGET_CAPACITY];
    uint32_t hidden;
} DlumaHudVisibilityRule;
#ifdef __cplusplus
extern "C" {
#endif
int dluma_hud_visibility_backend_initialize(void);
/* 1: unique target, 0: missing screen/target, -1: ambiguous, -2: limit. */
int dluma_hud_visibility_backend_validate(const DlumaHudVisibilityRule* rule);
typedef int (*DlumaHudElementVisitor)(void* user, void* element);
/* Synchronous current-frame callback; element is borrowed only for the call.
 * Returns 1 on success, 0 if missing, -1 ambiguous, -2 unavailable or visitor
 * failure, -3 wrong element class, -4 native entry may have partially changed
 * the UI. Do not automatically retry -4. The caller qualifies its native calls. */
int dluma_hud_visibility_backend_visit(const char* screen, const char* target,
    const char* required_class, DlumaHudElementVisitor visitor, void* user);
/* Resolve and mutate in one frame; no element pointer escapes this call.
 * Returns 1 on success, 0 if missing, -1 if ambiguous, -2 if unavailable. */
int dluma_hud_visibility_backend_set_position(void* unused, const char* screen,
    const char* target, const float position[3]);
int dluma_hud_visibility_backend_position_available(void);
/* Private diagnostic context only; ordinary builds always remain unavailable. */
void dluma_hud_visibility_backend_diagnostic_context(const wchar_t* project,
    const char* map, uint32_t generation);
int dluma_hud_visibility_backend_diagnostic_owner_allowed(uint32_t scope, uint32_t generation);
int dluma_hud_visibility_backend_visual_available(void);
int dluma_hud_visibility_backend_set_visual(void* unused, const char* screen,
    const char* target, DlumaXuiVisualProperty property, const float* values);
int dluma_hud_visibility_backend_named_available(DlumaXuiNamedAction action);
int dluma_hud_visibility_backend_apply_named(void* unused, const char* screen,
    const char* target, DlumaXuiNamedAction action, const char* value);
void dluma_hud_visibility_backend_apply(const DlumaHudVisibilityRule* rules, unsigned count);
void dluma_hud_visibility_backend_reset(void);
/* Reserve the resolver across a same-map HUD rebuild. No game UI is touched.
 * A successful begin must be paired with end even after a native fault. */
int dluma_hud_visibility_backend_begin_document_reload(void);
void dluma_hud_visibility_backend_end_document_reload(void);
/* A contended query also refuses native texture admission. */
int dluma_hud_visibility_backend_document_reload_active(void);
int dluma_hud_visibility_backend_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
