/* Match the loader's no-CRT link boundary while exercising the real core. */
#include "../src/game/dlw_xui_element_core.h"
#include <float.h>
#include <stdint.h>

__declspec(dllimport) void __stdcall ExitProcess(unsigned int code);
/* The native loader supplies this floating-point linker marker itself. */
int _fltused = 0;
static unsigned int calls;
static int set_position(void* user, const char* screen, const char* target,
                        const float position[3])
{
    (void)user; (void)screen; (void)target; (void)position;
    ++calls;
    return 1;
}

void entry(void)
{
    const DlumaXuiElementPositionBackend backend = { 0, set_position };
    float position[3] = { FLT_MAX, -FLT_MAX, 0.0f };
    union { uint32_t bits; float value; } special;
    if (dluma_xui_element_set_position(&backend, "HUD.xui", "Marker", position) !=
            DLUMA_XUI_ELEMENT_OK || calls != 1) ExitProcess(1);
    special.bits = 0x7fc00000U;
    position[0] = special.value;
    if (dluma_xui_element_set_position(&backend, "HUD.xui", "Marker", position) !=
            DLUMA_XUI_ELEMENT_INVALID || calls != 1) ExitProcess(2);
    special.bits = 0x7f800000U;
    position[0] = special.value;
    if (dluma_xui_element_set_position(&backend, "HUD.xui", "Marker", position) !=
            DLUMA_XUI_ELEMENT_INVALID || calls != 1) ExitProcess(3);
    special.bits = 0xff800000U;
    position[0] = special.value;
    if (dluma_xui_element_set_position(&backend, "HUD.xui", "Marker", position) !=
            DLUMA_XUI_ELEMENT_INVALID || calls != 1) ExitProcess(4);
    ExitProcess(0);
}
