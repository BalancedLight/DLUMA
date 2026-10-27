#include "dlw_common.h"

/* This translation unit is intentionally built without /GS: it initializes
 * the process cookie before any instrumented loader function can run. */
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_dluma.self_module = instance;
        dluma_security_init_cookie();
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        dluma_atomic_exchange(&g_dluma.stop_requested, 1);
    }
    return TRUE;
}
