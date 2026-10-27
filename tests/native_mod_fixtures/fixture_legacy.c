#include <windows.h>

static unsigned int g_initialize_calls;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}

/* This fixture deliberately has only the stock game-script ABI.  The native
 * chain harness reads its counter but must never invoke this initializer. */
__declspec(dllexport) void *__cdecl InitializeGameScriptDLL(void)
{
    ++g_initialize_calls;
    return (void *)1;
}

__declspec(dllexport) void __cdecl ShutdownGameScriptDLL(void)
{
}

__declspec(dllexport) unsigned int __cdecl FixtureLegacyGetInitializeCount(void)
{
    return g_initialize_calls;
}
