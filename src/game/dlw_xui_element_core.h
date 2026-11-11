#ifndef DLUMA_XUI_ELEMENT_CORE_H
#define DLUMA_XUI_ELEMENT_CORE_H

typedef enum DlumaXuiElementResult {
    DLUMA_XUI_ELEMENT_OK = 0,
    DLUMA_XUI_ELEMENT_INVALID,
    DLUMA_XUI_ELEMENT_NOT_FOUND,
    DLUMA_XUI_ELEMENT_AMBIGUOUS,
    DLUMA_XUI_ELEMENT_UNAVAILABLE,
    DLUMA_XUI_ELEMENT_WRONG_TYPE,
    DLUMA_XUI_ELEMENT_UNCERTAIN
} DlumaXuiElementResult;

typedef struct DlumaXuiElementPositionBackend {
    void* user;
    /* Resolution and mutation both occur within one authoritative frame. */
    int (*set_position)(void* user, const char* screen, const char* target,
                        const float position[3]);
} DlumaXuiElementPositionBackend;

DlumaXuiElementResult dluma_xui_element_set_position(
    const DlumaXuiElementPositionBackend* backend,
    const char* screen, const char* target, const float position[3]);

typedef enum DlumaXuiVisualProperty {
    DLUMA_XUI_VISUAL_OPACITY = 1,
    DLUMA_XUI_VISUAL_COLOR = 2
} DlumaXuiVisualProperty;
typedef struct DlumaXuiElementVisualBackend {
    void* user;
    int (*set_visual)(void* user, const char* screen, const char* target,
                      DlumaXuiVisualProperty property, const float* values);
} DlumaXuiElementVisualBackend;
DlumaXuiElementResult dluma_xui_element_set_visual(
    const DlumaXuiElementVisualBackend* backend, const char* screen,
    const char* target, DlumaXuiVisualProperty property, const float* values);

typedef enum DlumaXuiNamedAction {
    DLUMA_XUI_TEXT_KEY = 1,
    DLUMA_XUI_ANIMATION = 2
} DlumaXuiNamedAction;
typedef struct DlumaXuiElementNamedBackend {
    void* user;
    int (*apply)(void* user, const char* screen, const char* target,
                 DlumaXuiNamedAction action, const char* value);
} DlumaXuiElementNamedBackend;
DlumaXuiElementResult dluma_xui_element_apply_named(
    const DlumaXuiElementNamedBackend* backend, const char* screen,
    const char* target, DlumaXuiNamedAction action, const char* value);
#endif
