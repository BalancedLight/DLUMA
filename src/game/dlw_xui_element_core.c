#include "dlw_xui_element_core.h"
#include <float.h>

static int finite_coordinate(float value)
{
    /* Reject NaN and both infinities without pulling CRT classification
     * helpers into the loader's no-default-library native link. */
    return value >= -FLT_MAX && value <= FLT_MAX;
}

DlumaXuiElementResult dluma_xui_element_apply_named(
    const DlumaXuiElementNamedBackend* backend, const char* screen,
    const char* target, DlumaXuiNamedAction action, const char* value)
{
    char key_token[130];
    unsigned int i; int result;
    if (!screen || !screen[0] || !target || !target[0] || !value || !value[0] ||
        (action != DLUMA_XUI_TEXT_KEY && action != DLUMA_XUI_ANIMATION))
        return DLUMA_XUI_ELEMENT_INVALID;
    for (i = 0; i < 128U && value[i]; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (c < 32U || c > 126U || (action == DLUMA_XUI_TEXT_KEY && c == '&'))
            return DLUMA_XUI_ELEMENT_INVALID;
        if (action == DLUMA_XUI_TEXT_KEY) key_token[i + 1U] = (char)c;
    }
    if (!i || i == 128U) return DLUMA_XUI_ELEMENT_INVALID;
    if (action == DLUMA_XUI_TEXT_KEY) {
        key_token[0] = '&'; key_token[i + 1U] = '&'; key_token[i + 2U] = 0;
        value = key_token;
    }
    if (!backend || !backend->apply) return DLUMA_XUI_ELEMENT_UNAVAILABLE;
    result = backend->apply(backend->user, screen, target, action, value);
    if (result == 1) return DLUMA_XUI_ELEMENT_OK;
    if (result == 0) return DLUMA_XUI_ELEMENT_NOT_FOUND;
    if (result == -1) return DLUMA_XUI_ELEMENT_AMBIGUOUS;
    if (result == -3) return DLUMA_XUI_ELEMENT_WRONG_TYPE;
    if (result == -4) return DLUMA_XUI_ELEMENT_UNCERTAIN;
    return DLUMA_XUI_ELEMENT_UNAVAILABLE;
}

DlumaXuiElementResult dluma_xui_element_set_visual(
    const DlumaXuiElementVisualBackend* backend, const char* screen,
    const char* target, DlumaXuiVisualProperty property, const float* values)
{
    unsigned int i, count;
    int result;
    if (!screen || !screen[0] || !target || !target[0] || !values ||
        (property != DLUMA_XUI_VISUAL_OPACITY && property != DLUMA_XUI_VISUAL_COLOR))
        return DLUMA_XUI_ELEMENT_INVALID;
    count = property == DLUMA_XUI_VISUAL_OPACITY ? 1U : 4U;
    for (i = 0; i < count; ++i)
        if (!finite_coordinate(values[i])) return DLUMA_XUI_ELEMENT_INVALID;
    if (!backend || !backend->set_visual) return DLUMA_XUI_ELEMENT_UNAVAILABLE;
    result = backend->set_visual(backend->user, screen, target, property, values);
    if (result == 1) return DLUMA_XUI_ELEMENT_OK;
    if (result == 0) return DLUMA_XUI_ELEMENT_NOT_FOUND;
    if (result == -1) return DLUMA_XUI_ELEMENT_AMBIGUOUS;
    if (result == -3) return DLUMA_XUI_ELEMENT_WRONG_TYPE;
    if (result == -4) return DLUMA_XUI_ELEMENT_UNCERTAIN;
    return DLUMA_XUI_ELEMENT_UNAVAILABLE;
}

DlumaXuiElementResult dluma_xui_element_set_position(
    const DlumaXuiElementPositionBackend* backend,
    const char* screen, const char* target, const float position[3])
{
    int result;
    if (!screen || !screen[0] || !target || !target[0] || !position ||
        !finite_coordinate(position[0]) || !finite_coordinate(position[1]) ||
        !finite_coordinate(position[2]))
        return DLUMA_XUI_ELEMENT_INVALID;
    if (!backend || !backend->set_position) return DLUMA_XUI_ELEMENT_UNAVAILABLE;
    result = backend->set_position(backend->user, screen, target, position);
    if (result == 1) return DLUMA_XUI_ELEMENT_OK;
    if (result == 0) return DLUMA_XUI_ELEMENT_NOT_FOUND;
    if (result == -1) return DLUMA_XUI_ELEMENT_AMBIGUOUS;
    if (result == -3) return DLUMA_XUI_ELEMENT_WRONG_TYPE;
    if (result == -4) return DLUMA_XUI_ELEMENT_UNCERTAIN;
    return DLUMA_XUI_ELEMENT_UNAVAILABLE;
}
