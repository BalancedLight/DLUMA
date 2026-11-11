#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/game/dlw_xui_element_core.h"

typedef struct Fixture { int result, calls; float position[3]; } Fixture;
static int set_visual(void* user, const char* screen, const char* target,
                      DlumaXuiVisualProperty property, const float* values)
{
    Fixture* f = (Fixture*)user;
    (void)screen; (void)target; (void)values;
    ++f->calls;
    return property == DLUMA_XUI_VISUAL_COLOR ? -3 : f->result;
}
static int named(void* user, const char* screen, const char* target,
                 DlumaXuiNamedAction action, const char* value)
{
    Fixture* f = (Fixture*)user;
    (void)screen; (void)target; ++f->calls;
    if(action==DLUMA_XUI_TEXT_KEY && strcmp(value,"&MY_KEY&")) return -2;
    return f->result;
}
static int set_position(void* user, const char* screen, const char* target,
                        const float position[3])
{
    Fixture* f = (Fixture*)user;
    ++f->calls;
    if (strcmp(screen, "HUD.xui") || strcmp(target, "Panel/Item")) return 0;
    if (f->result == 1) memcpy(f->position, position, sizeof(f->position));
    return f->result;
}
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)
int main(void)
{
    Fixture f = {1, 0, {0, 0, 0}};
    DlumaXuiElementPositionBackend backend = {&f, set_position};
    float position[3] = {2.5f, -3.0f, 4.0f};
    CHECK(dluma_xui_element_set_position(&backend, "HUD.xui", "Panel/Item", position) == DLUMA_XUI_ELEMENT_OK);
    CHECK(f.calls == 1 && f.position[0] == 2.5f && f.position[1] == -3.0f);
    f.result = -1;
    CHECK(dluma_xui_element_set_position(&backend, "HUD.xui", "Panel/Item", position) == DLUMA_XUI_ELEMENT_AMBIGUOUS);
    f.result = 0;
    CHECK(dluma_xui_element_set_position(&backend, "HUD.xui", "Panel/Item", position) == DLUMA_XUI_ELEMENT_NOT_FOUND);
    f.result = -4;
    CHECK(dluma_xui_element_set_position(&backend, "HUD.xui", "Panel/Item", position) == DLUMA_XUI_ELEMENT_UNCERTAIN);
    f.result = -2;
    CHECK(dluma_xui_element_set_position(&backend, "HUD.xui", "Panel/Item", position) == DLUMA_XUI_ELEMENT_UNAVAILABLE);
    f.result = -3;
    CHECK(dluma_xui_element_set_position(&backend, "HUD.xui", "Panel/Item", position) == DLUMA_XUI_ELEMENT_WRONG_TYPE);
    position[0] = NAN;
    CHECK(dluma_xui_element_set_position(&backend, "HUD.xui", "Panel/Item", position) == DLUMA_XUI_ELEMENT_INVALID);
    CHECK(f.calls == 6);
    {
        DlumaXuiElementVisualBackend visual = {&f, set_visual};
        float values[4] = {0.5f, 0.2f, 0.3f, 1.0f};
        f.result = 1;
        CHECK(dluma_xui_element_set_visual(&visual,"HUD.xui","Panel/Item",DLUMA_XUI_VISUAL_OPACITY,values) == DLUMA_XUI_ELEMENT_OK);
        f.result = -4;
        CHECK(dluma_xui_element_set_visual(&visual,"HUD.xui","Panel/Item",DLUMA_XUI_VISUAL_OPACITY,values) == DLUMA_XUI_ELEMENT_UNCERTAIN);
        CHECK(dluma_xui_element_set_visual(&visual,"HUD.xui","Panel/Item",DLUMA_XUI_VISUAL_COLOR,values) == DLUMA_XUI_ELEMENT_WRONG_TYPE);
        values[3] = INFINITY;
        CHECK(dluma_xui_element_set_visual(&visual,"HUD.xui","Panel/Item",DLUMA_XUI_VISUAL_COLOR,values) == DLUMA_XUI_ELEMENT_INVALID);
    }
    {
        DlumaXuiElementNamedBackend backend_named={&f,named};
        f.result=1;
        CHECK(dluma_xui_element_apply_named(&backend_named,"HUD.xui","Panel",DLUMA_XUI_TEXT_KEY,"MY_KEY")==DLUMA_XUI_ELEMENT_OK);
        CHECK(dluma_xui_element_apply_named(&backend_named,"HUD.xui","Panel",DLUMA_XUI_TEXT_KEY,"BAD&KEY")==DLUMA_XUI_ELEMENT_INVALID);
        f.result=-3;
        CHECK(dluma_xui_element_apply_named(&backend_named,"HUD.xui","Panel",DLUMA_XUI_ANIMATION,"Shake")==DLUMA_XUI_ELEMENT_WRONG_TYPE);
        f.result=-4;
        CHECK(dluma_xui_element_apply_named(&backend_named,"HUD.xui","Panel",DLUMA_XUI_ANIMATION,"Shake")==DLUMA_XUI_ELEMENT_UNCERTAIN);
        CHECK(dluma_xui_element_apply_named(&backend_named,"HUD.xui","Panel",DLUMA_XUI_ANIMATION,"Shak\xC3\xA9")==DLUMA_XUI_ELEMENT_INVALID);
    }
    puts("xui element core passed");
    return 0;
}
